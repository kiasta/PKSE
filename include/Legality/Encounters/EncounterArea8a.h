/**
 * A Legends: Arceus wild encounter area: the slots of one spawner group, reachable from one or more
 * met locations. Ported from PKHeX PKHeX.Core/Legality/Encounters/Templates/Gen8a/EncounterArea8a.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERAREA8A_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERAREA8A_H

#include <cstdint>

#include "Legality/Encounters/EncounterRules8a.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterSlot8a;

        /// One slot of an area as the resource stores it; slotAt() builds the template on demand.
        struct EncounterSlotRecord8a
        {
            uint16_t speciesId;
            uint8_t formId;
            /// 0 never an Alpha, 1 sometimes, 2 always.
            uint8_t alphaType;
            uint8_t levelMin;
            uint8_t levelMax;
            /// PKHeX Gender: 0 male, 1 female, 2 random.
            uint8_t gender;
            uint8_t flawlessIVCount;
        };

        class EncounterArea8a
        {
        public:
            constexpr EncounterArea8a(const uint8_t *locations, uint8_t locationCount, SlotType8a type, const EncounterSlotRecord8a *slots, uint16_t slotCount) noexcept : areaLocations(locations), areaLocationCount(locationCount), areaType(type), areaSlots(slots), areaSlotCount(slotCount) {}

            /// PKHeX Location: the first of the area's met locations.
            constexpr uint16_t location() const noexcept { return areaLocations[0]; }
            constexpr SlotType8a type() const noexcept { return areaType; }
            constexpr uint16_t slotCount() const noexcept { return areaSlotCount; }
            /// The slot at `slotIndex`, built from its record; it points back at this area.
            EncounterSlot8a slotAt(uint16_t slotIndex) const noexcept;
            /// PKHeX IsMatchLocation. The resource stores locations as bytes, and PKHeX compares the
            /// met location truncated to a byte.
            bool isMatchLocation(uint16_t location) const noexcept;

        private:
            const uint8_t *areaLocations;
            uint8_t areaLocationCount;
            SlotType8a areaType;
            const EncounterSlotRecord8a *areaSlots;
            uint16_t areaSlotCount;
        };
    }
}

#endif
