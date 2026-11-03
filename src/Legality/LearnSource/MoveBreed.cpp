// Port of PKHeX PKHeX.Core/Legality/Moves/Breeding (MoveBreed.cs, MoveBreed2.cs ... MoveBreed6.cs,
// EggSource.cs). See MoveBreed.h.
#include "Legality/LearnSource/MoveBreed.h"

#include <algorithm>
#include <array>

#include "Legality/LearnSource/LearnSourceConstants.h"
#include "Legality/LearnSource/LearnSources.h"
#include "Legality/LearnSource/Learnset.h"

namespace Legality
{
    namespace LearnSource
    {
        namespace
        {
            using Enums::GameVersion;

            constexpr int MOVE_SLOT_COUNT = 4;
            /// Every generation's EggSource enum puts Base at 1 (None is 0).
            constexpr uint8_t EGG_SOURCE_BASE = 1;

            /// PKHeX EggSource2.
            constexpr uint8_t EGG_SOURCE_2_FATHER_EGG = 2;
            constexpr uint8_t EGG_SOURCE_2_FATHER_MACHINE = 3;
            constexpr uint8_t EGG_SOURCE_2_PARENT_LEVEL_UP = 4;
            constexpr uint8_t EGG_SOURCE_2_TUTOR = 5;
            constexpr uint8_t EGG_SOURCE_2_MAX = 6;
            /// PKHeX EggSource34.
            constexpr uint8_t EGG_SOURCE_34_FATHER_EGG = 2;
            constexpr uint8_t EGG_SOURCE_34_FATHER_MACHINE = 3;
            constexpr uint8_t EGG_SOURCE_34_PARENT_LEVEL_UP = 4;
            constexpr uint8_t EGG_SOURCE_34_MAX = 5;
            /// PKHeX EggSource5.
            constexpr uint8_t EGG_SOURCE_5_FATHER_EGG = 2;
            constexpr uint8_t EGG_SOURCE_5_PARENT_LEVEL_UP = 3;
            constexpr uint8_t EGG_SOURCE_5_FATHER_MACHINE = 4;
            constexpr uint8_t EGG_SOURCE_5_MAX = 5;
            /// PKHeX EggSource6.
            constexpr uint8_t EGG_SOURCE_6_PARENT_LEVEL_UP = 2;
            constexpr uint8_t EGG_SOURCE_6_PARENT_EGG = 3;
            constexpr uint8_t EGG_SOURCE_6_MAX = 4;
            /// EggSource34 / 5 / 6 VoltTackle (one past Max).
            constexpr uint8_t EGG_SOURCE_VOLT_TACKLE = 6;

            /// PKHeX BreedInfo<T>.
            struct BreedInfo
            {
                std::span<uint8_t, 4> actual;
                std::array<uint8_t, 4> possible{};
                Learnset learnset;
                std::span<const uint16_t, 4> moves;
                uint8_t level = 0;
            };

            bool containsMove(std::span<const uint16_t> moves, uint16_t moveId) noexcept
            {
                return std::find(moves.begin(), moves.end(), moveId) != moves.end();
            }

            int indexOfMove(std::span<const uint16_t> moves, uint16_t moveId) noexcept
            {
                const auto found = std::find(moves.begin(), moves.end(), moveId);
                return found == moves.end() ? -1 : static_cast<int>(found - moves.begin());
            }

            /// The first empty slot, the move count (PKHeX moves.IndexOf(0), -1 meaning all four).
            int getMoveCount(std::span<const uint16_t, 4> moves) noexcept
            {
                for (int slotIndex = 0; slotIndex < MOVE_SLOT_COUNT; ++slotIndex)
                {
                    if (moves[slotIndex] == 0)
                    {
                        return slotIndex;
                    }
                }
                return MOVE_SLOT_COUNT;
            }

