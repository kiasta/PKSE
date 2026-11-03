/**
 * A Gen 3 wild encounter area: one map's slots for one encounter method. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen3/EncounterArea3.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERAREA3_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERAREA3_H

#include <cstdint>

#include "Enums/GameVersion.h"
#include "Legality/RNG/EncounterSlotRNG.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterSlot3;
        class EncounterSlot3Swarm;

        class EncounterArea3
        {
        public:
            constexpr EncounterArea3(Enums::GameVersion version, RNG::SlotType3 type, uint8_t rate, uint8_t location, const EncounterSlot3 *slots, uint16_t slotCount) noexcept : areaVersion(version), areaType(type), areaRate(rate), areaLocation(location), regularSlots(slots), swarmSlots(nullptr), areaSlotCount(slotCount) {}
            constexpr EncounterArea3(Enums::GameVersion version, RNG::SlotType3 type, uint8_t rate, uint8_t location, const EncounterSlot3Swarm *slots, uint16_t slotCount) noexcept : areaVersion(version), areaType(type), areaRate(rate), areaLocation(location), regularSlots(nullptr), swarmSlots(slots), areaSlotCount(slotCount) {}

            constexpr Enums::GameVersion version() const noexcept { return areaVersion; }
            constexpr RNG::SlotType3 type() const noexcept { return areaType; }
            constexpr uint8_t rate() const noexcept { return areaRate; }
            constexpr uint8_t location() const noexcept { return areaLocation; }
            constexpr uint16_t slotCount() const noexcept { return areaSlotCount; }
            /// A swarm area's slots are EncounterSlot3Swarm; either kind is read through here.
            const EncounterSlot3 &slotAt(uint16_t slotIndex) const noexcept;

            bool isMatchLocation(uint16_t location) const noexcept { return location == areaLocation; }
            /// PKHeX GetPressureMax.
            uint8_t getPressureMax(uint16_t speciesId, uint8_t levelMax) const noexcept;

        private:
            Enums::GameVersion areaVersion;
            RNG::SlotType3 areaType;
            uint8_t areaRate;
            uint8_t areaLocation;
            const EncounterSlot3 *regularSlots;
            const EncounterSlot3Swarm *swarmSlots;
            uint16_t areaSlotCount;
        };
    }
}

#endif
