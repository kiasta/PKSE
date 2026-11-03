// Port of PKHeX PKHeX.Core/Legality/LearnSource/Group (LearnGroup1 ... LearnGroup9a, LearnGroupHOME,
// LearnGroupUtil) and PKHeX.Core/Legality/Evolutions/FormChangeUtil.cs. See LearnGroup.h.
#include "Legality/LearnSource/LearnGroup.h"

#include <algorithm>
#include <array>

#include "Legality/Encounters/GBRestrictions.h"
#include "Legality/LearnSource/LearnSourceConstants.h"
#include "Legality/LearnSource/LearnSources.h"

namespace Legality
{
    namespace LearnSource
    {
        namespace
        {
            using Enums::GameVersion;
            using Evolutions::EvolutionCriteria;

            constexpr int MOVE_SLOT_COUNT = 4;

            int indexOfMove(std::span<const uint16_t, 4> current, uint16_t moveId) noexcept
            {
                for (int slotIndex = 0; slotIndex < MOVE_SLOT_COUNT; ++slotIndex)
                {
                    if (current[slotIndex] == moveId)
                    {
                        return slotIndex;
                    }
                }
                return -1;
            }

            bool containsMove(std::span<const uint16_t> moves, uint16_t moveId) noexcept
            {
                return std::find(moves.begin(), moves.end(), moveId) != moves.end();
            }

            std::span<const EvolutionCriteria> getChain(const LearnCheck &check, GameVersion contextGroup) noexcept
            {
                return check.history.getChain(contextGroup);
            }

            bool isGameBoyContext(GameVersion contextGroup) noexcept
            {
                return contextGroup == GameVersion::RBY || contextGroup == GameVersion::GSC;
            }

            /// PKHeX EntityContext.IsEraHOME: generation 8 on.
            bool isHomeEraContext(GameVersion contextGroup) noexcept
            {
                return contextGroup == GameVersion::SWSH || contextGroup == GameVersion::PLA || contextGroup == GameVersion::BDSP || contextGroup == GameVersion::SV || contextGroup == GameVersion::ZA;
            }

            bool isVersion(const LearnEncounter &encounter, GameVersion version) noexcept
            {
                return encounter.version == version;
            }

            /// PKHeX LearnGroup1.HasPossiblyVisitedGen2: a tradeback item in the catch-rate byte.
            bool hasPossiblyVisitedGeneration2(const LearnEntity &entity) noexcept
            {
                return Encounters::GBRestrictions::isCatchRateHeldItem(entity.catchRate);
            }

            /// The loop every single-game check shares: each unexplained slot asks one game.
            void checkWithGame(const LearnCheck &check, LearnEnvironment environment, int rowIndex, const EvolutionCriteria &evolution, int stage, uint16_t types, LearnOption option, GameVersion contextGroup)
            {
                for (int slotIndex = MOVE_SLOT_COUNT - 1; slotIndex >= 0; --slotIndex)
                {
                    if (check.results[slotIndex].isValid())
                    {
                        continue;
                    }
                    const MoveLearnInfo info = getCanLearn(environment, check.entity, rowIndex, evolution, check.current[slotIndex], types, option);
                    if (!info.isEmpty())
                    {
                        check.results[slotIndex] = MoveResult::make(info, static_cast<uint8_t>(stage), contextGroup);
                    }
                }
            }

            /// The egg-template block each generation's CheckEncounterMoves shares.
            void checkEggEncounterMoves(const LearnCheck &check, LearnEnvironment environment, bool allowVoltTackle)
            {
                const LearnEncounter &encounter = check.encounter;
                const std::span<const uint16_t> eggMoves = getEggMoves(environment, encounter.speciesId, encounter.formId);
                const std::span<const uint16_t> levelMoves = getInheritMoves(environment, encounter.speciesId, encounter.formId);
                for (int slotIndex = MOVE_SLOT_COUNT - 1; slotIndex >= 0; --slotIndex)
                {
                    if (check.results[slotIndex].isValid())
                    {
                        continue;
                    }
                    const uint16_t moveId = check.current[slotIndex];
                    if (containsMove(eggMoves, moveId))
                    {
                        check.results[slotIndex] = MoveResult::make(LearnMethod::EggMove, environment);
                    }
                    else if (containsMove(levelMoves, moveId))
                    {
                        check.results[slotIndex] = MoveResult::make(LearnMethod::InheritLevelUp, environment);
                    }
                    else if (allowVoltTackle && moveId == VOLT_TACKLE_MOVE && encounter.canHaveVoltTackle)
                    {
                        check.results[slotIndex] = MoveResult::make(LearnMethod::SpecialEgg, environment);
                    }
                }
            }

            /// LearnGroup8 / 8b / 9 CheckSharedMoves: the egg moves of the species it hatches from.
            void checkSharedMoves(const LearnCheck &check, LearnEnvironment environment, const EvolutionCriteria &evolution)
            {
                uint16_t hatchSpeciesId = 0;
                uint8_t hatchFormId = 0;
                getHatchSpeciesForm(environment, evolution.speciesId, evolution.formId, hatchSpeciesId, hatchFormId);
                const std::span<const uint16_t> eggMoves = getEggMoves(environment, hatchSpeciesId, hatchFormId);
                for (int slotIndex = MOVE_SLOT_COUNT - 1; slotIndex >= 0; --slotIndex)
                {
                    if (check.results[slotIndex].isValid())
                    {
                        continue;
                    }
                    if (containsMove(eggMoves, check.current[slotIndex]))
                    {
                        check.results[slotIndex] = MoveResult::make(LearnMethod::Shared, environment);
                    }
                }
            }

            /// LearnGroup1 / LearnGroup2 GetIsPreferable: which of two Game Boy sources to keep.
            bool isPreferable(const MoveResult &entry, const MoveLearnInfo &candidate, int stage) noexcept
            {
                if (entry == MoveResult{})
                {
                    return true;
                }
                if (entry.info.method == LearnMethod::LevelUp)
                {
                    if (candidate.method != LearnMethod::LevelUp)
                    {
                        return true;
                    }
                    if (entry.evolutionStage == stage)
                    {
                        return entry.info.argument < candidate.argument;
                    }
                }
                else if (isEggSourceMethod(entry.info.method))
                {
                    return true;
                }
                else if (candidate.method == LearnMethod::LevelUp)
                {
                    return false;
                }
                return entry.evolutionStage < stage;
            }

            void setIfPreferable(MoveResult &entry, const MoveLearnInfo &candidate, int stage, GameVersion contextGroup, bool &assigned) noexcept
            {
                assigned = false;
                if (!candidate.isEmpty() && isPreferable(entry, candidate, stage))
                {
                    entry = MoveResult::make(candidate, static_cast<uint8_t>(stage), contextGroup);
                    assigned = true;
                }
            }

            // ---- Generation 1 ----

            void getEncounterMovesGeneration1(const LearnEncounter &encounter, std::span<uint16_t, 4> moves)
            {
                const LearnEnvironment environment = (isVersion(encounter, GameVersion::YW) || isVersion(encounter, GameVersion::RBY)) ? LearnEnvironment::YW : LearnEnvironment::RB;
                setEncounterMovesGeneration1(environment, encounter.speciesId, encounter.levelMin, moves);
            }

