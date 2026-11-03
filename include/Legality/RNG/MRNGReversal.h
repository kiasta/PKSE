/**
 * Recovers the MRNG seeds behind an IV spread whose halves are one call apart (My Pokemon Ranch).
 *
 * Ported from PKHeX PKHeX.Core/Legality/RNG/Algorithms/MRNGReversal.cs.
 */
#ifndef LEGALITY_RNG_MRNGREVERSAL_H
#define LEGALITY_RNG_MRNGREVERSAL_H

#include <cstdint>
#include <span>

namespace Legality
{
    namespace RNG
    {
        struct MRNGReversal
        {
            /// Seeds whose first and third 15-bit results are `first` and `third` (both already << 16).
            static int getSeedsIVs(std::span<uint32_t> result, uint32_t first, uint32_t third) noexcept;

            static int getSeedsIVs(std::span<uint32_t> result, uint32_t hpIV, uint32_t attackIV, uint32_t defenseIV, uint32_t specialAttackIV, uint32_t specialDefenseIV, uint32_t speedIV) noexcept;

            static bool hasSeeds(uint32_t hpIV, uint32_t attackIV, uint32_t defenseIV, uint32_t specialAttackIV, uint32_t specialDefenseIV, uint32_t speedIV) noexcept;

            /// `packedIVs` holds HP/Attack/Defense in its low 15 bits and Speed/Special Attack/Special
            /// Defense in the next 15.
            static bool hasSeeds(uint32_t packedIVs) noexcept;

            static bool hasSeeds(uint32_t first, uint32_t third) noexcept;
        };
    }
}

#endif
