#include "Legality/RNG/PIDType.h"

#include <cstddef>

namespace Legality
{
    namespace RNG
    {
        namespace
        {
            // Indexed by PIDType, in PKHeX's enum order.
            // clang-format off
            constexpr const char *PID_TYPE_SOURCE_NAMES[] = {
                "None",
                "Method_1", "Method_2", "Method_3", "Method_4",
                "Method_1_Unown", "Method_2_Unown", "Method_3_Unown", "Method_4_Unown",
                "Method_1_Roamer",
                "BACD", "BACD_A", "BACD_AX", "BACD_S",
                "BACD_EA", "BACD_EAX", "BACD_ES",
                "BACD_U", "BACD_U_AX",
                "BACD_R", "BACD_R_A", "BACD_RBCD", "BACD_TA", "BACD_TS", "BACD_M",
                "CuteCharm", "ChainShiny",
                "CXD", "CXDAnti", "CXD_ColoStarter", "Channel", "PokeSpot",
                "G4MGAntiShiny",
                "G5MGShiny", "Pokewalker", "Xoroshiro", "Overworld8", "Roaming8b", "Tera9",
                "Ranch",
            };
            // clang-format on
            static_assert(sizeof(PID_TYPE_SOURCE_NAMES) / sizeof(PID_TYPE_SOURCE_NAMES[0]) == static_cast<size_t>(PIDType::Ranch) + 1);
        }

        const char *getPIDTypeSourceName(PIDType type) noexcept
        {
            const size_t typeIndex = static_cast<size_t>(type);
            if (typeIndex >= sizeof(PID_TYPE_SOURCE_NAMES) / sizeof(PID_TYPE_SOURCE_NAMES[0]))
            {
                return "Unknown";
            }
            return PID_TYPE_SOURCE_NAMES[typeIndex];
        }
    }
}
