/**
 * The Wishing Star (Wishmkr) Jirachi bonus disc: one fixed 16-bit seed per nature it can give, and
 * the held item the seed rolls.
 *
 * Ported from PKHeX PKHeX.Core/Legality/RNG/ClassicEra/Gen3/Wishmkr.cs.
 */
#ifndef LEGALITY_RNG_WISHMKR_H
#define LEGALITY_RNG_WISHMKR_H

#include <cstdint>
#include <iterator>

#include "Legality/RNG/LCRNG.h"

namespace Legality
{
    namespace RNG
    {
        struct Wishmkr
        {
            static constexpr uint16_t TRAINER_ID = 20043;

            /// Indexed by nature; 0 for a nature the disc cannot give.
            static constexpr uint16_t SEEDS[] = {
                0x0000, 0x7236, 0x0000, 0x0000, 0xA030, 0x0000, 0xECDD, 0x0000, 0x0000, 0x0000, 0x7360, 0x9359, 0x3D60, 0xCF37, 0x0000, 0x0000, 0x0000, 0x0000, 0x353D, 0x0000, 0x0000, 0x0000, 0x0000, 0xF500,
            };

            /// Lonely, Naughty, Docile, Timid, Hasty, Serious, Jolly, Bashful, Careful.
            static constexpr uint16_t ALL_NINE[] = {0x7236, 0xA030, 0xECDD, 0x7360, 0x9359, 0x3D60, 0xCF37, 0x353D, 0xF500};

            static constexpr bool tryGetSeed(uint8_t nature, uint16_t &seed) noexcept
            {
                if (nature >= std::size(SEEDS))
                {
                    seed = 0;
                    return false;
                }
                seed = SEEDS[nature];
                return seed != 0;
            }

            /// Salac Berry (170) or Ganlon Berry (169).
            static constexpr uint8_t getHeldItem(uint32_t random16) noexcept { return static_cast<uint8_t>(170 - ((random16 / 3) & 1)); }

            static constexpr uint8_t getHeldItemFromSeed(uint16_t seed) noexcept { return getHeldItem(LCRNG::next5(seed) >> 16); }
        };
    }
}

#endif
