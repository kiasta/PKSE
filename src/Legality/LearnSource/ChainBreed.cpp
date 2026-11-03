// Port of PKHeX PKHeX.Core/Legality/Moves/Breeding/Chain/ChainBreedLegality.cs, with the parts of
// ChainBreedTrace.cs and ChainBreedStep.cs the proof reads. See ChainBreed.h.
//
// PKHeX names a ruleset by GameVersion; this port names it by LearnEnvironment, which maps one to one
// for every version the chain visits: Gold/Silver (GS), Crystal (C), Ruby/Sapphire (RS), Emerald (E),
// FireRed (FR), LeafGreen (LG), Diamond/Pearl (DP), Platinum (PT), HeartGold/SoulSilver (HGSS),
// Black/White (BW) and Black 2/White 2 (B2W2). PKHeX's RSE and DPPt aggregates read Emerald and
// Platinum, and FRLG reads FireRed.
#include "Legality/LearnSource/ChainBreed.h"

#include <algorithm>
#include <array>

#include "Legality/Encounters/EncounterBreeding.h"
#include "Legality/Encounters/EncounterData3GC.h"
#include "Legality/Evolutions/EvolutionTree.h"
#include "Legality/LearnSource/LearnSourceConstants.h"
#include "Legality/LearnSource/LearnSources.h"
#include "Legality/LearnSource/LearnVerifier.h"
#include "Legality/LearnSource/MoveBreed.h"
#include "Legality/LearnSource/MoveResult.h"

namespace Legality
{
    namespace LearnSource
    {
        namespace
        {
            /// PKHeX MaxChainDepth: the longest known in-generation chain is about five fathers; the rest
            /// leaves room for transfer steps.
            constexpr int MAX_CHAIN_DEPTH = 10;
            /// PKHeX MaxMoveCount.
            constexpr int MAX_MOVE_COUNT = 4;
            /// PKHeX MaxTransferPredecessorVersions.
            constexpr int MAX_TRANSFER_PREDECESSOR_COUNT = 2;
            /// Room for every species-form one species can evolve into (PKHeX GetEvolutions).
            constexpr int MAX_EVOLUTION_COUNT = 32;

            /// PKHeX's internal flags for where a child move can come from.
            constexpr uint8_t FLAG_BASE = 1 << 0;
            constexpr uint8_t FLAG_LEVEL_UP = 1 << 1;
            constexpr uint8_t FLAG_GENERAL = 1 << 2;

            /// PKHeX EggGroup values the father test reads.
            constexpr uint8_t EGG_GROUP_NONE = 0;
            constexpr uint8_t EGG_GROUP_DITTO = 13;
            constexpr uint8_t EGG_GROUP_UNDISCOVERED = 15;

            /// PKHeX ChainBreedStepKind.
            enum class ChainBreedStepKind : uint8_t
            {
                None,
                Breed,
                Transfer,
            };

            /// PKHeX ChainBreedStep, without the parents (only the summary text reads them).
            struct ChainBreedStep
            {
                uint16_t speciesId = 0;
                uint8_t formId = 0;
                ChainBreedStepKind kind = ChainBreedStepKind::None;
                LearnEnvironment environment = LearnEnvironment::None;
                LearnEnvironment originEnvironment = LearnEnvironment::None;
                uint8_t moveCount = 0;
                std::array<uint16_t, MAX_MOVE_COUNT> moves{};

                /// PKHeX QueryEquals.
                bool queryEquals(const ChainBreedStep &other) const noexcept
                {
                    return kind == other.kind && speciesId == other.speciesId && formId == other.formId && environment == other.environment && originEnvironment == other.originEnvironment && moveCount == other.moveCount && moves == other.moves;
                }

                /// PKHeX HasMoveGen2: a move Gen 1 does not have.
                bool hasMoveGeneration2() const noexcept
                {
                    return std::any_of(moves.begin(), moves.end(), [](uint16_t moveId) { return moveId > MAX_MOVE_ID_1; });
                }
            };

            ChainBreedStep createStep(ChainBreedStepKind kind, uint16_t speciesId, uint8_t formId, LearnEnvironment environment, LearnEnvironment originEnvironment, std::span<const uint16_t> moves) noexcept
            {
                ChainBreedStep step{};
                step.speciesId = speciesId;
                step.formId = formId;
                step.kind = kind;
                step.environment = environment;
                step.originEnvironment = originEnvironment;
                step.moveCount = static_cast<uint8_t>(std::min<size_t>(moves.size(), MAX_MOVE_COUNT));
                std::copy_n(moves.begin(), step.moveCount, step.moves.begin());
                return step;
            }

            /// PKHeX ChainBreedTrace.
            struct ChainBreedTrace
            {
                std::array<ChainBreedStep, MAX_CHAIN_DEPTH> steps{};
                int stepCount = 0;
                int pendingStepCount = 0;
                /// PKHeX ChainBreedEraFlags.NintendoSwitchGBA.
                bool isSwitchGameBoyAdvanceEra = false;

                void setPending(int index, const ChainBreedStep &step) noexcept
                {
                    steps[index] = step;
                    pendingStepCount = index + 1;
                }

