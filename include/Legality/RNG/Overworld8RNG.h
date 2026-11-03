/**
 * Sword/Shield overworld (and other fixed-seed) Pokemon: a 32-bit seed in xoroshiro128+ rolls the
 * EC, PID, IVs and size in turn, and since the EC is the first output the seed can be recovered.
 *
 * Ported from PKHeX PKHeX.Core/Legality/RNG/Methods/Gen8/Overworld8RNG.cs (the verification half;
 * ApplyDetails builds an entity and is not ported).
 */
#ifndef LEGALITY_RNG_OVERWORLD8RNG_H
#define LEGALITY_RNG_OVERWORLD8RNG_H

#include <cstdint>

#include "Legality/RNG/GenerateParameters.h"

namespace Pokemon
{
    class Pokemon;
}

namespace Legality
{
    namespace RNG
    {
        struct Overworld8RNG
        {
            /// `flawless` -1 tries 0, 2 and 3 guaranteed perfect IVs.
            static bool validateOverworldEncounter(const Pokemon::Pokemon &pokemon, Shiny shiny = Shiny::FixedValue, int flawless = -1) noexcept;
            static bool validateOverworldEncounter(const Pokemon::Pokemon &pokemon, uint32_t seed, Shiny shiny = Shiny::FixedValue, int flawless = -1) noexcept;

            static uint32_t getOriginalSeed(const Pokemon::Pokemon &pokemon) noexcept;
        };
    }
}

#endif
