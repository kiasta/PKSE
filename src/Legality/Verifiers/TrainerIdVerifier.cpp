// Port of PKHeX PKHeX.Core/Legality/Verifiers/TrainerIDVerifier.cs: trainer id and secret id.
#include <cstdint>

#include "Enums/LanguageID.h"
#include "Legality/RNG/MethodCXD.h"
#include "Legality/RNG/MethodH.h"
#include "Legality/Verifiers/NameRules.h"
#include "Legality/Verifiers/Verifiers.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace
    {
        constexpr uint8_t VERSION_COLOSSEUM_XD = 15;
        constexpr uint32_t ID32_MAX_SIGNED = 0x7FFFFFFF;

        void verifyColosseumXDTrainerId(const AnalysisContext &context)
        {
            // PKHeX softens the check for XD's own encounters, whose PAL copies skip the naming
            // screen's ball sequence. Colosseum and XD share one version id and PKSE cannot tell
            // their encounters apart, so the XD allowance is given to every one.
            Severity severity = Severity::Invalid;
            const uint8_t language = context.pokemon.language();
            if (language > static_cast<uint8_t>(Enums::LanguageID::English))
            {
                return;
            }
            if (language == static_cast<uint8_t>(Enums::LanguageID::English))
            {
                severity = Severity::Warning;
            }
            uint32_t nameScreenSeed = 0;
            if (!RNG::MethodCXD::tryGetSeedTrainerID(context.pokemon.tid16(), context.pokemon.sid16(), nameScreenSeed))
            {
                context.add(severity, CheckIdentifier::Trainer, ResultCode::TrainerIdNoSeed);
            }
        }

        /// PKHeX TrainerIDVerifier.IsOTIDSuspicious.
        bool isTrainerIdSuspicious(uint16_t TID16, uint16_t SID16)
        {
            return (TID16 == 12345 && SID16 == 54321) || (TID16 == 15040 && SID16 == 18831);
        }
    }

    void verifyTrainerId(const AnalysisContext &context)
    {
        if (!isPlayerOriginalTrainer(context))
        {
            return; // the encounter fixes the ids
        }
        const Pokemon::Pokemon &pokemon = context.pokemon;
        const uint16_t TID16 = pokemon.tid16();
        const uint16_t SID16 = pokemon.sid16();
        const uint8_t originVersion = pokemon.hasOriginGame() ? pokemon.originGame() : 0;
        if (getOriginContextGroup(context) == Enums::GameVersion::BDSP)
        {
            // The game rerolls a zero id, and Unity's Random.Range never yields int.MaxValue.
            const uint32_t ID32 = pokemon.id32();
            if (ID32 == 0 || ID32 == ID32_MAX_SIGNED)
            {
                context.add(Severity::Invalid, CheckIdentifier::Trainer, ResultCode::OTIdInvalid);
                return;
            }
        }
        else if (originVersion == VERSION_COLOSSEUM_XD)
        {
            verifyColosseumXDTrainerId(context);
        }
        else if (originVersion == static_cast<uint8_t>(Enums::GameVersion::RU) || originVersion == static_cast<uint8_t>(Enums::GameVersion::SA))
        {
            // An R/S egg hatched in Emerald keeps its version but takes the Emerald trainer's ids.
            const bool mayBeHatchedEgg = context.encounter.type == EncounterType::Egg || context.encounter.type == EncounterType::Unchecked;
            const Severity severity = mayBeHatchedEgg && !pokemon.isEgg() ? Severity::Warning : Severity::Invalid;
            uint32_t trainerIdSeed = 0;
            if (!RNG::MethodH::tryGetSeedTrainerID(TID16, SID16, trainerIdSeed))
            {
                context.add(severity, CheckIdentifier::Trainer, ResultCode::TrainerIdNoSeed);
            }
        }
        else if (isVirtualConsole(pokemon))
        {
            // A Game Boy trainer has only a TID.
            if (SID16 != 0)
            {
                context.add(Severity::Invalid, CheckIdentifier::Trainer, ResultCode::OTSIDZeroInvalid);
            }
            if (TID16 == 0)
            {
                context.add(Severity::Warning, CheckIdentifier::Trainer, ResultCode::OTTIDZero);
            }
            return;
        }
        else if (context.formatGeneration <= 2)
        {
            if (TID16 == 0)
            {
                context.add(Severity::Warning, CheckIdentifier::Trainer, ResultCode::OTTIDZero);
            }
            return;
        }

        if (pokemon.id32() == 0)
        {
            context.add(Severity::Warning, CheckIdentifier::Trainer, ResultCode::OTIdsZero);
        }
        else if (TID16 == SID16)
        {
            context.add(Severity::Warning, CheckIdentifier::Trainer, ResultCode::OTIdEqual);
        }
        else if (TID16 == 0)
        {
            context.add(Severity::Warning, CheckIdentifier::Trainer, ResultCode::OTTIDZero);
        }
        else if (SID16 == 0)
        {
            context.add(Severity::Warning, CheckIdentifier::Trainer, ResultCode::OTSIDZero);
        }
        else if (isTrainerIdSuspicious(TID16, SID16))
        {
            context.add(Severity::Warning, CheckIdentifier::Trainer, ResultCode::OTSuspicious);
        }
    }
}
