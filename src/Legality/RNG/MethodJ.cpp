#include "Legality/RNG/MethodJ.h"

#include "Legality/RNG/ClassicEraRNG.h"
#include "Legality/RNG/LCRNG.h"
#include "Legality/RNG/SlotMethod.h"

namespace Legality
{
    namespace RNG
    {
        namespace
        {
            // Nature: rand / 0xA3E. Cute Charm: rand / 0x5556 != 0 (2/3). Sync: rand >> 15 == 0 (1/2).
            // Static/Magnet Pull: rand >> 15 == 0. Pressure/Hustle/Vital Spirit: rand >> 15 == 1.
            // Intimidate/Keen Eye: rand >> 15 == 1, and 0 rejects the encounter.
            // Feebas spot: rand >> 15 == 1, and 0 uses the regular slots.
            using Slot = EncounterSlot4Parameters;
            using Frame = FrameCheckDetails<Slot>;

            constexpr uint16_t FEEBAS_SPECIES = 349;

            bool isCuteCharmFail(uint32_t random) noexcept { return (random / 0x5556) == 0; }
            bool isCuteCharmPass(uint32_t random) noexcept { return (random / 0x5556) != 0; }
            bool isSyncPass(uint32_t random) noexcept { return (random >> 15) == 0; }
            bool isStaticMagnetFail(uint32_t random) noexcept { return (random >> 15) != 0; }
            bool isStaticMagnetPass(uint32_t random) noexcept { return (random >> 15) == 0; }
            bool isHustleVitalFail(uint32_t random) noexcept { return (random >> 15) != 1; }
            bool isHustleVitalPass(uint32_t random) noexcept { return (random >> 15) == 1; }
            bool isIntimidateKeenEyePass(uint32_t random) noexcept { return (random >> 15) == 1; }
            bool isFeebasChance(uint32_t random) noexcept { return (random >> 15) == 1; }

            bool isFishingRodType(SlotType4 type) noexcept
            {
                return type == SlotType4::OldRod || type == SlotType4::GoodRod || type == SlotType4::SuperRod;
            }

            uint8_t getRodRate(SlotType4 type) noexcept
            {
                switch (type)
                {
                case SlotType4::OldRod:
                    return 25;
                case SlotType4::GoodRod:
                    return 50;
                case SlotType4::SuperRod:
                    return 75;
                default:
                    return 0;
                }
            }

            bool isFishPossible(SlotType4 type, uint32_t &seed) noexcept
            {
                const uint32_t roll = (seed >> 16) / 656;
                if (roll < getRodRate(type))
                {
                    seed = LCRNG::previous(seed);
                    return true;
                }
                return false;
            }

            // Mt. Coronet B1F rolls whether Feebas may replace the slots (0 hook, 1 tile check, 2 ESV),
            // a call made even off a Feebas tile.
            bool isValidCoronetB1F(const Slot &encounter, uint32_t &result) noexcept
            {
                if (encounter.species == FEEBAS_SPECIES && !isFeebasChance(result >> 16))
                {
                    return false;
                }
                // A regular slot can come from fishing off a Feebas tile, so the roll need not fail.
                result = LCRNG::previous(result);
                return true;
            }

            bool checkEncounterActivationCore(const Slot &encounter, uint32_t &seed) noexcept
            {
                if (isFishingRodType(encounter.type))
                {
                    if (encounter.isCoronetFeebasArea)
                    {
                        if (!isValidCoronetB1F(encounter, seed))
                        {
                            return false;
                        }
                    }
                    // Diamond, Pearl and Platinum never apply Suction Cups or Sticky Hold's rod boost: they lead like nothing.
                    return isFishPossible(encounter.type, seed);
                }
                if (encounter.type == SlotType4::HoneyTree)
                {
                    // A Honey Tree consumes no slot call; roll forward here instead of in every slot check.
                    seed = LCRNG::next(seed);
                }
                return true;
            }

