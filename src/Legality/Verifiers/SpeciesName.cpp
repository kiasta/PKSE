// Port of PKHeX PKHeX.Core/PKM/Util/SpeciesName.cs and PKM/Util/Language.cs (GetAvailableGameLanguages).
#include "Legality/Verifiers/SpeciesName.h"

#include <cstring>

#include "Enums/LanguageID.h"
#include "Legality/Verifiers/WordFilter.h"
#include "Names/NameLanguage.h"
#include "Pokemon/Evolution.h"
#include "Pokemon/PersonalInfoTable.h"
#include "Utils/StringHelpers.h"

namespace Names
{
    // Declared in Trainer/Trainer.h's layer, forward-declared here the way Conversion/Convert.cpp does.
    const char *getSpeciesNameLocalized(uint16_t speciesId, size_t languageIndex);
}

namespace Legality
{
    namespace
    {
        constexpr uint8_t LANGUAGE_JAPANESE = static_cast<uint8_t>(Enums::LanguageID::Japanese);
        constexpr uint8_t LANGUAGE_ENGLISH = static_cast<uint8_t>(Enums::LanguageID::English);
        constexpr uint8_t LANGUAGE_FRENCH = static_cast<uint8_t>(Enums::LanguageID::French);
        constexpr uint8_t LANGUAGE_ITALIAN = static_cast<uint8_t>(Enums::LanguageID::Italian);
        constexpr uint8_t LANGUAGE_GERMAN = static_cast<uint8_t>(Enums::LanguageID::German);
        constexpr uint8_t LANGUAGE_UNUSED = static_cast<uint8_t>(Enums::LanguageID::UNUSED_6);
        constexpr uint8_t LANGUAGE_SPANISH = static_cast<uint8_t>(Enums::LanguageID::Spanish);
        constexpr uint8_t LANGUAGE_KOREAN = static_cast<uint8_t>(Enums::LanguageID::Korean);
        constexpr uint8_t LANGUAGE_CHINESE_SIMPLIFIED = static_cast<uint8_t>(Enums::LanguageID::ChineseSimplified);
        constexpr uint8_t LANGUAGE_CHINESE_TRADITIONAL = static_cast<uint8_t>(Enums::LanguageID::ChineseTraditional);
        constexpr uint8_t LANGUAGE_SPANISH_LATIN_AMERICA = static_cast<uint8_t>(Enums::LanguageID::SpanishL);

        // clang-format off
        constexpr uint8_t ALL_LANGUAGES[] = {
            LANGUAGE_JAPANESE, LANGUAGE_ENGLISH, LANGUAGE_FRENCH, LANGUAGE_ITALIAN, LANGUAGE_GERMAN,
            LANGUAGE_SPANISH, LANGUAGE_KOREAN, LANGUAGE_CHINESE_SIMPLIFIED, LANGUAGE_CHINESE_TRADITIONAL,
            LANGUAGE_SPANISH_LATIN_AMERICA,
        };
        // clang-format on
        constexpr size_t GAME_BOY_LANGUAGE_COUNT = 7;
        constexpr size_t GENERATION_THREE_LANGUAGE_COUNT = 6;
        constexpr size_t GENERATION_NINE_LANGUAGE_COUNT = 9;

        constexpr uint16_t FARFETCHD_SPECIES = 83;
        constexpr uint16_t POLIWHIRL_SPECIES = 61;
        constexpr uint16_t TANGELA_SPECIES = 114;
        constexpr uint16_t PORYGON2_SPECIES = 233;
        constexpr uint16_t PORYGONZ_SPECIES = 474;
        constexpr uint16_t COFAGRIGUS_SPECIES = 563;
        constexpr uint16_t PANGORO_SPECIES = 675;
        constexpr uint16_t MIMIKYU_SPECIES = 778;

        bool hasSpeciesNameTable(uint8_t language)
        {
            return language != 0 && language != LANGUAGE_UNUSED && language <= LANGUAGE_SPANISH_LATIN_AMERICA;
        }

        /// The species table entry in UTF-8, or null when the language or species has none.
        const char *getPlainSpeciesNameText(uint16_t speciesId, uint8_t language)
        {
            if (!hasSpeciesNameTable(language) || speciesId == 0 || speciesId > Pokemon::PERSONAL_MAX_SPECIES)
            {
                return nullptr;
            }
            return Names::getSpeciesNameLocalized(speciesId, Names::languageIndexFor(static_cast<Enums::LanguageID>(language)));
        }

