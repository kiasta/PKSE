/**
 * The xoroshiro128+ RNG of Sword/Shield, Legends: Arceus and Scarlet/Violet. Those games seed it
 * with a 64-bit value in the first state word and a fixed constant in the second.
 *
 * Ported from PKHeX PKHeX.Core/Legality/RNG/Algorithms/Xoroshiro128Plus.cs. A plain value type:
 * copying it forks the sequence, which the seed checks rely on.
 */
#ifndef LEGALITY_RNG_XOROSHIRO128PLUS_H
#define LEGALITY_RNG_XOROSHIRO128PLUS_H

#include <bit>
#include <cstdint>

namespace Legality
{
    namespace RNG
    {
        class Xoroshiro128Plus
        {
        public:
            static constexpr uint64_t XOROSHIRO_CONSTANT_0 = 0x0F4B17A579F18960;
            static constexpr uint64_t XOROSHIRO_CONSTANT = 0x82A2B175229D6A5B;

            constexpr explicit Xoroshiro128Plus(uint64_t firstState = XOROSHIRO_CONSTANT_0, uint64_t secondState = XOROSHIRO_CONSTANT) noexcept : state0(firstState), state1(secondState) {}

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

            /// A value below `exclusiveMaximum`, re-rolling masked draws that land outside it.
            constexpr uint64_t nextInt(uint64_t exclusiveMaximum = UINT32_MAX) noexcept
            {
                const uint64_t mask = getBitmask(exclusiveMaximum);
                while (true)
                {
                    const uint64_t randomValue = next() & mask;
                    if (randomValue < exclusiveMaximum)
                    {
                        return randomValue;
                    }
                }
            }

            float nextFloat(float range, float bias = 0.0f) noexcept
            {
                // 1/(2^64), as the games load it into a float register.
                constexpr float inverseTwoToThe64 = 5.421e-20f;
                const uint64_t randomValue = next();
                return (range * (static_cast<float>(randomValue) * inverseTwoToThe64)) + bias;
            }

        private:
            // C#'s shift count is taken mod 64, so an exclusive maximum of 0 masks to 0 there too.
            static constexpr uint64_t getBitmask(uint64_t exclusiveMaximum) noexcept
            {
                const int leadingZeroCount = std::countl_zero(exclusiveMaximum - 1);
                return (1ULL << ((64 - leadingZeroCount) & 63)) - 1;
            }

            uint64_t state0;
            uint64_t state1;
        };
    }
}

#endif
