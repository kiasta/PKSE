/**
 * A Gen 1 wild encounter area: one map's slots for one encounter method (Red, Green, Blue, Yellow).
 * Ported from PKHeX PKHeX.Core/Legality/Encounters/Templates/Gen1/EncounterArea1.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERAREA1_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERAREA1_H

#include <cstdint>

#include "Enums/GameVersion.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterSlot1;

        /// PKHeX SlotType1.
        enum class SlotType1 : uint8_t
        {
            Grass = 0,
            Surf = 1,
            OldRod = 2,
            GoodRod = 3,
            SuperRod = 4,
        };

        class EncounterArea1
        {
        public:
            constexpr EncounterArea1(Enums::GameVersion version, SlotType1 type, uint8_t location, uint8_t rate, const EncounterSlot1 *slots, uint16_t slotCount) noexcept : areaVersion(version), areaType(type), areaLocation(location), areaRate(rate), areaSlots(slots), areaSlotCount(slotCount) {}

            constexpr Enums::GameVersion version() const noexcept { return areaVersion; }
            constexpr SlotType1 type() const noexcept { return areaType; }
            constexpr uint8_t location() const noexcept { return areaLocation; }
            constexpr uint8_t rate() const noexcept { return areaRate; }
            constexpr uint16_t slotCount() const noexcept { return areaSlotCount; }
            const EncounterSlot1 &slotAt(uint16_t slotIndex) const noexcept;

        private:
            Enums::GameVersion areaVersion;
            SlotType1 areaType;
            uint8_t areaLocation;
            uint8_t areaRate;
            const EncounterSlot1 *areaSlots;
            uint16_t areaSlotCount;
        };
    }
}

#endif
