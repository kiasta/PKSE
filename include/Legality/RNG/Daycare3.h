/**
 * Gen 3 Day Care eggs: whether a pending-egg PID is one the egg roll can make, and the earliest
 * seed whose IV and inheritance rolls give the egg's IVs.
 *
 * Ported from PKHeX PKHeX.Core/Legality/RNG/ClassicEra/Gen3/Daycare3.cs. IVs are in PKHeX's
 * GetIVs order: HP, Attack, Defense, Speed, Special Attack, Special Defense.
 */
#ifndef LEGALITY_RNG_DAYCARE3_H
#define LEGALITY_RNG_DAYCARE3_H

#include <cstdint>
#include <span>

namespace Pokemon
{
    class Pokemon;
}

namespace Legality
{
    namespace RNG
    {
        enum class Daycare3Correlation : uint8_t
        {
            None,
            Regular,
        };

        struct Daycare3Origin
        {
            uint32_t origin = 0;
            uint32_t advances = 0;
            uint16_t initial = 0;
            Daycare3Correlation pattern = Daycare3Correlation::None;
        };

        struct Daycare3
        {
            /// `version` is the raw GameVersion value.
            static bool isValidProcPID(uint32_t PID, uint8_t version) noexcept;

            static bool tryGetOriginSeed(const Pokemon::Pokemon &pokemon, Daycare3Origin &origin) noexcept;
            static bool tryGetOriginSeed(std::span<const int> individualValues, uint32_t PID, uint8_t version, Daycare3Origin &origin) noexcept;
            static bool tryGetOriginSeedEmerald(std::span<const int> individualValues, Daycare3Origin &origin) noexcept;
            static bool tryGetOriginSeed(std::span<const int> individualValues, uint32_t PID, Daycare3Origin &origin) noexcept;
        };
    }
}

#endif
