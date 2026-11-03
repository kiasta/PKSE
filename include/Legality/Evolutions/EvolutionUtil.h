/**
 * The chain arithmetic the evolution groups share: clamping level bounds, discarding stages a game
 * does not have, and trimming a chain to the stages a context saw. Ported from PKHeX
 * PKHeX.Core/Legality/Evolutions/EvolutionGroup/EvolutionUtil.cs. A working chain is a span of
 * MAX_EVOLUTION_STAGES entries, the most evolved first, with species 0 marking unused entries.
 */
#ifndef LEGALITY_EVOLUTIONS_EVOLUTIONUTIL_H
#define LEGALITY_EVOLUTIONS_EVOLUTIONUTIL_H

#include <cstdint>
#include <span>

#include "Legality/Evolutions/EvolutionCriteria.h"
#include "Legality/Evolutions/EvolutionTree.h"

namespace Legality
{
    namespace Evolutions
    {
        /// PKHeX EvolutionUtil.SetHistory: the run of stages present in `presenceTree`'s game.
        EvolutionChain setHistory(std::span<const EvolutionCriteria> result, const EvolutionTree &presenceTree) noexcept;

        /// PKHeX EvolutionUtil.Discard(result, pt).
        void discard(std::span<EvolutionCriteria> result, const EvolutionTree &presenceTree) noexcept;

        /// PKHeX EvolutionUtil.Discard(result, pt, ability, abilityIndex), with `personalGroup` the
        /// save-format group whose personal table pt is.
        void discardByAbility(std::span<EvolutionCriteria> result, const EvolutionTree &presenceTree, Enums::GameVersion personalGroup, uint16_t abilityId, int abilityIndex) noexcept;

        /// PKHeX EvolutionUtil.IndexOf; -1 when absent.
        int indexOfSpecies(std::span<const EvolutionCriteria> result, uint16_t speciesId) noexcept;

        /// PKHeX EvolutionUtil.UpdateCeiling.
        void updateCeiling(std::span<EvolutionCriteria> result, uint8_t level) noexcept;

        /// PKHeX EvolutionUtil.UpdateFloor.
        void updateFloor(std::span<EvolutionCriteria> result, uint8_t levelMin, uint8_t levelMax) noexcept;

        /// PKHeX EvolutionUtil.ConditionBaseChainForward.
        void conditionBaseChainForward(std::span<EvolutionCriteria> result, uint16_t encounterSpeciesId) noexcept;

        /// PKHeX EvolutionUtil.CleanDevolve.
        void cleanDevolve(std::span<EvolutionCriteria> result, uint8_t levelMin) noexcept;

        /// PKHeX EvolutionUtil.ConditionEncounterNoMet.
        void conditionEncounterNoMet(std::span<EvolutionCriteria> chain) noexcept;
    }
}

#endif