        /// PKHeX SpeciesName.GetEggName(language).
        std::u16string getPlainEggName(uint8_t language)
        {
            switch (static_cast<Enums::LanguageID>(language))
            {
            case Enums::LanguageID::Japanese:
                return u"タマゴ";
            case Enums::LanguageID::English:
                return u"Egg";
            case Enums::LanguageID::French:
                return u"Œuf";
            case Enums::LanguageID::Italian:
                return u"Uovo";
            case Enums::LanguageID::German:
                return u"Ei";
            case Enums::LanguageID::Spanish:
            case Enums::LanguageID::SpanishL:
                return u"Huevo";
            case Enums::LanguageID::Korean:
                return u"알";
            case Enums::LanguageID::ChineseSimplified:
            case Enums::LanguageID::ChineseTraditional:
                return u"蛋";
            default:
                return std::u16string();
            }
        }

        /// PKHeX SpeciesName.GetSpeciesName(species, language).
        std::u16string getPlainSpeciesName(uint16_t speciesId, uint8_t language)
        {
            if (speciesId == 0)
            {
                return getPlainEggName(language);
            }
            const char *speciesNameText = getPlainSpeciesNameText(speciesId, language);
            return speciesNameText ? Utils::utf8ToUtf16(speciesNameText) : std::u16string();
        }

        /// PKHeX SpeciesName.GetEggName1234.
        std::u16string getEggNameBeforeGenerationFive(uint8_t language, int generation)
        {
            if (generation == 3)
            {
                return u"タマゴ"; // every Gen 3 egg carries the Japanese name
            }
            if (language == LANGUAGE_FRENCH)
            {
                return generation == 2 ? u"OEUF" : u"Oeuf";
            }
            const std::u16string eggName = getPlainEggName(language);
            if (generation == 4)
            {
                return eggName;
            }
            std::u16string upperCaseEggName = eggName;
            for (char16_t &character : upperCaseEggName)
            {
                character = toUpperInvariant(character);
            }
            return upperCaseEggName;
        }

        /// The format group whose generation selects speciesNameAsStored's spelling.
        Enums::GameVersion getGroupForGeneration(int generation)
        {
            switch (generation)
            {
            case 1:
                return Enums::GameVersion::RBY;
            case 2:
                return Enums::GameVersion::GSC;
            case 3:
                return Enums::GameVersion::RSE;
            default:
                return Enums::GameVersion::DP;
            }
        }

        /// PKHeX SpeciesName.GetSpeciesName1234.
        std::u16string getSpeciesNameBeforeGenerationFive(uint16_t speciesId, uint8_t language, int generation)
        {
            if (speciesId == 0)
            {
                return getEggNameBeforeGenerationFive(language, generation);
            }
            if (getPlainSpeciesNameText(speciesId, language) == nullptr)
            {
                return std::u16string();
            }
            if (language == LANGUAGE_KOREAN && generation == 2)
            {
                // Korean Gold/Silver spell two names the way no later game does.
                if (speciesId == POLIWHIRL_SPECIES)
                {
                    return u"수륙챙이";
                }
                if (speciesId == TANGELA_SPECIES)
                {
                    return u"덩구리";
                }
            }
            return Pokemon::speciesNameAsStored(speciesId, getGroupForGeneration(generation), language);
        }

        /// PKHeX SpeciesName.GetSpeciesName7ZH: the Simplified Chinese names Generation 8's DLC revised.
        std::u16string getSpeciesNameGenerationSevenChinese(uint16_t speciesId, uint8_t language)
        {
            switch (speciesId)
            {
            case PORYGON2_SPECIES:
                return u"多边兽Ⅱ";
            case PORYGONZ_SPECIES:
                return u"多边兽Ｚ";
            case MIMIKYU_SPECIES:
                return u"谜拟Ｑ";
            case COFAGRIGUS_SPECIES:
                return u"死神棺";
            case PANGORO_SPECIES:
                return u"流氓熊猫";
            default:
                return getPlainSpeciesName(speciesId, language);
            }
        }

        bool equalsIgnoringCase(const char *speciesNameText, const std::u16string &lowerCaseName)
        {
            const std::u16string speciesName = Utils::utf8ToUtf16(speciesNameText);
            if (speciesName.size() != lowerCaseName.size())
            {
                return false;
            }
            for (size_t characterIndex = 0; characterIndex < speciesName.size(); ++characterIndex)
            {
                if (toLowerInvariant(speciesName[characterIndex]) != lowerCaseName[characterIndex])
                {
                    return false;
                }
            }
            return true;
        }

