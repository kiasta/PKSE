/**
 * Toxtricity's form-linked natures: Amped rolls from one nature list and Low Key from the other.
 *
 * Ported from PKHeX PKHeX.Core/Legality/RNG/Util/ToxtricityUtil.cs.
 */
#ifndef LEGALITY_RNG_TOXTRICITYUTIL_H
#define LEGALITY_RNG_TOXTRICITYUTIL_H

#include <cstdint>
#include <iterator>

#include "Legality/RNG/Xoroshiro128Plus.h"

namespace Legality
{
    namespace RNG
    {
        struct ToxtricityUtil
        {
            static constexpr uint8_t AMPED_NATURES[] = {3, 4, 2, 8, 9, 19, 22, 11, 13, 14, 0, 6, 24};
            static constexpr uint8_t LOW_KEY_NATURES[] = {1, 5, 7, 10, 12, 15, 16, 17, 18, 20, 21, 23};

            static constexpr uint8_t getRandomNature(Xoroshiro128Plus &random, uint8_t form) noexcept
            {
                if (form == 0)
                {
                    return AMPED_NATURES[random.nextInt(std::size(AMPED_NATURES))];
                }
                return LOW_KEY_NATURES[random.nextInt(std::size(LOW_KEY_NATURES))];
            }

            /// The form (0 Amped, 1 Low Key) a nature evolves Toxel into.
            static constexpr int getAmpLowKeyResult(uint8_t nature) noexcept
            {
                const uint32_t natureIndex = static_cast<uint32_t>(nature) - 1;
                if (natureIndex > 22)
                {
                    return 0;
                }
                return (0b010110111100101001010001 >> natureIndex) & 1;
            }

            static constexpr uint8_t getRandomNatureNeutral(uint8_t form, uint32_t random) noexcept { return static_cast<uint8_t>(6 * ((random & 1) | (static_cast<uint32_t>(form) << 1))); }

            /// Hardy for Amped, Serious for Low Key.
            static constexpr uint8_t getRandomNatureNeutral(uint8_t form) noexcept { return form == 0 ? 0 : 12; }
        };
    }
}

#endif
