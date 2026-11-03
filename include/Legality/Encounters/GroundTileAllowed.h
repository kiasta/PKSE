/**
 * The ground tiles a Gen 4 encounter can be met on, as a bit per tile type. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen4/GroundTileAllowed.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_GROUNDTILEALLOWED_H
#define LEGALITY_ENCOUNTERS_GROUNDTILEALLOWED_H

#include <cstdint>

namespace Legality
{
    namespace Encounters
    {
        constexpr uint32_t GROUND_TILE_UNDEFINED = 0;
        constexpr uint32_t GROUND_TILE_NONE = 1u << 0;
        constexpr uint32_t GROUND_TILE_SAND = 1u << 1;
        constexpr uint32_t GROUND_TILE_GRASS = 1u << 2;
        constexpr uint32_t GROUND_TILE_PUDDLE = 1u << 3;
        constexpr uint32_t GROUND_TILE_ROCK = 1u << 4;
        constexpr uint32_t GROUND_TILE_CAVE = 1u << 5;
        constexpr uint32_t GROUND_TILE_SNOW = 1u << 6;
        constexpr uint32_t GROUND_TILE_WATER = 1u << 7;
        constexpr uint32_t GROUND_TILE_ICE = 1u << 8;
        constexpr uint32_t GROUND_TILE_BUILDING = 1u << 9;
        constexpr uint32_t GROUND_TILE_MARSH = 1u << 10;
        constexpr uint32_t GROUND_TILE_BRIDGE = 1u << 11;
        /// Diamond/Pearl's catch-all for an undefined tile.
        constexpr uint32_t GROUND_TILE_MAX_DP = 1u << 12;
        constexpr uint32_t GROUND_TILE_DISTORTION = 1u << 23;
        /// Platinum's catch-all for an undefined tile.
        constexpr uint32_t GROUND_TILE_MAX_PT = 1u << 24;

        /// PKHeX GroundTileType values the matcher names.
        constexpr uint8_t GROUND_TILE_TYPE_GRASS = 2;
        constexpr uint8_t GROUND_TILE_TYPE_WATER = 7;

        /// PKHeX GroundTileAllowedExtensions.Contains: the stored tile type is one of `permit`.
        constexpr bool isGroundTileAllowed(uint32_t permit, uint8_t tileType) noexcept
        {
            return tileType < 32 && (permit & (1u << tileType)) != 0;
        }
    }
}

#endif