            void flagFishyMoveSlots(const LearnCheck &check)
            {
                if (indexOfMove(check.current, 0) < 0)
                {
                    return;
                }
                std::array<uint16_t, 4> moves{};
                if (check.encounter.hasMoves())
                {
                    moves = check.encounter.moves;
                }
                else
                {
                    getEncounterMovesGeneration1(check.encounter, moves);
                }
                // Count the initial moves missing from the current list; leveling past moves can skip
                // them, so only the empty slots are flagged.
                int missingCount = 0;
                for (const uint16_t expectedMove : moves)
                {
                    if (expectedMove == 0)
                    {
                        break;
                    }
                    if (indexOfMove(check.current, expectedMove) < 0)
                    {
                        ++missingCount;
                    }
                }
                if (missingCount == 0)
                {
                    return;
                }
                for (int slotIndex = 0; slotIndex < MOVE_SLOT_COUNT; ++slotIndex)
                {
                    if (check.current[slotIndex] != 0)
                    {
                        continue;
                    }
                    check.results[slotIndex] = MoveResult::emptyInvalid();
                    if (--missingCount == 0)
                    {
                        break;
                    }
                }
            }

            void checkEncounterMovesGeneration1(const LearnCheck &check)
            {
                std::array<uint16_t, 4> moves{};
                if (check.encounter.hasMoves())
                {
                    moves = check.encounter.moves;
                }
                else
                {
                    getEncounterMovesGeneration1(check.encounter, moves);
                }
                markInitialMoves(check.results, check.current, moves, isVersion(check.encounter, GameVersion::YW) ? LearnEnvironment::YW : LearnEnvironment::RB);
                // Empty slots are flagged unless the Pokemon could have met Gen 2's move deleter.
                if (check.entity.formatGroup != GameVersion::RBY || hasPossiblyVisitedGeneration2(check.entity))
                {
                    return;
                }
                flagFishyMoveSlots(check);
            }

            bool isAnyOtherResultLowerStageHigherLevel(const LearnCheck &check, int index, uint8_t level, uint16_t speciesId)
            {
                for (int slotIndex = 0; slotIndex < MOVE_SLOT_COUNT; ++slotIndex)
                {
                    if (slotIndex == index)
                    {
                        continue;
                    }
                    const MoveResult &detail = check.results[slotIndex];
                    if (!detail.isValid() || !isGameBoyContext(detail.contextGroup) || detail.info.method != LearnMethod::LevelUp)
                    {
                        continue;
                    }
                    const std::span<const EvolutionCriteria> chain = getChain(check, detail.contextGroup);
                    if (detail.evolutionStage >= chain.size())
                    {
                        continue;
                    }
                    if (detail.info.argument > level && chain[detail.evolutionStage].speciesId < speciesId)
                    {
                        return true;
                    }
                }
                return false;
            }

            void flagEvolutionSlots(const LearnCheck &check)
            {
                for (int slotIndex = 0; slotIndex < MOVE_SLOT_COUNT; ++slotIndex)
                {
                    if (check.current[slotIndex] == 0)
                    {
                        continue;
                    }
                    const MoveResult detail = check.results[slotIndex];
                    if (!detail.isValid() || !isGameBoyContext(detail.contextGroup) || detail.info.method != LearnMethod::LevelUp)
                    {
                        continue;
                    }
                    const std::span<const EvolutionCriteria> chain = getChain(check, detail.contextGroup);
                    if (detail.evolutionStage >= chain.size())
                    {
                        continue;
                    }
                    if (isAnyOtherResultLowerStageHigherLevel(check, slotIndex, detail.info.argument, chain[detail.evolutionStage].speciesId))
                    {
                        check.results[slotIndex] = MoveResult::unobtainable();
                    }
                }
            }

            void checkStageGeneration1(const LearnCheck &check, const EvolutionCriteria &evolution, int stage, uint16_t types)
            {
                int redRow = 0;
                int yellowRow = 0;
                if (!tryGetPersonal(LearnEnvironment::RB, evolution.speciesId, evolution.formId, redRow) || !tryGetPersonal(LearnEnvironment::YW, evolution.speciesId, evolution.formId, yellowRow))
                {
                    return;
                }
                for (int slotIndex = MOVE_SLOT_COUNT - 1; slotIndex >= 0; --slotIndex)
                {
                    MoveResult &entry = check.results[slotIndex];
                    if (entry.isValid() && !isGameBoyContext(entry.contextGroup))
                    {
                        continue;
                    }
                    const uint16_t moveId = check.current[slotIndex];
                    bool assigned = false;
                    setIfPreferable(entry, getCanLearn(LearnEnvironment::YW, check.entity, yellowRow, evolution, moveId, types), stage, GameVersion::RBY, assigned);
                    if (assigned)
                    {
                        continue;
                    }
                    setIfPreferable(entry, getCanLearn(LearnEnvironment::RB, check.entity, redRow, evolution, moveId, types), stage, GameVersion::RBY, assigned);
                }
            }

            bool checkGeneration1(const LearnCheck &check, uint16_t types)
            {
                if (check.encounter.generation == 1 && hasSourceType(types, MOVE_SOURCE_ENCOUNTER))
                {
                    checkEncounterMovesGeneration1(check);
                }
                const std::span<const EvolutionCriteria> evolutions = getChain(check, GameVersion::RBY);
                for (size_t stage = 0; stage < evolutions.size(); ++stage)
                {
                    checkStageGeneration1(check, evolutions[stage], static_cast<int>(stage), types);
                }
                if (getPreviousLearnGroup(LearnGroup::Generation1, check, LearnOption::Current) == LearnGroup::None)
                {
                    flagEvolutionSlots(check);
                }
                return areAllParsed(check.results);
            }

            // ---- Generation 2 ----

            void checkStageGeneration2(const LearnCheck &check, const EvolutionCriteria &evolution, int stage, uint16_t types)
            {
                int goldRow = 0;
                int crystalRow = 0;
                if (!tryGetPersonal(LearnEnvironment::GS, evolution.speciesId, evolution.formId, goldRow) || !tryGetPersonal(LearnEnvironment::C, evolution.speciesId, evolution.formId, crystalRow))
                {
                    return;
                }
                const bool allowsStadium = !check.entity.isKorean && check.entity.allowsGameBoyCartridgeEra;
                const bool isKorean = check.entity.isKorean;
                for (int slotIndex = MOVE_SLOT_COUNT - 1; slotIndex >= 0; --slotIndex)
                {
                    MoveResult &entry = check.results[slotIndex];
                    if (entry.isValid() && !isGameBoyContext(entry.contextGroup))
                    {
                        continue;
                    }
                    const uint16_t moveId = check.current[slotIndex];
                    bool assigned = false;
                    setIfPreferable(entry, getCanLearn(LearnEnvironment::GS, check.entity, goldRow, evolution, moveId, types), stage, GameVersion::GSC, assigned);
                    if (assigned || isKorean)
                    {
                        continue;
                    }
                    setIfPreferable(entry, getCanLearn(LearnEnvironment::C, check.entity, crystalRow, evolution, moveId, types), stage, GameVersion::GSC, assigned);
                    if (allowsStadium)
                    {
                        setIfPreferable(entry, getStadiumRelearn(evolution, moveId, types), stage, GameVersion::GSC, assigned);
                    }
                }
            }

            void checkEncounterMovesGeneration2(const LearnCheck &check)
            {
                const LearnEncounter &encounter = check.encounter;
                const bool isCrystal = isVersion(encounter, GameVersion::C) || isVersion(encounter, GameVersion::GSC);
                std::array<uint16_t, 4> moves{};
                if (encounter.hasMoves())
                {
                    moves = encounter.moves;
                }
                else
                {
                    uint8_t level = encounter.levelMin;
                    // LearnSource2C.GetEncounterMoves: Crystal's caught data keeps the met level.
                    if (isCrystal && check.entity.formatGroup == GameVersion::GSC && check.entity.caughtData != 0)
                    {
                        level = std::max(level, check.entity.metLevel);
                    }
                    getLearnset(isCrystal ? LearnEnvironment::C : LearnEnvironment::GS, encounter.speciesId, 0).setEncounterMoves(level, moves);
                }
                markInitialMoves(check.results, check.current, moves, isCrystal ? LearnEnvironment::C : LearnEnvironment::GS);
            }

