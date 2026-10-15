#ifndef UTILS_SCXORSHIFT32_H
#define UTILS_SCXORSHIFT32_H

#include <bit>
#include <cstdint>

namespace Utils {
    class SCXorShift32
    {
    private:
        int counter = 0;
        uint32_t state;

        static uint32_t GetInitialState(uint32_t state)
        {
            // std::popcount (C++20) rather than __builtin_popcount: the builtin is GCC/Clang-only. Same
            // codegen, no compiler dependence.
            const int pop_count = std::popcount(state);
            for (int index = 0; index < pop_count; ++index)
            {
                state = XorshiftAdvance(state);
            }
            return state;
        }

        static uint32_t XorshiftAdvance(uint32_t state)
        {
            state ^= state << 2;
            state ^= state >> 15;
            state ^= state << 13;
            return state;
        }

    public:
        SCXorShift32(uint32_t seed) : state(GetInitialState(seed)) {}

        uint8_t Next()
        {
            int shiftCount = counter;
            uint8_t result = static_cast<uint8_t>(state >> (shiftCount * 8));
            if (shiftCount == 3)
            {
                state = XorshiftAdvance(state);
                counter = 0;
            }
            else
            {
                ++counter;
            }
            return result;
        }

        uint32_t Next32()
        {
            return static_cast<uint32_t>(Next()) | (static_cast<uint32_t>(Next()) << 8) | (static_cast<uint32_t>(Next()) << 16) | (static_cast<uint32_t>(Next()) << 24);
        }
    };
}

#endif