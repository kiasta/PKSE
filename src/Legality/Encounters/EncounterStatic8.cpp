#include "Legality/Encounters/EncounterStatic8.h"

#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterLocations8.h"
#include "Legality/Encounters/EncounterTrade3.h"
#include "Legality/RNG/Overworld8RNG.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            constexpr uint16_t SPECIES_SHEDINJA = 292;
            constexpr uint8_t HIDDEN_ABILITY_NUMBER = 4;
        }

        bool EncounterStatic8::isOverworldCorrelationCorrect(const Pokemon::Pokemon &pokemon) const
        {
            const RNG::Shiny shiny = properties.shiny == RNG::Shiny::Random ? RNG::Shiny::FixedValue : properties.shiny;
            return RNG::Overworld8RNG::validateOverworldEncounter(pokemon, shiny, properties.flawlessIVCount);
        }

        // PKHeX EncounterStatic8.IsMatchExact
        bool EncounterStatic8::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            if (!isMatchLevel(pokemon))
            {
                return false;
            }
            if (!isMatchLocation(pokemon))
            {
                return false;
            }
            if (!isMatchEggLocation(pokemon, 0))
            {
                return false;
            }
            if (isFormatSwordShield(pokemon) && getDynamaxLevel(pokemon) < properties.dynamaxLevel)
            {
                return false;
            }
            if (pokemon.metLevel() < BOOST_LEVEL8 && properties.weather == WEATHER8_HEAVY_FOG && isBoostedArea60Fog(properties.location))
            {
                return false;
            }
            if (properties.gender != GENDER_RANDOM && pokemon.gender() != properties.gender)
            {
                return false;
            }
            if (properties.formId != evolution.formId && !isFormChangeable(properties.speciesId, properties.formId, pokemon.form(), Enums::GameVersion::SWSH, getEntityContextGroup(pokemon)))
            {
                return false;
            }
            if (properties.individualValues.isSpecified && !isFixedIVSequenceValidSkipRandom(properties.individualValues, pokemon))
            {
                return false;
            }
            if (properties.flawlessIVCount != 0 && getFlawlessIVCount(pokemon) < properties.flawlessIVCount)
            {
                return false;
            }
            return true;
        }

        // PKHeX EncounterStatic8.IsMatchLocation and Crossover8.IsMatchLocation
        bool EncounterStatic8::isMatchLocation(const Pokemon::Pokemon &pokemon) const noexcept
        {
            const uint16_t metLocation = pokemon.metLocation();
            if (metLocation == properties.location)
            {
                return true;
            }
            if (metLocation > UINT8_MAX || metLocation == 0 || properties.crossover[0] == 0)
            {
                return false;
            }
            for (const uint8_t crossoverLocation : properties.crossover)
            {
                if (crossoverLocation == metLocation)
                {
                    return true;
                }
            }
            return false;
        }

        // PKHeX EncounterStatic8.IsMatchLevel
        bool EncounterStatic8::isMatchLevel(const Pokemon::Pokemon &pokemon) const noexcept
        {
            const uint8_t metLevel = pokemon.metLevel();
            if (metLevel == properties.level)
            {
                return true;
            }
            if (properties.level < BOOST_LEVEL8 && isBoostedArea60(properties.location))
            {
                return metLevel == BOOST_LEVEL8;
            }
            return false;
        }

        // PKHeX EncounterStatic8.GetMatchRating
        EncounterMatchRating EncounterStatic8::getMatchRating(const Pokemon::Pokemon &pokemon) const
        {
            if (pokemon.abilityNumber() == HIDDEN_ABILITY_NUMBER && isPartialMatchHidden(pokemon.speciesID(), properties.speciesId))
            {
                return EncounterMatchRating::PartialMatch;
            }
            const bool isCorrelated = isOverworldCorrelationCorrect(pokemon);
            if ((getRequirement() == OverworldCorrelation8Requirement::MustHave) != isCorrelated)
            {
                return EncounterMatchRating::DeferredErrors;
            }
            // Only wild slots can carry these marks; defer so a colliding slot can win.
            const RibbonMarks8 marks = readRibbonMarks8(pokemon);
            if (pokemon.speciesID() == SPECIES_SHEDINJA)
            {
                // Evolving into Shedinja drops the mark but keeps the affixed ribbon value.
                if (marks.isPresent && marks.curry)
                {
                    return EncounterMatchRating::DeferredErrors;
                }
                if (isFormatSwordShield(pokemon) && marks.isAffixedCurry)
                {
                    return EncounterMatchRating::Deferred;
                }
                return EncounterMatchRating::Match;
            }
            if (marks.isPresent && (marks.curry || marks.fishing || !isMarkCompatible(properties.weather, marks)))
            {
                return EncounterMatchRating::DeferredErrors;
            }
            return EncounterMatchRating::Match;
        }
    }
}