            bool checkGeneration2(const LearnCheck &check, uint16_t types, LearnOption option)
            {
                if (check.encounter.contextGroup == GameVersion::GSC && hasSourceType(types, MOVE_SOURCE_ENCOUNTER))
                {
                    checkEncounterMovesGeneration2(check);
                }
                const std::span<const EvolutionCriteria> evolutions = getChain(check, GameVersion::GSC);
                for (size_t stage = 0; stage < evolutions.size(); ++stage)
                {
                    // No evolution moves for the encounter species, the last stage.
                    if (stage == evolutions.size() - 1 && hasSourceType(types, MOVE_SOURCE_EVOLVE))
                    {
                        types &= static_cast<uint16_t>(~MOVE_SOURCE_EVOLVE);
                    }
                    checkStageGeneration2(check, evolutions[stage], static_cast<int>(stage), types);
                }
                if (check.encounter.isEggTemplate && check.encounter.generation == 2)
                {
                    checkEggEncounterMoves(check, isVersion(check.encounter, GameVersion::C) ? LearnEnvironment::C : LearnEnvironment::GS, false);
                }
                const bool isVirtualConsole1 = check.entity.isVirtualConsoleGeneration1;
                if (!isVirtualConsole1 && areAllParsed(check.results))
                {
                    return true;
                }
                // To step back to Gen 1, drop what neither Gen 1 teaches nor Gen 2 can relearn.
                if (getPreviousLearnGroup(LearnGroup::Generation2, check, option) == LearnGroup::None)
                {
                    return true;
                }
                for (int slotIndex = 0; slotIndex < MOVE_SLOT_COUNT; ++slotIndex)
                {
                    if (check.current[slotIndex] <= MAX_MOVE_ID_1)
                    {
                        continue;
                    }
                    const MoveResult &result = check.results[slotIndex];
                    if (!result.isParsed())
                    {
                        continue;
                    }
                    const LearnMethod method = result.info.method;
                    if ((isVirtualConsole1 && result.contextGroup == GameVersion::GSC) || method == LearnMethod::Initial || isEggSourceMethod(method))
                    {
                        check.results[slotIndex] = MoveResult::unobtainable();
                    }
                }
                return false;
            }

            // ---- Generation 3 ----

            void checkNincadaMoves(const LearnCheck &check, const EvolutionCriteria &nincada, LearnEnvironment environment, GameVersion contextGroup, bool resetExisting)
            {
                if (areAllParsed(check.results))
                {
                    return;
                }
                if (resetExisting)
                {
                    // A Ninjask move already marked means Nincada used its one; reset and retry.
                    for (int slotIndex = 0; slotIndex < MOVE_SLOT_COUNT; ++slotIndex)
                    {
                        if (check.results[slotIndex].info.method == LearnMethod::ShedinjaEvolution)
                        {
                            check.results[slotIndex] = MoveResult{};
                            break;
                        }
                    }
                }
                const Learnset ninjask = getLearnset(environment, NINJASK_SPECIES, 0);
                for (int slotIndex = 0; slotIndex < MOVE_SLOT_COUNT; ++slotIndex)
                {
                    if (check.results[slotIndex].isValid())
                    {
                        continue;
                    }
                    const uint16_t moveId = check.current[slotIndex];
                    if (moveId == 0)
                    {
                        break;
                    }
                    uint8_t level = 0;
                    if (!ninjask.tryGetLevelLearnMove(moveId, level) || !nincada.isInsideLevelRange(level))
                    {
                        continue;
                    }
                    check.results[slotIndex] = MoveResult::make(MoveLearnInfo{LearnMethod::ShedinjaEvolution, environment, level}, 0, contextGroup);
                    break;
                }
            }

            bool isShedinjaFromNincada(const LearnCheck &check, std::span<const EvolutionCriteria> evolutions, uint16_t types) noexcept
            {
                return hasSourceType(types, MOVE_SOURCE_LEVEL_UP) && check.encounter.speciesId == NINCADA_SPECIES && evolutions.size() == 2 && evolutions[0].speciesId == SHEDINJA_SPECIES;
            }

            void checkStageGeneration3(const LearnCheck &check, const EvolutionCriteria &evolution, int stage, uint16_t types)
            {
                const uint16_t levelAndTutors = types & (MOVE_SOURCE_LEVEL_UP | MOVE_SOURCE_ALL_TUTORS);
                const uint16_t levelOnly = types & MOVE_SOURCE_LEVEL_UP;
                if (!check.entity.allowsGameBoyAdvanceCrossTransfer)
                {
                    // CheckNX: the Nintendo Switch FireRed/LeafGreen, which cannot trade with Hoenn.
                    int fireRedRow = 0;
                    if (!tryGetPersonal(LearnEnvironment::FR, evolution.speciesId, evolution.formId, fireRedRow))
                    {
                        return;
                    }
                    constexpr uint8_t VERSION_FIRERED = 4;
                    const bool isFireRed = check.entity.originVersion == VERSION_FIRERED;
                    const LearnEnvironment primary = isFireRed ? LearnEnvironment::FR : LearnEnvironment::LG;
                    const LearnEnvironment secondary = isFireRed ? LearnEnvironment::LG : LearnEnvironment::FR;
                    for (int slotIndex = MOVE_SLOT_COUNT - 1; slotIndex >= 0; --slotIndex)
                    {
                        if (check.results[slotIndex].isValid())
                        {
                            continue;
                        }
                        const uint16_t moveId = check.current[slotIndex];
                        MoveLearnInfo info = getCanLearn(primary, check.entity, evolution.speciesId, evolution, moveId, types);
                        if (info.isEmpty())
                        {
                            info = getCanLearn(secondary, check.entity, evolution.speciesId, evolution, moveId, levelOnly);
                        }
                        if (!info.isEmpty())
                        {
                            check.results[slotIndex] = MoveResult::make(info, static_cast<uint8_t>(stage), GameVersion::RSE);
                            continue;
                        }
                        if (isTutorFireRedLeafGreen(evolution.speciesId, moveId))
                        {
                            check.results[slotIndex] = MoveResult{MoveLearnInfo{LearnMethod::Tutor, primary, 0}, 0, GameVersion::Invalid, 0};
                        }
                    }
                    return;
                }
                int rubyRow = 0;
                if (!tryGetPersonal(LearnEnvironment::RS, evolution.speciesId, evolution.formId, rubyRow))
                {
                    return;
                }
                const int speciesRow = evolution.speciesId;
                for (int slotIndex = MOVE_SLOT_COUNT - 1; slotIndex >= 0; --slotIndex)
                {
                    if (check.results[slotIndex].isValid())
                    {
                        continue;
                    }
                    const uint16_t moveId = check.current[slotIndex];
                    MoveLearnInfo info = getCanLearn(LearnEnvironment::E, check.entity, speciesRow, evolution, moveId, types);
                    if (info.isEmpty())
                    {
                        info = getCanLearn(LearnEnvironment::RS, check.entity, rubyRow, evolution, moveId, levelAndTutors);
                    }
                    if (info.isEmpty())
                    {
                        info = getCanLearn(LearnEnvironment::FR, check.entity, speciesRow, evolution, moveId, levelAndTutors);
                    }
                    if (info.isEmpty())
                    {
                        info = getCanLearn(LearnEnvironment::LG, check.entity, speciesRow, evolution, moveId, levelOnly);
                    }
                    if (!info.isEmpty())
                    {
                        check.results[slotIndex] = MoveResult::make(info, static_cast<uint8_t>(stage), GameVersion::RSE);
                    }
                }
            }

