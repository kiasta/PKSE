/**
 * The alternate 32-bit LCRNG of the Gen 4 games, which re-rolls a Mystery Gift PID that would have
 * been shiny for the receiving trainer.
 *
 * Ported from PKHeX PKHeX.Core/Legality/RNG/Algorithms/ARNG.cs.
 */
#ifndef LEGALITY_RNG_ARNG_H
#define LEGALITY_RNG_ARNG_H

#include <cstdint>

namespace Legality
{
    namespace RNG
    {
        struct ARNG
        {
            static constexpr uint32_t MULTIPLIER = 0x6C078965;
            static constexpr uint32_t INCREMENT = 0x00000001;
            static constexpr uint32_t REVERSE_MULTIPLIER = 0x9638806D;
            static constexpr uint32_t REVERSE_INCREMENT = 0x69C77F93;

            static constexpr uint32_t next(uint32_t seed) noexcept { return (seed * MULTIPLIER) + INCREMENT; }
            static constexpr uint32_t previous(uint32_t seed) noexcept { return (seed * REVERSE_MULTIPLIER) + REVERSE_INCREMENT; }

            static constexpr uint32_t advance(uint32_t seed, int frameCount) noexcept
            {
                for (int i = 0; i < frameCount; i++)
                {
                    seed = next(seed);
                }
                return seed;
            }

            static constexpr uint32_t reverse(uint32_t seed, int frameCount) noexcept
            {
                for (int i = 0; i < frameCount; i++)
                {
                    seed = previous(seed);
                }
                return seed;
            }
        };
    }
}

#endif
