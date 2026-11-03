#include "Legality/Evolutions/EvolutionChainSearch.h"

#include <algorithm>
#include <array>

#include "Legality/Evolutions/EvolutionContext.h"
#include "Legality/Evolutions/EvolutionGroup.h"
#include "Legality/Evolutions/EvolutionUtil.h"

namespace Legality
{
    namespace Evolutions
    {
        namespace
        {
            using Enums::GameVersion;
            using WorkingChain = std::array<EvolutionCriteria, MAX_EVOLUTION_STAGES>;

            constexpr uint16_t LOCATION_NONE_8BDSP = 65535;

            /// PKHeX ICaughtData2.MetLevel: zero unless the record is a Crystal one with caught data.
            uint8_t getCaughtMetLevel(const EvolutionEntity &entity)
            {
                return entity.hasCaughtDataField ? entity.metLevel : 0;
            }

            // PKHeX EvolutionChain.GetMinLevel
            uint8_t getMinLevel(const EvolutionEntity &entity, const EvolutionEncounter &encounter)
            {
                if (encounter.generation == 2)
                {
                    return entity.hasCaughtDataField ? std::max(entity.metLevel, encounter.levelMin) : encounter.levelMin;
                }
                if (encounter.generation <= 4 && entity.formatGeneration != encounter.generation)
                {
                    return encounter.levelMin;
                }
                return std::max(entity.metLevel, encounter.levelMin);
            }

            // PKHeX EvolutionChain.IsMetLost
            bool isMetLost(const EvolutionEntity &entity, const EvolutionOrigin &origin)
            {
                if (origin.generation >= 5)
                {
                    return false;
                }
                if (origin.generation <= 2)
                {
                    return getCaughtMetLevel(entity) == 0;
                }
                return origin.generation != entity.formatGeneration;
            }

            int countStages(std::span<const EvolutionCriteria> result)
            {
                int stageCount = 0;
                for (const EvolutionCriteria &stage : result)
                {
                    if (stage.speciesId == 0)
                    {
                        break;
                    }
                    stageCount++;
                }
                return stageCount;
            }

            // PKHeX EvolutionChain.DevolveFrom
            int devolveFrom(std::span<EvolutionCriteria> result, const EvolutionEntity &entity, const EvolutionOrigin &origin, uint16_t encounterSpeciesId, bool shouldDiscard)
            {
                const EvolutionGroup *group = getEvolutionGroup(getEntityContextGroup(entity));
                if (!group)
                {
                    return 0;
                }
                while (true)
                {
                    group->devolve(result, entity, origin);
                    const EvolutionGroup *previous = group->getPrevious(entity, origin);
                    if (!previous)
                    {
                        break;
                    }
                    group = previous;
                }
                if (shouldDiscard)
                {
                    group->discardForOrigin(result, entity, origin);
                }
                if (encounterSpeciesId != 0)
                {
                    return indexOfSpecies(result, encounterSpeciesId) + 1;
                }
                return countStages(result);
            }

            // PKHeX EvolutionChain.GetOriginChain (the span form)
            int fillOriginChain(std::span<EvolutionCriteria> result, const EvolutionEntity &entity, const EvolutionOrigin &origin, uint16_t encounterSpeciesId, bool shouldDiscard)
            {
                if (entity.isEgg && !origin.shouldSkipChecks())
                {
                    result[0] = EvolutionCriteria{origin.speciesId, entity.formId, 0, origin.levelMax, origin.levelMax, EvolutionType::None};
                    return 1;
                }
                result[0] = EvolutionCriteria{origin.speciesId, entity.formId, 0, origin.levelMax, 0, EvolutionType::None};
                const int stageCount = devolveFrom(result, entity, origin, encounterSpeciesId, shouldDiscard);
                cleanDevolve(result.first(stageCount), origin.levelMin);
                return stageCount;
            }

            EvolutionOrigin normalizeOrigin(const EvolutionOrigin &requestedOrigin)
            {
                EvolutionOrigin origin = requestedOrigin;
                origin.contextGroup = getEvolutionContextGroup(requestedOrigin.contextGroup);
                return origin;
            }

            EvolutionChain toChain(std::span<const EvolutionCriteria> stages)
            {
                EvolutionChain chain;
                for (const EvolutionCriteria &stage : stages)
                {
                    chain.stages[chain.stageCount++] = stage;
                }
                return chain;
            }

            // PKHeX EncounterOrigin.IsEggLocationNonZero
            bool isEggLocationNonZero(const EvolutionEntity &entity)
            {
                const uint16_t noneLocation = getEntityContextGroup(entity) == GameVersion::BDSP ? LOCATION_NONE_8BDSP : 0;
                return entity.eggLocation != noneLocation;
            }

            // PKHeX EncounterOrigin.GetLevelOriginMin
            uint8_t getLevelOriginMin(const EvolutionEntity &entity, uint8_t generation)
            {
                if (generation == 3)
                {
                    constexpr uint8_t eggLevel = 5;
                    constexpr uint8_t minWildLevel = 2;
                    if (entity.formatGeneration != 3)
                    {
                        return minWildLevel;
                    }
                    return entity.isEgg ? eggLevel : entity.metLevel;
                }
                if (generation == 4)
                {
                    constexpr uint8_t eggLevel = 1;
                    constexpr uint8_t minWildLevel = 2;
                    if (entity.formatGeneration != 4)
                    {
                        return isEggLocationNonZero(entity) ? eggLevel : minWildLevel;
                    }
                    return entity.isEgg || isEggLocationNonZero(entity) ? eggLevel : entity.metLevel;
                }
                return std::max(static_cast<uint8_t>(1), entity.metLevel);
            }

