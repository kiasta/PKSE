/**
 * Shiny arithmetic shared by the RNG methods.
 *
 * Ported from PKHeX PKHeX.Core/Legality/RNG/Util/ShinyUtil.cs.
 */
#ifndef LEGALITY_RNG_SHINYUTIL_H
#define LEGALITY_RNG_SHINYUTIL_H

#include <cstdint>

namespace Pokemon
{
    class Pokemon;
}

namespace Legality
{
    namespace RNG
    {
        struct ShinyUtil
        {
            /// A PID keeping `PID`'s low half whose high half makes the shiny xor `xorType`.
            static constexpr uint32_t getShinyPID(uint16_t TID16, uint16_t SID16, uint32_t PID, uint32_t xorType) noexcept
            {
                const uint32_t lowHalf = PID & 0xFFFF;
                return ((xorType ^ TID16 ^ SID16 ^ lowHalf) << 16) | lowHalf;
            }

            /// The xor of a PID's halves alone (both ids already folded into it).
            static constexpr uint32_t getShinyXor(uint32_t combined) noexcept { return (combined ^ (combined >> 16)) & 0xFFFF; }

            /// PKHeX ShinyUtil.GetShinyXor(pid, id32).
            static constexpr uint32_t getShinyXor(uint32_t PID, uint32_t ID32) noexcept { return getShinyXor(PID ^ ID32); }

            /// Gen 6+ shininess (xor below 16).
            static bool isShiny6(uint32_t ID32, uint32_t PID) noexcept { return getShinyXor(ID32, PID) < 16; }

            /// Gen 3-5 shininess (xor below 8).
            static bool isShiny3(uint32_t ID32, uint32_t PID) noexcept { return getShinyXor(ID32, PID) < 8; }

            /// Makes `PID` shiny (with `xorType`) or not shiny for `ID32`, changing it only if needed.
            static void forceShinyState(bool isShiny, uint32_t &PID, uint32_t ID32, uint32_t xorType) noexcept
            {
                if (isShiny)
                {
                    if (!isShiny6(ID32, PID))
                    {
                        PID = getShinyPID(static_cast<uint16_t>(ID32 & 0xFFFF), static_cast<uint16_t>(ID32 >> 16), PID, xorType);
                    }
                }
                else if (isShiny6(ID32, PID))
                {
                    PID ^= 0x10000000;
                }
            }

            /// PKHeX PKM.IsShiny: the shiny window is 8 through Gen 5's formats and 16 after.
            static bool isShiny(const Pokemon::Pokemon &pokemon) noexcept;

            /// Gen 1/2 shininess from the 16-bit DV word.
            static constexpr bool isShinyGB(uint16_t dv16) noexcept { return (dv16 & 0x2FFF) == 0x2AAA; }
        };
    }
}

#endif
