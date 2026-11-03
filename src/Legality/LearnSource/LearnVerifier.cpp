// Port of PKHeX PKHeX.Core/Legality/LearnSource/Verify (LearnVerifier.cs, LearnVerifierHistory.cs,
// LearnVerifierEgg.cs, LearnVerifierRelearn.cs) and PKHeX.Core/Moves/MoveInfo.cs IsSketchValid. See
// LearnVerifier.h.
#include "Legality/LearnSource/LearnVerifier.h"

#include <algorithm>
#include <array>

#include "Legality/LearnSource/LearnGroup.h"
#include "Legality/LearnSource/LearnSourceConstants.h"
#include "Legality/LearnSource/LearnSources.h"
#include "Legality/LearnSource/Learnset.h"
#include "Legality/LearnSource/MoveBreed.h"
#include "Names/MovePresence.h"

namespace Legality
{
    namespace LearnSource
    {
        namespace
        {
            using Enums::GameVersion;

            constexpr int MOVE_SLOT_COUNT = 4;

            int indexOfMove(std::span<const uint16_t, 4> moves, uint16_t moveId) noexcept
            {
                for (int slotIndex = 0; slotIndex < MOVE_SLOT_COUNT; ++slotIndex)
                {
                    if (moves[slotIndex] == moveId)
                    {
                        return slotIndex;
                    }
                }
                return -1;
            }

            /// PKHeX MoveInfo.GetMaxMoveID.
            int getMaxMoveId(GameVersion contextGroup) noexcept
            {
                switch (contextGroup)
                {
                    case GameVersion::RBY: return MAX_MOVE_ID_1;
                    case GameVersion::GSC: return MAX_MOVE_ID_2;
                    case GameVersion::RSE: return MAX_MOVE_ID_3;
                    case GameVersion::DP: return MAX_MOVE_ID_4;
                    case GameVersion::BW: return MAX_MOVE_ID_5;
                    case GameVersion::XY: return MAX_MOVE_ID_6_ORAS;
                    case GameVersion::SM: return MAX_MOVE_ID_7_USUM;
                    case GameVersion::GG: return MAX_MOVE_ID_7_LETS_GO;
                    // PKHeX gives Sword/Shield Legends: Arceus's maximum.
                    case GameVersion::SWSH:
                    case GameVersion::PLA:
                        return MAX_MOVE_ID_8_LEGENDS_ARCEUS;
                    case GameVersion::BDSP: return MAX_MOVE_ID_8_BDSP;
                    case GameVersion::SV: return MAX_MOVE_ID_9;
                    case GameVersion::ZA: return MAX_MOVE_ID_9_LEGENDS_ZA;
                    default: return -1;
                }
            }

            /// PKHeX MoveInfo.IsMoveZ / IsMoveDynamax / IsMoveTorque, folded into IsMoveKnowable.
            bool isMoveKnowable(uint16_t moveId) noexcept
            {
                const bool isZMove = (moveId >= FIRST_Z_MOVE && moveId <= LAST_Z_MOVE_FIRST_BLOCK) || (moveId >= FIRST_Z_MOVE_SECOND_BLOCK && moveId <= LAST_Z_MOVE_SECOND_BLOCK) || moveId == SIGNATURE_Z_MOVE_PIKACHU_CAP || (moveId >= FIRST_Z_MOVE_THIRD_BLOCK && moveId <= LAST_Z_MOVE);
                const bool isDynamaxMove = moveId >= FIRST_DYNAMAX_MOVE && moveId <= LAST_DYNAMAX_MOVE;
                const bool isTorqueMove = moveId >= BLAZING_TORQUE_MOVE && moveId <= MAGICAL_TORQUE_MOVE;
                return !isZMove && !isDynamaxMove && !isTorqueMove;
            }