                void commit(int newStepCount) noexcept { stepCount = std::max(stepCount, newStepCount); }

                void setTransfer(int index, uint16_t speciesId, uint8_t formId, LearnEnvironment destination, LearnEnvironment origin, std::span<const uint16_t> moves) noexcept
                {
                    steps[index] = createStep(ChainBreedStepKind::Transfer, speciesId, formId, destination, origin, moves);
                }

                bool hasMoveGeneration2() const noexcept
                {
                    return std::any_of(steps.begin(), steps.begin() + pendingStepCount, [](const ChainBreedStep &step) { return step.hasMoveGeneration2(); });
                }
            };

            struct SpeciesFormPair
            {
                uint16_t speciesId = 0;
                uint8_t formId = 0;
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

            const Evolutions::EvolutionTree *getTree(LearnEnvironment environment) noexcept
            {
                return Evolutions::getEvolutionTree(getEnvironmentContextGroup(environment));
            }

            /// PKHeX IEvolutionForward.GetEvolutions for the ruleset's context.
            int getEvolutions(LearnEnvironment environment, uint16_t speciesId, uint8_t formId, std::span<Evolutions::SpeciesForm> result) noexcept
            {
                const Evolutions::EvolutionTree *tree = getTree(environment);
                return tree == nullptr ? 0 : tree->getEvolutions(speciesId, formId, result);
            }

            /// PKHeX IEvolutionReverse.GetReverse(...).First for the ruleset's context.
            SpeciesFormPair getPreEvolution(LearnEnvironment environment, uint16_t speciesId, uint8_t formId) noexcept
            {
                const Evolutions::EvolutionTree *tree = getTree(environment);
                if (tree == nullptr)
                {
                    return SpeciesFormPair{};
                }
                const Evolutions::EvolutionLink first = tree->getReverse(speciesId, formId).first;
                return SpeciesFormPair{first.sourceSpeciesId, first.sourceFormId};
            }

            /// PKHeX EggGroup.CanBreed.
            bool canEggGroupBreed(uint8_t eggGroup) noexcept
            {
                return eggGroup != EGG_GROUP_NONE && eggGroup != EGG_GROUP_DITTO && eggGroup != EGG_GROUP_UNDISCOVERED;
            }

            /// PKHeX IsMaleOnlySplitBreed.
            bool isMaleOnlySplitBreed(uint16_t speciesId) noexcept
            {
                return speciesId == NIDORAN_MALE_SPECIES || speciesId == VOLBEAT_SPECIES;
            }

            /// PKHeX RemapSpeciesToMother.
            uint16_t remapSpeciesToMother(uint16_t speciesId) noexcept
            {
                if (speciesId == NIDORAN_MALE_SPECIES)
                {
                    return NIDORAN_FEMALE_SPECIES;
                }
                if (speciesId == VOLBEAT_SPECIES)
                {
                    return ILLUMISE_SPECIES;
                }
                return speciesId;
            }

            /// PKHeX Breeding.IsSplitBreedNotBabySpecies.
            bool isSplitBreedNotBabySpecies(uint16_t speciesId, uint8_t generation) noexcept
            {
                if (generation == 3)
                {
                    return Encounters::isSplitBreedNotBabySpecies3(speciesId);
                }
                if (generation >= 4 && generation <= 8)
                {
                    return Encounters::isSplitBreedNotBabySpecies4(speciesId);
                }
                return false;
            }

            /// PKHeX TryGetEvolvedMother: the first evolution that can breed.
            bool tryGetEvolvedMother(uint16_t eggSpeciesId, uint8_t eggFormId, LearnEnvironment environment, SpeciesFormPair &newMother) noexcept
            {
                std::array<Evolutions::SpeciesForm, MAX_EVOLUTION_COUNT> evolutions{};
                const int evolutionCount = getEvolutions(environment, eggSpeciesId, eggFormId, evolutions);
                for (int evolutionIndex = 0; evolutionIndex < evolutionCount; ++evolutionIndex)
                {
                    const Evolutions::SpeciesForm evolution = evolutions[evolutionIndex];
                    if (getBreedingInfo(environment, evolution.speciesId, evolution.formId).eggGroup1 == EGG_GROUP_UNDISCOVERED)
                    {
                        continue;
                    }
                    newMother = SpeciesFormPair{evolution.speciesId, evolution.formId};
                    return true;
                }
                return false;
            }

            /// PKHeX ResolveMother: the species that lays the egg and its personal row.
            BreedingInfo resolveMother(uint16_t speciesId, uint8_t formId, LearnEnvironment environment, SpeciesFormPair &mother) noexcept
            {
                mother = SpeciesFormPair{speciesId, formId};
                if (isMaleOnlySplitBreed(speciesId))
                {
                    mother.speciesId = remapSpeciesToMother(speciesId);
                    return getBreedingInfo(environment, mother.speciesId, mother.formId);
                }
                BreedingInfo motherInfo = getBreedingInfo(environment, mother.speciesId, mother.formId);
                SpeciesFormPair evolvedMother{};
                if (motherInfo.eggGroup1 == EGG_GROUP_UNDISCOVERED && tryGetEvolvedMother(speciesId, formId, environment, evolvedMother))
                {
                    mother = evolvedMother;
                    motherInfo = getBreedingInfo(environment, mother.speciesId, mother.formId);
                }
                return motherInfo;
            }

