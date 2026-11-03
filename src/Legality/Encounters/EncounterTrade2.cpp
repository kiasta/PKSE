#include "Legality/Encounters/EncounterTrade2.h"

#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/GBRestrictions.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            constexpr uint16_t SPECIES_VOLTORB = 100;
            constexpr uint16_t SPECIES_SHUCKLE = 213;
            constexpr int LANGUAGE_JAPANESE = 1;
            constexpr int LANGUAGE_FRENCH = 3;
            constexpr int LANGUAGE_UNUSED_6 = 6;
            constexpr int LANGUAGE_SPANISH = 7;
            constexpr int LANGUAGE_KOREAN = 8;
        }

        // PKHeX EncounterTrade2.IsMatchExact
        bool EncounterTrade2::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            if (evolution.levelMax < properties.level)
            {
                return false;
            }
            if (getFormatGeneration(pokemon) <= 2)
            {
                // Gender follows the DVs, so the DVs alone are checked.
                if (properties.individualValues.isSpecified)
                {
                    if (!isFixedIVSequenceValidNoRandom(properties.individualValues, pokemon))
                    {
                        return false;
                    }
                    if (pokemon.tid16() != properties.TID16)
                    {
                        return false;
                    }
                }
                if (GameBoyEntity::getCaughtData(pokemon) != 0)
                {
                    if (pokemon.metLocation() != LOCATION_LINK_TRADE2_NPC)
                    {
                        return false;
                    }
                    if (pokemon.metLevel() != 0)
                    {
                        return false;
                    }
                    if (properties.individualValues.isSpecified && pokemon.otGender() != properties.trainerGender)
                    {
                        return false;
                    }
                }
            }
            else
            {
                // Gen 7+: male unless it came from Gold/Silver.
                if (GameBoyEntity::isVirtualConsole1(pokemon) && pokemon.otGender() != 0)
                {
                    return false;
                }
                if (properties.individualValues.isSpecified)
                {
                    if (pokemon.gender() != properties.gender)
                    {
                        return false;
                    }
                    if (pokemon.tid16() != properties.TID16)
                    {
                        return false;
                    }
                }
            }
            return true;
        }

        int EncounterTrade2::detectLanguage(const Pokemon::Pokemon &pokemon, const std::u16string &trainerName, const std::u16string &nickname) const
        {
            const LanguageNames4 &nicknames = *properties.nicknames;
            const LanguageNames4 &trainerNames = *properties.trainerNames;
            if (GameBoyEntity::isJapanese(pokemon))
            {
                if (nickname != nicknames[LANGUAGE_JAPANESE] || trainerName != trainerNames[LANGUAGE_JAPANESE])
                {
                    return -1;
                }
                return LANGUAGE_JAPANESE;
            }
            if (GameBoyEntity::isKorean(pokemon))
            {
                if (nickname != nicknames[LANGUAGE_KOREAN] || trainerName != trainerNames[LANGUAGE_KOREAN])
                {
                    return -1;
                }
                return LANGUAGE_KOREAN;
            }
            // Only the languages an international game can trade with.
            for (int languageIndex = 2; languageIndex < LANGUAGE_KOREAN; ++languageIndex)
            {
                if (languageIndex == LANGUAGE_UNUSED_6)
                {
                    continue;
                }
                if (nickname != nicknames[static_cast<size_t>(languageIndex)])
                {
                    continue;
                }
                if (isTrainerMatchExact(pokemon, trainerName, languageIndex))
                {
                    return languageIndex;
                }
            }
            return -1;
        }

        int EncounterTrade2::detectLanguageNickname(const Pokemon::Pokemon &pokemon, const std::u16string &nickname) const
        {
            const LanguageNames4 &names = *properties.nicknames;
            if (GameBoyEntity::isJapanese(pokemon))
            {
                return nickname == names[LANGUAGE_JAPANESE] ? LANGUAGE_JAPANESE : -1;
            }
            if (GameBoyEntity::isKorean(pokemon))
            {
                return nickname == names[LANGUAGE_KOREAN] ? LANGUAGE_KOREAN : -1;
            }
            for (int languageIndex = 2; languageIndex < static_cast<int>(names.size()); ++languageIndex)
            {
                if (languageIndex == LANGUAGE_UNUSED_6)
                {
                    continue;
                }
                if (nickname == names[static_cast<size_t>(languageIndex)])
                {
                    return languageIndex;
                }
            }
            return -1;
        }

        int EncounterTrade2::detectLanguageTrainer(const Pokemon::Pokemon &pokemon, const std::u16string &trainerName) const
        {
            const LanguageNames4 &names = *properties.trainerNames;
            if (GameBoyEntity::isJapanese(pokemon))
            {
                return trainerName == names[LANGUAGE_JAPANESE] ? LANGUAGE_JAPANESE : -1;
            }
            if (GameBoyEntity::isKorean(pokemon))
            {
                return trainerName == names[LANGUAGE_KOREAN] ? LANGUAGE_KOREAN : -1;
            }
            for (int languageIndex = 2; languageIndex < static_cast<int>(names.size()); ++languageIndex)
            {
                if (languageIndex == LANGUAGE_UNUSED_6)
                {
                    continue;
                }
                if (isTrainerMatchExact(pokemon, trainerName, languageIndex))
                {
                    return languageIndex;
                }
            }
            return -1;
        }

        bool EncounterTrade2::isTrainerMatchExact(const Pokemon::Pokemon &pokemon, const std::u16string &trainerName, int language) const
        {
            const char16_t *expected = (*properties.trainerNames)[static_cast<size_t>(language)];
            if (getFormatGeneration(pokemon) >= 7 && language == LANGUAGE_SPANISH && pokemon.language() == LANGUAGE_FRENCH)
            {
                // A Spanish name's accented glyphs do not survive the move to a French Virtual Console game.
                if (properties.speciesId == SPECIES_VOLTORB)
                {
                    expected = u"FALCçN";
                }
                else if (properties.speciesId == SPECIES_SHUCKLE)
                {
                    expected = u"MANôA";
                }
            }
            return trainerName == expected;
        }

        // PKHeX EncounterTrade2.GetMatchRating: IsTrainerNicknameCorrect
        EncounterMatchRating EncounterTrade2::getMatchRating(const Pokemon::Pokemon &pokemon) const
        {
            if (detectLanguage(pokemon, pokemon.otName(), pokemon.nickname()) == -1)
            {
                return EncounterMatchRating::DeferredErrors;
            }
            return EncounterMatchRating::Match;
        }

        bool EncounterTrade2::isNicknameMatch(const Pokemon::Pokemon &pokemon, const std::u16string &nickname, int) const
        {
            const std::u16string trainerName = pokemon.otName();
            if (detectLanguage(pokemon, trainerName, nickname) != -1)
            {
                return true;
            }
            // The trainer is flagged on its own, so blame the nickname only when the trainer matches
            // some language or the nickname matches none.
            if (detectLanguageTrainer(pokemon, trainerName) != -1)
            {
                return false;
            }
            if (detectLanguageNickname(pokemon, nickname) == -1)
            {
                return false;
            }
            return true;
        }

        bool EncounterTrade2::isTrainerMatch(const Pokemon::Pokemon &pokemon, const std::u16string &trainerName, int) const
        {
            return detectLanguageTrainer(pokemon, trainerName) != -1;
        }

        std::u16string EncounterTrade2::getNickname(int language) const
        {
            const size_t languageIndex = language >= 0 && static_cast<size_t>(language) < properties.nicknames->size() ? static_cast<size_t>(language) : 0;
            return (*properties.nicknames)[languageIndex];
        }
    }
}
