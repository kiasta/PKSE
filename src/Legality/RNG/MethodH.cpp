#include "Legality/RNG/MethodH.h"

#include <span>

#include "Enums/GameVersion.h"
#include "Legality/RNG/ClassicEraRNG.h"
#include "Legality/RNG/LCRNG.h"
#include "Legality/RNG/LCRNGReversal.h"
#include "Legality/RNG/MethodJ.h"
#include "Legality/RNG/SlotMethod.h"
#include "Pokemon/PersonalRecord.h"
#include "Pokemon/Pokemon3FRLG.h"

namespace Legality
{
    namespace RNG
    {
        namespace
        {
            // Random determinations, each a rand() result:
            // Nature: rand % 25 == nature. Cute Charm: rand % 3 != 0 (2/3). Sync: rand & 1 == 0 (1/2).
            // Static/Magnet Pull: rand & 1 == 0. Pressure/Hustle/Vital Spirit: rand & 1 == 1.
            // Intimidate/Keen Eye: rand & 1 == 1, and 0 rejects the encounter.
            using Slot = EncounterSlot3Parameters;
            using Frame = FrameCheckDetails<Slot>;

            constexpr uint16_t UNOWN_SPECIES = 201;
            constexpr uint8_t VERSION_EMERALD = static_cast<uint8_t>(Enums::GameVersion::EM);
            constexpr uint8_t RATIO_MAGIC_MALE = 0;
            constexpr uint8_t RATIO_MAGIC_FEMALE = 254;
            constexpr uint8_t RATIO_MAGIC_GENDERLESS = 255;
            constexpr uint16_t MAX_ENCOUNTER_RATE = 2880;

            bool isCuteCharmFail(uint32_t random) noexcept { return (random % 3) == 0; }
            bool isCuteCharmPass(uint32_t random) noexcept { return (random % 3) != 0; }
            bool isSyncPass(uint32_t random) noexcept { return (random & 1) == 0; }
            bool isStaticMagnetFail(uint32_t random) noexcept { return (random & 1) != 0; }
            bool isStaticMagnetPass(uint32_t random) noexcept { return (random & 1) == 0; }
            bool isHustleVitalFail(uint32_t random) noexcept { return (random & 1) != 1; }
            bool isHustleVitalPass(uint32_t random) noexcept { return (random & 1) == 1; }
            bool isIntimidateKeenEyePass(uint32_t random) noexcept { return (random & 1) == 1; }
            bool isSafariBlockProc(uint32_t seed) noexcept { return (seed >> 16) % 100 < 80; }
            uint32_t getNature(uint32_t random) noexcept { return random % 25; }

            bool isMatchGender(uint32_t PID, uint8_t genderMin, uint8_t genderMax) noexcept
            {
                const uint32_t genderValue = PID & 0xFF;
                return genderMin <= genderValue && genderValue <= genderMax;
            }

            // PKHeX PersonalInfo.GetGenderMinMax
            void getGenderMinMax(uint8_t gender, uint8_t ratio, uint8_t &minimum, uint8_t &maximum) noexcept
            {
                minimum = 0;
                maximum = 255;
                if (ratio == RATIO_MAGIC_MALE || ratio == RATIO_MAGIC_FEMALE || ratio == RATIO_MAGIC_GENDERLESS)
                {
                    return;
                }
                if (gender == 0)
                {
                    minimum = ratio;
                }
                else if (gender == 1)
                {
                    maximum = static_cast<uint8_t>(ratio - 1);
                }
            }

            uint8_t getEmeraldGenderRatio(uint16_t species) noexcept
            {
                return Pokemon::getPersonalRecord(Enums::GameVersion::RSE, species, 0).genderRatio;
            }

            bool isFishingRodType(SlotType3 type) noexcept
            {
                return type == SlotType3::OldRod || type == SlotType3::GoodRod || type == SlotType3::SuperRod || type == SlotType3::SwarmFish50;
            }

