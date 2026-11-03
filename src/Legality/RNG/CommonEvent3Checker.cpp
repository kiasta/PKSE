#include "Legality/RNG/CommonEvent3Checker.h"

#include "Legality/RNG/ChannelJirachi.h"
#include "Legality/RNG/LCRNG.h"
#include "Legality/RNG/MystryMew.h"
#include "Legality/RNG/PCJPFifthAnniversary.h"

namespace Legality
{
    namespace RNG
    {
        namespace
        {
            constexpr uint16_t JIRACHI_SPECIES = 385;
            // The largest digit sum of a binary-coded-decimal hh:mm:ss timestamp from the RTC.
            constexpr uint32_t BERRY_FIX_MAXIMUM_SEED = 213;

            bool isRegularOrAntishiny(PIDType observed) noexcept
            {
                return observed == PIDType::BACD || observed == PIDType::BACDAntishiny || observed == PIDType::BACDEggAntishiny;
            }

            bool isMatchTableRand(uint16_t species, bool isWish, uint16_t seed16, bool isShiny) noexcept
            {
                const uint32_t giftIndex = PCJPFifthAnniversary::getIndex(species);
                const PCJPFifthAnniversaryResult result = PCJPFifthAnniversary::getResult(seed16);
                return result.shiny == isShiny && result.giftIndex == giftIndex && result.wish == isWish;
            }

            bool isRestrictedTableMatch(PIDIV &value, uint16_t species, bool isWish, bool isShiny) noexcept
            {
                const uint32_t origin = LCRNG::previous2(value.originSeed);
                if (origin > 0xFFFF)
                {
                    return false;
                }
                // Negai Boshi Jirachi filled every table entry with itself; the Fifth Anniversary had several.
                if (species != JIRACHI_SPECIES)
                {
                    if (!isMatchTableRand(species, isWish, static_cast<uint16_t>(origin), isShiny))
                    {
                        return false;
                    }
                }
                value = value.asMutated(PIDType::BACDRestricted, origin);
                return true;
            }
        }

        bool CommonEvent3Checker::isRestrictedTable2(PIDIV &value, PIDType observed, uint16_t species, bool isWish) noexcept
        {
            if (!isRegularOrAntishiny(observed))
            {
                return false;
            }
            return isRestrictedTableMatch(value, species, isWish, false);
        }

        bool CommonEvent3Checker::isRestrictedTable3(PIDIV &value, PIDType observed, uint16_t species, bool isWish) noexcept
        {
            if (observed != PIDType::BACDShiny && observed != PIDType::BACDEggShiny)
            {
                return false;
            }
            return isRestrictedTableMatch(value, species, isWish, true);
        }

        bool CommonEvent3Checker::isMystryMew(PIDIV &value, PIDType observed) noexcept
        {
            if (observed != PIDType::BACD)
            {
                return false;
            }
            uint32_t seed = value.originSeed;
            const MystryMewSeedIndexes info = MystryMew::getSeedIndexes(seed);
            if (info.seedIndex == -1)
            {
                return false;
            }
            seed = MystryMew::isRestrictedIndex0th(info.subIndex) ? seed : MystryMew::getSeed(info.seedIndex);
            if (!MystryMew::isValidSeed(seed, info.subIndex))
            {
                return false;
            }
            value = value.asMutated(PIDType::BACDRestricted, seed);
            return true;
        }

        bool CommonEvent3Checker::isChannelJirachi(PIDIV &value, PIDType observed) noexcept
        {
            if (observed != PIDType::Channel)
            {
                return false;
            }
            const ChannelJirachiPossibility possibility = ChannelJirachi::getPossible(value.originSeed);
            if (possibility.pattern == ChannelJirachiRandomResult::None)
            {
                return false;
            }
            value = value.asMutated(PIDType::Channel, possibility.seed);
            return true;
        }

        bool CommonEvent3Checker::isRestrictedSimple(PIDIV &value, PIDType observed) noexcept
        {
            if (observed != PIDType::BACD)
            {
                return false;
            }
            if (value.originSeed > 0xFFFF)
            {
                return false;
            }
            value = value.asMutated(PIDType::BACDRestricted);
            return true;
        }

        bool CommonEvent3Checker::isUnrestrictedAnti(PIDIV &value, PIDType observed) noexcept
        {
            if (!isRegularOrAntishiny(observed))
            {
                return false;
            }
            value = value.asMutated(PIDType::BACDUnrestricted);
            return true;
        }

        bool CommonEvent3Checker::isRestrictedAnti(PIDIV &value, PIDType observed) noexcept
        {
            if (!isRegularOrAntishiny(observed))
            {
                return false;
            }
            if (value.originSeed > 0xFFFF)
            {
                return false;
            }
            value = value.asMutated(PIDType::BACDRestricted);
            return true;
        }

        bool CommonEvent3Checker::isUnrestrictedAntiX(PIDIV &value, PIDType observed) noexcept
        {
            if (observed != PIDType::BACDAntishinyXor)
            {
                return false;
            }
            value = value.asMutated(PIDType::BACDAntishinyXor);
            return true;
        }

        bool CommonEvent3Checker::isBerryFixShiny(PIDIV &value, PIDType observed) noexcept
        {
            if (observed != PIDType::BACDShiny)
            {
                return false;
            }
            if (value.originSeed > BERRY_FIX_MAXIMUM_SEED)
            {
                return false;
            }
            value = value.asMutated(PIDType::BACDRestricted);
            return true;
        }
    }
}