            /// PKHeX IsCompatibleFather.
            bool isCompatibleFather(const BreedingInfo &mother, const BreedingInfo &father) noexcept
            {
                if (!canEggGroupBreed(father.eggGroup1))
                {
                    return false;
                }
                if (father.isGenderless() || father.isOnlyFemale())
                {
                    return false;
                }
                return mother.sharesCommonEggGroup(father);
            }

            /// PKHeX IsCompatibleFatherForMove / IsCompatibleFatherForBreeding (the two are the same test).
            bool isCompatibleFatherRelaxed(const BreedingInfo &mother, const BreedingInfo &father) noexcept
            {
                if (father.isGenderless() || father.isOnlyFemale())
                {
                    return false;
                }
                return mother.sharesCommonEggGroup(father);
            }

            /// PKHeX CanLearnDirectly1: Red/Blue or Yellow, reachable only while no Gen 2 move is in the chain.
            bool canLearnDirectlyGeneration1(const ChainBreedTrace &trace, uint16_t speciesId, uint8_t formId, uint16_t moveId)
            {
                if (moveId > MAX_MOVE_ID_1)
                {
                    return false;
                }
                // To travel to Gen 1 a Pokemon must lose every Gen 2 move.
                if (trace.hasMoveGeneration2())
                {
                    return false;
                }
                for (const LearnEnvironment environment : {LearnEnvironment::RB, LearnEnvironment::YW})
                {
                    int rowIndex = 0;
                    if (!tryGetPersonal(environment, speciesId, formId, rowIndex))
                    {
                        continue;
                    }
                    if (getLearnset(environment, speciesId, formId).isLearn(moveId) || isBreedingMachineMove(environment, rowIndex, moveId))
                    {
                        return true;
                    }
                }
                return false;
            }

            /// PKHeX CanLearnDirectly and CanLearnDirectly2 ... 5: level-up, machine and tutor sources of one ruleset.
            bool canLearnDirectly(const ChainBreedTrace &trace, uint16_t speciesId, uint8_t formId, LearnEnvironment environment, uint16_t moveId)
            {
                const uint8_t generation = getEnvironmentGeneration(environment);
                if (generation < 2 || generation > 5)
                {
                    return false;
                }
                if (generation == 2 && moveId > MAX_MOVE_ID_2)
                {
                    return false;
                }

                int rowIndex = 0;
                const bool hasPersonal = tryGetPersonal(environment, speciesId, formId, rowIndex);
                if (hasPersonal)
                {
                    if (getLearnset(environment, speciesId, formId).isLearn(moveId))
                    {
                        return true;
                    }
                    if (isBreedingMachineMove(environment, rowIndex, moveId, true))
                    {
                        return true;
                    }
                    if (environment == LearnEnvironment::C && isBreedingTutorCrystal(rowIndex, moveId))
                    {
                        return true;
                    }
                    if (generation == 3)
                    {
                        if (isTutorFireRedLeafGreen(speciesId, moveId))
                        {
                            return true; // FireRed/LeafGreen and Emerald
                        }
                        if (trace.isSwitchGameBoyAdvanceEra)
                        {
                            return false; // No other game to pull from.
                        }
                        return isTutorEmerald(speciesId, moveId) || isTutorColosseumXD(speciesId, moveId);
                    }
                }
                // Gen 2 falls back to Gen 1 whether or not the species is in the Gen 2 game.
                return generation == 2 && canLearnDirectlyGeneration1(trace, speciesId, formId, moveId);
            }

            /// PKHeX CanLearnFromEncounterSpecial for Encounters3XD.Shadow.
            bool canLearnFromShadow3XD(uint16_t speciesId, uint16_t moveId)
            {
                for (const Encounters::EncounterShadow3XD &shadow : Encounters::getShadow3XD())
                {
                    if (shadow.speciesId() == speciesId && shadow.moves().contains(moveId))
                    {
                        return true;
                    }
                }
                return false;
            }

            /// PKHeX CanLearnFromHistoricalSource: initial-move encounters and past-generation learnsets,
            /// reached once every egg source is exhausted.
            bool canLearnFromHistoricalSource(const ChainBreedTrace &trace, uint16_t speciesId, uint8_t formId, LearnEnvironment environment, uint16_t moveId)
            {
                const uint8_t generation = getEnvironmentGeneration(environment);
                if (generation >= 3 && moveId <= MAX_MOVE_ID_3)
                {
                    if (canLearnDirectly(trace, speciesId, formId, LearnEnvironment::RS, moveId))
                    {
                        return true;
                    }
                    if (!trace.isSwitchGameBoyAdvanceEra && canLearnFromShadow3XD(speciesId, moveId))
                    {
                        return true;
                    }
                }
                // HeartGold/SoulSilver's Dratini gift knows Extreme Speed.
                if (generation >= 4 && moveId <= MAX_MOVE_ID_4 && speciesId == DRATINI_SPECIES && moveId == EXTREME_SPEED_MOVE)
                {
                    return true;
                }
                // Gen 5 can receive Gen 4 machine moves by transfer (for example Shellder with Avalanche).
                if (generation >= 5 && moveId <= MAX_MOVE_ID_4)
                {
                    if (canLearnDirectly(trace, speciesId, formId, LearnEnvironment::PT, moveId) || canLearnDirectly(trace, speciesId, formId, LearnEnvironment::HGSS, moveId))
                    {
                        return true;
                    }
                }
                return false;
            }

