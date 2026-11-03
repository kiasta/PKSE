#include "Legality/Encounters/EncounterTrade8.h"

#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterRules8.h"
#include "Legality/Encounters/EncounterTrade4PID.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        std::u16string getLanguageName(const LanguageNamesSwitch *names, int language)
        {
            if (names == nullptr || language < 0 || language >= static_cast<int>(names->size()))
            {
                return std::u16string();
            }
            const char16_t *text = (*names)[language];
            return text == nullptr ? std::u16string() : std::u16string(text);
        }

        bool EncounterTrade8::isTrainerMatch(const Pokemon::Pokemon &pokemon, const std::u16string &trainerName, int language) const
        {
            (void)pokemon;
            if (properties.trainerNames == nullptr || language < 0 || language >= static_cast<int>(properties.trainerNames->size()))
            {
                return false;
            }
            return trainerName == getLanguageName(properties.trainerNames, language);
        }

        bool EncounterTrade8::isNicknameMatch(const Pokemon::Pokemon &pokemon, const std::u16string &nickname, int language) const
        {
            (void)pokemon;
            if (properties.nicknames == nullptr || language < 0 || language >= static_cast<int>(properties.nicknames->size()))
            {
                return false;
            }
            return nickname == getLanguageName(properties.nicknames, language);
        }

        std::u16string EncounterTrade8::getNickname(int language) const
        {
            if (properties.nicknames == nullptr)
            {
                return std::u16string();
            }
            const bool isInRange = language >= 0 && language < static_cast<int>(properties.nicknames->size());
            return getLanguageName(properties.nicknames, isInRange ? language : 0);
        }

        // PKHeX EncounterTrade8.IsMatchExact and IsMatchNatureGenderShiny
        bool EncounterTrade8::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            if (pokemon.metLevel() != properties.level)
            {
                return false;
            }
            if (properties.individualValues.isSpecified && !isFixedIVSequenceValidNoRandom(properties.individualValues, pokemon))
            {
                return false;
            }
            if (!isShinyValid(properties.shiny, pokemon))
            {
                return false;
            }
            if (properties.nature < NATURE_RANDOM && pokemon.nature() != properties.nature)
            {
                return false;
            }
            if (properties.gender != GENDER_RANDOM && pokemon.gender() != properties.gender)
            {
                return false;
            }
            if (pokemon.id32() != properties.ID32)
            {
                return false;
            }
            if (isFormatSwordShield(pokemon) && getDynamaxLevel(pokemon) < properties.dynamaxLevel)
            {
                return false;
            }
            if (properties.flawlessIVCount != 0 && getFlawlessIVCount(pokemon) < properties.flawlessIVCount)
            {
                return false;
            }
            if (evolution.formId != properties.formId && !isFormChangeable(properties.speciesId, properties.formId, pokemon.form(), Enums::GameVersion::SWSH, getEntityContextGroup(pokemon)))
            {
                return false;
            }
            if (pokemon.otGender() != properties.trainerGender)
            {
                return false;
            }
            return isMatchEggLocation(pokemon, 0);
        }
    }
}
