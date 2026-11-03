#include "Legality/Encounters/EncounterBreeding.h"

#include <algorithm>

#include "Legality/Encounters/EncounterBreedingTable.h"
#include "Pokemon/FormInfo.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            constexpr uint16_t SPECIES_RATICATE = 20;
            constexpr uint16_t SPECIES_MAROWAK = 105;
            constexpr uint16_t SPECIES_MARILL = 183;
            constexpr uint16_t SPECIES_WOBBUFFET = 202;
            constexpr uint16_t SPECIES_MIMIKYU = 778;
            // PKHeX FormInfo.HasTotemForm.
            constexpr uint16_t TOTEM_SPECIES[] = {20, 105, 735, 738, 743, 752, 754, 758, 777, 778, 784};
        }

        bool canHatchAsEgg(uint16_t speciesId) noexcept
        {
            const auto unhatchable = getUnhatchableSpecies();
            return !std::binary_search(unhatchable.begin(), unhatchable.end(), speciesId);
        }

        bool isTotemForm(uint16_t speciesId, uint8_t formId) noexcept
        {
            if (formId == 0)
            {
                return false;
            }
            if (std::find(std::begin(TOTEM_SPECIES), std::end(TOTEM_SPECIES), speciesId) == std::end(TOTEM_SPECIES))
            {
                return false;
            }
            if (speciesId == SPECIES_MIMIKYU)
            {
                return formId == 2 || formId == 3;
            }
            if (speciesId == SPECIES_RATICATE || speciesId == SPECIES_MAROWAK)
            {
                return formId == 2;
            }
            return formId == 1;
        }

        bool canHatchAsEgg(uint16_t speciesId, uint8_t formId, Enums::GameVersion contextGroup) noexcept
        {
            if (formId == 0)
            {
                return true;
            }
            if (contextGroup == Enums::GameVersion::SM && isTotemForm(speciesId, formId))
            {
                return false;
            }
            if (contextGroup == Enums::GameVersion::PLA && Pokemon::isLordForm(speciesId, formId))
            {
                return false;
            }
            for (const UnbreedableForm &entry : getUnbreedableForms())
            {
                if (entry.speciesId == speciesId && (entry.formId == 0xFF || entry.formId == formId))
                {
                    return false;
                }
            }
            return true;
        }

        bool isSplitBreedNotBabySpecies3(uint16_t speciesId) noexcept
        {
            return speciesId == SPECIES_MARILL || speciesId == SPECIES_WOBBUFFET;
        }

        bool isSplitBreedNotBabySpecies4(uint16_t speciesId) noexcept
        {
            const auto splitBreed = getSplitBreedSpecies4();
            return std::binary_search(splitBreed.begin(), splitBreed.end(), speciesId);
        }
    }
}
