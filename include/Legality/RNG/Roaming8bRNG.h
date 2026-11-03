/**
 * Brilliant Diamond/Shining Pearl roamers (Mesprit, Cresselia): the EC is the 32-bit seed of the
 * Unity xoroshiro128+ that rolls the PID, IVs, ability, gender, nature and size.
 *
 * Ported from PKHeX PKHeX.Core/Legality/RNG/Methods/Gen8b/Roaming8bRNG.cs (the verification half;
 * ApplyDetails builds an entity and is not ported).
 */
#ifndef LEGALITY_RNG_ROAMING8BRNG_H
#define LEGALITY_RNG_ROAMING8BRNG_H

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
        struct Roaming8bRNG
        {
            static bool validateRoamingEncounter(const Pokemon::Pokemon &pokemon, Shiny shiny = Shiny::Random, int flawless = 0) noexcept;
        };
    }
}

#endif
