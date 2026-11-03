/**
 * Which moves a freshly hatched egg can know, and in what order: the base moves of the hatched
 * species, then the moves its parents passed down (egg moves, level-up moves, TMs, Crystal's
 * tutors, Pichu's Volt Tackle). Ported from PKHeX PKHeX.Core/Legality/Moves/Breeding (MoveBreed,
 * MoveBreed2 ... MoveBreed6, EggSource).
 */
#ifndef LEGALITY_LEARNSOURCE_MOVEBREED_H
#define LEGALITY_LEARNSOURCE_MOVEBREED_H

#include <cstdint>
#include <span>

#include "Enums/GameVersion.h"
#include "Legality/LearnSource/MoveResult.h"

namespace Legality
{
    namespace LearnSource
    {
        /// PKHeX MoveBreed.Validate: true when `moves` is an order an egg can hatch with. `origins`
        /// receives each slot's EggSource value (per generation); like PKHeX, slots the search does
        /// not reach keep what they held.
        bool validateBreedMoves(uint8_t generation, uint16_t speciesId, uint8_t formId, Enums::GameVersion version, std::span<const uint16_t, 4> moves, std::span<uint8_t, 4> origins);

        /// PKHeX MoveBreed.GetExpectedMoves(moves, enc, result): `moves` when valid, otherwise the
        /// closest valid order (or the base moves when none is).
        bool getExpectedBreedMoves(uint8_t generation, uint16_t speciesId, uint8_t formId, Enums::GameVersion version, std::span<const uint16_t, 4> moves, std::span<uint16_t, 4> result);

        /// PKHeX EggSourceUtil.GetSource.
        LearnMethod getEggSourceMethod(uint8_t origin, uint8_t generation) noexcept;

        /// PKHeX EggStateLegality.GetEggLevel: Gen 2 and 3 eggs hatch at 5, later ones at 1.
        constexpr uint8_t getEggLevel(uint8_t generation) noexcept { return generation >= 4 ? 1 : 5; }
    }
}

#endif
