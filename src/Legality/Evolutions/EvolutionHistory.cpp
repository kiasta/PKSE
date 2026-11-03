#include "Legality/Evolutions/EvolutionHistory.h"

namespace Legality
{
    namespace Evolutions
    {
        namespace
        {
            const EvolutionChain EMPTY_CHAIN{};

            bool containsContext(std::initializer_list<Enums::GameVersion> contextGroups, Enums::GameVersion contextGroup)
            {
                for (const Enums::GameVersion listedGroup : contextGroups)
                {
                    if (getEvolutionContextGroup(listedGroup) == contextGroup)
                    {
                        return true;
                    }
                }
                return false;
            }

            // PKHeX EvolutionHistory.PruneKeepPreEvolutions(EvoCriteria[], species)
            EvolutionChain pruneChain(const EvolutionChain &chain, uint16_t speciesId)
            {
                int speciesIndex = -1;
                for (int stageIndex = 0; stageIndex < chain.stageCount; stageIndex++)
                {
                    if (chain.stages[stageIndex].speciesId == speciesId)
                    {
                        speciesIndex = stageIndex;
                        break;
                    }
                }
                if (speciesIndex < 0)
                {
                    return chain;
                }
                EvolutionChain prunedChain;
                for (int stageIndex = speciesIndex + 1; stageIndex < chain.stageCount; stageIndex++)
                {
                    prunedChain.stages[prunedChain.stageCount++] = chain.stages[stageIndex];
                }
                return prunedChain;
            }
        }

        // PKHeX PKHeX.Core/Legality/Evolutions/EvolutionHistory.cs
        std::span<const EvolutionCriteria> EvolutionHistory::getChain(Enums::GameVersion contextGroup) const noexcept
        {
            return getChainRecord(contextGroup).view();
        }

        const EvolutionChain &EvolutionHistory::getChainRecord(Enums::GameVersion contextGroup) const noexcept
        {
            const int contextIndex = getEvolutionContextIndex(contextGroup);
            return contextIndex < 0 ? EMPTY_CHAIN : chains[contextIndex];
        }

        void EvolutionHistory::setChain(Enums::GameVersion contextGroup, const EvolutionChain &chain) noexcept
        {
            const int contextIndex = getEvolutionContextIndex(contextGroup);
            if (contextIndex >= 0)
            {
                chains[contextIndex] = chain;
            }
        }

        bool EvolutionHistory::hasVisitedExcept(std::initializer_list<Enums::GameVersion> contextGroups) const noexcept
        {
            using Enums::GameVersion;
            // PKHeX tests the BDSP flag against Gen8a and the Legends: Arceus flag against Gen8b.
            const auto isVisitedOutside = [&](GameVersion visitedGroup, GameVersion exceptGroup)
            {
                return hasVisited(visitedGroup) && !containsContext(contextGroups, exceptGroup);
            };
            return isVisitedOutside(GameVersion::ZA, GameVersion::ZA) || isVisitedOutside(GameVersion::SV, GameVersion::SV) || isVisitedOutside(GameVersion::BDSP, GameVersion::PLA) || isVisitedOutside(GameVersion::PLA, GameVersion::BDSP) || isVisitedOutside(GameVersion::SWSH, GameVersion::SWSH) || isVisitedOutside(GameVersion::GG, GameVersion::GG) || isVisitedOutside(GameVersion::SM, GameVersion::SM) || isVisitedOutside(GameVersion::XY, GameVersion::XY) || isVisitedOutside(GameVersion::BW, GameVersion::BW) || isVisitedOutside(GameVersion::DP, GameVersion::DP) || isVisitedOutside(GameVersion::RSE, GameVersion::RSE) || isVisitedOutside(GameVersion::GSC, GameVersion::GSC) || isVisitedOutside(GameVersion::RBY, GameVersion::RBY);
        }

        EvolutionHistory EvolutionHistory::asSingle(Enums::GameVersion contextGroup) const noexcept
        {
            EvolutionHistory singleHistory;
            singleHistory.setChain(contextGroup, getChainRecord(contextGroup));
            return singleHistory;
        }

        EvolutionHistory EvolutionHistory::pruneKeepPreEvolutions(uint16_t speciesId) const noexcept
        {
            EvolutionHistory prunedHistory;
            for (int contextIndex = 0; contextIndex < EVOLUTION_CONTEXT_COUNT; contextIndex++)
            {
                prunedHistory.chains[contextIndex] = pruneChain(chains[contextIndex], speciesId);
            }
            return prunedHistory;
        }
    }
}
