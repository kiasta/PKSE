/**
 * The Gen 8 met and egg locations the encounter engine names (Sword/Shield, Brilliant
 * Diamond/Shining Pearl, Legends: Arceus, and HOME's remapped values). Ported from PKHeX
 * PKHeX.Core/Game/Locations/Locations.cs, Locations8b.cs, LocationsHOME.cs and
 * PKHeX.Core/Legality/Encounters/Data/Gen8/Encounters8Nest.cs (the values are the games' own ids).
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERLOCATIONS8_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERLOCATIONS8_H

#include <cstdint>

#include "Legality/Encounters/EncounterLocations.h"

namespace Legality
{
    namespace Encounters
    {
        /// PKHeX Locations.HatchLocation8: Route 5, where a Sword/Shield egg template says it hatched.
        constexpr uint16_t LOCATION_HATCH8 = 40;
        /// PKHeX Locations.HatchLocation8b: Solaceon Town.
        constexpr uint16_t LOCATION_HATCH8B = 446;
        /// PKHeX Locations.Daycare8b.
        constexpr uint16_t LOCATION_DAYCARE8B = 60010;

        /// PKHeX Encounters8Nest.SharedNest: a raid joined through someone else's den.
        constexpr uint16_t LOCATION_SHARED_NEST8 = 162;
        /// PKHeX Encounters8Nest.Watchtower: Watchtower Ruins.
        constexpr uint16_t LOCATION_WATCHTOWER8 = 126;
        /// PKHeX Encounters8Nest.MaxLair.
        constexpr uint16_t LOCATION_MAX_LAIR8 = 244;

        /// PKHeX LocationsHOME: the Sword/Shield met location HOME writes for a Pokemon that came
        /// from another game.
        constexpr uint16_t LOCATION_HOME_SHLG = 59994;
        constexpr uint16_t LOCATION_HOME_SWFR = 59995;
        constexpr uint16_t LOCATION_HOME_SHVL = 59996;
        constexpr uint16_t LOCATION_HOME_SWSL = 59997;
        constexpr uint16_t LOCATION_HOME_SHSP = 59998;
        constexpr uint16_t LOCATION_HOME_SWBD = 59999;
        constexpr uint16_t LOCATION_HOME_SWLA = 60000;
        /// PKHeX LocationsHOME.SWSHEgg: the egg location HOME writes for a BDSP egg moved to Sword/Shield.
        constexpr uint16_t LOCATION_HOME_SWSH_EGG = 65534;
        /// PKHeX LocationsHOME.VirtualConsole3.
        constexpr uint16_t LOCATION_HOME_VIRTUAL_CONSOLE3_8 = 30007;
        /// PKHeX Locations8b.TransferPlaceholder0.
        constexpr uint16_t LOCATION_TRANSFER_PLACEHOLDER8B = 658;

        /// PKHeX Locations.IsEggLocationBred6.
        constexpr bool isEggLocationBred6(uint16_t location) noexcept { return location == 60002 || location == 30002; }
        /// PKHeX Locations.IsEggLocationBred8b.
        constexpr bool isEggLocationBred8b(uint16_t location) noexcept { return location == LOCATION_DAYCARE8B || location == 30001; }

        /// PKHeX Locations8b.GetLocationLocal: BDSP stores "none" as 65535 where every other game stores 0.
        constexpr uint16_t getLocationLocal8b(uint16_t location) noexcept
        {
            if (location == 0)
            {
                return LOCATION_DEFAULT8B_NONE;
            }
            if (location == LOCATION_TRANSFER_PLACEHOLDER8B)
            {
                return 0;
            }
            return location;
        }

        /// PKHeX EncounterArea8.IsWildArea8 / IsWildArea8Armor / IsWildArea8Crown / IsWildArea.
        constexpr bool isWildArea8(uint16_t location) noexcept { return location >= 122 && location <= 154; }
        constexpr bool isWildArea8Armor(uint16_t location) noexcept { return location >= 164 && location <= 194; }
        constexpr bool isWildArea8Crown(uint16_t location) noexcept { return location >= 204 && location <= 234 && location != 206; }
        constexpr bool isWildAreaSwordShield(uint16_t location) noexcept { return isWildArea8(location) || isWildArea8Armor(location) || isWildArea8Crown(location); }
        /// PKHeX EncounterArea8.IsBoostedArea60 / IsBoostedArea60Fog: the post-game level 60 boost.
        constexpr bool isBoostedArea60(uint16_t location) noexcept { return isWildAreaSwordShield(location); }
        constexpr bool isBoostedArea60Fog(uint16_t location) noexcept { return isWildArea8(location); }
        /// PKHeX EncounterArea8.BoostLevel.
        constexpr uint8_t BOOST_LEVEL8 = 60;
    }
}

#endif
