/**
 * The met and egg locations the Gen 6 and Gen 7 encounter templates name (X/Y, Omega Ruby/Alpha
 * Sapphire, Sun/Moon, Ultra Sun/Ultra Moon, Let's Go). Ported from PKHeX
 * PKHeX.Core/Game/Locations/Locations.cs (the values are the games' own location ids).
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERLOCATIONS67_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERLOCATIONS67_H

#include <cstdint>

namespace Legality
{
    namespace Encounters
    {
        constexpr uint16_t LOCATION_HATCH6XY = 38;
        constexpr uint16_t LOCATION_HATCH6AO = 318;
        constexpr uint16_t LOCATION_HATCH7 = 78;
        /// Poke Transporter's met location for a Virtual Console Red/Blue/Yellow and Gold/Silver/Crystal Pokemon.
        constexpr uint16_t LOCATION_TRANSFER1 = 30013;
        constexpr uint16_t LOCATION_TRANSFER2 = 30017;
        /// A Gen 6 Link Gift (a gift received in a link with another game).
        constexpr uint16_t LOCATION_LINK_GIFT6 = 30011;
        constexpr uint16_t LOCATION_PELAGO7 = 30016;
        /// Pokemon GO's met location in Let's Go.
        constexpr uint16_t LOCATION_GO7 = 50;
    }
}

#endif