            bool checkActivation(const Slot &encounter, uint32_t seed, LeadRequired lead, LeadSeed &result) noexcept
            {
                const bool pass = checkEncounterActivationCore(encounter, seed);
                result = LeadSeed{seed, lead};
                return pass;
            }

            bool canRadar(const Slot &encounter) noexcept
            {
                return encounter.type == SlotType4::Grass && encounter.canUseRadar;
            }

            uint32_t getRandomLevelPlain(const Slot &encounter, uint32_t levelRandom) noexcept
            {
                if (encounter.type == SlotType4::HoneyTree)
                {
                    return MethodJ::getHoneyTreeLevel(levelRandom);
                }
                const uint32_t modulus = 1u + encounter.levelMax - encounter.levelMin;
                return (levelRandom % modulus) + encounter.levelMin;
            }

            bool isOriginalLevelValid(uint8_t levelMin, uint8_t levelMax, uint8_t format, uint32_t level) noexcept
            {
                if (format == MethodJ::FORMAT && levelMin > 1)
                {
                    return level == levelMin;
                }
                return isLevelWithinRange(static_cast<uint8_t>(level), levelMin, levelMax);
            }

            bool isLevelValid(const Slot &encounter, uint8_t levelMin, uint8_t levelMax, uint8_t format, uint32_t levelRandom) noexcept
            {
                return isOriginalLevelValid(levelMin, levelMax, format, getRandomLevelPlain(encounter, levelRandom));
            }

            bool isSlotValid(const Slot &encounter, uint32_t slotRandom) noexcept
            {
                if (encounter.type == SlotType4::HoneyTree)
                {
                    return true;
                }
                return SlotMethodJ::getSlot(encounter.type, slotRandom) == encounter.slotNumber;
            }

            bool isLevelOrFixed(const Frame &frame, uint32_t levelRandom) noexcept
            {
                return frame.encounter.isFixedLevel() || isLevelValid(frame.encounter, frame.levelMin, frame.levelMax, frame.format, levelRandom);
            }

            bool isSlotValidFrom1Skip(const Frame &frame, uint32_t &result) noexcept
            {
                if (MethodJ::isLevelRand(frame.encounter))
                {
                    if (isLevelOrFixed(frame, frame.previous2()))
                    {
                        if (isSlotValid(frame.encounter, frame.previous3()))
                        {
                            result = frame.seed4();
                            return true;
                        }
                    }
                }
                else if (isSlotValid(frame.encounter, frame.previous2()))
                {
                    result = frame.seed3;
                    return true;
                }
                result = 0;
                return false;
            }

            bool isSlotValidRegular(const Frame &frame, LeadSeed &result, LeadRequired lead = LeadRequired::None) noexcept
            {
                if (MethodJ::isLevelRand(frame.encounter))
                {
                    if (isLevelOrFixed(frame, frame.previous1()))
                    {
                        if (isSlotValid(frame.encounter, frame.previous2()) && checkActivation(frame.encounter, frame.seed3, lead, result))
                        {
                            return true;
                        }
                    }
                }
                else if (isSlotValid(frame.encounter, frame.previous1()) && checkActivation(frame.encounter, frame.seed2, lead, result))
                {
                    return true;
                }
                result = LeadSeed{};
                return false;
            }

            bool tryGetMatchCuteCharmFrame(const Frame &frame, uint32_t &result) noexcept
            {
                if (isCuteCharmFail(frame.previous1()))
                {
                    result = 0;
                    return false;
                }
                return isSlotValidFrom1Skip(frame, result);
            }

            bool isSlotValidCuteCharmFail(const Frame &frame, uint32_t &result) noexcept
            {
                if (isCuteCharmPass(frame.previous1()))
                {
                    result = 0;
                    return false;
                }
                return isSlotValidFrom1Skip(frame, result);
            }