            uint32_t getEncounterRate(uint8_t areaRate, LeadRequired lead, bool ignoreAbility) noexcept
            {
                // Assumes no bike, Cleanse Tag or Black Flute, and a White Flute (+50%).
                uint32_t encounterRate = areaRate * 16u;
                encounterRate += (encounterRate >> 1);
                if (!ignoreAbility && lead == LeadRequired::None)
                {
                    // Illuminate and Arena Trap double the rate.
                    encounterRate <<= 1;
                }
                return encounterRate;
            }

            bool isRatePass(uint32_t seed, uint8_t areaRate, LeadRequired lead = LeadRequired::None, bool ignoreAbility = true) noexcept
            {
                const uint32_t random = seed >> 16;
                return random % MAX_ENCOUNTER_RATE < getEncounterRate(areaRate, lead, ignoreAbility);
            }

            bool isRockSmashPossible(uint8_t areaRate, uint32_t &seed) noexcept
            {
                // Emerald's Rock Smash ignores the lead's ability.
                if (isRatePass(seed, areaRate))
                {
                    seed = LCRNG::previous(seed);
                    return true;
                }
                return false;
            }

            bool checkEncounterActivationCore(const Slot &encounter, uint32_t &seed) noexcept
            {
                if (encounter.type == SlotType3::RockSmash)
                {
                    return isRockSmashPossible(encounter.areaRate, seed);
                }
                // Fishing waits for the hook, and Sweet Scent can trigger anything else.
                return true;
            }

            bool checkActivation(const Slot &encounter, uint32_t seed, LeadRequired lead, LeadSeed &result) noexcept
            {
                const bool pass = checkEncounterActivationCore(encounter, seed);
                result = LeadSeed{seed, lead};
                return pass;
            }

            uint32_t getRandomLevelPlain(const EncounterSlot34Parameters &encounter, uint32_t levelRandom) noexcept
            {
                const uint32_t modulus = 1u + encounter.levelMax - encounter.levelMin;
                return (levelRandom % modulus) + encounter.levelMin;
            }

            uint32_t getRandomLevelMinus1(const EncounterSlot34Parameters &encounter, uint32_t levelRandom) noexcept
            {
                const uint32_t modulus = 1u + encounter.levelMax - encounter.levelMin;
                uint32_t bias = levelRandom % modulus;
                if (bias != 0)
                {
                    bias--;
                }
                return encounter.levelMin + bias;
            }

            bool isOriginalLevelValid(uint8_t levelMin, uint8_t levelMax, uint8_t format, uint32_t level) noexcept
            {
                if (format == MethodH::FORMAT && levelMin > 1)
                {
                    return level == levelMin;
                }
                return isLevelWithinRange(static_cast<uint8_t>(level), levelMin, levelMax);
            }

            bool isLevelValid(const Slot &encounter, uint8_t levelMin, uint8_t levelMax, uint8_t format, uint32_t levelRandom) noexcept
            {
                return isOriginalLevelValid(levelMin, levelMax, format, getRandomLevelPlain(encounter, levelRandom));
            }

            bool isLevelValidMinus1(const Slot &encounter, uint8_t levelMin, uint8_t levelMax, uint8_t format, uint32_t levelRandom) noexcept
            {
                return isOriginalLevelValid(levelMin, levelMax, format, getRandomLevelMinus1(encounter, levelRandom));
            }

            bool isSlotValid(const Slot &encounter, uint32_t slotRandom) noexcept
            {
                return SlotMethodH::getSlot(encounter.type, slotRandom) == encounter.slotNumber;
            }

            // -3 ESV, -2 level, -1 the proc (already checked), 0 nature.
            bool isSlotValidFrom1Skip(const Frame &frame, uint32_t &result) noexcept
            {
                if (isLevelValid(frame.encounter, frame.levelMin, frame.levelMax, frame.format, frame.previous2()))
                {
                    if (isSlotValid(frame.encounter, frame.previous3()))
                    {
                        result = frame.seed4();
                        return true;
                    }
                }
                result = 0;
                return false;
            }

