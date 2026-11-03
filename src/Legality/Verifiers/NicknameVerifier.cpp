// Port of PKHeX PKHeX.Core/Legality/Verifiers/NicknameVerifier.cs: nickname, nickname flag and egg names.
//
// PKHeX checks a Mystery Gift's or in-game trade's fixed nickname and OT against the template
// (ILangNicknamedTemplate, IFixedTrainer, IFixedNickname, MysteryGift.IsNicknamed); the encounter
// engine's matched template answers those.
#include <string>

#include "Enums/LanguageID.h"
#include "Legality/Encounters/EncounterLocations67.h"
#include "Legality/Encounters/Encounterable.h"
#include "Legality/Encounters/LegalityInfo.h"
#include "Legality/Encounters/MysteryGiftPCD.h"
#include "Legality/Encounters/MysteryGiftPGF.h"
#include "Legality/Encounters/MysteryGiftWC6.h"
#include "Legality/Encounters/MysteryGiftWC7.h"
#include "Legality/Verifiers/HandlerRules.h"
#include "Legality/Verifiers/NameRules.h"
#include "Legality/Verifiers/SpeciesName.h"
#include "Legality/Verifiers/Verifiers.h"
#include "Legality/Verifiers/WordFilter.h"
#include "Pokemon/Pokemon.h"
#include "Utils/Gen2KoreanText.h"
#include "Utils/StringHelpers.h"

namespace Legality
{
    namespace
    {
        constexpr char16_t STRING_TERMINATOR_GENERATION_4 = u'￿';
        constexpr uint16_t FARFETCHD_SPECIES = 83;
        constexpr uint16_t SIRFETCHD_SPECIES = 865;
        constexpr uint16_t GRENINJA_SPECIES = 658;
        constexpr uint16_t STEELIX_SPECIES = 208;
        constexpr uint16_t GLALIE_SPECIES = 362;
        constexpr uint8_t VERSION_POKEMON_GO = 34;
        constexpr int GAME_BOY_MAX_NICKNAME_LENGTH_WESTERN = 10;
        constexpr int GAME_BOY_MAX_NICKNAME_LENGTH_JAPANESE = 5;
        constexpr uint16_t EGG_LOCATION_JACQ_SCARLET_VIOLET = 60005;

        /// PKHeX `match.IsEgg`.
        bool isEggEncounter(const AnalysisContext &context)
        {
            const Encounters::Encounterable *encounterMatch = Encounters::getEncounterMatch(context);
            return encounterMatch != nullptr && encounterMatch->isEgg();
        }

        /// The Switch-era Mystery Gifts, whose fixed nicknames PKHeX checks (ILangNicknamedTemplate).
        bool isLanguageNicknamedGift(const AnalysisContext &context)
        {
            return context.encounter.type == EncounterType::Event && (getOriginContextGroup(context) == Enums::GameVersion::GG || getOriginGeneration(context) >= 8);
        }

        /// PKHeX EggStateLegality.IsNicknameFlagSet(IEncounterTemplate, PKM); false when there is no
        /// matched encounter to answer it.
        bool tryGetEggNicknameFlag(const AnalysisContext &context, bool &flagState)
        {
            const Encounters::Encounterable *encounterMatch = Encounters::getEncounterMatch(context);
            if (encounterMatch == nullptr)
            {
                return false;
            }
            using Encounters::EncounterTemplateType;
            const EncounterTemplateType templateType = encounterMatch->templateType();
            if (templateType == EncounterTemplateType::Static7)
            {
                flagState = false;
            }
            else if ((templateType == EncounterTemplateType::MysteryGiftWB8 || templateType == EncounterTemplateType::Static8b) && isUntraded(context))
            {
                flagState = false;
            }
            else if (templateType == EncounterTemplateType::Static9 && encounterMatch->eggLocation() == EGG_LOCATION_JACQ_SCARLET_VIOLET)
            {
                flagState = false; // the Jacq egg does not have the flag set
            }
            else
            {
                flagState = encounterMatch->generation() != 4;
            }
            return true;
        }