            /// MoveBreed2 ... 6 VerifyBaseMoves (identical in every generation).
            bool verifyBaseMoves(const BreedInfo &info)
            {
                int baseCount = 0;
                for (const uint8_t origin : info.actual)
                {
                    if (origin != EGG_SOURCE_BASE)
                    {
                        break;
                    }
                    ++baseCount;
                }
                const std::span<const uint16_t> baseMoves = info.learnset.getBaseEggMoves(info.level);
                const int baseMoveCount = static_cast<int>(baseMoves.size());
                if (baseMoveCount < baseCount)
                {
                    return false;
                }
                if (info.moves[MOVE_SLOT_COUNT - 1] == 0 && baseCount != baseMoveCount)
                {
                    return false;
                }
                for (int slotIndex = baseCount - 1, baseIndex = baseMoveCount - 1; slotIndex >= 0; --slotIndex, --baseIndex)
                {
                    if (baseMoves[baseIndex] != info.moves[slotIndex])
                    {
                        return false;
                    }
                }
                // A low-index base egg move may be nudged out, but can only reappear if enough non-base
                // moves come before it.
                if (baseMoveCount == baseCount)
                {
                    return true;
                }
                for (int slotIndex = baseCount; slotIndex < MOVE_SLOT_COUNT; ++slotIndex)
                {
                    if ((info.possible[slotIndex] & (1 << EGG_SOURCE_BASE)) == 0)
                    {
                        continue;
                    }
                    const int baseIndex = indexOfMove(baseMoves, info.moves[slotIndex]);
                    const int minimumIndex = MOVE_SLOT_COUNT - baseMoveCount + baseIndex;
                    if (slotIndex < minimumIndex + baseCount)
                    {
                        return false;
                    }
                }
                return true;
            }

            /// MoveBreed3 ... 6 RecurseMovesForOrigin: peel each slot down to the lowest source type
            /// that still validates.
            bool recurseMovesForOrigin(BreedInfo &info, int start, uint8_t type)
            {
                int slotIndex = start;
                do
                {
                    const uint8_t unpeel = static_cast<uint8_t>(type - 1);
                    if (unpeel != 0 && recurseMovesForOrigin(info, slotIndex, unpeel))
                    {
                        return true;
                    }
                    if ((info.possible[slotIndex] & (1 << type)) == 0)
                    {
                        return false;
                    }
                    info.actual[slotIndex] = type;
                }
                while (--slotIndex >= 0);
                return verifyBaseMoves(info);
            }

            /// MoveBreed2.GetFirstType.
            uint8_t getFirstNonBaseTypeGeneration2(uint8_t permit) noexcept
            {
                for (uint8_t type = EGG_SOURCE_2_FATHER_EGG; type < EGG_SOURCE_2_MAX; ++type)
                {
                    if ((permit & (1 << type)) != 0)
                    {
                        return type;
                    }
                }
                return 0;
            }

            /// MoveBreed2.RecurseMovesForOrigin: Base or any other source.
            bool recurseMovesForOriginGeneration2(BreedInfo &info, int start, uint8_t type)
            {
                int slotIndex = start;
                do
                {
                    if (type != EGG_SOURCE_BASE && recurseMovesForOriginGeneration2(info, slotIndex, EGG_SOURCE_BASE))
                    {
                        return true;
                    }
                    int flag = 1 << EGG_SOURCE_BASE;
                    if (type != EGG_SOURCE_BASE)
                    {
                        flag = ~flag;
                    }
                    const uint8_t permit = info.possible[slotIndex];
                    if ((permit & flag) == 0)
                    {
                        return false;
                    }
                    info.actual[slotIndex] = type == EGG_SOURCE_BASE ? EGG_SOURCE_BASE : getFirstNonBaseTypeGeneration2(permit);
                }
                while (--slotIndex >= 0);
                return verifyBaseMoves(info);
            }