            // As isSlotValidFrom1Skip, with the level range biased down by 1.
            bool isSlotValidFrom1SkipMinus1(const Frame &frame, uint32_t &result) noexcept
            {
                if (isLevelValidMinus1(frame.encounter, frame.levelMin, frame.levelMax, frame.format, frame.previous2()))
                {
                    if (isSlotValid(frame.encounter, frame.previous3()))
                    {
                        result = frame.seed4();
                        return true;
                    }
                }
                result = 0;
                return false;
            }

            // -2 ESV, -1 level, 0 nature.
            bool isSlotValidRegular(const Frame &frame, LeadSeed &result, LeadRequired lead = LeadRequired::None) noexcept
            {
                if (isLevelValid(frame.encounter, frame.levelMin, frame.levelMax, frame.format, frame.previous1()))
                {
                    if (isSlotValid(frame.encounter, frame.previous2()) && checkActivation(frame.encounter, frame.seed3, lead, result))
                    {
                        return true;
                    }
                }
                result = LeadSeed{};
                return false;
            }

            bool tryGetMatchCuteCharm(const Frame &frame, uint32_t &result) noexcept
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

            // The level-adequacy roll aborts the encounter when it passes.
            bool isSlotValidIntimidate(const Frame &frame, uint32_t &result) noexcept
            {
                if (isIntimidateKeenEyePass(frame.previous1()))
                {
                    result = 0;
                    return false;
                }
                return isSlotValidFrom1Skip(frame, result);
            }

            // A failed max-level roll lowers the level range by 1 where it can.
            bool isSlotValidHustleVitalFail(const Frame &frame, uint32_t &result) noexcept
            {
                if (isHustleVitalPass(frame.previous1()))
                {
                    result = 0;
                    return false;
                }
                return isSlotValidFrom1SkipMinus1(frame, result);
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
                // The level roll is bumped to the maximum afterwards, so it is not evaluated.
                if (isSlotValid(frame.encounter, frame.previous3()))
                {
                    result = frame.seed4();
                    return true;
                }
                result = 0;
                return false;
            }

            // -3 slot proc, -2 ESV, -1 level, 0 nature.
            bool isSlotValidStaticMagnet(const Frame &frame, uint32_t &result, LeadRequired &lead) noexcept
            {
                lead = LeadRequired::None;
                if (isStaticMagnetFail(frame.previous3()))
                {
                    result = 0;
                    return false;
                }
                if (isLevelValid(frame.encounter, frame.levelMin, frame.levelMax, frame.format, frame.previous1()))
                {
                    if (frame.encounter.isSlotValidStaticMagnet(frame.previous2(), lead))
                    {
                        result = frame.seed4();
                        return true;
                    }
                }
                result = 0;
                return false;
            }

            bool isSlotValidStaticMagnetFail(const Frame &frame, uint32_t &result) noexcept
            {
                if (isStaticMagnetPass(frame.previous3()))
                {
                    result = 0;
                    return false;
                }
                if (isLevelValid(frame.encounter, frame.levelMin, frame.levelMax, frame.format, frame.previous1()))
                {
                    if (isSlotValid(frame.encounter, frame.previous2()))
                    {
                        result = frame.seed4();
                        return true;
                    }
                }
                result = 0;
                return false;
            }

            bool tryGetMatchNoSync(const Frame &frame, LeadSeed &result) noexcept
            {
                if (isSlotValidRegular(frame, result))
                {
                    return true;
                }

                uint32_t seed = 0;
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
                if (isSlotValidHustleVital(frame, seed) && checkActivation(frame.encounter, seed, LeadRequired::PressureHustleSpirit, result))
                {
                    return true;
                }
                if (isSlotValidIntimidate(frame, seed) && checkActivation(frame.encounter, seed, LeadRequired::IntimidateKeenEyeFail, result))
                {
                    return true;
                }
                if (tryGetMatchCuteCharm(frame, seed) && checkActivation(frame.encounter, seed, LeadRequired::CuteCharm, result))
                {
                    return true;
                }

                result = LeadSeed{};
                return false;
            }

            // The Hoenn Safari Zone's nature preference table costs 300 calls: 0xC048C851 and 0x196302B4 step back 300.
            uint32_t getSafariBlockSeed(uint32_t seed) noexcept
            {
                return (0xC048C851u * seed) + 0x196302B4u;
            }

