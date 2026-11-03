#include "Legality/RNG/XDRNG.h"

#include "Legality/RNG/ChannelJirachi.h"

namespace Legality
{
    namespace RNG
    {
        namespace
        {
            // Lattice constants from StarfBerry's PokeRNG LCG_Recovery.
            constexpr uint32_t PID_LAG_0 = 0xE8D1;
            constexpr uint32_t PID_LAG_1 = 0x5F47;
            constexpr uint32_t PID_LOWER_BOUND = 0x55FF8537;
            constexpr uint32_t PID_UPPER_BOUND = 0x55FFBC6C;

            constexpr uint32_t IV_LAG_0 = 0x44C5;
            constexpr uint32_t IV_LAG_1 = 0xE8D1;
            constexpr uint32_t IV_LOWER_BOUND = 0x1E694393;
            constexpr uint32_t IV_UPPER_BOUND = 0x1E69FAC8;

            void addPIDSeeds(std::span<uint32_t> result, uint32_t estimate, uint32_t first, uint32_t second, int &seedCount) noexcept
            {
                uint32_t lowBits = (estimate * PID_LAG_1) % PID_LAG_0;
                do
                {
                    const uint32_t seed = XDRNG::previous(second | lowBits);
                    if ((seed & 0xFFFF0000) == first)
                    {
                        result[seedCount++] = XDRNG::previous(seed);
                    }
                } while ((lowBits += PID_LAG_0) < 0x10000);
            }

            void addIVSeeds(std::span<uint32_t> result, uint32_t estimate, uint32_t first, uint32_t second, int &seedCount) noexcept
            {
                uint32_t lowBits = estimate % IV_LAG_1;
                do
                {
                    uint32_t seed = XDRNG::previous(second | lowBits);
                    if ((seed & 0x7FFF0000) != first)
                    {
                        continue;
                    }
                    seed = XDRNG::previous(seed);
                    result[seedCount++] = seed;
                    result[seedCount++] = seed ^ 0x80000000;
                } while ((lowBits += IV_LAG_1) < 0x10000);
            }

            // clang-format off
            constexpr uint32_t JUMP_MULTIPLIERS[32] = {
                0x000343FD, 0xA9FC6809, 0xDDFF5051, 0xF490B9A1, 0x43BA1741, 0xD290BE81, 0x82E3BD01, 0xBF507A01,
                0xF8C4F401, 0x7A19E801, 0x1673D001, 0xB5E7A001, 0x8FCF4001, 0xAF9E8001, 0x9F3D0001, 0x3E7A0001,
                0x7CF40001, 0xF9E80001, 0xF3D00001, 0xE7A00001, 0xCF400001, 0x9E800001, 0x3D000001, 0x7A000001,
                0xF4000001, 0xE8000001, 0xD0000001, 0xA0000001, 0x40000001, 0x80000001, 0x00000001, 0x00000001,
            };

            constexpr uint32_t JUMP_INCREMENTS[32] = {
                0x00269EC3, 0x1E278E7A, 0x098520C4, 0x7E1DBEC8, 0x3E314290, 0x824E1920, 0x844E8240, 0xFD864480,
                0xDFB18900, 0xD9F71200, 0x5E3E2400, 0x65BC4800, 0x70789000, 0x74F12000, 0x39E24000, 0xB3C48000,
                0x67890000, 0xCF120000, 0x9E240000, 0x3C480000, 0x78900000, 0xF1200000, 0xE2400000, 0xC4800000,
                0x89000000, 0x12000000, 0x24000000, 0x48000000, 0x90000000, 0x20000000, 0x40000000, 0x80000000,
            };
            // clang-format on
        }

        int XDRNG::getSeeds(std::span<uint32_t> result, uint32_t PID) noexcept
        {
            return getSeeds(result, PID & 0xFFFF0000, PID << 16);
        }

        int XDRNG::getSeeds(std::span<uint32_t> result, uint32_t hpIV, uint32_t attackIV, uint32_t defenseIV, uint32_t specialAttackIV, uint32_t specialDefenseIV, uint32_t speedIV) noexcept
        {
            const uint32_t first = (hpIV | (attackIV << 5) | (defenseIV << 10)) << 16;
            const uint32_t second = (speedIV | (specialAttackIV << 5) | (specialDefenseIV << 10)) << 16;
            return getSeedsIVs(result, first, second);
        }

