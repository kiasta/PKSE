/**
 * Species classifications the encounter templates read. Ported from PKHeX
 * PKHeX.Core/Legality/Tables/SpeciesCategory.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_SPECIESCATEGORY_H
#define LEGALITY_ENCOUNTERS_SPECIESCATEGORY_H

#include <cstdint>

namespace Legality
{
    namespace Encounters
    {
        struct SpeciesCategory
        {
            /// PKHeX IsMythical: a Mythical distribution species.
            static bool isMythical(uint16_t speciesId) noexcept;
            /// PKHeX IsLegendary: the games' "Legend" class.
            static bool isLegendary(uint16_t speciesId) noexcept;
            /// PKHeX IsSubLegendary: the games' "SubLegend" class.
            static bool isSubLegendary(uint16_t speciesId) noexcept;
            static bool isUltraBeast(uint16_t speciesId) noexcept;
            static bool isParadox(uint16_t speciesId) noexcept;
            /// PKHeX IsSpecialPokemon: any of the classes above.
            static bool isSpecialPokemon(uint16_t speciesId) noexcept;
        };
    }
}

#endif
