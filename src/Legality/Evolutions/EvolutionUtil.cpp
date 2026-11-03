#include "Legality/Evolutions/EvolutionUtil.h"

#include <algorithm>

#include "Pokemon/PersonalRecord.h"

namespace Legality
{
    namespace Evolutions
    {
        namespace
        {
            void shiftDown(std::span<EvolutionCriteria> result)
            {
                for (size_t i = 1; i < result.size(); i++)
                {
                    result[i - 1] = result[i];
                }
                result.back() = EvolutionCriteria{};
            }

            // PKHeX EvolutionUtil.CleanEvolve
            int cleanEvolve(std::span<EvolutionCriteria> result)
            {
                size_t stageIndex = 1;
                for (; stageIndex < result.size(); stageIndex++)
                {
                    const EvolutionCriteria &next = result[stageIndex - 1];
                    const uint8_t newLevelMax = next.levelMax;
                    EvolutionCriteria &stage = result[stageIndex];
                    if (stage.levelMin > newLevelMax - next.levelUpRequired)
                    {
                        break;
                    }
                    stage.levelMax = newLevelMax;
                }
                return static_cast<int>(stageIndex);
            }

            uint16_t getAbilityAtIndex(const Pokemon::PersonalRecord &record, int abilityIndex)
            {
                switch (abilityIndex)
                {
                case 0:
                    return record.ability1;
                case 1:
                    return record.ability2;
                case 2:
                    return record.abilityHidden;
                default:
                    return 0;
                }
            }
        }

        // PKHeX PKHeX.Core/Legality/Evolutions/EvolutionGroup/EvolutionUtil.cs
        EvolutionChain setHistory(std::span<const EvolutionCriteria> result, const EvolutionTree &presenceTree) noexcept
        {
            size_t startIndex = 0;
            size_t presentCount = 0;
            for (const EvolutionCriteria &stage : result)
            {
                if (stage.method == EvolutionType::Invalid)
                {
                    startIndex += presentCount + 1;
                    presentCount = 0;
                }
                else if (presenceTree.isPresentInGame(stage.speciesId, stage.formId))
                {
                    presentCount++;
                }
                else if (presentCount == 0)
                {
                    startIndex++;
                }
                else
                {
                    break;
                }
            }

            EvolutionChain chain;
            if (presentCount == 0)
            {
                return chain;
            }
            for (size_t stageIndex = 0; stageIndex < presentCount; stageIndex++)
            {
                chain.stages[stageIndex] = result[startIndex + stageIndex];
            }
            chain.stageCount = static_cast<uint8_t>(cleanEvolve(std::span<EvolutionCriteria>(chain.stages.data(), presentCount)));
            return chain;
        }

        void discard(std::span<EvolutionCriteria> result, const EvolutionTree &presenceTree) noexcept
        {
            for (int i = 0; i < static_cast<int>(result.size()); i++)
            {
                const EvolutionCriteria stage = result[i];
                if (stage.speciesId == 0)
                {
                    break;
                }
                if (presenceTree.isPresentInGame(stage.speciesId, stage.formId))
                {
                    continue;
                }
                shiftDown(result.subspan(i));
                i--;
            }
        }

        void discardByAbility(std::span<EvolutionCriteria> result, const EvolutionTree &presenceTree, Enums::GameVersion personalGroup, uint16_t abilityId, int abilityIndex) noexcept
        {
            discard(result, presenceTree);

            uint32_t removalMask = 0;
            for (size_t i = 0; i < result.size(); i++)
            {
                const EvolutionCriteria stage = result[i];
                if (stage.speciesId == 0)
                {
                    break;
                }
                if (!presenceTree.isPresentInGame(stage.speciesId, stage.formId))
                {
                    removalMask |= 1u << i;
                    continue;
                }
                const Pokemon::PersonalRecord &record = Pokemon::getPersonalRecord(personalGroup, stage.speciesId, stage.formId);
                if (getAbilityAtIndex(record, abilityIndex) == abilityId)
                {
                    continue;
                }
                removalMask |= 1u << i;
            }

            if (removalMask == 0 || removalMask == (1u << result.size()) - 1)
            {
                return;
            }
            for (int i = 0; i < static_cast<int>(result.size()); i++)
            {
                if ((removalMask & 1u) == 0u)
                {
                    removalMask >>= 1;
                    continue;
                }
                shiftDown(result.subspan(i));
                removalMask >>= 1;
                i--;
            }
        }

        int indexOfSpecies(std::span<const EvolutionCriteria> result, uint16_t speciesId) noexcept
        {
            for (size_t i = 0; i < result.size(); i++)
            {
                if (result[i].speciesId == speciesId)
                {
                    return static_cast<int>(i);
                }
            }
            return -1;
        }

        void updateCeiling(std::span<EvolutionCriteria> result, uint8_t level) noexcept
        {
            for (EvolutionCriteria &stage : result)
            {
                if (stage.speciesId == 0)
                {
                    break;
                }
                stage.levelMax = std::min(stage.levelMax, level);
            }
        }

        void updateFloor(std::span<EvolutionCriteria> result, uint8_t levelMin, uint8_t levelMax) noexcept
        {
            result[0].levelMax = levelMax;
            for (EvolutionCriteria &stage : result)
            {
                if (stage.speciesId == 0)
                {
                    break;
                }
                stage.levelMin = std::max(stage.levelMin, levelMin);
            }
        }

        void conditionBaseChainForward(std::span<EvolutionCriteria> result, uint16_t encounterSpeciesId) noexcept
        {
            for (EvolutionCriteria &stage : result)
            {
                if (stage.speciesId == encounterSpeciesId)
                {
                    break;
                }
                stage.method = EvolutionType::Invalid;
            }
        }

        void cleanDevolve(std::span<EvolutionCriteria> result, uint8_t levelMin) noexcept
        {
            // Trickle the evolving minimum up the chain, as if evolving from the lowest stage.
            uint8_t levelUpRequired = 0;
            EvolutionType method = EvolutionType::None;
            for (int i = static_cast<int>(result.size()) - 1; i >= 0; i--)
            {
                EvolutionCriteria &stage = result[i];
                const uint8_t nextLevelMin = stage.levelMin;
                const uint8_t nextLevelUpRequired = stage.levelUpRequired;
                const EvolutionType nextMethod = stage.method;
                stage.levelMin = std::min(stage.levelMax, levelMin);
                stage.levelUpRequired = levelUpRequired;
                stage.method = method;
                levelMin = std::max(nextLevelMin, levelMin);
                levelUpRequired = nextLevelUpRequired;
                method = nextMethod;
            }
        }

        void conditionEncounterNoMet(std::span<EvolutionCriteria> chain) noexcept
        {
            // An evolved encounter can be under the evolution level (a level 5 Silcoon slot).
            for (int i = 0; i < static_cast<int>(chain.size()) - 1; i++)
            {
                EvolutionCriteria &stage = chain[i];
                if (isLevelUpRequired(stage.method))
                {
                    stage.levelMin = static_cast<uint8_t>(chain[i + 1].levelMin + stage.levelUpRequired);
                }
            }
        }
    }
}
