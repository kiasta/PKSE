#include "Legality/RNG/ClassicEraRNG.h"

#include <algorithm>

#include "Legality/RNG/LCRNG.h"
#include "Legality/RNG/LCRNGReversal.h"

namespace Legality
{
    namespace RNG
    {
        namespace
        {
            constexpr uint32_t MAX_MINUTE_SECOND_BONUS = 59 + 59;
            // Delays below the Heart Gold/Soul Silver boot minimum, or past 6000 frames, would take
            // longer than anyone waits to launch the game.
            constexpr uint32_t BASE_DELAY = 400;
            constexpr uint32_t MAX_DELAY = 6000;
            constexpr uint32_t MAX_SEEK_DISTANCE = 5000;

            struct MinuteSecond
            {
                uint8_t minute;
                uint8_t second;
            };

            // Prefer a seconds value of at least 7 and at most 15 where the sum allows.
            MinuteSecond getMinuteSecond(uint8_t delta) noexcept
            {
                constexpr uint8_t minimumSecond = 7;
                constexpr uint8_t maximumSecond = 15;
                uint8_t minute = 0;
                uint8_t second = 0;
                if (delta < 59 + maximumSecond)
                {
                    if (delta <= minimumSecond)
                    {
                        second = delta;
                    }
                    else if (delta <= minimumSecond + 59)
                    {
                        second = static_cast<uint8_t>(std::max(static_cast<int>(minimumSecond), delta - 59));
                    }
                    else
                    {
                        second = maximumSecond;
                    }
                    minute = static_cast<uint8_t>(delta - second);
                }
                else
                {
                    minute = 59;
                    second = static_cast<uint8_t>(delta - 59);
                }
                return MinuteSecond{minute, second};
            }
        }

        uint32_t InitialSeedComponents4::toSeed() const noexcept
        {
            return ClassicEraRNG::getInitialSeed(year, month, day, hour, minute, second, delay);
        }

        uint32_t ClassicEraRNG::getChainShinyPID(uint32_t &seed, uint32_t ID32) noexcept
        {
            uint32_t lowHalf = LCRNG::next16(seed) & 7;
            uint32_t highHalf = LCRNG::next16(seed) & 7;
            for (int bitIndex = 3; bitIndex < 16; bitIndex++)
            {
                lowHalf |= (LCRNG::next16(seed) & 1) << bitIndex;
            }

            const uint16_t TID16 = static_cast<uint16_t>(ID32 & 0xFFFF);
            const uint16_t SID16 = static_cast<uint16_t>(ID32 >> 16);
            highHalf = ((lowHalf ^ TID16 ^ SID16) & 0xFFF8) | (highHalf & 0x7);
            return (highHalf << 16) | lowHalf;
        }

        uint32_t ClassicEraRNG::getSequentialPID(uint32_t &seed) noexcept
        {
            const uint32_t firstRandom = LCRNG::next16(seed);
            const uint32_t secondRandom = LCRNG::next16(seed);
            return (secondRandom << 16) | firstRandom;
        }

        uint32_t ClassicEraRNG::getSequentialPIDFrom(uint32_t seed) noexcept
        {
            return getSequentialPID(seed);
        }

        uint32_t ClassicEraRNG::getReversePID(uint32_t &seed) noexcept
        {
            const uint32_t firstRandom = LCRNG::next16(seed);
            const uint32_t secondRandom = LCRNG::next16(seed);
            return (secondRandom << 16) | firstRandom;
        }

        uint32_t ClassicEraRNG::getReversePIDFrom(uint32_t seed) noexcept
        {
            const uint32_t firstRandom = LCRNG::next16(seed);
            const uint32_t secondRandom = LCRNG::next16(seed);
            return (firstRandom << 16) | secondRandom;
        }

        uint32_t ClassicEraRNG::getSequentialIVs(uint32_t &seed) noexcept
        {
            const uint32_t firstRandom = LCRNG::next15(seed);
            const uint32_t secondRandom = LCRNG::next15(seed);
            return (secondRandom << 15) | firstRandom;
        }

        uint32_t ClassicEraRNG::getInitialSeed(uint32_t year, uint32_t month, uint32_t day, uint32_t hour, uint32_t minute, uint32_t second, uint32_t delay) noexcept
        {
            const uint8_t dateTimeByte = static_cast<uint8_t>((month * day) + minute + second);
            const uint8_t hourByte = static_cast<uint8_t>(hour);
            return ((static_cast<uint32_t>(dateTimeByte) << 24) | (static_cast<uint32_t>(hourByte) << 16)) + delay + year - 2000u;
        }

