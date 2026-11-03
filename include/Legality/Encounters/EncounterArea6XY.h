/**
 * An X/Y encounter area: one met location, one slot type and its slots. The Friend Safari is one
 * more area that both versions share. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen6/EncounterArea6XY.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERAREA6XY_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERAREA6XY_H

#include <cstdint>

#include "Enums/GameVersion.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterSlot6XY;

        /// PKHeX SlotType6 (shared by X/Y and Omega Ruby/Alpha Sapphire).
        enum class SlotType6 : uint8_t
        {
            Standard = 0,
            Grass = 1,
            Surf = 2,
            Old_Rod = 3,
            Good_Rod = 4,
            Super_Rod = 5,
            Rock_Smash = 6,
            Horde = 7,
            FriendSafari = 8,
        };

        /// PKHeX's SlotType6 name with '_' shown as a space, as its LongName prints it.
        const char *getSlotType6Name(SlotType6 type) noexcept;

        class EncounterArea6XY
        {
        public:
            constexpr EncounterArea6XY(Enums::GameVersion version, SlotType6 type, uint16_t location, const EncounterSlot6XY *slots, uint16_t slotCount) noexcept : areaVersion(version), areaType(type), areaLocation(location), areaSlots(slots), areaSlotCount(slotCount) {}

            constexpr Enums::GameVersion version() const noexcept { return areaVersion; }
            constexpr SlotType6 type() const noexcept { return areaType; }
            constexpr uint16_t location() const noexcept { return areaLocation; }
            constexpr uint16_t slotCount() const noexcept { return areaSlotCount; }
            const EncounterSlot6XY &slotAt(uint16_t slotIndex) const noexcept;

            bool isMatchLocation(uint16_t location) const noexcept { return location == areaLocation; }

        private:
            Enums::GameVersion areaVersion;
            SlotType6 areaType;
            uint16_t areaLocation;
            const EncounterSlot6XY *areaSlots;
            uint16_t areaSlotCount;
        };
    }
}

#endif
