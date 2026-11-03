/**
 * Per context, the species a Pokemon could have been there and the levels it could have held them
 * at: PKHeX's LegalInfo.EvoChainsAllGens. Ported from PKHeX
 * PKHeX.Core/Legality/Evolutions/EvolutionHistory.cs.
 */
#ifndef LEGALITY_EVOLUTIONS_EVOLUTIONHISTORY_H
#define LEGALITY_EVOLUTIONS_EVOLUTIONHISTORY_H

#include <array>
#include <cstdint>
#include <initializer_list>
#include <span>

#include "Enums/GameVersion.h"
#include "Legality/Evolutions/EvolutionContext.h"
#include "Legality/Evolutions/EvolutionCriteria.h"

namespace Legality
{
    namespace Evolutions
    {
        class EvolutionHistory
        {
        public:
            /// The chain for a context group, the most evolved first; empty when never visited or
            /// when `contextGroup` names no context.
            std::span<const EvolutionCriteria> getChain(Enums::GameVersion contextGroup) const noexcept;
            const EvolutionChain &getChainRecord(Enums::GameVersion contextGroup) const noexcept;
            void setChain(Enums::GameVersion contextGroup, const EvolutionChain &chain) noexcept;

            /// PKHeX HasVisitedGen1 ... HasVisitedZA.
            bool hasVisited(Enums::GameVersion contextGroup) const noexcept { return !getChain(contextGroup).empty(); }

            /// PKHeX EvolutionHistory.HasVisited(Get(context), species).
            bool hasVisitedAs(Enums::GameVersion contextGroup, uint16_t speciesId) const noexcept { return getChainRecord(contextGroup).contains(speciesId); }

            /// PKHeX EvolutionHistory.HasVisitedExcept, with PKHeX's own pairing of the
            /// Legends: Arceus and BDSP flags kept as it is.
            bool hasVisitedExcept(std::initializer_list<Enums::GameVersion> contextGroups) const noexcept;

            /// PKHeX EvolutionHistory.AsSingle.
            EvolutionHistory asSingle(Enums::GameVersion contextGroup) const noexcept;

            /// PKHeX EvolutionHistory.PruneKeepPreEvolutions: per context, only the stages before
            /// `speciesId`.
            EvolutionHistory pruneKeepPreEvolutions(uint16_t speciesId) const noexcept;

        private:
            std::array<EvolutionChain, EVOLUTION_CONTEXT_COUNT> chains{};
        };
    }
}

#endif
