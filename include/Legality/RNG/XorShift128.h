/**
 * The xorshift128 RNG of Brilliant Diamond/Shining Pearl's Unity runtime (wild encounters).
 *
 * Ported from PKHeX PKHeX.Core/Legality/RNG/Algorithms/XorShift128.cs. The four 32-bit words are
 * PKHeX's x, y, z and w; the 64-bit view packs them little-endian, x low.
 */
#ifndef LEGALITY_RNG_XORSHIFT128_H
#define LEGALITY_RNG_XORSHIFT128_H

#include <cstdint>

namespace Legality
{
    namespace RNG
    {
        class XorShift128
        {
        public:
            constexpr explicit XorShift128(uint32_t seed) noexcept : word0(seed), word1((0x6C078965 * seed) + 1), word2((0x6C078965 * word1) + 1), word3((0x6C078965 * word2) + 1) {}

            constexpr XorShift128(uint64_t firstState, uint64_t secondState) noexcept : word0(static_cast<uint32_t>(firstState)), word1(static_cast<uint32_t>(firstState >> 32)), word2(static_cast<uint32_t>(secondState)), word3(static_cast<uint32_t>(secondState >> 32)) {}

            constexpr XorShift128(uint32_t firstWord, uint32_t secondWord, uint32_t thirdWord, uint32_t fourthWord) noexcept : word0(firstWord), word1(secondWord), word2(thirdWord), word3(fourthWord) {}

            constexpr uint64_t firstState() const noexcept { return (static_cast<uint64_t>(word1) << 32) | word0; }
            constexpr uint64_t secondState() const noexcept { return (static_cast<uint64_t>(word3) << 32) | word2; }
            constexpr bool equals(uint64_t firstStateValue, uint64_t secondStateValue) const noexcept { return firstState() == firstStateValue && secondState() == secondStateValue; }

            constexpr uint32_t next() noexcept
            {
                const uint32_t shifted = word0 ^ (word0 << 11);
                word0 = word1;
                word1 = word2;
                word2 = word3;
                word3 = word3 ^ (word3 >> 19) ^ shifted ^ (shifted >> 8);
                return word3;
            }

            constexpr uint32_t previous() noexcept
            {
                uint32_t restored = word3 ^ word2 ^ (word2 >> 19);
                restored ^= restored >> 8;
                restored ^= restored >> 16;
                restored ^= restored << 11;
                restored ^= restored << 22;
                word3 = word2;
                word2 = word1;
                word1 = word0;
                word0 = restored;
                return word3;
            }

            constexpr uint32_t nextUInt() noexcept { return static_cast<uint32_t>(nextInt()); }
            constexpr uint32_t nextUInt(uint32_t exclusiveMaximum) noexcept { return nextUInt() % exclusiveMaximum; }

            /// PKHeX NextInt: the range arithmetic wraps exactly as C#'s unchecked int does.
            constexpr int32_t nextInt(int32_t start = INT32_MIN, int32_t end = INT32_MAX) noexcept
            {
                const uint32_t randomValue = next();
                const uint32_t rangeSize = static_cast<uint32_t>(end) - static_cast<uint32_t>(start);
                return static_cast<int32_t>(static_cast<uint32_t>(start) + (randomValue % rangeSize));
            }

        private:
            uint32_t word0;
            uint32_t word1;
            uint32_t word2;
            uint32_t word3;
        };
    }
}

#endif
