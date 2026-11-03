/**
 * One generation's step in an evolution search: how to walk a chain down (devolve) and up (evolve)
 * with that generation's evolution data, which generation to visit before and after it, and which
 * stages its game lacks. Ported from PKHeX PKHeX.Core/Legality/Evolutions/EvolutionGroup/
 * (IEvolutionGroup.cs, EvolutionGroup1-7b.cs, EvolutionGroupHOME.cs, EvolutionGroupHOME2.cs,
 * EvolutionGroupUtil.cs).
 */
#ifndef LEGALITY_EVOLUTIONS_EVOLUTIONGROUP_H
#define LEGALITY_EVOLUTIONS_EVOLUTIONGROUP_H

#include <span>

#include "Enums/GameVersion.h"
#include "Legality/Evolutions/EvolutionCriteria.h"
#include "Legality/Evolutions/EvolutionEntity.h"
#include "Legality/Evolutions/EvolutionHistory.h"
#include "Legality/Evolutions/EvolutionOrigin.h"

namespace Legality
{
    namespace Evolutions
    {
        class EvolutionGroup
        {
        public:
            virtual ~EvolutionGroup() = default;

            /// The group to walk back into while devolving; null when this one is the origin's.
            virtual const EvolutionGroup *getPrevious(const EvolutionEntity &entity, const EvolutionOrigin &origin) const noexcept = 0;

            /// The group to walk forward into while evolving; null when this one holds the entity.
            virtual const EvolutionGroup *getNext(const EvolutionEntity &entity, const EvolutionOrigin &origin) const noexcept = 0;

            /// Walks `result` down to earlier stages. Returns how many stages devolved.
            virtual int devolve(std::span<EvolutionCriteria> result, const EvolutionEntity &entity, EvolutionOrigin origin) const noexcept = 0;

            /// Walks `result` up to later stages and records this group's contexts in `history`.
            virtual int evolve(std::span<EvolutionCriteria> result, const EvolutionEntity &entity, EvolutionOrigin origin, EvolutionHistory &history) const noexcept = 0;

            /// Discards the stages that do not exist where the Pokemon originated.
            virtual void discardForOrigin(std::span<EvolutionCriteria> result, const EvolutionEntity &entity, const EvolutionOrigin &origin) const noexcept = 0;
        };

        /// PKHeX EvolutionGroupUtil.GetGroup; null when `contextGroup` names no context.
        const EvolutionGroup *getEvolutionGroup(Enums::GameVersion contextGroup) noexcept;
    }
}

#endif
