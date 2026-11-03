/**
 * What PKHeX's egg templates ask of a species before offering an egg for it. Ported from PKHeX
 * PKHeX.Core/Legality/Breeding.cs and FormInfo.IsTotemForm / IsLordForm.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERBREEDING_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERBREEDING_H

#include <cstdint>

#include "Enums/GameVersion.h"

namespace Legality
{
    namespace Encounters
    {
        /// PKHeX Breeding.CanHatchAsEgg(species).
        bool canHatchAsEgg(uint16_t speciesId) noexcept;
        /// PKHeX Breeding.CanHatchAsEgg(species, form, context), the context as a context group.
        bool canHatchAsEgg(uint16_t speciesId, uint8_t formId, Enums::GameVersion contextGroup) noexcept;
        /// PKHeX Breeding.IsSplitBreedNotBabySpecies3 (Marill and Wobbuffet).
        bool isSplitBreedNotBabySpecies3(uint16_t speciesId) noexcept;
        /// PKHeX Breeding.IsSplitBreedNotBabySpecies4.
        bool isSplitBreedNotBabySpecies4(uint16_t speciesId) noexcept;
        /// PKHeX FormInfo.IsTotemForm(species, form).
        bool isTotemForm(uint16_t speciesId, uint8_t formId) noexcept;
    }
}

#endif
