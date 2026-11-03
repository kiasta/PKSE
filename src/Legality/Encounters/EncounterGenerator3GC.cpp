#include "Legality/Encounters/EncounterGenerator3GC.h"
#include "Legality/Encounters/EncounterRules3GC.h"

#include <array>

#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterEnumerator3GC.h"
#include "Legality/Encounters/EncounterShadow3Colo.h"
#include "Legality/Encounters/EncounterShadow3XD.h"
#include "Legality/Encounters/EncounterSlot3XD.h"
#include "Legality/Evolutions/EvolutionChainSearch.h"
#include "Legality/RNG/LockFinder.h"
#include "Legality/RNG/MethodFinder.h"
#include "Legality/RNG/MethodPokeSpot.h"
#include "Legality/RNG/XDRNG.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            /// PKHeX GetIsShadowLockValidEReader: the e-Reader shadows have fixed IVs and no PID/IV
            /// correlation, so their seed comes from the PID alone.
            bool isShadowLockValidEReader(const Pokemon::Pokemon &pokemon, LegalityInfo &info, const EncounterShadow3Colo &shadow)
            {
                std::array<uint32_t, RNG::XDRNG::MAX_PID_SEED_COUNT> seeds{};
                const int seedCount = RNG::XDRNG::getSeeds(seeds, pokemon.encryptionConstant());
                for (int seedIndex = 0; seedIndex < seedCount; seedIndex++)
                {
                    const uint32_t origin = RNG::XDRNG::next4(seeds[static_cast<size_t>(seedIndex)]);
                    if (!RNG::LockFinder::isAllShadowLockValid(false, shadow.partyPrior(), origin, pokemon))
                    {
                        continue;
                    }
                    info.setPIDIV(RNG::PIDIV(RNG::PIDType::ColosseumXD, origin));
                    return true;
                }
                return false;
            }

            /// PKHeX GetIsShadowLockValid, for a shadow Pokemon.
            bool isShadowLockValid(const Pokemon::Pokemon &pokemon, LegalityInfo &info, const Encounterable &encounter)
            {
                if (const EncounterShadow3Colo *colosseum = encounterAs<EncounterShadow3Colo>(encounter))
                {
                    if (colosseum->isEReader())
                    {
                        return isShadowLockValidEReader(pokemon, info, *colosseum);
                    }
                    return RNG::LockFinder::isAllShadowLockValid(false, colosseum->partyPrior(), info.pidiv.originSeed, pokemon);
                }
                const EncounterShadow3XD *xd = encounterAs<EncounterShadow3XD>(encounter);
                return RNG::LockFinder::isAllShadowLockValid(true, xd->partyPrior(), info.pidiv.originSeed, pokemon);
            }

            /// PKHeX IsTypeCompatible: these games' correlations never overlap, so the answer is yes or no.
            bool isTypeCompatible(const Encounterable &encounter, const Pokemon::Pokemon &pokemon, RNG::PIDType type)
            {
                if (const RandomCorrelationTemplate *correlation = encounter.asRandomCorrelation())
                {
                    return correlation->isCompatible(type, pokemon) == RandomCorrelationRating::Match;
                }
                return type == RNG::PIDType::None;
            }
        }

        EncounterCandidates EncounterGenerator3GC::getEncounters(const Pokemon::Pokemon &pokemon, LegalityInfo &info)
        {
            const Evolutions::EvolutionChain chain = Evolutions::getEncounterOriginChain(info.entity, 3, Enums::GameVersion::RSE);
            return getEncounters(pokemon, chain, info);
        }

        EncounterCandidates EncounterGenerator3GC::getEncounters(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain, LegalityInfo &info)
        {
            if (chain.isEmpty())
            {
                co_return;
            }
            EncounterReference partial;
            info.setPIDIV(RNG::MethodFinder::analyze(pokemon));
            for (const MatchedEncounter &matched : EncounterEnumerator3GC::enumerate(pokemon, chain))
            {
                const Encounterable &encounter = *matched.encounter;
                if (const EncounterSlot3XD *slot = encounterAs<EncounterSlot3XD>(encounter))
                {
                    uint32_t PIDOrigin = 0;
                    uint32_t IVOrigin = 0;
                    if (RNG::MethodPokeSpot::tryGetOriginSeeds(pokemon, slot->slotNumber(), slot->levelMin(), slot->levelMax(), PIDOrigin, IVOrigin))
                    {
                        info.setPIDIV(RNG::PIDIV(RNG::PIDType::PokeSpot, PIDOrigin).asMutated(RNG::PIDType::PokeSpot, IVOrigin));
                    }
                }
                else if (isShadowEncounter(encounter))
                {
                    if (!isShadowLockValid(pokemon, info, encounter))
                    {
                        if (!partial)
                        {
                            partial = matched.encounter;
                        }
                        continue;
                    }
                }
                if (isTypeCompatible(encounter, pokemon, info.pidiv.type))
                {
                    co_yield matched.encounter;
                }
                else if (!partial)
                {
                    partial = matched.encounter;
                }
            }
            if (partial)
            {
                info.manualFlag = EncounterYieldFlag::InvalidPIDIV;
                co_yield partial;
            }
        }
    }
}
