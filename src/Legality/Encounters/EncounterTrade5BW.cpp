#include "Legality/Encounters/EncounterTrade5BW.h"

#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterRules8.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        bool isLanguageInRange5(const LanguageNames4 *names, int language) noexcept
        {
            return names != nullptr && language >= 0 && language < static_cast<int>(names->size());
        }

        std::u16string getLanguageName5(const LanguageNames4 *names, int language)
        {
            if (!isLanguageInRange5(names, language) || (*names)[language] == nullptr)
            {
                return std::u16string();
            }
            return std::u16string((*names)[language]);
        }

        bool EncounterTrade5BW::isValidMissingLanguage(const Pokemon::Pokemon &pokemon) noexcept
        {
            return getFormatGeneration(pokemon) == 5;
        }

        // PKHeX EncounterTrade5BW.IsMatchExact and IsMatchNatureGenderShiny
        bool EncounterTrade5BW::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            if (pokemon.metLevel() != properties.level)
            {
                return false;
            }
            if (!isFixedIVSequenceValidNoRandom(properties.individualValues, pokemon))
            {
                return false;
            }
            if (properties.PID != pokemon.encryptionConstant() || properties.nature != pokemon.nature())
            {
                return false;
            }
            if (pokemon.id32() != ID32())
            {
                return false;
            }
            if (evolution.formId != properties.formId && !isFormChangeable(properties.speciesId, properties.formId, pokemon.formID(), Enums::GameVersion::BW, getEntityContextGroup(pokemon)))
            {
                return false;
            }
            if (pokemon.otGender() != properties.trainerGender)
            {
                return false;
            }
            return isMatchEggLocation(pokemon, 0);
        }

        bool EncounterTrade5BW::isTrainerMatch(const Pokemon::Pokemon &, const std::u16string &trainerName, int language) const
        {
            return isLanguageInRange5(properties.trainerNames, language) && trainerName == getLanguageName5(properties.trainerNames, language);
        }

        bool EncounterTrade5BW::isNicknameMatch(const Pokemon::Pokemon &, const std::u16string &nickname, int language) const
        {
            return isLanguageInRange5(properties.nicknames, language) && nickname == getLanguageName5(properties.nicknames, language);
        }

        std::u16string EncounterTrade5BW::getNickname(int language) const
        {
            return getLanguageName5(properties.nicknames, isLanguageInRange5(properties.nicknames, language) ? language : 0);
        }
    }
}
