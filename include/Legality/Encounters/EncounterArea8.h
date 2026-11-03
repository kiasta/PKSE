/**
 * A Sword/Shield wild encounter area: one map's symbol (overworld) or hidden slots, and where the
 * symbol encounters there can wander to. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen8/EncounterArea8.cs and SlotType8.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERAREA8_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERAREA8_H

#include <cstdint>
#include <span>

#include "Enums/GameVersion.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterSlot8;

        /// PKHeX SlotType8.
        enum class SlotType8 : uint8_t
        {
            SymbolMain,
            SymbolMain2,
            SymbolMain3,
            HiddenMain,
            HiddenMain2,
            Surfing,
            Surfing2,
            Sky,
            Sky2,
            Ground,
            Ground2,
            Sharpedo,
            OnlyFishing,
            Inaccessible,
        };

        /// PKHeX SlotType8 member name.
        const char *getSlotTypeName8(SlotType8 type) noexcept;
        /// PKHeX SlotType8Extensions.CanCrossover / CanEncounterViaFishing / CanEncounterViaCurry.
        bool canCrossover8(SlotType8 type) noexcept;
        bool canEncounterViaFishing8(SlotType8 type, uint16_t weather) noexcept;
        bool canEncounterViaCurry8(SlotType8 type) noexcept;
        /// PKHeX EncounterArea8.GetWeather: the weather a location can have (WEATHER8_* flags);
        /// unlisted locations have only Normal weather.
        uint16_t getAreaWeather8(uint8_t location) noexcept;
        /// PKHeX EncounterArea8.IsWeatherBleedPossible: an adjacent location's weather reaching
        /// this location's slots of the given type.
        bool isWeatherBleedPossible8(SlotType8 type, uint16_t permit, uint8_t location) noexcept;

        class EncounterArea8
        {
        public:
            constexpr EncounterArea8(Enums::GameVersion version, uint8_t location, bool permitCrossover, const EncounterSlot8 *slots, uint16_t slotCount) noexcept : areaVersion(version), areaLocation(location), areaPermitCrossover(permitCrossover), areaSlots(slots), areaSlotCount(slotCount) {}

            constexpr Enums::GameVersion version() const noexcept { return areaVersion; }
            constexpr uint8_t location() const noexcept { return areaLocation; }
            /// PKHeX PermitCrossover: a symbol table, whose encounters walk into neighbouring areas.
            constexpr bool permitCrossover() const noexcept { return areaPermitCrossover; }
            constexpr uint16_t slotCount() const noexcept { return areaSlotCount; }
            const EncounterSlot8 &slotAt(uint16_t slotIndex) const noexcept;

            /// PKHeX IsMatchLocation.
            bool isMatchLocation(uint16_t location) const noexcept;
            /// PKHeX CanCrossoverTo.
            static bool canCrossoverTo(SlotType8 type) noexcept { return canCrossover8(type); }
            /// PKHeX GetAreasCanWanderTo.
            static std::span<const uint8_t> getAreasCanWanderTo(uint8_t location) noexcept;

        private:
            Enums::GameVersion areaVersion;
            uint8_t areaLocation;
            bool areaPermitCrossover;
            const EncounterSlot8 *areaSlots;
            uint16_t areaSlotCount;
        };
    }
}

#endif
