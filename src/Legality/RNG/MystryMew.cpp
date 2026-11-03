#include "Legality/RNG/MystryMew.h"

#include <algorithm>
#include <iterator>

#include "Legality/RNG/LCRNG.h"

namespace Legality
{
    namespace RNG
    {
        namespace
        {
            constexpr uint16_t SEEDS[] = {
                0x0652, 0x0932, 0x0C13, 0x0D43, 0x0EEE, 0x1263, 0x13C9, 0x1614, 0x1C09, 0x1EA5, 0x20BF, 0x2389, 0x2939, 0x302D, 0x306E, 0x34F3, 0x45F3, 0x46CE, 0x4A0D, 0x4B63, 0x4C79, 0x508E, 0x50AB, 0x5240, 0x5327, 0x56BA, 0x56CC, 0x5841, 0x5A60,
                0x5BC1, 0x5E2B, 0x5EF3, 0x6065, 0x643F, 0x6457, 0x67A3, 0x6944, 0x6E06, 0x6E62, 0x7667, 0x77EF, 0x78D2, 0x8655, 0x8A92, 0x8B48, 0x93D0, 0x941D, 0x95A0, 0x967D, 0x9690, 0x9C37, 0x9C40, 0x9D9C, 0x9DE4, 0x9E86, 0xA153, 0xA443, 0xA8AC,
                0xAC08, 0xAFFB, 0xB1F2, 0xB831, 0xBE96, 0xC2D4, 0xC385, 0xC6CE, 0xC92C, 0xC953, 0xC962, 0xCC43, 0xCD47, 0xCD96, 0xD1E4, 0xDFED, 0xE62C, 0xE6CC, 0xE90A, 0xE95D, 0xE991, 0xEBB2, 0xEE7F, 0xEE9F, 0xEFC8, 0xF0E4, 0xFE4E, 0xFE9D,
            };
            constexpr int SEED_COUNT = static_cast<int>(std::size(SEEDS));

            constexpr int MEW_PER_RESTRICTED_SEED = 5;
            constexpr int RESTRICTED_INDEX = 0;
            constexpr uint16_t RELEASED_SEED = 0x6065;
            constexpr int RELEASED_SEED_KEPT_INDEX = 2;
            // next5(next5(RELEASED_SEED))
            constexpr uint32_t RELEASED_SEED_KEPT_SEED = 0xE8D1A0BF;

            // PKHeX ReadOnlySpan.BinarySearch: the index, or a negative value when absent.
            int binarySearch(uint16_t lowSeed) noexcept
            {
                const uint16_t *found = std::lower_bound(std::begin(SEEDS), std::end(SEEDS), lowSeed);
                if (found == std::end(SEEDS) || *found != lowSeed)
                {
                    return ~static_cast<int>(found - std::begin(SEEDS));
                }
                return static_cast<int>(found - std::begin(SEEDS));
            }
        }

        bool MystryMew::isUnreleased(uint32_t seed, int subIndex) noexcept
        {
            return seed == RELEASED_SEED && subIndex != RELEASED_SEED_KEPT_INDEX;
        }

        uint32_t MystryMew::getSeed(uint32_t random, PIDType type) noexcept
        {
            const uint32_t restricted = random % static_cast<uint32_t>(SEED_COUNT);
            uint32_t seed = SEEDS[restricted];
            if (seed == RELEASED_SEED)
            {
                return RELEASED_SEED_KEPT_SEED;
            }
            if (type == PIDType::BACDRestricted)
            {
                return seed;
            }
            const uint32_t position = (random % (MEW_PER_RESTRICTED_SEED - 1)) + 1;
            for (uint32_t i = 0; i < position; i++)
            {
                seed = LCRNG::next5(seed);
            }
            return seed;
        }

        uint32_t MystryMew::getSeed(int seedIndex) noexcept
        {
            return SEEDS[static_cast<uint32_t>(seedIndex) >= static_cast<uint32_t>(SEED_COUNT) ? 0 : seedIndex];
        }

        int MystryMew::getSeedIndex(uint32_t seed) noexcept
        {
            if (seed <= 0xFFFF)
            {
                return binarySearch(static_cast<uint16_t>(seed));
            }
            for (int i = 0; i < MEW_PER_RESTRICTED_SEED; i++)
            {
                seed = LCRNG::previous5(seed);
                if (seed <= 0xFFFF)
                {
                    return binarySearch(static_cast<uint16_t>(seed));
                }
            }
            return -1;
        }

        MystryMewSeedIndexes MystryMew::getSeedIndexes(uint32_t seed) noexcept
        {
            if (seed <= 0xFFFF)
            {
                return MystryMewSeedIndexes{binarySearch(static_cast<uint16_t>(seed)), RESTRICTED_INDEX};
            }
            for (int i = RESTRICTED_INDEX + 1; i < RESTRICTED_INDEX + MEW_PER_RESTRICTED_SEED; i++)
            {
                seed = LCRNG::previous5(seed);
                if (seed <= 0xFFFF)
                {
                    return MystryMewSeedIndexes{binarySearch(static_cast<uint16_t>(seed)), i};
                }
            }
            return MystryMewSeedIndexes{-1, -1};
        }
    }
}