            /// MoveBreed2 ... 6 CleanResult: give each unresolved slot its lowest possible source.
            void cleanResult(BreedInfo &info, uint8_t maximum)
            {
                for (int slotIndex = 0; slotIndex < MOVE_SLOT_COUNT; ++slotIndex)
                {
                    const uint8_t permit = info.possible[slotIndex];
                    if (permit == 0 || info.actual[slotIndex] != 0)
                    {
                        continue;
                    }
                    for (uint8_t type = 0; type < maximum; ++type)
                    {
                        if ((permit & (1 << type)) != 0)
                        {
                            info.actual[slotIndex] = type;
                            break;
                        }
                    }
                }
            }

            struct BreedSources
            {
                LearnEnvironment learnsetEnvironment = LearnEnvironment::None;
                LearnEnvironment eggEnvironment = LearnEnvironment::None;
                LearnEnvironment personalEnvironment = LearnEnvironment::None;
            };

            /// Which game's tables each MoveBreed generation reads.
            bool getBreedSources(uint8_t generation, GameVersion version, BreedSources &sources) noexcept
            {
                switch (generation)
                {
                    case 2:
                    {
                        const LearnEnvironment environment = version == GameVersion::C ? LearnEnvironment::C : LearnEnvironment::GS;
                        sources = BreedSources{environment, environment, environment};
                        return true;
                    }
                    case 3:
                    {
                        LearnEnvironment environment = LearnEnvironment::None;
                        switch (version)
                        {
                            case GameVersion::RU:
                            case GameVersion::SA:
                                environment = LearnEnvironment::RS;
                                break;
                            case GameVersion::EM: environment = LearnEnvironment::E; break;
                            case GameVersion::FR: environment = LearnEnvironment::FR; break;
                            case GameVersion::LG: environment = LearnEnvironment::LG; break;
                            default:
                                // PKHeX throws for any other version; nothing hatches from it.
                                return false;
                        }
                        sources = BreedSources{environment, environment, environment};
                        return true;
                    }
                    case 4:
                    {
                        const bool isHeartGold = version == GameVersion::HG || version == GameVersion::SS;
                        const bool isDiamond = version == GameVersion::D || version == GameVersion::P;
                        const LearnEnvironment environment = isHeartGold ? LearnEnvironment::HGSS : (isDiamond ? LearnEnvironment::DP : LearnEnvironment::PT);
                        sources = BreedSources{environment, isHeartGold ? LearnEnvironment::HGSS : LearnEnvironment::DP, environment};
                        return true;
                    }
                    case 5:
                    {
                        const bool isBlack = version == GameVersion::B || version == GameVersion::W;
                        const LearnEnvironment environment = isBlack ? LearnEnvironment::BW : LearnEnvironment::B2W2;
                        sources = BreedSources{environment, LearnEnvironment::BW, environment};
                        return true;
                    }
                    default:
                    {
                        const LearnEnvironment environment = getLearnEnvironment(version);
                        sources = BreedSources{environment, environment, environment};
                        return environment != LearnEnvironment::None;
                    }
                }
            }

