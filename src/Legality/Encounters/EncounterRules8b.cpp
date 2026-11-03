#include "Legality/Encounters/EncounterRules8b.h"

#include "Enums/GameVersion.h"
#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterLocations8.h"
#include "Legality/Encounters/EncounterRules9.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        const char *getSlotTypeName8b(SlotType8b type) noexcept
        {
            switch (type)
            {
            case SlotType8b::Any:
                return "Any";
            case SlotType8b::Grass:
                return "Grass";
            case SlotType8b::Surf:
                return "Surf";
            case SlotType8b::OldRod:
                return "Old Rod";
            case SlotType8b::GoodRod:
                return "Good Rod";
            case SlotType8b::SuperRod:
                return "Super Rod";
            case SlotType8b::RockSmash:
                return "Rock Smash";
            case SlotType8b::Headbutt:
                return "Headbutt";
            case SlotType8b::HoneyTree:
                return "HoneyTree";
            }
            return "";
        }

        bool isValidMetBrilliantDiamondShiningPearl(uint16_t location, uint8_t version) noexcept
        {
            return (location == LOCATION_HOME_SHSP && version == static_cast<uint8_t>(Enums::GameVersion::SH)) || (location == LOCATION_HOME_SWBD && version == static_cast<uint8_t>(Enums::GameVersion::SW));
        }

        // PKHeX LocationsHOME.IsLocationSWSHEgg
        bool isLocationSwordShieldEgg(uint8_t version, uint16_t metLocation, uint16_t eggLocation, uint16_t original) noexcept
        {
            if (original > LOCATION_HOME_SWLA && eggLocation == LOCATION_HOME_SWSH_EGG)
            {
                return true;
            }
            // An egg location above 60000 can be reset to a link trade (30001) and then altered differently.
            const uint16_t expected = getMetSwordShield(original, version);
            return expected == metLocation && expected == eggLocation;
        }

        bool isMatchLocation8b(const Pokemon::Pokemon &pokemon, bool isExactMatch, uint16_t templateLocation)
        {
            const Enums::GameVersion currentContextGroup = getEntityContextGroup(pokemon);
            const LocationRemapState metState = getRemapState(Enums::GameVersion::BDSP, currentContextGroup);
            if (metState == LocationRemapState::Original)
            {
                return isExactMatch;
            }
            const uint16_t metLocation = pokemon.metLocation();
            const uint8_t version = getOriginVersion(pokemon);
            bool isRemappedMatch = false;
            if (currentContextGroup == Enums::GameVersion::SWSH)
            {
                isRemappedMatch = isValidMetBrilliantDiamondShiningPearl(metLocation, version);
            }
            else
            {
                isRemappedMatch = getMetSwordShield(templateLocation, version) == metLocation;
            }
            if (metState == LocationRemapState::Remapped)
            {
                return isRemappedMatch;
            }
            return isExactMatch || isRemappedMatch;
        }

        bool isMatchEggLocation8b(const Pokemon::Pokemon &pokemon, bool isExactMatch)
        {
            const LocationRemapState metState = getRemapState(Enums::GameVersion::BDSP, getEntityContextGroup(pokemon));
            const bool isRemappedMatch = pokemon.eggLocation() == 0;
            if (metState == LocationRemapState::Original)
            {
                return isExactMatch;
            }
            if (metState == LocationRemapState::Remapped)
            {
                return isRemappedMatch;
            }
            return isExactMatch || isRemappedMatch;
        }
    }
}
