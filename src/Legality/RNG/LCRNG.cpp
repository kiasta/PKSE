#include "Legality/RNG/LCRNG.h"

namespace Legality
{
    namespace RNG
    {
        namespace
        {
            // Entry n is the multiplier and increment of 2^n calls.
            // clang-format off
            constexpr uint32_t JUMP_MULTIPLIERS[32] = {
                0x41C64E6D, 0xC2A29A69, 0xEE067F11, 0xCFDDDF21, 0x5F748241, 0x8B2E1481, 0x76006901, 0x1711D201,
                0xBE67A401, 0xDDDF4801, 0x3FFE9001, 0x90FD2001, 0x65FA4001, 0xDBF48001, 0xF7E90001, 0xEFD20001,
                0xDFA40001, 0xBF480001, 0x7E900001, 0xFD200001, 0xFA400001, 0xF4800001, 0xE9000001, 0xD2000001,
                0xA4000001, 0x48000001, 0x90000001, 0x20000001, 0x40000001, 0x80000001, 0x00000001, 0x00000001,
            };

            constexpr uint32_t JUMP_INCREMENTS[32] = {
                0x00006073, 0xE97E7B6A, 0x31B0DDE4, 0x67DBB608, 0xCBA72510, 0x1D29AE20, 0xBA84EC40, 0x79F01880,
                0x08793100, 0x6B566200, 0x803CC400, 0xA6B98800, 0xE6731000, 0x30E62000, 0xF1CC4000, 0x23988000,
                0x47310000, 0x8E620000, 0x1CC40000, 0x39880000, 0x73100000, 0xE6200000, 0xCC400000, 0x98800000,
                0x31000000, 0x62000000, 0xC4000000, 0x88000000, 0x10000000, 0x20000000, 0x40000000, 0x80000000,
            };
            // clang-format on
        }

        // PKHeX PKHeX.Core/Legality/RNG/Algorithms/LCRNG.cs GetDistance
        uint32_t LCRNG::getDistance(uint32_t start, uint32_t end) noexcept
        {
            int jumpIndex = 0;
            uint32_t bit = 1u;
            uint32_t distance = 0u;
            uint32_t seed = start;
            while (seed != end)
            {
                if (((seed ^ end) & bit) != 0)
                {
                    seed = (seed * JUMP_MULTIPLIERS[jumpIndex]) + JUMP_INCREMENTS[jumpIndex];
                    distance |= bit;
                }
                jumpIndex++;
                bit <<= 1;
            }
            return distance;
        }
    }
}
