#include "Legality/RNG/PCJPFifthAnniversary.h"

namespace Legality
{
    namespace RNG
    {
        namespace
        {
            constexpr uint32_t MAX_TABLE_WEIGHT = 1000;
            constexpr uint32_t ENTRY_WEIGHT = 125;

            // Species index is tableWeight / 250, the Wish moveset is odd eighths, and a Pichu is shiny for (tableWeight % 125) >= 100.
            PCJPFifthAnniversaryResult getResultFrom32(uint32_t random) noexcept
            {
                const uint32_t tableWeight = WeightedTable3::getPeriodicWeight(random, MAX_TABLE_WEIGHT);
                const uint32_t eighth = tableWeight / ENTRY_WEIGHT;
                const bool wish = (eighth & 1) == 1;
                const uint32_t giftIndex = eighth >> 1;
                const bool shiny = giftIndex == 0 && (tableWeight % ENTRY_WEIGHT) >= 100;
                return PCJPFifthAnniversaryResult{giftIndex, wish, shiny};
            }
        }

        PCJPFifthAnniversaryResult PCJPFifthAnniversary::getResult(uint16_t random) noexcept
        {
            return getResultFrom32(WeightedTable3::getRandom32From(random));
        }

        bool PCJPFifthAnniversary::isMatch(uint16_t species, bool shiny, bool wish, uint16_t random) noexcept
        {
            const uint32_t giftIndex = getIndex(species);
            const PCJPFifthAnniversaryResult result = getResult(random);
            return giftIndex == result.giftIndex && wish == result.wish && shiny == result.shiny;
        }

        uint16_t PCJPFifthAnniversary::getSeedForResult(uint16_t species, bool shiny, bool wish, uint32_t seed) noexcept
        {
            while (true)
            {
                const uint16_t restricted = static_cast<uint16_t>(seed);
                if (isMatch(species, shiny, wish, restricted))
                {
                    return restricted;
                }
                seed = LCRNG::next(seed);
            }
        }
    }
}
