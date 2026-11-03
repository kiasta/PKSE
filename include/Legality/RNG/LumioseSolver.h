/**
 * Recovers the seed a Legends: Z-A Pokemon was generated from, out of its EC and PID. Ported from
 * PKHeX PKHeX.Core/Legality/Encounters/Templates/Gen9a/LumioseSolver.cs.
 *
 * PKHeX's brute-force searches for a shiny Pokemon (SearchShiny1, SearchShinyN) are off by default
 * and legality never turns them on, so only the direct EC/PID reversal is ported.
 */
#ifndef LEGALITY_RNG_LUMIOSESOLVER_H
#define LEGALITY_RNG_LUMIOSESOLVER_H

#include <cstdint>

#include "Legality/RNG/LumioseRNG.h"

namespace Pokemon
{
    class Pokemon;
}

namespace Legality
{
    namespace RNG
    {
        struct LumioseSolver
        {
            /// PKHeX LumioseSolver.SearchShiny1 / SearchShinyN, at their default (off).
            static constexpr bool SEARCH_SHINY_SINGLE_ROLL = false;
            static constexpr bool SEARCH_SHINY_MANY_ROLLS = false;

            /// PKHeX LumioseSolver.TryGetSeed.
            static bool tryGetSeed(const GenerateParam9a &parameters, const Pokemon::Pokemon &pokemon, uint64_t &seed) noexcept;
        };
    }
}

#endif
