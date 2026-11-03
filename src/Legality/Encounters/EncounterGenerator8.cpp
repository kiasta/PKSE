#include "Legality/Encounters/EncounterGenerator8.h"

#include "Legality/Encounters/EncounterBreeding.h"
#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterEnumerator8.h"
#include "Legality/Encounters/EncounterGenerator3.h"
#include "Legality/Encounters/EncounterGenerator8a.h"
#include "Legality/Encounters/EncounterGenerator8b.h"
#include "Legality/Encounters/EncounterGenerator9.h"
#include "Legality/Encounters/EncounterGeneratorGO.h"
#include "Legality/Encounters/EncounterLocations8.h"
#include "Legality/Encounters/EncounterRules8.h"
#include "Legality/Evolutions/EvolutionChainSearch.h"
#include "Legality/Evolutions/EvolutionTree.h"
#include "Legality/LearnSource/LearnSources.h"
#include "Pokemon/FormInfo.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            /// PKHeX GameVersion ids EncounterGenerator8X routes on that have no Enums::GameVersion member.
            constexpr uint8_t VERSION_GO = 34;
            constexpr uint8_t VERSION_FIRERED_SWITCH = 56;
            constexpr uint8_t VERSION_LEAFGREEN_SWITCH = 57;

            constexpr uint16_t SPECIES_CASTFORM = 351;
            constexpr uint16_t SPECIES_ROTOM = 479;
            constexpr uint16_t SPECIES_DARMANITAN = 555;
            constexpr uint16_t SPECIES_GRENINJA = 658;
            constexpr uint16_t SPECIES_ZYGARDE = 718;
            constexpr uint16_t SPECIES_MINIOR = 774;
            constexpr uint16_t SPECIES_MIMIKYU = 778;
            constexpr uint16_t SPECIES_FLOETTE = 670;
            constexpr uint16_t SPECIES_MEOWSTIC = 678;
            constexpr uint16_t SPECIES_MAGEARNA = 801;
            constexpr uint16_t SPECIES_OGERPON = 1017;
            constexpr uint16_t SPECIES_TATSUGIRI = 978;

            /// PKHeX FormInfo.IsBattleOnlyForm.
            bool isBattleOnlyForm(uint16_t speciesId, uint8_t formId)
            {
                return Pokemon::isMegaForm(speciesId, formId) || Pokemon::isBattleForm(speciesId, formId);
            }

            /// PKHeX FormInfo.GetOutOfBattleForm for format 8.
            uint8_t getOutOfBattleForm(uint16_t speciesId, uint8_t formId)
            {
                switch (speciesId)
                {
                case SPECIES_DARMANITAN:
                    return formId & 2;
                case SPECIES_GRENINJA:
                    return formId == 2 ? 1 : 0;
                case SPECIES_ZYGARDE:
                    return 3;
                case SPECIES_MINIOR:
                    return formId + 7;
                case SPECIES_MIMIKYU:
                    return formId & 2;
                case SPECIES_OGERPON:
                    return formId & 3;
                case SPECIES_FLOETTE:
                    return 5;
                case SPECIES_TATSUGIRI:
                    return formId - 3;
                case SPECIES_MAGEARNA:
                case SPECIES_MEOWSTIC:
                    return formId - 2;
                default:
                    return 0;
                }
            }

            // PKHeX EncounterGenerator8.CreateEggEncounter
            EncounterEgg8 createEggEncounter(uint16_t speciesId, uint8_t formId, Enums::GameVersion version)
            {
                if (isBattleOnlyForm(speciesId, formId) || speciesId == SPECIES_ROTOM || speciesId == SPECIES_CASTFORM)
                {
                    formId = getOutOfBattleForm(speciesId, formId);
                }
                return EncounterEgg8(speciesId, formId, version);
            }
        }

        EncounterCandidates EncounterGenerator8::getEncounters(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain, LegalityInfo &info)
        {
            (void)info;
            const Enums::GameVersion version = static_cast<Enums::GameVersion>(getOriginVersion(pokemon));
            for (const MatchedEncounter &matched : EncounterEnumerator8::enumerate(pokemon, chain, version))
            {
                co_yield matched.encounter;
            }
        }

        bool EncounterGenerator8::tryGetEgg(std::span<const Evolutions::EvolutionCriteria> chain, Enums::GameVersion version, EncounterEgg8 &result)
        {
            if (chain.empty())
            {
                return false;
            }
            const Evolutions::EvolutionCriteria &devolved = chain.back();
            if (!devolved.isInsideLevelRange(EncounterEgg8::LEVEL))
            {
                return false;
            }

            // PKHeX GetBaby: the lowest stage must be its own Sword/Shield hatch species.
            uint16_t hatchSpeciesId = 0;
            uint8_t hatchFormId = 0;
            LearnSource::getHatchSpeciesForm(LearnSource::LearnEnvironment::SWSH, devolved.speciesId, devolved.formId, hatchSpeciesId, hatchFormId);
            uint16_t speciesId = 0;
            uint8_t formId = 0;
            if (hatchSpeciesId == devolved.speciesId)
            {
                speciesId = devolved.speciesId;
                formId = devolved.formId;
            }
            if (speciesId != devolved.speciesId && !isSplitBreedNotBabySpecies4(devolved.speciesId))
            {
                return false;
            }
            if (!canHatchAsEgg(speciesId))
            {
                return false;
            }
            if (!canHatchAsEgg(speciesId, formId, Enums::GameVersion::SWSH))
            {
                return false;
            }
            const Evolutions::EvolutionTree *tree = Evolutions::getEvolutionTree(Enums::GameVersion::SWSH);
            if (tree == nullptr || !tree->isPresentInGame(speciesId, formId))
            {
                return false;
            }
            result = createEggEncounter(speciesId, formId, version);
            return true;
        }

        bool EncounterGenerator8::tryGetSplit(const EncounterEgg8 &other, std::span<const Evolutions::EvolutionCriteria> chain, EncounterEgg8 &result)
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

        EncounterCandidates EncounterGenerator8X::getEncounters(const Pokemon::Pokemon &pokemon, LegalityInfo &info)
        {
            const uint8_t originVersion = getOriginVersion(pokemon);
            Enums::GameVersion contextGroup = Enums::GameVersion::SWSH;
            if (originVersion == static_cast<uint8_t>(Enums::GameVersion::PLA))
            {
                contextGroup = Enums::GameVersion::PLA;
            }
            else if (originVersion == static_cast<uint8_t>(Enums::GameVersion::BD) || originVersion == static_cast<uint8_t>(Enums::GameVersion::SP))
            {
                contextGroup = Enums::GameVersion::BDSP;
            }
            const Evolutions::EvolutionChain chain = Evolutions::getEncounterOriginChain(info.entity, 8, contextGroup);

            const bool isFormatPK8 = isFormatSwordShield(pokemon);
            const uint16_t metLocation = pokemon.metLocation();
            if (originVersion == static_cast<uint8_t>(Enums::GameVersion::SW))
            {
                switch (metLocation)
                {
                case LOCATION_HOME_SWLA:
                    return EncounterGenerator8a::getEncounters(pokemon, chain, info);
                case LOCATION_HOME_SWBD:
                    return EncounterGenerator8b::getEncountersSwordShield(pokemon, chain, Enums::GameVersion::BD);
                case LOCATION_HOME_SWSL:
                    return EncounterGenerator9::getEncountersSWSH(pokemon, chain, Enums::GameVersion::SL);
                case LOCATION_HOME_SWFR:
                    if (isFormatPK8)
                    {
                        return EncounterGenerator3::getEncounters(pokemon, chain, info, static_cast<Enums::GameVersion>(VERSION_FIRERED_SWITCH));
                    }
                    break;
                default:
                    break;
                }
                return EncounterGenerator8::getEncounters(pokemon, chain, info);
            }
            if (originVersion == static_cast<uint8_t>(Enums::GameVersion::SH))
            {
                switch (metLocation)
                {
                case LOCATION_HOME_SHSP:
                    return EncounterGenerator8b::getEncountersSwordShield(pokemon, chain, Enums::GameVersion::SP);
                case LOCATION_HOME_SHVL:
                    return EncounterGenerator9::getEncountersSWSH(pokemon, chain, Enums::GameVersion::VL);
                case LOCATION_HOME_SHLG:
                    if (isFormatPK8)
                    {
                        return EncounterGenerator3::getEncounters(pokemon, chain, info, static_cast<Enums::GameVersion>(VERSION_LEAFGREEN_SWITCH));
                    }
                    break;
                default:
                    break;
                }
                return EncounterGenerator8::getEncounters(pokemon, chain, info);
            }
            if (originVersion == static_cast<uint8_t>(Enums::GameVersion::PLA) && !isFormatPK8)
            {
                return EncounterGenerator8a::getEncounters(pokemon, chain, info);
            }
            const bool isBrilliantDiamondShiningPearl = originVersion == static_cast<uint8_t>(Enums::GameVersion::BD) || originVersion == static_cast<uint8_t>(Enums::GameVersion::SP);
            if (isBrilliantDiamondShiningPearl && !isFormatPK8)
            {
                return EncounterGenerator8b::getEncounters(pokemon, chain, info);
            }
            if (originVersion == VERSION_GO)
            {
                return EncounterGeneratorGO::getEncounters(pokemon, chain);
            }
            // A PK8 claiming a BDSP or Legends: Arceus version yields nothing in PKHeX too.
            return EncounterCandidates();
        }

    }
}