            /// PKHeX MoveInfo.IsSketchPossible(move, context).
            bool isSketchPossibleInContext(uint16_t moveId, GameVersion contextGroup)
            {
                switch (contextGroup)
                {
                    case GameVersion::GSC:
                        return moveId != SELF_DESTRUCT_MOVE && moveId != EXPLOSION_MOVE && moveId != MIMIC_MOVE && moveId != METRONOME_MOVE && moveId != MIRROR_MOVE_MOVE && moveId != TRANSFORM_MOVE && moveId != SLEEP_TALK_MOVE;
                    case GameVersion::XY:
                        return moveId != THOUSAND_ARROWS_MOVE && moveId != THOUSAND_WAVES_MOVE && moveId != LIGHT_OF_RUIN_MOVE;
                    case GameVersion::SM:
                    case GameVersion::SWSH:
                        return moveId != LIGHT_OF_RUIN_MOVE;
                    case GameVersion::BDSP:
                        // MoveInfo8b.DummiedMoves, which the move presence table mirrors.
                        return Names::isMovePresent(moveId, GameVersion::BDSP);
                    case GameVersion::SV:
                    {
                        // MoveInfo.DisallowSketch9.
                        constexpr uint16_t DISALLOWED_SKETCH_MOVES_9[] = {
                            DARK_VOID_MOVE, LIGHT_OF_RUIN_MOVE, HYPERSPACE_FURY_MOVE, REVIVAL_BLESSING_MOVE,
                            BLAZING_TORQUE_MOVE, WICKED_TORQUE_MOVE, NOXIOUS_TORQUE_MOVE, COMBAT_TORQUE_MOVE,
                            MAGICAL_TORQUE_MOVE, TERA_STARSTORM_MOVE,
                        };
                        if (std::find(std::begin(DISALLOWED_SKETCH_MOVES_9), std::end(DISALLOWED_SKETCH_MOVES_9), moveId) != std::end(DISALLOWED_SKETCH_MOVES_9))
                        {
                            return false;
                        }
                        return Names::isMovePresent(moveId, GameVersion::SV);
                    }
                    default:
                        return true;
                }
            }

            // ---- LearnVerifierHistory ----

            void markEmptySlots(std::span<MoveResult, 4> results, std::span<const uint16_t, 4> current) noexcept
            {
                for (int slotIndex = MOVE_SLOT_COUNT - 1; slotIndex >= 0; --slotIndex)
                {
                    if (current[slotIndex] != 0)
                    {
                        return;
                    }
                    results[slotIndex] = MoveResult::empty();
                }
            }

            void verifySmeargle(std::span<MoveResult, 4> results, std::span<const uint16_t, 4> current, GameVersion contextGroup)
            {
                for (int slotIndex = MOVE_SLOT_COUNT - 1; slotIndex >= 0; --slotIndex)
                {
                    const uint16_t moveId = current[slotIndex];
                    if (moveId == 0)
                    {
                        results[slotIndex] = MoveResult::empty();
                    }
                    else if (isSketchValid(moveId, contextGroup))
                    {
                        results[slotIndex] = MoveResult::sketch();
                    }
                    else
                    {
                        results[slotIndex] = MoveResult::unobtainable();
                    }
                }
            }

            void markRelearnMoves(std::span<MoveResult, 4> results, std::span<const uint16_t, 4> current, const LearnEntity &entity) noexcept
            {
                for (const uint16_t relearnMove : entity.relearnMoves)
                {
                    if (relearnMove == 0)
                    {
                        return;
                    }
                    const int slotIndex = indexOfMove(current, relearnMove);
                    if (slotIndex != -1)
                    {
                        results[slotIndex] = MoveResult::relearn();
                    }
                }
            }

            void markSpecialMoves(std::span<MoveResult, 4> results, std::span<const uint16_t, 4> current, const LearnEncounter &encounter)
            {
                if (encounter.hasMoves())
                {
                    markInitialMoves(results, current, encounter.moves, getLearnEnvironment(encounter.version));
                }
                else if (encounter.goImportFormat != GoImportFormat::Other)
                {
                    markInitialMoves(results, current, encounter.goInitialMoves, encounter.goImportFormat == GoImportFormat::PK7 ? LearnEnvironment::USUM : LearnEnvironment::GG);
                }
            }

            /// LearnVerifierHistory.MarkAndIterate.
            void markAndIterate(std::span<MoveResult, 4> results, std::span<const uint16_t, 4> current, const LearnEncounter &encounter, const LearnEntity &entity, const Evolutions::EvolutionHistory &history, LearnGroup group, uint16_t types = MOVE_SOURCE_ALL, LearnOption option = LearnOption::Current)
            {
                if (!entity.isOriginalMovesetDeleted() && hasSourceType(types, MOVE_SOURCE_ENCOUNTER))
                {
                    if (entity.formatGeneration >= 6)
                    {
                        markRelearnMoves(results, current, entity);
                    }
                    // Gen 2 handles its trade-backs itself.
                    if (encounter.generation != 2)
                    {
                        markSpecialMoves(results, current, encounter);
                    }
                }
                const LearnCheck check{results, current, entity, history, encounter};
                while (group != LearnGroup::None)
                {
                    if (checkLearnGroup(group, check, types, option))
                    {
                        return;
                    }
                    group = getPreviousLearnGroup(group, check, option);
                }
                for (MoveResult &result : results)
                {
                    if (!result.isParsed())
                    {
                        result = MoveResult::unobtainable();
                    }
                }
            }

