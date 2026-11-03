#include "Legality/RNG/ChannelJirachi.h"

namespace Legality
{
    namespace RNG
    {
        namespace
        {
            // Bits 1-3 set: patterns 1, 2 and 3 all seen (bit 0 is pattern 0, which is skipped).
            constexpr uint32_t BIT_PATTERN_123 = 0b1110;
            constexpr uint32_t QUARTER_THRESHOLD = 0x4000;
            constexpr uint32_t THIRD_THRESHOLD = 0x547A;
        }

        bool ChannelJirachi::isPossible(uint32_t seed) noexcept
        {
            return getPossible(seed).pattern != ChannelJirachiRandomResult::None;
        }

        ChannelJirachiPossibility ChannelJirachi::getPossible(uint32_t seed) noexcept
        {
            for (uint32_t branchIndex = 0; branchIndex < 3; branchIndex++)
            {
                uint32_t origin = seed;
                if (!isValid(origin, branchIndex))
                {
                    continue;
                }
                return ChannelJirachiPossibility{static_cast<ChannelJirachiRandomResult>(branchIndex + 1), origin};
            }
            return ChannelJirachiPossibility{};
        }

        bool ChannelJirachi::isValid(uint32_t &origin, uint32_t possibleBranch) noexcept
        {
            if (!isValidAccept(origin, possibleBranch))
            {
                return false;
            }
            if (!isValidMenu(origin))
            {
                return false;
            }
            origin = XDRNG::previous(origin);
            return true;
        }

        // PKHeX PKHeX.Core/Legality/RNG/ClassicEra/Gen3/ChannelJirachi.cs IsValidAccept: either
        // check passing costs one call, both failing two.
        bool ChannelJirachi::isValidAccept(uint32_t &seed, uint32_t acceptPivot) noexcept
        {
            if (acceptPivot >= 3)
            {
                return false;
            }
            if (acceptPivot == 0)
            {
                seed = XDRNG::previous(seed);
                if (XDRNG::previous16(seed) <= THIRD_THRESHOLD)
                {
                    return false;
                }
                if (XDRNG::previous16(seed) <= QUARTER_THRESHOLD)
                {
                    return false;
                }
            }
            else if (acceptPivot == 1)
            {
                if (XDRNG::previous16(seed) > QUARTER_THRESHOLD)
                {
                    return false;
                }
            }
            else
            {
                if (XDRNG::previous16(seed) > THIRD_THRESHOLD)
                {
                    return false;
                }
                if (XDRNG::previous16(seed) <= QUARTER_THRESHOLD)
                {
                    return false;
                }
            }
            seed = XDRNG::previous5(seed);
            return true;
        }

        // The menu draws (seed >> 30) until 1, 2 and 3 have all appeared; walking back, the end
        // value reappearing before the other two means the menu would have stopped earlier.
        bool ChannelJirachi::isValidMenu(uint32_t &seed) noexcept
        {
            const uint32_t endPattern = seed >> 30;
            if (endPattern == 0)
            {
                return false;
            }

            uint32_t seenPatterns = 1u << endPattern;
            while (true)
            {
                seed = XDRNG::previous(seed);
                const uint32_t pattern = seed >> 30;
                if (pattern == endPattern)
                {
                    return false;
                }
                seenPatterns |= 1u << pattern;
                if (seenPatterns >= BIT_PATTERN_123)
                {
                    return true;
                }
            }
        }

        ChannelJirachiMenuPattern ChannelJirachi::getMenuPattern(uint32_t seed) noexcept
        {
            uint8_t patterns[3] = {0, 0, 0};
            int patternCount = 0;
            while (patternCount != 3)
            {
                seed = XDRNG::next(seed);
                const uint8_t pattern = static_cast<uint8_t>(seed >> 30);
                // The unfilled entries are 0, so a 0 always counts as seen.
                if (pattern == patterns[0] || pattern == patterns[1] || pattern == patterns[2])
                {
                    continue;
                }
                patterns[patternCount++] = pattern;
            }
            return ChannelJirachiMenuPattern{patterns[0], patterns[1], patterns[2], seed};
        }

        uint32_t ChannelJirachi::skipToPIDIV(uint32_t seed) noexcept
        {
            seed = skipMenuPattern(seed);
            seed = skipAccept(seed);
            return seed;
        }

        uint32_t ChannelJirachi::skipMenuPattern(uint32_t seed) noexcept
        {
            uint32_t seenPatterns = 0;
            while (true)
            {
                seed = XDRNG::next(seed);
                const uint8_t pattern = static_cast<uint8_t>(seed >> 30);
                seenPatterns |= 1u << pattern;
                if (seenPatterns >= BIT_PATTERN_123)
                {
                    return seed;
                }
            }
        }

        uint32_t ChannelJirachi::skipAccept(uint32_t seed) noexcept
        {
            seed = XDRNG::next4(seed);
            uint32_t randomValue = XDRNG::next16(seed);
            if (randomValue <= QUARTER_THRESHOLD)
            {
                return XDRNG::next(seed);
            }
            randomValue = XDRNG::next16(seed);
            if (randomValue <= THIRD_THRESHOLD)
            {
                return XDRNG::next(seed);
            }
            return XDRNG::next2(seed);
        }
    }
}
