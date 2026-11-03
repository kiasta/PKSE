// Port of PKHeX PKHeX.Core/Legality/Verifiers/PIDVerifier.cs: PID, encryption constant and shiny rules.
#include "Legality/Verifiers/Verifiers.h"

#include <cstdint>

#include "Enums/GameVersion.h"
#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterRules8.h"
#include "Legality/Encounters/EncounterSlot5.h"
#include "Legality/Encounters/EncounterStatic5.h"
#include "Legality/Encounters/EncounterStatic8Nest.h"
#include "Legality/Encounters/EncounterTrade5BW.h"
#include "Legality/Encounters/Encounterable.h"
#include "Legality/Encounters/LegalityInfo.h"
#include "Legality/Encounters/MysteryGiftPCD.h"
#include "Legality/Encounters/MysteryGiftPGF.h"
#include "Legality/Encounters/MysteryGiftWC7.h"
#include "Legality/Verifiers/NameRules.h"
#include "Pokemon/Pokemon.h"
#include "Legality/RNG/ShinyUtil.h"

namespace Legality
{
    namespace
    {
        constexpr uint16_t WURMPLE_SPECIES = 265;
        constexpr uint16_t SILCOON_SPECIES = 266;
        constexpr uint16_t NIDORAN_FEMALE_SPECIES = 29;
        constexpr uint16_t NIDORAN_MALE_SPECIES = 32;
        constexpr uint16_t VOLBEAT_SPECIES = 313;
        constexpr uint16_t ILLUMISE_SPECIES = 314;
        constexpr uint8_t EMERALD_VERSION = 3;
        constexpr uint32_t GENERATION_SIX_SHINY_FLIP = 0x80000000;
        constexpr uint32_t HOME_SHINY_FLIP = 0x10000000;

        bool isEventFrom(const AnalysisContext &context, Enums::GameVersion group)
        {
            return context.encounter.type == EncounterType::Event && context.encounter.originGroup == group;
        }

        /// PKHeX WurmpleUtil.GetWurmpleEvoVal: 0 evolves into Silcoon, 1 into Cascoon.
        uint32_t getWurmpleEvolution(uint32_t encryptionConstant)
        {
            return (encryptionConstant >> 16) % 10 / 5;
        }

        bool isGenderSpeciesDetermination(uint16_t speciesId)
        {
            return speciesId == NIDORAN_FEMALE_SPECIES || speciesId == NIDORAN_MALE_SPECIES || speciesId == VOLBEAT_SPECIES || speciesId == ILLUMISE_SPECIES;
        }

        /// PKHeX Daycare3.IsValidProcPID: the day care never stores a zero PID (Emerald) or a zero lower half (the others).
        bool isValidDaycareThreePID(uint32_t PID, uint8_t originVersion)
        {
            if (originVersion != EMERALD_VERSION)
            {
                return (PID & 0xFFFF) != 0;
            }
            return PID != 0;
        }

        /// PKHeX PIDVerifier.IsEggBitRequiredMale34: without Ditto a male inherits level-up moves, and
        /// then must match the correlation too. Egg moves (passed by the male) prove nothing.
        bool isEggBitRequiredMale(const AnalysisContext &context)
        {
            if (context.legalityInfo == nullptr)
            {
                return false; // a hand-built context has no move results
            }
            for (const LearnSource::MoveResult &move : context.legalityInfo->moves)
            {
                if (move.info.method == LearnSource::LearnMethod::InheritLevelUp)
                {
                    return true;
                }
            }
            return false;
        }

        /// PKHeX PIDVerifier.VerifyEggGender8000.
        void verifyEggGenderBit(const AnalysisContext &context)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            const uint8_t gender = pokemon.gender();
            const bool isValidSpeciesBit = gender != ((pokemon.encryptionConstant() & 0x8000) >> 15);
            if (isValidSpeciesBit)
            {
                return;
            }
            if (gender == 1 || isEggBitRequiredMale(context))
            {
                context.add(Severity::Invalid, CheckIdentifier::EncryptionConstant, ResultCode::PIDGenderMismatch);
            }
        }

