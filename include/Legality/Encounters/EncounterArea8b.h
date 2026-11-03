/**
 * A Brilliant Diamond/Shining Pearl wild encounter area: one map's slots for one encounter method,
 * overworld or Grand Underground. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen8b/EncounterArea8b.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERAREA8B_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERAREA8B_H

#include <cstdint>

#include "Enums/GameVersion.h"
#include "Legality/Encounters/EncounterRules8b.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterSlot8b;

        /// One slot of an area as the resource stores it. The Grand Underground alone has some 47,000
        /// slots, so the tables keep these 6-byte records and slotAt() builds the template on demand.
        struct EncounterSlotRecord8b
        {
            uint16_t speciesId;
            uint8_t formId;
            uint8_t levelMin;
            uint8_t levelMax;
        };

        class EncounterArea8b
        {
        public:
            constexpr EncounterArea8b(Enums::GameVersion version, uint16_t location, SlotType8b type, const EncounterSlotRecord8b *slots, uint16_t slotCount) noexcept : areaVersion(version), areaLocation(location), areaType(type), areaSlots(slots), areaSlotCount(slotCount) {}

            constexpr Enums::GameVersion version() const noexcept { return areaVersion; }
            constexpr uint16_t location() const noexcept { return areaLocation; }
            constexpr SlotType8b type() const noexcept { return areaType; }
            constexpr uint16_t slotCount() const noexcept { return areaSlotCount; }
            /// The slot at `slotIndex`, built from its record; it points back at this area.
            EncounterSlot8b slotAt(uint16_t slotIndex) const noexcept;

            /// PKHeX IsMatchLocation, with CanCrossoverTo's surf crossovers.
            bool isMatchLocation(uint16_t location) const noexcept;
            /// PKHeX IsMunchlaxTree: the area is one of the honey trees the trainer id makes rare.
            bool isMunchlaxTree(uint32_t ID32) const noexcept;

        private:
            Enums::GameVersion areaVersion;
            uint16_t areaLocation;
            SlotType8b areaType;
            const EncounterSlotRecord8b *areaSlots;
            uint16_t areaSlotCount;
        };
    }
}

#endif
