#include "Legality/RNG/CommonEvent3.h"

#include "Legality/RNG/LCRNG.h"
#include "Legality/RNG/ShinyUtil.h"

namespace Legality
{
    namespace RNG
    {
        namespace
        {
            constexpr uint32_t getRoundedAntishiny(uint32_t PID) noexcept
            {
                return (PID + 8) & 0xFFFFFFF8;
            }
        }

        uint32_t CommonEvent3::getRegularAntishiny(uint32_t firstRandom, uint32_t secondRandom, uint32_t idXor) noexcept
        {
            uint32_t PID = getRegular(firstRandom, secondRandom);
            if (ShinyUtil::isShiny3(idXor, PID))
            {
                PID = getRoundedAntishiny(PID);
            }
            return PID;
        }

        uint32_t CommonEvent3::getRegular(uint32_t &seed) noexcept
        {
            const uint32_t firstRandom = LCRNG::next16(seed);
            const uint32_t secondRandom = LCRNG::next16(seed);
            return getRegular(firstRandom, secondRandom);
        }

        // A first call of 0-7 would leave the PID shiny, so the event draws again.
        uint32_t CommonEvent3::getAntishiny(uint32_t &seed, uint32_t idXor) noexcept
        {
            uint32_t firstRandom = 0;
            do
            {
                firstRandom = LCRNG::next16(seed);
            } while ((firstRandom & ~0b111u) == 0);

            const uint32_t secondRandom = LCRNG::next16(seed);
            return getAntishiny(firstRandom, secondRandom, idXor);
        }

        uint32_t CommonEvent3::getForceShiny(uint32_t &seed, uint32_t idXor) noexcept
        {
            const uint32_t highRandom = LCRNG::next16(seed);
            LCRNG::next16(seed);
            const uint32_t secondRandom = LCRNG::next16(seed);
            return getForceShiny(highRandom, secondRandom, idXor);
        }

        uint32_t CommonEvent3::getRegularAntishiny(uint32_t &seed, uint32_t idXor) noexcept
        {
            const uint32_t firstRandom = LCRNG::next16(seed);
            const uint32_t secondRandom = LCRNG::next16(seed);
            return getRegularAntishiny(firstRandom, secondRandom, idXor);
        }

        bool CommonEvent3::isRegular(uint32_t PID, uint32_t firstRandom, uint32_t secondRandom) noexcept
        {
            return getRegular(firstRandom, secondRandom) == PID;
        }

        bool CommonEvent3::isForceAntishiny(uint32_t PID, uint32_t firstRandom, uint32_t secondRandom, uint32_t idXor) noexcept
        {
            if ((firstRandom & ~0b111u) == 0)
            {
                return false;
            }
            return getAntishiny(firstRandom, secondRandom, idXor) == PID;
        }

        bool CommonEvent3::isForceShiny(uint32_t PID, uint32_t highRandom, uint32_t secondRandom, uint32_t idXor) noexcept
        {
            if (highRandom != PID >> 16)
            {
                return false;
            }
            return getForceShiny(highRandom, secondRandom, idXor) == PID;
        }

        bool CommonEvent3::isRegularAntishiny(uint32_t PID, uint32_t firstRandom, uint32_t secondRandom, uint32_t idXor) noexcept
        {
            return getRegularAntishiny(firstRandom, secondRandom, idXor) == PID;
        }

        bool CommonEvent3::isRegularAntishiny(uint32_t actualPID, uint32_t expectedPID) noexcept
        {
            return getRoundedAntishiny(expectedPID) == actualPID;
        }

        // The rounding only ever adds, by less than 8, up to a multiple of 8.
        bool CommonEvent3::isRegularAntishinyDifferentOT(uint32_t actualPID, uint32_t expectedPID) noexcept
        {
            if ((actualPID & 0b111) != 0)
            {
                return false;
            }
            const uint32_t delta = actualPID - expectedPID;
            return delta != 0 && delta < 8;
        }

        // The id xor lands in the high half; for a different OT it must differ there, ignoring the
        // 3 shiny-value bits.
        bool CommonEvent3::isForceAntishinyDifferentOT(uint32_t PID, uint32_t firstRandom, uint32_t secondRandom, uint32_t idXor) noexcept
        {
            if ((firstRandom & ~0b111u) == 0)
            {
                return false;
            }
            if ((PID & 0xFFFF) != secondRandom)
            {
                return false;
            }
            const uint32_t expectedPID = getAntishiny(firstRandom, secondRandom, idXor);
            return expectedPID >> 19 != PID >> 19;
        }

        // Bits 0-2 come from the second call and bits 3-15 from the id xor, which for a different
        // OT must not be the hatching OT's.
        bool CommonEvent3::isForceShinyDifferentOT(uint32_t PID, uint32_t highRandom, uint32_t secondRandom, uint32_t idXor) noexcept
        {
            if (highRandom != PID >> 16)
            {
                return false;
            }
            if ((PID & 0b111) != (secondRandom & 0b111))
            {
                return false;
            }
            const uint32_t expectedLowBits = (idXor ^ highRandom) & 0xFFF8;
            const uint32_t actualLowBits = PID & 0xFFF8;
            return expectedLowBits != actualLowBits;
        }

        uint32_t CommonEvent3::nextGenderBit0(uint32_t &seed) noexcept
        {
            return getGenderBit0(LCRNG::next16(seed));
        }

        uint32_t CommonEvent3::nextGenderBit3(uint32_t &seed) noexcept
        {
            return getGenderBit3(LCRNG::next16(seed));
        }

        uint32_t CommonEvent3::nextGenderBit7(uint32_t &seed) noexcept
        {
            return getGenderBit7(LCRNG::next16(seed));
        }

        uint32_t CommonEvent3::nextGenderBit15(uint32_t &seed) noexcept
        {
            return getGenderBit15(LCRNG::next2(seed) >> 15);
        }

        uint32_t CommonEvent3::getRandomRestrictedGenderBit0(uint32_t seed, uint8_t gender) noexcept
        {
            while (true)
            {
                const uint32_t originSeed = seed & 0xFFFF;
                const uint32_t fifthRandom = LCRNG::next5(originSeed) >> 16;
                if (getGenderBit0(fifthRandom) == gender)
                {
                    return originSeed;
                }
                seed = LCRNG::next(seed);
            }
        }
    }
}