        bool tryGetSpecies(const std::u16string &name, Enums::GameVersion contextGroup, bool ignoreCase, uint16_t &speciesId)
        {
            speciesId = 0;
            if (name.empty())
            {
                return false;
            }
            const std::string nameText = Utils::utf16ToUtf8(name);
            std::u16string lowerCaseName = name;
            for (char16_t &character : lowerCaseName)
            {
                character = toLowerInvariant(character);
            }
            for (const uint8_t language : getContextLanguages(contextGroup))
            {
                // PKHeX's dictionary keeps the LAST species of a duplicated name, so search downward.
                for (uint16_t candidateSpecies = Pokemon::PERSONAL_MAX_SPECIES; candidateSpecies >= 1; --candidateSpecies)
                {
                    const char *speciesNameText = getPlainSpeciesNameText(candidateSpecies, language);
                    if (speciesNameText == nullptr)
                    {
                        continue;
                    }
                    const bool isMatch = ignoreCase ? equalsIgnoringCase(speciesNameText, lowerCaseName) : std::strcmp(speciesNameText, nameText.c_str()) == 0;
                    if (isMatch)
                    {
                        speciesId = candidateSpecies;
                        return true;
                    }
                }
            }
            return false;
        }
    }

    int getContextGeneration(Enums::GameVersion contextGroup)
    {
        return Enums::getVersionGeneration(Enums::getGroupRepVersion(contextGroup));
    }

    std::span<const uint8_t> getContextLanguages(Enums::GameVersion contextGroup)
    {
        const std::span<const uint8_t> allLanguages(ALL_LANGUAGES);
        switch (getContextGeneration(contextGroup))
        {
        case 1:
        case 3:
            return allLanguages.first(GENERATION_THREE_LANGUAGE_COUNT);
        case 2:
        case 4:
        case 5:
        case 6:
            return allLanguages.first(GAME_BOY_LANGUAGE_COUNT);
        case 7:
        case 8:
            return allLanguages.first(GENERATION_NINE_LANGUAGE_COUNT);
        case 9:
            if (contextGroup == Enums::GameVersion::SV)
            {
                return allLanguages.first(GENERATION_NINE_LANGUAGE_COUNT);
            }
            return allLanguages;
        default:
            return allLanguages;
        }
    }

    bool isApostropheFarfetchdLanguage(uint8_t language)
    {
        return language == LANGUAGE_ENGLISH || language == LANGUAGE_ITALIAN || language == LANGUAGE_SPANISH;
    }

    std::u16string getSpeciesNameForGeneration(uint16_t speciesId, uint8_t language, int generation)
    {
        if (generation <= 4)
        {
            return getSpeciesNameBeforeGenerationFive(speciesId, language, generation);
        }
        if (generation == 5 && speciesId == FARFETCHD_SPECIES && isApostropheFarfetchdLanguage(language))
        {
            return u"Farfetch'd"; // the Gen 5 font has no curly apostrophe
        }
        if (generation == 7 && language == LANGUAGE_CHINESE_SIMPLIFIED)
        {
            return getSpeciesNameGenerationSevenChinese(speciesId, language);
        }
        return getPlainSpeciesName(speciesId, language);
    }

    std::u16string getEggNameForGeneration(uint8_t language, int generation)
    {
        if (generation <= 4)
        {
            return getEggNameBeforeGenerationFive(language, generation);
        }
        return getPlainEggName(language);
    }

    bool isNicknamedInLanguage(uint16_t speciesId, const std::u16string &nickname, uint8_t language, int generation)
    {
        return nickname != getSpeciesNameForGeneration(speciesId, language, generation);
    }

    bool isNicknamedAnyLanguage(uint16_t speciesId, const std::u16string &nickname, Enums::GameVersion contextGroup)
    {
        const int generation = getContextGeneration(contextGroup);
        for (const uint8_t language : getContextLanguages(contextGroup))
        {
            if (!isNicknamedInLanguage(speciesId, nickname, language, generation))
            {
                return false;
            }
        }
        return true;
    }

    bool tryGetSpeciesAnyLanguage(const std::u16string &name, Enums::GameVersion contextGroup, uint16_t &speciesId)
    {
        return tryGetSpecies(name, contextGroup, false, speciesId);
    }

    bool tryGetSpeciesAnyLanguageIgnoringCase(const std::u16string &name, Enums::GameVersion contextGroup, uint16_t &speciesId)
    {
        return tryGetSpecies(name, contextGroup, true, speciesId);
    }
}
