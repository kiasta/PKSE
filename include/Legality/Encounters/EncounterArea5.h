/**
 * A Gen 5 wild encounter area: one map's slots for one encounter method (Black, White, Black 2,
 * White 2). Ported from PKHeX PKHeX.Core/Legality/Encounters/Templates/Gen5/EncounterArea5.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERAREA5_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERAREA5_H

#include <cstdint>

#include "Enums/GameVersion.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterSlot5;

        /// PKHeX SlotType5.
        enum class SlotType5 : uint8_t
        {
            Standard = 0,
            Grass = 1,
            Surf = 2,
            SuperRod = 3,
            Swarm = 4,
            HiddenGrotto = 5,
        };

        class EncounterArea5
        {
        public:
            constexpr EncounterArea5(Enums::GameVersion version, SlotType5 type, uint16_t location, const EncounterSlot5 *slots, uint16_t slotCount) noexcept : areaVersion(version), areaType(type), areaLocation(location), areaSlots(slots), areaSlotCount(slotCount) {}

            constexpr Enums::GameVersion version() const noexcept { return areaVersion; }
            constexpr SlotType5 type() const noexcept { return areaType; }
            constexpr uint16_t location() const noexcept { return areaLocation; }
            constexpr uint16_t slotCount() const noexcept { return areaSlotCount; }
            const EncounterSlot5 &slotAt(uint16_t slotIndex) const noexcept;

            bool isMatchLocation(uint16_t location) const noexcept { return location == areaLocation; }

        private:
            Enums::GameVersion areaVersion;
            SlotType5 areaType;
            uint16_t areaLocation;
            const EncounterSlot5 *areaSlots;
            uint16_t areaSlotCount;
        };
    }
}

#endif
