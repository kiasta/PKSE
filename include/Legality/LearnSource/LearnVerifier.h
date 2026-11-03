/**
 * The per-move verdicts: which source explains each current move and each relearn move. Ported
 * from PKHeX PKHeX.Core/Legality/LearnSource/Verify (LearnVerifier, LearnVerifierHistory,
 * LearnVerifierEgg, LearnVerifierRelearn) and MoveInfo.IsSketchValid.
 */
#ifndef LEGALITY_LEARNSOURCE_LEARNVERIFIER_H
#define LEGALITY_LEARNSOURCE_LEARNVERIFIER_H

#include <cstdint>
#include <span>

#include "Enums/GameVersion.h"
#include "Legality/Evolutions/EvolutionHistory.h"
#include "Legality/LearnSource/LearnEncounter.h"
#include "Legality/LearnSource/LearnEntity.h"
#include "Legality/LearnSource/LearnGroup.h"
#include "Legality/LearnSource/MoveResult.h"

namespace Legality
{
    namespace LearnSource
    {
        /// PKHeX LearnVerifier.Verify: one result per current move slot.
        void verifyCurrentMoves(std::span<MoveResult, 4> results, const LearnEntity &entity, const LearnEncounter &encounter, const Evolutions::EvolutionHistory &history);

        /// PKHeX LearnVerifier.Finalize plus Z-A's gap rule: duplicates and empty slots before a move.
        void finalizeMoveSlots(std::span<MoveResult, 4> results, std::span<const uint16_t, 4> current, Enums::GameVersion contextGroup) noexcept;

        /// PKHeX LearnVerifierRelearn.Verify: one result per relearn move slot.
        void verifyRelearnMoves(std::span<MoveResult, 4> results, const LearnEncounter &encounter, const LearnEntity &entity);

        /// PKHeX LearnVerifierRelearn.VerifyEggMoveset: an egg's moves (its relearn moves in Gen 6 on,
        /// its current moves before) against the order breeding can produce.
        void verifyEggMoveset(const LearnEncounter &encounter, std::span<MoveResult, 4> results, std::span<const uint16_t, 4> moves);

        /// For a template whose fixed moves PKSE cannot see (LearnEncounter.hasUnknownMoves): turn each
        /// Unobtainable verdict back into "not parsed", which the report reads as unknown.
        void forgetUnobtainableResults(std::span<MoveResult, 4> results) noexcept;

        /// PKHeX MemoryPermissions.GetCanKnowMove: the Pokemon could know `moveId`, judged from `group`
        /// back through the earlier games with `option`.
        bool canKnowMove(const LearnEncounter &encounter, uint16_t moveId, const Evolutions::EvolutionHistory &history, const LearnEntity &entity, LearnGroup group, LearnOption option);

        /// PKHeX MoveInfo.IsSketchValid: Smeargle can Sketch `moveId` in that context.
        bool isSketchValid(uint16_t moveId, Enums::GameVersion contextGroup);
    }
}

#endif
