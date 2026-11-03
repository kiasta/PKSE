/**
 * The Pokemon Center Japan Fifth Anniversary event's weighted table (Pichu, Bagon, Absol and Ralts,
 * each with or without Wish, 125/1000 apiece) and the Gen 3 weighted-roll arithmetic it uses.
 *
 * Ported from PKHeX PKHeX.Core/Legality/RNG/ClassicEra/Gen3/PCJPFifthAnniversary.cs and
 * WeightedTable3.cs.
 */
#ifndef LEGALITY_RNG_PCJPFIFTHANNIVERSARY_H
#define LEGALITY_RNG_PCJPFIFTHANNIVERSARY_H

#include <cstdint>

#include "Legality/RNG/LCRNG.h"

namespace Legality
{
    namespace RNG
    {
        struct WeightedTable3
        {
            /// Reduces a 32-bit roll to [0, maximum).
            static constexpr uint32_t getPeriodicWeight(uint32_t random, uint32_t maximum) noexcept
            {
                const uint32_t high = random >> 16;
                const uint32_t first = ((high << 2) & 0xFFFF) + high;
                uint32_t second = ((random & 0xFFFF) << 1) + (first >> 16);
                second += high + (second >> 16);
                return (maximum * (second & 0xFFFF)) >> 16;
            }

            /// Two calls, the first the high half.
            static constexpr uint32_t getRandom32(uint32_t &seed) noexcept
            {
                const uint32_t firstRandom = LCRNG::next16(seed);
                const uint32_t secondRandom = LCRNG::next16(seed);
                return (firstRandom << 16) | secondRandom;
            }

            static constexpr uint32_t getRandom32From(uint32_t seed) noexcept { return getRandom32(seed); }
        };

        struct PCJPFifthAnniversaryResult
        {
            uint32_t giftIndex;
            bool wish;
            bool shiny;
        };

        struct PCJPFifthAnniversary
        {
            static PCJPFifthAnniversaryResult getResult(uint16_t random) noexcept;

            /// Pichu 0, Bagon 1, Absol 2, Ralts 3: bits 2 and 3 of the species id, plus one.
            static constexpr uint32_t getIndex(uint16_t species) noexcept { return ((static_cast<uint32_t>(species) >> 2) + 1) & 3u; }

            static bool isMatch(uint16_t species, bool shiny, bool wish, uint16_t random) noexcept;

            static uint16_t getSeedForResult(uint16_t species, bool shiny, bool wish, uint32_t seed) noexcept;
        };
    }
}

#endif
