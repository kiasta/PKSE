/**
 * LCRNGReversal for two results with one unused call between them (the Method 3 / "A_C" pattern).
 *
 * Ported from PKHeX PKHeX.Core/Legality/RNG/Algorithms/LCRNGReversalSkip.cs.
 */
#ifndef LEGALITY_RNG_LCRNGREVERSALSKIP_H
#define LEGALITY_RNG_LCRNGREVERSALSKIP_H

#include <cstdint>
#include <span>

namespace Legality
{
    namespace RNG
    {
        struct LCRNGReversalSkip
        {
            /// Seeds whose first and third upper halves are `first` and `third` (both already << 16).
            static int getSeeds(std::span<uint32_t> result, uint32_t first, uint32_t third) noexcept;

            /// Seeds whose first and third 15-bit results are `first` and `third` (both already << 16).
            static int getSeedsIVs(std::span<uint32_t> result, uint32_t first, uint32_t third) noexcept;

            static int getSeedsIVs(std::span<uint32_t> result, uint32_t hpIV, uint32_t attackIV, uint32_t defenseIV, uint32_t specialAttackIV, uint32_t specialDefenseIV, uint32_t speedIV) noexcept;
        };
    }
}

#endif