            // PKHeX EncounterOrigin.GetLevelOriginMinGB
            uint8_t getLevelOriginMinGameBoy(const EvolutionEntity &entity)
            {
                constexpr uint8_t eggLevel = 5;
                constexpr uint8_t minWildLevel = 2;
                if (entity.isEgg)
                {
                    return eggLevel;
                }
                if (!entity.hasCaughtDataField || entity.caughtData == 0)
                {
                    return minWildLevel;
                }
                return entity.metLevel;
            }
        }

        // PKHeX EncounterInvalid(PKM)
        EvolutionEncounter getInvalidEvolutionEncounter(const EvolutionEntity &entity) noexcept
        {
            return EvolutionEncounter{entity.speciesId, getEntityContextGroup(entity), static_cast<uint8_t>(entity.originGeneration), entity.metLevel, true};
        }

        // PKHeX PKHeX.Core/Legality/Evolutions/EvolutionChain.cs
        EvolutionHistory getEvolutionChainsAllGens(const EvolutionEntity &entity, const EvolutionEncounter &encounter) noexcept
        {
            const uint8_t levelMin = getMinLevel(entity, encounter);
            const EvolutionOrigin origin{entity.speciesId, encounter.contextGroup, encounter.generation, levelMin, entity.currentLevel, ORIGIN_OPTION_NONE};
            if (!entity.isEgg && !encounter.isInvalid)
            {
                return getEvolutionChainsSearch(entity, origin, encounter.contextGroup, encounter.speciesId);
            }
            return getEvolutionChainsSearch(entity, origin, getEntityContextGroup(entity), encounter.speciesId);
        }

        EvolutionHistory getEvolutionChainsSearch(const EvolutionEntity &entity, const EvolutionOrigin &requestedOrigin, Enums::GameVersion contextGroup, uint16_t encounterSpeciesId) noexcept
        {
            const EvolutionOrigin origin = normalizeOrigin(requestedOrigin);
            EvolutionHistory history;
            WorkingChain workingChain{};
            const int stageCount = fillOriginChain(workingChain, entity, origin, encounterSpeciesId, origin.isDiscardRequired(entity.formatGeneration));
            if (stageCount <= 0)
            {
                return history;
            }
            const std::span<EvolutionCriteria> chain(workingChain.data(), stageCount);

            // Stages after the encountered species are unknown until evolved into.
            if (encounterSpeciesId != 0)
            {
                conditionBaseChainForward(chain, encounterSpeciesId);
            }
            GameVersion context = getEvolutionContextGroup(contextGroup);
            if (context == GameVersion::GSC)
            {
                getEvolutionGroup(GameVersion::GSC)->evolve(chain, entity, origin, history);
                getEvolutionGroup(GameVersion::RBY)->evolve(chain, entity, origin, history);
                if (entity.formatGeneration <= 2)
                {
                    return history;
                }
                context = GameVersion::SM;
            }

            const EvolutionGroup *group = getEvolutionGroup(context);
            while (group)
            {
                group->evolve(chain, entity, origin, history);
                group = group->getNext(entity, origin);
            }
            return history;
        }

        EvolutionChain getOriginChain(const EvolutionEntity &entity, const EvolutionOrigin &requestedOrigin, uint16_t encounterSpeciesId, bool shouldDiscard) noexcept
        {
            const EvolutionOrigin origin = normalizeOrigin(requestedOrigin);
            WorkingChain workingChain{};
            const int stageCount = fillOriginChain(workingChain, entity, origin, encounterSpeciesId, shouldDiscard);
            if (stageCount <= 0)
            {
                return EvolutionChain{};
            }
            const std::span<EvolutionCriteria> chain(workingChain.data(), stageCount);
            // The original met level is lost, so an under-levelled evolved encounter must still match.
            if (isMetLost(entity, origin))
            {
                conditionEncounterNoMet(chain);
            }
            return toChain(chain);
        }

        // PKHeX PKHeX.Core/Legality/Evolutions/EncounterOrigin.cs
        EvolutionChain getEncounterOriginChain(const EvolutionEntity &entity, uint8_t generation, Enums::GameVersion contextGroup) noexcept
        {
            const EvolutionOrigin origin{entity.speciesId, getEvolutionContextGroup(contextGroup), generation, getLevelOriginMin(entity, generation), entity.currentLevel, ORIGIN_OPTION_NONE};
            return getOriginChain(entity, origin);
        }

        EvolutionChain getEncounterOriginChainGameBoy(const EvolutionEntity &entity, uint8_t generation, Enums::GameVersion contextGroup) noexcept
        {
            const EvolutionOrigin origin{entity.speciesId, getEvolutionContextGroup(contextGroup), generation, getLevelOriginMinGameBoy(entity), entity.currentLevel, ORIGIN_OPTION_NONE};
            return getOriginChain(entity, origin);
        }
    }
}
