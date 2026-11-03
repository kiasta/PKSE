#include "Legality/Evolutions/EvolutionContext.h"

#include "Legality/Verifiers/HandlerRules.h"

namespace Legality
{
    namespace Evolutions
    {
        namespace
        {
            constexpr std::array<int, EVOLUTION_CONTEXT_COUNT> CONTEXT_GENERATIONS = {1, 2, 3, 4, 5, 6, 7, 8, 9, 7, 8, 8, 9};
            constexpr std::array<const char *, EVOLUTION_CONTEXT_COUNT> CONTEXT_SOURCE_NAMES = {"Gen1", "Gen2", "Gen3", "Gen4", "Gen5", "Gen6", "Gen7", "Gen8", "Gen9", "Gen7b", "Gen8a", "Gen8b", "Gen9a"};
        }

        int getEvolutionContextIndex(Enums::GameVersion contextGroup) noexcept
        {
            const Enums::GameVersion foldedGroup = getContextGroup(contextGroup);
            for (int contextIndex = 0; contextIndex < EVOLUTION_CONTEXT_COUNT; contextIndex++)
            {
                if (EVOLUTION_CONTEXT_GROUPS[contextIndex] == foldedGroup)
                {
                    return contextIndex;
                }
            }
            return -1;
        }

        Enums::GameVersion getEvolutionContextGroup(Enums::GameVersion group) noexcept
        {
            const int contextIndex = getEvolutionContextIndex(group);
            return contextIndex < 0 ? Enums::GameVersion::Invalid : EVOLUTION_CONTEXT_GROUPS[contextIndex];
        }

        int getEvolutionContextGeneration(Enums::GameVersion contextGroup) noexcept
        {
            const int contextIndex = getEvolutionContextIndex(contextGroup);
            return contextIndex < 0 ? 0 : CONTEXT_GENERATIONS[contextIndex];
        }

        const char *getEvolutionContextSourceName(Enums::GameVersion contextGroup) noexcept
        {
            const int contextIndex = getEvolutionContextIndex(contextGroup);
            return contextIndex < 0 ? "None" : CONTEXT_SOURCE_NAMES[contextIndex];
        }
    }
}
