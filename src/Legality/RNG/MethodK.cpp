#include "Legality/RNG/MethodK.h"

#include "Legality/RNG/ClassicEraRNG.h"
#include "Legality/RNG/LCRNG.h"
#include "Legality/RNG/MethodJ.h"
#include "Legality/RNG/SlotMethod.h"

namespace Legality
{
    namespace RNG
    {
        namespace
        {
            // Nature: rand % 25. Cute Charm: rand % 3 != 0 (2/3). Sync: rand & 1 == 0 (1/2).
            // Static/Magnet Pull: rand & 1 == 0. Pressure/Hustle/Vital Spirit: rand & 1 == 1.
            // Intimidate/Keen Eye: rand & 1 == 1, and 0 rejects the encounter.
            using Slot = EncounterSlot4Parameters;
            using Frame = FrameCheckDetails<Slot>;

            // PKHeX passes these as Nature values past Nature.Random (25).
            constexpr uint8_t LEAD_SYNC_ALLOWED = 25;
            constexpr uint8_t LEAD_SYNC_DISALLOWED = 26;
            constexpr uint8_t LEAD_SYNC_REQUIRED_FAIL_UNSPECIFIC = 27;

            constexpr int MAX_SAFARI_CONTEST = 4;

            bool isCuteCharmFail(uint32_t random) noexcept { return (random % 3) == 0; }
            bool isCuteCharmPass(uint32_t random) noexcept { return (random % 3) != 0; }
            bool isSyncPass(uint32_t random) noexcept { return (random & 1) == 0; }
            bool isStaticMagnetFail(uint32_t random) noexcept { return (random & 1) != 0; }
            bool isStaticMagnetPass(uint32_t random) noexcept { return (random & 1) == 0; }
            bool isHustleVitalFail(uint32_t random) noexcept { return (random & 1) != 1; }
            bool isHustleVitalPass(uint32_t random) noexcept { return (random & 1) == 1; }
            bool isIntimidateKeenEyePass(uint32_t random) noexcept { return (random & 1) == 1; }

            bool isFishingRodType(SlotType4 type) noexcept
            {
                switch (type)
                {
                case SlotType4::OldRod:
                case SlotType4::GoodRod:
                case SlotType4::SuperRod:
                case SlotType4::SafariOldRod:
                case SlotType4::SafariGoodRod:
                case SlotType4::SafariSuperRod:
                    return true;
                default:
                    return false;
                }
            }

            uint8_t getRodRate(SlotType4 type) noexcept
            {
                switch (type)
                {
                case SlotType4::OldRod:
                case SlotType4::SafariOldRod:
                    return 25;
                case SlotType4::GoodRod:
                case SlotType4::SafariGoodRod:
                    return 50;
                case SlotType4::SuperRod:
                case SlotType4::SafariSuperRod:
                    return 75;
                default:
                    return 0;
                }
            }

            // Leads Sweet Scent can trigger the Bug-Catching Contest with.
            bool isAbleToSweetScent(LeadRequired lead) noexcept
            {
                return lead == LeadRequired::None || lead == LeadRequired::IntimidateKeenEyeFail || lead == LeadRequired::PressureHustleSpirit || lead == LeadRequired::PressureHustleSpiritFail;
            }

            bool isBugContestPossibleDeadlock(uint32_t areaRate, uint32_t &result) noexcept
            {
                const uint32_t roll = (result >> 16) % 100;
                if (roll >= areaRate)
                {
                    return false;
                }
                result = LCRNG::previous2(result);
                return true;
            }

            bool isFishPossible(SlotType4 type, uint32_t &seed, LeadRequired &lead) noexcept
            {
                uint32_t rodRate = getRodRate(type);
                const uint32_t roll = (seed >> 16) % 100;
                // The +50 lands before Suction Cups / Sticky Hold, which compound it.
                rodRate += 50;
                if (roll < rodRate)
                {
                    seed = LCRNG::previous(seed);
                    return true;
                }
                if (lead != LeadRequired::None)
                {
                    return false;
                }
                if (roll < rodRate * 2)
                {
                    seed = LCRNG::previous(seed);
                    lead = LeadRequired::SuctionCups;
                    return true;
                }
                return false;
            }

