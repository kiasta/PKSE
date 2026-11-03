/**
 * Recovers the LCRNG seeds that produce two consecutive 16-bit results (a PID's halves) or two
 * consecutive 15-bit results (an IV spread's halves), without walking the 2^32 seed space.
 *
 * Ported from PKHeX PKHeX.Core/Legality/RNG/Algorithms/LCRNGReversal.cs, which follows
 * StarfBerry's PokeRNG LCG_Recovery. Every seed written is the one BEFORE the first result.
 */
#ifndef LEGALITY_RNG_LCRNGREVERSAL_H
#define LEGALITY_RNG_LCRNGREVERSAL_H

#include <cstdint>
#include <span>

namespace Legality
{
    namespace RNG
    {
        struct LCRNGReversal
        {
            /// Seeds whose next two upper halves are `first` and `second` (both already << 16).
            /// `result` must hold LCRNG::MAX_PID_SEED_COUNT; returns how many were written.
            static int getSeeds(std::span<uint32_t> result, uint32_t first, uint32_t second) noexcept;

            /// Seeds whose next two 15-bit results are `first` and `second` (both already << 16).
            /// `result` must hold LCRNG::MAX_IV_SEED_COUNT; returns how many were written.
            static int getSeedsIVs(std::span<uint32_t> result, uint32_t first, uint32_t second) noexcept;

            static int getSeedsIVs(std::span<uint32_t> result, uint32_t hpIV, uint32_t attackIV, uint32_t defenseIV, uint32_t specialAttackIV, uint32_t specialDefenseIV, uint32_t speedIV) noexcept;
        };
    }
}

#endif
