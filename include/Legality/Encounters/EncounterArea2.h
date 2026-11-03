/**
 * A Gen 2 wild encounter area: one map's slots for one encounter method and time of day (Gold,
 * Silver, Crystal). Ported from PKHeX PKHeX.Core/Legality/Encounters/Templates/Gen2/EncounterArea2.cs
 * and EncounterTime.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERAREA2_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERAREA2_H

#include <cstdint>

#include "Enums/GameVersion.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterSlot2;

        /// PKHeX SlotType2.
        enum class SlotType2 : uint8_t
        {
            Grass = 0,
            Surf = 1,
            OldRod = 2,
            GoodRod = 3,
            SuperRod = 4,
            RockSmash = 5,
            Headbutt = 6,
            HeadbuttSpecial = 7,
            BugContest = 8,
        };

        /// PKHeX EncounterTime flags: Morning, Day and Night bits; 0 is any time.
        enum EncounterTime : uint8_t
        {
            ENCOUNTER_TIME_ANY = 0,
            ENCOUNTER_TIME_MORNING = 1 << 1,
            ENCOUNTER_TIME_DAY = 1 << 2,
            ENCOUNTER_TIME_NIGHT = 1 << 3,
        };

        /// PKHeX EncounterTimeExtension.Contains: `timeOfDay` (1-3, Crystal's met time) is allowed.
        constexpr bool isEncounterTimeAllowed(uint8_t time, uint8_t timeOfDay) noexcept
        {
            return time == ENCOUNTER_TIME_ANY || (timeOfDay < 8 && (time & (1u << timeOfDay)) != 0);
        }

        class EncounterArea2
        {
        public:
            constexpr EncounterArea2(Enums::GameVersion version, SlotType2 type, uint8_t time, uint8_t location, uint8_t rate, const EncounterSlot2 *slots, uint16_t slotCount) noexcept : areaVersion(version), areaType(type), areaTime(time), areaLocation(location), areaRate(rate), areaSlots(slots), areaSlotCount(slotCount) {}

            constexpr Enums::GameVersion version() const noexcept { return areaVersion; }
            constexpr SlotType2 type() const noexcept { return areaType; }
            constexpr uint8_t time() const noexcept { return areaTime; }
            constexpr uint8_t location() const noexcept { return areaLocation; }
            constexpr uint8_t rate() const noexcept { return areaRate; }
            constexpr uint16_t slotCount() const noexcept { return areaSlotCount; }
            const EncounterSlot2 &slotAt(uint16_t slotIndex) const noexcept;

            bool isMatchLocation(uint16_t location) const noexcept { return location == areaLocation; }

        private:
            Enums::GameVersion areaVersion;
            SlotType2 areaType;
            uint8_t areaTime;
            uint8_t areaLocation;
            uint8_t areaRate;
            const EncounterSlot2 *areaSlots;
            uint16_t areaSlotCount;
        };
    }
}

#endif
