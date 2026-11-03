// Port of PKHeX PKHeX.Core/Legality/Verifiers/Misc/MiscVerifierPK9.cs and MiscVerifierPA9.cs: the
// Scarlet/Violet and Legends: Z-A formats.
#include "Legality/Verifiers/MiscVerifier.h"

#include <cstdint>

#include "Enums/GameVersion.h"
#include "Names/MoveNames.h"
#include "Pokemon/FormInfo.h"
#include "Pokemon/MoveRecordTable.h"
#include "Pokemon/PersonalInfo9SV.h"
#include "Pokemon/PersonalInfoTable.h"
#include "Pokemon/PersonalRecord.h"
#include "Pokemon/Pokemon9LZA.h"
#include "Pokemon/Pokemon9SV.h"
#include "Pokemon/PokemonTypes.h"

namespace Legality
{
    namespace
    {
        constexpr uint16_t DEOXYS_SPECIES = 386;
        constexpr uint16_t ARCEUS_SPECIES = 493;
        constexpr uint16_t BURMY_SPECIES = 412;
        constexpr uint16_t FURFROU_SPECIES = 676;
        constexpr uint16_t ORICORIO_SPECIES = 741;
        constexpr uint16_t OGERPON_SPECIES = 1017;
        constexpr uint16_t TERAPAGOS_SPECIES = 1024;

        constexpr uint8_t POKEMON_GO_VERSION = 34;
        constexpr uint8_t ALPHA_TRACKED_SCALAR = 255;

        /// PKHeX TeraTypeUtil: the "no override" marker, the highest real type, and Stellar.
        constexpr uint8_t TERA_TYPE_OVERRIDE_NONE = 19;
        constexpr uint8_t TERA_TYPE_MAXIMUM = 17;
        constexpr uint8_t TERA_TYPE_STELLAR = 99;

        /// PKHeX TeraTypeUtil.IsValid.
        bool isTeraTypeValid(uint8_t teraType)
        {
            return teraType <= TERA_TYPE_MAXIMUM || teraType == TERA_TYPE_OVERRIDE_NONE || teraType == TERA_TYPE_STELLAR;
        }

        /// PKHeX TeraTypeUtil.IsValidOgerpon: each mask fixes the Tera Type.
        bool isOgerponTeraTypeValid(uint8_t teraType, uint8_t formId)
        {
            switch (formId & 3)
            {
                case 0:
                    return teraType == Pokemon::TYPE_GRASS || teraType == TERA_TYPE_OVERRIDE_NONE;
                case 1:
                    return teraType == Pokemon::TYPE_WATER;
                case 2:
                    return teraType == Pokemon::TYPE_FIRE;
                default:
                    return teraType == Pokemon::TYPE_ROCK;
            }
        }

        /// The second type as PKHeX stores it: a single-typed species repeats its first type.
        uint8_t getSecondType(const Pokemon::PersonalRecord &personalRecord)
        {
            return personalRecord.type2 == Pokemon::PERSONAL_TYPE_NONE ? personalRecord.type1 : personalRecord.type2;
        }

        bool isPresentInScarletViolet(uint16_t speciesId, uint8_t formId)
        {
            return (Pokemon::getPersonalInfo(speciesId, formId).presence & Pokemon::PERSONAL_GAME_SV) != 0;
        }

        /// PKHeX Tera9RNG.IsMatchType: a bred Tera Type is one of the species' own types.
        bool isTeraTypePersonalMatch(uint16_t speciesId, uint8_t formId, uint8_t teraType)
        {
            const Pokemon::PersonalRecord &personalRecord = Pokemon::getPersonalInfo9SV(speciesId, formId);
            return teraType == personalRecord.type1 || teraType == getSecondType(personalRecord);
        }

        /// PKHeX Tera9RNG.IsMatchTeraTypeImport: HOME picks the first type unless it is Normal.
        bool isTeraTypeImportMatch(uint16_t speciesId, uint8_t formId, uint8_t teraType)
        {
            const Pokemon::PersonalRecord &personalRecord = Pokemon::getPersonalInfo9SV(speciesId, formId);
            const uint8_t importType = personalRecord.type1 != Pokemon::TYPE_NORMAL ? personalRecord.type1 : getSecondType(personalRecord);
            return teraType == importType;
        }

