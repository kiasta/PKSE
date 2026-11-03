#include "Legality/Encounters/EncounterGenerator7.h"

#include "Legality/Encounters/EncounterBreeding.h"
#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterEnumerator7.h"
#include "Legality/Encounters/EncounterEnumerator7GG.h"
#include "Legality/Encounters/EncounterGeneratorGO.h"
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
            /// PKHeX GameVersion.GO: Pokemon GO's stored version id, which PKSE has no member for.
            constexpr uint8_t VERSION_GO = 34;
            constexpr uint8_t GENERATION = 7;

            bool isGen7Version(uint8_t version) noexcept
            {
                return version >= static_cast<uint8_t>(Enums::GameVersion::SN) && version <= static_cast<uint8_t>(Enums::GameVersion::UM);
            }
        }

        EncounterCandidates EncounterGenerator7::getEncounters(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain, LegalityInfo &info)
        {
            (void)info;
            const Enums::GameVersion version = static_cast<Enums::GameVersion>(getOriginVersion(pokemon));
            for (const MatchedEncounter &matched : EncounterEnumerator7::enumerate(pokemon, chain, version))
            {
                co_yield matched.encounter;
            }
        }

        bool EncounterGenerator7::tryGetEgg(std::span<const Evolutions::EvolutionCriteria> chain, Enums::GameVersion version, EncounterEgg7 &result)
        {
            if (chain.empty())
            {
                return false;
            }
            const Evolutions::EvolutionCriteria &devolved = chain.back();
            if (!devolved.isInsideLevelRange(EncounterEgg7::LEVEL))
            {
                return false;
            }

            // The most devolved stage must be the egg species, unless it is a split-breed parent.
            const Evolutions::EvolutionTree *tree = Evolutions::getEvolutionTree(Enums::GameVersion::SM);
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
            if (!canHatchAsEgg(baby.speciesId, baby.formId, Enums::GameVersion::SM))
            {
                return false;
            }
            if (!tree->isPresentInGame(baby.speciesId, baby.formId))
            {
                return false;
            }
            result = EncounterEgg7(baby.speciesId, FormInfo::getEggForm(baby.speciesId, baby.formId, GENERATION), version);
            return true;
        }

        EncounterEgg7 EncounterGenerator7::mutateEggTrade(const EncounterEgg7 &egg) noexcept
        {
            // 30 <-> 32 (Sun, Ultra Sun) and 31 <-> 33 (Moon, Ultra Moon).
            return egg.withVersion(static_cast<Enums::GameVersion>(static_cast<uint8_t>(egg.version()) ^ 0b111110));
        }

        bool EncounterGenerator7::tryGetSplit(const EncounterEgg7 &other, std::span<const Evolutions::EvolutionCriteria> chain, EncounterEgg7 &result)
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

        EncounterCandidates EncounterGenerator7X::getEncounters(const Pokemon::Pokemon &pokemon, LegalityInfo &info)
        {
            const uint8_t version = getOriginVersion(pokemon);
            const Enums::GameVersion contextGroup = isGen7Version(version) ? Enums::GameVersion::SM : Enums::GameVersion::GG;
            const Evolutions::EvolutionChain chain = Evolutions::getEncounterOriginChain(info.entity, GENERATION, contextGroup);
            if (version == VERSION_GO)
            {
                return EncounterGeneratorGO::getEncountersLetsGo(pokemon, chain);
            }
            if (version > VERSION_GO)
            {
                return getEncountersLetsGo(pokemon, chain);
            }
            return EncounterGenerator7::getEncounters(pokemon, chain, info);
        }

        // PKHeX EncounterGenerator7GG.GetEncounters
        EncounterCandidates EncounterGenerator7X::getEncountersLetsGo(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain)
        {
            const Enums::GameVersion version = static_cast<Enums::GameVersion>(getOriginVersion(pokemon));
            for (const MatchedEncounter &matched : EncounterEnumerator7GG::enumerate(pokemon, chain, version))
            {
                co_yield matched.encounter;
            }
        }

    }
}
