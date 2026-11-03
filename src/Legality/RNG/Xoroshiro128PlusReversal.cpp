#include "Legality/RNG/Xoroshiro128PlusReversal.h"

#include "Legality/RNG/Xoroshiro128Plus.h"

namespace Legality
{
    namespace RNG
    {
        namespace
        {
            constexpr uint64_t XOROSHIRO_CONSTANT = Xoroshiro128Plus::XOROSHIRO_CONSTANT;
        }

        // PKHeX PKHeX.Core/Legality/RNG/Algorithms/Xoroshiro128PlusReversal.cs Explore. The masks
        // 0x1515 and 0x75 are XOROSHIRO_CONSTANT's bits 45-58 and 32-39.
        bool Xoroshiro128PlusReversal::explore(uint64_t &seed, uint32_t firstOutput, uint32_t secondOutput, uint8_t assumedBits, uint8_t carry) noexcept
        {
            uint64_t baseSeed = static_cast<uint32_t>(firstOutput - XOROSHIRO_CONSTANT);
            const uint64_t mixed0 = baseSeed ^ XOROSHIRO_CONSTANT;

            baseSeed |= (((secondOutput - (mixed0 >> 27)) ^ mixed0) & 0x1F) << 40;
            const uint64_t mixed1 = (baseSeed << 6) ^ (mixed0 >> 18) ^ (mixed0 >> 2);
            const uint64_t difference = (secondOutput >> 18) - (mixed1 ^ assumedBits);
            const uint64_t recoveredBits = (difference - carry) & 0x3FFF;

            seed = (static_cast<uint64_t>(assumedBits) << 58) | baseSeed;
            if ((assumedBits & 1u) != (recoveredBits >> 13))
            {
                return false;
            }

            seed |= ((recoveredBits ^ 0x1515) << 45);
            seed |= (((((secondOutput - ((seed >> 40) ^ mixed0)) >> 5) ^ 0x75) & 0xFF) << 32);

            Xoroshiro128Plus check(seed);
            if (static_cast<uint32_t>(check.next()) != firstOutput)
            {
                return false;
            }
            return static_cast<uint32_t>(check.next()) == secondOutput;
        }

