// Port of PKHeX PKHeX.Core/Legality/Verifiers/TrainerNameVerifier.cs: original trainer name,
// including its Game Boy path (VerifyOTGB) and the word filter over the OT and handler names.
#include <string>

#include "Enums/LanguageID.h"
#include "Legality/Encounters/LegalityInfo.h"
#include "Legality/Verifiers/NameRules.h"
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
        constexpr char16_t GAME_BOY_TRADE_TRAINER_MARK = u'*';
        constexpr int GAME_BOY_MAX_TRAINER_LENGTH_JAPANESE = 5;
        constexpr int GAME_BOY_MAX_TRAINER_LENGTH_WESTERN = 7;

        /// PKHeX TrainerNameVerifier.SuspiciousOTNames, compared case-insensitively as a prefix.
        bool isTrainerNameSuspicious(const std::u16string &trainerName)
        {
            const std::u16string suspiciousNames[] = {u"PKHeX", u"ＰＫＨｅＸ"};
            for (const std::u16string &suspiciousName : suspiciousNames)
            {
                if (trainerName.size() < suspiciousName.size())
                {
                    continue;
                }
                bool isPrefix = true;
                for (size_t characterIndex = 0; characterIndex < suspiciousName.size(); ++characterIndex)
                {
                    if (toUpperInvariant(trainerName[characterIndex]) != toUpperInvariant(suspiciousName[characterIndex]))
                    {
                        isPrefix = false;
                        break;
                    }
                }
                if (isPrefix)
                {
                    return true;
                }
            }
            return false;
        }

        /// PKHeX TrainerNameVerifier.IsEdgeCaseLength: an OT allowed past its language's limit.
        bool isEdgeCaseLength(const AnalysisContext &context, const std::u16string &trainerName)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            const bool isEggEncounter = Encounters::isEncounterEgg(context);
            if (isEggEncounter)
            {
                // A Gen 3 Mystery Gift egg may carry the gift's own fixed OT (known only to the
                // encounter engine's template).
                if (Encounters::isFixedTrainerGiftEgg(context, trainerName))
                {
                    return true;
                }
                const bool eggEdge = pokemon.isEgg() ? (isTradedEgg(context) || context.formatGeneration == 3) : wasTradedEgg(context);
                if (!eggEdge)
                {
                    return false;
                }
                return static_cast<int>(trainerName.size()) <= getMaxTrainerNameLength(getOriginGeneration(context), static_cast<uint8_t>(Enums::LanguageID::English));
            }
            return context.encounter.type == EncounterType::Trade;
        }

        /// PKHeX StringConverter1/2.GetIsJapanese: every character is kana or CJK punctuation; Gen 2 adds ？ and ！.
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

        /// PKHeX StringConverter1.GetIsEnglish.
        bool isGameBoyEnglish(const std::u16string &text)
        {
            return !isGameBoyJapanese(text, 1) && (text.empty() || text[0] != GAME_BOY_TRADE_TRAINER_MARK);
        }

        /// PKHeX StringConverter12Transporter.GetFilteredOT: the name Poke Transporter gives a Game
        /// Boy OT it cannot display.
        std::u16string getTransporterFilteredTrainerName(uint8_t language, uint8_t originVersion)
        {
            switch (static_cast<Enums::GameVersion>(originVersion))
            {
            case Enums::GameVersion::RD:
                switch (static_cast<Enums::LanguageID>(language))
                {
                case Enums::LanguageID::Japanese:
                    return u"レッド．";
                case Enums::LanguageID::English:
                    return u"Red*";
                case Enums::LanguageID::French:
                    return u"Rouge*";
                case Enums::LanguageID::Italian:
                    return u"Rosso*";
                case Enums::LanguageID::German:
                    return u"Rot*";
                case Enums::LanguageID::Spanish:
                    return u"Rojo*";
                default:
                    return std::u16string();
                }
            case Enums::GameVersion::GN:
                switch (static_cast<Enums::LanguageID>(language))
                {
                case Enums::LanguageID::Japanese:
                    return u"グリーン．";
                case Enums::LanguageID::English:
                    return u"Blue*";
                case Enums::LanguageID::French:
                    return u"Bleu*";
                case Enums::LanguageID::Italian:
                    return u"Blu*";
                case Enums::LanguageID::German:
                    return u"Blau*";
                case Enums::LanguageID::Spanish:
                    return u"Azul*";
                default:
                    return std::u16string();
                }
            case Enums::GameVersion::BU:
                return language == static_cast<uint8_t>(Enums::LanguageID::Japanese) ? u"ブルー．" : std::u16string();
            case Enums::GameVersion::YW:
                switch (static_cast<Enums::LanguageID>(language))
                {
                case Enums::LanguageID::Japanese:
                    return u"イエロー．";
                case Enums::LanguageID::English:
                    return u"Yellow*";
                case Enums::LanguageID::French:
                    return u"Jaune*";
                case Enums::LanguageID::Italian:
                    return u"Giallo*";
                case Enums::LanguageID::German:
                    return u"Gelb*";
                case Enums::LanguageID::Spanish:
                    return u"Amarillo*";
                default:
                    return std::u16string();
                }
            case Enums::GameVersion::GD:
                switch (static_cast<Enums::LanguageID>(language))
                {
                case Enums::LanguageID::Japanese:
                    return u"ゴールド．";
                case Enums::LanguageID::English:
                    return u"Gold*";
                case Enums::LanguageID::French:
                    return u"Or*";
                case Enums::LanguageID::Italian:
                    return u"Oro*";
                case Enums::LanguageID::German:
                    return u"Gold*";
                case Enums::LanguageID::Spanish:
                    return u"Oro*";
                case Enums::LanguageID::Korean:
                    return u"금.";
                default:
                    return std::u16string();
                }
            case Enums::GameVersion::SI:
                switch (static_cast<Enums::LanguageID>(language))
                {
                case Enums::LanguageID::Japanese:
                    return u"シルバー．";
                case Enums::LanguageID::English:
                    return u"Silver*";
                case Enums::LanguageID::French:
                    return u"Argent*";
                case Enums::LanguageID::Italian:
                    return u"Argento*";
                case Enums::LanguageID::German:
                    return u"Silber*";
                case Enums::LanguageID::Spanish:
                    return u"Plata*";
                case Enums::LanguageID::Korean:
                    return u"은.";
                default:
                    return std::u16string();
                }
            case Enums::GameVersion::C:
                switch (static_cast<Enums::LanguageID>(language))
                {
                case Enums::LanguageID::Japanese:
                    return u"クリスタル．";
                case Enums::LanguageID::English:
                    return u"Crystal*";
                case Enums::LanguageID::French:
                    return u"Cristal*";
                case Enums::LanguageID::Italian:
                    return u"Cristallo*";
                case Enums::LanguageID::German:
                    return u"Kristall*";
                case Enums::LanguageID::Spanish:
                    return u"Cristal*";
                default:
                    return std::u16string();
                }
            default:
                return std::u16string();
            }
        }

        /// PKHeX TrainerNameVerifier.VerifyGBOTWithinBounds.
        void verifyGameBoyTrainerNameWithinBounds(const AnalysisContext &context, const std::u16string &trainerName)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            if (context.formatGeneration >= 7)
            {
                const std::u16string filteredName = getTransporterFilteredTrainerName(pokemon.language(), pokemon.originGame());
                if (trainerName == filteredName)
                {
                    return; // Poke Transporter replaced a name it could not carry
                }
            }

            const uint8_t language = pokemon.language();
            const int trainerNameLength = static_cast<int>(trainerName.size());
            if (language == static_cast<uint8_t>(Enums::LanguageID::Japanese))
            {
                if (trainerNameLength > GAME_BOY_MAX_TRAINER_LENGTH_JAPANESE)
                {
                    context.add(Severity::Invalid, CheckIdentifier::Trainer, ResultCode::OTLong, std::to_string(GAME_BOY_MAX_TRAINER_LENGTH_JAPANESE));
                }
                if (!isGameBoyJapanese(trainerName, getOriginGeneration(context)))
                {
                    context.add(Severity::Invalid, CheckIdentifier::Trainer, ResultCode::Gen1CharacterOT);
                }
            }
            else if (language == static_cast<uint8_t>(Enums::LanguageID::Korean))
            {
                if (trainerNameLength > GAME_BOY_MAX_TRAINER_LENGTH_JAPANESE)
                {
                    context.add(Severity::Invalid, CheckIdentifier::Trainer, ResultCode::OTLong, std::to_string(GAME_BOY_MAX_TRAINER_LENGTH_JAPANESE));
                }
                if (!isGameBoyKorean(trainerName))
                {
                    context.add(Severity::Invalid, CheckIdentifier::Trainer, ResultCode::Gen1CharacterOT);
                }
            }
            else
            {
                if (trainerNameLength > GAME_BOY_MAX_TRAINER_LENGTH_WESTERN)
                {
                    context.add(Severity::Invalid, CheckIdentifier::Trainer, ResultCode::OTLong, std::to_string(GAME_BOY_MAX_TRAINER_LENGTH_WESTERN));
                }
                if (!isGameBoyEnglish(trainerName))
                {
                    context.add(Severity::Invalid, CheckIdentifier::Trainer, ResultCode::Gen1CharacterOT);
                }
            }
        }

        /// PKHeX TrainerNameVerifier.VerifyOTGB, for a Game Boy format or a Virtual Console transfer.
        void verifyGameBoyTrainerName(const AnalysisContext &context)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            if (pokemon.otGender() == 1)
            {
                // Red/Blue/Yellow store no OT gender, and the Gen 2 gifts that are not eggs set none.
                // PKHeX also flags a Crystal gift (EncounterGift2) other than an egg, which needs the
                // encounter engine.
                const bool isVirtualConsoleGenerationOne = context.formatGeneration > 2 && pokemon.hasOriginGame() && Enums::getVersionGeneration(pokemon.originGame()) == 1;
                if (isVirtualConsoleGenerationOne || getOriginGeneration(context) == 1)
                {
                    context.add(Severity::Invalid, CheckIdentifier::Trainer, ResultCode::Gen1OTGender);
                }
            }

            if (!isPlayerOriginalTrainer(context))
            {
                return; // the encounter's own OT is the encounter's check
            }

            // PKSE cannot decode Korean Gold/Silver text, so a Korean PK2's name is unknown rather
            // than missing. PKHeX's SK2 (Stadium 2 rental) case does not arise: PKSE opens no
            // Stadium data.
            if (context.formatGeneration <= 2 && pokemon.language() == static_cast<uint8_t>(Enums::LanguageID::Korean))
            {
                return;
            }
            const std::u16string trainerName = pokemon.otName();
            if (trainerName.empty())
            {
                context.add(Severity::Invalid, CheckIdentifier::Trainer, ResultCode::OTShort);
                return;
            }
            verifyGameBoyTrainerNameWithinBounds(context, trainerName);
        }
    }

    void verifyTrainerName(const AnalysisContext &context)
    {
        const Pokemon::Pokemon &pokemon = context.pokemon;
        if (context.formatGeneration <= 2)
        {
            // PKHeX ParsePK1/ParsePK2 call VerifyOTGB directly.
            verifyGameBoyTrainerName(context);
            return;
        }
        if (!isPlayerOriginalTrainer(context))
        {
            return; // the encounter fixes the OT
        }

        const std::u16string trainerName = pokemon.otName();
        if (trainerName.empty())
        {
            context.add(Severity::Invalid, CheckIdentifier::Trainer, ResultCode::OTShort);
            return;
        }
        if (context.formatGeneration == 4 && trainerName.find(STRING_TERMINATOR_GENERATION_4) != std::u16string::npos)
        {
            context.add(Severity::Invalid, CheckIdentifier::Trainer, ResultCode::WordFilterInvalidCharacter, "\xEF\xBF\xBF", "FFFF");
            return;
        }

        if (isTrainerNameSuspicious(trainerName))
        {
            context.add(Severity::Warning, CheckIdentifier::Trainer, ResultCode::OTSuspicious);
        }

        const int originGeneration = getOriginGeneration(context);
        if (isVirtualConsole(pokemon))
        {
            verifyGameBoyTrainerName(context);
        }
        else if (static_cast<int>(trainerName.size()) > getMaxTrainerNameLength(originGeneration, pokemon.language()))
        {
            if (!isEdgeCaseLength(context, trainerName) && !isGlobalTradeSanitizedTrainer(context))
            {
                context.add(Severity::Invalid, CheckIdentifier::Trainer, ResultCode::OTLong);
            }
        }

        // PKHeX WordFilterSettings default: on for every format.
        const Enums::GameVersion originalGroup = getOriginContextGroup(context);
        WordFilterMatch filterMatch;
        if (isWordFiltered(trainerName, context.formatGroup, originalGroup, filterMatch))
        {
            context.add(Severity::Invalid, CheckIdentifier::Trainer, ResultCode::WordFilterFlaggedPattern, Utils::utf16ToUtf8(filterMatch.patternText), getWordFilterTypeName(filterMatch.type));
        }
        if (containsTooManyNumbers(trainerName, originGeneration))
        {
            context.add(Severity::Invalid, CheckIdentifier::Trainer, ResultCode::WordFilterTooManyNumbers, std::to_string(getMaxNumberCount(originGeneration)));
        }

        // The handler's name was entered in the current format, so only its filter applies.
        const std::u16string handlerName = pokemon.htName();
        if (isWordFiltered(handlerName, context.formatGroup, context.formatGroup, filterMatch))
        {
            context.add(Severity::Invalid, CheckIdentifier::Handler, ResultCode::WordFilterFlaggedPattern, Utils::utf16ToUtf8(filterMatch.patternText), getWordFilterTypeName(filterMatch.type));
        }
        if (containsTooManyNumbers(handlerName, context.formatGeneration))
        {
            context.add(Severity::Invalid, CheckIdentifier::Handler, ResultCode::WordFilterTooManyNumbers, std::to_string(getMaxNumberCount(context.formatGeneration)));
        }
    }
}
