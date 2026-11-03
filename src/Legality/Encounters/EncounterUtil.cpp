#include "Legality/Encounters/EncounterUtil.h"

#include "Pokemon/FormInfo.h"
#include "Pokemon/PIDSearch.h"
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
            constexpr uint16_t SPECIES_FLOETTE = 670;
            constexpr uint16_t SPECIES_FURFROU = 676;
            constexpr uint16_t SPECIES_MEOWSTIC = 678;
            constexpr uint16_t SPECIES_ZYGARDE = 718;
            constexpr uint16_t SPECIES_MINIOR = 774;
            constexpr uint16_t SPECIES_MIMIKYU = 778;
            constexpr uint16_t SPECIES_MAGEARNA = 801;
            constexpr uint16_t SPECIES_TATSUGIRI = 978;
            constexpr uint16_t SPECIES_OGERPON = 1017;
        }

        bool FormInfo::isBattleOnlyForm(uint16_t speciesId, uint8_t formId) noexcept
        {
            return Pokemon::isMegaForm(speciesId, formId) || Pokemon::isBattleForm(speciesId, formId);
        }

        uint8_t FormInfo::getOutOfBattleForm(uint16_t speciesId, uint8_t formId, uint8_t formatGeneration) noexcept
        {
            switch (speciesId)
            {
            case SPECIES_DARMANITAN:
                return formId & 2;
            case SPECIES_GRENINJA:
                return formId == 2 ? 1 : 0;
            case SPECIES_ZYGARDE:
                return formatGeneration > 6 ? 3 : 0;
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

        uint8_t FormInfo::getTotemBaseForm(uint16_t speciesId, uint8_t formId) noexcept
        {
            if (speciesId == SPECIES_MIMIKYU)
            {
                return 0;
            }
            return formId - 1;
        }

        uint8_t FormInfo::getEggForm(uint16_t speciesId, uint8_t formId, uint8_t generation) noexcept
        {
            if (isBattleOnlyForm(speciesId, formId) || speciesId == SPECIES_ROTOM || speciesId == SPECIES_CASTFORM || speciesId == SPECIES_FURFROU)
            {
                return getOutOfBattleForm(speciesId, formId, generation);
            }
            return formId;
        }

        bool WurmpleUtil::isWurmpleEvolutionValid(const Pokemon::Pokemon &pokemon) noexcept
        {
            constexpr uint16_t speciesSilcoon = 266;
            constexpr uint16_t speciesBeautifly = 267;
            constexpr uint16_t speciesCascoon = 268;
            constexpr uint16_t speciesDustox = 269;
            const uint16_t speciesId = pokemon.speciesID();
            int evolutionGroup = -1;
            if (speciesId == speciesSilcoon || speciesId == speciesBeautifly)
            {
                evolutionGroup = 0;
            }
            else if (speciesId == speciesCascoon || speciesId == speciesDustox)
            {
                evolutionGroup = 1;
            }
            return Pokemon::wurmpleEvolutionFromPID(pokemon.encryptionConstant()) == evolutionGroup;
        }
    }
}
