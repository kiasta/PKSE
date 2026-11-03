#include "Legality/Encounters/EncounterSlot7b.h"

#include "Legality/Encounters/EncounterEntity.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            /// PKHeX CatchComboBonus: a catch combo can raise the level by one.
            constexpr uint8_t CATCH_COMBO_BONUS = 1;
        }

        const EncounterSlot7b &EncounterArea7b::slotAt(uint16_t slotIndex) const noexcept
        {
            return areaSlots[slotIndex];
        }

        std::string EncounterSlot7b::name() const
        {
            return std::string("Wild Encounter (") + getVersionSourceName(version()) + ")";
        }

        // PKHeX EncounterSlot7b.IsMatchExact
        bool EncounterSlot7b::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            if (!isLevelWithinRange(pokemon.metLevel(), 0, CATCH_COMBO_BONUS))
            {
                return false;
            }
            if (evolution.formId != 0)
            {
                return false;
            }
            const uint16_t met = pokemon.metLocation();
            return met == parent->location() || isCrossoverAllowed(met);
        }

        // PKHeX EncounterSlot7b.IsCrossoverAllowed: the met location already matched one of the two neighbours.
        bool EncounterSlot7b::isCrossoverAllowed(uint16_t metLocation) const noexcept
        {
            const uint8_t bit = metLocation == parent->toAreaFirst() ? 0b01 : 0b10;
            return (slotCrossoverFlags & bit) != 0;
        }
    }
}