            bool isRockSmashPossible(uint8_t areaRate, uint32_t &seed, LeadRequired &lead) noexcept
            {
                const uint32_t roll = (seed >> 16) % 100;
                if (roll < areaRate)
                {
                    seed = LCRNG::previous(seed);
                    return true;
                }
                if (lead != LeadRequired::None)
                {
                    return false;
                }
                if (roll < areaRate * 2u)
                {
                    seed = LCRNG::previous(seed);
                    lead = LeadRequired::Illuminate;
                    return true;
                }
                return false;
            }

            bool checkEncounterActivationCore(const Slot &encounter, uint32_t &seed, LeadRequired &lead) noexcept
            {
                if (isFishingRodType(encounter.type))
                {
                    return isFishPossible(encounter.type, seed, lead);
                }
                if (encounter.type == SlotType4::RockSmash)
                {
                    return isRockSmashPossible(encounter.areaRate, seed, lead);
                }
                if (encounter.type == SlotType4::BugContest && !isAbleToSweetScent(lead))
                {
                    return isBugContestPossibleDeadlock(encounter.areaRate, seed);
                }
                return true;
            }

            bool checkActivation(const Slot &encounter, uint32_t seed, LeadRequired lead, LeadSeed &result) noexcept
            {
                const bool pass = checkEncounterActivationCore(encounter, seed, lead);
                result = LeadSeed{seed, lead};
                return pass;
            }

            uint32_t getRandomLevelPlain(const Slot &encounter, uint32_t levelRandom) noexcept
            {
                const uint32_t modulus = 1u + encounter.levelMax - encounter.levelMin;
                return (levelRandom % modulus) + encounter.levelMin;
            }

            bool isOriginalLevelValid(uint8_t levelMin, uint8_t levelMax, uint8_t format, uint32_t level) noexcept
            {
                if (format == MethodK::FORMAT && levelMin > 1)
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
                return SlotMethodK::getSlot(encounter.type, slotRandom) == encounter.slotNumber;
            }

            bool isLevelOrFixed(const Frame &frame, uint32_t levelRandom) noexcept
            {
                return frame.encounter.isFixedLevel() || isLevelValid(frame.encounter, frame.levelMin, frame.levelMax, frame.format, levelRandom);
            }

            bool isSlotValidFrom1Skip(const Frame &frame, uint32_t &result) noexcept
            {
                if (MethodK::isLevelRand(frame.encounter))
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
                if (MethodK::isLevelRand(frame.encounter))
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
                if (MethodK::isLevelRand(frame.encounter))
                {
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
                if (MethodK::isLevelRand(frame.encounter))
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
                    if (isStaticMagnetFail(frame.previous2()))
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
                if (MethodK::isLevelRand(frame.encounter))
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
                        // PKHeX returns Seed2 here, where Method J's same branch returns Seed3; kept as PKHeX has it.
                        result = frame.seed2;
                        return true;
                    }
                }
                result = 0;
                return false;
            }

            bool tryGetMatchOnlyFailSync(const Frame &frame, LeadSeed &result) noexcept
            {
                if (frame.levelMin > frame.encounter.levelMax)
                {
                    result = LeadSeed{};
                    return false;
                }
                uint32_t seed = 0;
                if (isSlotValidSyncFail(frame, seed) && checkActivation(frame.encounter, seed, LeadRequired::SynchronizeFail, result))
                {
                    return true;
                }
                result = LeadSeed{};
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
                result = LeadSeed{};
                return false;
            }

            bool hasAny31IV(uint32_t origin) noexcept
            {
                // Hop over the two PID calls to the first IV word.
                uint32_t seed = LCRNG::next3(origin);
                if (MethodK::isAny31(seed >> 16))
                {
                    return true;
                }
                return MethodK::isAny31(LCRNG::next16(seed));
            }

            struct SearchContext
            {
                const Slot &encounter;
                uint8_t levelMin;
                uint8_t levelMax;
                uint8_t currentEntityFormat;
                bool isRerollMinimum31;
                bool mustFailAllPreviousRerolls;

                Frame getFrame(uint32_t seed) const noexcept { return Frame(encounter, seed, levelMin, levelMax, currentEntityFormat); }

                bool canAcceptDirectMatch(int depth) const noexcept { return !mustFailAllPreviousRerolls || depth == (MAX_SAFARI_CONTEST - 1); }

