/**
 * Colosseum/XD seed recovery: the starters (made right after the trainer ids), the trainer ids
 * themselves (reachable from the name screen), and the shared PID helpers.
 *
 * Ported from PKHeX PKHeX.Core/Legality/RNG/CXD/MethodCXD.cs, without the SetFromIVs / SetRandom /
 * SetStarter* family, which builds Pokemon for PKHeX's encounter generator. `firstIVs` is HP,
 * Attack and Defense (15 bits), `secondIVs` Speed, Special Attack and Special Defense.
 */
#ifndef LEGALITY_RNG_METHODCXD_H
#define LEGALITY_RNG_METHODCXD_H

#include <cstdint>

namespace Pokemon
{
    class Pokemon;
}

namespace Legality
{
    namespace RNG
    {
        struct MethodCXD
        {
            /// The first Colosseum starter PID from `seed` that is a male Eevee and not shiny.
            static uint32_t getColoStarterPID(uint32_t &seed, uint16_t TID16, uint16_t SID16) noexcept;

            /// Umbreon is the first starter, Espeon the second.
            static bool tryGetSeedStarterColo(uint32_t firstIVs, uint32_t secondIVs, uint32_t expectedPID, uint16_t TID16, uint16_t SID16, uint16_t speciesId, uint32_t &result) noexcept;

            static bool tryGetSeedStarterXD(uint32_t firstIVs, uint32_t secondIVs, uint32_t expectedPID, uint16_t TID16, uint16_t SID16, uint32_t &result) noexcept;

            static bool tryGetSeedStarterColo(const Pokemon::Pokemon &pokemon, uint32_t &result) noexcept;

            static bool tryGetSeedStarterXD(const Pokemon::Pokemon &pokemon, uint32_t &result) noexcept;

            /// The name screen spawns a ball 10% of frames (4 calls); the seed must be landable with
            /// four quiet frames in a row, or be catapulted to by a ball.
            static bool isValidNameScreenEndSeed(uint32_t seed, uint32_t &origin) noexcept;

            static bool tryGetSeedTrainerID(uint16_t TID16, uint16_t SID16, uint32_t &origin) noexcept;

            /// A SID whose xor with `xorValue` keeps the low 3 bits `bits` (cycling from it) and
            /// that the name screen can reach.
            static bool tryGetShinySID(uint16_t TID16, uint16_t &SID16, uint32_t xorValue, uint32_t bits) noexcept;
        };
    }
}

#endif
