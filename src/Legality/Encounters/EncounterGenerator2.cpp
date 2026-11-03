#include "Legality/Encounters/EncounterGenerator2.h"

#include "Legality/Encounters/EncounterBreeding.h"
#include "Legality/Encounters/EncounterEnumerator2.h"
#include "Legality/Encounters/GBRestrictions.h"
#include "Legality/Evolutions/EvolutionChainSearch.h"
#include "Legality/Evolutions/EvolutionTree.h"

namespace Legality
{
    namespace Encounters
    {
        EncounterCandidates EncounterGenerator2::getEncounters(const Pokemon::Pokemon &pokemon, const LegalityInfo &info)
        {
            const Evolutions::EvolutionChain chain = Evolutions::getEncounterOriginChainGameBoy(info.entity, 2, Enums::GameVersion::GSC);
            if (chain.isEmpty())
            {
                co_return;
            }
            for (const MatchedEncounter &matched : EncounterEnumerator2::enumerate(pokemon, chain, info))
            {
                co_yield matched.encounter;
            }
        }

        bool EncounterGenerator2::tryGetEgg(std::span<const Evolutions::EvolutionCriteria> chain, Enums::GameVersion version, EncounterEgg2 &result)
        {
            if (chain.empty())
            {
                return false;
            }
            const Evolutions::EvolutionCriteria &devolved = chain.back();
            if (!devolved.isInsideLevelRange(EncounterEgg2::LEVEL))
            {
                return false;
            }

            // The most devolved stage must be the egg species: Gen 2 has no split-breed exception.
            const Evolutions::EvolutionTree *tree = Evolutions::getEvolutionTree(Enums::GameVersion::GSC);
            if (tree == nullptr)
            {
                return false;
            }
            const Evolutions::SpeciesForm baby = tree->getBaseSpeciesForm(devolved.speciesId, devolved.formId);
            if (baby.speciesId != devolved.speciesId)
            {
                return false;
            }
            if (!canHatchAsEgg(baby.speciesId))
            {
                return false;
            }
            // Gen 2 has no breedable forms (Unown cannot breed) and nothing changes form.
            if (baby.formId != 0)
            {
                return false;
            }
            if (!tree->isPresentInGame(baby.speciesId, baby.formId))
            {
                return false;
            }
            result = EncounterEgg2(baby.speciesId, version);
            return true;
        }

        bool EncounterGenerator2::tryGetEggCrystal(const Pokemon::Pokemon &pokemon, const EncounterEgg2 &egg, EncounterEgg2 &crystal)
        {
            // PKHeX ParseSettings.AllowGen2Crystal.
            if (GameBoyEntity::isKorean(pokemon))
            {
                return false;
            }
            crystal = EncounterEgg2(egg.speciesId(), Enums::GameVersion::C);
            return true;
        }
    }
}
