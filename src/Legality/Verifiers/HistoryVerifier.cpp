// Port of PKHeX PKHeX.Core/Legality/Verifiers/HistoryVerifier.cs: handling trainer history, friendship and affection.
#include <cstdint>
#include <optional>
#include <string>

#include "Enums/GameVersion.h"
#include "Legality/Encounters/EncounterSlot8GO.h"
#include "Legality/Encounters/EncounterStatic9.h"
#include "Legality/Encounters/Encounterable.h"
#include "Legality/Encounters/LegalityInfo.h"
#include "Legality/Evolutions/EvolutionHistory.h"
#include "Legality/Verifiers/GeolocationTable.h"
#include "Legality/Verifiers/HandlerRules.h"
#include "Legality/Verifiers/NameRules.h"
#include "Legality/Verifiers/Verifiers.h"
#include "Pokemon/EvolutionTable.h"
#include "Pokemon/PersonalRecord.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace
    {
        constexpr uint16_t PIKACHU_SPECIES = 25;
        constexpr uint16_t PICHU_SPECIES = 172;
        constexpr uint16_t CHATOT_SPECIES = 441;
        constexpr uint8_t SPIKY_EARED_PICHU_FORM = 1;
        constexpr uint16_t LOCATION_LETS_GO_STARTER = 28;
        constexpr uint8_t AFFECTION_MAXIMUM = 255;
        constexpr uint8_t CONTEST_AFFECTION_STEP = 20;

        uint32_t getID32(uint16_t TID16, uint16_t SID16)
        {
            return (static_cast<uint32_t>(SID16) << 16) | TID16;
        }

        /// PKHeX HistoryVerifier.GetCanOTHandle; empty when the encounter PKSE matched cannot say
        /// whether it fixes the original trainer (a Mystery Gift, a Legends: Z-A gift, a HOME gift).
        std::optional<bool> canOriginalTrainerHandle(const AnalysisContext &context)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            const int generation = getOriginGeneration(context);
            if (generation < 6)
            {
                return generation >= 3;
            }
            if (isGoHome(pokemon))
            {
                return false; // EncounterSlot8GO
            }
            if (pokemon.metLocation() == LOCATION_HOME_GIFT)
            {
                return std::nullopt;
            }
            switch (context.encounter.type)
            {
            case EncounterType::Trade:
                return false; // every Generation 6+ in-game trade is IFixedTrainer
            case EncounterType::Event:
                return std::nullopt;
            case EncounterType::Gift:
            case EncounterType::Static:
                if (context.encounter.originGroup == Enums::GameVersion::ZA)
                {
                    return std::nullopt; // EncounterGift9a fixes the trainer for some gifts
                }
                return true;
            case EncounterType::Unchecked:
                if (pokemon.isFatefulEncounter() || !isPlayerOriginalTrainer(context))
                {
                    return std::nullopt;
                }
                return true;
            default:
                return true;
            }
        }

        /// PKHeX HistoryVerifier.GetBaseFriendship(IEncounterTemplate), for the engine's template.
        std::optional<uint8_t> getTemplateBaseFriendship(const Encounters::Encounterable &encounter)
        {
            // PKHeX IFixedOTFriendship: EncounterSlot8GO and EncounterTrade8b.
            if (const Encounters::EncounterSlot8GO *pogo = Encounters::encounterAs<Encounters::EncounterSlot8GO>(encounter))
            {
                return pogo->originalTrainerFriendship();
            }
            const uint16_t speciesId = encounter.speciesId();
            if (encounter.templateType() == Encounters::EncounterTemplateType::Trade8b)
            {
                return speciesId == CHATOT_SPECIES ? 35 : 50;
            }
            using Enums::GameVersion;
            const GameVersion contextGroup = encounter.contextGroup();
            switch (contextGroup)
            {
            case GameVersion::XY:
            case GameVersion::ORAS:
                return Pokemon::getPersonalRecord(GameVersion::ORAS, speciesId, 0).baseFriendship;
            case GameVersion::SM:
            case GameVersion::USUM:
                return Pokemon::getPersonalRecord(GameVersion::USUM, speciesId, 0).baseFriendship;
            case GameVersion::GG:
                return Pokemon::getPersonalRecord(GameVersion::GG, speciesId, 0).baseFriendship;
            case GameVersion::SWSH:
            case GameVersion::PLA:
            case GameVersion::BDSP:
            case GameVersion::SV:
            case GameVersion::ZA:
                return Pokemon::getPersonalRecord(contextGroup, speciesId, encounter.formId()).baseFriendship;
            default:
                return std::nullopt;
            }
        }

        /// PKHeX HistoryVerifier.GetBaseFriendship(IEncounterTemplate); empty without a matched encounter.
        std::optional<uint8_t> getEncounterBaseFriendship(const AnalysisContext &context)
        {
            if (const Encounters::Encounterable *encounterMatch = Encounters::getEncounterMatch(context))
            {
                return getTemplateBaseFriendship(*encounterMatch);
            }
            return std::nullopt;
        }

        /// PKHeX ITrainerInfo.IsFromTrainer.
        bool isFromTrainer(const AnalysisContext &context, const ActiveTrainer &activeTrainer)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            if (getID32(activeTrainer.TID16, activeTrainer.SID16) != getID32(pokemon.tid16(), pokemon.sid16()))
            {
                return false;
            }
            if (pokemon.otName() != activeTrainer.trainerName)
            {
                return false;
            }
            if (activeTrainer.trainerGender != pokemon.otGender())
            {
                return false;
            }
            const uint8_t trainerVersion = static_cast<uint8_t>(activeTrainer.gameVersion);
            if (pokemon.isEgg())
            {
                if (getContextGroup(Enums::getGameGroup(activeTrainer.gameVersion)) != getContextGroup(context.formatGroup))
                {
                    return false;
                }
                const bool isVersionlessEgg = context.formatGroup == Enums::GameVersion::SV && pokemon.originGame() == 0;
                return trainerVersion == pokemon.originGame() || isVersionlessEgg;
            }
            if (trainerVersion == pokemon.originGame())
            {
                return true;
            }
            if (isGoLetsGo(pokemon))
            {
                return activeTrainer.gameVersion == Enums::GameVersion::GP || activeTrainer.gameVersion == Enums::GameVersion::GE;
            }
            return context.formatGeneration <= 2;
        }

        /// PKHeX TradeRestrictions.IsUntradableEncounter.
        bool isUntradableEncounter(const AnalysisContext &context)
        {
            const Encounters::Encounterable *encounterMatch = Encounters::getEncounterMatch(context);
            if (encounterMatch == nullptr)
            {
                return false;
            }
            switch (encounterMatch->templateType())
            {
            case Encounters::EncounterTemplateType::Static4:
                return encounterMatch->speciesId() == PICHU_SPECIES && encounterMatch->formId() == SPIKY_EARED_PICHU_FORM;
            case Encounters::EncounterTemplateType::Static7b:
                return encounterMatch->location() == LOCATION_LETS_GO_STARTER;
            case Encounters::EncounterTemplateType::Static9:
                {
                const Encounters::EncounterStatic9 *static9 = Encounters::encounterAs<Encounters::EncounterStatic9>(*encounterMatch);
                return static9 != nullptr && static9->isStarterBoxLegend();
            }
            default:
                return false;
            }
        }

        /// PKHeX HistoryVerifier.IsHandlerOriginalBug; empty when the encounter could be Sun/Moon's Ash
        /// Pikachu QR gift, which PKSE's encounter data cannot single out.
        std::optional<bool> isHandlerOriginalBug(const AnalysisContext &context)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            const bool mayBeGift = context.encounter.type == EncounterType::Event || context.encounter.type == EncounterType::Unchecked;
            if (getContextGroup(context.formatGroup) == Enums::GameVersion::SM && pokemon.speciesID() == PIKACHU_SPECIES && pokemon.form() != 0 && mayBeGift)
            {
                return std::nullopt;
            }
            return false;
        }

        void verifyHandlingTrainerEquals(const AnalysisContext &context, const ActiveTrainer &activeTrainer)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            if (pokemon.htName() != activeTrainer.trainerName)
            {
                context.add(Severity::Invalid, CheckIdentifier::Memory, ResultCode::TransferHandlerMismatchName);
            }
            if (pokemon.htGender() != activeTrainer.trainerGender)
            {
                context.add(Severity::Invalid, CheckIdentifier::Memory, ResultCode::TransferHandlerMismatchGender);
            }
            // The game matches only name and gender, so a different language is merely unlikely.
            if (pokemon.hasHandlerLanguage() && activeTrainer.language != 0 && pokemon.htLanguage() != activeTrainer.language)
            {
                context.add(Severity::Warning, CheckIdentifier::Memory, ResultCode::TransferHandlerMismatchLanguage);
            }
        }

        void verifyHandlerState(const AnalysisContext &context, std::optional<bool> neverOriginalTrainer)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            const uint8_t currentHandler = pokemon.currentHandler();
            const bool untraded = isUntraded(context);
            if (context.options.activeTrainer != nullptr)
            {
                const ActiveTrainer &activeTrainer = *context.options.activeTrainer;
                const uint8_t expectedHandler = isFromTrainer(context, activeTrainer) ? 0 : 1;
                if (currentHandler != expectedHandler)
                {
                    const std::optional<bool> originalBug = currentHandler == 0 ? isHandlerOriginalBug(context) : std::optional<bool>(false);
                    if (originalBug.has_value() && !*originalBug)
                    {
                        context.add(Severity::Invalid, CheckIdentifier::Memory, ResultCode::TransferCurrentHandlerInvalid);
                        if (untraded)
                        {
                            return;
                        }
                    }
                }
                if (currentHandler == 1)
                {
                    verifyHandlingTrainerEquals(context, activeTrainer);
                }
            }

            if (currentHandler != 1)
            {
                const Enums::GameVersion encounterContext = getEncounterContextGroup(context);
                const bool contextChanged = encounterContext != Enums::GameVersion::Invalid && encounterContext != getContextGroup(context.formatGroup);
                if (contextChanged || neverOriginalTrainer.value_or(false))
                {
                    context.add(Severity::Invalid, CheckIdentifier::Memory, ResultCode::TransferHandlerFlagRequired);
                }
            }
            if (!untraded && isUntradableEncounter(context))
            {
                context.add(Severity::Invalid, CheckIdentifier::Memory, ResultCode::TransferCurrentHandlerInvalid);
            }
        }

        /// PKHeX EggStateLegality.IsValidHTEgg.
        bool isValidHandlingTrainerEgg(const AnalysisContext &context)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            if (context.formatGroup == Enums::GameVersion::BDSP && pokemon.metLocation() == LOCATION_LINK_TRADE_6_NPC)
            {
                return pokemon.htFriendship() == Pokemon::getPersonalRecord(Enums::GameVersion::BDSP, pokemon.speciesID(), 0).baseFriendship;
            }
            return context.formatGroup == Enums::GameVersion::SV && pokemon.metLocation() == LOCATION_LINK_TRADE_6 && pokemon.htLanguage() != 0;
        }

        /// PKHeX IGeoTrack.GetValidity, reported as HistoryVerifier.VerifyGeoLocationData does.
        void verifyGeolocation(const AnalysisContext &context, bool untraded)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            bool seenEmpty = false;
            for (int slotIndex = 0; slotIndex < Pokemon::Pokemon::GEOLOCATION_SLOT_COUNT; ++slotIndex)
            {
                const uint8_t country = pokemon.geolocationCountry(slotIndex);
                const uint8_t region = pokemon.geolocationRegion(slotIndex);
                const std::string memoryNumber = std::to_string(slotIndex + 1);
                if (country == 0)
                {
                    if (region == 0)
                    {
                        seenEmpty = true;
                        continue;
                    }
                    context.add(Severity::Invalid, CheckIdentifier::Memory, ResultCode::GeographyNoCountry, memoryNumber);
                    break;
                }
                if (!isGeolocationRegionInCountry(country, region))
                {
                    context.add(Severity::Invalid, CheckIdentifier::Memory, ResultCode::GeographyNoRegion, memoryNumber);
                    break;
                }
                if (seenEmpty)
                {
                    context.add(Severity::Invalid, CheckIdentifier::Memory, ResultCode::GeographyBadOrder, memoryNumber);
                    break;
                }
            }
            if (pokemon.geolocationCountry(0) != 0 && untraded)
            {
                context.add(Severity::Invalid, CheckIdentifier::Memory, ResultCode::GeographyNoCountryHandler);
            }
        }

        void verifyTradeState(const AnalysisContext &context)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            const bool untraded = isUntraded(context);
            if (pokemon.hasGeolocation())
            {
                verifyGeolocation(context, untraded);
            }
            // A Virtual Console transfer is given the first geolocation.
            if (isVirtualConsole(pokemon) && pokemon.hasGeolocation() && context.formatGeneration == 7 && pokemon.geolocationCountry(0) == 0)
            {
                context.add(Severity::Invalid, CheckIdentifier::Memory, ResultCode::GeographyMemoryMissing);
            }

            if (!untraded)
            {
                if (pokemon.isEgg() && !isValidHandlingTrainerEgg(context))
                {
                    context.add(Severity::Invalid, CheckIdentifier::Memory, ResultCode::MemoryArgumentBadHandler);
                }
                return;
            }

            if (pokemon.currentHandler() != 0)
            {
                context.add(Severity::Invalid, CheckIdentifier::Memory, ResultCode::MemoryHandlerFlagInvalid);
            }
            else if (pokemon.htFriendship() != 0)
            {
                context.add(Severity::Invalid, CheckIdentifier::Memory, ResultCode::MemoryStatFriendshipHandler0);
            }
            else if (pokemon.hasHandlerAffection() && pokemon.htAffection() != 0)
            {
                context.add(Severity::Invalid, CheckIdentifier::Memory, ResultCode::MemoryStatAffectionHandler0);
            }
        }

        /// ORAS contests add 20 affection to the original trainer rather than the current handler.
        bool isInvalidContestAffection(uint8_t originalTrainerAffection)
        {
            return originalTrainerAffection != AFFECTION_MAXIMUM && originalTrainerAffection % CONTEST_AFFECTION_STEP != 0;
        }

        void verifyOriginalTrainerAffection(const AnalysisContext &context, std::optional<bool> neverOriginalTrainer, int originGeneration)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            if (!pokemon.hasHandlerAffection())
            {
                return;
            }
            const uint8_t affection = pokemon.otAffection();
            if (originGeneration < 6)
            {
                // Virtual Console Pokemon never visit Generation 6, where the contest affection applies.
                if ((originGeneration <= 2 && affection != 0) || isInvalidContestAffection(affection))
                {
                    context.add(Severity::Invalid, CheckIdentifier::Memory, ResultCode::MemoryStatAffectionOT0);
                }
                return;
            }
            if (!neverOriginalTrainer.value_or(false))
            {
                return;
            }
            if (originGeneration == 6)
            {
                const uint8_t originVersion = pokemon.originGame();
                const bool isFromXY = originVersion == static_cast<uint8_t>(Enums::GameVersion::X) || originVersion == static_cast<uint8_t>(Enums::GameVersion::Y);
                if (isUntraded(context) && isFromXY)
                {
                    if (affection != 0)
                    {
                        context.add(Severity::Invalid, CheckIdentifier::Memory, ResultCode::MemoryStatAffectionOT0);
                    }
                }
                else if (isInvalidContestAffection(affection))
                {
                    context.add(Severity::Invalid, CheckIdentifier::Memory, ResultCode::MemoryStatAffectionOT0);
                }
                return;
            }
            if (affection != 0)
            {
                context.add(Severity::Invalid, CheckIdentifier::Memory, ResultCode::MemoryStatAffectionOT0);
            }
        }

        /// HistoryVerifier.VerifyOTFriendshipVC12: some evolution in the Pokemon's chain must have the
        /// friendship it arrived with, read from Ultra Sun/Ultra Moon's table as Poke Transporter does.
        void verifyVirtualConsoleFriendship(const AnalysisContext &context)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            // PKHeX walks EvoChainsAllGens.Gen7: the stages the Pokemon could have been in Gen 7.
            uint8_t expectedFriendship = 0;
            for (const Evolutions::EvolutionCriteria &stage : context.evolutionHistory().getChain(Enums::GameVersion::SM))
            {
                const Pokemon::PersonalRecord &record = Pokemon::getPersonalRecord(Enums::GameVersion::USUM, stage.speciesId, stage.formId);
                if (record.hp == 0)
                {
                    continue;
                }
                expectedFriendship = record.baseFriendship;
                if (expectedFriendship == pokemon.otFriendship())
                {
                    return;
                }
            }
            context.add(Severity::Invalid, CheckIdentifier::Memory, ResultCode::MemoryStatFriendshipOTBaseEvent, std::to_string(expectedFriendship));
        }

        void verifyOriginalTrainerFriendship(const AnalysisContext &context, std::optional<bool> neverOriginalTrainer, int originGeneration)
        {
            if (originGeneration == 0)
            {
                return;
            }
            if (originGeneration <= 2)
            {
                verifyVirtualConsoleFriendship(context);
                return;
            }
            if (!neverOriginalTrainer.value_or(false))
            {
                return;
            }
            // The original trainer never held it, so the friendship it was given cannot have changed.
            const std::optional<uint8_t> expectedFriendship = getEncounterBaseFriendship(context);
            if (expectedFriendship.has_value() && context.pokemon.otFriendship() != *expectedFriendship)
            {
                context.add(Severity::Invalid, CheckIdentifier::Memory, ResultCode::MemoryStatFriendshipOTBaseEvent, std::to_string(*expectedFriendship));
            }
        }

        void verifyHandlingTrainerMisc(const AnalysisContext &context)
        {
            const uint8_t handlingTrainerGender = context.pokemon.htGender();
            if (handlingTrainerGender > 1 || (isUntraded(context) && handlingTrainerGender != 0))
            {
                context.add(Severity::Invalid, CheckIdentifier::Memory, ResultCode::MemoryHandlerGender, std::to_string(handlingTrainerGender));
            }
        }
    }

    void verifyHistory(const AnalysisContext &context)
    {
        const std::optional<bool> canHandle = canOriginalTrainerHandle(context);
        const std::optional<bool> neverOriginalTrainer = canHandle.has_value() ? std::optional<bool>(!*canHandle) : std::nullopt;
        const int originGeneration = getOriginGeneration(context);
        verifyHandlerState(context, neverOriginalTrainer);
        verifyTradeState(context);
        verifyOriginalTrainerAffection(context, neverOriginalTrainer, originGeneration);
        verifyOriginalTrainerFriendship(context, neverOriginalTrainer, originGeneration);
        verifyHandlingTrainerMisc(context);
    }
}
