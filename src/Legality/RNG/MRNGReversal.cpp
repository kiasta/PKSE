#include "Legality/RNG/MRNGReversal.h"

#include "Legality/RNG/LCRNG.h"
#include "Legality/RNG/MRNG.h"

namespace Legality
{
    namespace RNG
    {
        namespace
        {
            // Lattice constants from StarfBerry's PokeRNG LCG_Recovery.
            constexpr uint32_t LAG_0 = 0x6C31;
            constexpr uint32_t IV_LAG_1 = 0x2E90;
            constexpr uint32_t LOWER_BOUND = 0x30F18357;
            constexpr uint32_t UPPER_BOUND = 0x30F1AA11;

            void addSeeds(std::span<uint32_t> result, uint32_t lowBits, uint32_t first, uint32_t third, int &seedCount) noexcept
            {
                do
                {
                    uint32_t seed = MRNG::previous2(third | lowBits);
                    if ((seed & 0x7FFF0000) != first)
                    {
                        continue;
                    }
                    seed = MRNG::previous(seed);
                    result[seedCount++] = seed;
                    result[seedCount++] = seed ^ 0x80000000;
                } while ((lowBits += LAG_0) < 0x10000);
            }
        }

        // PKHeX PKHeX.Core/Legality/RNG/Algorithms/MRNGReversal.cs GetSeedsIVs
        int MRNGReversal::getSeedsIVs(std::span<uint32_t> result, uint32_t first, uint32_t third) noexcept
        {
            const uint32_t lattice = ((first - (third * MRNG::REVERSE_MULTIPLIER_2)) >> 16) * LAG_0;
            const uint32_t lowerEstimate = (lattice + LOWER_BOUND) >> 15;
            const uint32_t upperEstimate = (lattice + UPPER_BOUND) >> 15;

            int seedCount = 0;
            addSeeds(result, (lowerEstimate * IV_LAG_1) % LAG_0, first, third, seedCount);
            if (lowerEstimate != upperEstimate)
            {
                addSeeds(result, (upperEstimate * IV_LAG_1) % LAG_0, first, third, seedCount);
            }
            return seedCount;
        }

        // The Ranch draws Speed/Special Attack/Special Defense first, HP/Attack/Defense third.
        int MRNGReversal::getSeedsIVs(std::span<uint32_t> result, uint32_t hpIV, uint32_t attackIV, uint32_t defenseIV, uint32_t specialAttackIV, uint32_t specialDefenseIV, uint32_t speedIV) noexcept
        {
            const uint32_t first = (speedIV | (specialAttackIV << 5) | (specialDefenseIV << 10)) << 16;
            const uint32_t third = (hpIV | (attackIV << 5) | (defenseIV << 10)) << 16;
            return getSeedsIVs(result, first, third);
        }

        bool MRNGReversal::hasSeeds(uint32_t hpIV, uint32_t attackIV, uint32_t defenseIV, uint32_t specialAttackIV, uint32_t specialDefenseIV, uint32_t speedIV) noexcept
        {
            const uint32_t third = (hpIV | (attackIV << 5) | (defenseIV << 10)) << 16;
            const uint32_t first = (speedIV | (specialAttackIV << 5) | (specialDefenseIV << 10)) << 16;
            return hasSeeds(first, third);
        }

        // PKHeX passes the unshifted 15-bit halves here, exactly as written.
        bool MRNGReversal::hasSeeds(uint32_t packedIVs) noexcept
        {
            const uint32_t third = packedIVs & 0x7FFF;
            const uint32_t first = (packedIVs >> 15) & 0x7FFF;
            return hasSeeds(first, third);
        }

        bool MRNGReversal::hasSeeds(uint32_t first, uint32_t third) noexcept
        {
            uint32_t seeds[LCRNG::MAX_IV_SEED_COUNT];
            return getSeedsIVs(seeds, first, third) != 0;
        }
    }
}
