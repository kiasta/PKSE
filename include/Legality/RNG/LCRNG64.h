/**
 * The 64-bit LCRNG of the Gen 5 games (Black, White, Black 2, White 2).
 *
 * Ported from PKHeX PKHeX.Core/Legality/RNG/Algorithms/LCRNG64.cs.
 */
#ifndef LEGALITY_RNG_LCRNG64_H
#define LEGALITY_RNG_LCRNG64_H

#include <cstdint>

namespace Legality
{
    namespace RNG
    {
        struct LCRNG64
        {
            static constexpr uint64_t MULTIPLIER = 0x5D588B656C078965;
            static constexpr uint64_t INCREMENT = 0x0000000000269EC3;
            static constexpr uint64_t REVERSE_MULTIPLIER = 0xDEDCEDAE9638806D;
            static constexpr uint64_t REVERSE_INCREMENT = 0x9B1AE6E9A384E6F9;

            static constexpr uint64_t next(uint64_t seed) noexcept { return (seed * MULTIPLIER) + INCREMENT; }
            static constexpr uint64_t previous(uint64_t seed) noexcept { return (seed * REVERSE_MULTIPLIER) + REVERSE_INCREMENT; }

            /// PKHeX stores the shifted result back into the seed, and so does this.
            static constexpr uint32_t next32(uint64_t &seed) noexcept
            {
                seed = next(seed) >> 32;
                return static_cast<uint32_t>(seed);
            }

            /// A value below `exclusiveMaximum` from the call after `seed`, leaving `seed` as it was.
            static constexpr uint32_t nextRangeFrom(uint64_t seed, uint32_t exclusiveMaximum) noexcept
            {
                return static_cast<uint32_t>(((next(seed) >> 32) * exclusiveMaximum) >> 32);
            }

            static constexpr uint32_t nextRange(uint64_t &seed, uint32_t exclusiveMaximum) noexcept
            {
                seed = next(seed);
                return static_cast<uint32_t>(((seed >> 32) * exclusiveMaximum) >> 32);
            }
        };
    }
}

#endif
