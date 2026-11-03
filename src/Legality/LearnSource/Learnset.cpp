// Port of PKHeX PKHeX.Core/Legality/Learnset/Learnset.cs and LearnsetStadium.cs. See Learnset.h.
#include "Legality/LearnSource/Learnset.h"

#include <algorithm>

namespace Legality
{
    namespace LearnSource
    {
        namespace
        {
            constexpr size_t MOVE_SLOT_COUNT = 4;
            constexpr size_t STADIUM_TUPLE_SIZE = 3;

            /// PKHeX Learnset.AddMoveShiftLater: append unless known, overwriting the oldest slot.
            void addMoveShiftLater(std::span<uint16_t, 4> encounterMoves, int &counter, uint16_t moveId) noexcept
            {
                if (std::find(encounterMoves.begin(), encounterMoves.end(), moveId) != encounterMoves.end())
                {
                    return;
                }
                encounterMoves[counter & 3] = moveId;
                ++counter;
            }

            /// PKHeX Learnset.RectifyOrderShift: rotate so the oldest move ends up first.
            void rectifyOrderShift(std::span<uint16_t, 4> encounterMoves, int counter) noexcept
            {
                if (counter <= static_cast<int>(MOVE_SLOT_COUNT))
                {
                    return;
                }
                const int rotation = counter & 3;
                for (int rotationIndex = 0; rotationIndex < rotation; ++rotationIndex)
                {
                    const uint16_t firstMove = encounterMoves[0];
                    for (size_t slotIndex = 0; slotIndex < MOVE_SLOT_COUNT - 1; ++slotIndex)
                    {
                        encounterMoves[slotIndex] = encounterMoves[slotIndex + 1];
                    }
                    encounterMoves[MOVE_SLOT_COUNT - 1] = firstMove;
                }
            }
        }

        bool Learnset::isLearn(uint16_t moveId) const noexcept
        {
            return std::find(moves.begin(), moves.end(), moveId) != moves.end();
        }

        bool Learnset::tryGetLevelLearnMove(uint16_t moveId, uint8_t &level) const noexcept
        {
            const auto found = std::find(moves.begin(), moves.end(), moveId);
            if (found == moves.end())
            {
                level = 0;
                return false;
            }
            level = levels[static_cast<size_t>(found - moves.begin())];
            return true;
        }

        std::span<const uint16_t> Learnset::getBaseEggMoves(uint8_t level) const noexcept
        {
            size_t learnedCount = 0;
            for (const uint8_t moveLevel : levels)
            {
                if (moveLevel > level)
                {
                    break;
                }
                ++learnedCount;
            }
            if (learnedCount == 0)
            {
                return {};
            }
            size_t firstMoveIndex = 0;
            if (learnedCount > MOVE_SLOT_COUNT)
            {
                firstMoveIndex = learnedCount - MOVE_SLOT_COUNT;
                learnedCount = MOVE_SLOT_COUNT;
            }
            return moves.subspan(firstMoveIndex, learnedCount);
        }

        void Learnset::setEncounterMoves(uint8_t level, std::span<uint16_t, 4> encounterMoves, int startIndex) const noexcept
        {
            int counter = startIndex;
            for (size_t moveIndex = 0; moveIndex < moves.size(); ++moveIndex)
            {
                const uint8_t requiredLevel = levels[moveIndex];
                // Evolution and reminder-only moves.
                if (requiredLevel < 1)
                {
                    continue;
                }
                if (requiredLevel > level)
                {
                    break;
                }
                addMoveShiftLater(encounterMoves, counter, moves[moveIndex]);
            }
            rectifyOrderShift(encounterMoves, counter);
        }

        void Learnset::setEncounterMovesBackwards(uint8_t level, std::span<uint16_t, 4> encounterMoves, int startIndex) const noexcept
        {
            int counter = startIndex;
            int lastMoveIndex = static_cast<int>(levels.size()) - 1;
            while (lastMoveIndex >= 0 && levels[static_cast<size_t>(lastMoveIndex)] > level)
            {
                --lastMoveIndex;
            }
            while (lastMoveIndex >= 0)
            {
                int firstMoveIndex = lastMoveIndex;
                while (firstMoveIndex != 0 && levels[static_cast<size_t>(firstMoveIndex)] == levels[static_cast<size_t>(firstMoveIndex - 1)])
                {
                    --firstMoveIndex;
                }
                for (int moveIndex = firstMoveIndex; moveIndex <= lastMoveIndex; ++moveIndex)
                {
                    const uint16_t moveId = moves[static_cast<size_t>(moveIndex)];
                    if (std::find(encounterMoves.begin(), encounterMoves.end(), moveId) != encounterMoves.end())
                    {
                        continue;
                    }
                    // Evolution and reminder-only moves are not level-up moves.
                    if (levels[static_cast<size_t>(moveIndex)] == 0)
                    {
                        break;
                    }
                    encounterMoves[static_cast<size_t>(counter)] = moveId;
                    ++counter;
                    if (counter == static_cast<int>(MOVE_SLOT_COUNT))
                    {
                        return;
                    }
                }
                lastMoveIndex = firstMoveIndex - 1;
            }
        }

        bool StadiumLearnset::canRelearn(uint16_t moveId, uint8_t level, uint8_t &criteriaLevel) const noexcept
        {
            criteriaLevel = 0;
            for (size_t tupleOffset = 0; tupleOffset + STADIUM_TUPLE_SIZE <= tuples.size(); tupleOffset += STADIUM_TUPLE_SIZE)
            {
                const uint8_t tupleLevel = tuples[tupleOffset];
                const uint8_t tupleMove = tuples[tupleOffset + 1];
                const uint8_t tupleSource = tuples[tupleOffset + 2];
                // Sorted by level, so nothing later qualifies.
                if (level < tupleLevel)
                {
                    return false;
                }
                if (moveId != tupleMove)
                {
                    continue;
                }
                if ((tupleSource & STADIUM_SOURCE_RELEARNABLE) == 0)
                {
                    continue;
                }
                criteriaLevel = tupleLevel;
                return true;
            }
            return false;
        }
    }
}
