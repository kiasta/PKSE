/**
 * A Pokemon XD Poke Spot (Rock, Oasis, Cave) and its three slots. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen3/XD/EncounterArea3XD.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERAREA3XD_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERAREA3XD_H

#include <array>
#include <cstdint>

#include "Legality/Encounters/EncounterSlot3XD.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterArea3XD
        {
        public:
            /// PKHeX's constructor: the location, then each slot's species and maximum level (every
            /// Poke Spot slot starts at level 10).
            constexpr EncounterArea3XD(uint8_t location, uint16_t firstSpeciesId, uint8_t firstLevelMax, uint16_t secondSpeciesId, uint8_t secondLevelMax, uint16_t thirdSpeciesId, uint8_t thirdLevelMax) noexcept
                : areaLocation(location),
                  areaSlots{EncounterSlot3XD(*this, firstSpeciesId, POKE_SPOT_LEVEL_MIN, firstLevelMax, 0), EncounterSlot3XD(*this, secondSpeciesId, POKE_SPOT_LEVEL_MIN, secondLevelMax, 1), EncounterSlot3XD(*this, thirdSpeciesId, POKE_SPOT_LEVEL_MIN, thirdLevelMax, 2)}
            {
            }

            /// Areas hold their slots, which point back at them; copying would leave the slots on the old area.
            EncounterArea3XD(const EncounterArea3XD &) = delete;
            EncounterArea3XD &operator=(const EncounterArea3XD &) = delete;

            static constexpr uint8_t POKE_SPOT_LEVEL_MIN = 10;

            constexpr uint8_t location() const noexcept { return areaLocation; }
            /// PKHeX Type: the Poke Spot, numbered from location 90.
            constexpr PokeSpotArea type() const noexcept { return static_cast<PokeSpotArea>(areaLocation - 90); }
            constexpr uint16_t slotCount() const noexcept { return static_cast<uint16_t>(areaSlots.size()); }
            constexpr const EncounterSlot3XD &slotAt(uint16_t slotIndex) const noexcept { return areaSlots[slotIndex]; }
            constexpr bool isMatchLocation(uint16_t location) const noexcept { return location == areaLocation; }

        private:
            uint8_t areaLocation;
            std::array<EncounterSlot3XD, 3> areaSlots;
        };
    }
}

#endif
