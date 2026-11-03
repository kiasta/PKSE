// Port of PKHeX PKHeX.Core/Legality/Verifiers/LegendsArceusVerifier.cs. CheckLearnset is not ported: it
// rewrites the move parse's results, and PKSE has no move verifier for it to feed.
#include "Legality/Verifiers/MiscVerifier.h"

#include <cstdint>
#include <string>

#include "Enums/GameVersion.h"
#include "Legality/Encounters/EncounterSlot8a.h"
#include "Legality/Encounters/EncounterStatic8a.h"
#include "Legality/Encounters/LegalityInfo.h"
#include "Names/MoveNames.h"
#include "Pokemon/MoveRecordTable.h"
#include "Pokemon/Pokemon8LA.h"
#include "Legality/Evolutions/EvolutionEntity.h"

namespace Legality
{
    namespace
    {
        constexpr uint8_t ALPHA_SLOT_SCALAR = 255;
        constexpr int STAT_COUNT = 6;
        constexpr uint8_t GRIT_TRUE_MAXIMUM = 10;

        /// PKHeX GanbaruExtensions.GetBias: a higher IV starts closer to the cap.
        uint8_t getGritBias(uint8_t individualValue)
        {
            if (individualValue >= 31)
            {
                return 3;
            }
            if (individualValue >= 26)
            {
                return 2;
            }
            if (individualValue >= 20)
            {
                return 1;
            }
            return 0;
        }

        int findMoveShopIndex(uint16_t moveId)
        {
            for (int shopIndex = 0; shopIndex < Pokemon::MOVE_SHOP_COUNT_8LA; ++shopIndex)
            {
                if (Pokemon::MOVE_SHOP_MOVES_8LA[shopIndex] == moveId)
                {
                    return shopIndex;
                }
            }
            return -1;
        }

        bool isMoveShopPermitted(uint64_t permissions, int shopIndex)
        {
            return shopIndex >= 0 && ((permissions >> shopIndex) & 1) != 0;
        }

        /// PKHeX LegendsArceusVerifier.CheckGanbaru: only the first stat over its cap is reported.
        void verifyGrit(const AnalysisContext &context, const Pokemon::Pokemon8LA &pokemon)
        {
            const uint8_t gritValues[STAT_COUNT] = { pokemon.gritHP(), pokemon.gritATK(), pokemon.gritDEF(), pokemon.gritSPE(), pokemon.gritSPA(), pokemon.gritSPD() };
            const uint8_t individualValues[STAT_COUNT] = { pokemon.ivHP(), pokemon.ivATK(), pokemon.ivDEF(), pokemon.ivSPE(), pokemon.ivSPA(), pokemon.ivSPD() };
            for (int statIndex = 0; statIndex < STAT_COUNT; ++statIndex)
            {
                const uint8_t gritMaximum = GRIT_TRUE_MAXIMUM - getGritBias(individualValues[statIndex]);
                if (gritValues[statIndex] <= gritMaximum)
                {
                    continue;
                }
                context.add(Severity::Invalid, CheckIdentifier::GanbaruValues, ResultCode::GanbaruStatAtMost, std::to_string(gritMaximum), std::to_string(statIndex));
                return;
            }
        }

        /// PKHeX LegendsArceusVerifier.CheckScalars.
        void verifyScalars(const AnalysisContext &context, const Pokemon::Pokemon8LA &pokemon)
        {
            // A static encounter fixes its sizes itself; only a wild Alpha slot is forced to 255.
            const bool isWildSlot = context.encounter.type == EncounterType::Wild && context.encounter.originGroup == Enums::GameVersion::PLA;
            if (pokemon.isAlpha() && isWildSlot)
            {
                if (pokemon.heightScalar() != ALPHA_SLOT_SCALAR)
                {
                    context.add(Severity::Invalid, CheckIdentifier::RelearnMove, ResultCode::StatIncorrectHeightValue, std::to_string(ALPHA_SLOT_SCALAR));
                }
                if (pokemon.weightScalar() != ALPHA_SLOT_SCALAR)
                {
                    context.add(Severity::Invalid, CheckIdentifier::RelearnMove, ResultCode::StatIncorrectWeightValue, std::to_string(ALPHA_SLOT_SCALAR));
                }
            }
            if (pokemon.heightScalar() != pokemon.scale())
            {
                context.add(Severity::Invalid, CheckIdentifier::Encounter, ResultCode::StatIncorrectScaleValue, std::to_string(pokemon.heightScalar()));
            }
        }

        /// PKHeX LegendsArceusVerifier.CanLearnMoveByLevelUp: the lowest level any visited stage learns it at.
        bool canLearnMoveByLevelUp(const AnalysisContext &context, uint16_t moveId)
        {
            const std::span<const Evolutions::EvolutionCriteria> stages = context.evolutionHistory().getChain(Enums::GameVersion::PLA);
            const int stageCount = static_cast<int>(stages.size());
            int lowestLevel = 101;
            for (int stageIndex = 0; stageIndex < stageCount; ++stageIndex)
            {
                for (const Pokemon::LevelUpMove &levelUpMove : Pokemon::getLevelUpMoves8LA(stages[stageIndex].speciesId, stages[stageIndex].formId))
                {
                    if (levelUpMove.moveId == moveId && levelUpMove.level < lowestLevel)
                    {
                        lowestLevel = levelUpMove.level;
                    }
                }
            }
            return lowestLevel <= Evolutions::getEntityCurrentLevel(context.pokemon);
        }

        /// The encounter engine's match, or null where the origin is not ported to it.
        const Encounters::Encounterable *getMatchedEncounter(const AnalysisContext &context)
        {
            if (context.legalityInfo == nullptr || !context.legalityInfo->encounterMatch())
            {
                return nullptr;
            }
            return &*context.legalityInfo->encounterMatch();
        }

