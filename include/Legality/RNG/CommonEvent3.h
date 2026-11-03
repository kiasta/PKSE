/**
 * The PID routines of Gen 3 event distributions ("BACD": the high half is drawn first), with their
 * anti-shiny and force-shiny variants and the reverse checks MethodFinder runs.
 *
 * Ported from PKHeX PKHeX.Core/Legality/RNG/ClassicEra/Gen3/CommonEvent3.cs. `a16`/`b16` in PKHeX
 * are the first and second 16-bit calls; here `firstRandom`/`secondRandom`. `idXor` is TID ^ SID.
 */
#ifndef LEGALITY_RNG_COMMONEVENT3_H
#define LEGALITY_RNG_COMMONEVENT3_H

#include <cstdint>

namespace Legality
{
    namespace RNG
    {
        struct CommonEvent3
        {
            static constexpr uint32_t getRegular(uint32_t firstRandom, uint32_t secondRandom) noexcept { return (firstRandom << 16) | secondRandom; }

            static constexpr uint32_t getAntishiny(uint32_t firstRandom, uint32_t secondRandom, uint32_t idXor) noexcept { return ((firstRandom ^ (idXor ^ secondRandom)) << 16) | secondRandom; }

            static constexpr uint32_t getForceShiny(uint32_t highRandom, uint32_t secondRandom, uint32_t idXor) noexcept { return (highRandom << 16) | (((idXor ^ highRandom) & 0xFFF8) | (secondRandom & 0b111)); }

            /// Anti-shiny by rounding the PID up to the next multiple of 8 when it would be shiny.
            static uint32_t getRegularAntishiny(uint32_t firstRandom, uint32_t secondRandom, uint32_t idXor) noexcept;

            static uint32_t getRegular(uint32_t &seed) noexcept;
            static uint32_t getAntishiny(uint32_t &seed, uint32_t idXor) noexcept;
            static uint32_t getForceShiny(uint32_t &seed, uint32_t idXor) noexcept;
            static uint32_t getRegularAntishiny(uint32_t &seed, uint32_t idXor) noexcept;

            static bool isRegular(uint32_t PID, uint32_t firstRandom, uint32_t secondRandom) noexcept;
            static bool isForceAntishiny(uint32_t PID, uint32_t firstRandom, uint32_t secondRandom, uint32_t idXor) noexcept;
            static bool isForceShiny(uint32_t PID, uint32_t highRandom, uint32_t secondRandom, uint32_t idXor) noexcept;
            static bool isRegularAntishiny(uint32_t PID, uint32_t firstRandom, uint32_t secondRandom, uint32_t idXor) noexcept;
            static bool isRegularAntishiny(uint32_t actualPID, uint32_t expectedPID) noexcept;
            static bool isRegularAntishinyDifferentOT(uint32_t actualPID, uint32_t expectedPID) noexcept;
            static bool isForceAntishinyDifferentOT(uint32_t PID, uint32_t firstRandom, uint32_t secondRandom, uint32_t idXor) noexcept;
            static bool isForceShinyDifferentOT(uint32_t PID, uint32_t highRandom, uint32_t secondRandom, uint32_t idXor) noexcept;

            // The events' gender bit, from one call, by the bit each distribution read.
            static constexpr uint32_t getGenderBit0(uint32_t random16) noexcept { return (random16 / 3) & 1; }
            static constexpr uint32_t getGenderBit3(uint32_t random16) noexcept { return (random16 >> 3) & 1; }
            /// Female (1) when the bit is 0.
            static constexpr uint32_t getGenderBit7(uint32_t random16) noexcept { return ((random16 >> 7) & 1) ^ 1; }
            static constexpr uint32_t getGenderBit15(uint32_t random16) noexcept { return (random16 >> 15) & 1; }
            static uint32_t nextGenderBit0(uint32_t &seed) noexcept;
            static uint32_t nextGenderBit3(uint32_t &seed) noexcept;
            static uint32_t nextGenderBit7(uint32_t &seed) noexcept;
            /// PKHeX reads this one two calls ahead without advancing `seed`.
            static uint32_t nextGenderBit15(uint32_t &seed) noexcept;

            /// The first 16-bit origin seed at or after `seed` whose fifth call gives `gender`.
            static uint32_t getRandomRestrictedGenderBit0(uint32_t seed, uint8_t gender) noexcept;
        };
    }
}

#endif
