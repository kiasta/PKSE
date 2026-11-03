#include "Legality/Encounters/EncounterTrade4PID.h"

#include "Enums/LanguageID.h"
#include "Legality/Encounters/EncounterEntity.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            constexpr uint16_t SPECIES_PIKACHU = 25;
            constexpr uint16_t SPECIES_MAGIKARP = 129;

            std::u16string getLanguageName(const LanguageNames4 *names, int language)
            {
                if (names == nullptr || language < 0 || language >= static_cast<int>(names->size()) || (*names)[language] == nullptr)
                {
                    return std::u16string();
                }
                return std::u16string((*names)[language]);
            }

            bool isLanguageInRange(const LanguageNames4 *names, int language) noexcept
            {
                return names != nullptr && language >= 0 && language < static_cast<int>(names->size());
            }

            /// PKHeX IContestStatsReadOnly.IsContestBelow: any of the Pokemon's contest stats is below the template's.
            bool isContestBelow(const Pokemon::Pokemon &pokemon, const std::array<uint8_t, 6> &initialStats) noexcept
            {
                if (!pokemon.hasContestStats())
                {
                    return false;
                }
                for (int statIndex = 0; statIndex < static_cast<int>(initialStats.size()); statIndex++)
                {
                    if (pokemon.contestStat(statIndex) < initialStats[statIndex])
                    {
                        return true;
                    }
                }
                return false;
            }
        }

        bool isFixedIVSequenceValidNoRandom(const RNG::IndividualValueSet &individualValues, const Pokemon::Pokemon &pokemon) noexcept
        {
            return individualValues.HP == pokemon.ivHP() && individualValues.ATK == pokemon.ivATK() && individualValues.DEF == pokemon.ivDEF() && individualValues.SPE == pokemon.ivSPE() && individualValues.SPA == pokemon.ivSPA() && individualValues.SPD == pokemon.ivSPD();
        }

        // PKHeX EncounterTrade4PID.IsMatchExact and its helpers
        bool EncounterTrade4PID::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            const bool isFormat4 = getFormatGeneration(pokemon) == 4;
            if (!isFormat4)
            {
                if (evolution.levelMax < properties.level)
                {
                    return false;
                }
            }
            else if (!isMetUnset() ? pokemon.metLevel() != properties.level : pokemon.metLevel() < properties.level)
            {
                return false;
            }
            if (pokemon.id32() != ID32())
            {
                return false;
            }
            if (isFormat4 && pokemon.metLocation() != location())
            {
                return false;
            }
            if (!isFixedIVSequenceValidNoRandom(properties.individualValues, pokemon))
            {
                return false;
            }
            if (pokemon.encryptionConstant() != properties.PID || fixedNature() != pokemon.nature())
            {
                return false;
            }
            if (evolution.formId != 0 && !isFormChangeable(properties.speciesId, 0, pokemon.formID(), Enums::GameVersion::DP, getEntityContextGroup(pokemon)))
            {
                return false;
            }
            if (pokemon.otGender() != properties.trainerGender)
            {
                return false;
            }
            if (pokemon.eggLocation() != 0)
            {
                return false;
            }
            return !isContestBelow(pokemon, contestStats());
        }

        bool EncounterTrade4PID::isTrainerMatch(const Pokemon::Pokemon &, const std::u16string &trainerName, int language) const
        {
            return isLanguageInRange(properties.trainerNames, language) && trainerName == getLanguageName(properties.trainerNames, language);
        }

        bool EncounterTrade4PID::isNicknameMatch(const Pokemon::Pokemon &, const std::u16string &nickname, int language) const
        {
            return isLanguageInRange(properties.nicknames, language) && nickname == getLanguageName(properties.nicknames, language);
        }

        std::u16string EncounterTrade4PID::getNickname(int language) const
        {
            return getLanguageName(properties.nicknames, isLanguageInRange(properties.nicknames, language) ? language : 0);
        }

        bool EncounterTrade4PID::isLanguageSwap() const noexcept
        {
            return properties.speciesId == SPECIES_MAGIKARP || properties.speciesId == SPECIES_PIKACHU;
        }

        bool EncounterTrade4PID::isIncorrectEnglish(const Pokemon::Pokemon &pokemon) const noexcept
        {
            return pokemon.language() == static_cast<uint8_t>(Enums::LanguageID::English) && (isVersion(pokemon, Enums::GameVersion::D) || isVersion(pokemon, Enums::GameVersion::P));
        }

        // PKHeX EncounterTrade4PID.DetectOriginalLanguage and the per-species helpers
        int EncounterTrade4PID::detectOriginalLanguage(const Pokemon::Pokemon &pokemon) const
        {
            using Enums::LanguageID;
            const int language = pokemon.language();
            if (properties.speciesId == SPECIES_PIKACHU)
            {
                // Every HeartGold/SoulSilver Pikachu has the English names, whatever the game's language.
                if (language == static_cast<int>(LanguageID::French))
                {
                    return static_cast<int>(LanguageID::English);
                }
                const int detected = detectTradeLanguage(pokemon.otName(), language);
                if (detected == static_cast<int>(LanguageID::English))
                {
                    const bool isItalian = pokemon.nickname() == getLanguageName(properties.nicknames, static_cast<int>(LanguageID::Italian));
                    return static_cast<int>(isItalian ? LanguageID::Italian : LanguageID::Spanish);
                }
                return detected;
            }
            if (properties.speciesId == SPECIES_MAGIKARP)
            {
                // Every Diamond/Pearl/Platinum Magikarp has the German names.
                if (language == static_cast<int>(LanguageID::English))
                {
                    return static_cast<int>(LanguageID::German);
                }
                const int detected = detectTradeLanguage(pokemon.otName(), language);
                if (detected == static_cast<int>(LanguageID::English))
                {
                    const bool isFrench = pokemon.nickname() == getLanguageName(properties.nicknames, static_cast<int>(LanguageID::French));
                    return static_cast<int>(isFrench ? LanguageID::French : LanguageID::Spanish);
                }
                return detected;
            }
            // English Diamond/Pearl wrote the Japanese language id.
            if (language != static_cast<int>(LanguageID::Japanese) || !(isVersion(pokemon, Enums::GameVersion::D) || isVersion(pokemon, Enums::GameVersion::P)))
            {
                return language;
            }
            const bool isJapaneseName = pokemon.otName() == getLanguageName(properties.trainerNames, static_cast<int>(LanguageID::Japanese));
            return isJapaneseName ? language : static_cast<int>(LanguageID::English);
        }

        int EncounterTrade4PID::detectTradeLanguage(const std::u16string &trainerName, int currentLanguage) const
        {
            if (properties.trainerNames == nullptr)
            {
                return currentLanguage;
            }
            for (int language = 1; language < static_cast<int>(properties.trainerNames->size()); language++)
            {
                if (trainerName == getLanguageName(properties.trainerNames, language))
                {
                    return language;
                }
            }
            return currentLanguage;
        }
    }
}
