/**
 * Where an egg of each game can hatch. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Verifiers/EggHatchLocation3.cs ... EggHatchLocation9.cs (EggHatchLocation8b.cs included).
 */
#ifndef LEGALITY_ENCOUNTERS_EGGHATCHLOCATION_H
#define LEGALITY_ENCOUNTERS_EGGHATCHLOCATION_H

#include <cstdint>

#include "Enums/GameVersion.h"

namespace Legality
{
    namespace Encounters
    {
        /// PKHeX EggHatchLocation3.IsValidMet3 (version is the stored version id).
        bool isValidMet3(uint16_t location, uint8_t version) noexcept;
        bool isValidMet3Any(uint16_t location) noexcept;
        /// PKHeX EggHatchLocation4.IsValidMet4 (version is the stored version id).
        bool isValidMet4(uint16_t location, uint8_t version) noexcept;
        bool isValidMet4Any(uint16_t location) noexcept;
        /// PKHeX EggHatchLocation5.IsValidMet5 (version is the stored version id).
        bool isValidMet5(uint16_t location, uint8_t version) noexcept;
        /// PKHeX EggHatchLocation6.IsValidMet6XY / IsValidMet6AO.
        bool isValidMet6XY(uint16_t location) noexcept;
        bool isValidMet6AO(uint16_t location) noexcept;
        /// PKHeX EggHatchLocation7.IsValidMet7SM / IsValidMet7USUM.
        bool isValidMet7SM(uint16_t location) noexcept;
        bool isValidMet7USUM(uint16_t location) noexcept;
        /// PKHeX EggHatchLocation8.IsValidMet8SWSH.
        bool isValidMet8SWSH(uint16_t location) noexcept;
        /// PKHeX EggHatchLocation8b.IsValidMet8BD / IsValidMet8SP.
        bool isValidMet8BD(uint16_t location) noexcept;
        bool isValidMet8SP(uint16_t location) noexcept;
        /// PKHeX EggHatchLocation9.IsValidMet9SL.
        bool isValidMet9SL(uint16_t location) noexcept;
        /// PKHeX EggHatchLocation9.IsValidMet9VL.
        bool isValidMet9VL(uint16_t location) noexcept;
    }
}

#endif
