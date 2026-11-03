#include "Legality/Encounters/EncounterGenerator12.h"

#include <vector>

#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterGenerator1.h"
#include "Legality/Encounters/EncounterGenerator2.h"
#include "Legality/Encounters/EncounterTrade1.h"
#include "Legality/Encounters/EncounterTrade2.h"
#include "Legality/Encounters/GBRestrictions.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            /// PKHeX GBEncounterPriority: which generation's candidate is offered first.
            enum class GameBoyEncounterPriority : uint8_t
            {
                Least,
                EggEncounter,
                WildEncounter,
                StaticEncounter,
                TradeEncounterG1,
                TradeEncounterG2,
            };

            GameBoyEncounterPriority getGameBoyEncounterPriority(const Pokemon::Pokemon &pokemon, const Encounterable &encounter)
            {
                switch (encounter.templateType())
                {
                case EncounterTemplateType::Trade1:
                    if (encounterAs<EncounterTrade1>(encounter)->getMatchRating(pokemon) != EncounterMatchRating::Match)
                    {
                        return GameBoyEncounterPriority::Least;
                    }
                    return GameBoyEncounterPriority::TradeEncounterG1;
                case EncounterTemplateType::Trade2:
                    return GameBoyEncounterPriority::TradeEncounterG2;
                case EncounterTemplateType::Slot1:
                case EncounterTemplateType::Slot2:
                    return GameBoyEncounterPriority::WildEncounter;
                case EncounterTemplateType::Egg2:
                    return GameBoyEncounterPriority::EggEncounter;
                default:
                    return GameBoyEncounterPriority::StaticEncounter;
                }
            }

            /// PKHeX GenerateFilteredEncounters12BothKorean: a Korean record can only come from Gold or
            /// Silver, but Crystal and Gen 1 matches are offered after them for the later checks to flag.
            EncounterCandidates getEncountersBothKorean(const Pokemon::Pokemon &pokemon, const LegalityInfo &info)
            {
                std::vector<EncounterReference> deferred;
                for (const EncounterReference &encounter : EncounterGenerator2::getEncounters(pokemon, info))
                {
                    if (encounter->version() == Enums::GameVersion::C)
                    {
                        deferred.push_back(encounter);
                    }
                    else
                    {
                        co_yield encounter;
                    }
                }
                for (const EncounterReference &encounter : deferred)
                {
                    co_yield encounter;
                }
                for (const EncounterReference &encounter : EncounterGenerator1::getEncounters(pokemon, info))
                {
                    co_yield encounter;
                }
            }

            /// PKHeX GenerateFilteredEncounters12Both: both generations at once, taking from whichever
            /// offers the higher priority candidate next (Gen 2 on a tie).
            EncounterCandidates getEncountersBoth(const Pokemon::Pokemon &pokemon, const LegalityInfo &info)
            {
                PeekEnumerator generationOne(EncounterGenerator1::getEncounters(pokemon, info));
                PeekEnumerator generationTwo(EncounterGenerator2::getEncounters(pokemon, info));
                while (generationTwo.peekIsNext() || generationOne.peekIsNext())
                {
                    PeekEnumerator *preferred = &generationTwo;
                    if (!generationTwo.peekIsNext())
                    {
                        preferred = &generationOne;
                    }
                    else if (generationOne.peekIsNext())
                    {
                        const GameBoyEncounterPriority priorityOne = getGameBoyEncounterPriority(pokemon, *generationOne.current());
                        const GameBoyEncounterPriority priorityTwo = getGameBoyEncounterPriority(pokemon, *generationTwo.current());
                        if (priorityOne > priorityTwo)
                        {
                            preferred = &generationOne;
                        }
                    }
                    const EncounterReference encounter = preferred->current();
                    preferred->moveNext();
                    co_yield encounter;
                }
            }

            EncounterCandidates getEncountersByOrigin(const Pokemon::Pokemon &pokemon, const LegalityInfo &info)
            {
                // Crystal's met data, or a female trainer (only Crystal had one), means Crystal alone.
                const bool isCrystal = GameBoyEntity::getCaughtData(pokemon) != 0 || (getFormatGeneration(pokemon) >= 7 && pokemon.otGender() == 1);
                if (isCrystal)
                {
                    return EncounterGenerator2::getEncounters(pokemon, info);
                }
                switch (GBRestrictions::getTradebackStatusInitial(pokemon))
                {
                case PotentialGBOrigin::Gen1Only:
                    return EncounterGenerator1::getEncounters(pokemon, info);
                case PotentialGBOrigin::Gen2Only:
                    return EncounterGenerator2::getEncounters(pokemon, info);
                case PotentialGBOrigin::Either:
                    break;
                }
                if (GameBoyEntity::isKorean(pokemon))
                {
                    return getEncountersBothKorean(pokemon, info);
                }
                return getEncountersBoth(pokemon, info);
            }
        }

        EncounterCandidates EncounterGenerator12::getEncounters(const Pokemon::Pokemon &pokemon, LegalityInfo &info)
        {
            for (const EncounterReference &encounter : getEncountersByOrigin(pokemon, info))
            {
                info.generation = encounter->generation();
                co_yield encounter;
            }
        }
    }
}
