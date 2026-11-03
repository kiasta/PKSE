/**
 * A Gen 4 wild encounter area: one map's slots for one encounter method. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen4/EncounterArea4.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERAREA4_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERAREA4_H

#include <array>
#include <cstdint>

#include "Enums/GameVersion.h"
#include "Legality/RNG/EncounterSlotRNG.h"

namespace Pokemon
{
    class Pokemon;
}

namespace Legality
{
    namespace Encounters
    {
        class EncounterSlot4;

        /// PKHeX HoneyTreeUtil.HoneyTreeCount: the Sinnoh honey trees, in Diamond/Pearl/Platinum and
        /// Brilliant Diamond/Shining Pearl alike.
        constexpr uint8_t HONEY_TREE_COUNT = 21;
        /// PKHeX HoneyTreeUtil.CalculateMunchlaxTrees: the honey tree indexes the trainer id makes rare.
        std::array<uint8_t, 4> calculateMunchlaxTrees(uint32_t ID32) noexcept;

        class EncounterArea4
        {
        public:
            constexpr EncounterArea4(Enums::GameVersion version, RNG::SlotType4 type, uint8_t rate, uint8_t location, uint32_t groundTile, const EncounterSlot4 *slots, uint16_t slotCount) noexcept : areaVersion(version), areaType(type), areaRate(rate), areaLocation(location), areaGroundTile(groundTile), areaSlots(slots), areaSlotCount(slotCount) {}

            constexpr Enums::GameVersion version() const noexcept { return areaVersion; }
            constexpr RNG::SlotType4 type() const noexcept { return areaType; }
            constexpr uint8_t rate() const noexcept { return areaRate; }
            constexpr uint8_t location() const noexcept { return areaLocation; }
            /// GROUND_TILE_* bits.
            constexpr uint32_t groundTile() const noexcept { return areaGroundTile; }
            constexpr uint16_t slotCount() const noexcept { return areaSlotCount; }
            const EncounterSlot4 &slotAt(uint16_t slotIndex) const noexcept;

            bool isMatchLocation(uint16_t location) const noexcept { return location == areaLocation; }
            /// PKHeX GetPressureMax.
            uint8_t getPressureMax(uint16_t speciesId, uint8_t levelMax) const noexcept;
            /// PKHeX IsMunchlaxTree: the area is one of the four honey trees the trainer id makes rare.
            bool isMunchlaxTree(uint32_t ID32) const noexcept;
            /// PKHeX IsCoronetFeebasArea: the rate byte is a sentinel, only on fishing areas.
            bool isCoronetFeebasArea() const noexcept { return areaRate == 0xFF; }

            /// PKHeX IsUnownFormValid.
            static bool isUnownFormValid(const Pokemon::Pokemon &pokemon, uint8_t formId, bool isRuinsOfAlph);

        private:
            Enums::GameVersion areaVersion;
            RNG::SlotType4 areaType;
            uint8_t areaRate;
            uint8_t areaLocation;
            uint32_t areaGroundTile;
            const EncounterSlot4 *areaSlots;
            uint16_t areaSlotCount;
        };
    }
}

#endif
