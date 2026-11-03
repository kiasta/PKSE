/**
 * The thirteen PKHeX EntityContexts the evolution engine walks, each named by the save-format group
 * Legality::getContextGroup() answers for it: RBY, GSC, RSE, DP, BW, XY, SM, GG, SWSH, PLA, BDSP,
 * SV and ZA. Any other value is "no context".
 */
#ifndef LEGALITY_EVOLUTIONS_EVOLUTIONCONTEXT_H
#define LEGALITY_EVOLUTIONS_EVOLUTIONCONTEXT_H

#include <array>

#include "Enums/GameVersion.h"

namespace Legality
{
    namespace Evolutions
    {
        constexpr int EVOLUTION_CONTEXT_COUNT = 13;

        /// PKHeX EntityContext order: Gen1-Gen9, then Gen7b, Gen8a, Gen8b, Gen9a.
        constexpr std::array<Enums::GameVersion, EVOLUTION_CONTEXT_COUNT> EVOLUTION_CONTEXT_GROUPS = {
            Enums::GameVersion::RBY,
            Enums::GameVersion::GSC,
            Enums::GameVersion::RSE,
            Enums::GameVersion::DP,
            Enums::GameVersion::BW,
            Enums::GameVersion::XY,
            Enums::GameVersion::SM,
            Enums::GameVersion::SWSH,
            Enums::GameVersion::SV,
            Enums::GameVersion::GG,
            Enums::GameVersion::PLA,
            Enums::GameVersion::BDSP,
            Enums::GameVersion::ZA,
        };

        /// Position of `contextGroup` in EVOLUTION_CONTEXT_GROUPS, or -1 when it names no context.
        /// Any save-format group is accepted and folded to its context (FRLG -> RSE, USUM -> SM...).
        int getEvolutionContextIndex(Enums::GameVersion contextGroup) noexcept;

        /// The context group a save-format group's Pokemon live in; Invalid for anything else.
        Enums::GameVersion getEvolutionContextGroup(Enums::GameVersion group) noexcept;

        /// PKHeX EntityContext.Generation.
        int getEvolutionContextGeneration(Enums::GameVersion contextGroup) noexcept;

        /// PKHeX's EntityContext name ("Gen1" ... "Gen9a"), for logs and the parity harness.
        const char *getEvolutionContextSourceName(Enums::GameVersion contextGroup) noexcept;
    }
}

#endif
