/**
 * What the Gen 3 and Gen 4 wild-slot frame checks (MethodH, MethodJ, MethodK) need to know about an
 * encounter slot, and the four seeds before a nature roll that each check reads.
 *
 * PKHeX passes its encounter template through the IEncounterSlot3 / IEncounterSlot4 interfaces
 * (PKHeX.Core/Legality/Encounters/Templates/Interfaces/Properties/IEncounterSlot34.cs, IMagnetStatic.cs,
 * INumberedSlot.cs, ILevelRange.cs). The encounter engine fills one of these parameter structs from its
 * own slot instead. The slot types are PKHeX's SlotType3 (EncounterArea3.cs) and SlotType4
 * (EncounterArea4.cs), with the same values.
 */
#ifndef LEGALITY_RNG_ENCOUNTERSLOTRNG_H
#define LEGALITY_RNG_ENCOUNTERSLOTRNG_H

#include <cstdint>

#include "Legality/RNG/LCRNG.h"
#include "Legality/RNG/LeadRequired.h"

namespace Legality
{
    namespace RNG
    {
        enum class SlotType3 : uint8_t
        {
            Grass = 0,
            Surf = 1,
            OldRod = 2,
            GoodRod = 3,
            SuperRod = 4,
            RockSmash = 5,
            SwarmGrass50 = 6,
            SwarmFish50 = 7,
        };

        enum class SlotType4 : uint8_t
        {
            Grass = 0,
            Surf = 1,
            OldRod = 2,
            GoodRod = 3,
            SuperRod = 4,
            RockSmash = 5,
            Headbutt = 6,
            HeadbuttSpecial = 7,
            BugContest = 8,
            HoneyTree = 9,
            SafariGrass = 10,
            SafariSurf = 11,
            SafariOldRod = 12,
            SafariGoodRod = 13,
            SafariSuperRod = 14,
        };

        // PKHeX SlotType4Extensions.
        constexpr bool isSafari(SlotType4 type) noexcept { return type >= SlotType4::SafariGrass; }
        constexpr bool isLevelRandDPPt(SlotType4 type) noexcept { return type != SlotType4::Grass; }
        constexpr bool isLevelRandHGSS(SlotType4 type) noexcept { return type != SlotType4::Grass && !isSafari(type); }

        /// PKHeX IEncounterSlot34: ILevelRange, IMagnetStatic, INumberedSlot and ISpeciesForm.
        struct EncounterSlot34Parameters
        {
            uint16_t species = 0;
            uint8_t form = 0;
            uint8_t levelMin = 0;
            uint8_t levelMax = 0;
            uint8_t slotNumber = 0;
            uint8_t staticIndex = 0;
            uint8_t magnetPullIndex = 0;
            uint8_t staticCount = 0;
            uint8_t magnetPullCount = 0;
            uint8_t pressureLevel = 0;
            uint8_t areaRate = 0;

            constexpr bool isFixedLevel() const noexcept { return levelMin == levelMax; }
            constexpr bool isStaticSlot() const noexcept { return staticCount != 0; }
            constexpr bool isMagnetSlot() const noexcept { return magnetPullCount != 0; }

            // PKHeX MagnetStaticExtensions.IsSlotValidStaticMagnet
            constexpr bool isSlotValidStaticMagnet(uint32_t slotRandom, LeadRequired &lead) const noexcept
            {
                if (isStaticSlot() && slotRandom % staticCount == staticIndex)
                {
                    lead = LeadRequired::Static;
                    return true;
                }
                // Fishing slots never reach here with a magnet count: no fishing slot is Steel type.
                if (isMagnetSlot() && slotRandom % magnetPullCount == magnetPullIndex)
                {
                    lead = LeadRequired::MagnetPull;
                    return true;
                }
                lead = LeadRequired::None;
                return false;
            }
        };

        /// PKHeX IEncounterSlot3.
        struct EncounterSlot3Parameters : EncounterSlot34Parameters
        {
            SlotType3 type = SlotType3::Grass;
            bool isSafariHoenn = false;
        };

        /// PKHeX IEncounterSlot4, plus the two EncounterSlot4 properties MethodJ and MethodK read.
        struct EncounterSlot4Parameters : EncounterSlot34Parameters
        {
            SlotType4 type = SlotType4::Grass;
            bool isRerollMinimum31 = false;
            /// PKHeX EncounterSlot4.CanUseRadar
            bool canUseRadar = false;
            /// PKHeX EncounterArea4.IsCoronetFeebasArea (the slot's parent area)
            bool isCoronetFeebasArea = false;
        };

        // PKHeX PKHeX.Core/Legality/RNG/Frame/FrameCheckDetails.cs
        template <typename Slot>
        struct FrameCheckDetails
        {
            const Slot &encounter;
            uint8_t levelMin;
            uint8_t levelMax;
            uint32_t seed1;
            uint32_t seed2;
            uint32_t seed3;
            uint8_t format;

            constexpr FrameCheckDetails(const Slot &slot, uint32_t seed, uint8_t minimumLevel, uint8_t maximumLevel, uint8_t formatValue) noexcept : encounter(slot), levelMin(minimumLevel), levelMax(maximumLevel), seed1(LCRNG::previous(seed)), seed2(LCRNG::previous(seed1)), seed3(LCRNG::previous(seed2)), format(formatValue) {}

            constexpr uint32_t seed4() const noexcept { return LCRNG::previous(seed3); }
            constexpr uint32_t previous1() const noexcept { return seed1 >> 16; }
            constexpr uint32_t previous2() const noexcept { return seed2 >> 16; }
            constexpr uint32_t previous3() const noexcept { return seed3 >> 16; }
        };

        // PKHeX LevelRangeExtensions.IsLevelWithinRange
        constexpr bool isLevelWithinRange(uint8_t level, uint8_t minimumLevel, uint8_t maximumLevel) noexcept
        {
            return minimumLevel <= level && level <= maximumLevel;
        }
    }
}

#endif
