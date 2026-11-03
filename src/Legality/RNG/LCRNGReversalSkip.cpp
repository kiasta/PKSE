#include "Legality/RNG/LCRNGReversalSkip.h"

#include "Legality/RNG/LCRNG.h"

namespace Legality
{
    namespace RNG
    {
        namespace
        {
            // Lattice constants from StarfBerry's PokeRNG LCG_Recovery.
            constexpr uint32_t LAG_0 = 0x6C31;
            constexpr uint32_t PID_LAG_1 = 0xF11;
            constexpr uint32_t IV_LAG_1 = 0x2E90;
            constexpr uint32_t PID_LOWER_BOUND = 0x20A4F729;
            constexpr uint32_t PID_UPPER_BOUND = 0x20A49DE2;
            constexpr uint32_t IV_LOWER_BOUND = 0x1574621D;
            constexpr uint32_t IV_UPPER_BOUND = 0x157488D6;

            void addSeeds(std::span<uint32_t> result, uint32_t lowBits, uint32_t first, uint32_t third, int &seedCount) noexcept
            {
                do
                {
                    uint32_t seed = LCRNG::previous2(third | lowBits);
                    if ((seed & 0x7FFF0000) != first)
                    {
                        continue;
                    }
                    seed = LCRNG::previous(seed);
                    result[seedCount++] = seed;
                    result[seedCount++] = seed ^ 0x80000000;
                } while ((lowBits += LAG_0) < 0x10000);
            }
        }

        // PKHeX PKHeX.Core/Legality/RNG/Algorithms/LCRNGReversalSkip.cs GetSeeds
        int LCRNGReversalSkip::getSeeds(std::span<uint32_t> result, uint32_t first, uint32_t third) noexcept
        {
            const uint32_t lattice = (((third * LCRNG::REVERSE_MULTIPLIER_2) - first) >> 16) * LAG_0;
            const uint32_t lowerEstimate = (lattice + PID_LOWER_BOUND) >> 16;
            const uint32_t upperEstimate = (lattice + PID_UPPER_BOUND) >> 16;
            if (lowerEstimate != upperEstimate)
            {
                return 0;
            }

            int seedCount = 0;
            uint32_t lowBits = (lowerEstimate * PID_LAG_1) % LAG_0;
            do
            {
                const uint32_t seed = LCRNG::previous2(third | lowBits);
                if ((seed & 0xFFFF0000) == first)
                {
                    result[seedCount++] = LCRNG::previous(seed);
                }
            } while ((lowBits += LAG_0) < 0x10000);
            return seedCount;
        }

        // PKHeX PKHeX.Core/Legality/RNG/Algorithms/LCRNGReversalSkip.cs GetSeedsIVs
        int LCRNGReversalSkip::getSeedsIVs(std::span<uint32_t> result, uint32_t first, uint32_t third) noexcept
        {
            const uint32_t lattice = ((first - (third * LCRNG::REVERSE_MULTIPLIER_2)) >> 16) * LAG_0;
            const uint32_t lowerEstimate = (lattice + IV_LOWER_BOUND) >> 15;
            const uint32_t upperEstimate = (lattice + IV_UPPER_BOUND) >> 15;

            int seedCount = 0;
            addSeeds(result, (lowerEstimate * IV_LAG_1) % LAG_0, first, third, seedCount);
            if (lowerEstimate != upperEstimate)
            {
                addSeeds(result, (upperEstimate * IV_LAG_1) % LAG_0, first, third, seedCount);
            }
            return seedCount;
        }

        int LCRNGReversalSkip::getSeedsIVs(std::span<uint32_t> result, uint32_t hpIV, uint32_t attackIV, uint32_t defenseIV, uint32_t specialAttackIV, uint32_t specialDefenseIV, uint32_t speedIV) noexcept
        {
            const uint32_t first = (hpIV | (attackIV << 5) | (defenseIV << 10)) << 16;
            const uint32_t third = (speedIV | (specialAttackIV << 5) | (specialDefenseIV << 10)) << 16;
            return getSeedsIVs(result, first, third);
        }
    }
}