            bool isSlotValidSyncFail(const Frame &frame, uint32_t &result) noexcept
            {
                if (isSyncPass(frame.previous1()))
                {
                    result = 0;
                    return false;
                }
                return isSlotValidFrom1Skip(frame, result);
            }

            // Needs a lead 5+ levels above the encounter, which is always possible.
            bool isSlotValidIntimidate(const Frame &frame, uint32_t &result) noexcept
            {
                if (isIntimidateKeenEyePass(frame.previous1()))
                {
                    result = 0;
                    return false;
                }
                return isSlotValidFrom1Skip(frame, result);
            }

            bool isSlotValidHustleVitalFail(const Frame &frame, uint32_t &result) noexcept
            {
                if (isHustleVitalPass(frame.previous1()))
                {
                    result = 0;
                    return false;
                }
                return isSlotValidFrom1Skip(frame, result);
            }

            bool isSlotValidHustleVital(const Frame &frame, uint32_t &result) noexcept
            {
                if (isHustleVitalFail(frame.previous1()))
                {
                    result = 0;
                    return false;
                }
                if (!isOriginalLevelValid(frame.levelMin, frame.levelMax, frame.format, frame.encounter.pressureLevel))
                {
                    result = 0;
                    return false;
                }
                if (MethodJ::isLevelRand(frame.encounter))
                {
                    // The level roll is bumped to the maximum afterwards, so it is not evaluated.
                    if (isSlotValid(frame.encounter, frame.previous3()))
                    {
                        result = frame.seed4();
                        return true;
                    }
                }
                else if (isSlotValid(frame.encounter, frame.previous2()))
                {
                    result = frame.seed3;
                    return true;
                }
                result = 0;
                return false;
            }

            bool isSlotValidStaticMagnet(const Frame &frame, uint32_t &result, LeadRequired &lead) noexcept
            {
                lead = LeadRequired::None;
                if (MethodJ::isLevelRand(frame.encounter))
                {
                    if (isStaticMagnetFail(frame.previous3()))
                    {
                        result = 0;
                        return false;
                    }
                    if (isLevelOrFixed(frame, frame.previous1()))
                    {
                        if (frame.encounter.isSlotValidStaticMagnet(frame.previous2(), lead))
                        {
                            result = frame.seed4();
                            return true;
                        }
                    }
                }
                else
                {
                    if (isStaticMagnetFail(frame.previous3()))
                    {
                        result = 0;
                        return false;
                    }
                    if (frame.encounter.isSlotValidStaticMagnet(frame.previous1(), lead))
                    {
                        result = frame.seed3;
                        return true;
                    }
                }
                result = 0;
                return false;
            }

            bool isSlotValidStaticMagnetFail(const Frame &frame, uint32_t &result) noexcept
            {
                if (MethodJ::isLevelRand(frame.encounter))
                {
                    if (isStaticMagnetPass(frame.previous3()))
                    {
                        result = 0;
                        return false;
                    }
                    if (isLevelOrFixed(frame, frame.previous1()))
                    {
                        if (isSlotValid(frame.encounter, frame.previous2()))
                        {
                            result = frame.seed4();
                            return true;
                        }
                    }
                }
                else
                {
                    if (isStaticMagnetPass(frame.previous2()))
                    {
                        result = 0;
                        return false;
                    }
                    if (isSlotValid(frame.encounter, frame.previous1()))
                    {
                        result = frame.seed3;
                        return true;
                    }
                }
                result = 0;
                return false;
            }

