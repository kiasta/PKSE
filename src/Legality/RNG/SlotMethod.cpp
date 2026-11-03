#include "Legality/RNG/SlotMethod.h"

#include <span>

namespace Legality
{
    namespace RNG
    {
        namespace
        {
            // Each entry is the exclusive upper roll of its slot: slot n covers [bound(n-1), bound(n)).
            constexpr uint8_t REGULAR_BOUNDS[] = {20, 40, 50, 60, 70, 80, 85, 90, 94, 98, 99, 100};
            constexpr uint8_t SURF_BOUNDS[] = {60, 90, 95, 99, 100};
            constexpr uint8_t OLD_ROD_BOUNDS_H[] = {70, 100};
            constexpr uint8_t GOOD_ROD_BOUNDS_H[] = {60, 80, 100};
            constexpr uint8_t SUPER_ROD_BOUNDS_H[] = {40, 80, 95, 99, 100};
            constexpr uint8_t SUPER_ROD_BOUNDS_J[] = {40, 80, 95, 99, 100};
            constexpr uint8_t SUPER_ROD_BOUNDS_K[] = {40, 70, 85, 95, 100};
            constexpr uint8_t HEADBUTT_BOUNDS_K[] = {50, 65, 80, 90, 95, 100};

            constexpr SlotRange GRASS_RANGES[] = {{0, 19}, {20, 39}, {40, 49}, {50, 59}, {60, 69}, {70, 79}, {80, 84}, {85, 89}, {90, 93}, {94, 97}, {98, 98}, {99, 99}};
            constexpr SlotRange SURF_RANGES[] = {{0, 59}, {60, 89}, {90, 94}, {95, 98}, {99, 99}};
            constexpr SlotRange OLD_ROD_RANGES_H[] = {{0, 69}, {70, 99}};
            constexpr SlotRange GOOD_ROD_RANGES_H[] = {{0, 59}, {60, 79}, {80, 99}};
            // PKHeX's Gen 3 Super Rod ranges disagree with its own GetSuperRod roll table for slots 1 and 2; both are kept as PKHeX has them.
            constexpr SlotRange SUPER_ROD_RANGES_H[] = {{0, 39}, {40, 69}, {70, 94}, {95, 98}, {99, 99}};
            constexpr SlotRange SUPER_ROD_RANGES_J[] = {{0, 39}, {40, 79}, {80, 94}, {95, 98}, {99, 99}};
            constexpr SlotRange SUPER_ROD_RANGES_K[] = {{0, 39}, {40, 69}, {70, 84}, {85, 94}, {95, 99}};
            constexpr SlotRange ROCK_SMASH_RANGES_K[] = {{0, 79}, {80, 99}};
            constexpr SlotRange HEADBUTT_RANGES_K[] = {{0, 49}, {50, 64}, {65, 79}, {80, 89}, {90, 94}, {95, 99}};
            // Indexed by slot: the Bug-Catching Contest table runs from the rarest roll down.
            constexpr SlotRange BUG_CONTEST_RANGES_K[] = {{80, 99}, {60, 79}, {50, 59}, {40, 49}, {30, 39}, {20, 29}, {15, 19}, {10, 14}, {5, 9}, {0, 4}};

            constexpr SlotRange INVALID_RANGE = {INVALID_SLOT, INVALID_SLOT};

            uint8_t getSlotFromBounds(uint32_t roll, std::span<const uint8_t> bounds) noexcept
            {
                for (size_t slotIndex = 0; slotIndex < bounds.size(); slotIndex++)
                {
                    if (roll < bounds[slotIndex])
                    {
                        return static_cast<uint8_t>(slotIndex);
                    }
                }
                return INVALID_SLOT;
            }

            SlotRange getRangeFromTable(uint8_t slotNumber, std::span<const SlotRange> ranges) noexcept
            {
                if (slotNumber >= ranges.size())
                {
                    return INVALID_RANGE;
                }
                return ranges[slotNumber];
            }
        }