            bool tryGetMatchNoLead(const Slot &encounter, uint8_t levelMin, uint8_t levelMax, uint32_t seed, uint8_t nature, uint8_t format, LeadSeed &result) noexcept
            {
                const uint32_t firstRandom = seed >> 16;

                const bool hoennSafari = encounter.isSafariHoenn;
                if (hoennSafari)
                {
                    const uint32_t safariBlockSeed = getSafariBlockSeed(seed);
                    if (isSafariBlockProc(safariBlockSeed))
                    {
                        const Frame frame(encounter, safariBlockSeed, levelMin, levelMax, format);
                        if (isSlotValidRegular(frame, result))
                        {
                            return true;
                        }
                    }
                }
                if (encounter.species == UNOWN_SPECIES)
                {
                    // Unown rolls no nature: shift one frame so the seeds line up with their uses.
                    const uint32_t noNatureCallUsed = LCRNG::next(seed);
                    const Frame frame(encounter, noNatureCallUsed, levelMin, levelMax, format);
                    if (isSlotValidRegular(frame, result))
                    {
                        return true;
                    }
                }
                else if (getNature(firstRandom) == nature)
                {
                    // Without a Safari block placed, a dummy call still sits between the nature and the frames before it.
                    if (hoennSafari)
                    {
                        seed = LCRNG::previous(seed);
                    }

                    const Frame frame(encounter, seed, levelMin, levelMax, format);
                    if (isSlotValidRegular(frame, result))
                    {
                        return true;
                    }
                }
                result = LeadSeed{};
                return false;
            }

            bool tryGetMatch(const Slot &encounter, uint8_t levelMin, uint8_t levelMax, uint32_t seed, uint8_t nature, uint8_t format, LeadSeed &result) noexcept
            {
                const uint32_t firstRandom = seed >> 16;

                if (encounter.isSafariHoenn)
                {
                    const uint32_t safariBlockSeed = getSafariBlockSeed(seed);
                    if (isSafariBlockProc(safariBlockSeed))
                    {
                        const Frame frame(encounter, safariBlockSeed, levelMin, levelMax, format);
                        if (tryGetMatchNoSync(frame, result))
                        {
                            return true;
                        }
                    }

                    // Without a Safari block placed, a dummy call sits between the nature and the frames before it.
                    seed = LCRNG::previous(seed);
                }

                if (isSyncPass(firstRandom))
                {
                    const Frame frame(encounter, seed, levelMin, levelMax, format);
                    if (isSlotValidRegular(frame, result, LeadRequired::Synchronize))
                    {
                        return true;
                    }
                }
                if (getNature(firstRandom) == nature)
                {
                    const Frame frame(encounter, seed, levelMin, levelMax, format);
                    if (tryGetMatchNoSync(frame, result))
                    {
                        return true;
                    }
                }
                result = LeadSeed{};
                return false;
            }

            LeadSeed getOriginSeedEmerald(const Slot &encounter, uint32_t seed, uint8_t nature, int reverseCount, int reverseCuteCount, uint8_t levelMin, uint8_t levelMax, uint8_t format) noexcept
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
                        // With no lead found yet, Cute Charm frames are worth checking.
                        if (reverseCuteCount == 0 || prefer.isValid())
                        {
                            return prefer;
                        }
                        break;
                    }
                    reverseCount--;
                    seed = LCRNG::previous2(seed);
                }