            /// PKHeX CanLearnDirectlyInLine: the species, its pre-evolutions, its evolutions, then the
            /// historical sources.
            bool canLearnDirectlyInLine(const ChainBreedTrace &trace, uint16_t speciesId, uint8_t formId, LearnEnvironment environment, uint16_t moveId)
            {
                if (speciesId == SMEARGLE_SPECIES)
                {
                    return isSketchValid(moveId, getEnvironmentContextGroup(environment));
                }

                SpeciesFormPair current{speciesId, formId};
                while (true)
                {
                    if (canLearnDirectly(trace, current.speciesId, current.formId, environment, moveId))
                    {
                        return true;
                    }
                    const SpeciesFormPair previous = getPreEvolution(environment, current.speciesId, current.formId);
                    if (previous.speciesId == 0)
                    {
                        break;
                    }
                    current = previous;
                }

                // Branched evolutions all count: any of them can source an egg move (Tyrogue's three).
                std::array<Evolutions::SpeciesForm, MAX_EVOLUTION_COUNT> evolutions{};
                const int evolutionCount = getEvolutions(environment, speciesId, formId, evolutions);
                for (int evolutionIndex = 0; evolutionIndex < evolutionCount; ++evolutionIndex)
                {
                    if (canLearnDirectly(trace, evolutions[evolutionIndex].speciesId, evolutions[evolutionIndex].formId, environment, moveId))
                    {
                        return true;
                    }
                }

                return canLearnFromHistoricalSource(trace, speciesId, formId, environment, moveId);
            }

            /// PKHeX MarkChildMoveFlags2 ... 5: where each requested move can come from for the child.
            /// The personal row and egg moves are those of the ruleset's own game, except that Ruby/Sapphire
            /// read Emerald's and LeafGreen reads FireRed's (identical for anything that can breed).
            bool markChildMoveFlags(uint16_t speciesId, uint8_t formId, LearnEnvironment environment, std::span<const uint16_t> moves, const Learnset &learnset, std::span<uint8_t> flags)
            {
                const uint8_t generation = getEnvironmentGeneration(environment);
                if (generation < 2 || generation > 5)
                {
                    return false;
                }
                LearnEnvironment personalEnvironment = environment;
                if (environment == LearnEnvironment::RS)
                {
                    personalEnvironment = LearnEnvironment::E;
                }
                else if (environment == LearnEnvironment::LG)
                {
                    personalEnvironment = LearnEnvironment::FR;
                }

                int rowIndex = 0;
                tryGetPersonal(personalEnvironment, speciesId, formId, rowIndex);
                const bool inheritLevelUp = canInheritMoves(speciesId);
                const std::span<const uint16_t> baseMoves = learnset.getBaseEggMoves(getEggLevel(generation));
                const std::span<const uint16_t> eggMoves = getEggMoves(personalEnvironment, speciesId, formId);

                for (size_t moveIndex = 0; moveIndex < moves.size(); ++moveIndex)
                {
                    const uint16_t moveId = moves[moveIndex];
                    uint8_t sourceFlags = 0;
                    if (containsMove(baseMoves, moveId))
                    {
                        sourceFlags |= FLAG_BASE;
                    }
                    // Gen 2 reads no source past its own last move.
                    if (generation != 2 || moveId <= MAX_MOVE_ID_2)
                    {
                        if (containsMove(eggMoves, moveId))
                        {
                            sourceFlags |= FLAG_GENERAL;
                        }
                        if (isBreedingMachineMove(personalEnvironment, rowIndex, moveId, true))
                        {
                            sourceFlags |= FLAG_GENERAL;
                        }
                        if (inheritLevelUp && learnset.isLearn(moveId))
                        {
                            sourceFlags |= FLAG_LEVEL_UP;
                        }
                        if (environment == LearnEnvironment::C && isBreedingTutorCrystal(rowIndex, moveId))
                        {
                            sourceFlags |= FLAG_GENERAL;
                        }
                    }
                    if (sourceFlags == 0)
                    {
                        return false;
                    }
                    flags[moveIndex] = sourceFlags;
                }
                return true;
            }

