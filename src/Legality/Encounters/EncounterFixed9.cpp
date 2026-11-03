#include "Legality/Encounters/EncounterFixed9.h"

#include "Legality/Encounters/EncounterRules8.h"
#include "Legality/RNG/HomeQuirks.h"
#include "Legality/RNG/Tera9RNG.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            constexpr uint16_t SPECIES_DEERLING = 585;
            constexpr uint16_t SPECIES_SAWSBUCK = 586;
            /// PKHeX EncounterFixed9.MinScaleStrongTera: a Tera-glowing spawn is always large.
            constexpr uint8_t MIN_SCALE_STRONG_TERA = 200;
        }

        // PKHeX EncounterFixed9.IsMatchExact
        bool EncounterFixed9::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            if (!isLevelWithinRange(pokemon.metLevel()))
            {
                return false;
            }
            if (properties.gender != GENDER_RANDOM && pokemon.gender() != properties.gender)
            {
                return false;
            }
            if (!isMatchEggLocation(pokemon, 0))
            {
                return false;
            }
            if (!isMatchLocation9(pokemon, isMatchLocationExact(pokemon), properties.locations[0], Enums::GameVersion::SV))
            {
                return false;
            }
            // PKHeX IsMatchForm
            if (evolution.formId != properties.formId)
            {
                const bool isSeasonal = properties.speciesId == SPECIES_DEERLING || properties.speciesId == SPECIES_SAWSBUCK;
                if (!isSeasonal || pokemon.form() > 3)
                {
                    return false;
                }
            }
            if (properties.teraType != RNG::GemType::Random && pokemon.hasTeraType() && !RNG::Tera9RNG::isMatchTeraType(properties.teraType, properties.speciesId, properties.formId, pokemon.teraTypeOriginal()))
            {
                return false;
            }
            if (properties.teraType != RNG::GemType::Default)
            {
                if (pokemon.hasScale())
                {
                    if (pokemon.scale() < MIN_SCALE_STRONG_TERA)
                    {
                        return false;
                    }
                }
                else if (RNG::HomeQuirks::hasSizeScalars(pokemon) && pokemon.heightScalar() < MIN_SCALE_STRONG_TERA)
                {
                    return false;
                }
            }
            if (properties.flawlessIVCount != 0 && getFlawlessIVCount(pokemon) < properties.flawlessIVCount)
            {
                return false;
            }
            return true;
        }

        // PKHeX EncounterFixed9.IsMatchLocationExact
        bool EncounterFixed9::isMatchLocationExact(const Pokemon::Pokemon &pokemon) const noexcept
        {
            const uint16_t metLocation = pokemon.metLocation();
            if (metLocation == properties.locations[0])
            {
                return true;
            }
            if (metLocation == 0)
            {
                return false;
            }
            return metLocation == properties.locations[1] || metLocation == properties.locations[2] || metLocation == properties.locations[3];
        }

        // PKHeX EncounterFixed9.GetMatchRating and IsMatchDeferred (its Shiny is always Random)
        EncounterMatchRating EncounterFixed9::getMatchRating(const Pokemon::Pokemon &pokemon) const
        {
            if (isPartialMatchHiddenAbility(pokemon, properties.speciesId))
            {
                return EncounterMatchRating::PartialMatch;
            }
            return getAbilityDeferral9(pokemon, properties.ability, properties.speciesId, properties.formId, Enums::GameVersion::SV);
        }
    }
}
