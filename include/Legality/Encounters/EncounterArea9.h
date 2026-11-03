/**
 * A Scarlet/Violet wild encounter area: one map location's overworld slots. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen9/EncounterArea9.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERAREA9_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERAREA9_H

#include <cstdint>

#include "Enums/GameVersion.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterSlot9;

        class EncounterArea9
        {
        public:
            constexpr EncounterArea9(Enums::GameVersion version, uint8_t location, uint8_t crossFrom, const EncounterSlot9 *slots, uint16_t slotCount) noexcept : areaVersion(version), areaLocation(location), areaCrossFrom(crossFrom), areaSlots(slots), areaSlotCount(slotCount) {}

            constexpr Enums::GameVersion version() const noexcept { return areaVersion; }
            constexpr uint8_t location() const noexcept { return areaLocation; }
            /// PKHeX ActualLocation: the location a slot reports, which a cross-over area takes from its source.
            constexpr uint8_t actualLocation() const noexcept { return areaCrossFrom == 0 ? areaLocation : areaCrossFrom; }
            constexpr uint16_t slotCount() const noexcept { return areaSlotCount; }
            const EncounterSlot9 &slotAt(uint16_t slotIndex) const noexcept;

            bool isMatchLocation(uint16_t location) const noexcept { return location == areaLocation; }

        private:
            Enums::GameVersion areaVersion;
            uint8_t areaLocation;
            uint8_t areaCrossFrom;
            const EncounterSlot9 *areaSlots;
            uint16_t areaSlotCount;
        };
    }
}

#endif
