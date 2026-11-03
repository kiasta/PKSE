/**
 * Legends: Arceus overworld spawns: the entity seed in xoroshiro128+ rolls the EC, a fake trainer,
 * up to `rollCount` PIDs for shininess, IVs, ability, gender, nature and size.
 *
 * Ported from PKHeX PKHeX.Core/Legality/RNG/Methods/Gen8a/Overworld8aRNG.cs (Verify and
 * GetRandomLevel; ApplyDetails builds an entity and is not ported).
 */
#ifndef LEGALITY_RNG_OVERWORLD8ARNG_H
#define LEGALITY_RNG_OVERWORLD8ARNG_H

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
        struct Overworld8aRNG
        {
            static bool verify(const Pokemon::Pokemon &pokemon, uint64_t seed, const OverworldParam8a &parameters, bool isFixedHeight = false, bool isFixedWeight = false) noexcept;

            /// The slot seed's third output picks the level in [levelMin, levelMax].
            static uint8_t getRandomLevel(uint64_t slotSeed, uint8_t levelMin, uint8_t levelMax) noexcept;
        };
    }
}

#endif