        /// PKHeX PIDVerifier.VerifyEggPID, for the day-care eggs of Gens 3 and 4 (EncounterEgg3 / EncounterEgg4).
        void verifyEggPID(const AnalysisContext &context, const Encounters::Encounterable &encounter)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            const Encounters::EncounterTemplateType templateType = encounter.templateType();
            if (templateType == Encounters::EncounterTemplateType::Egg4)
            {
                // A zero PID reads as "no egg waiting", but the Masuda Method's re-roll can still land on it.
                if (pokemon.encryptionConstant() == 0)
                {
                    context.add(Severity::Warning, CheckIdentifier::EncryptionConstant, ResultCode::PIDEncryptZero);
                }
            }
            else if (templateType == Encounters::EncounterTemplateType::Egg3)
            {
                if (!isValidDaycareThreePID(pokemon.encryptionConstant(), pokemon.originGame()))
                {
                    context.add(Severity::Invalid, CheckIdentifier::EncryptionConstant, ResultCode::PIDEncryptZero);
                }
            }
            else
            {
                return;
            }
            if (isGenderSpeciesDetermination(encounter.speciesId()))
            {
                verifyEggGenderBit(context);
            }
        }

        /// PKHeX PIDVerifier.VerifyFutureOriginEC.
        void verifyFutureOriginEncryptionConstant(const AnalysisContext &context)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            if (pokemon.pid() == pokemon.encryptionConstant())
            {
                // Legends: Arceus and Brilliant Diamond/Shining Pearl gifts with an equal fixed EC and
                // PID (WA8/WB8 IsEquivalentFixedECPID) are unknown without the gift's data.
                if (isEventFrom(context, Enums::GameVersion::PLA) || isEventFrom(context, Enums::GameVersion::BDSP))
                {
                    return;
                }
                context.add(Severity::Invalid, CheckIdentifier::EncryptionConstant, ResultCode::PIDEqualsEncryptionConstant);
                return;
            }
            if ((pokemon.pid() ^ GENERATION_SIX_SHINY_FLIP) == pokemon.encryptionConstant())
            {
                const uint32_t shinyXor = RNG::ShinyUtil::getShinyXor(pokemon.pid(), pokemon.id32());
                if (shinyXor >> 3 == 1)
                {
                    context.add(Severity::Warning, CheckIdentifier::EncryptionConstant, ResultCode::TransferEncryptGen6Xor);
                }
            }
        }

        /// PKHeX PIDVerifier.VerifyTransferEC with PK5.GetTransferPID: Poke Transfer copies the PID into
        /// the encryption constant and flips the top bit where Gen 6's wider shiny odds would make it shiny.
        void verifyTransferEncryptionConstant(const AnalysisContext &context)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            const uint32_t encryptionConstant = pokemon.encryptionConstant();
            const uint32_t combined = encryptionConstant ^ pokemon.id32();
            const uint32_t shinyXor = combined ^ (combined >> 16);
            const bool isBitFlipped = (shinyXor & 0xFFF8u) == 8;
            const uint32_t expectedPID = isBitFlipped ? (encryptionConstant ^ GENERATION_SIX_SHINY_FLIP) : encryptionConstant;
            if (pokemon.pid() == expectedPID)
            {
                return;
            }
            context.add(Severity::Invalid, CheckIdentifier::EncryptionConstant, isBitFlipped ? ResultCode::TransferEncryptGen6BitFlip : ResultCode::TransferEncryptGen6Equals);
        }

        /// PKHeX PIDVerifier.VerifyTransferECHOME with HomeConverter.GetTransferPID.
        void verifyHomeTransferEncryptionConstant(const AnalysisContext &context)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            // A Sword/Shield gift (WC8) is judged like any other modern origin; PKSE's events carry no
            // card type, so a Sword/Shield event stands in for it.
            if (isEventFrom(context, Enums::GameVersion::SWSH))
            {
                verifyFutureOriginEncryptionConstant(context);
                return;
            }
            const uint32_t encryptionConstant = pokemon.encryptionConstant();
            const uint32_t shinyXor = RNG::ShinyUtil::getShinyXor(encryptionConstant, pokemon.id32());
            const bool isBitFlipped = shinyXor >= 8 && shinyXor <= 15;
            const uint32_t expectedPID = isBitFlipped ? (encryptionConstant ^ HOME_SHINY_FLIP) : encryptionConstant;
            if (pokemon.pid() == expectedPID)
            {
                return;
            }
            context.add(Severity::Invalid, CheckIdentifier::EncryptionConstant, isBitFlipped ? ResultCode::TransferEncryptGen6BitFlip : ResultCode::TransferEncryptGen6Equals);
        }

        /// PKHeX PIDVerifier.VerifyEC.
        void verifyEncryptionConstant(const AnalysisContext &context)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            // HOME gifts (WC8 IsHOMEGift) carry a zero EC; a Sword/Shield event may be one.
            if (pokemon.encryptionConstant() == 0 && !isEventFrom(context, Enums::GameVersion::SWSH))
            {
                context.add(Severity::Warning, CheckIdentifier::EncryptionConstant, ResultCode::PIDEncryptZero);
            }
            if (isVirtualConsoleGenerationThree(context))
            {
                verifyHomeTransferEncryptionConstant(context);
                return;
            }
            const int originGeneration = getOriginGeneration(context);
            if (originGeneration >= 3 && originGeneration <= 5)
            {
                verifyTransferEncryptionConstant(context);
                return;
            }
            verifyFutureOriginEncryptionConstant(context);
        }

        /// PKHeX MonochromeRNG.IsValidForcedRandomGender.
        bool isValidForcedRandomGender(uint32_t PID, uint8_t gender)
        {
            const uint32_t genderValue = PID & 0xFF;
            if (gender == 0)
            {
                return genderValue != 254 && genderValue != 255;
            }
            return genderValue != 0;
        }

        /// PKHeX MonochromeRNG.GetBitXor: Black/White's wild PIDs tie the PID's top and bottom bits to the trainer ids.
        uint32_t getMonochromeBitXor(const Pokemon::Pokemon &pokemon)
        {
            const uint32_t PID = pokemon.encryptionConstant();
            const uint32_t trainerBitXor = (static_cast<uint32_t>(pokemon.tid16()) ^ pokemon.sid16()) & 1;
            return trainerBitXor ^ (PID & 1) ^ (PID >> 31);
        }
    }

    namespace
    {
        /// PKHeX PIDVerifier.VerifyG5PID_IDCorrelation.
        void verifyGeneration5Correlation(const AnalysisContext &context)
        {
            if (getMonochromeBitXor(context.pokemon) != 0)
            {
                context.add(Severity::Invalid, CheckIdentifier::PID, ResultCode::PIDTypeMismatch);
            }
        }

        /// PKHeX PIDVerifier.VerifyShinyEgg.
        void verifyShinyEgg(const AnalysisContext &context, const Encounters::Encounterable &encounter)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            if (Encounters::isShinyValid(encounter.shiny(), pokemon))
            {
                return;
            }
            // A hatched egg may have changed hands, which changes its shininess; Gen 4-7 record the trade.
            if (!pokemon.isEgg())
            {
                const uint8_t generation = encounter.generation();
                if (generation == 3 || (generation >= 4 && generation <= 7 && isTradedEgg(context)))
                {
                    return;
                }
            }
            context.add(Severity::Invalid, CheckIdentifier::Shiny, ResultCode::EncounterStaticPIDShiny);
        }

        /// PKHeX PIDVerifier.VerifyShiny, against the encounter engine's template.
        void verifyShinyTemplate(const AnalysisContext &context, const Encounters::Encounterable &encounter)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            if (encounter.isEgg())
            {
                verifyShinyEgg(context, encounter);
                return;
            }
            if (!Encounters::isShinyValid(encounter.shiny(), pokemon))
            {
                context.add(Severity::Invalid, CheckIdentifier::Shiny, ResultCode::EncounterStaticPIDShiny);
            }
            const Encounters::EncounterStatic5 *static5 = Encounters::encounterAs<Encounters::EncounterStatic5>(encounter);
            const bool isForcedGenderTemplate = encounter.generation() == 5 && Encounters::encounterAs<Encounters::EncounterTrade5BW>(encounter) == nullptr && Encounters::encounterAs<Encounters::MysteryGiftPGF>(encounter) == nullptr;
            const uint8_t fixedGender = encounter.fixedGender();
            if (isForcedGenderTemplate && (fixedGender == 0 || fixedGender == 1))
            {
                if (!isValidForcedRandomGender(pokemon.encryptionConstant(), fixedGender))
                {
                    context.add(Severity::Invalid, CheckIdentifier::PID, ResultCode::EncounterConditionBadRNGFrame);
                }
                if (static5 != nullptr && static5->isWildCorrelationPID())
                {
                    verifyGeneration5Correlation(context);
                }
                return;
            }
            if (static5 != nullptr)
            {
                if (static5->isWildCorrelationPID())
                {
                    verifyGeneration5Correlation(context);
                }
                return;
            }
            if (const Encounters::EncounterSlot5 *slot5 = Encounters::encounterAs<Encounters::EncounterSlot5>(encounter))
            {
                if (slot5->isHiddenGrotto())
                {
                    if (Encounters::isShiny(pokemon))
                    {
                        context.add(Severity::Invalid, CheckIdentifier::Shiny, ResultCode::Gen5PIDShinyGrotto);
                    }
                    return;
                }
                verifyGeneration5Correlation(context);
                return;
            }
            if (const Encounters::MysteryGiftPCD *card = Encounters::encounterAs<Encounters::MysteryGiftPCD>(encounter))
            {
                if (!card->isRandomPID() && pokemon.encryptionConstant() != card->giftData().PID)
                {
                    context.add(Severity::Invalid, CheckIdentifier::Shiny, ResultCode::EncounterGiftPIDMismatch);
                }
                return;
            }
            if (const Encounters::MysteryGiftWC7 *card = Encounters::encounterAs<Encounters::MysteryGiftWC7>(encounter))
            {
                if (card->isAshGreninja() && Encounters::isShiny(pokemon))
                {
                    context.add(Severity::Invalid, CheckIdentifier::Shiny, ResultCode::EncounterGiftShinyMismatch);
                }
                return;
            }
            if (Encounters::encounterAs<Encounters::EncounterStatic8U>(encounter) != nullptr && !Encounters::EncounterStatic8U::isShinyXorValid(RNG::ShinyUtil::getShinyXor(pokemon.pid(), pokemon.id32())))
            {
                context.add(Severity::Invalid, CheckIdentifier::Shiny, ResultCode::EncounterStaticPIDShiny);
            }
        }
    }

    void verifyPID(const AnalysisContext &context)
    {
        const Pokemon::Pokemon &pokemon = context.pokemon;
        if (context.formatGeneration >= 6)
        {
            verifyEncryptionConstant(context);
        }

        const Encounters::Encounterable *encounterMatch = Encounters::getEncounterMatch(context);
        // Tandemaus and Dunsparce only add PKHeX's Valid hint about the evolution, which PKSE does not report.
        if (encounterMatch != nullptr && encounterMatch->speciesId() == WURMPLE_SPECIES && pokemon.speciesID() != WURMPLE_SPECIES)
        {
            const int evolutionGroupIndex = pokemon.speciesID() - SILCOON_SPECIES;
            const bool isWurmpleEvolution = evolutionGroupIndex >= 0 && evolutionGroupIndex <= 3;
            if (!isWurmpleEvolution || getWurmpleEvolution(pokemon.encryptionConstant()) != static_cast<uint32_t>(evolutionGroupIndex >> 1))
            {
                context.add(Severity::Invalid, CheckIdentifier::EncryptionConstant, ResultCode::PIDEncryptWurmple);
            }
        }

        if (pokemon.pid() == 0)
        {
            context.add(Severity::Warning, CheckIdentifier::PID, ResultCode::PIDZero);
        }
        constexpr uint8_t natureCount = 25;
        if (pokemon.nature() >= natureCount)
        {
            context.add(Severity::Invalid, CheckIdentifier::PID, ResultCode::PIDNatureMismatch);
        }
        if (encounterMatch != nullptr)
        {
            verifyEggPID(context, *encounterMatch);
            verifyShinyTemplate(context, *encounterMatch);
        }
    }
}
