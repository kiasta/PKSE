#include "Legality/Encounters/EncounterTrade9a.h"

#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterRules8.h"
#include "Legality/Encounters/EncounterTrade3.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            std::u16string getTradeName(const LanguageNames9a *names, int language)
            {
                if (names == nullptr || language < 0 || language >= static_cast<int>(names->size()) || (*names)[language] == nullptr)
                {
                    return std::u16string();
                }
                return std::u16string((*names)[language]);
            }
        }

        bool EncounterTrade9a::isTrainerMatch(const Pokemon::Pokemon &pokemon, const std::u16string &trainerName, int language) const
        {
            (void)pokemon;
            return language >= 0 && language < static_cast<int>(LanguageNames9a().size()) && trainerName == getTradeName(properties.trainerNames, language);
        }

        bool EncounterTrade9a::isNicknameMatch(const Pokemon::Pokemon &pokemon, const std::u16string &nickname, int language) const
        {
            (void)pokemon;
            return language >= 0 && language < static_cast<int>(LanguageNames9a().size()) && nickname == getTradeName(properties.nicknames, language);
        }

        std::u16string EncounterTrade9a::getNickname(int language) const
        {
            const bool isInRange = language >= 0 && language < static_cast<int>(LanguageNames9a().size());
            return getTradeName(properties.nicknames, isInRange ? language : 0);
        }

        // PKHeX EncounterTrade9a.IsMatchExact and IsMatchNatureGenderShiny
        bool EncounterTrade9a::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            if (pokemon.metLevel() != properties.level)
            {
                return false;
            }
            if (pokemon.metLocation() != LOCATION_LINK_TRADE6_NPC)
            {
                return false;
            }
            if (!isShinyValid(RNG::Shiny::Never, pokemon))
            {
                return false;
            }
            if (pokemon.gender() != properties.gender)
            {
                return false;
            }
            if (properties.nature != NATURE_RANDOM && pokemon.nature() != properties.nature)
            {
                return false;
            }
            if (pokemon.id32() != properties.ID32)
            {
                return false;
            }
            if (evolution.formId != properties.formId && !isFormChangeable(properties.speciesId, properties.formId, pokemon.form(), Enums::GameVersion::ZA, getEntityContextGroup(pokemon)))
            {
                return false;
            }
            if (pokemon.otGender() != properties.trainerGender)
            {
                return false;
            }
            if (!isMatchEggLocation(pokemon, 0))
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
            if (pokemon.hasAlphaFlag() && pokemon.isAlpha())
            {
                return false;
            }
            return true;
        }

        // PKHeX EncounterTrade9a.GetMatchRating
        EncounterMatchRating EncounterTrade9a::getMatchRating(const Pokemon::Pokemon &pokemon) const
        {
            if (pokemon.hasScale() && pokemon.scale() != SCALE)
            {
                return EncounterMatchRating::PartialMatch;
            }
            if (tryGetSeed(pokemon) != SeedCorrelationResult::Success)
            {
                return EncounterMatchRating::DeferredErrors;
            }
            return EncounterMatchRating::Match;
        }

        // PKHeX EncounterTrade9a.TryGetSeed
        SeedCorrelationResult EncounterTrade9a::tryGetSeed(const Pokemon::Pokemon &pokemon) const noexcept
        {
            return isSeedFound9a(getParameters(), pokemon) ? SeedCorrelationResult::Success : SeedCorrelationResult::Invalid;
        }

        RNG::GenerateParam9a EncounterTrade9a::getParameters() const noexcept
        {
            RNG::GenerateParam9a parameters;
            parameters.genderRatio = getGenderRatio9a(properties.gender, properties.speciesId, properties.formId);
            parameters.flawlessIVs = properties.flawlessIVCount;
            parameters.rollCount = 1;
            parameters.correlation = RNG::LumioseCorrelation::ReApplyIVs;
            parameters.sizeType = RNG::SizeType9::VALUE;
            parameters.scale = SCALE;
            parameters.nature = properties.nature;
            parameters.ability = RNG::AbilityPermission::Any12;
            parameters.shiny = RNG::Shiny::Never;
            parameters.individualValues = properties.individualValues;
            return parameters;
        }
    }
}
