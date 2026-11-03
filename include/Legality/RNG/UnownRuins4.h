/**
 * The Unown letter a Gen 4 ruin's encounter rolls: HeartGold/SoulSilver's Ruins of Alph and
 * Diamond/Pearl/Platinum's Solaceon Ruins. The form roll is the call after the Method 1 PID and IVs.
 *
 * Ported from PKHeX PKHeX.Core/Legality/RNG/ClassicEra/Gen4/RuinsOfAlph4.cs and SolaceonRuins4.cs.
 */
#ifndef LEGALITY_RNG_UNOWNRUINS4_H
#define LEGALITY_RNG_UNOWNRUINS4_H

#include <cstdint>

namespace Pokemon
{
    class Pokemon;
}

namespace Legality
{
    namespace RNG
    {
        struct RuinsOfAlph4
        {
            static constexpr uint16_t LOCATION = 209;

            static bool isFormValid(uint32_t seed, uint8_t form) noexcept;
            /// True when the Pokemon is not Method 1 at all: that is reported elsewhere.
            static bool isFormValid(const Pokemon::Pokemon &pokemon, uint8_t form) noexcept;
            static bool isFormValidFrame(uint32_t seed, uint8_t form) noexcept;
            /// The entrance gives ! or ?.
            static constexpr uint8_t getEntranceForm(uint32_t seed) noexcept { return static_cast<uint8_t>(26 + ((seed >> 16) & 1)); }
        };

        struct SolaceonRuins4
        {
            static constexpr uint16_t LOCATION = 53;

            static bool isFormValid(uint32_t seed, uint8_t form) noexcept;
            /// True when the Pokemon is not Method 1 at all: that is reported elsewhere.
            static bool isFormValid(const Pokemon::Pokemon &pokemon, uint8_t form) noexcept;
            /// F, R, I, E, N and D each have a room to themselves.
            static constexpr bool isSingleFormRoomUnown(uint8_t form) noexcept { return (0b1111111111011101111011000111u & (1u << (form & 31))) == 0; }
        };
    }
}

#endif
