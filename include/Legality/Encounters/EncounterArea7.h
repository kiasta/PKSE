/**
 * A Sun/Moon or Ultra Sun/Ultra Moon encounter area: one met location, one slot type and its slots.
 * Ported from PKHeX PKHeX.Core/Legality/Encounters/Templates/Gen7/EncounterArea7.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERAREA7_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERAREA7_H

#include <cstdint>

#include "Enums/GameVersion.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterSlot7;

        /// PKHeX SlotType7.
        enum class SlotType7 : uint8_t
        {
            Standard = 0,
            SOS = 1,
        };

        class EncounterArea7
        {
        public:
            constexpr EncounterArea7(Enums::GameVersion version, SlotType7 type, uint16_t location, const EncounterSlot7 *slots, uint16_t slotCount) noexcept : areaVersion(version), areaType(type), areaLocation(location), areaSlots(slots), areaSlotCount(slotCount) {}

            constexpr Enums::GameVersion version() const noexcept { return areaVersion; }
            constexpr SlotType7 type() const noexcept { return areaType; }
            constexpr uint16_t location() const noexcept { return areaLocation; }
            constexpr uint16_t slotCount() const noexcept { return areaSlotCount; }
            const EncounterSlot7 &slotAt(uint16_t slotIndex) const noexcept;

            bool isMatchLocation(uint16_t location) const noexcept { return location == areaLocation; }

        private:
            Enums::GameVersion areaVersion;
            SlotType7 areaType;
            uint16_t areaLocation;
            const EncounterSlot7 *areaSlots;
            uint16_t areaSlotCount;
        };
    }
}

#endif
