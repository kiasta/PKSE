#include "Legality/Encounters/EncounterGenerator6.h"

#include "Legality/Encounters/EncounterBreeding.h"
#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterEnumerator6.h"
#include "Legality/Encounters/EncounterUtil.h"
#include "Legality/Evolutions/EvolutionChainSearch.h"
#include "Legality/Evolutions/EvolutionTree.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            constexpr uint8_t GENERATION = 6;
        }

        EncounterCandidates EncounterGenerator6::getEncounters(const Pokemon::Pokemon &pokemon, LegalityInfo &info)
        {
            const Evolutions::EvolutionChain chain = Evolutions::getEncounterOriginChain(info.entity, GENERATION, Enums::GameVersion::XY);
            const Enums::GameVersion version = static_cast<Enums::GameVersion>(getOriginVersion(pokemon));
            for (const MatchedEncounter &matched : EncounterEnumerator6::enumerate(pokemon, chain, version))
            {
                co_yield matched.encounter;
            }
        }

        bool EncounterGenerator6::tryGetEgg(std::span<const Evolutions::EvolutionCriteria> chain, Enums::GameVersion version, EncounterEgg6 &result)
        {
            if (chain.empty())
            {
                return false;
            }
            const Evolutions::EvolutionCriteria &devolved = chain.back();
            if (!devolved.isInsideLevelRange(EncounterEgg6::LEVEL))
            {
                return false;
            }

            // The most devolved stage must be the egg species, unless it is a split-breed parent.
            const Evolutions::EvolutionTree *tree = Evolutions::getEvolutionTree(Enums::GameVersion::XY);
            if (tree == nullptr)
            {
                return false;
            }
            const Evolutions::SpeciesForm baby = tree->getBaseSpeciesForm(devolved.speciesId, devolved.formId);
            if (baby.speciesId != devolved.speciesId && !isSplitBreedNotBabySpecies4(devolved.speciesId))
            {
                return false;
            }
            if (!canHatchAsEgg(baby.speciesId))
            {
                return false;
            }
            if (!canHatchAsEgg(baby.speciesId, baby.formId, Enums::GameVersion::XY))
            {
                return false;
            }
            if (!tree->isPresentInGame(baby.speciesId, baby.formId))
            {
                return false;
            }
            result = EncounterEgg6(baby.speciesId, FormInfo::getEggForm(baby.speciesId, baby.formId, GENERATION), version);
            return true;
        }

        EncounterEgg6 EncounterGenerator6::mutateEggTrade(const EncounterEgg6 &egg) noexcept
        {
            // 24 <-> 26 (X, Alpha Sapphire) and 25 <-> 27 (Y, Omega Ruby).
            return egg.withVersion(static_cast<Enums::GameVersion>(static_cast<uint8_t>(egg.version()) ^ 2));
        }

        bool EncounterGenerator6::tryGetSplit(const EncounterEgg6 &other, std::span<const Evolutions::EvolutionCriteria> chain, EncounterEgg6 &result)
        {
            if (chain.empty())
            {
                return false;
            }
            const Evolutions::EvolutionCriteria *devolved = &chain.back();
            if (other.speciesId() == devolved->speciesId)
            {
                if (chain.size() < 2)
                {
                    return false;
                }
                devolved = &chain[chain.size() - 2];
            }
            if (!isSplitBreedNotBabySpecies4(devolved->speciesId))
            {
                return false;
            }
            result = other.withSpeciesForm(devolved->speciesId, devolved->formId);
            return true;
        }

    }
}