        // PKHeX PKHeX.Core/Legality/RNG/Algorithms/XDRNG.cs GetSeeds
        int XDRNG::getSeeds(std::span<uint32_t> result, uint32_t first, uint32_t second) noexcept
        {
            // 64-bit so the bound additions cannot overflow.
            const uint64_t lattice = static_cast<uint64_t>((first - (second * REVERSE_MULTIPLIER)) >> 16) * PID_LAG_0;
            const uint32_t lowerEstimate = static_cast<uint32_t>((lattice + PID_LOWER_BOUND) >> 16);
            const uint32_t upperEstimate = static_cast<uint32_t>((lattice + PID_UPPER_BOUND) >> 16);

            int seedCount = 0;
            addPIDSeeds(result, lowerEstimate, first, second, seedCount);
            if (lowerEstimate == upperEstimate)
            {
                return seedCount;
            }
            addPIDSeeds(result, upperEstimate, first, second, seedCount);
            return seedCount;
        }

        // PKHeX PKHeX.Core/Legality/RNG/Algorithms/XDRNG.cs GetSeedsIVs
        int XDRNG::getSeedsIVs(std::span<uint32_t> result, uint32_t first, uint32_t second) noexcept
        {
            const uint64_t lattice = static_cast<uint64_t>(((REVERSE_MULTIPLIER * second) - first) >> 16) * IV_LAG_1;
            const uint32_t lowerEstimate = static_cast<uint32_t>((lattice + IV_LOWER_BOUND) >> 15) * IV_LAG_0;
            const uint32_t middleEstimate = lowerEstimate + IV_LAG_0;
            const uint32_t upperEstimate = static_cast<uint32_t>((lattice + IV_UPPER_BOUND) >> 15) * IV_LAG_0;

            int seedCount = 0;
            addIVSeeds(result, lowerEstimate, first, second, seedCount);
            addIVSeeds(result, middleEstimate, first, second, seedCount);
            if (middleEstimate == upperEstimate)
            {
                return seedCount;
            }
            addIVSeeds(result, upperEstimate, first, second, seedCount);
            return seedCount;
        }

