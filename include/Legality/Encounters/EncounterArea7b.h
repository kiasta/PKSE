/**
 * A Let's Go encounter area: one met location, up to two neighbouring locations its Pokemon can
 * wander into, and its slots. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen7b/EncounterArea7b.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERAREA7B_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERAREA7B_H

#include <cstdint>

#include "Enums/GameVersion.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterSlot7b;

        class EncounterArea7b
        {
        public:
            constexpr EncounterArea7b(Enums::GameVersion version, uint8_t location, uint8_t toAreaFirst, uint8_t toAreaSecond, const EncounterSlot7b *slots, uint16_t slotCount) noexcept : areaVersion(version), areaLocation(location), areaToFirst(toAreaFirst), areaToSecond(toAreaSecond), areaSlots(slots), areaSlotCount(slotCount) {}

            constexpr Enums::GameVersion version() const noexcept { return areaVersion; }
            constexpr uint16_t location() const noexcept { return areaLocation; }
            /// PKHeX ToArea1 / ToArea2: the locations a Pokemon here can be caught in after crossing over.
            constexpr uint8_t toAreaFirst() const noexcept { return areaToFirst; }
            constexpr uint8_t toAreaSecond() const noexcept { return areaToSecond; }
            constexpr uint16_t slotCount() const noexcept { return areaSlotCount; }
            const EncounterSlot7b &slotAt(uint16_t slotIndex) const noexcept;

            /// PKHeX IsMatchLocation: the area's own location or either crossover location.
            bool isMatchLocation(uint16_t location) const noexcept
            {
                if (location == areaLocation)
                {
                    return true;
                }
                if (location == 0)
                {
                    return false;
                }
                return location == areaToFirst || location == areaToSecond;
            }

        private:
            Enums::GameVersion areaVersion;
            uint8_t areaLocation;
            uint8_t areaToFirst;
            uint8_t areaToSecond;
            const EncounterSlot7b *areaSlots;
            uint16_t areaSlotCount;
        };
    }
}

#endif
