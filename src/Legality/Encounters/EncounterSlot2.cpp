#include "Legality/Encounters/EncounterSlot2.h"

#include <algorithm>
#include <array>

#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterTransfer7.h"
#include "Legality/Encounters/GBRestrictions.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            constexpr uint16_t SPECIES_UNOWN = 201;
            /// Unown's letters A-Z; ! and ? are never a wild Gen 2 slot.
            constexpr uint8_t UNOWN_LETTER_COUNT = 26;

            /// PKHeX EncounterSlot2.TreeIndexes: the headbutt locations, sorted.
            constexpr std::array<uint8_t, 19> TREE_LOCATIONS = {2, 4, 5, 8, 11, 12, 14, 15, 18, 20, 21, 25, 26, 34, 37, 38, 39, 91, 92};

            /// PKHeX EncounterSlot2.Trees: per location, bits 0-9 for the common trees and 12-21 for
            /// the rare ones, one per last digit of the trainer id.
            constexpr std::array<uint32_t, 19> TREE_PERMISSIONS = {
                0x3FF3FF, // Route 29
                0x0FF3FF, // Route 30
                0x3FE3FF, // Route 31
                0x3EE3FF, // Route 32
                0x2403FF, // Route 33
                0x37F3FF, // Azalea Town
                0x3FF3FF, // Ilex Forest
                0x0013FE, // Route 34
                0x2613FF, // Route 35
                0x3FF3FF, // Route 36
                0x2B93FF, // Route 37
                0x3FF3FF, // Route 38
                0x1843FF, // Route 39
                0x3FF3FF, // Route 42
                0x3FF3FF, // Route 43
                0x3FF3FF, // Lake of Rage
                0x2FF3FF, // Route 44
                0x2001FF, // Route 26
                0x2BB3FF, // Route 27
            };

            /// PKHeX SlotType2.ToString() with '_' replaced by a space.
            const char *getSlotTypeName(SlotType2 type) noexcept
            {
                switch (type)
                {
                case SlotType2::Grass:
                    return "Grass";
                case SlotType2::Surf:
                    return "Surf";
                case SlotType2::OldRod:
                    return "Old Rod";
                case SlotType2::GoodRod:
                    return "Good Rod";
                case SlotType2::SuperRod:
                    return "Super Rod";
                case SlotType2::RockSmash:
                    return "Rock Smash";
                case SlotType2::Headbutt:
                    return "Headbutt";
                case SlotType2::HeadbuttSpecial:
                    return "HeadbuttSpecial";
                case SlotType2::BugContest:
                    return "BugContest";
                }
                return "";
            }
        }

        const EncounterSlot2 &EncounterArea2::slotAt(uint16_t slotIndex) const noexcept
        {
            return areaSlots[slotIndex];
        }

        std::string EncounterSlot2::name() const
        {
            return std::string("Wild Encounter (") + getVersionSourceName(version()) + ")";
        }

        std::string EncounterSlot2::longName() const
        {
            return name() + " " + getSlotTypeName(type());
        }

        RNG::AbilityPermission EncounterSlot2::ability() const noexcept
        {
            return TransporterLogic::isHiddenDisallowedVC2(slotSpeciesId) ? RNG::AbilityPermission::OnlyFirst : RNG::AbilityPermission::OnlyHidden;
        }

        // PKHeX EncounterSlot2.IsTreeAvailable
        bool EncounterSlot2::isTreeAvailable(uint16_t trainerId) const noexcept
        {
            const uint8_t treeLocation = static_cast<uint8_t>(location());
            const auto found = std::lower_bound(TREE_LOCATIONS.begin(), TREE_LOCATIONS.end(), treeLocation);
            if (found == TREE_LOCATIONS.end() || *found != treeLocation)
            {
                return false;
            }
            const uint32_t permissions = TREE_PERMISSIONS[static_cast<size_t>(found - TREE_LOCATIONS.begin())];
            const int pivot = trainerId % 10;
            if (type() == SlotType2::Headbutt)
            {
                return (permissions & (1u << pivot)) != 0;
            }
            return (permissions & (1u << (pivot + 12))) != 0;
        }

        // PKHeX EncounterSlot2.IsMatchExact
        bool EncounterSlot2::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            if (evolution.formId != slotFormId)
            {
                if (slotSpeciesId != SPECIES_UNOWN || evolution.formId >= UNOWN_LETTER_COUNT)
                {
                    return false;
                }
            }
            if (GameBoyEntity::getCaughtData(pokemon) == 0)
            {
                return slotLevelMin <= evolution.levelMax;
            }
            if (!isLevelWithinRange(pokemon.metLevel()))
            {
                return false;
            }
            if (!isEncounterTimeAllowed(parent->time(), GameBoyEntity::getMetTimeOfDay(pokemon)))
            {
                return false;
            }
            return true;
        }

        // PKHeX EncounterSlot2.GetMatchRating
        EncounterMatchRating EncounterSlot2::getMatchRating(const Pokemon::Pokemon &pokemon) const
        {
            if (isHeadbutt() && !isTreeAvailable(pokemon.tid16()))
            {
                return EncounterMatchRating::DeferredErrors;
            }
            return EncounterMatchRating::Match;
        }
    }
}
