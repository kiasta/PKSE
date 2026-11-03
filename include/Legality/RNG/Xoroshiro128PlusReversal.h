/**
 * Recovers the 64-bit seeds of a Xoroshiro128Plus whose second state word is the games' constant,
 * from two 32-bit outputs: consecutive ones (XoroMachineConsecutive) or with one call between
 * them (XoroMachineSkip).
 *
 * Ported from PKHeX PKHeX.Core/Legality/RNG/Algorithms/Xoroshiro128PlusReversal.cs, which follows
 * StarfBerry's PokeRNG Xoroshiro_Recovery: bits 58-63 (or 43-50 with a skip) are assumed and the
 * rest solved, 10 brute-forced bits instead of 12.
 */
#ifndef LEGALITY_RNG_XOROSHIRO128PLUSREVERSAL_H
#define LEGALITY_RNG_XOROSHIRO128PLUSREVERSAL_H

#include <cstdint>

namespace Legality
{
    namespace RNG
    {
        struct Xoroshiro128PlusReversal
        {
            /// One candidate for two consecutive outputs, given assumed bits 58-63 and a carry.
            static bool explore(uint64_t &seed, uint32_t firstOutput, uint32_t secondOutput, uint8_t assumedBits, uint8_t carry) noexcept;

            /// One candidate for outputs one call apart, given assumed bits 43-50 and two carries.
            static bool exploreDouble(uint64_t &seed, uint32_t firstOutput, uint32_t thirdOutput, uint8_t assumedBits, uint8_t firstCarry, uint8_t secondCarry) noexcept;
        };

        /// Every seed whose first two outputs are `firstOutput` and `secondOutput`, one per
        /// moveNext() that returns true.
        class XoroMachineConsecutive
        {
        public:
            constexpr XoroMachineConsecutive(uint32_t firstOutput, uint32_t secondOutput) noexcept : first(firstOutput), second(secondOutput) {}

            bool moveNext() noexcept;
            constexpr uint64_t current() const noexcept { return seed; }
            constexpr void reset() noexcept
            {
                assumedBits = 0;
                carry = 0;
            }

        private:
            uint32_t first;
            uint32_t second;
            uint64_t seed = 0;
            uint8_t assumedBits = 0;
            uint8_t carry = 0;
        };

        /// Every seed whose first and third outputs are `firstOutput` and `thirdOutput`.
        class XoroMachineSkip
        {
        public:
            constexpr XoroMachineSkip(uint32_t firstOutput, uint32_t thirdOutput) noexcept : first(firstOutput), third(thirdOutput) {}

            bool moveNext() noexcept;
            constexpr uint64_t current() const noexcept { return seed; }
            constexpr void reset() noexcept
            {
                assumedBits = 0;
                firstCarry = 0;
                secondCarry = 0;
            }

        private:
            uint32_t first;
            uint32_t third;
            uint64_t seed = 0;
            uint8_t assumedBits = 0;
            uint8_t firstCarry = 0;
            uint8_t secondCarry = 0;
        };
    }
}

#endif
