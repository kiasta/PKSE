#include "Legality/Encounters/EncounterStatic5.h"

#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterLocations5.h"
#include "Legality/Encounters/EncounterRules8.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            constexpr uint8_t HIDDEN_ABILITY_NUMBER = 4;

            /// PKHeX IsLocationAsEgg: a spin trade sets another trade location.
            bool isLocationAsEgg(uint16_t location, uint16_t expected) noexcept
            {
                return location == expected || isLinkTradeLocation5(location);
            }

            /// PKHeX IsRoamerMet: the routes the Black/White roamers can be met on.
            bool isRoamerMet(uint16_t location) noexcept
            {
                if (location >= 32)
                {
                    return false;
                }
                return (0b10111111111111111000000000000000u & (1u << location)) != 0;
            }
        }

        // PKHeX EncounterStatic5.GetMatchRating
        EncounterMatchRating EncounterStatic5::getMatchRating(const Pokemon::Pokemon &pokemon) const
        {
            if (isMatchPartial(pokemon))
            {
                return EncounterMatchRating::PartialMatch;
            }
            return EncounterMatchRating::Match;
        }

        // PKHeX EncounterStatic5.IsMatchExact
        bool EncounterStatic5::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            if (!isMatchEggLocationInternal(pokemon))
            {
                return false;
            }
            if (!isMatchLocation(pokemon))
            {
                return false;
            }
            if (pokemon.metLevel() != properties.level)
            {
                return false;
            }
            if (properties.gender != GENDER_RANDOM && pokemon.gender() != properties.gender)
            {
                return false;
            }
            if (properties.formId != evolution.formId && !isFormChangeable(properties.speciesId, properties.formId, pokemon.formID(), Enums::GameVersion::BW, getEntityContextGroup(pokemon)))
            {
                return false;
            }
            return true;
        }

        bool EncounterStatic5::isMatchPartial(const Pokemon::Pokemon &pokemon) const
        {
            const bool isHidden = pokemon.abilityNumber() == HIDDEN_ABILITY_NUMBER;
            // The Black 2/White 2 hidden-ability Jellicent collides with the wild surf slots.
            if (properties.ability == RNG::AbilityPermission::OnlyHidden && !isHidden && getFormatGeneration(pokemon) <= 7)
            {
                return true;
            }
            if (properties.eggLocation == LOCATION_DAYCARE5 && pokemon.relearnMove(0) != 0)
            {
                return true;
            }
            if (isHidden && isPartialMatchHidden(pokemon.speciesID(), properties.speciesId))
            {
                return true;
            }
            return false;
        }

        bool EncounterStatic5::isMatchLocation(const Pokemon::Pokemon &pokemon) const
        {
            const uint16_t metLocation = pokemon.metLocation();
            if (isEgg())
            {
                return !pokemon.isEgg() || isLocationAsEgg(metLocation, properties.location);
            }
            if (!properties.isRoaming)
            {
                return metLocation == properties.location;
            }
            return isRoamerMet(metLocation);
        }

        bool EncounterStatic5::isMatchEggLocationInternal(const Pokemon::Pokemon &pokemon) const
        {
            if (!isEgg())
            {
                return isMatchEggLocation(pokemon, properties.eggLocation);
            }
            const uint16_t eggLocation = pokemon.eggLocation();
            if (!pokemon.isEgg())
            {
                return isLocationAsEgg(eggLocation, properties.eggLocation);
            }
            return eggLocation == properties.eggLocation;
        }
    }
}
