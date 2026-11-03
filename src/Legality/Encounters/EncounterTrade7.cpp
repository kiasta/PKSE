#include "Legality/Encounters/EncounterTrade7.h"

#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterRules8.h"
#include "Legality/Encounters/EncounterTrade3.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        bool isLanguageNameMatch(const LanguageNamesSwitch *names, const std::u16string &text, int language)
        {
            // A nullptr entry is past the end of PKHeX's table for that game.
            if (names == nullptr || language < 0 || language >= static_cast<int>(names->size()) || (*names)[language] == nullptr)
            {
                return false;
            }
            return text == getLanguageName(names, language);
        }

        std::u16string getLanguageNameOrFirst(const LanguageNamesSwitch *names, int language)
        {
            if (names == nullptr)
            {
                return std::u16string();
            }
            const bool isInRange = language >= 0 && language < static_cast<int>(names->size()) && (*names)[language] != nullptr;
            return getLanguageName(names, isInRange ? language : 0);
        }

        bool EncounterTrade7::isTrainerMatch(const Pokemon::Pokemon &pokemon, const std::u16string &trainerName, int language) const
        {
            (void)pokemon;
            return isLanguageNameMatch(properties.trainerNames, trainerName, language);
        }

        bool EncounterTrade7::isNicknameMatch(const Pokemon::Pokemon &pokemon, const std::u16string &nickname, int language) const
        {
            (void)pokemon;
            return isLanguageNameMatch(properties.nicknames, nickname, language);
        }

        std::u16string EncounterTrade7::getNickname(int language) const
        {
            return getLanguageNameOrFirst(properties.nicknames, language);
        }

        // PKHeX EncounterTrade7.IsMatchExact and IsMatchNatureGenderShiny
        bool EncounterTrade7::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            if (pokemon.metLevel() != properties.level)
            {
                return false;
            }
            if (properties.individualValues.isSpecified && !isFixedIVSequenceValidSkipRandom(properties.individualValues, pokemon))
            {
                return false;
            }
            if (!isShinyValid(RNG::Shiny::Never, pokemon))
            {
                return false;
            }
            if (properties.gender != pokemon.gender())
            {
                return false;
            }
            if (properties.nature < NATURE_RANDOM && pokemon.nature() != properties.nature)
            {
                return false;
            }
            if (pokemon.id32() != properties.ID32)
            {
                return false;
            }
            if (evolution.formId != properties.formId && !isFormChangeable(properties.speciesId, properties.formId, pokemon.form(), Enums::GameVersion::SM, getEntityContextGroup(pokemon)))
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
            if (properties.evolveOnTrade && pokemon.speciesID() == properties.speciesId)
            {
                return false;
            }
            return true;
        }
    }
}