        /// PKHeX NicknameVerifier.IsMysteryGiftNoNickname: a gift the player cannot nickname.
        bool isMysteryGiftNoNickname(const Encounters::Encounterable &encounter)
        {
            if (!Encounters::isMysteryGift(encounter.templateType()) || encounter.isEgg())
            {
                return false;
            }
            if (const Encounters::MysteryGiftPCD *card = Encounters::encounterAs<Encounters::MysteryGiftPCD>(encounter))
            {
                return !card->giftData().isNicknamed;
            }
            if (const Encounters::MysteryGiftPGF *card = Encounters::encounterAs<Encounters::MysteryGiftPGF>(encounter))
            {
                return !card->isNicknamed();
            }
            if (const Encounters::MysteryGiftWC6 *card = Encounters::encounterAs<Encounters::MysteryGiftWC6>(encounter))
            {
                const bool isOriginalTrainerNameSet = card->card().trainerName[0] != u'\0';
                // The link demo gifts can be nicknamed.
                const bool isLinkDemoGift = card->location() == Encounters::LOCATION_LINK_GIFT6 && (card->speciesId() == GLALIE_SPECIES || card->speciesId() == STEELIX_SPECIES);
                return !card->card().isNicknamed && isOriginalTrainerNameSet && !isLinkDemoGift;
            }
            if (const Encounters::MysteryGiftWC7 *card = Encounters::encounterAs<Encounters::MysteryGiftWC7>(encounter))
            {
                return !card->card().isNicknamed && card->card().trainerName[0] != u'\0';
            }
            return true;
        }

        /// PKHeX NicknameVerifier.VerifyNicknameEgg.
        void verifyEggNickname(const AnalysisContext &context, const std::u16string &nickname)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            bool flagState = false;
            if (tryGetEggNicknameFlag(context, flagState) && isNicknamedAsStored(context) != flagState)
            {
                context.add(Severity::Invalid, CheckIdentifier::Egg, flagState ? ResultCode::NicknameFlagEggYes : ResultCode::NicknameFlagEggNo);
            }
            if (context.formatGeneration == 2 && !isNicknamedAnyLanguage(0, nickname, Enums::GameVersion::GSC))
            {
                return;
            }
            if (nickname != getEggNameForGeneration(pokemon.language(), getOriginGeneration(context)))
            {
                context.add(Severity::Invalid, CheckIdentifier::Egg, ResultCode::NicknameMatchLanguageEggFail);
            }
        }

        /// PKHeX StringConverter1/2.GetIsJapanese.
        bool isGameBoyJapanese(const std::u16string &text, int originGeneration)
        {
            for (const char16_t character : text)
            {
                const bool isKana = character >= u'　' && character <= u'ー';
                const bool isGenerationTwoPunctuation = originGeneration != 1 && (character == u'？' || character == u'！');
                if (!isKana && !isGenerationTwoPunctuation)
                {
                    return false;
                }
            }
            return true;
        }

        /// PKHeX StringConverter2KOR.GetIsKorean.
        bool isGameBoyKorean(const std::u16string &text)
        {
            for (const char16_t character : text)
            {
                if (!Utils::isGen2KoreanCharacter(character))
                {
                    return false;
                }
            }
            return true;
        }

        /// PKHeX NicknameVerifier.VerifyG1NicknameWithinBounds.
        void verifyGameBoyNicknameWithinBounds(const AnalysisContext &context, const std::u16string &nickname)
        {
            const int nicknameLength = static_cast<int>(nickname.size());
            const bool isEnglish = !isGameBoyJapanese(nickname, 1) && nickname[0] != u'*';
            if (isEnglish)
            {
                if (nicknameLength > GAME_BOY_MAX_NICKNAME_LENGTH_WESTERN)
                {
                    context.add(Severity::Invalid, CheckIdentifier::Nickname, ResultCode::NicknameLengthLong, std::to_string(GAME_BOY_MAX_NICKNAME_LENGTH_WESTERN));
                }
            }
            else if (isGameBoyJapanese(nickname, getOriginGeneration(context)))
            {
                if (nicknameLength > GAME_BOY_MAX_NICKNAME_LENGTH_JAPANESE)
                {
                    context.add(Severity::Invalid, CheckIdentifier::Nickname, ResultCode::NicknameLengthLong, std::to_string(GAME_BOY_MAX_NICKNAME_LENGTH_JAPANESE));
                }
            }
            else if (context.pokemon.language() == static_cast<uint8_t>(Enums::LanguageID::Korean) && isGameBoyKorean(nickname))
            {
                if (nicknameLength > GAME_BOY_MAX_NICKNAME_LENGTH_JAPANESE)
                {
                    context.add(Severity::Invalid, CheckIdentifier::Nickname, ResultCode::NicknameLengthLong, std::to_string(GAME_BOY_MAX_NICKNAME_LENGTH_JAPANESE));
                }
            }
            else
            {
                context.add(Severity::Invalid, CheckIdentifier::Nickname, ResultCode::Gen1CharacterNickname);
            }
        }

