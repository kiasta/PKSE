#include "Legality/Encounters/EncounterTrade3.h"

#include "Enums/LanguageID.h"
#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Verifiers/NameRules.h"
#include "Pokemon/Pokemon.h"
#include "Utils/Gen3Text.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            constexpr uint16_t SPECIES_JYNX = 124;
            constexpr uint8_t JAPANESE_START_GLYPH = 0x01;
            constexpr uint8_t JAPANESE_END_GLYPH = 0xA0;
            constexpr uint8_t JAPANESE_YEN_GLYPH = 0xB7;

            // LeafGreen's Italian Jynx trade carries the English names.
            int getTradeLanguage(uint16_t speciesId, const Pokemon::Pokemon &pokemon, int language)
            {
                if (speciesId == SPECIES_JYNX && isVersion(pokemon, Enums::GameVersion::LG) && language == static_cast<int>(Enums::LanguageID::Italian))
                {
                    return static_cast<int>(Enums::LanguageID::English);
                }
                return language;
            }
        }

        bool isFixedIVSequenceValidSkipRandom(const RNG::IndividualValueSet &individualValues, const Pokemon::Pokemon &pokemon) noexcept
        {
            constexpr int maxIndividualValue = 31;
            const auto isMismatch = [](int8_t templateValue, uint8_t pokemonValue) { return templateValue >= 0 && templateValue <= maxIndividualValue && templateValue != pokemonValue; };
            if (isMismatch(individualValues.HP, pokemon.ivHP()) || isMismatch(individualValues.ATK, pokemon.ivATK()) || isMismatch(individualValues.DEF, pokemon.ivDEF()))
            {
                return false;
            }
            if (isMismatch(individualValues.SPE, pokemon.ivSPE()) || isMismatch(individualValues.SPA, pokemon.ivSPA()) || isMismatch(individualValues.SPD, pokemon.ivSPD()))
            {
                return false;
            }
            return true;
        }

        // PKHeX StringConverter345.TransferGlyphs34(ReadOnlySpan<char>, int, Span<char>)
        std::u16string transferGlyphs34(const std::u16string &text, int language)
        {
            if (language == static_cast<int>(Enums::LanguageID::Japanese))
            {
                return text;
            }
            std::u16string result = text;
            for (char16_t &character : result)
            {
                const uint8_t glyph = Utils::charToGen3(character, static_cast<uint8_t>(language));
                const bool isJapaneseGlyph = (glyph >= JAPANESE_START_GLYPH && glyph <= JAPANESE_END_GLYPH) || glyph == JAPANESE_YEN_GLYPH;
                if (isJapaneseGlyph)
                {
                    character = Utils::gen3ToChar(glyph, static_cast<uint8_t>(Enums::LanguageID::Japanese));
                }
            }
            return result;
        }

        // PKHeX StringConverter345.TransferGlyphs56
        std::u16string transferGlyphs56(std::u16string text)
        {
            for (char16_t &character : text)
            {
                if (character == u'\'')
                {
                    character = u'’';
                }
            }
            return text;
        }

        // PKHeX EncounterTrade3.IsMatchExact
        bool EncounterTrade3::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            if (pokemon.encryptionConstant() != properties.PID)
            {
                return false;
            }
            if (!isFixedIVSequenceValidSkipRandom(properties.individualValues, pokemon))
            {
                return false;
            }
            if (pokemon.tid16() != properties.TID16 || pokemon.sid16() != properties.SID16)
            {
                return false;
            }
            if (evolution.levelMax < properties.level)
            {
                return false;
            }
            if (evolution.formId != 0 && !isFormChangeable(properties.speciesId, 0, pokemon.form(), Enums::GameVersion::RSE, getEntityContextGroup(pokemon)))
            {
                return false;
            }
            if (pokemon.gender() != properties.gender || pokemon.otGender() != properties.trainerGender)
            {
                return false;
            }
            if (pokemon.eggLocation() != 0)
            {
                return false;
            }
            if (pokemon.hasContestStats())
            {
                for (int statIndex = 0; statIndex < static_cast<int>(properties.contestStats.size()); statIndex++)
                {
                    if (pokemon.contestStat(statIndex) < properties.contestStats[statIndex])
                    {
                        return false;
                    }
                }
            }
            return true;
        }

        // PKHeX EncounterTrade3.IsTrainerMatch
        bool EncounterTrade3::isTrainerMatch(const Pokemon::Pokemon &pokemon, const std::u16string &trainerName, int language) const
        {
            language = getTradeLanguage(properties.speciesId, pokemon, language);
            if (language <= 0 || language >= static_cast<int>(properties.trainerNames->size()) || (*properties.trainerNames)[language] == nullptr)
            {
                return false;
            }
            const std::u16string expectedName = (*properties.trainerNames)[language];
            if (getEntityContextGroup(pokemon) == Enums::GameVersion::RSE || isVirtualConsole(pokemon))
            {
                return trainerName == expectedName;
            }
            return trainerName == transferGlyphs34(expectedName, language);
        }

        // PKHeX EncounterTrade3.IsNicknameMatch
        bool EncounterTrade3::isNicknameMatch(const Pokemon::Pokemon &pokemon, const std::u16string &nickname, int language) const
        {
            language = getTradeLanguage(properties.speciesId, pokemon, language);
            if (language <= 0 || language >= static_cast<int>(properties.nicknames->size()) || (*properties.nicknames)[language] == nullptr)
            {
                return false;
            }
            const std::u16string expectedName = (*properties.nicknames)[language];
            if (getEntityContextGroup(pokemon) == Enums::GameVersion::RSE || isVirtualConsole(pokemon))
            {
                return nickname == expectedName;
            }
            std::u16string transferredName = transferGlyphs34(expectedName, language);
            if (getFormatGeneration(pokemon) >= 6)
            {
                transferredName = transferGlyphs56(transferredName);
            }
            return nickname == transferredName;
        }

        std::u16string EncounterTrade3::getNickname(int language) const
        {
            if (language < 0 || language >= static_cast<int>(properties.nicknames->size()) || (*properties.nicknames)[language] == nullptr)
            {
                language = 0;
            }
            const char16_t *nickname = (*properties.nicknames)[language];
            return nickname != nullptr ? std::u16string(nickname) : std::u16string();
        }
    }
}
