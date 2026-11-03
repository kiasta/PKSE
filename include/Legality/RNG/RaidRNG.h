/**
 * Sword/Shield Max Raid Battles: a 64-bit den seed in xoroshiro128+ rolls the EC, a fake trainer,
 * the PID, IVs, ability, gender, nature and size.
 *
 * Ported from PKHeX PKHeX.Core/Legality/RNG/Methods/Gen8/RaidRNG.cs (Verify; TryApply builds an
 * entity and is not ported).
 */
#ifndef LEGALITY_RNG_RAIDRNG_H
#define LEGALITY_RNG_RAIDRNG_H

#include <cstdint>
#include <span>

#include "Legality/RNG/GenerateParameters.h"

namespace Pokemon
{
    class Pokemon;
}

namespace Legality
{
    namespace RNG
    {
        struct RaidRNG
        {
            /// `individualValues` is six ints of scratch space, left holding the regenerated IVs (speed last).
            static bool verify(const Pokemon::Pokemon &pokemon, uint64_t seed, std::span<int> individualValues, const GenerateParam8 &parameters, bool forceNoShiny = false) noexcept;
        };
    }
}

#endif