            /// PKHeX IsValidBaseCount: the first `baseCount` moves are the egg's base moves in order, and
            /// every later move is inheritable and not a base move that would sit earlier.
            bool isValidBaseCount(int baseCount, std::span<const uint16_t> moves, std::span<const uint16_t> baseMoves, std::span<const uint8_t> flags)
            {
                const int baseMoveCount = static_cast<int>(baseMoves.size());
                const int moveCount = static_cast<int>(moves.size());
                if (baseMoveCount < baseCount)
                {
                    return false;
                }
                for (int moveIndex = 0; moveIndex < baseCount; ++moveIndex)
                {
                    if ((flags[moveIndex] & FLAG_BASE) == 0)
                    {
                        return false;
                    }
                    if (moves[moveIndex] != baseMoves[baseMoveCount - baseCount + moveIndex])
                    {
                        return false;
                    }
                }
                for (int moveIndex = baseCount; moveIndex < moveCount; ++moveIndex)
                {
                    if ((flags[moveIndex] & (FLAG_LEVEL_UP | FLAG_GENERAL)) == 0)
                    {
                        return false;
                    }
                    if ((flags[moveIndex] & FLAG_BASE) == 0)
                    {
                        continue;
                    }
                    const int baseIndex = indexOfMove(baseMoves, moves[moveIndex]);
                    if (baseIndex == -1)
                    {
                        continue;
                    }
                    const int minimumIndex = moveCount - baseMoveCount + baseIndex;
                    if (moveIndex < minimumIndex + baseCount)
                    {
                        return false;
                    }
                }
                return true;
            }

            /// PKHeX GetEggSpeciesCandidates: the base of the father's line, plus the split-breed parent
            /// (Marill for Azurill, ...) when the line has one.
            int getEggSpeciesCandidates(uint16_t speciesId, uint8_t formId, LearnEnvironment environment, std::span<SpeciesFormPair, 2> result) noexcept
            {
                const uint8_t generation = getEnvironmentGeneration(environment);
                SpeciesFormPair current{speciesId, formId};
                SpeciesFormPair baseSpecies{};
                SpeciesFormPair split{};
                while (true)
                {
                    baseSpecies = current;
                    if (split.speciesId == 0 && isSplitBreedNotBabySpecies(current.speciesId, generation))
                    {
                        split = current;
                    }
                    const SpeciesFormPair previous = getPreEvolution(environment, current.speciesId, current.formId);
                    if (previous.speciesId == 0)
                    {
                        break;
                    }
                    current = previous;
                }
                result[0] = baseSpecies;
                int candidateCount = 1;
                if (split.speciesId != 0 && (split.speciesId != baseSpecies.speciesId || split.formId != baseSpecies.formId))
                {
                    result[candidateCount++] = split;
                }
                return candidateCount;
            }

            /// PKHeX GetTransferPredecessorVersions: there is no Gen 2 to Gen 3 transfer, and recursing
            /// one generation at a time also permits Gen 3 to Gen 4 to Gen 5.
            int getTransferPredecessors(LearnEnvironment environment, std::span<LearnEnvironment, MAX_TRANSFER_PREDECESSOR_COUNT> result) noexcept
            {
                switch (getEnvironmentGeneration(environment))
                {
                    case 4:
                        result[0] = LearnEnvironment::E;  // PKHeX RSE
                        result[1] = LearnEnvironment::FR; // PKHeX FRLG
                        return 2;
                    case 5:
                        result[0] = LearnEnvironment::HGSS;
                        result[1] = LearnEnvironment::PT; // PKHeX DPPt
                        return 2;
                    default:
                        return 0;
                }
            }

            bool tryValidateCore(uint16_t eggSpeciesId, uint8_t eggFormId, LearnEnvironment environment, std::span<const uint16_t> moves, std::span<const uint16_t> baseMoves, std::span<const uint8_t> flags, ChainBreedTrace &trace, int depth);
            bool canFatherKnowAllMoves(uint16_t fatherSpeciesId, uint8_t fatherFormId, LearnEnvironment environment, std::span<const uint16_t> moves, ChainBreedTrace &trace, int depth);

            /// PKHeX CanFatherKnowAllMovesPast: the father may have come from the generation before with
            /// its whole moveset.
            bool canFatherKnowAllMovesPast(uint16_t fatherSpeciesId, uint8_t fatherFormId, LearnEnvironment environment, std::span<const uint16_t> moves, ChainBreedTrace &trace, int depth)
            {
                if (depth >= MAX_CHAIN_DEPTH)
                {
                    return false;
                }
                std::array<LearnEnvironment, MAX_TRANSFER_PREDECESSOR_COUNT> predecessors{};
                const int predecessorCount = getTransferPredecessors(environment, predecessors);
                for (int predecessorIndex = 0; predecessorIndex < predecessorCount; ++predecessorIndex)
                {
                    trace.setTransfer(depth, fatherSpeciesId, fatherFormId, environment, predecessors[predecessorIndex], moves);
                    if (!canFatherKnowAllMoves(fatherSpeciesId, fatherFormId, predecessors[predecessorIndex], moves, trace, depth + 1))
                    {
                        continue;
                    }
                    // A transfer changes neither the breeding links nor the father the caller reports.
                    trace.commit(depth + 1);
                    return true;
                }
                return false;
            }

