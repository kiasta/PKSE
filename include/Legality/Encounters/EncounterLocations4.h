/**
 * The Gen 4 met and egg locations the encounter engine names. Ported from PKHeX
 * PKHeX.Core/Game/Locations/Locations4.cs and the Gen 4 members of Locations.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERLOCATIONS4_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERLOCATIONS4_H

#include <cstdint>

#include "Enums/GameVersion.h"
#include "Legality/Encounters/EncounterLocations.h"

namespace Legality
{
    namespace Encounters
    {
        constexpr uint16_t LOCATION_POKEWALKER4 = 233;
        constexpr uint16_t LOCATION_RANGER4 = 3001;
        constexpr uint16_t LOCATION_HATCH_DPPT = 4;
        constexpr uint16_t LOCATION_HATCH_HGSS = 182;
        constexpr uint16_t LOCATION_SAFARI_ZONE_HGSS = 202;
        constexpr uint16_t LOCATION_GREAT_MARSH_DPPT = 52;

        constexpr bool isSafariBallRequired4(uint16_t location) noexcept { return location == LOCATION_GREAT_MARSH_DPPT || location == LOCATION_SAFARI_ZONE_HGSS; }
        constexpr bool isMarshLocation4(uint16_t location) noexcept { return location == LOCATION_GREAT_MARSH_DPPT; }
        constexpr bool isSafariLocationHGSS(uint16_t location) noexcept { return location == LOCATION_SAFARI_ZONE_HGSS; }
        /// PKHeX Locations.IsPtHGSSLocationEgg: an egg location Diamond and Pearl lack.
        constexpr bool isPtHGSSLocationEgg(uint16_t location) noexcept { return location > 2010 && location < 3000; }

        /// PKHeX Locations.IsEggLocationBred4 (version is the stored version id).
        constexpr bool isEggLocationBred4(uint16_t location, uint8_t version) noexcept
        {
            if (location == LOCATION_DAYCARE4 || location == LOCATION_LINK_TRADE4)
            {
                return true;
            }
            const bool isPlatinumOrLater = version == static_cast<uint8_t>(Enums::GameVersion::Pt) || version == static_cast<uint8_t>(Enums::GameVersion::HG) || version == static_cast<uint8_t>(Enums::GameVersion::SS);
            return location == LOCATION_FARAWAY4 && isPlatinumOrLater;
        }

        /// PKHeX EggHatchLegality.GetHatchLocation4.
        constexpr uint16_t getHatchLocation4(Enums::GameVersion version) noexcept
        {
            return version == Enums::GameVersion::HG || version == Enums::GameVersion::SS ? LOCATION_HATCH_HGSS : LOCATION_HATCH_DPPT;
        }
    }
}

#endif
