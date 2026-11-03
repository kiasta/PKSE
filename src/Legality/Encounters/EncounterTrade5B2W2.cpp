#include "Legality/Encounters/EncounterTrade5B2W2.h"

#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterRules8.h"
#include "Legality/Encounters/EncounterTrade5BW.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        // PKHeX EncounterTrade5B2W2.IsMatchExact and IsMatchNatureGenderShiny
        bool EncounterTrade5B2W2::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            if (pokemon.metLevel() != properties.level)
            {
                return false;
            }
            if (properties.individualValues.isSpecified && !isFixedIVSequenceValidNoRandom(properties.individualValues, pokemon))
            {
                return false;
            }
            if (!isShinyValid(RNG::Shiny::Never, pokemon))
            {
                return false;
            }
            if (properties.gender != GENDER_RANDOM && properties.gender != pokemon.gender())
            {
                return false;
            }
            if (isFixedNature() && pokemon.nature() != properties.nature)
            {
                return false;
            }
            if (pokemon.id32() != properties.ID32)
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

        bool EncounterTrade5B2W2::isTrainerMatch(const Pokemon::Pokemon &, const std::u16string &trainerName, int language) const
        {
            return isLanguageInRange5(properties.trainerNames, language) && trainerName == getLanguageName5(properties.trainerNames, language);
        }

        bool EncounterTrade5B2W2::isNicknameMatch(const Pokemon::Pokemon &, const std::u16string &nickname, int language) const
        {
            return isLanguageInRange5(properties.nicknames, language) && nickname == getLanguageName5(properties.nicknames, language);
        }

        std::u16string EncounterTrade5B2W2::getNickname(int language) const
        {
            return getLanguageName5(properties.nicknames, isLanguageInRange5(properties.nicknames, language) ? language : 0);
        }
    }
}
