/**
 * Legends: Z-A seeded Pokemon: a seed in xoroshiro128+ rolls the EC, a fake trainer id, the PID, IVs,
 * ability, gender, nature and scale. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen9a/LumioseRNG.cs and GenerateParam9a.cs (IsMatch and
 * its helpers; TryApply64/GenerateData build an entity and are not ported).
 */
#ifndef LEGALITY_RNG_LUMIOSERNG_H
#define LEGALITY_RNG_LUMIOSERNG_H

#include <cstdint>

#include "Legality/RNG/GenerateParameters.h"
#include "Legality/RNG/LumioseCorrelation.h"

namespace Pokemon
{
    class Pokemon;
}

namespace Legality
{
    namespace RNG
    {
        /// PKHeX GenerateParam9a: what an encounter fixes before the seed rolls the rest.
        struct GenerateParam9a
        {
            uint8_t genderRatio = 0;
            uint8_t flawlessIVs = 0;
            uint8_t rollCount = 1;
            LumioseCorrelation correlation = LumioseCorrelation::Normal;
            SizeType9 sizeType = SizeType9::RANDOM;
            uint8_t scale = 0;
            /// A nature id, or RANDOM_NATURE.
            uint8_t nature = RANDOM_NATURE;
            AbilityPermission ability = AbilityPermission::Any12;
            Shiny shiny = Shiny::Random;
            IndividualValueSet individualValues{};
        };

        struct LumioseRNG
        {
            /// PKHeX LumioseRNG.IsMatch (and Verify): `seed` regenerates the Pokemon.
            static bool isMatch(const Pokemon::Pokemon &pokemon, const GenerateParam9a &parameters, uint64_t seed) noexcept;

            /// PKHeX LumioseRNG.AntiShiny: the fake trainer's anti-shiny flip.
            static constexpr uint32_t antiShiny(uint32_t PID) noexcept { return PID ^ 0x10000000u; }
        };
    }
}

#endif