        // PKHeX PKHeX.Core/Legality/RNG/Algorithms/XDRNG.cs GetSeedsChannel: the first row of a
        // BKZ-reduced lattice bounds six linear combinations, at most 720 candidates in total.
        int XDRNG::getSeedsChannel(std::span<uint32_t> result, uint32_t hpIV, uint32_t attackIV, uint32_t defenseIV, uint32_t specialAttackIV, uint32_t specialDefenseIV, uint32_t speedIV) noexcept
        {
            constexpr uint32_t row0 = 0xFFD96A7C;
            constexpr uint32_t row1 = 0xFE8F9BCA;
            constexpr uint32_t row2 = 0x3282056;
            constexpr uint32_t row3 = 0x737133;
            constexpr uint32_t row4 = 0x17CA524;
            constexpr uint32_t row5 = 0xFA0B0157;

            constexpr int64_t lower0 = 0x2AB966D1C2;
            constexpr int64_t lower1 = 0x2169A3AA47;
            constexpr int64_t lower2 = -0x05049D5FDC;
            constexpr int64_t lower3 = -0x02AACDA387;
            constexpr int64_t lower4 = 0x0FE7FFFFFF;
            constexpr int64_t lower5 = -0x0898000001;
            constexpr int64_t upper0 = 0x2E8966D1C3;
            constexpr int64_t upper1 = 0x23D9A3AA48;
            constexpr int64_t upper2 = -0x03549D5FDB;
            constexpr int64_t upper3 = -0x00DACDA386;
            constexpr int64_t upper4 = 0x1098000000;
            constexpr int64_t upper5 = -0x07E8000000;

            const int64_t hp = hpIV;
            const int64_t attack = attackIV;
            const int64_t defense = defenseIV;
            const int64_t specialAttack = specialAttackIV;
            const int64_t specialDefense = specialDefenseIV;
            const int64_t speed = speedIV;

            // The bounds of row0, row1 and row5 are swapped because those rows are negative.
            const int64_t combination0 = static_cast<int64_t>(static_cast<uint64_t>((-10 * hp) + (23 * attack) - defense - (15 * speed) + (52 * specialAttack) - (53 * specialDefense)) << 27);
            const uint32_t term0Min = static_cast<uint32_t>((combination0 + upper0) >> 32) * row0;
            const uint32_t term0Max = (static_cast<uint32_t>((combination0 + lower0) >> 32) * row0) - row0;
            const int64_t combination1 = static_cast<int64_t>(static_cast<uint64_t>((-14 * hp) + (7 * attack) - (18 * defense) - (21 * speed) - (26 * specialAttack) - (24 * specialDefense)) << 27);
            const uint32_t term1Min = static_cast<uint32_t>((combination1 + upper1) >> 32) * row1;
            const uint32_t term1Max = (static_cast<uint32_t>((combination1 + lower1) >> 32) * row1) - row1;
            const int64_t combination2 = static_cast<int64_t>(static_cast<uint64_t>((24 * hp) - (5 * attack) + (22 * defense) + (15 * speed) - (5 * specialAttack) - (15 * specialDefense)) << 27);
            const uint32_t term2Min = static_cast<uint32_t>((combination2 + lower2) >> 32) * row2;
            const uint32_t term2Max = (static_cast<uint32_t>((combination2 + upper2) >> 32) * row2) + row2;
            const int64_t combination3 = static_cast<int64_t>(static_cast<uint64_t>((-5 * hp) - (24 * attack) + (26 * defense) - (12 * speed) + (9 * specialAttack) + (14 * specialDefense)) << 27);
            const uint32_t term3Min = static_cast<uint32_t>((combination3 + lower3) >> 32) * row3;
            const uint32_t term3Max = (static_cast<uint32_t>((combination3 + upper3) >> 32) * row3) + row3;
            const int64_t combination4 = static_cast<int64_t>(static_cast<uint64_t>((27 * attack) - (18 * speed) - (8 * specialAttack) - specialDefense) << 27);
            const uint32_t term4Min = static_cast<uint32_t>((combination4 + lower4) >> 32) * row4;
            const uint32_t term4Max = (static_cast<uint32_t>((combination4 + upper4) >> 32) * row4) + row4;
            const int64_t combination5 = static_cast<int64_t>(static_cast<uint64_t>((-27 * hp) + (18 * defense) + (8 * speed) + specialAttack) << 27);
            const uint32_t term5Min = static_cast<uint32_t>((combination5 + upper5) >> 32) * row5;
            const uint32_t term5Max = (static_cast<uint32_t>((combination5 + lower5) >> 32) * row5) - row5;

            int seedCount = 0;
            for (uint32_t term5 = term5Min; term5 != term5Max; term5 -= row5)
            {
                for (uint32_t term4 = term4Min; term4 != term4Max; term4 += row4)
                {
                    const uint32_t sum4 = term5 + term4;
                    for (uint32_t term2 = term2Min; term2 != term2Max; term2 += row2)
                    {
                        const uint32_t sum2 = sum4 + term2;
                        for (uint32_t term3 = term3Min; term3 != term3Max; term3 += row3)
                        {
                            const uint32_t sum3 = sum2 + term3;
                            for (uint32_t term1 = term1Min; term1 != term1Max; term1 -= row1)
                            {
                                const uint32_t sum1 = sum3 + term1;
                                for (uint32_t term0 = term0Min; term0 != term0Max; term0 -= row0)
                                {
                                    uint32_t seed = sum1 + term0;
                                    if ((seed >> 27) != hpIV)
                                    {
                                        continue;
                                    }
                                    if (nextFiveBits(seed) != attackIV)
                                    {
                                        continue;
                                    }
                                    if (nextFiveBits(seed) != defenseIV)
                                    {
                                        continue;
                                    }
                                    if (nextFiveBits(seed) != speedIV)
                                    {
                                        continue;
                                    }
                                    if (nextFiveBits(seed) != specialAttackIV)
                                    {
                                        continue;
                                    }
                                    if (nextFiveBits(seed) != specialDefenseIV)
                                    {
                                        continue;
                                    }
                                    result[seedCount++] = ChannelJirachi::previousToOrigin(seed);
                                }
                            }
                        }
                    }
                }
            }
            return seedCount;
        }

        // PKHeX PKHeX.Core/Legality/RNG/Algorithms/XDRNG.cs GetDistance
        uint32_t XDRNG::getDistance(uint32_t start, uint32_t end) noexcept
        {
            int jumpIndex = 0;
            uint32_t bit = 1u;
            uint32_t distance = 0u;
            uint32_t seed = start;
            while (seed != end)
            {
                if (((seed ^ end) & bit) != 0)
                {
                    seed = (seed * JUMP_MULTIPLIERS[jumpIndex]) + JUMP_INCREMENTS[jumpIndex];
                    distance |= bit;
                }
                jumpIndex++;
                bit <<= 1;
            }
            return distance;
        }
    }
}