        // PKHeX PKHeX.Core/Legality/RNG/ClassicEra/ClassicEraRNG.cs SeekInitialSeed
        uint32_t ClassicEraRNG::seekInitialSeed(uint32_t year, uint32_t month, uint32_t day, uint32_t seed) noexcept
        {
            while (!isInitialSeed(year, month, day, seed))
            {
                seed = LCRNG::previous(seed);
            }
            const std::optional<InitialSeedComponents4> decomposed = decomposeSeed(seed, year, month, day);

            uint32_t previousSeed = LCRNG::previous(seed);
            while (!isInitialSeed(year, month, day, previousSeed))
            {
                previousSeed = LCRNG::previous(previousSeed);
            }

            const uint32_t distance = LCRNG::getDistance(previousSeed, seed);
            if (distance > MAX_SEEK_DISTANCE)
            {
                return seed;
            }

            const std::optional<InitialSeedComponents4> previousDecomposed = decomposeSeed(previousSeed, year, month, day);
            if (decomposed && previousDecomposed && previousDecomposed->delay < decomposed->delay)
            {
                return previousSeed;
            }
            return seed;
        }

        // PKHeX PKHeX.Core/Legality/RNG/ClassicEra/ClassicEraRNG.cs IsInitialSeed: the top byte is
        // compared mod 256, the way the game's byte arithmetic overflows.
        bool ClassicEraRNG::isInitialSeed(uint32_t year, uint32_t month, uint32_t day, uint32_t seed) noexcept
        {
            const uint8_t hourByte = static_cast<uint8_t>((seed >> 16) & 0xFF);
            if (hourByte > 23)
            {
                return false;
            }

            const uint8_t topByte = static_cast<uint8_t>(seed >> 24);
            const uint8_t dateProduct = static_cast<uint8_t>(month * day);
            if (static_cast<uint8_t>(topByte - dateProduct) > MAX_MINUTE_SECOND_BONUS)
            {
                return false;
            }

            const uint32_t minimumDelay = BASE_DELAY + (year - 2000u);
            const uint16_t delayComponent = static_cast<uint16_t>(seed - minimumDelay);
            return delayComponent <= MAX_DELAY;
        }

        std::optional<InitialSeedComponents4> ClassicEraRNG::decomposeSeed(uint32_t seed, uint32_t year, uint32_t month, uint32_t day) noexcept
        {
            const uint8_t hourByte = static_cast<uint8_t>((seed >> 16) & 0xFF);
            if (hourByte > 23)
            {
                return std::nullopt;
            }

            const uint8_t topByte = static_cast<uint8_t>(seed >> 24);
            const uint8_t dateProduct = static_cast<uint8_t>(month * day);
            const uint8_t delta = static_cast<uint8_t>(topByte - dateProduct);
            if (delta > MAX_MINUTE_SECOND_BONUS)
            {
                return std::nullopt;
            }

            const uint8_t yearComponent = static_cast<uint8_t>(year - 2000u);
            const MinuteSecond minuteSecond = getMinuteSecond(delta);
            InitialSeedComponents4 components;
            components.year = yearComponent;
            components.month = static_cast<uint8_t>(month);
            components.day = static_cast<uint8_t>(day);
            components.hour = hourByte;
            components.delay = static_cast<uint16_t>(static_cast<uint16_t>(seed) - yearComponent);
            components.minute = minuteSecond.minute;
            components.second = minuteSecond.second;
            return components;
        }

        uint32_t ClassicEraRNG::seekInitialSeedForIVs(std::span<const int> individualValues, uint32_t year, uint32_t month, uint32_t day, uint32_t &origin) noexcept
        {
            origin = 0;
            uint32_t bestDistance = UINT32_MAX;

            uint32_t seeds[LCRNG::MAX_IV_SEED_COUNT];
            const int seedCount = LCRNGReversal::getSeedsIVs(seeds, static_cast<uint32_t>(individualValues[0]), static_cast<uint32_t>(individualValues[1]), static_cast<uint32_t>(individualValues[2]), static_cast<uint32_t>(individualValues[4]), static_cast<uint32_t>(individualValues[5]), static_cast<uint32_t>(individualValues[3]));
            if (seedCount == 0)
            {
                return 0;
            }

            uint32_t bestSeed = 0;
            for (int seedIndex = 0; seedIndex < seedCount; seedIndex++)
            {
                const uint32_t seed = seeds[seedIndex];
                const uint32_t initialSeed = seekInitialSeed(year, month, day, seed);
                const uint32_t distance = LCRNG::getDistance(initialSeed, seed);
                if (distance > bestDistance)
                {
                    continue;
                }
                bestDistance = distance;
                bestSeed = initialSeed;
                origin = seed;
            }
            return bestSeed;
        }
    }
}
