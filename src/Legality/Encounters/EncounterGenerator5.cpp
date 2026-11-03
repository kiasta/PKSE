#include "Legality/Encounters/EncounterGenerator5.h"

#include "Legality/Encounters/EncounterBreeding.h"
#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterEnumerator5.h"
#include "Legality/Evolutions/EvolutionChainSearch.h"
#include "Legality/Evolutions/EvolutionTree.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        EncounterCandidates EncounterGenerator5::getEncounters(const Pokemon::Pokemon &pokemon, LegalityInfo &info)
        {
            const Evolutions::EvolutionChain chain = Evolutions::getEncounterOriginChain(info.entity, 5, Enums::GameVersion::BW);
            return getEncounters(pokemon, chain, info);
        }

        EncounterCandidates EncounterGenerator5::getEncounters(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain, LegalityInfo &info)
        {
            (void)info;
            if (chain.isEmpty())
            {
                co_return;
            }
            const Enums::GameVersion version = static_cast<Enums::GameVersion>(getOriginVersion(pokemon));
            for (const MatchedEncounter &matched : EncounterEnumerator5::enumerate(pokemon, chain, version))
            {
                co_yield matched.encounter;
            }
        }

        bool EncounterGenerator5::tryGetEgg(std::span<const Evolutions::EvolutionCriteria> chain, Enums::GameVersion version, EncounterEgg5 &result)
        {
            if (chain.empty())
            {
                return false;
            }
            const Evolutions::EvolutionCriteria &devolved = chain.back();
            if (!devolved.isInsideLevelRange(EncounterEgg5::LEVEL))
            {
                return false;
            }

            // The most devolved stage must be the egg species, unless it is a split-breed parent.
            const Evolutions::EvolutionTree *tree = Evolutions::getEvolutionTree(Enums::GameVersion::BW);
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
            if (!canHatchAsEgg(baby.speciesId, baby.formId, Enums::GameVersion::BW))
            {
                return false;
            }
            if (!tree->isPresentInGame(baby.speciesId, baby.formId))
            {
                return false;
            }
            result = EncounterEgg5(baby.speciesId, baby.formId, version);
            return true;
        }

        bool EncounterGenerator5::tryGetSplit(const EncounterEgg5 &other, std::span<const Evolutions::EvolutionCriteria> chain, EncounterEgg5 &result)
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
            result = other.withSpecies(devolved->speciesId);
            return true;
        }
    }
}
