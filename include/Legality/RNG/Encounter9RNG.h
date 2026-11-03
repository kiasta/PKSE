/**
 * Scarlet/Violet fixed-seed Pokemon (Tera Raids, overworld fixed spawns, distributions): the seed
 * in xoroshiro128+ rolls the EC, a fake trainer, the PID, IVs, ability, gender, nature and sizes.
 *
 * Ported from PKHeX PKHeX.Core/Legality/RNG/Methods/Gen9/Encounter9RNG.cs (IsMatch and helpers;
 * TryApply32/TryApply64/GenerateData build an entity and are not ported).
 */
#ifndef LEGALITY_RNG_ENCOUNTER9RNG_H
#define LEGALITY_RNG_ENCOUNTER9RNG_H

#include <cstdint>

#include "Legality/RNG/GenerateParameters.h"

namespace Pokemon
{
    class Pokemon;
}

namespace Legality
{
    namespace RNG
    {
        struct Encounter9RNG
        {
            static bool isMatch(const Pokemon::Pokemon &pokemon, const GenerateParam9 &parameters, uint64_t seed) noexcept;

            static bool isHeightMatchSV(const Pokemon::Pokemon &pokemon, uint8_t heightScalar) noexcept;

            /// Scarlet/Violet's percentage gender roll for the five ratios; 0 for any other ratio, where PKHeX throws.
            static uint8_t getGender(uint8_t ratio, uint64_t random100) noexcept;
        };
    }
}

#endif
