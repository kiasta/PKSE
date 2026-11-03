/**
 * The linear congruential RNG of Pokemon Colosseum and Pokemon XD (and the Ruby/Sapphire Channel
 * Jirachi event, which ran on the GameCube).
 *
 * Ported from PKHeX PKHeX.Core/Legality/RNG/Algorithms/XDRNG.cs. Seed recovery follows StarfBerry's
 * PokeRNG LCG_Recovery; every recovered seed is the one BEFORE the first result.
 */
#ifndef LEGALITY_RNG_XDRNG_H
#define LEGALITY_RNG_XDRNG_H

#include <cstdint>
#include <span>

namespace Legality
{
    namespace RNG
    {
        struct XDRNG
        {
            static constexpr uint32_t MULTIPLIER = 0x000343FD;
            static constexpr uint32_t INCREMENT = 0x00269EC3;
            static constexpr uint32_t REVERSE_MULTIPLIER = 0xB9B33155;
            static constexpr uint32_t REVERSE_INCREMENT = 0xA170F641;

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

            static constexpr uint32_t REVERSE_MULTIPLIER_10 = REVERSE_MULTIPLIER_9 * REVERSE_MULTIPLIER;
            static constexpr uint32_t REVERSE_INCREMENT_10 = (REVERSE_INCREMENT_9 * REVERSE_MULTIPLIER) + REVERSE_INCREMENT;

            static constexpr uint32_t REVERSE_MULTIPLIER_11 = REVERSE_MULTIPLIER_10 * REVERSE_MULTIPLIER;
            static constexpr uint32_t REVERSE_INCREMENT_11 = (REVERSE_INCREMENT_10 * REVERSE_MULTIPLIER) + REVERSE_INCREMENT;

            static constexpr uint32_t REVERSE_MULTIPLIER_12 = REVERSE_MULTIPLIER_11 * REVERSE_MULTIPLIER;
            static constexpr uint32_t REVERSE_INCREMENT_12 = (REVERSE_INCREMENT_11 * REVERSE_MULTIPLIER) + REVERSE_INCREMENT;

            static constexpr int MAX_PID_SEED_COUNT = 2;
            static constexpr int MAX_IV_SEED_COUNT = 6;
            static constexpr int MAX_CHANNEL_SEED_COUNT = 12;

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
            static constexpr uint32_t previous10(uint32_t seed) noexcept { return (seed * REVERSE_MULTIPLIER_10) + REVERSE_INCREMENT_10; }
            static constexpr uint32_t previous11(uint32_t seed) noexcept { return (seed * REVERSE_MULTIPLIER_11) + REVERSE_INCREMENT_11; }
            static constexpr uint32_t previous12(uint32_t seed) noexcept { return (seed * REVERSE_MULTIPLIER_12) + REVERSE_INCREMENT_12; }

            static constexpr uint32_t next1000(uint32_t seed) noexcept { return (0xDD867B21 * seed) + 0xD252C5A8; }
            static constexpr uint32_t previous1000(uint32_t seed) noexcept { return (0x251CC8E1 * seed) + 0x94750758; }

            static constexpr uint32_t next16(uint32_t &seed) noexcept
            {
                seed = next(seed);
                return seed >> 16;
            }

            /// Advances `seed` and returns its top 5 bits (a Channel IV).
            static constexpr uint32_t nextFiveBits(uint32_t &seed) noexcept
            {
                seed = next(seed);
                return seed >> 27;
            }

            static constexpr uint32_t next15(uint32_t &seed) noexcept
            {
                seed = next(seed);
                return (seed >> 16) & 0x7FFF;
            }

            static constexpr uint32_t previous16(uint32_t &seed) noexcept
            {
                seed = previous(seed);
                return seed >> 16;
            }

            static constexpr uint32_t previous15(uint32_t &seed) noexcept
            {
                seed = previous(seed);
                return (seed >> 16) & 0x7FFF;
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

            /// Six 5-bit IVs from consecutive calls, packed low to high (the Channel order).
            static constexpr uint32_t getSequentialIV32(uint32_t seed) noexcept
            {
                uint32_t packedIVs = 0;
                for (int statIndex = 0; statIndex < 6; statIndex++)
                {
                    packedIVs |= nextFiveBits(seed) << (statIndex * 5);
                }
                return packedIVs;
            }

            /// Seeds whose next two upper halves are the PID's high and low halves.
            static int getSeeds(std::span<uint32_t> result, uint32_t PID) noexcept;

            /// Seeds whose next two upper halves are `first` and `second` (both already << 16).
            /// `result` must hold MAX_PID_SEED_COUNT.
            static int getSeeds(std::span<uint32_t> result, uint32_t first, uint32_t second) noexcept;

            static int getSeeds(std::span<uint32_t> result, uint32_t hpIV, uint32_t attackIV, uint32_t defenseIV, uint32_t specialAttackIV, uint32_t specialDefenseIV, uint32_t speedIV) noexcept;

            /// Seeds whose next two 15-bit results are `first` and `second` (both already << 16).
            /// `result` must hold MAX_IV_SEED_COUNT.
            static int getSeedsIVs(std::span<uint32_t> result, uint32_t first, uint32_t second) noexcept;

            /// Channel Jirachi origin seeds for six 5-bit IVs. `result` must hold MAX_CHANNEL_SEED_COUNT.
            static int getSeedsChannel(std::span<uint32_t> result, uint32_t hpIV, uint32_t attackIV, uint32_t defenseIV, uint32_t specialAttackIV, uint32_t specialDefenseIV, uint32_t speedIV) noexcept;

            /// How many next() calls take `start` to `end`.
            static uint32_t getDistance(uint32_t start, uint32_t end) noexcept;
        };
    }
}

#endif
