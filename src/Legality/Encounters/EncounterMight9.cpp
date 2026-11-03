#include "Legality/Encounters/EncounterMight9.h"

#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterRules8.h"
#include "Legality/Encounters/EncounterTera9.h"
#include "Legality/Encounters/EncounterTrade3.h"
#include "Legality/RNG/Encounter9RNG.h"
#include "Legality/RNG/Tera9RNG.h"
#include "Pokemon/PersonalRecord.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            // PKHeX PersonalInfo.RatioMagicMale / RatioMagicFemale / RatioMagicGenderless.
            constexpr uint8_t RATIO_MAGIC_MALE = 0;
            constexpr uint8_t RATIO_MAGIC_FEMALE = 254;
            constexpr uint8_t RATIO_MAGIC_GENDERLESS = 255;
        }

        // PKHeX EncounterMight9.GetGender: a fixed gender rolls as that gender's ratio.
        uint8_t EncounterMight9::getGenderRatio() const noexcept
        {
            switch (properties.gender)
            {
            case 0:
                return RATIO_MAGIC_MALE;
            case 1:
                return RATIO_MAGIC_FEMALE;
            case 2:
                return RATIO_MAGIC_GENDERLESS;
            default:
                return Pokemon::getPersonalRecord(Enums::GameVersion::SV, properties.speciesId, properties.formId).genderRatio;
            }
        }

        // PKHeX EncounterMight9.IsMatchExact
        bool EncounterMight9::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
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
            if (!isMatchLocation9(pokemon, pokemon.metLocation() == LOCATION_TERA_CAVERN9, LOCATION_TERA_CAVERN9, Enums::GameVersion::SV))
            {
                return false;
            }
            if (properties.formId != evolution.formId && !isFormChangeable(properties.speciesId, properties.formId, pokemon.form(), Enums::GameVersion::SV, getEntityContextGroup(pokemon)))
            {
                return false;
            }
            return true;
        }

        // PKHeX EncounterMight9.GetMatchRating and IsMatchDeferred (which checks no shiny state)
        EncounterMatchRating EncounterMight9::getMatchRating(const Pokemon::Pokemon &pokemon) const
        {
            if (isMatchPartial(pokemon))
            {
                return EncounterMatchRating::PartialMatch;
            }
            return getAbilityDeferral9(pokemon, properties.ability, properties.speciesId, properties.formId, Enums::GameVersion::SV);
        }

        // PKHeX EncounterMight9.IsMatchPartial
        bool EncounterMight9::isMatchPartial(const Pokemon::Pokemon &pokemon) const
        {
            if (isShinyStateMismatch(properties.shiny, pokemon))
            {
                return true;
            }
            if (properties.individualValues.isSpecified && !isFixedIVSequenceValidSkipRandom(properties.individualValues, pokemon))
            {
                return true;
            }
            const uint32_t seed = RNG::Tera9RNG::getOriginalSeed(pokemon);
            if (isRaidSeedMismatch9(pokemon, seed, properties.teraType, properties.speciesId, properties.formId))
            {
                return true;
            }
            if (!canBeEncountered(seed))
            {
                return true;
            }
            RNG::GenerateParam9 parameters;
            parameters.species = properties.speciesId;
            parameters.genderRatio = getGenderRatio();
            parameters.flawlessIVs = properties.flawlessIVCount;
            parameters.rollCount = 1;
            parameters.scaleType = properties.scaleType;
            parameters.scale = properties.scale;
            parameters.ability = properties.ability;
            parameters.shiny = properties.shiny;
            parameters.nature = properties.nature;
            parameters.individualValues = properties.individualValues;
            if (!RNG::Encounter9RNG::isMatch(pokemon, parameters, seed))
            {
                return true;
            }
            return isPartialMatchHiddenAbility(pokemon, properties.speciesId);
        }
    }
}