        /// PKHeX Tera9RNG.IsMatchTeraTypePersonalAnyForm(Import): any form present in Scarlet/Violet matches.
        bool isTeraTypeAnyFormMatch(uint16_t speciesId, uint8_t teraType, bool isImport)
        {
            const uint8_t formCount = Pokemon::getPersonalInfo9SV(speciesId, 0).formCount;
            for (uint8_t formId = 0; formId < formCount || formId == 0; ++formId)
            {
                if (!isPresentInScarletViolet(speciesId, formId))
                {
                    continue;
                }
                const bool isMatch = isImport ? isTeraTypeImportMatch(speciesId, formId, teraType) : isTeraTypePersonalMatch(speciesId, formId, teraType);
                if (isMatch)
                {
                    return true;
                }
            }
            return false;
        }

        /// PKHeX MiscVerifierPK9.GetTeraImportMatch, reduced to whether any stage matches.
        bool isTeraImportMatch(std::span<const Evolutions::EvolutionCriteria> stages, uint8_t teraType)
        {
            if (stages.empty() || stages[0].speciesId == ARCEUS_SPECIES)
            {
                return teraType == Pokemon::TYPE_NORMAL;
            }
            for (size_t stageIndex = stages.size(); stageIndex-- > 0;)
            {
                const Evolutions::EvolutionCriteria &stage = stages[stageIndex];
                // PKHeX FormInfo.IsFormChangeable with Scarlet/Violet as the current game, where Zygarde and
                // Deerling/Sawsbuck change freely, which is the whole FormChange list PKSE ports.
                const bool isMatch = Pokemon::isFormChangeable(stage.speciesId) ? isTeraTypeAnyFormMatch(stage.speciesId, teraType, true) : isTeraTypeImportMatch(stage.speciesId, stage.formId, teraType);
                if (isMatch)
                {
                    return true;
                }
            }
            return false;
        }

        /// PKHeX MiscVerifierPK9.VerifyTechRecordFlags.
        void verifyMachineFlags(const AnalysisContext &context, const Pokemon::Pokemon9SV &pokemon)
        {
            const std::span<const Evolutions::EvolutionCriteria> stages = context.evolutionHistory().getChain(Enums::GameVersion::SV);
            const int stageCount = static_cast<int>(stages.size());
            for (int machineIndex = 0; machineIndex < Pokemon::TM_COUNT_9SV; ++machineIndex)
            {
                if (!pokemon.isTMFlagSet(machineIndex))
                {
                    continue;
                }
                if (stageCount != 0)
                {
                    const Evolutions::EvolutionCriteria &head = stages[0];
                    if (Pokemon::isTMPermitted9SV(head.speciesId, head.formId, machineIndex))
                    {
                        continue;
                    }
                    bool isLearnable = false;
                    if (head.speciesId == DEOXYS_SPECIES)
                    {
                        // Deoxys' permissions differ by form.
                        const uint8_t formCount = Pokemon::getPersonalInfo9SV(head.speciesId, 0).formCount;
                        for (uint8_t formId = 1; formId < formCount && !isLearnable; ++formId)
                        {
                            isLearnable = Pokemon::isTMPermitted9SV(head.speciesId, formId, machineIndex);
                        }
                    }
                    else
                    {
                        // Zoroark cannot learn Encore by TM but Zorua can.
                        for (int stageIndex = 1; stageIndex < stageCount && !isLearnable; ++stageIndex)
                        {
                            isLearnable = Pokemon::isTMPermitted9SV(stages[stageIndex].speciesId, stages[stageIndex].formId, machineIndex);
                        }
                    }
                    if (isLearnable)
                    {
                        continue;
                    }
                }
                context.add(Severity::Invalid, CheckIdentifier::Misc, ResultCode::MoveTechRecordFlagMissing, Names::getMoveName(Pokemon::TM_MOVES_9SV[machineIndex]));
            }
        }

        /// PKHeX MiscVerifierPK9.VerifyTeraType.
        void verifyTeraType(const AnalysisContext &context, const Pokemon::Pokemon9SV &pokemon)
        {
            bool isValid = true;
            if (pokemon.isEgg())
            {
                isValid = pokemon.teraTypeOverride() == TERA_TYPE_OVERRIDE_NONE;
            }
            else if (pokemon.speciesID() == TERAPAGOS_SPECIES)
            {
                isValid = pokemon.teraTypeOverride() == TERA_TYPE_OVERRIDE_NONE;
            }
            else if (pokemon.speciesID() == OGERPON_SPECIES)
            {
                isValid = isOgerponTeraTypeValid(pokemon.teraTypeOverride(), pokemon.form());
            }
            else
            {
                isValid = isTeraTypeValid(pokemon.teraTypeOriginal());
            }
            if (!isValid)
            {
                context.add(Severity::Invalid, CheckIdentifier::Misc, ResultCode::TeraTypeIncorrect);
            }
        }

