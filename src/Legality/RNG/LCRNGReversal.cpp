#include "Legality/RNG/LCRNGReversal.h"

#include "Legality/RNG/LCRNG.h"

namespace Legality
{
    namespace RNG
    {
        namespace
        {
            // Lattice constants from StarfBerry's PokeRNG LCG_Recovery.
            constexpr uint32_t PID_LAG_0 = 0x7ED7;
            constexpr uint32_t PID_LAG_1 = 0xD33;
            constexpr uint32_t PID_LOWER_BOUND = 0x50F5A0B;
            constexpr uint32_t PID_UPPER_BOUND = 0x50F40B4;

            constexpr uint32_t IV_LAG_0 = 0x67D3;
            constexpr uint32_t IV_LAG_1 = 0xC907;
            constexpr uint32_t IV_LOWER_BOUND = 0x3443;
            constexpr uint32_t IV_UPPER_BOUND = 0xC34E;

            void addSeeds(std::span<uint32_t> result, uint32_t lowBits, uint32_t first, uint32_t second, int &seedCount) noexcept
            {
                lowBits %= IV_LAG_1;
                do
                {
                    uint32_t seed = first | lowBits;
                    if ((LCRNG::next(seed) & 0x7FFF0000) != second)
                    {
                        continue;
                    }
                    seed = LCRNG::previous(seed);
                    result[seedCount++] = seed;
                    result[seedCount++] = seed ^ 0x80000000;
                } while ((lowBits += IV_LAG_1) < 0x10000);
            }
        }

        // PKHeX PKHeX.Core/Legality/RNG/Algorithms/LCRNGReversal.cs GetSeeds
        int LCRNGReversal::getSeeds(std::span<uint32_t> result, uint32_t first, uint32_t second) noexcept
        {
            const uint32_t lattice = ((second * LCRNG::REVERSE_MULTIPLIER - first) >> 16) * PID_LAG_0;
            const uint32_t lowerEstimate = (lattice + PID_LOWER_BOUND) >> 16;
            const uint32_t upperEstimate = (lattice + PID_UPPER_BOUND) >> 16;
            if (lowerEstimate != upperEstimate)
            {
                return 0;
            }

            int seedCount = 0;
            uint32_t lowBits = (lowerEstimate * PID_LAG_1) % PID_LAG_0;
            do
            {
                const uint32_t seed = LCRNG::previous(second | lowBits);
                if ((seed & 0xFFFF0000) == first)
                {
                    result[seedCount++] = LCRNG::previous(seed);
                }
            } while ((lowBits += PID_LAG_0) < 0x10000);
            return seedCount;
        }

        // PKHeX PKHeX.Core/Legality/RNG/Algorithms/LCRNGReversal.cs GetSeedsIVs
        int LCRNGReversal::getSeedsIVs(std::span<uint32_t> result, uint32_t first, uint32_t second) noexcept
        {
            const uint32_t lattice = ((second - LCRNG::MULTIPLIER * first) >> 16) * IV_LAG_1;
            const uint32_t lowerEstimate = ((lattice + IV_LOWER_BOUND) >> 15) * IV_LAG_0;
            const uint32_t middleEstimate = lowerEstimate + IV_LAG_0;
            const uint32_t upperEstimate = ((lattice + IV_UPPER_BOUND) >> 15) * IV_LAG_0;

            int seedCount = 0;
            addSeeds(result, lowerEstimate, first, second, seedCount);
            addSeeds(result, middleEstimate, first, second, seedCount);
            if (middleEstimate != upperEstimate)
            {
                addSeeds(result, upperEstimate, first, second, seedCount);
            }
            return seedCount;
        }

        int LCRNGReversal::getSeedsIVs(std::span<uint32_t> result, uint32_t hpIV, uint32_t attackIV, uint32_t defenseIV, uint32_t specialAttackIV, uint32_t specialDefenseIV, uint32_t speedIV) noexcept
        {
            const uint32_t first = (hpIV | (attackIV << 5) | (defenseIV << 10)) << 16;
            const uint32_t second = (speedIV | (specialAttackIV << 5) | (specialDefenseIV << 10)) << 16;
            return getSeedsIVs(result, first, second);
        }
    }
}