            /// MoveBreed2 ... 6 MarkMovesForOrigin.
            void markMovesForOrigin(BreedInfo &info, uint8_t generation, GameVersion version, std::span<const uint16_t> eggMoves, int moveCount, bool canInherit, int personalRow, LearnEnvironment personalEnvironment)
            {
                const std::span<const uint16_t> baseEgg = info.learnset.getBaseEggMoves(info.level);
                for (int slotIndex = 0; slotIndex < moveCount; ++slotIndex)
                {
                    const uint16_t moveId = info.moves[slotIndex];
                    uint8_t &possible = info.possible[slotIndex];
                    if (generation == 2 && moveId > MAX_MOVE_ID_2)
                    {
                        continue;
                    }
                    if (containsMove(baseEgg, moveId))
                    {
                        possible |= 1 << EGG_SOURCE_BASE;
                    }
                    if (canInherit && info.learnset.isLearn(moveId))
                    {
                        const uint8_t levelUpSource = generation == 2 ? EGG_SOURCE_2_PARENT_LEVEL_UP : (generation <= 4 ? EGG_SOURCE_34_PARENT_LEVEL_UP : (generation == 5 ? EGG_SOURCE_5_PARENT_LEVEL_UP : EGG_SOURCE_6_PARENT_LEVEL_UP));
                        possible |= 1 << levelUpSource;
                    }
                    if (containsMove(eggMoves, moveId))
                    {
                        const uint8_t eggSource = generation == 2 ? EGG_SOURCE_2_FATHER_EGG : (generation <= 4 ? EGG_SOURCE_34_FATHER_EGG : (generation == 5 ? EGG_SOURCE_5_FATHER_EGG : EGG_SOURCE_6_PARENT_EGG));
                        possible |= 1 << eggSource;
                    }
                    if (generation >= 6)
                    {
                        continue;
                    }
                    if (isBreedingMachineMove(personalEnvironment, personalRow, moveId))
                    {
                        const uint8_t machineSource = generation == 2 ? EGG_SOURCE_2_FATHER_MACHINE : (generation <= 4 ? EGG_SOURCE_34_FATHER_MACHINE : EGG_SOURCE_5_FATHER_MACHINE);
                        possible |= 1 << machineSource;
                    }
                    if (generation == 2 && version == GameVersion::C && isBreedingTutorCrystal(personalRow, moveId))
                    {
                        possible |= 1 << EGG_SOURCE_2_TUTOR;
                    }
                }
            }

            uint8_t getSourceMaximum(uint8_t generation) noexcept
            {
                if (generation == 2)
                {
                    return EGG_SOURCE_2_MAX;
                }
                if (generation <= 4)
                {
                    return EGG_SOURCE_34_MAX;
                }
                return generation == 5 ? EGG_SOURCE_5_MAX : EGG_SOURCE_6_MAX;
            }

            struct MoveOrder
            {
                uint16_t moveId = 0;
                uint8_t origin = 0;
            };

            /// MoveBreed.GetSortedMoveOrder (and GetSortedMoveOrder2 for Gen 2: base moves first).
            void getSortedMoveOrder(uint8_t generation, std::span<const uint16_t, 4> moves, std::span<const uint8_t, 4> origins, std::array<MoveOrder, 4> &expected)
            {
                if (generation == 2)
                {
                    const int baseCount = static_cast<int>(std::count(origins.begin(), origins.end(), EGG_SOURCE_BASE));
                    int baseCounter = 0;
                    int nonBaseCounter = 0;
                    for (int slotIndex = 0; slotIndex < MOVE_SLOT_COUNT; ++slotIndex)
                    {
                        const uint8_t origin = origins[slotIndex];
                        if (origin == 0)
                        {
                            continue;
                        }
                        const int orderIndex = origin == EGG_SOURCE_BASE ? baseCounter++ : baseCount + nonBaseCounter++;
                        expected[orderIndex] = MoveOrder{moves[slotIndex], origin};
                    }
                    return;
                }
                int sortedCount = 0;
                for (int slotIndex = MOVE_SLOT_COUNT - 1; slotIndex >= 0; --slotIndex)
                {
                    const uint8_t origin = origins[slotIndex];
                    if (origin == 0)
                    {
                        continue;
                    }
                    int insertIndex = 0;
                    while (insertIndex < sortedCount && origin >= expected[insertIndex].origin)
                    {
                        ++insertIndex;
                    }
                    if (insertIndex < sortedCount)
                    {
                        for (int shiftIndex = MOVE_SLOT_COUNT - 1; shiftIndex > insertIndex; --shiftIndex)
                        {
                            expected[shiftIndex] = expected[shiftIndex - 1];
                        }
                    }
                    expected[insertIndex] = MoveOrder{moves[slotIndex], origin};
                    ++sortedCount;
                }
            }