        // PKHeX PKHeX.Core/Legality/RNG/Algorithms/Xoroshiro128PlusReversal.cs ExploreDouble
        bool Xoroshiro128PlusReversal::exploreDouble(uint64_t &seed, uint32_t firstOutput, uint32_t thirdOutput, uint8_t assumedBits, uint8_t firstCarry, uint8_t secondCarry) noexcept
        {
            const uint64_t baseSeed = static_cast<uint32_t>(firstOutput - XOROSHIRO_CONSTANT);
            const uint64_t bitsCheck = baseSeed & 7;
            uint64_t mixed0 = baseSeed ^ XOROSHIRO_CONSTANT;

            uint64_t mixed1 = (baseSeed >> 19) ^ (mixed0 >> 6) ^ 0x56;
            uint64_t mixed2 = (mixed0 >> 16) ^ 0x65;
            uint64_t mixed3 = mixed1 ^ (mixed0 >> 27);
            uint64_t mixed4 = mixed2 ^ (mixed0 >> 27);
            const uint64_t mixed5 = (baseSeed >> 16) ^ mixed0 ^ 0x2B1;
            const uint64_t mixed6 = (baseSeed >> 3) ^ (mixed0 >> 11) ^ 0xE0A;
            const uint64_t mixed7 = XOROSHIRO_CONSTANT ^ (mixed0 >> 24);
            const uint64_t outputHigh16 = thirdOutput >> 16;
            const uint64_t outputHigh27 = thirdOutput >> 27;

            const uint64_t assumedSeed = baseSeed | (static_cast<uint64_t>(assumedBits) << 43);
            mixed1 ^= assumedBits;
            mixed2 ^= assumedBits;
            mixed3 ^= assumedBits;
            mixed4 ^= assumedBits;

            const uint64_t difference0 = outputHigh16 - firstCarry;

            // Bits 32-36.
            uint64_t partialSeed = ((((difference0 - mixed3) ^ mixed4) & 0x1F) << 32) | assumedSeed;
            mixed0 = partialSeed ^ XOROSHIRO_CONSTANT;

            // Bits 37-39.
            partialSeed |= (((difference0 - (mixed1 ^ (mixed0 >> 27))) ^ mixed2 ^ (mixed0 >> 27)) & 0xFF) << 32;
            mixed0 = partialSeed ^ XOROSHIRO_CONSTANT;

            uint64_t recoveredBits = (((thirdOutput - (mixed5 ^ (mixed0 >> 27) ^ (mixed0 >> 24))) ^ mixed6 ^ (mixed0 >> 27)) & 0x1FFF);
            if ((recoveredBits >> 10) != bitsCheck)
            {
                seed = 0;
                return false;
            }

            // Bits 54-63.
            partialSeed |= (recoveredBits & 0x3FF) << 54;
            mixed0 = partialSeed ^ XOROSHIRO_CONSTANT;
            const uint64_t mixed8 = (partialSeed >> 30) ^ (mixed0 >> 17) ^ (mixed0 >> 54);
            const uint64_t mixed9 = (partialSeed >> 43) ^ (partialSeed >> 51) ^ (mixed0 >> 27) ^ (mixed0 >> 54) ^ 3;
            const uint64_t mixed10 = mixed8 ^ (mixed0 >> 38);
            const uint64_t mixed11 = mixed9 ^ (mixed0 >> 38);
            const uint64_t mixed12 = mixed7 ^ (mixed0 >> 35) ^ (partialSeed >> 48);
            const uint64_t mixed13 = (partialSeed >> 19) ^ (mixed0 >> 6);
            const uint64_t mixed14 = mixed13 ^ (mixed0 >> 27);

            const uint64_t difference1 = outputHigh27 - secondCarry;
            seed = ((((difference1 - mixed10) ^ mixed11) & 3) << 51) | partialSeed;
            mixed0 = seed ^ XOROSHIRO_CONSTANT;
            seed |= ((((difference0 - (mixed14 ^ (mixed0 >> 43))) >> 8) ^ mixed12 ^ (mixed0 >> 51)) & 3) << 40;
            mixed0 = seed ^ XOROSHIRO_CONSTANT;
            seed |= (((difference1 - (mixed8 ^ (mixed0 >> 38))) ^ mixed9 ^ (mixed0 >> 38)) & 7) << 51;
            mixed0 = seed ^ XOROSHIRO_CONSTANT;

            recoveredBits = ((((difference0 - (mixed13 ^ (mixed0 >> 27) ^ (mixed0 >> 43))) >> 8) ^ mixed7 ^ (mixed0 >> 35) ^ (seed >> 48) ^ (mixed0 >> 51)) & 0x7F);
            if ((assumedBits & 0xFu) != (recoveredBits >> 3))
            {
                return false;
            }

            seed |= recoveredBits << 40;
            Xoroshiro128Plus check(seed);
            if (static_cast<uint32_t>(check.next()) != firstOutput)
            {
                return false;
            }
            check.next();
            return static_cast<uint32_t>(check.next()) == thirdOutput;
        }

        bool XoroMachineConsecutive::moveNext() noexcept
        {
            do
            {
                while (carry < 2)
                {
                    if (Xoroshiro128PlusReversal::explore(seed, first, second, assumedBits, carry++))
                    {
                        return true;
                    }
                }
                carry = 0;
            } while (++assumedBits < 0x40);
            return false;
        }

        bool XoroMachineSkip::moveNext() noexcept
        {
            do
            {
                do
                {
                    while (firstCarry < 2)
                    {
                        if (Xoroshiro128PlusReversal::exploreDouble(seed, first, third, assumedBits, firstCarry++, secondCarry))
                        {
                            return true;
                        }
                    }
                    firstCarry = 0;
                } while ((secondCarry ^= 1) != 0);
            } while (++assumedBits != 0);
            return false;
        }
    }
}
