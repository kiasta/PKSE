/**
 * Four move ids an encounter fixes. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Shared/Moveset.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_MOVESET_H
#define LEGALITY_ENCOUNTERS_MOVESET_H

#include <array>
#include <cstdint>

namespace Legality
{
    namespace Encounters
    {
        struct Moveset
        {
            std::array<uint16_t, 4> moveIds{};

            constexpr bool hasMoves() const noexcept { return moveIds[0] != 0; }
            constexpr bool contains(uint16_t moveId) const noexcept { return moveIds[0] == moveId || moveIds[1] == moveId || moveIds[2] == moveId || moveIds[3] == moveId; }
            constexpr bool operator==(const Moveset &other) const noexcept = default;
        };
    }
}

#endif
