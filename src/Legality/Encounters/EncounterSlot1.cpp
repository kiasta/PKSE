#include "Legality/Encounters/EncounterSlot1.h"

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
            /// PKHeX SlotType1.ToString() with '_' replaced by a space.
            const char *getSlotTypeName(SlotType1 type) noexcept
            {
                switch (type)
                {
                case SlotType1::Grass:
                    return "Grass";
                case SlotType1::Surf:
                    return "Surf";
                case SlotType1::OldRod:
                    return "Old Rod";
                case SlotType1::GoodRod:
                    return "Good Rod";
                case SlotType1::SuperRod:
                    return "Super Rod";
                }
                return "";
            }
        }

        const EncounterSlot1 &EncounterArea1::slotAt(uint16_t slotIndex) const noexcept
        {
            return areaSlots[slotIndex];
        }

        std::string EncounterSlot1::name() const
        {
            return std::string("Wild Encounter (") + getVersionSourceName(version()) + ")";
        }

        std::string EncounterSlot1::longName() const
        {
            return name() + " " + getSlotTypeName(type());
        }

        RNG::AbilityPermission EncounterSlot1::ability() const noexcept
        {
            return TransporterLogic::isHiddenDisallowedVC1(slotSpeciesId) ? RNG::AbilityPermission::OnlyFirst : RNG::AbilityPermission::OnlyHidden;
        }

        // PKHeX EncounterSlot1.IsMatchExact
        bool EncounterSlot1::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            if (slotLevelMin > evolution.levelMax)
            {
                return false;
            }
            if (pokemon.getGameGroup() != Enums::GameVersion::RBY)
            {
                return true;
            }
            const uint8_t rate = GameBoyEntity::getCatchRate(pokemon);
            const uint8_t expected = GBRestrictions::getCatchRate1(version(), slotSpeciesId);
            if (expected != rate && !(ALLOW_GEN1_TRADEBACK && GBRestrictions::isCatchRateHeldItem(rate)))
            {
                return false;
            }
            return true;
        }
    }
}