            // ---- LearnVerifierEgg ----

            MoveResult getMethodInitial(uint16_t current, uint16_t initial, LearnEnvironment environment) noexcept
            {
                if (current != initial)
                {
                    return MoveResult::unobtainableExpect(initial);
                }
                if (current == 0)
                {
                    return MoveResult::empty();
                }
                return MoveResult::initial(environment);
            }

            void verifyMovesInitial(std::span<MoveResult, 4> results, std::span<const uint16_t, 4> current, std::span<const uint16_t> initial, LearnEnvironment environment)
            {
                const int initialCount = std::min(static_cast<int>(initial.size()), MOVE_SLOT_COUNT);
                for (int slotIndex = 0; slotIndex < initialCount; ++slotIndex)
                {
                    results[slotIndex] = getMethodInitial(current[slotIndex], initial[slotIndex], environment);
                }
                for (int slotIndex = initialCount; slotIndex < MOVE_SLOT_COUNT; ++slotIndex)
                {
                    results[slotIndex] = current[slotIndex] == 0 ? MoveResult::empty() : MoveResult::unobtainableExpect(0);
                }
            }

            void verifyEggFromEncounter(std::span<MoveResult, 4> results, std::span<const uint16_t, 4> current, const LearnEncounter &encounter)
            {
                if (encounter.hasMoves())
                {
                    verifyMovesInitial(results, current, encounter.moves, getLearnEnvironment(encounter.version));
                    return;
                }
                if (encounter.version == GameVersion::Any || encounter.version == GameVersion::Invalid)
                {
                    return;
                }
                const LearnEnvironment environment = getLearnEnvironment(encounter.version);
                const Learnset learnset = getLearnset(environment, encounter.speciesId, encounter.formId);
                verifyMovesInitial(results, current, learnset.getBaseEggMoves(encounter.levelMin), environment);
            }

            void verifyEgg(std::span<MoveResult, 4> results, std::span<const uint16_t, 4> current, const LearnEncounter &encounter, const LearnEntity &entity)
            {
                if (encounter.generation >= 6)
                {
                    if (encounter.isEggTemplate)
                    {
                        for (int slotIndex = 0; slotIndex < MOVE_SLOT_COUNT; ++slotIndex)
                        {
                            const uint16_t relearnMove = entity.relearnMoves[slotIndex];
                            if (current[slotIndex] != relearnMove)
                            {
                                results[slotIndex] = MoveResult::unobtainableExpect(relearnMove);
                            }
                            else
                            {
                                results[slotIndex] = current[slotIndex] == 0 ? MoveResult::empty() : MoveResult::relearn();
                            }
                        }
                    }
                    else if (encounter.hasMoves())
                    {
                        verifyMovesInitial(results, current, encounter.moves, getLearnEnvironment(encounter.version));
                    }
                    else
                    {
                        verifyEggFromEncounter(results, current, encounter);
                    }
                    return;
                }
                if (encounter.isEggTemplate)
                {
                    verifyEggMoveset(encounter, results, current);
                }
                else
                {
                    verifyEggFromEncounter(results, current, encounter);
                }
            }

            // ---- LearnVerifier ----

            void flagFormExclusiveMoves(std::span<MoveResult, 4> results, std::span<const uint16_t, 4> current, const LearnEntity &entity)
            {
                if (entity.speciesId == HOOPA_SPECIES)
                {
                    if (entity.formatGeneration < 8)
                    {
                        return;
                    }
                    // Hoopa in Gen 8 on keeps only its own form's move.
                    const uint16_t disallowed = entity.formId != 0 ? HYPERSPACE_HOLE_MOVE : HYPERSPACE_FURY_MOVE;
                    const int slotIndex = indexOfMove(current, disallowed);
                    if (slotIndex >= 0)
                    {
                        results[slotIndex] = MoveResult::unobtainableExpect(entity.formId == 0 ? HYPERSPACE_HOLE_MOVE : HYPERSPACE_FURY_MOVE);
                    }
                    return;
                }
                if (entity.speciesId != KYUREM_SPECIES)
                {
                    return;
                }
                // Kyurem's forms swap Scary Face for their fusion move and back.
                if (entity.formId != 1)
                {
                    const int slotIndex = indexOfMove(current, FUSION_FLARE_MOVE);
                    if (slotIndex >= 0)
                    {
                        results[slotIndex] = MoveResult::unobtainableExpect(SCARY_FACE_MOVE);
                    }
                }
                if (entity.formId != 2)
                {
                    const int slotIndex = indexOfMove(current, FUSION_BOLT_MOVE);
                    if (slotIndex >= 0)
                    {
                        results[slotIndex] = MoveResult::unobtainableExpect(SCARY_FACE_MOVE);
                    }
                }
                if (entity.formId != 0 && entity.formatGeneration < 8)
                {
                    const int slotIndex = indexOfMove(current, SCARY_FACE_MOVE);
                    if (slotIndex >= 0)
                    {
                        results[slotIndex] = MoveResult::unobtainableExpect(entity.formId == 1 ? FUSION_FLARE_MOVE : FUSION_BOLT_MOVE);
                    }
                }
            }