                bool shouldRecurse(int depth) const noexcept { return isRerollMinimum31 && depth < MAX_SAFARI_CONTEST; }
            };

            SearchContext getSearchContext(const Slot &encounter, uint32_t seed, uint8_t levelMin, uint8_t levelMax, uint8_t format) noexcept
            {
                // A spread with a 31 IV arises about 53% of the time across the four tries.
                const bool isRerollMinimum31 = encounter.isRerollMinimum31;
                const bool mustFailAllPreviousRerolls = isRerollMinimum31 && !hasAny31IV(seed);
                return SearchContext{encounter, levelMin, levelMax, format, isRerollMinimum31, mustFailAllPreviousRerolls};
            }

            LeadSeed getSeedCore(const SearchContext &context, uint32_t seed, int depth, uint8_t forceSyncLead) noexcept;

            bool recurseReject(const SearchContext &context, uint32_t seed, LeadSeed &result, int depth, uint8_t forceSyncLead) noexcept
            {
                result = LeadSeed{};
                const uint32_t secondIVs = LCRNG::previous16(seed);
                if (MethodK::isAny31(secondIVs))
                {
                    return false;
                }
                const uint32_t firstIVs = LCRNG::previous16(seed);
                if (MethodK::isAny31(firstIVs))
                {
                    return false;
                }
                // Back before the rejected spread's PID frames.
                seed = LCRNG::previous3(seed);
                result = getSeedCore(context, seed, depth, forceSyncLead);
                return result.isValid();
            }

            bool tryGetMatch(const SearchContext &context, uint32_t seed, uint8_t nature, LeadSeed &result, int depth, uint8_t forceSyncLead) noexcept
            {
                const uint32_t firstRandom = seed >> 16;
                if (MethodK::getNature(firstRandom) == nature)
                {
                    const Frame frame = context.getFrame(seed);
                    if (forceSyncLead == LEAD_SYNC_ALLOWED || forceSyncLead == LEAD_SYNC_DISALLOWED)
                    {
                        if (tryGetMatchNoSync(frame, result) && context.canAcceptDirectMatch(depth))
                        {
                            return true;
                        }
                    }
                    else if (tryGetMatchOnlyFailSync(frame, result) && context.canAcceptDirectMatch(depth))
                    {
                        // A specific Synchronize nature was locked in by a deeper reroll: report the end result.
                        if (forceSyncLead != LEAD_SYNC_REQUIRED_FAIL_UNSPECIFIC)
                        {
                            result.lead = LeadRequired::Synchronize;
                        }
                        return true;
                    }

                    if (context.shouldRecurse(depth))
                    {
                        if (forceSyncLead == LEAD_SYNC_ALLOWED || forceSyncLead == LEAD_SYNC_DISALLOWED)
                        {
                            if (recurseReject(context, seed, result, depth + 1, LEAD_SYNC_DISALLOWED))
                            {
                                return true;
                            }
                        }
                        const uint32_t previousSeed = frame.seed1;
                        // Synchronize cannot combine with a level boost.
                        if (forceSyncLead != LEAD_SYNC_DISALLOWED && !isSyncPass(previousSeed >> 16) && context.levelMin <= context.encounter.levelMax)
                        {
                            const uint8_t lead = forceSyncLead == LEAD_SYNC_ALLOWED ? LEAD_SYNC_REQUIRED_FAIL_UNSPECIFIC : forceSyncLead;
                            if (recurseReject(context, previousSeed, result, depth + 1, lead))
                            {
                                return true;
                            }
                        }
                    }
                }

                if (forceSyncLead != LEAD_SYNC_ALLOWED && forceSyncLead != LEAD_SYNC_REQUIRED_FAIL_UNSPECIFIC)
                {
                    if (forceSyncLead != nature)
                    {
                        result = LeadSeed{};
                        return false;
                    }
                }

                if (isSyncPass(firstRandom) && context.levelMin <= context.encounter.levelMax)
                {
                    const Frame frame = context.getFrame(seed);
                    if (isSlotValidRegular(frame, result, LeadRequired::Synchronize) && context.canAcceptDirectMatch(depth))
                    {
                        return true;
                    }
                    if (context.shouldRecurse(depth))
                    {
                        if (recurseReject(context, seed, result, depth + 1, nature))
                        {
                            return true;
                        }
                    }
                }
                result = LeadSeed{};
                return false;
            }

