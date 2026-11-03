#include "Legality/Encounters/EncounterStatic7.h"

#include "Legality/Encounters/EncounterBreeding.h"
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
            constexpr uint16_t SPECIES_MAROWAK = 105;
            constexpr uint16_t SPECIES_ARAQUANID = 752;
            constexpr uint16_t SPECIES_TOGEDEMARU = 777;
            constexpr uint16_t SPECIES_RIBOMBEE = 743;
            constexpr uint16_t SPECIES_MAGEARNA = 801;
        }

        bool EncounterStatic7::hasWishingRibbon() const noexcept
        {
            return properties.speciesId == SPECIES_MAGEARNA;
        }

        bool EncounterStatic7::isTotem() const noexcept
        {
            return isTotemForm(properties.speciesId, properties.formId);
        }

        bool EncounterStatic7::isTotemNoTransfer() const noexcept
        {
            const uint16_t speciesId = properties.speciesId;
            return speciesId == SPECIES_MAROWAK || speciesId == SPECIES_ARAQUANID || speciesId == SPECIES_TOGEDEMARU || speciesId == SPECIES_RIBOMBEE;
        }

        // PKHeX EncounterStatic7.GetMatchRating and IsMatchPartial
        EncounterMatchRating EncounterStatic7::getMatchRating(const Pokemon::Pokemon &pokemon) const
        {
            if (pokemon.abilityNumber() == HIDDEN_ABILITY_NUMBER && isPartialMatchHidden(pokemon.speciesID(), properties.speciesId))
            {
                return EncounterMatchRating::PartialMatch;
            }
            return EncounterMatchRating::Match;
        }

        // PKHeX EncounterStatic7.IsMatchExact
        bool EncounterStatic7::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
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
            if (!isMatchForm(pokemon, evolution))
            {
                return false;
            }
            if (properties.gender != GENDER_RANDOM && pokemon.gender() != properties.gender)
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

        bool EncounterStatic7::isMatchLocation(const Pokemon::Pokemon &pokemon) const noexcept
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
            return false;
        }

        bool EncounterStatic7::isMatchEggLocationInternal(const Pokemon::Pokemon &pokemon) const noexcept
        {
            if (!isEgg())
            {
                return isMatchEggLocation(pokemon, properties.eggLocation);
            }
            // The Nursery helpers' Eevee egg carries no relearn moves of its own.
            if (properties.eggLocation == LOCATION_DAYCARE5 && !properties.relearnMoves.hasMoves() && pokemon.relearnMove(0) != 0)
            {
                return false;
            }
            const uint16_t eggLocation = pokemon.eggLocation();
            if (!pokemon.isEgg())
            {
                return eggLocation == properties.eggLocation || eggLocation == LOCATION_LINK_TRADE6;
            }
            return eggLocation == properties.eggLocation;
        }

        bool EncounterStatic7::isMatchForm(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const noexcept
        {
            if (isRandomUnspecificForm())
            {
                return true;
            }
            if (isTotem())
            {
                // A Totem reverts to its base form when it leaves Gen 7.
                const uint8_t expectedForm = getFormatGeneration(pokemon) == 7 ? properties.formId : FormInfo::getTotemBaseForm(properties.speciesId, properties.formId);
                return expectedForm == evolution.formId;
            }
            if (properties.formId != evolution.formId && !isFormChangeable(properties.speciesId, properties.formId, pokemon.form(), Enums::GameVersion::SM, getEntityContextGroup(pokemon)))
            {
                return false;
            }
            return true;
        }
    }
}
