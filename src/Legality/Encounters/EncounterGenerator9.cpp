#include "Legality/Encounters/EncounterGenerator9.h"

#include "Legality/Encounters/EncounterBreeding.h"
#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterEnumerator9.h"
#include "Legality/Encounters/EncounterGenerator8.h"
#include "Legality/Encounters/EncounterGenerator9a.h"
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
            constexpr uint16_t SPECIES_CASTFORM = 351;
            constexpr uint16_t SPECIES_ROTOM = 479;
            constexpr uint16_t SPECIES_DARMANITAN = 555;
            constexpr uint16_t SPECIES_GRENINJA = 658;
            constexpr uint16_t SPECIES_SCATTERBUG = 664;
            constexpr uint16_t SPECIES_FLOETTE = 670;
            constexpr uint16_t SPECIES_MEOWSTIC = 678;
            constexpr uint16_t SPECIES_ZYGARDE = 718;
            constexpr uint16_t SPECIES_MINIOR = 774;
            constexpr uint16_t SPECIES_MIMIKYU = 778;
            constexpr uint16_t SPECIES_MAGEARNA = 801;
            constexpr uint16_t SPECIES_TATSUGIRI = 978;
            constexpr uint16_t SPECIES_OGERPON = 1017;
            /// PKHeX Vivillon3DS.FancyFormID: a Scarlet/Violet Scatterbug egg always carries the Fancy pattern.
            constexpr uint8_t FORM_VIVILLON_FANCY = 18;

            /// PKHeX FormInfo.IsBattleOnlyForm.
            bool isBattleOnlyForm(uint16_t speciesId, uint8_t formId)
            {
                return Pokemon::isMegaForm(speciesId, formId) || Pokemon::isBattleForm(speciesId, formId);
            }

            /// PKHeX FormInfo.GetOutOfBattleForm for format 9.
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

            // PKHeX EncounterGenerator9.CreateEggEncounter
            EncounterEgg9 createEggEncounter(uint16_t speciesId, uint8_t formId, Enums::GameVersion version)
            {
                if (speciesId == SPECIES_SCATTERBUG)
                {
                    formId = FORM_VIVILLON_FANCY;
                }
                else if (isBattleOnlyForm(speciesId, formId) || speciesId == SPECIES_ROTOM || speciesId == SPECIES_CASTFORM)
                {
                    formId = getOutOfBattleForm(speciesId, formId);
                }
                return EncounterEgg9(speciesId, formId, version);
            }
        }

        EncounterCandidates EncounterGenerator9::getEncounters(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain, LegalityInfo &info)
        {
            (void)info;
            const Enums::GameVersion version = static_cast<Enums::GameVersion>(getOriginVersion(pokemon));
            for (const MatchedEncounter &matched : EncounterEnumerator9::enumerate(pokemon, chain, version))
            {
                co_yield matched.encounter;
            }
        }

        EncounterCandidates EncounterGenerator9::getEncountersSWSH(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain, Enums::GameVersion version)
        {
            if (!isFormatSwordShield(pokemon))
            {
                co_return;
            }
            for (const MatchedEncounter &matched : EncounterEnumerator9::enumerateSWSH(pokemon, chain, version))
            {
                co_yield matched.encounter;
            }
        }

        bool EncounterGenerator9::tryGetEgg(const Pokemon::Pokemon &pokemon, std::span<const Evolutions::EvolutionCriteria> chain, Enums::GameVersion version, EncounterEgg9 &result)
        {
            if (static_cast<uint8_t>(version) == 0 && pokemon.isEgg())
            {
                version = Enums::GameVersion::SL;
            }
            return tryGetEgg(chain, version, result);
        }

        bool EncounterGenerator9::tryGetEgg(std::span<const Evolutions::EvolutionCriteria> chain, Enums::GameVersion version, EncounterEgg9 &result)
        {
            if (chain.empty())
            {
                return false;
            }
            const Evolutions::EvolutionCriteria &devolved = chain.back();
            if (!devolved.isInsideLevelRange(EncounterEgg9::LEVEL))
            {
                return false;
            }

            // PKHeX GetBaby: the lowest stage must be its own Scarlet/Violet hatch species; Gen 9 has
            // no split-breed.
            uint16_t hatchSpeciesId = 0;
            uint8_t hatchFormId = 0;
            LearnSource::getHatchSpeciesForm(LearnSource::LearnEnvironment::SV, devolved.speciesId, devolved.formId, hatchSpeciesId, hatchFormId);
            if (hatchSpeciesId != devolved.speciesId)
            {
                return false;
            }
            const uint16_t speciesId = devolved.speciesId;
            const uint8_t formId = devolved.formId;
            if (!canHatchAsEgg(speciesId))
            {
                return false;
            }
            if (!canHatchAsEgg(speciesId, formId, Enums::GameVersion::SV))
            {
                return false;
            }
            const Evolutions::EvolutionTree *tree = Evolutions::getEvolutionTree(Enums::GameVersion::SV);
            if (tree == nullptr || !tree->isPresentInGame(speciesId, formId))
            {
                return false;
            }
            result = createEggEncounter(speciesId, formId, version);
            return true;
        }

        EncounterCandidates EncounterGenerator9X::getEncounters(const Pokemon::Pokemon &pokemon, LegalityInfo &info)
        {
            const Evolutions::EvolutionChain chain = Evolutions::getEncounterOriginChain(info.entity, 9, getEntityContextGroup(pokemon));
            if (chain.isEmpty())
            {
                return EncounterCandidates();
            }
            const uint8_t originVersion = getOriginVersion(pokemon);
            const bool isScarletOrViolet = originVersion == static_cast<uint8_t>(Enums::GameVersion::SL) || originVersion == static_cast<uint8_t>(Enums::GameVersion::VL);
            if (isScarletOrViolet || (originVersion == 0 && pokemon.isEgg()))
            {
                return EncounterGenerator9::getEncounters(pokemon, chain, info);
            }
            if (originVersion == static_cast<uint8_t>(Enums::GameVersion::ZA))
            {
                return EncounterGenerator9a::getEncounters(pokemon, chain, info);
            }
            // PKHeX also routes Sword/Shield here, which a generation-9 origin never carries.
            return EncounterCandidates();
        }

    }
}
