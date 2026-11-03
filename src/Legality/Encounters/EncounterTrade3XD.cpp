#include "Legality/Encounters/EncounterTrade3XD.h"

#include "Enums/LanguageID.h"
#include "Legality/Encounters/EncounterEntity.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            constexpr uint16_t SPECIES_ELEKID = 239;
        }

        // PKHeX EncounterTrade3XD.IsMatchExact. The SID is the player's, so only the TID is fixed.
        bool EncounterTrade3XD::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            if (!isMatchExactGameCube(pokemon, evolution, properties.speciesId, properties.level, properties.location))
            {
                return false;
            }
            return pokemon.tid16() == properties.TID16;
        }

        // PKHeX EncounterTrade3XD.GetMatchRating: every XD trade arrives in a Poke Ball.
        EncounterMatchRating EncounterTrade3XD::getMatchRating(const Pokemon::Pokemon &pokemon) const
        {
            return pokemon.ball() != static_cast<uint8_t>(Enums::Ball::Poke) ? EncounterMatchRating::PartialMatch : EncounterMatchRating::Match;
        }

        // PKHeX EncounterTrade3XD.IsTrainerMatch
        bool EncounterTrade3XD::isTrainerMatch(const Pokemon::Pokemon &pokemon, const std::u16string &trainerName, int language) const
        {
            if (language <= 0 || static_cast<size_t>(language) >= properties.trainerNames->size())
            {
                return false;
            }
            const char16_t *expected = (*properties.trainerNames)[static_cast<size_t>(language)];
            if (getFormatGeneration(pokemon) == 3)
            {
                return trainerName == expected;
            }
            // Duking's Spanish name loses its accent past Gen 3; Hordel's Elekid has none to lose.
            if (language == static_cast<int>(Enums::LanguageID::Spanish) && properties.speciesId != SPECIES_ELEKID)
            {
                return trainerName == TRAINER_NAME_DUKING_SPANISH4;
            }
            return trainerName == expected;
        }

        // PKHeX EncounterTrade3XD.IsNicknameMatch
        bool EncounterTrade3XD::isNicknameMatch(const Pokemon::Pokemon &pokemon, const std::u16string &nickname, int language) const
        {
            if (properties.nicknames == nullptr)
            {
                return true;
            }
            if (language <= 0 || static_cast<size_t>(language) >= properties.nicknames->size())
            {
                return false;
            }
            const std::u16string expected = (*properties.nicknames)[static_cast<size_t>(language)];
            if (getFormatGeneration(pokemon) == 3)
            {
                return nickname == expected;
            }
            return nickname == transferGlyphs34(expected, language);
        }

        std::u16string EncounterTrade3XD::getNickname(int language) const
        {
            if (properties.nicknames == nullptr)
            {
                return std::u16string();
            }
            const size_t languageIndex = language >= 0 && static_cast<size_t>(language) < properties.nicknames->size() ? static_cast<size_t>(language) : 0;
            return (*properties.nicknames)[languageIndex];
        }
    }
}
