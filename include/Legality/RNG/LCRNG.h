/**
 * The linear congruential RNG of the Game Boy Advance and Nintendo DS games (Gen 3 and 4): every
 * PID, IV spread and encounter roll those games make comes out of it.
 *
 * Ported from PKHeX PKHeX.Core/Legality/RNG/Algorithms/LCRNG.cs. The jump constants are the
 * multiplier and increment applied n times, so next5(seed) is next() five times in one step.
 */
#ifndef LEGALITY_RNG_LCRNG_H
#define LEGALITY_RNG_LCRNG_H

#include <cstdint>

namespace Legality
{
    namespace RNG
    {
        struct LCRNG
        {
            static constexpr uint32_t MULTIPLIER = 0x41C64E6D;
            static constexpr uint32_t INCREMENT = 0x00006073;
            static constexpr uint32_t REVERSE_MULTIPLIER = 0xEEB9EB65;
            static constexpr uint32_t REVERSE_INCREMENT = 0x0A3561A1;

            static constexpr uint32_t MULTIPLIER_2 = MULTIPLIER * MULTIPLIER;
            static constexpr uint32_t REVERSE_MULTIPLIER_2 = REVERSE_MULTIPLIER * REVERSE_MULTIPLIER;
            static constexpr uint32_t INCREMENT_2 = INCREMENT * (MULTIPLIER + 1);
            static constexpr uint32_t REVERSE_INCREMENT_2 = REVERSE_INCREMENT * (REVERSE_MULTIPLIER + 1);

            static constexpr uint32_t MULTIPLIER_3 = MULTIPLIER_2 * MULTIPLIER;
            static constexpr uint32_t REVERSE_MULTIPLIER_3 = REVERSE_MULTIPLIER_2 * REVERSE_MULTIPLIER;
            static constexpr uint32_t INCREMENT_3 = (INCREMENT_2 * MULTIPLIER) + INCREMENT;
            static constexpr uint32_t REVERSE_INCREMENT_3 = (REVERSE_INCREMENT_2 * REVERSE_MULTIPLIER) + REVERSE_INCREMENT;

            static constexpr uint32_t MULTIPLIER_4 = MULTIPLIER_3 * MULTIPLIER;
            static constexpr uint32_t REVERSE_MULTIPLIER_4 = REVERSE_MULTIPLIER_3 * REVERSE_MULTIPLIER;
            static constexpr uint32_t INCREMENT_4 = (INCREMENT_3 * MULTIPLIER) + INCREMENT;
            static constexpr uint32_t REVERSE_INCREMENT_4 = (REVERSE_INCREMENT_3 * REVERSE_MULTIPLIER) + REVERSE_INCREMENT;

            static constexpr uint32_t MULTIPLIER_5 = MULTIPLIER_4 * MULTIPLIER;
            static constexpr uint32_t REVERSE_MULTIPLIER_5 = REVERSE_MULTIPLIER_4 * REVERSE_MULTIPLIER;
            static constexpr uint32_t INCREMENT_5 = (INCREMENT_4 * MULTIPLIER) + INCREMENT;
            static constexpr uint32_t REVERSE_INCREMENT_5 = (REVERSE_INCREMENT_4 * REVERSE_MULTIPLIER) + REVERSE_INCREMENT;

            static constexpr uint32_t MULTIPLIER_6 = MULTIPLIER_5 * MULTIPLIER;
            static constexpr uint32_t REVERSE_MULTIPLIER_6 = REVERSE_MULTIPLIER_5 * REVERSE_MULTIPLIER;
            static constexpr uint32_t INCREMENT_6 = (INCREMENT_5 * MULTIPLIER) + INCREMENT;
            static constexpr uint32_t REVERSE_INCREMENT_6 = (REVERSE_INCREMENT_5 * REVERSE_MULTIPLIER) + REVERSE_INCREMENT;

            static constexpr uint32_t MULTIPLIER_7 = MULTIPLIER_6 * MULTIPLIER;
            static constexpr uint32_t REVERSE_MULTIPLIER_7 = REVERSE_MULTIPLIER_6 * REVERSE_MULTIPLIER;
            static constexpr uint32_t INCREMENT_7 = (INCREMENT_6 * MULTIPLIER) + INCREMENT;
            static constexpr uint32_t REVERSE_INCREMENT_7 = (REVERSE_INCREMENT_6 * REVERSE_MULTIPLIER) + REVERSE_INCREMENT;

            static constexpr uint32_t MULTIPLIER_8 = MULTIPLIER_7 * MULTIPLIER;
            static constexpr uint32_t REVERSE_MULTIPLIER_8 = REVERSE_MULTIPLIER_7 * REVERSE_MULTIPLIER;
            static constexpr uint32_t INCREMENT_8 = (INCREMENT_7 * MULTIPLIER) + INCREMENT;
            static constexpr uint32_t REVERSE_INCREMENT_8 = (REVERSE_INCREMENT_7 * REVERSE_MULTIPLIER) + REVERSE_INCREMENT;