        // PKHeX SlotMethodH.GetSlot
        uint8_t SlotMethodH::getSlot(SlotType3 type, uint32_t random) noexcept
        {
            switch (type)
            {
            case SlotType3::Grass:
                return getRegular(random % 100);
            case SlotType3::Surf:
            case SlotType3::RockSmash:
                return getSurf(random % 100);
            case SlotType3::OldRod:
                return getOldRod(random % 100);
            case SlotType3::GoodRod:
                return getGoodRod(random % 100);
            case SlotType3::SuperRod:
                return getSuperRod(random % 100);
            case SlotType3::SwarmFish50:
            case SlotType3::SwarmGrass50:
                return (random % 100 < 50) ? 0 : INVALID_SLOT;
            default:
                return INVALID_SLOT;
            }
        }

        // PKHeX SlotMethodH.GetRange
        SlotRange SlotMethodH::getRange(SlotType3 type, uint8_t slotNumber) noexcept
        {
            switch (type)
            {
            case SlotType3::Grass:
                return getRangeGrass(slotNumber);
            case SlotType3::Surf:
            case SlotType3::RockSmash:
                return getRangeSurf(slotNumber);
            case SlotType3::OldRod:
                return getRangeOldRod(slotNumber);
            case SlotType3::GoodRod:
                return getRangeGoodRod(slotNumber);
            case SlotType3::SuperRod:
                return getRangeSuperRod(slotNumber);
            case SlotType3::SwarmFish50:
            case SlotType3::SwarmGrass50:
                return {0, 49};
            default:
                return INVALID_RANGE;
            }
        }

        uint8_t SlotMethodH::getRegular(uint32_t roll) noexcept { return getSlotFromBounds(roll, REGULAR_BOUNDS); }
        uint8_t SlotMethodH::getSurf(uint32_t roll) noexcept { return getSlotFromBounds(roll, SURF_BOUNDS); }
        uint8_t SlotMethodH::getOldRod(uint32_t roll) noexcept { return getSlotFromBounds(roll, OLD_ROD_BOUNDS_H); }
        uint8_t SlotMethodH::getGoodRod(uint32_t roll) noexcept { return getSlotFromBounds(roll, GOOD_ROD_BOUNDS_H); }
        uint8_t SlotMethodH::getSuperRod(uint32_t roll) noexcept { return getSlotFromBounds(roll, SUPER_ROD_BOUNDS_H); }

        SlotRange SlotMethodH::getRangeGrass(uint8_t slotNumber) noexcept { return getRangeFromTable(slotNumber, GRASS_RANGES); }
        SlotRange SlotMethodH::getRangeSurf(uint8_t slotNumber) noexcept { return getRangeFromTable(slotNumber, SURF_RANGES); }
        SlotRange SlotMethodH::getRangeOldRod(uint8_t slotNumber) noexcept { return getRangeFromTable(slotNumber, OLD_ROD_RANGES_H); }
        SlotRange SlotMethodH::getRangeGoodRod(uint8_t slotNumber) noexcept { return getRangeFromTable(slotNumber, GOOD_ROD_RANGES_H); }
        SlotRange SlotMethodH::getRangeSuperRod(uint8_t slotNumber) noexcept { return getRangeFromTable(slotNumber, SUPER_ROD_RANGES_H); }

        // PKHeX SlotMethodJ.GetSlot
        int SlotMethodJ::getSlot(SlotType4 type, uint32_t random) noexcept
        {
            const uint32_t encounterSlotValue = random / 656;
            switch (type)
            {
            case SlotType4::OldRod:
            case SlotType4::Surf:
                return SlotMethodH::getSurf(encounterSlotValue);
            case SlotType4::GoodRod:
            case SlotType4::SuperRod:
                return getSuperRod(encounterSlotValue);
            case SlotType4::HoneyTree:
                return 0;
            default:
                return SlotMethodH::getRegular(encounterSlotValue);
            }
        }

        // PKHeX SlotMethodJ.GetRange
        SlotRange SlotMethodJ::getRange(SlotType4 type, uint8_t slotNumber) noexcept
        {
            switch (type)
            {
            case SlotType4::OldRod:
            case SlotType4::Surf:
                return SlotMethodH::getRangeSurf(slotNumber);
            case SlotType4::GoodRod:
            case SlotType4::SuperRod:
                return getRangeSuperRod(slotNumber);
            case SlotType4::HoneyTree:
                return {0, 99};
            default:
                return SlotMethodH::getRangeGrass(slotNumber);
            }
        }

        int SlotMethodJ::getSuperRod(uint32_t roll) noexcept { return getSlotFromBounds(roll, SUPER_ROD_BOUNDS_J); }
        SlotRange SlotMethodJ::getRangeSuperRod(uint8_t slotNumber) noexcept { return getRangeFromTable(slotNumber, SUPER_ROD_RANGES_J); }