            LearnEnvironment getEggEnvironmentGeneration3(const LearnEncounter &encounter) noexcept
            {
                switch (encounter.version)
                {
                    case GameVersion::EM: return LearnEnvironment::E;
                    case GameVersion::FR: return LearnEnvironment::FR;
                    case GameVersion::LG: return LearnEnvironment::LG;
                    default: return LearnEnvironment::RS;
                }
            }

            bool checkGeneration3(const LearnCheck &check, uint16_t types)
            {
                const std::span<const EvolutionCriteria> evolutions = getChain(check, GameVersion::RSE);
                for (size_t stage = 0; stage < evolutions.size(); ++stage)
                {
                    checkStageGeneration3(check, evolutions[stage], static_cast<int>(stage), types);
                }
                if (hasSourceType(types, MOVE_SOURCE_ENCOUNTER) && check.encounter.isEggTemplate && check.encounter.contextGroup == GameVersion::RSE)
                {
                    checkEggEncounterMoves(check, getEggEnvironmentGeneration3(check.encounter), true);
                }
                if (isShedinjaFromNincada(check, evolutions, types))
                {
                    checkNincadaMoves(check, evolutions.back(), LearnEnvironment::E, GameVersion::RSE, true);
                }
                return areAllParsed(check.results);
            }

            // ---- Generation 4 ----

            void checkStageInternalGeneration4(const LearnCheck &check, const EvolutionCriteria &evolution, int stage)
            {
                int heartGoldRow = 0;
                int platinumRow = 0;
                int diamondRow = 0;
                if (!tryGetPersonal(LearnEnvironment::HGSS, evolution.speciesId, evolution.formId, heartGoldRow) || !tryGetPersonal(LearnEnvironment::PT, evolution.speciesId, evolution.formId, platinumRow))
                {
                    return;
                }
                tryGetPersonal(LearnEnvironment::DP, evolution.speciesId, evolution.formId, diamondRow);
                for (int slotIndex = MOVE_SLOT_COUNT - 1; slotIndex >= 0; --slotIndex)
                {
                    if (check.results[slotIndex].isValid())
                    {
                        continue;
                    }
                    // Level-up differs per game; TM and tutor data are shared (except Platinum's Defog).
                    const uint16_t moveId = check.current[slotIndex];
                    MoveLearnInfo info = getCanLearn(LearnEnvironment::HGSS, check.entity, heartGoldRow, evolution, moveId);
                    if (info.isEmpty())
                    {
                        info = getCanLearn(LearnEnvironment::PT, check.entity, platinumRow, evolution, moveId, MOVE_SOURCE_LEVEL_UP | MOVE_SOURCE_ALL_MACHINES);
                    }
                    if (info.isEmpty())
                    {
                        info = getCanLearn(LearnEnvironment::DP, check.entity, diamondRow, evolution, moveId, MOVE_SOURCE_LEVEL_UP);
                    }
                    if (!info.isEmpty())
                    {
                        check.results[slotIndex] = MoveResult::make(info, static_cast<uint8_t>(stage), GameVersion::DP);
                    }
                }
            }

            LearnEnvironment getEggEnvironmentGeneration4(const LearnEncounter &encounter) noexcept
            {
                const int version = static_cast<int>(encounter.version);
                if (version <= static_cast<int>(GameVersion::SS) || encounter.version == GameVersion::HGSS)
                {
                    return LearnEnvironment::HGSS;
                }
                if (encounter.version == GameVersion::Pt || encounter.version == GameVersion::PT)
                {
                    return LearnEnvironment::PT;
                }
                return LearnEnvironment::DP;
            }

            bool checkGeneration4(const LearnCheck &check, uint16_t types)
            {
                const std::span<const EvolutionCriteria> evolutions = getChain(check, GameVersion::DP);
                for (size_t stage = 0; stage < evolutions.size(); ++stage)
                {
                    const EvolutionCriteria &evolution = evolutions[stage];
                    if (evolution.speciesId != DEOXYS_SPECIES && evolution.speciesId != GIRATINA_SPECIES && evolution.speciesId != SHAYMIN_SPECIES)
                    {
                        checkStageInternalGeneration4(check, evolution, static_cast<int>(stage));
                        continue;
                    }
                    const uint8_t formCount = getFormCount(LearnEnvironment::HGSS, evolution.speciesId);
                    for (uint8_t formId = 0; formId < formCount; ++formId)
                    {
                        EvolutionCriteria formEvolution = evolution;
                        formEvolution.formId = formId;
                        checkStageInternalGeneration4(check, formEvolution, static_cast<int>(stage));
                    }
                }
                if (hasSourceType(types, MOVE_SOURCE_ENCOUNTER) && check.encounter.isEggTemplate && check.encounter.generation == 4)
                {
                    checkEggEncounterMoves(check, getEggEnvironmentGeneration4(check.encounter), true);
                }
                if (isShedinjaFromNincada(check, evolutions, types))
                {
                    // Ninjask's Gen 4 learnset is a superset of Gen 3's, so no reset.
                    checkNincadaMoves(check, evolutions.back(), LearnEnvironment::PT, GameVersion::DP, false);
                }
                return areAllParsed(check.results);
            }

            // ---- Generation 5 ----

            void checkStageInternalGeneration5(const LearnCheck &check, const EvolutionCriteria &evolution, int stage, uint16_t types, LearnOption option)
            {
                int black2Row = 0;
                if (!tryGetPersonal(LearnEnvironment::B2W2, evolution.speciesId, evolution.formId, black2Row))
                {
                    return;
                }
                // Some forms are missing from Black/White (Kyurem's).
                int blackRow = 0;
                const bool hasBlack = tryGetPersonal(LearnEnvironment::BW, evolution.speciesId, evolution.formId, blackRow);
                for (int slotIndex = MOVE_SLOT_COUNT - 1; slotIndex >= 0; --slotIndex)
                {
                    if (check.results[slotIndex].isValid())
                    {
                        continue;
                    }
                    const uint16_t moveId = check.current[slotIndex];
                    MoveLearnInfo info = getCanLearn(LearnEnvironment::B2W2, check.entity, black2Row, evolution, moveId, types, option);
                    if (!info.isEmpty())
                    {
                        check.results[slotIndex] = MoveResult::make(info, static_cast<uint8_t>(stage), GameVersion::BW);
                    }
                    if (!hasBlack)
                    {
                        continue;
                    }
                    info = getCanLearn(LearnEnvironment::BW, check.entity, blackRow, evolution, moveId, types & MOVE_SOURCE_LEVEL_UP, option);
                    if (!info.isEmpty())
                    {
                        check.results[slotIndex] = MoveResult::make(info, static_cast<uint8_t>(stage), GameVersion::BW);
                    }
                }
            }

