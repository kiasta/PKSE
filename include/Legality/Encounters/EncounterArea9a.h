/**
 * A Legends: Z-A wild encounter area: one location's overworld or Hyperspace slots. Ported from
 * PKHeX PKHeX.Core/Legality/Encounters/Templates/Gen9a/EncounterArea9a.cs and SlotType9a.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERAREA9A_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERAREA9A_H

#include <cstdint>

namespace Legality
{
    namespace Encounters
    {
        class EncounterSlot9a;

        /// PKHeX SlotType9a.
        enum class SlotType9a : uint8_t
        {
            Standard = 0,
            Hyperspace = 1,
        };

        class EncounterArea9a
        {
        public:
            constexpr EncounterArea9a(uint16_t location, SlotType9a type, const EncounterSlot9a *slots, uint16_t slotCount) noexcept : areaLocation(location), areaType(type), areaSlots(slots), areaSlotCount(slotCount) {}

            constexpr uint16_t location() const noexcept { return areaLocation; }
            constexpr SlotType9a type() const noexcept { return areaType; }
            constexpr uint16_t slotCount() const noexcept { return areaSlotCount; }
            const EncounterSlot9a &slotAt(uint16_t slotIndex) const noexcept;

            /// No cross-overs in Z-A.
            bool isMatchLocation(uint16_t location) const noexcept { return location == areaLocation; }

        private:
            uint16_t areaLocation;
            SlotType9a areaType;
            const EncounterSlot9a *areaSlots;
            uint16_t areaSlotCount;
        };
    }
}

#endif