            bool tryGetMatchNoSync(const Frame &frame, LeadSeed &result) noexcept
            {
                uint32_t seed = 0;
                if (frame.levelMin > frame.encounter.levelMax)
                {
                    // Only Pressure/Hustle/Vital Spirit can boost the level past the slot's maximum.
                    if (isSlotValidHustleVital(frame, seed) && checkActivation(frame.encounter, seed, LeadRequired::PressureHustleSpirit, result))
                    {
                        return true;
                    }
                    result = LeadSeed{};
                    return false;
                }

                if (isSlotValidRegular(frame, result))
                {
                    return true;
                }

                if (isSlotValidSyncFail(frame, seed) && checkActivation(frame.encounter, seed, LeadRequired::SynchronizeFail, result))
                {
                    return true;
                }
                if (isSlotValidCuteCharmFail(frame, seed) && checkActivation(frame.encounter, seed, LeadRequired::CuteCharmFail, result))
                {
                    return true;
                }
                if (isSlotValidHustleVitalFail(frame, seed) && checkActivation(frame.encounter, seed, LeadRequired::PressureHustleSpiritFail, result))
                {
                    return true;
                }
                if (isSlotValidStaticMagnetFail(frame, seed) && checkActivation(frame.encounter, seed, LeadRequired::StaticMagnetFail, result))
                {
                    return true;
                }
                // A failed Intimidate/Keen Eye means no encounter at all.

                LeadRequired lead = LeadRequired::None;
                if (isSlotValidStaticMagnet(frame, seed, lead) && checkActivation(frame.encounter, seed, lead, result))
                {
                    return true;
                }
                if (isSlotValidIntimidate(frame, seed) && checkActivation(frame.encounter, seed, LeadRequired::IntimidateKeenEyeFail, result))
                {
                    return true;
                }
                if (frame.levelMax >= frame.encounter.pressureLevel)
                {
                    if (isSlotValidHustleVital(frame, seed) && checkActivation(frame.encounter, seed, LeadRequired::PressureHustleSpirit, result))
                    {
                        return true;
                    }
                }

                if (canRadar(frame.encounter))
                {
                    result = LeadSeed{frame.seed1, LeadRequired::Radar};
                    return true;
                }
                result = LeadSeed{};
                return false;
            }

            bool tryGetMatch(const Slot &encounter, uint8_t levelMin, uint8_t levelMax, uint32_t seed, uint8_t nature, uint8_t format, LeadSeed &result) noexcept
            {
                const uint32_t firstRandom = seed >> 16;
                if (MethodJ::getNature(firstRandom) == nature)
                {
                    const Frame frame(encounter, seed, levelMin, levelMax, format);
                    return tryGetMatchNoSync(frame, result);
                }

                // Synchronize cannot combine with a level boost.
                if (isSyncPass(firstRandom) && levelMin <= encounter.levelMax)
                {
                    const Frame frame(encounter, seed, levelMin, levelMax, format);
                    if (isSlotValidRegular(frame, result, LeadRequired::Synchronize))
                    {
                        return true;
                    }
                    if (canRadar(encounter))
                    {
                        result = LeadSeed{frame.seed1, LeadRequired::SynchronizeRadar};
                        return true;
                    }
                }
                result = LeadSeed{};
                return false;
            }
        }

        // PKHeX MethodJ.GetSeed
        LeadSeed MethodJ::getSeed(const EncounterSlot4Parameters &encounter, uint32_t seed, uint8_t levelMin, uint8_t levelMax, uint8_t format) noexcept
        {
            const uint32_t PID = ClassicEraRNG::getSequentialPIDFrom(seed);
            const uint8_t nature = static_cast<uint8_t>(PID % 25);
            const int frameCount = getReversalWindow(seed, nature);
            return getOriginSeed(encounter, seed, nature, frameCount, levelMin, levelMax, format);
        }

        LeadSeed MethodJ::getSeed(const EncounterSlot4Parameters &encounter, uint32_t seed) noexcept
        {
            return getSeed(encounter, seed, encounter.levelMin, encounter.levelMax, FORMAT_NO_LEVEL_CHECK);
        }

        LeadSeed MethodJ::getSeed(const EncounterSlot4Parameters &encounter, uint32_t seed, uint8_t levelMin, uint8_t levelMax) noexcept
        {
            return getSeed(encounter, seed, levelMin, levelMax, FORMAT);
        }

