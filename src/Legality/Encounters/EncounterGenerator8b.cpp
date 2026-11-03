#include "Legality/Encounters/EncounterGenerator8b.h"

#include "Legality/Encounters/EncounterBreeding.h"
#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterEnumerator8b.h"
#include "Legality/Encounters/EncounterRules8.h"
#include "Legality/Encounters/EncounterUtil.h"
#include "Legality/Evolutions/EvolutionTree.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            constexpr uint16_t SPECIES_CASTFORM = 351;
            constexpr uint16_t SPECIES_ROTOM = 479;

            // PKHeX EncounterGenerator8b.CreateEggEncounter
            EncounterEgg8b createEggEncounter(uint16_t speciesId, uint8_t formId, Enums::GameVersion version)
            {
                if (FormInfo::isBattleOnlyForm(speciesId, formId) || speciesId == SPECIES_ROTOM || speciesId == SPECIES_CASTFORM)
                {
                    formId = FormInfo::getOutOfBattleForm(speciesId, formId, 8);
                }
                return EncounterEgg8b(speciesId, formId, version);
            }
        }

        EncounterCandidates EncounterGenerator8b::getEncounters(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain, LegalityInfo &info)
        {
            (void)info;
            const Enums::GameVersion version = static_cast<Enums::GameVersion>(getOriginVersion(pokemon));
            for (const MatchedEncounter &matched : EncounterEnumerator8b::enumerate(pokemon, chain, version))
            {
                co_yield matched.encounter;
            }
        }

        EncounterCandidates EncounterGenerator8b::getEncountersSwordShield(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain, Enums::GameVersion version)
        {
            if (!isFormatSwordShield(pokemon))
            {
                co_return;
            }
            for (const MatchedEncounter &matched : EncounterEnumerator8b::enumerateSwordShield(pokemon, chain, version))
            {
                co_yield matched.encounter;
            }
        }

        bool EncounterGenerator8b::tryGetEgg(std::span<const Evolutions::EvolutionCriteria> chain, Enums::GameVersion version, EncounterEgg8b &result)
        {
            if (chain.empty())
            {
                return false;
            }
            const Evolutions::EvolutionCriteria &devolved = chain.back();
            if (!devolved.isInsideLevelRange(EncounterEgg8b::LEVEL))
            {
                return false;
            }
            const Evolutions::EvolutionTree *tree = Evolutions::getEvolutionTree(Enums::GameVersion::BDSP);
            if (tree == nullptr)
            {
                return false;
            }
            // PKHeX GetBaby: the most devolved stage must be the egg's species, or a split-breed.
            const Evolutions::SpeciesForm baby = tree->getBaseSpeciesForm(devolved.speciesId, devolved.formId);
            if (baby.speciesId != devolved.speciesId && !isSplitBreedNotBabySpecies4(devolved.speciesId))
            {
                return false;
            }
            if (!canHatchAsEgg(baby.speciesId))
            {
                return false;
            }
            if (!canHatchAsEgg(baby.speciesId, baby.formId, Enums::GameVersion::BDSP))
            {
                return false;
            }
            if (!tree->isPresentInGame(baby.speciesId, baby.formId))
            {
                return false;
            }
            result = createEggEncounter(baby.speciesId, baby.formId, version);
            return true;
        }

        bool EncounterGenerator8b::tryGetSplit(const EncounterEgg8b &other, std::span<const Evolutions::EvolutionCriteria> chain, EncounterEgg8b &result)
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