        /// PKHeX `EncounterMatch is EncounterSlot8a { Type: SlotType8a.Landmark }`.
        bool isLandmarkSlot(const AnalysisContext &context)
        {
            const Encounters::Encounterable *encounter = getMatchedEncounter(context);
            const Encounters::EncounterSlot8a *slot = encounter == nullptr ? nullptr : Encounters::encounterAs<Encounters::EncounterSlot8a>(*encounter);
            return slot != nullptr && slot->type() == Encounters::SlotType8a::Landmark;
        }

        /**
         * PKHeX `EncounterMatch is (IMoveset m and IMasteryInitialMoveShop8) && m.Moves.Contains(move)`:
         * the encounter handed the move over already mastered, which IsForcedMasteryCorrect checked.
         * Without an engine match (a hand-built context) any encounter that could have a fixed
         * moveset excuses the move.
         */
        bool isInitialMasteredMove(const AnalysisContext &context, uint16_t moveId)
        {
            if (context.legalityInfo != nullptr)
            {
                const Encounters::Encounterable *encounter = getMatchedEncounter(context);
                const Encounters::EncounterStatic8a *staticEncounter = encounter == nullptr ? nullptr : Encounters::encounterAs<Encounters::EncounterStatic8a>(*encounter);
                return staticEncounter != nullptr && staticEncounter->moves().contains(moveId);
            }
            switch (context.encounter.type)
            {
                case EncounterType::Unchecked:
                case EncounterType::Static:
                case EncounterType::Gift:
                case EncounterType::Trade:
                case EncounterType::Event:
                    return true;
                default:
                    return false;
            }
        }

        /// PKHeX LegendsArceusVerifier.CheckMastery.
        void verifyMastery(const AnalysisContext &context, const Pokemon::Pokemon8LA &pokemon)
        {
            const uint64_t permissions = Pokemon::getMoveShopPermissions8LA(pokemon.speciesID(), pokemon.form());
            const uint16_t alphaMoveId = pokemon.alphaMove();
            if (alphaMoveId != 0)
            {
                if (!pokemon.isAlpha() || isLandmarkSlot(context))
                {
                    context.add(Severity::Invalid, CheckIdentifier::RelearnMove, ResultCode::MoveShopAlphaMoveShouldBeZero);
                }
                else
                {
                    const int alphaShopIndex = findMoveShopIndex(alphaMoveId);
                    if (!isMoveShopPermitted(permissions, alphaShopIndex))
                    {
                        context.add(Severity::Invalid, CheckIdentifier::RelearnMove, ResultCode::MoveShopAlphaMoveShouldBeOther);
                    }
                    else if (!pokemon.isMoveShopMastered(alphaShopIndex))
                    {
                        context.add(Severity::Invalid, CheckIdentifier::RelearnMove, ResultCode::MoveShopAlphaMoveShouldBeMastered, Names::getMoveName(alphaMoveId));
                    }
                }
            }
            else if (isEncounterAlpha(context).value_or(false) && !isLandmarkSlot(context))
            {
                uint16_t encounterSpeciesId = pokemon.speciesID();
                uint8_t encounterFormId = pokemon.form();
                if (const Encounters::Encounterable *encounterMatch = Encounters::getEncounterMatch(context))
                {
                    encounterSpeciesId = encounterMatch->speciesId();
                    encounterFormId = encounterMatch->formId();
                }
                if (Pokemon::getMoveShopPermissions8LA(encounterSpeciesId, encounterFormId) == 0)
                {
                    context.add(Severity::Invalid, CheckIdentifier::RelearnMove, ResultCode::MoveShopAlphaMoveShouldBeOther);
                }
            }

            for (int shopIndex = 0; shopIndex < Pokemon::MOVE_SHOP_COUNT_8LA; ++shopIndex)
            {
                const uint16_t moveId = Pokemon::MOVE_SHOP_MOVES_8LA[shopIndex];
                if (pokemon.isMoveShopPurchased(shopIndex))
                {
                    if (!isMoveShopPermitted(permissions, shopIndex))
                    {
                        context.add(Severity::Invalid, CheckIdentifier::RelearnMove, ResultCode::MoveShopPurchaseInvalid, Names::getMoveName(moveId));
                    }
                    continue;
                }
                if (!pokemon.isMoveShopMastered(shopIndex) || alphaMoveId == moveId || isInitialMasteredMove(context, moveId))
                {
                    continue;
                }
                if (!isMoveShopPermitted(permissions, shopIndex))
                {
                    context.add(Severity::Invalid, CheckIdentifier::RelearnMove, ResultCode::MoveShopMasterInvalid, Names::getMoveName(moveId));
                }
                else if (!canLearnMoveByLevelUp(context, moveId))
                {
                    context.add(Severity::Invalid, CheckIdentifier::RelearnMove, ResultCode::MoveShopMasterNotLearned, Names::getMoveName(moveId));
                }
            }
        }
    }

    void verifyLegendsArceus(const AnalysisContext &context)
    {
        const auto &pokemon = static_cast<const Pokemon::Pokemon8LA &>(context.pokemon);
        if (pokemon.isNoble())
        {
            context.add(Severity::Invalid, CheckIdentifier::RelearnMove, ResultCode::StatNobleInvalid);
        }
        const std::optional<bool> encounterAlpha = isEncounterAlpha(context);
        if (encounterAlpha.has_value() && *encounterAlpha != pokemon.isAlpha())
        {
            context.add(Severity::Invalid, CheckIdentifier::RelearnMove, ResultCode::StatAlphaInvalid);
        }
        verifyScalars(context, pokemon);
        verifyGrit(context, pokemon);
        verifyMastery(context, pokemon);
    }
}
