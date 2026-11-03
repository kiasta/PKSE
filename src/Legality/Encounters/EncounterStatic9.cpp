#include "Legality/Encounters/EncounterStatic9.h"

#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterLocations.h"
#include "Legality/Encounters/EncounterRules8.h"
#include "Legality/Encounters/EncounterTrade3.h"
#include "Legality/RNG/HomeQuirks.h"
#include "Legality/RNG/Tera9RNG.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            constexpr uint16_t SPECIES_YUNGOOS = 734;
            constexpr uint16_t SPECIES_MUNCHLAX = 446;
            constexpr uint16_t SPECIES_KORAIDON = 1007;
            constexpr uint16_t SPECIES_MIRAIDON = 1008;
        }

        bool EncounterStatic9::isScriptedYungoos() const noexcept
        {
            return properties.speciesId == SPECIES_YUNGOOS && properties.level == 2;
        }

        bool EncounterStatic9::isStarterBoxLegend() const noexcept
        {
            return isGift() && (properties.speciesId == SPECIES_KORAIDON || properties.speciesId == SPECIES_MIRAIDON);
        }

        bool EncounterStatic9::hasRequiredCraftyMark() const noexcept
        {
            return properties.speciesId == SPECIES_MUNCHLAX;
        }

        // PKHeX EncounterStatic9.IsMatchExact
        bool EncounterStatic9::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            if (!isLevelWithinRange(pokemon.metLevel()))
            {
                return false;
            }
            if (properties.gender != GENDER_RANDOM && pokemon.gender() != properties.gender)
            {
                return false;
            }
            if (!isMatchEggLocationInternal(pokemon))
            {
                return false;
            }
            if (!isMatchLocation9(pokemon, isMatchLocationExact(pokemon), properties.location, Enums::GameVersion::SV))
            {
                return false;
            }
            if (properties.formId != evolution.formId && !isFormChangeable(properties.speciesId, properties.formId, pokemon.form(), Enums::GameVersion::SV, getEntityContextGroup(pokemon)))
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
            if (properties.teraType != RNG::GemType::Random && pokemon.hasTeraType() && !RNG::Tera9RNG::isMatchTeraType(properties.teraType, properties.speciesId, properties.formId, pokemon.teraTypeOriginal()))
            {
                return false;
            }
            if (properties.nature < NATURE_RANDOM && pokemon.nature() != properties.nature)
            {
                return false;
            }
            return true;
        }

        // PKHeX EncounterStatic9.IsMatchEggLocationInternal
        bool EncounterStatic9::isMatchEggLocationInternal(const Pokemon::Pokemon &pokemon) const
        {
            if (!isEgg())
            {
                return isMatchEggLocation(pokemon, properties.eggLocation);
            }
            const uint16_t eggLocation = pokemon.eggLocation();
            if (getRemapState(Enums::GameVersion::SV, getEntityContextGroup(pokemon)) == LocationRemapState::Remapped)
            {
                return eggLocation == 0;
            }
            return eggLocation == properties.eggLocation || (!pokemon.isEgg() && eggLocation == LOCATION_LINK_TRADE6);
        }

        // PKHeX EncounterStatic9.IsMatchLocationExact
        bool EncounterStatic9::isMatchLocationExact(const Pokemon::Pokemon &pokemon) const noexcept
        {
            const uint16_t metLocation = pokemon.metLocation();
            if (metLocation == properties.location)
            {
                return true;
            }
            if (isEgg())
            {
                return !pokemon.isEgg() || metLocation == LOCATION_LINK_TRADE6;
            }
            return false;
        }

        // PKHeX EncounterStatic9.GetMatchRating and IsMatchDeferred
        EncounterMatchRating EncounterStatic9::getMatchRating(const Pokemon::Pokemon &pokemon) const
        {
            if (isMatchPartial(pokemon))
            {
                return EncounterMatchRating::PartialMatch;
            }
            if (properties.shiny != RNG::Shiny::Random && !isShinyValid(properties.shiny, pokemon))
            {
                return EncounterMatchRating::DeferredErrors;
            }
            return getAbilityDeferral9(pokemon, properties.ability, properties.speciesId, properties.formId, Enums::GameVersion::SV);
        }

        // PKHeX EncounterStatic9.IsMatchPartial
        bool EncounterStatic9::isMatchPartial(const Pokemon::Pokemon &pokemon) const
        {
            if (isShinyStateMismatch(properties.shiny, pokemon))
            {
                return true;
            }
            if (RNG::HomeQuirks::hasSizeScalars(pokemon) && properties.scalarValue != 0)
            {
                if (isGift())
                {
                    if (pokemon.heightScalar() != properties.scalarValue)
                    {
                        return true;
                    }
                    if (pokemon.weightScalar() != properties.scalarValue)
                    {
                        return true;
                    }
                }
                const uint8_t currentSize = pokemon.hasScale() ? pokemon.scale() : pokemon.heightScalar();
                if (currentSize != properties.scalarValue)
                {
                    return true;
                }
            }
            return isPartialMatchHiddenAbility(pokemon, properties.speciesId);
        }
    }
}
