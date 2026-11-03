/**
 * The Pokemon HOME behaviors the Gen 8/9 seed checks have to accept.
 *
 * Ported from PKHeX PKHeX.Core/Legality/Restrictions/HomeQuirks.cs (the parts the RNG methods read).
 */
#ifndef LEGALITY_RNG_HOMEQUIRKS_H
#define LEGALITY_RNG_HOMEQUIRKS_H

#include <cstdint>

namespace Pokemon
{
    class Pokemon;
}

namespace Legality
{
    namespace RNG
    {
        struct HomeQuirks
        {
            /// HOME 4.0.0 copies Height into an unset Scale, and a 0 Height zeroes Weight too.
            static bool hasEnteredSetZeroScale(uint16_t species, uint8_t form) noexcept;
            /// Precondition: height and weight are both 0 and break the seed's correlation.
            static bool hasEnteredSetZeroScale(const Pokemon::Pokemon &pokemon) noexcept;
            static bool isTouchedScaleCopiedOrUntouched(const Pokemon::Pokemon &pokemon, uint8_t expectedScale) noexcept;
            /// PKHeX IScaledSize: the formats that store height and weight scalars.
            static bool hasSizeScalars(const Pokemon::Pokemon &pokemon) noexcept;
            /// PKHeX HasEnteredSetAlphaMark: an Alpha that went through HOME 3.0.0 or later carries the
            /// Alpha Mark. False for a Legends: Arceus Pokemon still in a Gen 8 format (it may have moved
            /// before 3.0.0) and for a Legends: Z-A one HOME has not rescaled yet.
            static bool hasEnteredSetAlphaMark(const Pokemon::Pokemon &pokemon) noexcept;
        };
    }
}

#endif