            LeadSeed getOriginSeedCore(const SearchContext &context, uint32_t seed, uint8_t nature, int reverseCount, int depth, uint8_t forceSyncLead) noexcept
            {
                LeadSeed prefer{};
                while (true)
                {
                    LeadSeed result{};
                    if (tryGetMatch(context, seed, nature, result, depth, forceSyncLead))
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

            LeadSeed getSeedCore(const SearchContext &context, uint32_t seed, int depth, uint8_t forceSyncLead) noexcept
            {
                const uint32_t PID = ClassicEraRNG::getSequentialPIDFrom(seed);
                const uint8_t nature = static_cast<uint8_t>(PID % 25);
                const int frameCount = MethodK::getReversalWindow(seed, nature);
                return getOriginSeedCore(context, seed, nature, frameCount, depth, forceSyncLead);
            }
        }

        // PKHeX MethodK.GetSeed
        LeadSeed MethodK::getSeed(const EncounterSlot4Parameters &encounter, uint32_t seed, uint8_t levelMin, uint8_t levelMax, uint8_t format) noexcept
        {
            const SearchContext context = getSearchContext(encounter, seed, levelMin, levelMax, format);
            return getSeedCore(context, seed, 0, LEAD_SYNC_ALLOWED);
        }

        LeadSeed MethodK::getSeed(const EncounterSlot4Parameters &encounter, uint32_t seed) noexcept
        {
            return getSeed(encounter, seed, encounter.levelMin, encounter.levelMax, FORMAT_NO_LEVEL_CHECK);
        }

        LeadSeed MethodK::getSeed(const EncounterSlot4Parameters &encounter, uint32_t seed, uint8_t levelMin, uint8_t levelMax) noexcept
        {
            return getSeed(encounter, seed, levelMin, levelMax, FORMAT);
        }

        int MethodK::getReversalWindow(uint32_t seed, uint8_t nature) noexcept
        {
            return MethodJ::getReversalWindow(seed, nature);
        }

        LeadSeed MethodK::getOriginSeed(const EncounterSlot4Parameters &encounter, uint32_t seed, uint8_t nature, int reverseCount, uint8_t levelMin, uint8_t levelMax, uint8_t format) noexcept
        {
            const SearchContext context = getSearchContext(encounter, seed, levelMin, levelMax, format);
            return getOriginSeedCore(context, seed, nature, reverseCount, 0, LEAD_SYNC_ALLOWED);
        }

        bool MethodK::isEncounterCheckApplicable(SlotType4 type) noexcept
        {
            return type == SlotType4::RockSmash || type == SlotType4::BugContest || isFishingRodType(type);
        }

        uint32_t MethodK::skipToLevelRand(const EncounterSlot4Parameters &encounter, uint32_t seed, LeadRequired lead) noexcept
        {
            if (isFishingRodType(encounter.type) || encounter.type == SlotType4::RockSmash)
            {
                return LCRNG::next3(seed);
            }
            if (encounter.type == SlotType4::BugContest && !isAbleToSweetScent(lead))
            {
                return LCRNG::next4(seed);
            }
            return LCRNG::next2(seed);
        }

        bool MethodK::checkEncounterActivation(const EncounterSlot4Parameters &encounter, uint32_t seed, LeadRequired lead, LeadSeed &result) noexcept
        {
            return checkActivation(encounter, seed, lead, result);
        }

        // PKHeX MethodK.TryGetMatchCuteCharm
        bool MethodK::tryGetMatchCuteCharm(const EncounterSlot4Parameters &encounter, std::span<const uint32_t> seeds, uint8_t nature, uint8_t levelMin, uint8_t levelMax, uint8_t format, LeadSeed &result) noexcept
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
            result = LeadSeed{};
            return false;
        }

        uint32_t MethodK::getRandomLevel(const EncounterSlot4Parameters &encounter, uint32_t levelRandom, LeadRequired lead) noexcept
        {
            if (lead == LeadRequired::PressureHustleSpirit)
            {
                return encounter.pressureLevel;
            }
            return getRandomLevelPlain(encounter, levelRandom);
        }
    }
}
