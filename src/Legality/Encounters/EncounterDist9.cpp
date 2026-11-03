#include "Legality/Encounters/EncounterDist9.h"

#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterRules8.h"
#include "Legality/Encounters/EncounterTera9.h"
#include "Legality/Encounters/EncounterTrade3.h"
#include "Legality/RNG/Encounter9RNG.h"
#include "Legality/RNG/Tera9RNG.h"
#include "Legality/RNG/Xoroshiro128Plus.h"
#include "Pokemon/PersonalRecord.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            constexpr int STAGE_COUNT = 4;

            // PKHeX EncounterDist9.GetIsPossibleSlot, for one game's weights.
            bool isPossibleSlotForGame(uint16_t rateTotal, uint16_t rateMinimum, uint8_t randomRate, uint32_t seed) noexcept
            {
                if (rateTotal == 0)
                {
                    return false;
                }
                RNG::Xoroshiro128Plus random(seed);
                random.nextInt(100);
                const int rateRandom = static_cast<int>(random.nextInt(rateTotal));
                return static_cast<uint32_t>(rateRandom - rateMinimum) < randomRate;
            }
        }

        int getRaidProgressMaximum9(const RaidStageRates9 &rates, uint8_t randomRate, uint32_t seed) noexcept
        {
            // From the highest stage down, where most raids are joined.
            for (int stage = STAGE_COUNT - 1; stage >= 0; stage--)
            {
                if (isPossibleSlotForGame(rates.totalScarlet[stage], rates.minimumScarlet[stage], randomRate, seed))
                {
                    return stage;
                }
                if (isPossibleSlotForGame(rates.totalViolet[stage], rates.minimumViolet[stage], randomRate, seed))
                {
                    return stage;
                }
            }
            return -1;
        }

        // PKHeX EncounterDist9.IsMatchExact
        bool EncounterDist9::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
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
            if (properties.individualValues.isSpecified && !isFixedIVSequenceValidSkipRandom(properties.individualValues, pokemon))
            {
                return false;
            }
            return true;
        }

        // PKHeX EncounterDist9.GetMatchRating and IsMatchDeferred
        EncounterMatchRating EncounterDist9::getMatchRating(const Pokemon::Pokemon &pokemon) const
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

        // PKHeX EncounterDist9.IsMatchPartial
        bool EncounterDist9::isMatchPartial(const Pokemon::Pokemon &pokemon) const
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
            parameters.genderRatio = Pokemon::getPersonalRecord(Enums::GameVersion::SV, properties.speciesId, properties.formId).genderRatio;
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
