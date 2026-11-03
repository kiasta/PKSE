/**
 * A level-up learnset (moves with the level each is learned at) and Stadium 2's move reminder list,
 * as views into Pokemon/LearnSourceTable. Ported from PKHeX PKHeX.Core/Legality/Learnset
 * (Learnset.cs, LearnsetStadium.cs).
 */
#ifndef LEGALITY_LEARNSOURCE_LEARNSET_H
#define LEGALITY_LEARNSOURCE_LEARNSET_H

#include <cstdint>
#include <span>

namespace Legality
{
    namespace LearnSource
    {
        /// PKHeX Learnset. Level 0 marks an evolution move, which the games list first.
        struct Learnset
        {
            std::span<const uint16_t> moves;
            std::span<const uint8_t> levels;

            /// PKHeX GetIsLearn.
            bool isLearn(uint16_t moveId) const noexcept;
            /// PKHeX TryGetLevelLearnMove: the level of the move's first entry.
            bool tryGetLevelLearnMove(uint16_t moveId, uint8_t &level) const noexcept;
            /// PKHeX GetBaseEggMoves: the last four moves learned at or below `level`.
            std::span<const uint16_t> getBaseEggMoves(uint8_t level) const noexcept;
            /// PKHeX SetEncounterMoves: the four moves a Pokemon generated at `level` knows, rotating
            /// the oldest out as the games do. `startIndex` is where the next move is written.
            void setEncounterMoves(uint8_t level, std::span<uint16_t, 4> encounterMoves, int startIndex = 0) const noexcept;
            /// PKHeX SetEncounterMovesBackwards: fill from `startIndex` with the most recent moves learned
            /// at or below `level`, newest first; moves sharing a level keep their listed order.
            void setEncounterMovesBackwards(uint8_t level, std::span<uint16_t, 4> encounterMoves, int startIndex = 0) const noexcept;
        };

        /// PKHeX LearnMethodStadium2: how Stadium 2 knows a species can hold a move.
        enum StadiumMoveSource : uint8_t
        {
            STADIUM_SOURCE_LEVEL_UP_RB = 1 << 0,
            STADIUM_SOURCE_LEVEL_UP_YW = 1 << 1,
            STADIUM_SOURCE_LEVEL_UP_GS = 1 << 2,
            STADIUM_SOURCE_LEVEL_UP_C = 1 << 3,
            STADIUM_SOURCE_TUTOR_C = 1 << 4,
            STADIUM_SOURCE_EGG_C = 1 << 5,
            STADIUM_SOURCE_EGG_GS = 1 << 6,
            STADIUM_SOURCE_EVENT = 1 << 7,
            /// PKHeX IsAbleToBeRelearned: the sources Stadium 2's reminder teaches from.
            STADIUM_SOURCE_RELEARNABLE = STADIUM_SOURCE_LEVEL_UP_RB | STADIUM_SOURCE_LEVEL_UP_YW | STADIUM_SOURCE_LEVEL_UP_GS | STADIUM_SOURCE_LEVEL_UP_C,
        };

        /// PKHeX LearnsetStadium: (level, move, source) triples sorted by level.
        struct StadiumLearnset
        {
            std::span<const uint8_t> tuples;

            /// PKHeX CanRelearn: the reminder teaches `moveId` at or below `level`; `criteriaLevel`
            /// is the lowest level it does.
            bool canRelearn(uint16_t moveId, uint8_t level, uint8_t &criteriaLevel) const noexcept;
        };
    }
}

#endif