                while (true)
                {
                    LeadSeed result{};
                    if (tryGetMatch(encounter, levelMin, levelMax, seed, nature, format, result) && result.isNoRequirement())
                    {
                        result.lead = LeadRequired::CuteCharm;
                        if (result.isBetterThan(prefer))
                        {
                            prefer = result;
                        }
                    }
                    reverseCuteCount--;
                    if (reverseCuteCount == 0)
                    {
                        return prefer;
                    }
                    seed = LCRNG::previous2(seed);
                }
            }

            LeadSeed getOriginSeedRegular(const Slot &encounter, uint32_t seed, uint8_t nature, int reverseCount, uint8_t levelMin, uint8_t levelMax, uint8_t format) noexcept
            {
                while (true)
                {
                    LeadSeed result{};
                    if (tryGetMatchNoLead(encounter, levelMin, levelMax, seed, nature, format, result))
                    {
                        return result;
                    }
                    if (reverseCount == 0)
                    {
                        break;
                    }
                    reverseCount--;
                    seed = LCRNG::previous2(seed);
                }
                return LeadSeed{};
            }

            struct CuteCharmWindow
            {
                int noLead;
                int cute;
            };

            CuteCharmWindow getReversalWindowCute(uint32_t seed, uint8_t nature, uint8_t genderMin, uint8_t genderMax) noexcept
            {
                int noLead = 0;
                int frameCount = 0;
                uint32_t pidHigh = seed >> 16;
                while (true)
                {
                    const uint32_t pidLow = LCRNG::previous16(seed);
                    const uint32_t PID = (pidHigh << 16) | pidLow;
                    if (PID % 25 == nature)
                    {
                        if (noLead == 0)
                        {
                            noLead = frameCount;
                        }
                        if (isMatchGender(PID, genderMin, genderMax))
                        {
                            return CuteCharmWindow{noLead, frameCount - noLead};
                        }
                    }
                    pidHigh = LCRNG::previous16(seed);
                    frameCount++;
                }
            }

            int getReversalWindowUnown(uint32_t seed, uint8_t form) noexcept
            {
                int frameCount = 0;
                uint32_t pidLow = seed >> 16;
                while (true)
                {
                    const uint32_t pidHigh = LCRNG::previous16(seed);
                    const uint32_t PID = (pidHigh << 16) | pidLow;
                    if (Pokemon::unownFormFromPID(PID) == form)
                    {
                        break;
                    }
                    pidLow = LCRNG::previous16(seed);
                    frameCount++;
                }
                return frameCount;
            }
        }

        bool MethodHWindowInfo::shouldRevise(uint16_t species) const noexcept
        {
            if (type == MethodHCondition::Empty)
            {
                return true;
            }
            return type == MethodHCondition::Emerald && getEmeraldGenderRatio(species) != gender;
        }

        // PKHeX MethodH.GetSeed
        LeadSeed MethodH::getSeed(const EncounterSlot3Parameters &encounter, uint32_t seed, uint8_t levelMin, uint8_t levelMax, bool emerald, uint8_t gender, uint8_t format) noexcept
        {
            const uint32_t PID = encounter.species != UNOWN_SPECIES ? ClassicEraRNG::getSequentialPIDFrom(seed) : ClassicEraRNG::getReversePIDFrom(seed);
            const uint8_t nature = static_cast<uint8_t>(PID % 25);

            const MethodHWindowInfo info = getReversalWindow(encounter, seed, nature, emerald, gender);
            return getOriginSeed(info, encounter, seed, nature, levelMin, levelMax, format);
        }

        LeadSeed MethodH::getSeed(const EncounterSlot3Parameters &encounter, uint32_t seed, bool emerald, uint8_t gender) noexcept
        {
            return getSeed(encounter, seed, encounter.levelMin, encounter.levelMax, emerald, gender, FORMAT_NO_LEVEL_CHECK);
        }

        LeadSeed MethodH::getSeed(const EncounterSlot3Parameters &encounter, uint32_t seed, bool emerald, uint8_t gender, uint8_t levelMin, uint8_t levelMax) noexcept
        {
            return getSeed(encounter, seed, levelMin, levelMax, emerald, gender, FORMAT);
        }

        MethodHCondition MethodH::getCondition(uint16_t species, uint8_t version) noexcept
        {
            if (species == UNOWN_SPECIES)
            {
                return MethodHCondition::Unown;
            }
            if (version == VERSION_EMERALD)
            {
                return MethodHCondition::Emerald;
            }
            return MethodHCondition::Regular;
        }

        // PKHeX MethodH.GetReversalWindow
        MethodHWindowInfo MethodH::getReversalWindow(const EncounterSlot3Parameters &encounter, uint32_t seed, uint8_t nature, bool emerald, uint8_t gender) noexcept
        {
            if (emerald)
            {
                const uint8_t ratio = getEmeraldGenderRatio(encounter.species);
                if (ratio == RATIO_MAGIC_GENDERLESS)
                {
                    return MethodHWindowInfo{static_cast<uint16_t>(MethodJ::getReversalWindow(seed, nature)), MethodHCondition::Emerald};
                }

                uint8_t genderMin = 0;
                uint8_t genderMax = 0;
                getGenderMinMax(gender, ratio, genderMin, genderMax);
                const CuteCharmWindow window = getReversalWindowCute(seed, nature, genderMin, genderMax);
                return MethodHWindowInfo{static_cast<uint16_t>(window.noLead), MethodHCondition::Emerald, ratio, static_cast<uint16_t>(window.cute)};
            }
            // Unown is never in Emerald.
            if (encounter.species == UNOWN_SPECIES)
            {
                return MethodHWindowInfo{static_cast<uint16_t>(getReversalWindowUnown(seed, encounter.form)), MethodHCondition::Unown};
            }
            return MethodHWindowInfo{static_cast<uint16_t>(MethodJ::getReversalWindow(seed, nature)), MethodHCondition::Regular};
        }

        LeadSeed MethodH::getOriginSeed(const MethodHWindowInfo &info, const EncounterSlot3Parameters &encounter, uint32_t seed, uint8_t nature, uint8_t levelMin, uint8_t levelMax, uint8_t format) noexcept
        {
            if (info.type == MethodHCondition::Emerald)
            {
                return getOriginSeedEmerald(encounter, seed, nature, info.countRegular, info.countCute, levelMin, levelMax, format);
            }
            return getOriginSeedRegular(encounter, seed, nature, info.countRegular, levelMin, levelMax, format);
        }

        bool MethodH::isEncounterCheckApplicable(SlotType3 type) noexcept
        {
            // Fishing can use Sticky Hold/Suction Cups along with the friendship boost.
            return type == SlotType3::RockSmash;
        }

        uint32_t MethodH::skipToLevelRand(const EncounterSlot3Parameters &encounter, uint32_t seed) noexcept
        {
            if (encounter.type == SlotType3::RockSmash)
            {
                return LCRNG::next3(seed);
            }
            if (isFishingRodType(encounter.type))
            {
                return LCRNG::next2(seed);
            }
            return LCRNG::next2(seed);
        }

        bool MethodH::checkEncounterActivation(const EncounterSlot3Parameters &encounter, uint32_t seed, LeadRequired lead, LeadSeed &result) noexcept
        {
            return checkActivation(encounter, seed, lead, result);
        }

        uint32_t MethodH::getRandomLevel(const EncounterSlot34Parameters &encounter, uint32_t levelRandom, LeadRequired lead) noexcept
        {
            switch (lead)
            {
            case LeadRequired::PressureHustleSpiritFail:
                return getRandomLevelMinus1(encounter, levelRandom);
            case LeadRequired::PressureHustleSpirit:
                return encounter.levelMax;
            default:
                return getRandomLevelPlain(encounter, levelRandom);
            }
        }

        bool MethodH::tryGetSeedTrainerID(uint16_t TID16, uint16_t SID16, uint32_t &seed) noexcept
        {
            uint32_t seeds[LCRNG::MAX_PID_SEED_COUNT];
            const int seedCount = LCRNGReversal::getSeeds(seeds, static_cast<uint32_t>(SID16) << 16, static_cast<uint32_t>(TID16) << 16);
            if (seedCount == 0)
            {
                seed = 0;
                return false;
            }
            seed = seeds[0];
            return true;
        }

        bool MethodH::tryGetShinySID(uint16_t TID16, uint16_t &SID16, uint32_t shinyXor, uint32_t bits) noexcept
        {
            for (int i = 0; i < 8; i++, bits++)
            {
                const uint16_t newSID16 = static_cast<uint16_t>(shinyXor ^ (bits & 7));
                uint32_t seed = 0;
                if (!tryGetSeedTrainerID(TID16, newSID16, seed))
                {
                    continue;
                }
                SID16 = newSID16;
                return true;
            }
            SID16 = 0;
            return false;
        }
    }
}
