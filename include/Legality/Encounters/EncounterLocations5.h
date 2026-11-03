/**
 * The Gen 5 met and egg locations the encounter engine names. Ported from PKHeX
 * PKHeX.Core/Game/Locations/Locations.cs (the Gen 5 members).
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERLOCATIONS5_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERLOCATIONS5_H

#include <cstdint>

#include "Legality/Encounters/EncounterLocations.h"

namespace Legality
{
    namespace Encounters
    {
        constexpr uint16_t LOCATION_HATCH5 = 64;
        constexpr uint16_t LOCATION_ENTREE_FOREST5 = 75;
        constexpr uint16_t LOCATION_DREAM_RADAR5 = 30015;

        /// PKHeX Locations.IsEggLocationBred5.
        constexpr bool isEggLocationBred5(uint16_t location) noexcept { return location == LOCATION_DAYCARE5 || location == LOCATION_LINK_TRADE5 || location == LOCATION_LINK_TRADE5_NPC; }
        /// An egg location a traded or spin-traded Gen 5 egg is given.
        constexpr bool isLinkTradeLocation5(uint16_t location) noexcept { return location == LOCATION_LINK_TRADE5 || location == LOCATION_LINK_TRADE5_NPC; }
    }
}

#endif
