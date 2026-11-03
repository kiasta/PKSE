/**
 * The met and egg locations the encounter engine names. Ported from PKHeX
 * PKHeX.Core/Game/Locations/Locations.cs (the values are the games' own location ids).
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERLOCATIONS_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERLOCATIONS_H

#include <cstdint>

namespace Legality
{
    namespace Encounters
    {
        constexpr uint16_t LOCATION_LINK_TRADE4 = 2002;
        constexpr uint16_t LOCATION_LINK_TRADE5 = 30003;
        constexpr uint16_t LOCATION_LINK_TRADE6 = 30002;
        constexpr uint16_t LOCATION_DAYCARE4 = 2000;
        constexpr uint16_t LOCATION_DAYCARE5 = 60002;
        constexpr uint16_t LOCATION_LINK_TRADE2_NPC = 126;
        constexpr uint16_t LOCATION_LINK_TRADE3_NPC = 254;
        constexpr uint16_t LOCATION_LINK_TRADE4_NPC = 2001;
        constexpr uint16_t LOCATION_LINK_TRADE5_NPC = 30002;
        constexpr uint16_t LOCATION_LINK_TRADE6_NPC = 30001;
        constexpr uint16_t LOCATION_BREEDER5 = 60003;
        constexpr uint16_t LOCATION_BREEDER6 = 60004;
        constexpr uint16_t LOCATION_FARAWAY4 = 3002;
        constexpr uint16_t LOCATION_HATCH_RSE = 32;
        constexpr uint16_t LOCATION_HATCH_FRLG = 146;
        constexpr uint16_t LOCATION_TRANSFER3 = 0x37;
        constexpr uint16_t LOCATION_TRANSFER4 = 30001;
        /// The Gen 3 met location of a gift egg (PKHeX EncounterFinder.WasGiftEgg).
        constexpr uint8_t LOCATION_GIFT_EGG3 = 253;
        constexpr uint16_t LOCATION_SAFARI_ZONE_RSE = 57;
        constexpr uint16_t LOCATION_SAFARI_ZONE_FRLG = 136;

        constexpr uint16_t LOCATION_DEFAULT8B_NONE = 65535;

        constexpr bool isEventLocation3(uint16_t location) noexcept { return location == 255; }
        constexpr bool isEventLocation4(uint16_t location) noexcept { return location >= 3000 && location <= 3076; }
        constexpr bool isEventLocation5(uint16_t location) noexcept { return location > 40000 && location < 50000; }
        constexpr bool isSafariZoneLocation3(uint8_t location) noexcept { return location == LOCATION_SAFARI_ZONE_RSE || location == LOCATION_SAFARI_ZONE_FRLG; }
        constexpr bool isSafariZoneLocation3RSE(uint8_t location) noexcept { return location == LOCATION_SAFARI_ZONE_RSE; }
    }
}

#endif
