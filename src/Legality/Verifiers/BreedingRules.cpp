// Port of PKHeX PKHeX.Core/Legality/Verifiers/Ability/AbilityBreedLegality.cs and the Species/Form of
// the EncounterEgg* templates.
#include "Legality/Verifiers/BreedingRules.h"

#include "Enums/GameVersion.h"
#include "Legality/Encounters/Encounterable.h"
#include "Legality/Encounters/LegalityInfo.h"
#include "Legality/Verifiers/BreedingPermissionTable.h"
#include "Legality/Verifiers/NameRules.h"
#include "Pokemon/EvolutionTable.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace
    {
        constexpr uint16_t CASTFORM_SPECIES = 351;
        constexpr uint16_t CARNIVINE_SPECIES = 455;
        constexpr uint16_t ROTOM_SPECIES = 479;
        constexpr uint16_t PHIONE_SPECIES = 489;
        constexpr uint16_t ARCHEN_SPECIES = 566;
        constexpr uint16_t CRYOGONAL_SPECIES = 615;
        constexpr uint16_t FLABEBE_SPECIES = 669;
        constexpr uint16_t FURFROU_SPECIES = 676;
        constexpr uint16_t HONEDGE_SPECIES = 679;
        constexpr uint16_t PUMPKABOO_SPECIES = 710;
        constexpr uint16_t WIMPOD_SPECIES = 767;
        constexpr uint16_t MINIOR_SPECIES = 774;
        constexpr uint16_t KOMALA_SPECIES = 775;

        bool isHiddenPossible5(uint16_t speciesId)
        {
            const size_t byteIndex = speciesId >> 3;
            if (byteIndex >= HIDDEN_ABILITY_BAN_GENERATION_5_LENGTH)
            {
                return true;
            }
            return (HIDDEN_ABILITY_BAN_GENERATION_5[byteIndex] & (1 << (speciesId & 7))) == 0;
        }

        bool isHiddenPossible6(uint16_t speciesId, uint8_t formId)
        {
            switch (speciesId)
            {
            case CASTFORM_SPECIES:
            case CARNIVINE_SPECIES:
            case ROTOM_SPECIES:
            case PHIONE_SPECIES:
            case ARCHEN_SPECIES:
            case CRYOGONAL_SPECIES:
            case HONEDGE_SPECIES:
            case FURFROU_SPECIES:
                return false;
            // Only the Orange and White flowers appear in the Friend Safari or a horde.
            case FLABEBE_SPECIES:
                return formId == 2 || formId == 4;
            // Size and ability come from the mother of an earlier generation.
            case PUMPKABOO_SPECIES:
                return formId == 1 || formId == 2;
            default:
                return true;
            }
        }

        bool isHiddenPossible7(uint16_t speciesId, uint8_t formId)
        {
            switch (speciesId)
            {
            case CARNIVINE_SPECIES:
            case ROTOM_SPECIES:
            case PHIONE_SPECIES:
            case ARCHEN_SPECIES:
            case CRYOGONAL_SPECIES:
            case HONEDGE_SPECIES:
            // Minior has no SOS encounter; Wimpod's and Komala's SOS slots are never called.
            case MINIOR_SPECIES:
            case WIMPOD_SPECIES:
            case KOMALA_SPECIES:
                return false;
            case PUMPKABOO_SPECIES:
                return formId == 1 || formId == 2;
            default:
                return true;
            }
        }
    }

    std::optional<Evolutions::EvolutionCriteria> getEggStage(const AnalysisContext &context)
    {
        if (context.encounter.type != EncounterType::Egg)
        {
            return std::nullopt;
        }
        const Pokemon::Pokemon &pokemon = context.pokemon;
        uint16_t chainSpecies[Pokemon::EVO_MAX_CHAIN];
        uint8_t chainForms[Pokemon::EVO_MAX_CHAIN];
        const int chainLength = Pokemon::getPreEvolutionChain(context.encounter.originGroup, pokemon.speciesID(), pokemon.form(), chainSpecies, chainForms, Pokemon::EVO_MAX_CHAIN);
        if (chainLength == 0)
        {
            return Evolutions::EvolutionCriteria{pokemon.speciesID(), pokemon.form()};
        }
        return Evolutions::EvolutionCriteria{chainSpecies[chainLength - 1], chainForms[chainLength - 1]};
    }

    int getEncounterGeneration(const AnalysisContext &context)
    {
        if (const Encounters::Encounterable *encounterMatch = Encounters::getEncounterMatch(context))
        {
            return encounterMatch->generation();
        }
        if (context.encounter.isValid() && context.encounter.originGroup != Enums::GameVersion::Invalid)
        {
            const int generation = Enums::getVersionGeneration(Enums::getGroupRepVersion(context.encounter.originGroup));
            if (generation != 0)
            {
                return generation;
            }
        }
        return getOriginGeneration(context);
    }

    bool isHiddenAbilityBreedable(int generation, uint16_t speciesId, uint8_t formId)
    {
        switch (generation)
        {
        case 5:
            return isHiddenPossible5(speciesId);
        case 6:
            return isHiddenPossible6(speciesId, formId);
        case 7:
            return isHiddenPossible7(speciesId, formId);
        default:
            return generation >= 8 && speciesId != PHIONE_SPECIES;
        }
    }
}