            /// PKHeX CanFatherKnowAllMoves: the father learns each move itself, or was bred with the rest.
            bool canFatherKnowAllMoves(uint16_t fatherSpeciesId, uint8_t fatherFormId, LearnEnvironment environment, std::span<const uint16_t> moves, ChainBreedTrace &trace, int depth)
            {
                std::array<uint16_t, MAX_MOVE_COUNT> pending{};
                int pendingCount = 0;
                for (const uint16_t moveId : moves)
                {
                    if (!canLearnDirectlyInLine(trace, fatherSpeciesId, fatherFormId, environment, moveId))
                    {
                        pending[pendingCount++] = moveId;
                    }
                }
                if (pendingCount == 0)
                {
                    return true;
                }

                const std::span<const uint16_t> pendingMoves(pending.data(), pendingCount);
                std::array<SpeciesFormPair, 2> eggSpecies{};
                const int eggSpeciesCount = getEggSpeciesCandidates(fatherSpeciesId, fatherFormId, environment, eggSpecies);
                std::array<uint8_t, MAX_MOVE_COUNT> flags{};
                const uint8_t eggLevel = getEggLevel(getEnvironmentGeneration(environment));
                for (int candidateIndex = 0; candidateIndex < eggSpeciesCount; ++candidateIndex)
                {
                    const SpeciesFormPair candidate = eggSpecies[candidateIndex];
                    if (candidate.speciesId == 0)
                    {
                        continue;
                    }
                    const Learnset learnset = getLearnset(environment, candidate.speciesId, candidate.formId);
                    flags.fill(0);
                    if (!markChildMoveFlags(candidate.speciesId, candidate.formId, environment, pendingMoves, learnset, std::span<uint8_t>(flags.data(), pendingCount)))
                    {
                        continue;
                    }
                    const std::span<const uint16_t> baseMoves = learnset.getBaseEggMoves(eggLevel);
                    if (!tryValidateCore(candidate.speciesId, candidate.formId, environment, pendingMoves, baseMoves, std::span<const uint8_t>(flags.data(), pendingCount), trace, depth))
                    {
                        continue;
                    }
                    return true;
                }

                return canFatherKnowAllMovesPast(fatherSpeciesId, fatherFormId, environment, pendingMoves, trace, depth);
            }

            /// PKHeX TryResolveFatherViaEvolution: an egg species that cannot breed (Tyrogue) can be
            /// fathered by its evolutions.
            bool tryResolveFatherViaEvolution(uint16_t eggSpeciesId, uint8_t eggFormId, LearnEnvironment environment, std::span<const uint16_t> moves, ChainBreedTrace &trace, int depth)
            {
                std::array<Evolutions::SpeciesForm, MAX_EVOLUTION_COUNT> evolutions{};
                const int evolutionCount = getEvolutions(environment, eggSpeciesId, eggFormId, evolutions);
                for (int evolutionIndex = 0; evolutionIndex < evolutionCount; ++evolutionIndex)
                {
                    if (canFatherKnowAllMoves(evolutions[evolutionIndex].speciesId, evolutions[evolutionIndex].formId, environment, moves, trace, depth))
                    {
                        return true;
                    }
                }
                return false;
            }

            /// PKHeX TryResolveFather: some compatible father in the game knows every inherited move.
            bool tryResolveFather(uint16_t eggSpeciesId, uint8_t eggFormId, LearnEnvironment environment, std::span<const uint16_t> moves, ChainBreedTrace &trace, int depth)
            {
                if (!isPresentInGame(environment, eggSpeciesId, eggFormId))
                {
                    return false;
                }
                SpeciesFormPair mother{};
                const BreedingInfo motherInfo = resolveMother(eggSpeciesId, eggFormId, environment, mother);
                if (motherInfo.isGenderless() || motherInfo.isOnlyMale() || motherInfo.eggGroup1 == EGG_GROUP_UNDISCOVERED)
                {
                    return tryResolveFatherViaEvolution(eggSpeciesId, eggFormId, environment, moves, trace, depth);
                }

                const uint16_t maxSpecies = getMaxSpecies(environment);
                for (uint16_t fatherSpeciesId = 1; fatherSpeciesId <= maxSpecies; ++fatherSpeciesId)
                {
                    const uint8_t formCount = getFormCount(environment, fatherSpeciesId);
                    for (uint8_t fatherFormId = 0; fatherFormId < formCount; ++fatherFormId)
                    {
                        if (!isPresentInGame(environment, fatherSpeciesId, fatherFormId))
                        {
                            continue;
                        }
                        if (!isCompatibleFather(motherInfo, getBreedingInfo(environment, fatherSpeciesId, fatherFormId)))
                        {
                            continue;
                        }
                        if (!canFatherKnowAllMoves(fatherSpeciesId, fatherFormId, environment, moves, trace, depth))
                        {
                            continue;
                        }
                        return true;
                    }
                }
                return false;
            }

            /// PKHeX TryResolveInheritedSources. Both branches recurse identically, so a move with either
            /// an inheritable or a level-up source moves on; PKHeX keeps the two calls and so does this.
            bool tryResolveInheritedSources(uint16_t eggSpeciesId, uint8_t eggFormId, LearnEnvironment environment, std::span<const uint16_t> moves, std::span<const uint8_t> flags, size_t index, ChainBreedTrace &trace, int depth)
            {
                if (index == moves.size())
                {
                    return tryResolveFather(eggSpeciesId, eggFormId, environment, moves, trace, depth);
                }
                const uint8_t flag = flags[index];
                if ((flag & FLAG_GENERAL) != 0 && tryResolveInheritedSources(eggSpeciesId, eggFormId, environment, moves, flags, index + 1, trace, depth))
                {
                    return true;
                }
                if ((flag & FLAG_LEVEL_UP) != 0 && tryResolveInheritedSources(eggSpeciesId, eggFormId, environment, moves, flags, index + 1, trace, depth))
                {
                    return true;
                }
                return false;
            }