            bool checkGeneration5(const LearnCheck &check, uint16_t types, LearnOption option)
            {
                const std::span<const EvolutionCriteria> evolutions = getChain(check, GameVersion::BW);
                for (size_t stage = 0; stage < evolutions.size(); ++stage)
                {
                    const EvolutionCriteria &evolution = evolutions[stage];
                    if (evolution.speciesId != DEOXYS_SPECIES && evolution.speciesId != GIRATINA_SPECIES && evolution.speciesId != SHAYMIN_SPECIES)
                    {
                        checkStageInternalGeneration5(check, evolution, static_cast<int>(stage), types, option);
                        continue;
                    }
                    const uint8_t formCount = getFormCount(LearnEnvironment::B2W2, evolution.speciesId);
                    for (uint8_t formId = 0; formId < formCount; ++formId)
                    {
                        EvolutionCriteria formEvolution = evolution;
                        formEvolution.formId = formId;
                        checkStageInternalGeneration5(check, formEvolution, static_cast<int>(stage), types, option);
                    }
                }
                if (hasSourceType(types, MOVE_SOURCE_ENCOUNTER) && check.encounter.isEggTemplate && check.encounter.generation == 5)
                {
                    const bool isBlack2 = static_cast<int>(check.encounter.version) > static_cast<int>(GameVersion::B);
                    checkEggEncounterMoves(check, isBlack2 ? LearnEnvironment::B2W2 : LearnEnvironment::BW, true);
                }
                return areAllParsed(check.results);
            }

            // ---- Generations 6 and 7 ----

            enum class DualGameMode : uint8_t
            {
                Both,
                First,
                Second,
            };

            /// LearnGroup6 / LearnGroup7 CheckBoth: the later game (all sources), then the earlier one's
            /// level-up moves.
            void checkBothGames(const LearnCheck &check, const EvolutionCriteria &evolution, int stage, uint16_t types, LearnOption option, LearnEnvironment later, LearnEnvironment earlier, GameVersion contextGroup)
            {
                int laterRow = 0;
                if (!tryGetPersonal(later, evolution.speciesId, evolution.formId, laterRow))
                {
                    return;
                }
                int earlierRow = 0;
                const bool hasEarlier = tryGetPersonal(earlier, evolution.speciesId, evolution.formId, earlierRow);
                for (int slotIndex = MOVE_SLOT_COUNT - 1; slotIndex >= 0; --slotIndex)
                {
                    if (check.results[slotIndex].isValid())
                    {
                        continue;
                    }
                    // LearnGroup6 skips a species X/Y lacks entirely (PKHeX checks xy_pi first).
                    if (contextGroup == GameVersion::XY && !hasEarlier)
                    {
                        continue;
                    }
                    const uint16_t moveId = check.current[slotIndex];
                    MoveLearnInfo info = getCanLearn(later, check.entity, laterRow, evolution, moveId, types, option);
                    if (!info.isEmpty())
                    {
                        check.results[slotIndex] = MoveResult::make(info, static_cast<uint8_t>(stage), contextGroup);
                        continue;
                    }
                    // LearnGroup7 reads Sun/Moon only for species it has.
                    if (contextGroup == GameVersion::SM && evolution.speciesId > MAX_SPECIES_ID_7_SUN_MOON)
                    {
                        continue;
                    }
                    info = getCanLearn(earlier, check.entity, contextGroup == GameVersion::SM ? laterRow : earlierRow, evolution, moveId, types & MOVE_SOURCE_LEVEL_UP, option);
                    if (!info.isEmpty())
                    {
                        check.results[slotIndex] = MoveResult::make(info, static_cast<uint8_t>(stage), contextGroup);
                    }
                }
            }

            void checkSingleGame(const LearnCheck &check, const EvolutionCriteria &evolution, int stage, uint16_t types, LearnOption option, LearnEnvironment environment, GameVersion contextGroup)
            {
                int rowIndex = 0;
                if (!tryGetPersonal(environment, evolution.speciesId, evolution.formId, rowIndex))
                {
                    return;
                }
                checkWithGame(check, environment, rowIndex, evolution, stage, types, option, contextGroup);
            }

            void checkDualStage(const LearnCheck &check, const EvolutionCriteria &evolution, int stage, uint16_t types, LearnOption option, DualGameMode mode, LearnEnvironment later, LearnEnvironment earlier, GameVersion contextGroup)
            {
                if (mode == DualGameMode::Both)
                {
                    checkBothGames(check, evolution, stage, types, option, later, earlier, contextGroup);
                }
                else if (mode == DualGameMode::Second)
                {
                    checkSingleGame(check, evolution, stage, types, option, later, contextGroup);
                }
                else
                {
                    checkSingleGame(check, evolution, stage, types, option, earlier, contextGroup);
                }
            }

            void checkDualGroup(const LearnCheck &check, uint16_t types, LearnOption option, DualGameMode mode, LearnEnvironment later, LearnEnvironment earlier, GameVersion contextGroup, uint8_t generation)
            {
                const std::span<const EvolutionCriteria> evolutions = getChain(check, contextGroup);
                for (size_t stage = 0; stage < evolutions.size(); ++stage)
                {
                    const EvolutionCriteria &evolution = evolutions[stage];
                    if (!shouldIterateForms(evolution.speciesId, generation, option))
                    {
                        checkDualStage(check, evolution, static_cast<int>(stage), types, option, mode, later, earlier, contextGroup);
                        continue;
                    }
                    int rowIndex = 0;
                    if (!tryGetPersonal(later, evolution.speciesId, evolution.formId, rowIndex))
                    {
                        continue;
                    }
                    const uint8_t formCount = getFormCount(later, evolution.speciesId);
                    for (uint8_t formId = 0; formId < formCount; ++formId)
                    {
                        EvolutionCriteria formEvolution = evolution;
                        formEvolution.formId = formId;
                        checkDualStage(check, formEvolution, static_cast<int>(stage), types, option, mode, later, earlier, contextGroup);
                    }
                }
            }

            bool isVersionPair(uint8_t version, GameVersion first, GameVersion second) noexcept
            {
                return version == static_cast<uint8_t>(first) || version == static_cast<uint8_t>(second);
            }

            bool checkGeneration6(const LearnCheck &check, uint16_t types, LearnOption option)
            {
                DualGameMode mode = DualGameMode::Both;
                if (check.encounter.contextGroup == GameVersion::XY && check.entity.isUntraded)
                {
                    mode = isVersionPair(check.entity.originVersion, GameVersion::AS, GameVersion::OR) ? DualGameMode::Second : DualGameMode::First;
                }
                checkDualGroup(check, types, option, mode, LearnEnvironment::ORAS, LearnEnvironment::XY, GameVersion::XY, 6);
                if (isPastOption(option) && hasSourceType(types, MOVE_SOURCE_ENCOUNTER))
                {
                    if (check.encounter.isEggTemplate && check.encounter.generation == 6)
                    {
                        const bool isOmegaRuby = static_cast<int>(check.encounter.version) > static_cast<int>(GameVersion::Y);
                        checkEggEncounterMoves(check, isOmegaRuby ? LearnEnvironment::ORAS : LearnEnvironment::XY, true);
                    }
                    else if (check.encounter.isDexNavSlot && check.encounter.isMoveBonusPossible && check.entity.isOriginalMovesetDeleted())
                    {
                        // One DexNav bonus move.
                        for (int slotIndex = 0; slotIndex < MOVE_SLOT_COUNT; ++slotIndex)
                        {
                            if (check.results[slotIndex].isValid() || !check.encounter.isMoveBonus(check.current[slotIndex]))
                            {
                                continue;
                            }
                            check.results[slotIndex] = MoveResult{MoveLearnInfo{LearnMethod::Encounter, LearnEnvironment::ORAS, 0}, 0, GameVersion::XY, 0};
                            break;
                        }
                    }
                }
                return areAllParsed(check.results);
            }

