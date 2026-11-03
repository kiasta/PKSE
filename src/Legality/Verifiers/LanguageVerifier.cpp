// Port of PKHeX PKHeX.Core/Legality/Verifiers/LanguageVerifier.cs: language id against the origin and format.
#include <string>

#include "Enums/LanguageID.h"
#include "Legality/Verifiers/NameRules.h"
#include "Legality/Verifiers/Verifiers.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace
    {
        constexpr uint8_t LANGUAGE_JAPANESE = static_cast<uint8_t>(Enums::LanguageID::Japanese);
        constexpr uint8_t LANGUAGE_KOREAN = static_cast<uint8_t>(Enums::LanguageID::Korean);

        /// PKHeX's finding names a language by its abbreviation (LanguageID's display form).
        std::string describeLanguage(uint8_t language)
        {
            return std::string(Enums::getLanguageName(language)) + " (" + std::to_string(language) + ")";
        }

        /// PKHeX LanguageVerifier.CheckGTS for a Gen 4 format: Korean and international Gen 4
        /// games trade only through the GTS, which refuses eggs and sanitizes Korean trainer names.
        void verifyGlobalTradeStation(const AnalysisContext &context)
        {
            const ActiveTrainer *activeTrainer = context.options.activeTrainer;
            if (context.formatGeneration != 4 || activeTrainer == nullptr)
            {
                return;
            }
            if (Enums::getVersionGeneration(static_cast<uint8_t>(activeTrainer->gameVersion)) != 4 && Enums::getVersionGeneration(Enums::getGroupRepVersion(activeTrainer->gameVersion)) != 4)
            {
                return;
            }
            const uint8_t language = context.pokemon.language();
            const bool isKoreanTrainer = activeTrainer->language == LANGUAGE_KOREAN;
            const bool isKoreanPokemon = language == LANGUAGE_KOREAN;
            if (isKoreanTrainer == isKoreanPokemon)
            {
                return;
            }
            // PKHeX skips an EncounterTrade4PID with IsLanguageSwap, and flags an event carrying the
            // Classic Ribbon (IRibbonSetEvent4); neither encounter property is known yet.
            if (context.encounter.type == EncounterType::Trade || context.encounter.type == EncounterType::Event)
            {
                return;
            }
            if (context.pokemon.isEgg())
            {
                context.add(Severity::Invalid, CheckIdentifier::Language, ResultCode::GTSDisallowedTradedEgg);
            }
            else if (isKoreanPokemon && !isGlobalTradeSanitizedTrainer(context))
            {
                context.add(Severity::Invalid, CheckIdentifier::Language, ResultCode::GTSTrainerSanitizedExpected);
            }
        }
    }

    void verifyLanguage(const AnalysisContext &context)
    {
        const Pokemon::Pokemon &pokemon = context.pokemon;
        const uint8_t language = pokemon.language();
        if (!isValidLanguageId(context))
        {
            context.add(Severity::Invalid, CheckIdentifier::Language, ResultCode::OTLanguageShouldBeAtMost, describeLanguage(getMaxLanguageId(context)), describeLanguage(language));
            return;
        }

        if (context.formatGeneration >= 4)
        {
            verifyGlobalTradeStation(context);
        }

        if (getOriginGeneration(context) <= 2)
        {
            const uint8_t originVersion = pokemon.hasOriginGame() ? pokemon.originGame() : 0;
            const bool isGoldSilver = originVersion == static_cast<uint8_t>(Enums::GameVersion::GD) || originVersion == static_cast<uint8_t>(Enums::GameVersion::SI);
            // Korean Crystal and a Korean Gen 1 Virtual Console release never existed.
            if (language == LANGUAGE_KOREAN && !isGoldSilver)
            {
                context.add(Severity::Invalid, CheckIdentifier::Language, ResultCode::OTLanguageCannotPlayOnVersion, std::to_string(originVersion));
            }
            // The Japanese Blue Virtual Console release is locked to Japanese.
            if (language != LANGUAGE_JAPANESE && originVersion == static_cast<uint8_t>(Enums::GameVersion::BU))
            {
                context.add(Severity::Invalid, CheckIdentifier::Language, ResultCode::OTLanguageCannotPlayOnVersion, std::to_string(originVersion));
            }
        }
    }
}