            /// PKHeX TryValidateBaseCounts: try every count of leading base moves, then resolve the rest.
            bool tryValidateBaseCounts(uint16_t eggSpeciesId, uint8_t eggFormId, LearnEnvironment environment, std::span<const uint16_t> moves, std::span<const uint16_t> baseMoves, std::span<const uint8_t> flags, int maxBase, ChainBreedTrace &trace, int depth)
            {
                std::array<uint16_t, MAX_MOVE_COUNT> inheritedMoves{};
                for (int baseCount = 0; baseCount <= maxBase; ++baseCount)
                {
                    if (!isValidBaseCount(baseCount, moves, baseMoves, flags))
                    {
                        continue;
                    }
                    const size_t inheritedCount = moves.size() - baseCount;
                    if (inheritedCount == 0)
                    {
                        return true;
                    }
                    // An Undiscovered egg species (a baby) can still inherit through its evolved forms;
                    // tryResolveFather handles that through tryResolveFatherViaEvolution.
                    std::copy_n(moves.begin() + baseCount, inheritedCount, inheritedMoves.begin());
                    if (tryResolveInheritedSources(eggSpeciesId, eggFormId, environment, std::span<const uint16_t>(inheritedMoves.data(), inheritedCount), flags.subspan(baseCount, inheritedCount), 0, trace, depth + 1))
                    {
                        return true;
                    }
                }
                return false;
            }

            /// PKHeX TryValidateCore: one breeding step, with the cycle check.
            bool tryValidateCore(uint16_t eggSpeciesId, uint8_t eggFormId, LearnEnvironment environment, std::span<const uint16_t> moves, std::span<const uint16_t> baseMoves, std::span<const uint8_t> flags, ChainBreedTrace &trace, int depth)
            {
                if (depth < 0 || depth >= MAX_CHAIN_DEPTH)
                {
                    return false;
                }
                const ChainBreedStep state = createStep(ChainBreedStepKind::Breed, eggSpeciesId, eggFormId, environment, environment, moves);
                for (int stepIndex = 0; stepIndex < depth; ++stepIndex)
                {
                    if (trace.steps[stepIndex].queryEquals(state))
                    {
                        return false;
                    }
                }
                trace.setPending(depth, state);

                const int maxBase = static_cast<int>(std::min(moves.size(), baseMoves.size()));
                const bool isValid = tryValidateBaseCounts(eggSpeciesId, eggFormId, environment, moves, baseMoves, flags, maxBase, trace, depth);
                if (isValid)
                {
                    trace.commit(depth + 1);
                }
                return isValid;
            }

            /// PKHeX CanFatherLearnAll.
            bool canFatherLearnAll(const ChainBreedTrace &trace, std::span<const uint16_t> moves, LearnEnvironment environment, uint16_t fatherSpeciesId, uint8_t fatherFormId)
            {
                for (const uint16_t moveId : moves)
                {
                    if (moveId == 0)
                    {
                        break;
                    }
                    if (!canLearnDirectlyInLine(trace, fatherSpeciesId, fatherFormId, environment, moveId))
                    {
                        return false;
                    }
                }
                return true;
            }

            /// PKHeX CanSingleFatherPassAllMovesRelaxed.
            bool canSingleFatherPassAllMovesRelaxed(const ChainBreedTrace &trace, const BreedingInfo &motherInfo, std::span<const uint16_t> moves, LearnEnvironment environment)
            {
                const uint16_t maxSpecies = getMaxSpecies(environment);
                for (uint16_t fatherSpeciesId = 1; fatherSpeciesId <= maxSpecies; ++fatherSpeciesId)
                {
                    const uint8_t formCount = getFormCount(environment, fatherSpeciesId);
                    for (uint8_t fatherFormId = 0; fatherFormId < formCount; ++fatherFormId)
                    {
                        if (!isPresentInGame(environment, fatherSpeciesId, fatherFormId))
                        {
                            continue;
                        }
                        if (!isCompatibleFatherRelaxed(motherInfo, getBreedingInfo(environment, fatherSpeciesId, fatherFormId)))
                        {
                            continue;
                        }
                        if (canFatherLearnAll(trace, moves, environment, fatherSpeciesId, fatherFormId))
                        {
                            return true;
                        }
                    }
                }
                return false;
            }