        LeadSeed MethodJ::getOriginSeed(const EncounterSlot4Parameters &encounter, uint32_t seed, uint8_t nature, int reverseCount, uint8_t levelMin, uint8_t levelMax, uint8_t format) noexcept
        {
            LeadSeed prefer{};
            while (true)
            {
                LeadSeed result{};
                if (tryGetMatch(encounter, levelMin, levelMax, seed, nature, format, result))
                {
                    if (result.isNoRequirement())
                    {
                        return result;
                    }
                    if (result.isBetterThan(prefer))
                    {
                        prefer = result;
                    }
                }
                if (reverseCount == 0)
                {
                    return prefer;
                }
                reverseCount--;
                seed = LCRNG::previous2(seed);
            }
        }

        bool MethodJ::isEncounterCheckApplicable(SlotType4 type) noexcept
        {
            return type == SlotType4::HoneyTree || isFishingRodType(type);
        }

        uint32_t MethodJ::skipToLevelRand(const EncounterSlot4Parameters &encounter, uint32_t seed) noexcept
        {
            if (encounter.type == SlotType4::HoneyTree)
            {
                return LCRNG::next(seed);
            }
            if (!isFishingRodType(encounter.type))
            {
                return LCRNG::next2(seed);
            }
            if (encounter.isCoronetFeebasArea)
            {
                return LCRNG::next4(seed);
            }
            return LCRNG::next3(seed);
        }

        bool MethodJ::checkEncounterActivation(const EncounterSlot4Parameters &encounter, uint32_t seed, LeadRequired lead, LeadSeed &result) noexcept
        {
            return checkActivation(encounter, seed, lead, result);
        }

        // PKHeX MethodJ.TryGetMatchCuteCharm
        bool MethodJ::tryGetMatchCuteCharm(const EncounterSlot4Parameters &encounter, std::span<const uint32_t> seeds, uint8_t nature, uint8_t levelMin, uint8_t levelMax, uint8_t format, LeadSeed &result) noexcept
        {
            for (const uint32_t seed : seeds)
            {
                if (getNature(seed >> 16) != nature)
                {
                    continue;
                }
                const Frame frame(encounter, seed, levelMin, levelMax, format);
                uint32_t start = 0;
                if (!tryGetMatchCuteCharmFrame(frame, start))
                {
                    continue;
                }
                if (!checkActivation(encounter, start, LeadRequired::CuteCharm, result))
                {
                    continue;
                }
                return true;
            }
            if (canRadar(encounter))
            {
                for (const uint32_t seed : seeds)
                {
                    if (getNature(seed >> 16) != nature)
                    {
                        continue;
                    }
                    const Frame frame(encounter, seed, levelMin, levelMax, format);
                    if (isCuteCharmFail(frame.previous1()))
                    {
                        continue;
                    }
                    result = LeadSeed{frame.seed2, LeadRequired::CuteCharmRadar};
                    return true;
                }
            }
            result = LeadSeed{};
            return false;
        }

        uint32_t MethodJ::getRandomLevel(const EncounterSlot4Parameters &encounter, uint32_t levelRandom, LeadRequired lead) noexcept
        {
            if (lead == LeadRequired::PressureHustleSpirit)
            {
                return encounter.pressureLevel;
            }
            return getRandomLevelPlain(encounter, levelRandom);
        }

        // PKHeX MethodJ.GetReversalWindow
        int MethodJ::getReversalWindow(uint32_t seed, uint8_t nature) noexcept
        {
            int frameCount = 0;
            uint32_t pidHigh = seed >> 16;
            while (true)
            {
                const uint32_t pidLow = LCRNG::previous16(seed);
                const uint32_t PID = (pidHigh << 16) | pidLow;
                if (PID % 25 == nature)
                {
                    break;
                }
                pidHigh = LCRNG::previous16(seed);
                frameCount++;
            }
            return frameCount;
        }
    }
}