            static constexpr uint32_t MULTIPLIER_9 = MULTIPLIER_8 * MULTIPLIER;
            static constexpr uint32_t REVERSE_MULTIPLIER_9 = REVERSE_MULTIPLIER_8 * REVERSE_MULTIPLIER;
            static constexpr uint32_t INCREMENT_9 = (INCREMENT_8 * MULTIPLIER) + INCREMENT;
            static constexpr uint32_t REVERSE_INCREMENT_9 = (REVERSE_INCREMENT_8 * REVERSE_MULTIPLIER) + REVERSE_INCREMENT;

            /// The most seeds one PID (two 16-bit halves) can come from.
            static constexpr int MAX_PID_SEED_COUNT = 3;
            /// The most seeds one IV spread (two 15-bit halves) can come from.
            static constexpr int MAX_IV_SEED_COUNT = 6;

            static constexpr uint32_t next(uint32_t seed) noexcept { return (seed * MULTIPLIER) + INCREMENT; }
            static constexpr uint32_t next2(uint32_t seed) noexcept { return (seed * MULTIPLIER_2) + INCREMENT_2; }
            static constexpr uint32_t next3(uint32_t seed) noexcept { return (seed * MULTIPLIER_3) + INCREMENT_3; }
            static constexpr uint32_t next4(uint32_t seed) noexcept { return (seed * MULTIPLIER_4) + INCREMENT_4; }
            static constexpr uint32_t next5(uint32_t seed) noexcept { return (seed * MULTIPLIER_5) + INCREMENT_5; }
            static constexpr uint32_t next6(uint32_t seed) noexcept { return (seed * MULTIPLIER_6) + INCREMENT_6; }
            static constexpr uint32_t next7(uint32_t seed) noexcept { return (seed * MULTIPLIER_7) + INCREMENT_7; }
            static constexpr uint32_t next8(uint32_t seed) noexcept { return (seed * MULTIPLIER_8) + INCREMENT_8; }
            static constexpr uint32_t next9(uint32_t seed) noexcept { return (seed * MULTIPLIER_9) + INCREMENT_9; }

            static constexpr uint32_t previous(uint32_t seed) noexcept { return (seed * REVERSE_MULTIPLIER) + REVERSE_INCREMENT; }
            static constexpr uint32_t previous2(uint32_t seed) noexcept { return (seed * REVERSE_MULTIPLIER_2) + REVERSE_INCREMENT_2; }
            static constexpr uint32_t previous3(uint32_t seed) noexcept { return (seed * REVERSE_MULTIPLIER_3) + REVERSE_INCREMENT_3; }
            static constexpr uint32_t previous4(uint32_t seed) noexcept { return (seed * REVERSE_MULTIPLIER_4) + REVERSE_INCREMENT_4; }
            static constexpr uint32_t previous5(uint32_t seed) noexcept { return (seed * REVERSE_MULTIPLIER_5) + REVERSE_INCREMENT_5; }
            static constexpr uint32_t previous6(uint32_t seed) noexcept { return (seed * REVERSE_MULTIPLIER_6) + REVERSE_INCREMENT_6; }
            static constexpr uint32_t previous7(uint32_t seed) noexcept { return (seed * REVERSE_MULTIPLIER_7) + REVERSE_INCREMENT_7; }
            static constexpr uint32_t previous8(uint32_t seed) noexcept { return (seed * REVERSE_MULTIPLIER_8) + REVERSE_INCREMENT_8; }
            static constexpr uint32_t previous9(uint32_t seed) noexcept { return (seed * REVERSE_MULTIPLIER_9) + REVERSE_INCREMENT_9; }

            /// Advances `seed` and returns its upper 16 bits.
            static constexpr uint32_t next16(uint32_t &seed) noexcept
            {
                seed = next(seed);
                return seed >> 16;
            }

            /// Advances `seed` and returns its upper 16 bits without the top one (an IV word).
            static constexpr uint32_t next15(uint32_t &seed) noexcept
            {
                seed = next(seed);
                return (seed >> 16) & 0x7FFF;
            }

            /// Reverses `seed` and returns its upper 16 bits.
            static constexpr uint32_t previous16(uint32_t &seed) noexcept
            {
                seed = previous(seed);
                return seed >> 16;
            }

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

            /// How many next() calls take `start` to `end`, in at most 32 jumps.
            static uint32_t getDistance(uint32_t start, uint32_t end) noexcept;
        };
    }
}

#endif
