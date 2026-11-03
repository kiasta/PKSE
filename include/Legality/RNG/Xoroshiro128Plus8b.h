/**
 * The xoroshiro128+ RNG of Brilliant Diamond/Shining Pearl, seeded through SplitMix64.
 *
 * Ported from PKHeX PKHeX.Core/Legality/RNG/Algorithms/Xoroshiro128Plus8b.cs.
 */
#ifndef LEGALITY_RNG_XOROSHIRO128PLUS8B_H
#define LEGALITY_RNG_XOROSHIRO128PLUS8B_H

#include <bit>
#include <cstdint>

namespace Legality
{
    namespace RNG
    {
        class Xoroshiro128Plus8b
        {
        public:
            constexpr Xoroshiro128Plus8b(uint64_t firstState, uint64_t secondState) noexcept : state0(firstState), state1(secondState) {}

            constexpr explicit Xoroshiro128Plus8b(uint64_t seed) noexcept : state0(splitMix64(seed + 0x9E3779B97F4A7C15)), state1(splitMix64(seed + 0x3C6EF372FE94F82A)) {}

            constexpr uint64_t firstState() const noexcept { return state0; }
            constexpr uint64_t secondState() const noexcept { return state1; }
            constexpr bool equals(uint64_t firstStateValue, uint64_t secondStateValue) const noexcept { return state0 == firstStateValue && state1 == secondStateValue; }

            constexpr uint64_t next() noexcept
            {
                const uint64_t oldState0 = state0;
                uint64_t mixedState1 = state1;
                const uint64_t randomValue = oldState0 + mixedState1;
                mixedState1 ^= oldState0;
                state0 = std::rotl(oldState0, 24) ^ mixedState1 ^ (mixedState1 << 16);
                state1 = std::rotl(mixedState1, 37);
                return randomValue;
            }

            constexpr uint64_t previous() noexcept
            {
                uint64_t restoredState0 = state0;
                uint64_t restoredState1 = std::rotl(state1, 27);
                restoredState0 = restoredState0 ^ restoredState1 ^ (restoredState1 << 16);
                restoredState0 = std::rotl(restoredState0, 40);
                restoredState1 ^= restoredState0;
                const uint64_t randomValue = restoredState0 + restoredState1;
                state0 = restoredState0;
                state1 = restoredState1;
                return randomValue;
            }

            constexpr bool nextBool() noexcept { return (next() >> 63) != 0; }
            constexpr uint8_t nextByte() noexcept { return static_cast<uint8_t>(next() >> 56); }
            constexpr uint16_t nextUInt16() noexcept { return static_cast<uint16_t>(next() >> 48); }
            constexpr uint32_t nextUInt() noexcept { return static_cast<uint32_t>(next() >> 32); }

            constexpr uint32_t nextUInt(uint32_t exclusiveMaximum) noexcept
            {
                const uint32_t randomValue = nextUInt();
                return randomValue - ((randomValue / exclusiveMaximum) * exclusiveMaximum);
            }

        private:
            static constexpr uint64_t splitMix64(uint64_t seed) noexcept
            {
                seed = 0xBF58476D1CE4E5B9 * (seed ^ (seed >> 30));
                seed = 0x94D049BB133111EB * (seed ^ (seed >> 27));
                return seed ^ (seed >> 31);
            }

            uint64_t state0;
            uint64_t state1;
        };
    }
}

#endif