            /// MoveBreed.RebuildMoves: fresh base moves, then the non-base moves in sorted order.
            void rebuildMoves(std::span<const uint16_t> baseMoves, const std::array<MoveOrder, 4> &expected, std::span<uint16_t, 4> result)
            {
                std::array<uint16_t, 4> notBase{};
                int notBaseCount = 0;
                for (const MoveOrder &order : expected)
                {
                    if (order.origin == 0)
                    {
                        continue;
                    }
                    if (!containsMove(baseMoves, order.moveId))
                    {
                        notBase[notBaseCount++] = order.moveId;
                    }
                }
                int baseCount = MOVE_SLOT_COUNT - notBaseCount;
                const int baseMoveCount = static_cast<int>(baseMoves.size());
                if (baseCount > baseMoveCount)
                {
                    baseCount = baseMoveCount;
                }
                int resultIndex = 0;
                for (; resultIndex < baseCount; ++resultIndex)
                {
                    result[resultIndex] = baseMoves[baseMoveCount - baseCount + resultIndex];
                }
                for (int notBaseIndex = 0; notBaseIndex < notBaseCount; ++notBaseIndex)
                {
                    result[resultIndex++] = notBase[notBaseIndex];
                }
                for (; resultIndex < MOVE_SLOT_COUNT; ++resultIndex)
                {
                    result[resultIndex] = 0;
                }
            }

            /// MoveBreed.GetExpectedMoves(generation, ...): `origins` holds the failed validation's.
            bool getExpectedMovesFromOrigins(uint8_t generation, uint16_t speciesId, uint8_t formId, GameVersion version, std::span<const uint16_t, 4> moves, std::span<const uint8_t, 4> origins, std::span<uint16_t, 4> result)
            {
                std::array<MoveOrder, 4> expected{};
                getSortedMoveOrder(generation, moves, origins, expected);
                std::array<uint8_t, 4> temporary{};
                for (int slotIndex = 0; slotIndex < MOVE_SLOT_COUNT; ++slotIndex)
                {
                    result[slotIndex] = expected[slotIndex].moveId;
                }
                if (validateBreedMoves(generation, speciesId, formId, version, result, temporary))
                {
                    return true;
                }
                // Drop the present base moves and rebuild with a fresh set.
                const Learnset learnset = getLearnset(getLearnEnvironment(version), speciesId, formId);
                const std::span<const uint16_t> baseMoves = learnset.getBaseEggMoves(getEggLevel(generation));
                rebuildMoves(baseMoves, expected, result);
                temporary.fill(0);
                if (validateBreedMoves(generation, speciesId, formId, version, result, temporary))
                {
                    return true;
                }
                // Total failure: the base moves alone.
                for (int slotIndex = 0; slotIndex < MOVE_SLOT_COUNT; ++slotIndex)
                {
                    result[slotIndex] = slotIndex < static_cast<int>(baseMoves.size()) ? baseMoves[slotIndex] : 0;
                }
                return false;
            }
        }

        bool validateBreedMoves(uint8_t generation, uint16_t speciesId, uint8_t formId, Enums::GameVersion version, std::span<const uint16_t, 4> moves, std::span<uint8_t, 4> origins)
        {
            int moveCount = getMoveCount(moves);
            if (moveCount == 0)
            {
                return false;
            }
            BreedSources sources;
            if (!getBreedSources(generation, version, sources))
            {
                return false;
            }
            // Generations 2 to 5 read form 0's tables; Gen 6 on reads the egg's form.
            const uint8_t tableFormId = generation >= 6 ? formId : 0;
            int personalRow = 0;
            if (!tryGetPersonal(sources.personalEnvironment, speciesId, tableFormId, personalRow) && generation <= 3)
            {
                return false;
            }
            BreedInfo info{origins, {}, getLearnset(sources.learnsetEnvironment, speciesId, tableFormId), moves, getEggLevel(generation)};
            const std::span<const uint16_t> eggMoves = getEggMoves(sources.eggEnvironment, speciesId, tableFormId);
            const bool canInherit = canInheritMoves(speciesId);
            const uint8_t maximum = getSourceMaximum(generation);
            bool isValid = false;
            if (generation == 2)
            {
                markMovesForOrigin(info, generation, version, eggMoves, moveCount, canInherit, personalRow, sources.personalEnvironment);
                isValid = recurseMovesForOriginGeneration2(info, moveCount - 1, EGG_SOURCE_2_MAX);
            }
            else
            {
                // Pichu's Volt Tackle (Emerald only in Gen 3) is always the last move.
                const bool allowsVoltTackle = generation != 3 || version == GameVersion::EM;
                if (speciesId == PICHU_SPECIES && moves[moveCount - 1] == VOLT_TACKLE_MOVE && allowsVoltTackle)
                {
                    origins[--moveCount] = EGG_SOURCE_VOLT_TACKLE;
                }
                if (moveCount == 0)
                {
                    isValid = verifyBaseMoves(info);
                }
                else
                {
                    markMovesForOrigin(info, generation, version, eggMoves, moveCount, canInherit, personalRow, sources.personalEnvironment);
                    isValid = recurseMovesForOrigin(info, moveCount - 1, static_cast<uint8_t>(maximum - 1));
                }
            }
            if (!isValid)
            {
                cleanResult(info, maximum);
            }
            return isValid;
        }

