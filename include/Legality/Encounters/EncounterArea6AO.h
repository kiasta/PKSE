/**
 * An Omega Ruby/Alpha Sapphire encounter area: one met location, one slot type and its slots.
 * Ported from PKHeX PKHeX.Core/Legality/Encounters/Templates/Gen6/EncounterArea6AO.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERAREA6AO_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERAREA6AO_H

#include <cstdint>

#include "Enums/GameVersion.h"
#include "Legality/Encounters/EncounterArea6XY.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterSlot6AO;

        class EncounterArea6AO
        {
        public:
            constexpr EncounterArea6AO(Enums::GameVersion version, SlotType6 type, uint16_t location, const EncounterSlot6AO *slots, uint16_t slotCount) noexcept : areaVersion(version), areaType(type), areaLocation(location), areaSlots(slots), areaSlotCount(slotCount) {}

            constexpr Enums::GameVersion version() const noexcept { return areaVersion; }
            constexpr SlotType6 type() const noexcept { return areaType; }
            constexpr uint16_t location() const noexcept { return areaLocation; }
            constexpr uint16_t slotCount() const noexcept { return areaSlotCount; }
            const EncounterSlot6AO &slotAt(uint16_t slotIndex) const noexcept;

            bool isMatchLocation(uint16_t location) const noexcept { return location == areaLocation; }

        private:
            Enums::GameVersion areaVersion;
            SlotType6 areaType;
            uint16_t areaLocation;
            const EncounterSlot6AO *areaSlots;
            uint16_t areaSlotCount;
        };
    }
}

#endif