        /**
         * PKHeX MiscVerifierPK9.VerifyEncounter. A raid's seed only feeds the PIDIV, and the ride legends'
         * transfer rule needs EncounterStatic9.StarterBoxLegend, which PKSE's rows do not carry.
         */
        void verifyTeraEncounter(const AnalysisContext &context, const Pokemon::Pokemon9SV &pokemon)
        {
            const EncounterInfo &encounter = context.encounter;
            if (!encounter.isValid())
            {
                return;
            }
            const bool isScarletVioletEncounter = encounter.originGroup == Enums::GameVersion::SV;
            if (isScarletVioletEncounter && encounter.type == EncounterType::Egg)
            {
                const std::span<const Evolutions::EvolutionCriteria> stages = context.evolutionHistory().getChain(Enums::GameVersion::SV);
                const int stageCount = static_cast<int>(stages.size());
                if (stageCount == 0)
                {
                    return;
                }
                const Evolutions::EvolutionCriteria &eggStage = stages[stageCount - 1];
                const uint16_t eggSpeciesId = eggStage.speciesId;
                const bool isEggFormFree = eggSpeciesId == BURMY_SPECIES || eggSpeciesId == FURFROU_SPECIES || eggSpeciesId == ORICORIO_SPECIES;
                const bool isMatch = isEggFormFree ? isTeraTypeAnyFormMatch(eggSpeciesId, pokemon.teraTypeOriginal(), false) : isTeraTypePersonalMatch(eggSpeciesId, eggStage.formId, pokemon.teraTypeOriginal());
                if (!isMatch)
                {
                    context.add(Severity::Invalid, CheckIdentifier::Misc, ResultCode::TeraTypeMismatch);
                }
                return;
            }
            if (encounter.type == EncounterType::Raid && isScarletVioletEncounter)
            {
                return;
            }
            if (!isScarletVioletEncounter || pokemon.originGame() == POKEMON_GO_VERSION)
            {
                if (pokemon.teraTypeOverride() == TERA_TYPE_OVERRIDE_NONE)
                {
                    context.add(Severity::Invalid, CheckIdentifier::Misc, ResultCode::TeraTypeIncorrect);
                    return;
                }
                const std::span<const Evolutions::EvolutionCriteria> stages = context.evolutionHistory().getChain(Enums::GameVersion::SV);
                if (!isTeraImportMatch(stages, pokemon.teraTypeOriginal()))
                {
                    context.add(Severity::Invalid, CheckIdentifier::Misc, ResultCode::TeraTypeIncorrect);
                }
            }
        }
    }

    void verifyMisc9SV(const AnalysisContext &context)
    {
        const auto &pokemon = static_cast<const Pokemon::Pokemon9SV &>(context.pokemon);
        verifyStatAlignment(context);
        verifyMachineFlags(context, pokemon);
        if (!isBattleVersionValid(context, pokemon.battleVersion()))
        {
            context.add(Severity::Invalid, CheckIdentifier::Misc, ResultCode::StatBattleVersionInvalid);
        }
        if (!isObedienceLevelValid(context, pokemon.obedienceLevel()))
        {
            context.add(Severity::Invalid, CheckIdentifier::Misc, ResultCode::TransferObedienceLevel);
        }
        verifyTeraType(context, pokemon);
        verifyTeraEncounter(context, pokemon);
    }

    void verifyMisc9LZA(const AnalysisContext &context)
    {
        const auto &pokemon = static_cast<const Pokemon::Pokemon9LZA &>(context.pokemon);
        verifyStatAlignment(context);
        verifyLegendsZA(context);
        if (!isBattleVersionValid(context, pokemon.battleVersion()))
        {
            context.add(Severity::Invalid, CheckIdentifier::Misc, ResultCode::StatBattleVersionInvalid);
        }
        if (!isObedienceLevelValid(context, pokemon.obedienceLevel()))
        {
            context.add(Severity::Invalid, CheckIdentifier::Misc, ResultCode::TransferObedienceLevel);
        }

        const std::optional<bool> encounterAlpha = isEncounterAlpha(context);
        if (encounterAlpha.has_value() && *encounterAlpha != pokemon.isAlpha())
        {
            context.add(Severity::Invalid, CheckIdentifier::Misc, ResultCode::StatAlphaInvalid);
        }
        else if (pokemon.isAlpha() && pokemon.homeTracker() != 0 && (pokemon.heightScalar() != ALPHA_TRACKED_SCALAR || pokemon.weightScalar() != ALPHA_TRACKED_SCALAR))
        {
            // HOME forces 255/255/255 on an inbound Alpha.
            context.add(Severity::Invalid, CheckIdentifier::Misc, ResultCode::StatAlphaInvalid);
        }
    }
}
