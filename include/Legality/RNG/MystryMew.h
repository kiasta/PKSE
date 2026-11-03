/**
 * The Mystery Mew event: 86 restricted 16-bit seeds, each the first of five Mew a distribution
 * machine gave out back to back (the next ones are 5 LCRNG calls apart).
 *
 * Ported from PKHeX PKHeX.Core/Legality/RNG/ClassicEra/Gen3/MystryMew.cs.
 */
#ifndef LEGALITY_RNG_MYSTRYMEW_H
#define LEGALITY_RNG_MYSTRYMEW_H

#include <cstdint>

#include "Legality/RNG/PIDType.h"

namespace Legality
{
    namespace RNG
    {
        struct MystryMewSeedIndexes
        {
            int seedIndex;
            int subIndex;
        };

        struct MystryMew
        {
            /// Of the seeds that were released, 0x6065 kept only its second sibling.
            static bool isUnreleased(uint32_t seed, int subIndex) noexcept;
            static constexpr bool isRestrictedIndex0th(int seedIndex) noexcept { return seedIndex == 0; }

            static uint32_t getSeed(uint32_t random, PIDType type = PIDType::BACDUnrestricted) noexcept;
            /// The restricted seed at `index`, or the first one when out of range.
            static uint32_t getSeed(int seedIndex) noexcept;

            static bool isValidSeed(uint32_t seed) noexcept { return getSeedIndex(seed) != -1; }
            static bool isValidSeed(uint32_t seed, int subIndex) noexcept { return !isUnreleased(seed, subIndex); }

            static int getSeedIndex(uint32_t seed) noexcept;
            static MystryMewSeedIndexes getSeedIndexes(uint32_t seed) noexcept;
        };
    }
}

#endif
