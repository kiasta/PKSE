/**
 * One stage of an evolution chain: the species and form, the levels it could have held them at, and
 * how it evolved into them. Ported from PKHeX PKHeX.Core/Legality/Evolutions/Methods/EvoCriteria.cs.
 */
#ifndef LEGALITY_EVOLUTIONS_EVOLUTIONCRITERIA_H
#define LEGALITY_EVOLUTIONS_EVOLUTIONCRITERIA_H

#include <array>
#include <cstdint>
#include <span>

#include "Legality/Evolutions/EvolutionType.h"

namespace Legality
{
    namespace Evolutions
    {
        /// PKHeX EvolutionTree.MaxEvolutions: no lineage has more than three stages.
        constexpr int MAX_EVOLUTION_STAGES = 3;

        struct EvolutionCriteria
        {
            uint16_t speciesId = 0;
            uint8_t formId = 0;
            /// Levels the stage needs before it can evolve again (0 or 1).
            uint8_t levelUpRequired = 0;
            uint8_t levelMax = 0;
            uint8_t levelMin = 0;
            /// How this stage was reached from the previous one; Invalid when it is not reached.
            EvolutionType method = EvolutionType::None;

            bool requiresLevelUp() const noexcept { return levelUpRequired != 0; }
            bool isInsideLevelRange(uint8_t level) const noexcept { return levelMin <= level && level <= levelMax; }

            /// PKHeX EvoCriteria.IsBetterDevolution.
            bool isBetterDevolution(const EvolutionCriteria &reference) const noexcept
            {
                if (reference.speciesId == 0)
                {
                    return true;
                }
                return levelMin + levelUpRequired < reference.levelMin + reference.levelUpRequired;
            }

            /// PKHeX EvoCriteria.IsBetterEvolution.
            bool isBetterEvolution(const EvolutionCriteria &reference) const noexcept
            {
                if (reference.method == EvolutionType::Invalid)
                {
                    return true;
                }
                if (levelMin + levelUpRequired > reference.levelMin + reference.levelUpRequired)
                {
                    return false;
                }
                return levelMax > reference.levelMax;
            }

            bool operator==(const EvolutionCriteria &other) const noexcept = default;
        };

        /// A chain of stages, the most evolved first (PKHeX's EvoCriteria[] order).
        struct EvolutionChain
        {
            std::array<EvolutionCriteria, MAX_EVOLUTION_STAGES> stages{};
            uint8_t stageCount = 0;

            std::span<const EvolutionCriteria> view() const noexcept { return std::span<const EvolutionCriteria>(stages.data(), stageCount); }
            bool isEmpty() const noexcept { return stageCount == 0; }

            /// PKHeX EvolutionHistory.HasVisited(span, species).
            bool contains(uint16_t speciesId) const noexcept
            {
                for (const EvolutionCriteria &stage : view())
                {
                    if (stage.speciesId == speciesId)
                    {
                        return true;
                    }
                }
                return false;
            }
        };
    }
}

#endif
