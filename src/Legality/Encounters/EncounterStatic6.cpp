#include "Legality/Encounters/EncounterStatic6.h"

#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterLocations.h"
#include "Legality/Encounters/EncounterRules8.h"
#include "Legality/Encounters/EncounterTrade3.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            constexpr uint8_t HIDDEN_ABILITY_NUMBER = 4;
            constexpr uint16_t SPECIES_PIKACHU = 25;
        }

        // PKHeX EncounterStatic6.GetMatchRating and IsMatchPartial
        EncounterMatchRating EncounterStatic6::getMatchRating(const Pokemon::Pokemon &pokemon) const
        {
            if (pokemon.abilityNumber() == HIDDEN_ABILITY_NUMBER && isPartialMatchHidden(pokemon.speciesID(), properties.speciesId))
            {
                return EncounterMatchRating::PartialMatch;
            }
            return EncounterMatchRating::Match;
        }

        // PKHeX EncounterStatic6.IsMatchExact
        bool EncounterStatic6::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
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
            if (properties.formId != evolution.formId && !isFormChangeable(properties.speciesId, properties.formId, pokemon.form(), Enums::GameVersion::XY, getEntityContextGroup(pokemon)))
            {
                return false;
            }
            if (ContestStatsReadOnly::isContestBelow(pokemon, properties.contestStats))
            {
                return false;
            }
            if (properties.individualValues.isSpecified && !isFixedIVSequenceValidSkipRandom(properties.individualValues, pokemon))
            {
                return false;
            }
            if (properties.nature < NATURE_RANDOM && pokemon.nature() != properties.nature)
            {
                return false;
            }
            if (properties.flawlessIVCount != 0 && getFlawlessIVCount(pokemon) < properties.flawlessIVCount)
            {
                return false;
            }
            return true;
        }

        bool EncounterStatic6::isMatchLocation(const Pokemon::Pokemon &pokemon) const noexcept
        {
            const uint16_t met = pokemon.metLocation();
            if (met == properties.location)
            {
                return true;
            }
            if (isEgg())
            {
                return !pokemon.isEgg() || met == LOCATION_LINK_TRADE6;
            }
            // The Cosplay Pikachu is handed out at several other places and can change form after.
            if (properties.speciesId == SPECIES_PIKACHU)
            {
                return met == 180 || met == 186 || met == 194;
            }
            return false;
        }

        bool EncounterStatic6::isMatchEggLocationInternal(const Pokemon::Pokemon &pokemon) const noexcept
        {
            if (!isEgg())
            {
                return isMatchEggLocation(pokemon, properties.eggLocation);
            }
            const uint16_t eggLocation = pokemon.eggLocation();
            if (!pokemon.isEgg())
            {
                return eggLocation == properties.eggLocation || eggLocation == LOCATION_LINK_TRADE6;
            }
            if (eggLocation != properties.eggLocation)
            {
                return false;
            }
            const uint16_t met = pokemon.metLocation();
            return met == 0 || met == LOCATION_LINK_TRADE6;
        }
    }
}
