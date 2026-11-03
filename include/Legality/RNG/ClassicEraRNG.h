/**
 * The PID and IV building blocks of the Gen 3/4 LCRNG games, and the Gen 4 initial seed (the
 * console date, time and boot delay folded into one 32-bit value).
 *
 * Ported from PKHeX PKHeX.Core/Legality/RNG/ClassicEra/ClassicEraRNG.cs and
 * ClassicEra/Gen4/InitialSeedComponents4.cs.
 */
#ifndef LEGALITY_RNG_CLASSICERARNG_H
#define LEGALITY_RNG_CLASSICERARNG_H

#include <cstdint>
#include <optional>
#include <span>

namespace Legality
{
    namespace RNG
    {
        /// The date, time and delay that make a Gen 4 initial seed. Year is years since 2000.
        struct InitialSeedComponents4
        {
            uint8_t year = 0;
            uint8_t month = 0;
            uint8_t day = 0;
            uint8_t hour = 0;
            uint8_t minute = 0;
            uint8_t second = 0;
            uint16_t delay = 0;

            uint32_t toSeed() const noexcept;
            constexpr bool isInvalid() const noexcept { return month == 0; }
        };

        struct ClassicEraRNG
        {
            /// A Poke Radar chain shiny PID: 3+3 random low bits, 13 single-bit calls, then the high
            /// half forced shiny against `ID32`.
            static uint32_t getChainShinyPID(uint32_t &seed, uint32_t ID32) noexcept;

            /// Two calls, the first the low half.
            static uint32_t getSequentialPID(uint32_t &seed) noexcept;

            /// As getSequentialPID, leaving `seed` unchanged.
            static uint32_t getSequentialPIDFrom(uint32_t seed) noexcept;

            /// PKHeX's ref overload builds the PID in sequential order too; kept as written.
            static uint32_t getReversePID(uint32_t &seed) noexcept;

            /// Two calls, the first the high half.
            static uint32_t getReversePIDFrom(uint32_t seed) noexcept;

            /// Two 15-bit calls, the first HP/Attack/Defense.
            static uint32_t getSequentialIVs(uint32_t &seed) noexcept;

            static uint32_t getInitialSeed(uint32_t year, uint32_t month, uint32_t day, uint32_t hour, uint32_t minute, uint32_t second, uint32_t delay) noexcept;

            /// Walks `seed` back to the nearest plausible initial seed for the date, preferring the
            /// one before it when that has the shorter delay and is within 5000 calls.
            static uint32_t seekInitialSeed(uint32_t year, uint32_t month, uint32_t day, uint32_t seed) noexcept;

            static bool isInitialSeed(uint32_t year, uint32_t month, uint32_t day, uint32_t seed) noexcept;

            /// The date and time `seed` encodes; empty when it is not an initial seed for the date.
            static std::optional<InitialSeedComponents4> decomposeSeed(uint32_t seed, uint32_t year, uint32_t month, uint32_t day) noexcept;

            /// The initial seed closest to an IV spread's origin. `individualValues` is in PKHeX's
            /// HP, Attack, Defense, Speed, Special Attack, Special Defense order.
            static uint32_t seekInitialSeedForIVs(std::span<const int> individualValues, uint32_t year, uint32_t month, uint32_t day, uint32_t &origin) noexcept;
        };
    }
}

#endif