            bool checkGeneration7(const LearnCheck &check, uint16_t types, LearnOption option)
            {
                DualGameMode mode = DualGameMode::Both;
                if (check.encounter.contextGroup == GameVersion::SM && check.entity.isUntraded)
                {
                    mode = isVersionPair(check.entity.originVersion, GameVersion::US, GameVersion::UM) ? DualGameMode::Second : DualGameMode::First;
                }
                checkDualGroup(check, types, option, mode, LearnEnvironment::USUM, LearnEnvironment::SM, GameVersion::SM, 7);
                if (isPastOption(option) && hasSourceType(types, MOVE_SOURCE_ENCOUNTER) && check.encounter.isEggTemplate && check.encounter.generation == 7)
                {
                    const bool isUltraSun = static_cast<int>(check.encounter.version) > static_cast<int>(GameVersion::MN);
                    checkEggEncounterMoves(check, isUltraSun ? LearnEnvironment::USUM : LearnEnvironment::SM, true);
                }
                return areAllParsed(check.results);
            }

            // ---- Switch groups ----

            /// The single-game group shape (8, 8a, 8b, 9, 9a, Let's Go): every stage, iterating forms
            /// where a form change keeps moves. `levelUpOnlyForOtherForms` is BDSP's and Z-A's rule.
            void checkSwitchStages(const LearnCheck &check, LearnEnvironment environment, GameVersion contextGroup, uint16_t types, LearnOption option, bool iterateForms, bool levelUpOnlyForOtherForms)
            {
                const std::span<const EvolutionCriteria> evolutions = getChain(check, contextGroup);
                const uint8_t generation = getEnvironmentGeneration(environment);
                for (size_t stage = 0; stage < evolutions.size(); ++stage)
                {
                    const EvolutionCriteria &evolution = evolutions[stage];
                    if (!iterateForms || !shouldIterateForms(evolution.speciesId, generation, option))
                    {
                        checkSingleGame(check, evolution, static_cast<int>(stage), types, option, environment, contextGroup);
                        continue;
                    }
                    int rowIndex = 0;
                    if (!tryGetPersonal(environment, evolution.speciesId, evolution.formId, rowIndex))
                    {
                        continue;
                    }
                    const uint8_t formCount = getFormCount(environment, evolution.speciesId);
                    for (uint8_t formId = 0; formId < formCount; ++formId)
                    {
                        EvolutionCriteria formEvolution = evolution;
                        formEvolution.formId = formId;
                        const uint16_t formTypes = (levelUpOnlyForOtherForms && formId != 0) ? static_cast<uint16_t>(types & MOVE_SOURCE_LEVEL_UP) : types;
                        checkSingleGame(check, formEvolution, static_cast<int>(stage), formTypes, option, environment, contextGroup);
                    }
                }
            }

            bool checkHome(const LearnCheck &check, uint16_t types, LearnOption option);

            bool hasVisitedHome(const LearnCheck &check) noexcept
            {
                // ParseSettings.IgnoreTransferIfNoTracker is on by default: HOME counts only with a tracker.
                return check.entity.hasHomeTracker;
            }

            bool finishWithHome(const LearnCheck &check, uint16_t types, LearnOption option)
            {
                if (areAllParsed(check.results))
                {
                    return true;
                }
                if (option != LearnOption::Home && hasVisitedHome(check))
                {
                    return checkHome(check, types, option);
                }
                return false;
            }

            bool checkSwordShield(const LearnCheck &check, uint16_t types, LearnOption option)
            {
                const std::span<const EvolutionCriteria> evolutions = getChain(check, GameVersion::SWSH);
                if (evolutions.empty())
                {
                    return false;
                }
                checkSwitchStages(check, LearnEnvironment::SWSH, GameVersion::SWSH, types, option, true, false);
                if (check.encounter.isDownLeveledRaid)
                {
                    // A raid lowered to the joining player's level: check the original level range too.
                    const size_t stage = evolutions.size() - 1;
                    EvolutionCriteria original = evolutions[stage];
                    original.levelMin = original.levelMax;
                    original.levelMax = check.encounter.raidLevelMax;
                    checkSingleGame(check, original, static_cast<int>(stage), types & MOVE_SOURCE_LEVEL_UP, LearnOption::Current, LearnEnvironment::SWSH, GameVersion::SWSH);
                }
                checkSharedMoves(check, LearnEnvironment::SWSH, evolutions[0]);
                if (isPastOption(option) && hasSourceType(types, MOVE_SOURCE_ENCOUNTER) && check.entity.isOriginalMovesetDeleted() && check.encounter.isEggTemplate && check.encounter.contextGroup == GameVersion::SWSH)
                {
                    checkEggEncounterMoves(check, LearnEnvironment::SWSH, true);
                }
                return finishWithHome(check, types, option);
            }

            bool checkBrilliantDiamond(const LearnCheck &check, uint16_t types, LearnOption option)
            {
                const std::span<const EvolutionCriteria> evolutions = getChain(check, GameVersion::BDSP);
                if (evolutions.empty())
                {
                    return false;
                }
                // PKHeX calls the stage check with its default sources and option.
                checkSwitchStages(check, LearnEnvironment::BDSP, GameVersion::BDSP, MOVE_SOURCE_ALL, LearnOption::Current, true, true);
                checkSharedMoves(check, LearnEnvironment::BDSP, evolutions[0]);
                return finishWithHome(check, types, option);
            }

            bool checkLegendsArceus(const LearnCheck &check, uint16_t types, LearnOption option)
            {
                checkSwitchStages(check, LearnEnvironment::PLA, GameVersion::PLA, MOVE_SOURCE_ALL, LearnOption::Current, false, false);
                return finishWithHome(check, types, option);
            }

            bool checkScarletViolet(const LearnCheck &check, uint16_t types, LearnOption option)
            {
                const std::span<const EvolutionCriteria> evolutions = getChain(check, GameVersion::SV);
                if (evolutions.empty())
                {
                    return false;
                }
                checkSwitchStages(check, LearnEnvironment::SV, GameVersion::SV, types, option, true, false);
                checkSharedMoves(check, LearnEnvironment::SV, evolutions[0]);
                if (isPastOption(option) && hasSourceType(types, MOVE_SOURCE_ENCOUNTER) && check.entity.isOriginalMovesetDeleted() && check.encounter.isEggTemplate && check.encounter.contextGroup == GameVersion::SV)
                {
                    checkEggEncounterMoves(check, LearnEnvironment::SV, true);
                }
                return finishWithHome(check, types, option);
            }

            bool checkLegendsZA(const LearnCheck &check, uint16_t types, LearnOption option)
            {
                if (getChain(check, GameVersion::ZA).empty())
                {
                    return false;
                }
                checkSwitchStages(check, LearnEnvironment::ZA, GameVersion::ZA, MOVE_SOURCE_ALL, LearnOption::Current, true, true);
                return finishWithHome(check, types, option);
            }

            bool checkLetsGo(const LearnCheck &check)
            {
                checkSwitchStages(check, LearnEnvironment::GG, GameVersion::GG, MOVE_SOURCE_ALL, LearnOption::Current, false, false);
                return areAllParsed(check.results);
            }

            // ---- HOME ----

            /// PKHeX LearnGroupHOME.IsWipedPK8: a Sword/Shield Battle Version wiped every older move.
            bool isWipedSwordShield(const LearnEntity &entity) noexcept
            {
                return entity.formatGroup == GameVersion::SWSH && isVersionPair(entity.battleVersion, GameVersion::SW, GameVersion::SH);
            }