        // PKHeX SlotMethodK.GetSlot
        uint8_t SlotMethodK::getSlot(SlotType4 type, uint32_t random) noexcept
        {
            switch (type)
            {
            case SlotType4::Grass:
                return SlotMethodH::getRegular(random % 100);
            case SlotType4::Surf:
                return SlotMethodH::getSurf(random % 100);
            case SlotType4::OldRod:
            case SlotType4::GoodRod:
            case SlotType4::SuperRod:
                return getSuperRod(random % 100);
            case SlotType4::RockSmash:
                return getRockSmash(random % 100);
            case SlotType4::Headbutt:
            case SlotType4::HeadbuttSpecial:
                return getHeadbutt(random % 100);
            case SlotType4::BugContest:
                return getBugCatchingContest(random % 100);
            case SlotType4::SafariGrass:
            case SlotType4::SafariSurf:
            case SlotType4::SafariOldRod:
            case SlotType4::SafariGoodRod:
            case SlotType4::SafariSuperRod:
                return getSafari(random % 10);
            default:
                return INVALID_SLOT;
            }
        }

        // PKHeX SlotMethodK.GetRange
        SlotRange SlotMethodK::getRange(SlotType4 type, uint8_t slotNumber) noexcept
        {
            switch (type)
            {
            case SlotType4::Grass:
                return SlotMethodH::getRangeGrass(slotNumber);
            case SlotType4::Surf:
                return SlotMethodH::getRangeSurf(slotNumber);
            case SlotType4::OldRod:
            case SlotType4::GoodRod:
            case SlotType4::SuperRod:
                return getRangeSuperRod(slotNumber);
            case SlotType4::RockSmash:
                return getRangeRockSmash(slotNumber);
            case SlotType4::Headbutt:
            case SlotType4::HeadbuttSpecial:
                return getRangeHeadbutt(slotNumber);
            case SlotType4::BugContest:
                return getRangeBugCatchingContest(slotNumber);
            case SlotType4::SafariGrass:
            case SlotType4::SafariSurf:
            case SlotType4::SafariOldRod:
            case SlotType4::SafariGoodRod:
            case SlotType4::SafariSuperRod:
                return getRangeSafari(slotNumber);
            default:
                return INVALID_RANGE;
            }
        }

        uint8_t SlotMethodK::getRockSmash(uint32_t roll) noexcept { return roll >= 80 ? 1 : 0; }
        uint8_t SlotMethodK::getSuperRod(uint32_t roll) noexcept { return getSlotFromBounds(roll, SUPER_ROD_BOUNDS_K); }
        uint8_t SlotMethodK::getSafari(uint32_t roll) noexcept { return static_cast<uint8_t>(roll); }

        uint8_t SlotMethodK::getBugCatchingContest(uint32_t roll) noexcept
        {
            for (uint8_t slotNumber = 0; slotNumber < 10; slotNumber++)
            {
                if (BUG_CONTEST_RANGES_K[slotNumber].minimum <= roll && roll <= BUG_CONTEST_RANGES_K[slotNumber].maximum)
                {
                    return slotNumber;
                }
            }
            return INVALID_SLOT;
        }

        uint8_t SlotMethodK::getHeadbutt(uint32_t roll) noexcept { return getSlotFromBounds(roll, HEADBUTT_BOUNDS_K); }

        SlotRange SlotMethodK::getRangeSuperRod(uint8_t slotNumber) noexcept { return getRangeFromTable(slotNumber, SUPER_ROD_RANGES_K); }
        SlotRange SlotMethodK::getRangeRockSmash(uint8_t slotNumber) noexcept { return getRangeFromTable(slotNumber, ROCK_SMASH_RANGES_K); }
        SlotRange SlotMethodK::getRangeHeadbutt(uint8_t slotNumber) noexcept { return getRangeFromTable(slotNumber, HEADBUTT_RANGES_K); }
        SlotRange SlotMethodK::getRangeBugCatchingContest(uint8_t slotNumber) noexcept { return getRangeFromTable(slotNumber, BUG_CONTEST_RANGES_K); }
        SlotRange SlotMethodK::getRangeSafari(uint8_t slotNumber) noexcept { return {slotNumber, slotNumber}; }
    }
}