            /// LearnVerifier.Finalize: duplicates, gaps before a move, and an empty first slot.
            void finalizeResults(std::span<MoveResult, 4> results, std::span<const uint16_t, 4> current) noexcept
            {
                bool hasEmptySlot = false;
                for (int slotIndex = 0; slotIndex < MOVE_SLOT_COUNT; ++slotIndex)
                {
                    const uint16_t moveId = current[slotIndex];
                    if (moveId == 0)
                    {
                        hasEmptySlot = true;
                        continue;
                    }
                    if (hasEmptySlot)
                    {
                        for (int emptyIndex = slotIndex - 1; emptyIndex >= 0 && current[emptyIndex] == 0; --emptyIndex)
                        {
                            results[emptyIndex] = MoveResult::emptyInvalid();
                        }
                        hasEmptySlot = false;
                        continue;
                    }
                    for (int laterIndex = MOVE_SLOT_COUNT - 1; laterIndex > slotIndex; --laterIndex)
                    {
                        if (current[laterIndex] == moveId)
                        {
                            results[slotIndex] = MoveResult::duplicate();
                            break;
                        }
                    }
                }
                if (current[0] == 0)
                {
                    results[0] = MoveResult::emptyInvalid();
                }
            }

            MoveResult parseExpect(uint16_t moveId, uint16_t expectedMove = 0) noexcept
            {
                if (moveId != expectedMove)
                {
                    return MoveResult::unobtainableExpect(expectedMove);
                }
                if (moveId == 0)
                {
                    return MoveResult::empty();
                }
                return MoveResult::relearn();
            }

            int findDuplicateMoveSlot(std::span<const uint16_t, 4> moves) noexcept
            {
                if (moves[0] != 0 && moves[0] == moves[1])
                {
                    return 1;
                }
                if (moves[2] != 0 && (moves[0] == moves[2] || moves[1] == moves[2]))
                {
                    return 2;
                }
                if (moves[3] != 0 && (moves[0] == moves[3] || moves[1] == moves[3] || moves[2] == moves[3]))
                {
                    return 3;
                }
                return -1;
            }
        }

        void forgetUnobtainableResults(std::span<MoveResult, 4> results) noexcept
        {
            for (MoveResult &result : results)
            {
                if (result.info.method == LearnMethod::Unobtainable || result.info.method == LearnMethod::UnobtainableExpect)
                {
                    result = MoveResult{};
                }
            }
        }

        bool isSketchValid(uint16_t moveId, Enums::GameVersion contextGroup)
        {
            if (static_cast<int>(moveId) > getMaxMoveId(contextGroup))
            {
                return false;
            }
            if (moveId == STRUGGLE_MOVE || moveId == CHATTER_MOVE || !isMoveKnowable(moveId))
            {
                return false;
            }
            return isSketchPossibleInContext(moveId, contextGroup);
        }

        void verifyCurrentMoves(std::span<MoveResult, 4> results, const LearnEntity &entity, const LearnEncounter &encounter, const Evolutions::EvolutionHistory &history)
        {
            std::fill(results.begin(), results.end(), MoveResult{});
            const std::span<const uint16_t, 4> current(entity.moves);
            if (entity.isEgg)
            {
                verifyEgg(results, current, encounter, entity);
            }
            else if (encounter.speciesId == SMEARGLE_SPECIES)
            {
                verifySmeargle(results, current, entity.contextGroup);
                flagFormExclusiveMoves(results, current, entity);
            }
            else
            {
                markEmptySlots(results, current);
                markAndIterate(results, current, encounter, entity, history, getCurrentLearnGroup(entity.contextGroup));
                flagFormExclusiveMoves(results, current, entity);
            }
            finalizeMoveSlots(results, current, entity.contextGroup);
        }