            LearnEnvironment getHomeLocalEnvironment(GameVersion contextGroup) noexcept
            {
                switch (contextGroup)
                {
                    case GameVersion::SWSH: return LearnEnvironment::SWSH;
                    case GameVersion::PLA: return LearnEnvironment::PLA;
                    case GameVersion::BDSP: return LearnEnvironment::BDSP;
                    case GameVersion::SV: return LearnEnvironment::SV;
                    case GameVersion::ZA: return LearnEnvironment::ZA;
                    default: return LearnEnvironment::None;
                }
            }

            /// PKHeX LearnGroupHOME.CleanPurge: drop what the current game cannot accept through HOME.
            bool cleanPurge(const LearnCheck &check, uint16_t types, LearnEnvironment local, std::span<const EvolutionCriteria> evolutions, LearnOption option)
            {
                if (option == LearnOption::AtAnyTime)
                {
                    return areAllParsed(check.results);
                }
                for (int slotIndex = 0; slotIndex < MOVE_SLOT_COUNT; ++slotIndex)
                {
                    MoveResult &result = check.results[slotIndex];
                    if (!result.isValid() || result.contextGroup == GameVersion::Invalid || result.info.environment == local)
                    {
                        continue;
                    }
                    const uint16_t moveId = check.current[slotIndex];
                    if (moveId == 0)
                    {
                        continue;
                    }
                    bool isValid = false;
                    for (const EvolutionCriteria &evolution : evolutions)
                    {
                        if (getCanLearnHome(local, check.entity, evolution, moveId, types).method != LearnMethod::None)
                        {
                            isValid = true;
                        }
                    }
                    // HOME lets Volt Tackle through outside any learnset.
                    if (!isValid && moveId != VOLT_TACKLE_MOVE && (isHomeEraContext(result.contextGroup) || local != LearnEnvironment::SWSH))
                    {
                        result = MoveResult{};
                    }
                }
                return areAllParsed(check.results);
            }

            bool addOriginalMoves(const LearnCheck &check, std::span<const uint16_t> moves, LearnEnvironment environment, bool onlyOne)
            {
                bool addedAny = false;
                for (const uint16_t moveId : moves)
                {
                    if (moveId == 0)
                    {
                        break;
                    }
                    const int slotIndex = indexOfMove(check.current, moveId);
                    if (slotIndex == -1 || check.results[slotIndex].isValid())
                    {
                        continue;
                    }
                    check.results[slotIndex] = MoveResult::initial(environment);
                    addedAny = true;
                    if (onlyOne)
                    {
                        return true;
                    }
                }
                return addedAny;
            }

            bool tryAddOriginalMoves(const LearnCheck &check, LearnOption option)
            {
                const LearnEncounter &encounter = check.encounter;
                const bool isWiped = option == LearnOption::Current && isWipedSwordShield(check.entity);
                if (encounter.goImportFormat != GoImportFormat::Other)
                {
                    if (isWiped && encounter.goImportFormat == GoImportFormat::PK7)
                    {
                        return false;
                    }
                    return addOriginalMoves(check, encounter.goInitialMoves, encounter.goImportFormat == GoImportFormat::PK7 ? LearnEnvironment::USUM : LearnEnvironment::GG, false);
                }
                if (encounter.isEggTemplate)
                {
                    if (isWiped && encounter.generation <= 7)
                    {
                        return false;
                    }
                    const LearnEnvironment environment = getLearnEnvironment(encounter.version);
                    if (addOriginalMoves(check, getInheritMoves(environment, encounter.speciesId, encounter.formId), environment, false))
                    {
                        return true;
                    }
                    if (addOriginalMoves(check, getEggMoves(environment, encounter.speciesId, encounter.formId), environment, false))
                    {
                        return true;
                    }
                }
                const bool isWipedOlder = isWiped && encounter.generation <= 7 && encounter.contextGroup != GameVersion::GG;
                if (encounter.hasMoves())
                {
                    if (isWipedOlder)
                    {
                        return false;
                    }
                    if (addOriginalMoves(check, encounter.moves, getLearnEnvironment(encounter.version), false))
                    {
                        return true;
                    }
                }
                if (encounter.hasRelearnMoves())
                {
                    if (isWipedOlder)
                    {
                        return false;
                    }
                    if (addOriginalMoves(check, encounter.relearnMoves, getLearnEnvironment(encounter.version), false))
                    {
                        return true;
                    }
                }
                if (encounter.isMoveBonusPossible)
                {
                    if (addOriginalMoves(check, encounter.moveBonusPossible, getLearnEnvironment(encounter.version), true))
                    {
                        return true;
                    }
                }
                return false;
            }

            /// PKHeX LearnGroupHOME.TryAddExclusiveMoves: HOME hands Hoopa its form's move.
            bool tryAddExclusiveMoves(const LearnCheck &check)
            {
                if (check.entity.speciesId != HOOPA_SPECIES)
                {
                    return false;
                }
                const uint16_t moveId = check.entity.formId == 0 ? HYPERSPACE_HOLE_MOVE : HYPERSPACE_FURY_MOVE;
                const int slotIndex = indexOfMove(check.current, moveId);
                if (slotIndex < 0 || check.results[slotIndex].isValid())
                {
                    return false;
                }
                check.results[slotIndex] = MoveResult{MoveLearnInfo{LearnMethod::Home, LearnEnvironment::Home, 0}, 0, GameVersion::Invalid, 0};
                return true;
            }

            bool checkHome(const LearnCheck &check, uint16_t types, LearnOption option)
            {
                const GameVersion context = check.entity.contextGroup;
                const LearnEnvironment local = getHomeLocalEnvironment(context);
                if (local == LearnEnvironment::None)
                {
                    return false;
                }
                const std::span<const EvolutionCriteria> evolutions = getChain(check, context);
                struct HomeVisit
                {
                    GameVersion contextGroup;
                    LearnGroup group;
                };
                // PKHeX's order: Scarlet/Violet, Z-A, Sword/Shield, Legends: Arceus, BDSP.
                constexpr HomeVisit visits[] = {
                    {GameVersion::SV, LearnGroup::ScarletViolet},
                    {GameVersion::ZA, LearnGroup::LegendsZA},
                    {GameVersion::SWSH, LearnGroup::SwordShield},
                    {GameVersion::PLA, LearnGroup::LegendsArceus},
                    {GameVersion::BDSP, LearnGroup::BrilliantDiamond},
                };
                for (const HomeVisit &visit : visits)
                {
                    if (!check.history.hasVisited(visit.contextGroup) || context == visit.contextGroup)
                    {
                        continue;
                    }
                    checkLearnGroup(visit.group, check, types, LearnOption::Home);
                    if (cleanPurge(check, types, local, evolutions, option))
                    {
                        return true;
                    }
                }
                if (tryAddOriginalMoves(check, option) && cleanPurge(check, types, local, evolutions, option))
                {
                    return true;
                }
                if (tryAddExclusiveMoves(check) && cleanPurge(check, types, local, evolutions, option))
                {
                    return true;
                }
                if (check.history.hasVisited(GameVersion::GG))
                {
                    checkLearnGroup(LearnGroup::LetsGo, check, types, LearnOption::Home);
                    if (cleanPurge(check, types, local, evolutions, option))
                    {
                        return true;
                    }
                }
                else if (check.history.hasVisited(GameVersion::SM))
                {
                    if (isWipedSwordShield(check.entity))
                    {
                        return false;
                    }
                    LearnGroup group = LearnGroup::Generation7;
                    while (true)
                    {
                        checkLearnGroup(group, check, types, LearnOption::Home);
                        if (cleanPurge(check, types, local, evolutions, option))
                        {
                            return true;
                        }
                        const LearnGroup previous = getPreviousLearnGroup(group, check, option);
                        if (previous == LearnGroup::None)
                        {
                            break;
                        }
                        group = previous;
                    }
                }
                return false;
            }

