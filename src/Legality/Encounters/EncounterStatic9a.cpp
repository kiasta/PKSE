#include "Legality/Encounters/EncounterStatic9a.h"

#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterRules8.h"
#include "Legality/Encounters/EncounterTrade3.h"
#include "Legality/RNG/LumioseSolver.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            constexpr uint16_t SPECIES_MELTAN = 808;
            /// PKHeX: Meltan can also be met in Rouge Sector 1, depending on where the player stands.
            constexpr uint16_t LOCATION_ROUGE_SECTOR1 = 70;
            constexpr uint8_t SHINY_CHARM_ROLLS = 3;
            constexpr uint8_t SHINY_HYPERSPACE_ROLLS = 3;
        }

        // PKHeX EncounterStatic9a.IsMatchExact
        bool EncounterStatic9a::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            if (pokemon.metLevel() != properties.level)
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
            const uint16_t metLocation = pokemon.metLocation();
            const bool isMeltanCrossover = properties.speciesId == SPECIES_MELTAN && metLocation == LOCATION_ROUGE_SECTOR1;
            if (metLocation != properties.location && !isMeltanCrossover)
            {
                return false;
            }
            if (properties.formId != evolution.formId && !isFormChangeable(properties.speciesId, properties.formId, pokemon.form(), Enums::GameVersion::ZA, getEntityContextGroup(pokemon)))
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
            if (properties.nature != NATURE_RANDOM && pokemon.nature() != properties.nature)
            {
                return false;
            }
            if (pokemon.hasAlphaFlag() && pokemon.isAlpha() != properties.isAlpha)
            {
                return false;
            }
            return true;
        }

        // PKHeX EncounterStatic9a.GetMatchRating (IsMatchPartial, then IsMatchDeferred)
        EncounterMatchRating EncounterStatic9a::getMatchRating(const Pokemon::Pokemon &pokemon) const
        {
            const bool isShinyPokemon = isShiny(pokemon);
            if ((properties.shiny == RNG::Shiny::Never && isShinyPokemon) || (properties.shiny == RNG::Shiny::Always && !isShinyPokemon))
            {
                return EncounterMatchRating::PartialMatch;
            }
            if (properties.scale != 0 && pokemon.hasScale() && pokemon.scale() != properties.scale)
            {
                return EncounterMatchRating::PartialMatch;
            }

            if (properties.shiny != RNG::Shiny::Random && !isShinyValid(properties.shiny, pokemon))
            {
                return EncounterMatchRating::DeferredErrors;
            }
            if (properties.isAlpha && isAlphaMoveFlagMissing9a(pokemon, properties.speciesId, properties.formId))
            {
                return EncounterMatchRating::DeferredErrors;
            }
            const SeedCorrelationResult correlation = tryGetSeed(pokemon);
            if (correlation == SeedCorrelationResult::Invalid)
            {
                return EncounterMatchRating::DeferredErrors;
            }
            if (correlation == SeedCorrelationResult::Ignore)
            {
                return EncounterMatchRating::Deferred;
            }
            return EncounterMatchRating::Match;
        }

        // PKHeX EncounterStatic9a.TryGetSeed: one roll, as for everything but the Hyperspace legends.
        SeedCorrelationResult EncounterStatic9a::tryGetSeed(const Pokemon::Pokemon &pokemon) const noexcept
        {
            RNG::GenerateParam9a parameters = getParameters(false, false);
            if (isSeedFound9a(parameters, pokemon))
            {
                return SeedCorrelationResult::Success;
            }
            if (!isHyperspaceShinyPossible())
            {
                return SeedCorrelationResult::Invalid;
            }
            if ((isShiny(pokemon) && !RNG::LumioseSolver::SEARCH_SHINY_SINGLE_ROLL) || !RNG::LumioseSolver::SEARCH_SHINY_MANY_ROLLS)
            {
                return SeedCorrelationResult::Ignore;
            }
            parameters.rollCount = 1 + SHINY_CHARM_ROLLS + SHINY_HYPERSPACE_ROLLS;
            return isSeedFound9a(parameters, pokemon) ? SeedCorrelationResult::Success : SeedCorrelationResult::Invalid;
        }

        RNG::GenerateParam9a EncounterStatic9a::getParameters(bool hasShinyCharm, bool hasShinyPower) const noexcept
        {
            RNG::GenerateParam9a parameters;
            parameters.rollCount = 1;
            if (isHyperspaceShinyPossible())
            {
                parameters.rollCount = static_cast<uint8_t>(parameters.rollCount + (hasShinyCharm ? SHINY_CHARM_ROLLS : 0) + (hasShinyPower ? SHINY_HYPERSPACE_ROLLS : 0));
            }
            parameters.genderRatio = getGenderRatio9a(properties.gender, properties.speciesId, properties.formId);
            parameters.flawlessIVs = properties.flawlessIVCount;
            if (properties.isAlpha)
            {
                parameters.correlation = RNG::LumioseCorrelation::PreApplyIVs;
            }
            else if (properties.flawlessIVCount != 0)
            {
                parameters.correlation = RNG::LumioseCorrelation::ReApplyIVs;
            }
            parameters.sizeType = properties.scale == 0 ? RNG::SizeType9::RANDOM : RNG::SizeType9::VALUE;
            parameters.scale = properties.scale;
            parameters.nature = properties.nature;
            parameters.ability = RNG::AbilityPermission::Any12;
            parameters.shiny = properties.shiny;
            parameters.individualValues = properties.individualValues;
            return parameters;
        }
    }
}
