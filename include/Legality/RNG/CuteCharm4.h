/**
 * Gen 4 Cute Charm: with a Cute Charm lead the game skips the PID correlation and picks an 8-bit
 * PID that gives the opposite gender and the synchronised nature.
 *
 * Ported from PKHeX PKHeX.Core/Legality/RNG/ClassicEra/Gen4/CuteCharm4.cs.
 */
#ifndef LEGALITY_RNG_CUTECHARM4_H
#define LEGALITY_RNG_CUTECHARM4_H

#include <cstdint>

namespace Pokemon
{
    class Pokemon;
}

namespace Legality
{
    namespace RNG
    {
        struct CuteCharm4
        {
            /// The lowest male PID with `natureId` for the gender ratio.
            static uint32_t getPIDMale(uint8_t genderRatio, uint32_t natureId) noexcept;

            static constexpr uint32_t getPIDFemale(uint32_t natureId) noexcept { return natureId; }

            /// PKHeX IsValid: a Marill/Azumarill with a male Azurill PID must come from an Azurill
            /// encounter; a genderless Pokemon only as a Shedinja.
            static bool isValid(uint16_t encounterSpeciesId, const Pokemon::Pokemon &pokemon) noexcept;

            static bool isCuteCharm(const Pokemon::Pokemon &pokemon, uint32_t PID) noexcept;
        };
    }
}

#endif
