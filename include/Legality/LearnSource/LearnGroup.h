/**
 * The learn groups: one per context a Pokemon can have lived in, each checking the moves its games
 * teach for every evolution stage the Pokemon held there, then naming the group to step back to.
 * Ported from PKHeX PKHeX.Core/Legality/LearnSource/Group (ILearnGroup, LearnGroup1 ... LearnGroup9a,
 * LearnGroupHOME, LearnGroupUtil) and PKHeX.Core/Legality/Evolutions/FormChangeUtil.cs. PKHeX's
 * GetAllMoves (move suggestions) is not ported.
 */
#ifndef LEGALITY_LEARNSOURCE_LEARNGROUP_H
#define LEGALITY_LEARNSOURCE_LEARNGROUP_H

#include <cstdint>
#include <span>

#include "Enums/GameVersion.h"
#include "Legality/Evolutions/EvolutionHistory.h"
#include "Legality/LearnSource/LearnEncounter.h"
#include "Legality/LearnSource/LearnEntity.h"
#include "Legality/LearnSource/MoveResult.h"

namespace Legality
{
    namespace LearnSource
    {
        /// PKHeX's ILearnGroup instances.
        enum class LearnGroup : uint8_t
        {
            None,
            Generation1,
            Generation2,
            Generation3,
            Generation4,
            Generation5,
            Generation6,
            Generation7,
            LetsGo,
            SwordShield,
            LegendsArceus,
            BrilliantDiamond,
            ScarletViolet,
            LegendsZA,
            Home,
        };

        /// What every group check reads: the slots being judged, the moves in them, and the Pokemon,
        /// its evolution history and its encounter.
        struct LearnCheck
        {
            std::span<MoveResult, 4> results;
            std::span<const uint16_t, 4> current;
            const LearnEntity &entity;
            const Evolutions::EvolutionHistory &history;
            const LearnEncounter &encounter;
        };

        /// PKHeX LearnGroupUtil.GetCurrentGroup(context); None for a context with no group.
        LearnGroup getCurrentLearnGroup(Enums::GameVersion contextGroup) noexcept;
        /// PKHeX ILearnGroup.GetPrevious.
        LearnGroup getPreviousLearnGroup(LearnGroup group, const LearnCheck &check, LearnOption option) noexcept;
        /// PKHeX ILearnGroup.Check: marks what the group explains; true when every slot is parsed.
        bool checkLearnGroup(LearnGroup group, const LearnCheck &check, uint16_t types, LearnOption option);

        /// PKHeX FormChangeUtil.ShouldIterateForms: a form change keeps (or swaps) moves, so every
        /// form's learnset counts.
        bool shouldIterateForms(uint16_t speciesId, uint8_t generation, LearnOption option) noexcept;
        /// PKHeX LearnVerifierHistory.MarkInitialMoves.
        void markInitialMoves(std::span<MoveResult, 4> results, std::span<const uint16_t, 4> current, std::span<const uint16_t> initialMoves, LearnEnvironment environment) noexcept;
    }
}

#endif