            /// LearnGroup8.GoBackwards.
            LearnGroup goBackwardsFromSwordShield(const LearnCheck &check, LearnOption option) noexcept
            {
                if (check.encounter.generation >= 8)
                {
                    return LearnGroup::None;
                }
                if (option == LearnOption::Current && check.entity.isOriginalMovesetDeleted())
                {
                    return LearnGroup::None;
                }
                if (check.history.hasVisited(GameVersion::GG))
                {
                    return LearnGroup::LetsGo;
                }
                return LearnGroup::Generation7;
            }
        }

        LearnGroup getCurrentLearnGroup(Enums::GameVersion contextGroup) noexcept
        {
            switch (contextGroup)
            {
                case GameVersion::RBY: return LearnGroup::Generation1;
                case GameVersion::GSC: return LearnGroup::Generation2;
                case GameVersion::RSE: return LearnGroup::Generation3;
                case GameVersion::DP: return LearnGroup::Generation4;
                case GameVersion::BW: return LearnGroup::Generation5;
                case GameVersion::XY: return LearnGroup::Generation6;
                case GameVersion::SM: return LearnGroup::Generation7;
                case GameVersion::GG: return LearnGroup::LetsGo;
                case GameVersion::SWSH: return LearnGroup::SwordShield;
                case GameVersion::PLA: return LearnGroup::LegendsArceus;
                case GameVersion::BDSP: return LearnGroup::BrilliantDiamond;
                case GameVersion::SV: return LearnGroup::ScarletViolet;
                case GameVersion::ZA: return LearnGroup::LegendsZA;
                default: return LearnGroup::None;
            }
        }

        LearnGroup getPreviousLearnGroup(LearnGroup group, const LearnCheck &check, LearnOption option) noexcept
        {
            const GameVersion entityContext = check.entity.contextGroup;
            const LearnEncounter &encounter = check.encounter;
            switch (group)
            {
                case LearnGroup::Generation1:
                    if (entityContext == GameVersion::RBY)
                    {
                        if (encounter.generation == 1 && hasPossiblyVisitedGeneration2(check.entity))
                        {
                            return LearnGroup::Generation2;
                        }
                        return encounter.generation == 2 ? LearnGroup::Generation2 : LearnGroup::None;
                    }
                    if (entityContext == GameVersion::GSC)
                    {
                        return LearnGroup::None;
                    }
                    return encounter.generation == 1 ? LearnGroup::Generation2 : LearnGroup::None;
                case LearnGroup::Generation2:
                    if (entityContext == GameVersion::GSC && encounter.contextGroup == GameVersion::RBY)
                    {
                        return LearnGroup::Generation1;
                    }
                    if (entityContext == GameVersion::RBY)
                    {
                        return LearnGroup::None;
                    }
                    return (encounter.contextGroup != GameVersion::RBY && !check.entity.isKorean && check.history.hasVisited(GameVersion::RBY)) ? LearnGroup::Generation1 : LearnGroup::None;
                case LearnGroup::Generation3:
                    return LearnGroup::None;
                case LearnGroup::Generation4:
                    return encounter.contextGroup == GameVersion::DP ? LearnGroup::None : LearnGroup::Generation3;
                case LearnGroup::Generation5:
                    return encounter.contextGroup == GameVersion::BW ? LearnGroup::None : LearnGroup::Generation4;
                case LearnGroup::Generation6:
                    return encounter.contextGroup == GameVersion::XY ? LearnGroup::None : LearnGroup::Generation5;
                case LearnGroup::Generation7:
                    switch (encounter.contextGroup)
                    {
                        case GameVersion::RBY: return LearnGroup::Generation1;
                        case GameVersion::GSC: return LearnGroup::Generation2;
                        case GameVersion::RSE:
                        case GameVersion::DP:
                        case GameVersion::BW:
                        case GameVersion::XY:
                            return LearnGroup::Generation6;
                        default: return LearnGroup::None;
                    }
                case LearnGroup::SwordShield:
                    if (option == LearnOption::AtAnyTimeChain)
                    {
                        return LearnGroup::Home;
                    }
                    return goBackwardsFromSwordShield(check, option);
                case LearnGroup::LegendsArceus:
                case LearnGroup::BrilliantDiamond:
                case LearnGroup::ScarletViolet:
                case LearnGroup::LegendsZA:
                    return option == LearnOption::AtAnyTimeChain ? LearnGroup::Home : LearnGroup::None;
                case LearnGroup::Home:
                    return option == LearnOption::AtAnyTimeChain ? goBackwardsFromSwordShield(check, option) : LearnGroup::None;
                default:
                    return LearnGroup::None;
            }
        }

        bool checkLearnGroup(LearnGroup group, const LearnCheck &check, uint16_t types, LearnOption option)
        {
            switch (group)
            {
                case LearnGroup::Generation1: return checkGeneration1(check, types);
                case LearnGroup::Generation2: return checkGeneration2(check, types, option);
                case LearnGroup::Generation3: return checkGeneration3(check, types);
                case LearnGroup::Generation4: return checkGeneration4(check, types);
                case LearnGroup::Generation5: return checkGeneration5(check, types, option);
                case LearnGroup::Generation6: return checkGeneration6(check, types, option);
                case LearnGroup::Generation7: return checkGeneration7(check, types, option);
                case LearnGroup::LetsGo: return checkLetsGo(check);
                case LearnGroup::SwordShield: return checkSwordShield(check, types, option);
                case LearnGroup::LegendsArceus: return checkLegendsArceus(check, types, option);
                case LearnGroup::BrilliantDiamond: return checkBrilliantDiamond(check, types, option);
                case LearnGroup::ScarletViolet: return checkScarletViolet(check, types, option);
                case LearnGroup::LegendsZA: return checkLegendsZA(check, types, option);
                case LearnGroup::Home: return checkHome(check, types, option);
                default: return false;
            }
        }

        bool shouldIterateForms(uint16_t speciesId, uint8_t generation, LearnOption option) noexcept
        {
            if (!isPastOption(option))
            {
                return speciesId == DEOXYS_SPECIES || speciesId == GIRATINA_SPECIES || speciesId == SHAYMIN_SPECIES || speciesId == HOOPA_SPECIES;
            }
            switch (speciesId)
            {
                case DEOXYS_SPECIES:
                case GIRATINA_SPECIES:
                case SHAYMIN_SPECIES:
                case ROTOM_SPECIES:
                case HOOPA_SPECIES:
                case TORNADUS_SPECIES:
                case THUNDURUS_SPECIES:
                case LANDORUS_SPECIES:
                case KYUREM_SPECIES:
                    return generation >= 6;
                case URSHIFU_SPECIES:
                case ENAMORUS_SPECIES:
                case NECROZMA_SPECIES:
                case CALYREX_SPECIES:
                    return generation >= 8;
                case PIKACHU_SPECIES:
                    return generation == 6;
                default:
                    return false;
            }
        }

        void markInitialMoves(std::span<MoveResult, 4> results, std::span<const uint16_t, 4> current, std::span<const uint16_t> initialMoves, LearnEnvironment environment) noexcept
        {
            for (const uint16_t moveId : initialMoves)
            {
                if (moveId == 0)
                {
                    break;
                }
                const int slotIndex = indexOfMove(current, moveId);
                if (slotIndex != -1)
                {
                    results[slotIndex] = MoveResult::initial(environment);
                }
            }
        }
    }
}