        bool getExpectedBreedMoves(uint8_t generation, uint16_t speciesId, uint8_t formId, Enums::GameVersion version, std::span<const uint16_t, 4> moves, std::span<uint16_t, 4> result)
        {
            std::array<uint8_t, 4> origins{};
            if (validateBreedMoves(generation, speciesId, formId, version, moves, origins))
            {
                std::copy(moves.begin(), moves.end(), result.begin());
                return true;
            }
            return getExpectedMovesFromOrigins(generation, speciesId, formId, version, moves, origins, result);
        }

        LearnMethod getEggSourceMethod(uint8_t origin, uint8_t generation) noexcept
        {
            if (origin == EGG_SOURCE_BASE && generation >= 2)
            {
                return LearnMethod::Initial;
            }
            if (generation == 2)
            {
                switch (origin)
                {
                    case EGG_SOURCE_2_FATHER_EGG: return LearnMethod::EggMove;
                    case EGG_SOURCE_2_FATHER_MACHINE: return LearnMethod::TechnicalMachine;
                    case EGG_SOURCE_2_PARENT_LEVEL_UP: return LearnMethod::InheritLevelUp;
                    case EGG_SOURCE_2_TUTOR: return LearnMethod::Tutor;
                    default: return LearnMethod::None;
                }
            }
            if (generation == 3 || generation == 4)
            {
                switch (origin)
                {
                    case EGG_SOURCE_34_FATHER_EGG: return LearnMethod::EggMove;
                    case EGG_SOURCE_34_FATHER_MACHINE: return LearnMethod::TechnicalMachine;
                    case EGG_SOURCE_34_PARENT_LEVEL_UP: return LearnMethod::InheritLevelUp;
                    case EGG_SOURCE_VOLT_TACKLE: return LearnMethod::SpecialEgg;
                    default: return LearnMethod::None;
                }
            }
            if (generation == 5)
            {
                switch (origin)
                {
                    case EGG_SOURCE_5_FATHER_EGG: return LearnMethod::EggMove;
                    case EGG_SOURCE_5_PARENT_LEVEL_UP: return LearnMethod::InheritLevelUp;
                    case EGG_SOURCE_5_FATHER_MACHINE: return LearnMethod::TechnicalMachine;
                    case EGG_SOURCE_VOLT_TACKLE: return LearnMethod::SpecialEgg;
                    default: return LearnMethod::None;
                }
            }
            if (generation >= 6)
            {
                switch (origin)
                {
                    case EGG_SOURCE_6_PARENT_LEVEL_UP: return LearnMethod::InheritLevelUp;
                    case EGG_SOURCE_6_PARENT_EGG: return LearnMethod::EggMove;
                    case EGG_SOURCE_VOLT_TACKLE: return LearnMethod::SpecialEgg;
                    default: return LearnMethod::None;
                }
            }
            return LearnMethod::None;
        }
    }
}