        /// PKHeX NicknameVerifier.CanNicknameForeign8Plus. A visit to Sword/Shield or Z-A through
        /// HOME is only seen when the Pokemon is in or from one of them.
        bool canNicknameForeignGenerationEightPlus(const AnalysisContext &context)
        {
            const Enums::GameVersion originGroup = getOriginContextGroup(context);
            const bool hasVisitedNicknamingGame = context.formatGroup == Enums::GameVersion::SWSH || context.formatGroup == Enums::GameVersion::ZA || originGroup == Enums::GameVersion::SWSH || originGroup == Enums::GameVersion::ZA;
            if (hasVisitedNicknamingGame)
            {
                return true;
            }
            if (context.formatGeneration >= 9)
            {
                return !context.pokemon.isFatefulEncounter(); // Scarlet/Violet will not rename an event
            }
            return false;
        }

        /// PKHeX NicknameVerifier.GetForeignNicknameLength.
        int getForeignNicknameLength(const AnalysisContext &context, int originGeneration)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            int foreignLength = 0;
            if (originGeneration >= 4 && originGeneration <= 7 && isEggEncounter(context) && wasTradedEgg(context))
            {
                foreignLength = getMaxNicknameLength(originGeneration, static_cast<uint8_t>(Enums::LanguageID::English));
            }
            if (pokemon.isFatefulEncounter())
            {
                return foreignLength;
            }
            const uint8_t originVersion = pokemon.hasOriginGame() ? pokemon.originGame() : 0;
            const bool isBrilliantDiamondShiningPearl = originVersion == static_cast<uint8_t>(Enums::GameVersion::BD) || originVersion == static_cast<uint8_t>(Enums::GameVersion::SP);
            if (context.formatGeneration < 8 || isBrilliantDiamondShiningPearl)
            {
                return foreignLength;
            }
            const int futureLength = getMaxNicknameLength(context.formatGeneration, pokemon.language());
            return futureLength > foreignLength ? futureLength : foreignLength;
        }

        /// PKHeX NicknameVerifier.IsMatch45: a Gen 4 species name carried into Gen 5.
        bool isGenerationFourNameInGenerationFive(const std::u16string &nickname, uint16_t speciesId, const std::u16string &expectedName, uint8_t language, bool canHaveAnyLanguage)
        {
            if (speciesId == FARFETCHD_SPECIES)
            {
                if (nickname.size() == expectedName.size())
                {
                    // The Gen 4 apostrophe differs from Gen 5's, so compare upper-cased.
                    bool isMatch = true;
                    for (size_t characterIndex = 0; characterIndex < expectedName.size(); ++characterIndex)
                    {
                        if (nickname[characterIndex] != toUpperInvariant(expectedName[characterIndex]))
                        {
                            isMatch = false;
                            break;
                        }
                    }
                    if (isMatch)
                    {
                        return true;
                    }
                }
                if (isApostropheFarfetchdLanguage(language))
                {
                    return false;
                }
            }
            if (canHaveAnyLanguage)
            {
                return !isNicknamedAnyLanguage(speciesId, nickname, Enums::GameVersion::DP);
            }
            return nickname == getSpeciesNameForGeneration(speciesId, language, 4);
        }

        /// PKHeX NicknameVerifier.IsNotNicknameValid: an un-nicknamed Pokemon holds a name its games
        /// could have written. Where the encounter species is unknown the more permissive reading
        /// is taken, so a missing template never produces a finding.
        bool isUnnicknamedNameValid(const AnalysisContext &context, const std::u16string &nickname)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            const uint16_t speciesId = pokemon.speciesID();
            const int formatGeneration = context.formatGeneration;
            const uint8_t language = pokemon.language();
            const int originGeneration = getOriginGeneration(context);
            const Encounters::Encounterable *encounterMatch = Encounters::getEncounterMatch(context);
            const bool isEncounterSpeciesKnown = encounterMatch != nullptr;
            // An unmatched Pokemon is its own encounter (PKHeX EncounterInvalid takes the species).
            const bool isUnmatched = context.encounter.type == EncounterType::Unmatched;
            const bool isUnevolved = isEncounterSpeciesKnown ? encounterMatch->speciesId() == speciesId : (isUnmatched || speciesId == FARFETCHD_SPECIES);

            // HOME writes a straight apostrophe into a Farfetch'd or Sirfetch'd it imports from the
            // 3DS or GO; evolving in a game corrects it.
            if (formatGeneration >= 8 && (speciesId == FARFETCHD_SPECIES || speciesId == SIRFETCHD_SPECIES) && isUnevolved && isApostropheFarfetchdLanguage(language))
            {
                const bool isFromGo = pokemon.hasOriginGame() && pokemon.originGame() == VERSION_POKEMON_GO;
                if (!isFromGo && originGeneration < 8 && getOriginContextGroup(context) != Enums::GameVersion::GG)
                {
                    return nickname == u"Farfetch'd";
                }
                if (isFromGo && originGeneration == 8)
                {
                    return speciesId == FARFETCHD_SPECIES ? nickname == u"Farfetch'd" : nickname == u"Sirfetch'd";
                }
            }

            if (formatGeneration == 4 && isGlobalTradeSanitizedTrainer(context))
            {
                // A Korean Pokemon traded to an international game through the GTS gets its English name.
                const ActiveTrainer *activeTrainer = context.options.activeTrainer;
                const bool isKoreanGenerationFourTrainer = activeTrainer != nullptr && activeTrainer->language == static_cast<uint8_t>(Enums::LanguageID::Korean) && getContextGeneration(activeTrainer->gameVersion) == 4;
                if (!isKoreanGenerationFourTrainer && nickname == getSpeciesNameForGeneration(speciesId, static_cast<uint8_t>(Enums::LanguageID::English), formatGeneration))
                {
                    return true;
                }
            }

            const std::u16string expectedName = getSpeciesNameForGeneration(speciesId, language, formatGeneration);
            if (nickname == expectedName)
            {
                return true;
            }

            // An evolved or language-traded Pokemon may carry another language's name before Gen 8.
            const bool isAshGreninjaGift = context.encounter.type == EncounterType::Event && speciesId == GRENINJA_SPECIES;
            const bool mayHaveEvolved = isEncounterSpeciesKnown ? encounterMatch->speciesId() != speciesId : !isUnmatched;
            const bool canHaveAnyLanguage = formatGeneration <= 7 && (mayHaveEvolved || wasTradedEgg(context) || isAshGreninjaGift) && context.formatGroup != Enums::GameVersion::GG;
            if (canHaveAnyLanguage && !isNicknamedAnyLanguage(speciesId, nickname, context.formatGroup))
            {
                return true;
            }

            // A gift with a fixed language and no nickname (ILangNick) may hold its own language's name.
            if (context.encounter.type == EncounterType::Event && !isNicknamedAnyLanguage(speciesId, nickname, context.formatGroup))
            {
                return true;
            }

            if (formatGeneration == 5 && originGeneration != 5)
            {
                return isGenerationFourNameInGenerationFive(nickname, speciesId, expectedName, language, canHaveAnyLanguage);
            }
            return false;
        }

        /// PKHeX NicknameVerifier.VerifyUnNicknamedEncounter; true when it reported a finding that ends the check.
        bool verifyNicknameAgainstSpecies(const AnalysisContext &context, const std::u16string &nickname)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            const int originGeneration = getOriginGeneration(context);
            if (!isNicknamedAsStored(context))
            {
                if (context.formatGeneration >= 3 && !isUnnicknamedNameValid(context, nickname))
                {
                    context.add(Severity::Invalid, CheckIdentifier::Nickname, ResultCode::NicknameMatchLanguageFail);
                }
                return false;
            }

            if (originGeneration >= 8 && canNicknameForeignGenerationEightPlus(context))
            {
                // A nickname equal to the species name leaves the flag unset.
                if (!isNicknamedInLanguage(pokemon.speciesID(), nickname, pokemon.language(), context.formatGeneration))
                {
                    context.add(Severity::Invalid, CheckIdentifier::Nickname, ResultCode::NicknameMatchLanguageFlag);
                    return true;
                }
            }
            uint16_t namedSpecies = 0;
            if (tryGetSpeciesAnyLanguage(nickname, context.formatGroup, namedSpecies))
            {
                // PKHeX NicknameSettings.NicknamedAnotherSpecies defaults to Fishy.
                const ResultCode code = namedSpecies == pokemon.speciesID() ? ResultCode::NicknameMatchLanguageFlag : ResultCode::NicknameMatchNoOthersFail;
                context.add(Severity::Warning, CheckIdentifier::Nickname, code);
                return true;
            }
            if (context.formatGeneration <= 7 && context.formatGroup != Enums::GameVersion::GG)
            {
                for (const char16_t character : nickname)
                {
                    if (character >= u'一' && character <= u'鿿')
                    {
                        context.add(Severity::Invalid, CheckIdentifier::Nickname, ResultCode::NicknameInvalidCharacter);
                        return true;
                    }
                }
            }
            const int nicknameLength = static_cast<int>(nickname.size());
            if (nicknameLength > getMaxNicknameLength(originGeneration, pokemon.language()))
            {
                const int foreignLength = getForeignNicknameLength(context, originGeneration);
                const Severity severity = (foreignLength != 0 && nicknameLength <= foreignLength) ? Severity::Warning : Severity::Invalid;
                context.add(severity, CheckIdentifier::Nickname, ResultCode::NicknameLengthLong);
                return true;
            }
            return false;
        }
    }

    void verifyNickname(const AnalysisContext &context)
    {
        const Pokemon::Pokemon &pokemon = context.pokemon;
        const std::u16string nickname = pokemon.nickname();
        if (nickname.empty())
        {
            context.add(Severity::Invalid, CheckIdentifier::Nickname, ResultCode::NicknameLengthShort);
            return;
        }
        if (context.formatGeneration == 4 && nickname.find(STRING_TERMINATOR_GENERATION_4) != std::u16string::npos)
        {
            context.add(Severity::Invalid, CheckIdentifier::Nickname, ResultCode::NicknameInvalidCharacter, "65535");
            return;
        }

        const bool isNicknamed = isNicknamedAsStored(context);
        if (isLanguageNicknamedGift(context))
        {
            if (pokemon.isEgg())
            {
                verifyEggNickname(context, nickname);
            }
            if (isNicknamed)
            {
                return;
            }
        }

        if (context.formatGeneration <= 7 && isNicknamed)
        {
            const Encounters::Encounterable *encounterOriginal = Encounters::getEncounterOriginal(context);
            if (isVirtualConsole(pokemon))
            {
                verifyGameBoyNicknameWithinBounds(context, nickname);
            }
            else if (encounterOriginal != nullptr && isMysteryGiftNoNickname(*encounterOriginal))
            {
                // PKHeX NicknameSettings.NicknamedMysteryGift defaults to Invalid.
                context.add(Severity::Invalid, CheckIdentifier::Nickname, ResultCode::EncounterGiftNicknamed);
            }
        }

        if (context.encounter.type == EncounterType::Trade)
        {
            return; // whether the trade fixes the nickname is the template's
        }

        if (pokemon.isEgg())
        {
            verifyEggNickname(context, nickname);
            return;
        }

        if (verifyNicknameAgainstSpecies(context, nickname))
        {
            return;
        }

        // PKHeX WordFilterSettings default: on for every format.
        if (isNicknamed)
        {
            const Enums::GameVersion nicknameContextGroup = context.formatGeneration >= 8 ? context.formatGroup : getOriginContextGroup(context);
            WordFilterMatch filterMatch;
            if (isWordFiltered(nickname, context.formatGroup, nicknameContextGroup, filterMatch))
            {
                context.add(Severity::Invalid, CheckIdentifier::Nickname, ResultCode::WordFilterFlaggedPattern, Utils::utf16ToUtf8(filterMatch.patternText), getWordFilterTypeName(filterMatch.type));
            }
            const int originGeneration = getOriginGeneration(context);
            if (containsTooManyNumbers(nickname, originGeneration))
            {
                context.add(Severity::Invalid, CheckIdentifier::Nickname, ResultCode::WordFilterTooManyNumbers, std::to_string(getMaxNumberCount(originGeneration)));
            }
        }
    }
}