        void finalizeMoveSlots(std::span<MoveResult, 4> results, std::span<const uint16_t, 4> current, Enums::GameVersion contextGroup) noexcept
        {
            finalizeResults(results, current);
            // Legends: Z-A allows gaps between moves.
            const bool hasAnyMove = std::any_of(current.begin(), current.end(), [](uint16_t moveId) { return moveId != 0; });
            if (contextGroup == GameVersion::ZA && hasAnyMove)
            {
                for (MoveResult &result : results)
                {
                    if (result.info.method == LearnMethod::EmptyInvalid)
                    {
                        result = MoveResult::empty();
                    }
                }
            }
        }

        void verifyEggMoveset(const LearnEncounter &encounter, std::span<MoveResult, 4> results, std::span<const uint16_t, 4> moves)
        {
            const uint8_t generation = encounter.generation;
            const LearnEnvironment environment = getLearnEnvironment(encounter.version);
            std::array<uint8_t, 4> origins{};
            const bool isValid = validateBreedMoves(generation, encounter.speciesId, encounter.formId, encounter.version, moves, origins);
            std::array<uint16_t, 4> expected{};
            if (isValid)
            {
                std::copy(moves.begin(), moves.end(), expected.begin());
            }
            else
            {
                getExpectedBreedMoves(generation, encounter.speciesId, encounter.formId, encounter.version, moves, expected);
                validateBreedMoves(generation, encounter.speciesId, encounter.formId, encounter.version, expected, origins);
            }
            for (int slotIndex = MOVE_SLOT_COUNT - 1; slotIndex >= 0; --slotIndex)
            {
                const uint16_t moveId = moves[slotIndex];
                if (moveId != expected[slotIndex])
                {
                    results[slotIndex] = MoveResult::unobtainableExpect(expected[slotIndex]);
                }
                else if (moveId == 0)
                {
                    results[slotIndex] = MoveResult::empty();
                }
                else
                {
                    results[slotIndex] = MoveResult::make(getEggSourceMethod(origins[slotIndex], generation), environment);
                }
            }
            const int duplicateSlot = findDuplicateMoveSlot(moves);
            if (duplicateSlot != -1)
            {
                results[duplicateSlot] = MoveResult::duplicate();
            }
        }

        void verifyRelearnMoves(std::span<MoveResult, 4> results, const LearnEncounter &encounter, const LearnEntity &entity)
        {
            const std::array<uint16_t, 4> &relearn = entity.relearnMoves;
            // ShouldNotHaveRelearnMoves: before Gen 6, or after a Battle Version wiped them.
            const bool shouldHaveNone = encounter.generation < 6 || entity.isOriginalMovesetDeleted();
            if (!shouldHaveNone && encounter.hasRelearnMoves())
            {
                for (int slotIndex = MOVE_SLOT_COUNT - 1; slotIndex >= 0; --slotIndex)
                {
                    results[slotIndex] = parseExpect(relearn[slotIndex], encounter.relearnMoves[slotIndex]);
                }
                return;
            }
            if (!shouldHaveNone && encounter.isEggTemplate)
            {
                verifyEggMoveset(encounter, results, relearn);
                return;
            }
            if (!shouldHaveNone && encounter.isMoveBonusPossible && (encounter.isMoveBonusRequired || relearn[0] != 0))
            {
                // One random egg move as the only relearn move.
                for (int slotIndex = MOVE_SLOT_COUNT - 1; slotIndex >= 1; --slotIndex)
                {
                    results[slotIndex] = parseExpect(relearn[slotIndex]);
                }
                results[0] = encounter.isMoveBonus(relearn[0]) ? MoveResult::relearn() : MoveResult::unobtainable();
                return;
            }
            for (int slotIndex = MOVE_SLOT_COUNT - 1; slotIndex >= 0; --slotIndex)
            {
                results[slotIndex] = parseExpect(relearn[slotIndex]);
            }
        }

        bool canKnowMove(const LearnEncounter &encounter, uint16_t moveId, const Evolutions::EvolutionHistory &history, const LearnEntity &entity, LearnGroup group, LearnOption option)
        {
            std::array<MoveResult, MOVE_SLOT_COUNT> results{};
            const std::array<uint16_t, MOVE_SLOT_COUNT> current{moveId, 0, 0, 0};
            markAndIterate(results, current, encounter, entity, history, group, MOVE_SOURCE_ALL, option);
            return results[0].isValid();
        }
    }
}
