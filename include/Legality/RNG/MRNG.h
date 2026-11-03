/**
 * The C standard library's rand() (add 12345): Gen 3 wild encounter triggers, the Gen 4 lottery,
 * Pokemon Rumble and My Pokemon Ranch.
 *
 * Ported from PKHeX PKHeX.Core/Legality/RNG/Algorithms/MRNG.cs.
 */
#ifndef LEGALITY_RNG_MRNG_H
#define LEGALITY_RNG_MRNG_H

#include <cstdint>

namespace Legality
{
    namespace RNG
    {
        struct MRNG
        {
            static constexpr uint32_t MULTIPLIER = 0x41C64E6D;
            static constexpr uint32_t INCREMENT = 0x00003039;
            static constexpr uint32_t REVERSE_MULTIPLIER = 0xEEB9EB65;
            static constexpr uint32_t REVERSE_INCREMENT = 0xFC77A683;

            static constexpr uint32_t MULTIPLIER_2 = MULTIPLIER * MULTIPLIER;
            static constexpr uint32_t REVERSE_MULTIPLIER_2 = REVERSE_MULTIPLIER * REVERSE_MULTIPLIER;
            static constexpr uint32_t INCREMENT_2 = INCREMENT * (MULTIPLIER + 1);
            static constexpr uint32_t REVERSE_INCREMENT_2 = REVERSE_INCREMENT * (REVERSE_MULTIPLIER + 1);

            static constexpr uint32_t next(uint32_t seed) noexcept { return (seed * MULTIPLIER) + INCREMENT; }
            static constexpr uint32_t next2(uint32_t seed) noexcept { return (seed * MULTIPLIER_2) + INCREMENT_2; }
            static constexpr uint32_t previous(uint32_t seed) noexcept { return (seed * REVERSE_MULTIPLIER) + REVERSE_INCREMENT; }
            static constexpr uint32_t previous2(uint32_t seed) noexcept { return (seed * REVERSE_MULTIPLIER_2) + REVERSE_INCREMENT_2; }

            static constexpr uint32_t next15(uint32_t &seed) noexcept
            {
                seed = next(seed);
                return (seed >> 16) & 0x7FFF;
            }

            /// IVs packed HP, Attack, Defense, Speed, Special Attack, Special Defense (low to high).
            /// The second call is drawn but never lands in the 30 bits kept.
            static constexpr uint32_t getSequentialIVs(uint32_t seed) noexcept
            {
                const uint32_t firstRandom = next15(seed);
                const uint32_t secondRandom = next15(seed);
                const uint32_t thirdRandom = next15(seed);
                return ((secondRandom << 30) | (firstRandom << 15) | thirdRandom) & 0x3FFFFFFF;
            }
        };
    }
}

#endif
