#include "Legality/Encounters/EncounterTrade6.h"

#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterRules8.h"
#include "Legality/Encounters/EncounterTrade3.h"
#include "Legality/Encounters/EncounterTrade7.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            constexpr uint16_t SPECIES_FARFETCHD = 83;
        }

        bool EncounterTrade6::isTrainerMatch(const Pokemon::Pokemon &pokemon, const std::u16string &trainerName, int language) const
        {
            (void)pokemon;
            return isLanguageNameMatch(properties.trainerNames, trainerName, language);
        }

        bool EncounterTrade6::isNicknameMatch(const Pokemon::Pokemon &pokemon, const std::u16string &nickname, int language) const
        {
            (void)pokemon;
            // PKHeX accepts both apostrophes in the English Farfetch'd's name.
            if (properties.speciesId == SPECIES_FARFETCHD && (nickname == u"Quacklin’" || nickname == u"Quacklin'"))
            {
                return true;
            }
            return isLanguageNameMatch(properties.nicknames, nickname, language);
        }

        std::u16string EncounterTrade6::getNickname(int language) const
        {
            return getLanguageNameOrFirst(properties.nicknames, language);
        }

        // PKHeX EncounterTrade6.IsMatchExact and IsMatchNatureGenderShiny
        bool EncounterTrade6::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
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
            if (pokemon.id32() != ID32())
            {
                return false;
            }
            if (evolution.formId != 0 && !isFormChangeable(properties.speciesId, 0, pokemon.form(), Enums::GameVersion::XY, getEntityContextGroup(pokemon)))
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
