#include "Legality/Encounters/EncounterOutbreak9.h"

#include <bit>

#include "Legality/Encounters/EncounterRules8.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        // PKHeX EncounterOutbreak9.GetFirstMetLocation
        uint16_t EncounterOutbreak9::location() const noexcept
        {
            int firstIndex = std::countr_zero(properties.metFlagsLow);
            if (firstIndex == 64)
            {
                firstIndex += std::countr_zero(properties.metFlagsHigh);
            }
            return static_cast<uint16_t>(properties.metBase + firstIndex);
        }

        bool EncounterOutbreak9::isMetLocationMatch(uint16_t metLocation) const noexcept
        {
            const int flagIndex = static_cast<int>(metLocation) - properties.metBase;
            if (flagIndex < 0 || flagIndex >= 128)
            {
                return false;
            }
            if (flagIndex < 64)
            {
                return ((properties.metFlagsLow >> flagIndex) & 1) != 0;
            }
            return ((properties.metFlagsHigh >> (flagIndex - 64)) & 1) != 0;
        }

        // PKHeX EncounterOutbreak9.IsMatchExact
        bool EncounterOutbreak9::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            if (!isLevelWithinRange(pokemon.metLevel()))
            {
                return false;
            }
            if (properties.gender != GENDER_RANDOM && pokemon.gender() != properties.gender)
            {
                return false;
            }
            if (evolution.formId != properties.formId)
            {
                return false;
            }
            if (!isMatchEggLocation(pokemon, 0))
            {
                return false;
            }
            return isMatchLocation9(pokemon, isMetLocationMatch(pokemon.metLocation()), location(), Enums::GameVersion::SV);
        }

        // PKHeX EncounterOutbreak9.GetMatchRating and IsMatchDeferred
        EncounterMatchRating EncounterOutbreak9::getMatchRating(const Pokemon::Pokemon &pokemon) const
        {
            if (isPartialMatchHiddenAbility(pokemon, properties.speciesId))
            {
                return EncounterMatchRating::PartialMatch;
            }
            const RNG::Shiny outbreakShiny = shiny();
            if (outbreakShiny != RNG::Shiny::Random && !isShinyValid(outbreakShiny, pokemon))
            {
                return EncounterMatchRating::DeferredErrors;
            }
            const EncounterMatchRating abilityRating = getAbilityDeferral9(pokemon, RNG::AbilityPermission::Any12, properties.speciesId, properties.formId, Enums::GameVersion::SV);
            if (abilityRating != EncounterMatchRating::Match)
            {
                return abilityRating;
            }
            const RibbonMarks9 marks = readRibbonMarks9(pokemon);
            if (marks.isPresent && marks.weather != WeatherMark::None && !isMarkCompatible9(properties.weather, marks.weather))
            {
                return EncounterMatchRating::DeferredErrors;
            }
            return EncounterMatchRating::Match;
        }
    }
}
