/**
 * Gen 5 (Black/White, Black 2/White 2) PID rules. The 64-bit RNG cannot be traced back to a seed,
 * so only the PID's own correlations are checkable.
 *
 * Ported from PKHeX PKHeX.Core/Legality/RNG/ClassicEra/MonochromeRNG.cs, without Generate (it
 * builds a new Pokemon for PKHeX's encounter generator, which the checker never does).
 */
#ifndef LEGALITY_RNG_MONOCHROMERNG_H
#define LEGALITY_RNG_MONOCHROMERNG_H

#include <cstdint>

namespace Legality
{
    namespace RNG
    {
        struct MonochromeRNG
        {
            /// The low bit of TID ^ SID.
            static constexpr uint32_t getBitXor(uint16_t TID16, uint16_t SID16) noexcept { return (static_cast<uint32_t>(TID16) ^ SID16) & 1; }

            /// The wild correlation: 0 when the PID's top and bottom bits agree with the ids' low bit.
            static constexpr uint32_t getBitXor(uint32_t PID, uint16_t TID16, uint16_t SID16) noexcept { return getBitXor(TID16, SID16) ^ (PID & 1) ^ (PID >> 31); }

            /// Whether a forced-gender PID's low byte can give `gender` (0 male, 1 female); false
            /// for any other gender, where PKHeX throws.
            static constexpr bool isValidForcedRandomGender(uint32_t PID, uint8_t gender) noexcept
            {
                if (gender == 0)
                {
                    return (PID & 0xFF) != 254 && (PID & 0xFF) != 255;
                }
                if (gender == 1)
                {
                    return (PID & 0xFF) != 0;
                }
                return false;
            }

            /// A Gen 5 Mystery Gift shiny PID from its low half (`genderValue`) and ability bit.
            static constexpr uint32_t getShinyPID(uint32_t genderValue, uint32_t abilityBit, uint16_t TID16, uint16_t SID16) noexcept
            {
                uint32_t PID = ((genderValue ^ TID16 ^ SID16) << 16) | genderValue;
                if ((PID & 0x10000) != (abilityBit & 1) << 16)
                {
                    PID ^= 0x10000;
                }
                return PID;
            }
        };
    }
}

#endif