            /// PKHeX CanAnyCompatibleFatherLearnMove: some father in the mother's egg groups learns the move
            /// by level-up (a machine or tutor move is always obtainable by re-breeding).
            bool canAnyCompatibleFatherLearnMove(const BreedingInfo &motherInfo, uint16_t moveId, LearnEnvironment environment)
            {
                const uint16_t maxSpecies = getMaxSpecies(environment);
                for (uint16_t fatherSpeciesId = 1; fatherSpeciesId <= maxSpecies; ++fatherSpeciesId)
                {
                    const uint8_t formCount = getFormCount(environment, fatherSpeciesId);
                    for (uint8_t fatherFormId = 0; fatherFormId < formCount; ++fatherFormId)
                    {
                        if (!isPresentInGame(environment, fatherSpeciesId, fatherFormId))
                        {
                            continue;
                        }
                        if (!isCompatibleFatherRelaxed(motherInfo, getBreedingInfo(environment, fatherSpeciesId, fatherFormId)))
                        {
                            continue;
                        }
                        uint8_t level = 0;
                        if (getLearnset(environment, fatherSpeciesId, fatherFormId).tryGetLevelLearnMove(moveId, level))
                        {
                            return true;
                        }
                    }
                }
                return false;
            }

            /// PKHeX IsValidRelaxed: Gen 6 on, where mothers pass egg moves too.
            bool isValidRelaxed(const ChainBreedTrace &trace, uint16_t speciesId, uint8_t formId, LearnEnvironment environment, std::span<const uint16_t> moves)
            {
                // A genderless offspring comes from Ditto: no level-up moves are inherited and no egg moves assigned.
                if (getBreedingInfo(environment, speciesId, formId).isGenderless())
                {
                    return true;
                }
                SpeciesFormPair mother{};
                const BreedingInfo motherInfo = resolveMother(speciesId, formId, environment, mother);
                // Every case but a female-only mother is solved by one father, or several, passing the moves.
                if (!motherInfo.isOnlyFemale())
                {
                    return true;
                }

                const bool isMaleSplit = isMaleOnlySplitBreed(speciesId);
                const bool ignoreEggMoves = getEnvironmentGeneration(environment) >= 8; // Gen 8 on shares egg moves.
                const bool checkBaby = speciesId != VOLBEAT_SPECIES && speciesId != NIDORAN_MALE_SPECIES;

                if (isMaleSplit && !ignoreEggMoves)
                {
                    // The moves the mother cannot hold as egg moves must all come from one father.
                    std::array<uint16_t, MAX_MOVE_COUNT> fatherMustPass{};
                    int fatherMoveCount = 0;
                    const std::span<const uint16_t> motherEggMoves = getEggMoves(environment, mother.speciesId, mother.formId);
                    for (const uint16_t moveId : moves)
                    {
                        if (moveId == 0)
                        {
                            break;
                        }
                        if (!containsMove(motherEggMoves, moveId))
                        {
                            fatherMustPass[fatherMoveCount++] = moveId;
                        }
                    }
                    if (fatherMoveCount > 0 && !canSingleFatherPassAllMovesRelaxed(trace, motherInfo, std::span<const uint16_t>(fatherMustPass.data(), fatherMoveCount), environment))
                    {
                        return false;
                    }
                    return true;
                }

                // A level-up move needs a compatible father that learns it.
                const Learnset babyLearnset = getLearnset(environment, speciesId, formId);
                const Learnset evolvedLearnset = getLearnset(environment, mother.speciesId, mother.formId);
                for (const uint16_t moveId : moves)
                {
                    if (moveId == 0)
                    {
                        break;
                    }
                    uint8_t level = 0;
                    const bool isLevelUpMove = (checkBaby && babyLearnset.tryGetLevelLearnMove(moveId, level)) || evolvedLearnset.tryGetLevelLearnMove(moveId, level);
                    if (!isLevelUpMove)
                    {
                        continue;
                    }
                    if (!canAnyCompatibleFatherLearnMove(motherInfo, moveId, environment))
                    {
                        return false;
                    }
                }
                return true;
            }
        }

        bool isChainBreedValid(uint16_t speciesId, uint8_t formId, Enums::GameVersion version, bool isSwitchGameBoyAdvanceEra, std::span<const uint16_t> moves)
        {
            ChainBreedTrace trace{};
            trace.isSwitchGameBoyAdvanceEra = isSwitchGameBoyAdvanceEra;

            size_t moveCount = static_cast<size_t>(std::find(moves.begin(), moves.end(), static_cast<uint16_t>(0)) - moves.begin());
            if (moveCount == 0 || moveCount > MAX_MOVE_COUNT)
            {
                return false;
            }
            const std::span<const uint16_t> requestedMoves = moves.first(moveCount);

            const LearnEnvironment environment = getLearnEnvironment(version);
            const uint8_t generation = getEnvironmentGeneration(environment);
            if (generation >= 6)
            {
                return isValidRelaxed(trace, speciesId, formId, environment, requestedMoves);
            }

            const Learnset learnset = getLearnset(environment, speciesId, formId);
            const std::span<const uint16_t> baseMoves = learnset.getBaseEggMoves(getEggLevel(generation));
            std::array<uint8_t, MAX_MOVE_COUNT> flags{};
            if (!markChildMoveFlags(speciesId, formId, environment, requestedMoves, learnset, std::span<uint8_t>(flags.data(), moveCount)))
            {
                return false;
            }
            return tryValidateCore(speciesId, formId, environment, requestedMoves, baseMoves, std::span<const uint8_t>(flags.data(), moveCount), trace, 0);
        }
    }
}
