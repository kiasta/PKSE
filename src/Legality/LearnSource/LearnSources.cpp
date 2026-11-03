// Port of PKHeX PKHeX.Core/Legality/LearnSource/Sources (every game's ILearnSource), GameData.GetLearnSource
// and Breeding.GetCanInheritMoves. See LearnSources.h.
#include "Legality/LearnSource/LearnSources.h"

#include <algorithm>

#include "Enums/GameVersion.h"
#include "Legality/LearnSource/LearnSourceConstants.h"
#include "Pokemon/FormInfo.h"
#include "Pokemon/LearnSourceTable.h"
#include "Pokemon/MoveRecordTable.h"

namespace Legality
{
    namespace LearnSource
    {
        namespace
        {
            /// PKHeX's Gen 3 TM count, before the HM bits in the machine flags.
            constexpr int TECHNICAL_MACHINE_COUNT_GENERATION3 = 50;
            constexpr int TECHNICAL_MACHINE_COUNT_GENERATION4 = 92;
            /// PKHeX PersonalInfo bit counts (each accessor's bounds check).
            constexpr int MACHINE_BIT_COUNT_1RBY = 55;
            constexpr int MACHINE_BIT_COUNT_2GSC = 57;
            constexpr int TUTOR_BIT_COUNT_2GSC = 3;
            constexpr int HIDDEN_MACHINE_COUNT_GENERATION4 = 8;
            constexpr int MACHINE_BIT_COUNT_5BW = 101;
            constexpr int TYPE_TUTOR_BIT_COUNT_5BW = 8;
            constexpr int MACHINE_BIT_COUNT_6XY = 105;
            constexpr int TYPE_TUTOR_BIT_COUNT_6XY = 7;
            constexpr int MACHINE_BIT_COUNT_6ORAS = 107;
            constexpr int TYPE_TUTOR_BIT_COUNT_6ORAS = 8;
            constexpr int MACHINE_BIT_COUNT_7SM = 105;
            constexpr int TYPE_TUTOR_BIT_COUNT_7SM = 8;
            constexpr int MACHINE_BIT_COUNT_7LGPE = 60;
            constexpr int MACHINE_BIT_COUNT_8SWSH = 100;
            constexpr int TYPE_TUTOR_BIT_COUNT_8SWSH = 8;
            constexpr int MACHINE_BIT_COUNT_8BDSP = 100;
            constexpr int TYPE_TUTOR_BIT_COUNT_8BDSP = 4;
            /// LearnSource5BW: TM95 (index 94) is not available in Black/White.
            constexpr int UNAVAILABLE_MACHINE_INDEX_5BW = 94;
            /// LearnSource8SWSH.GetIsTR: TR12 (Agility), which Calyrex knows without the TR glitch.
            constexpr int CALYREX_AGILITY_RECORD_INDEX = 12;
            /// LearnSource9SV: the level the 2.0.1 update stopped teaching these at.
            constexpr uint8_t LARVESTA_BUG_BITE_LEVEL = 28;
            constexpr uint8_t ZORUA_HISUI_SPITE_LEVEL = 24;
            constexpr int VERSION_LANGUAGE_KOREAN = 8;

            bool hasGameTables(LearnEnvironment environment) noexcept
            {
                return environment != LearnEnvironment::None && environment != LearnEnvironment::Stadium2 && environment != LearnEnvironment::Home;
            }

            Pokemon::LearnSourceGame getTableGame(LearnEnvironment environment) noexcept
            {
                switch (environment)
                {
                    case LearnEnvironment::RB: return Pokemon::LearnSourceGame::RB;
                    case LearnEnvironment::YW: return Pokemon::LearnSourceGame::YW;
                    case LearnEnvironment::GS: return Pokemon::LearnSourceGame::GS;
                    case LearnEnvironment::C: return Pokemon::LearnSourceGame::C;
                    case LearnEnvironment::RS: return Pokemon::LearnSourceGame::RS;
                    case LearnEnvironment::E: return Pokemon::LearnSourceGame::E;
                    case LearnEnvironment::FR: return Pokemon::LearnSourceGame::FR;
                    case LearnEnvironment::LG: return Pokemon::LearnSourceGame::LG;
                    case LearnEnvironment::DP: return Pokemon::LearnSourceGame::DP;
                    case LearnEnvironment::PT: return Pokemon::LearnSourceGame::PT;
                    case LearnEnvironment::HGSS: return Pokemon::LearnSourceGame::HGSS;
                    case LearnEnvironment::BW: return Pokemon::LearnSourceGame::BW;
                    case LearnEnvironment::B2W2: return Pokemon::LearnSourceGame::B2W2;
                    case LearnEnvironment::XY: return Pokemon::LearnSourceGame::XY;
                    case LearnEnvironment::ORAS: return Pokemon::LearnSourceGame::ORAS;
                    case LearnEnvironment::SM: return Pokemon::LearnSourceGame::SM;
                    case LearnEnvironment::USUM: return Pokemon::LearnSourceGame::USUM;
                    case LearnEnvironment::GG: return Pokemon::LearnSourceGame::GG;
                    case LearnEnvironment::SWSH: return Pokemon::LearnSourceGame::SWSH;
                    case LearnEnvironment::BDSP: return Pokemon::LearnSourceGame::BDSP;
                    case LearnEnvironment::PLA: return Pokemon::LearnSourceGame::PLA;
                    case LearnEnvironment::SV: return Pokemon::LearnSourceGame::SV;
                    case LearnEnvironment::ZA: return Pokemon::LearnSourceGame::ZA;
                    default: return Pokemon::LearnSourceGame::Count;
                }
            }

            const Pokemon::LearnSourceTables &getTables(LearnEnvironment environment) noexcept
            {
                return Pokemon::getLearnSourceTables(getTableGame(environment));
            }

            /// PKHeX PersonalTable.GetFormIndex (PersonalInfo.FormIndex / HasForm).
            int getFormIndex(const Pokemon::LearnSourceTables &tables, uint16_t speciesId, uint8_t formId) noexcept
            {
                if (speciesId > tables.maxSpecies)
                {
                    return 0;
                }
                if (tables.formStatsIndex == nullptr || formId == 0)
                {
                    return speciesId;
                }
                const uint16_t formStatsIndex = tables.formStatsIndex[speciesId];
                if (formStatsIndex == 0 || formId >= tables.formCounts[speciesId])
                {
                    return speciesId;
                }
                return formStatsIndex + formId - 1;
            }

            std::span<const uint16_t> getEntryMoves(const uint16_t *moves, const uint16_t *offsets, int entryIndex) noexcept
            {
                return std::span<const uint16_t>(moves + offsets[entryIndex], moves + offsets[entryIndex + 1]);
            }

            bool containsMove(std::span<const uint16_t> moves, uint16_t moveId) noexcept
            {
                return std::find(moves.begin(), moves.end(), moveId) != moves.end();
            }

            /// Position of `moveId` in a list, -1 when absent (PKHeX ReadOnlySpan.IndexOf).
            int indexOfMove(const uint16_t *moves, int count, uint16_t moveId) noexcept
            {
                for (int moveIndex = 0; moveIndex < count; ++moveIndex)
                {
                    if (moves[moveIndex] == moveId)
                    {
                        return moveIndex;
                    }
                }
                return -1;
            }

            /// One permission bit of a personal row: PKHeX's `(Data[offset + (index >> 3)] >> (index & 7)) & 1`
            /// with the accessor's `(uint)index >= count` bounds check.
            bool readPermission(const Pokemon::LearnSourceTables &tables, int rowIndex, int regionOffset, int bitIndex, int bitCount) noexcept
            {
                if (bitIndex < 0 || bitIndex >= bitCount || tables.permissions == nullptr || rowIndex < 0 || rowIndex >= tables.personalRowCount)
                {
                    return false;
                }
                const uint8_t permissionByte = tables.permissions[rowIndex * tables.permissionRowSize + regionOffset + (bitIndex >> 3)];
                return ((permissionByte >> (bitIndex & 7)) & 1) != 0;
            }

            bool readListPermission(const Pokemon::LearnSourceTables &tables, int rowIndex, int regionOffset, const uint16_t *moves, int moveCount, uint16_t moveId, int bitCount) noexcept
            {
                return readPermission(tables, rowIndex, regionOffset, indexOfMove(moves, moveCount, moveId), bitCount);
            }

            MoveLearnInfo learnInfo(LearnMethod method, LearnEnvironment environment, uint8_t argument = 0) noexcept
            {
                return MoveLearnInfo{method, environment, argument};
            }

            /// The level-up check most learn sources share: a level-up move at or below the stage's
            /// highest level.
            bool tryLevelUpWithinMax(LearnEnvironment environment, const Evolutions::EvolutionCriteria &evolution, uint16_t moveId, uint8_t &level) noexcept
            {
                return getLearnset(environment, evolution.speciesId, evolution.formId).tryGetLevelLearnMove(moveId, level) && level <= evolution.levelMax;
            }

            /// PKHeX GetIsEnhancedTutor's Rotom arm: an appliance form's own move (any of them when
            /// looking into the past).
            bool isRotomFormMove(uint8_t currentFormId, uint16_t moveId, bool allowPast) noexcept
            {
                switch (moveId)
                {
                    case OVERHEAT_MOVE: return allowPast || currentFormId == 1;
                    case HYDRO_PUMP_MOVE: return allowPast || currentFormId == 2;
                    case BLIZZARD_MOVE: return allowPast || currentFormId == 3;
                    case AIR_SLASH_MOVE: return allowPast || currentFormId == 4;
                    case LEAF_STORM_MOVE: return allowPast || currentFormId == 5;
                    default: return false;
                }
            }

            bool isNecrozmaFusionMove(uint8_t currentFormId, uint16_t moveId, LearnOption option) noexcept
            {
                if (moveId == SUNSTEEL_STRIKE_MOVE)
                {
                    return isPastOption(option) || currentFormId == 1;
                }
                if (moveId == MOONGEIST_BEAM_MOVE)
                {
                    return isPastOption(option) || currentFormId == 2;
                }
                return false;
            }

            /// LearnSource5BW / 5B2W2 / 6XY / 6AO GetIsEnhancedTutor.
            bool isEnhancedTutorGeneration5And6(uint16_t speciesId, uint8_t currentFormId, uint16_t moveId, LearnOption option) noexcept
            {
                switch (speciesId)
                {
                    case KELDEO_SPECIES: return moveId == SECRET_SWORD_MOVE;
                    case MELOETTA_SPECIES: return moveId == RELIC_SONG_MOVE;
                    case ROTOM_SPECIES: return isRotomFormMove(currentFormId, moveId, isPastOption(option));
                    default: return false;
                }
            }

            /// LearnSource7SM / 7USUM GetIsEnhancedTutor.
            bool isEnhancedTutorGeneration7(uint16_t speciesId, uint8_t currentFormId, uint16_t moveId, LearnOption option) noexcept
            {
                switch (speciesId)
                {
                    case PIKACHU_SPECIES:
                    case RAICHU_SPECIES:
                        return moveId == VOLT_TACKLE_MOVE;
                    case NECROZMA_SPECIES: return isNecrozmaFusionMove(currentFormId, moveId, option);
                    case KELDEO_SPECIES: return moveId == SECRET_SWORD_MOVE;
                    case MELOETTA_SPECIES: return moveId == RELIC_SONG_MOVE;
                    case ROTOM_SPECIES: return isRotomFormMove(currentFormId, moveId, isPastOption(option));
                    case ZYGARDE_SPECIES: return moveId == EXTREME_SPEED_MOVE || moveId == DRAGON_DANCE_MOVE || moveId == THOUSAND_ARROWS_MOVE || moveId == THOUSAND_WAVES_MOVE || moveId == CORE_ENFORCER_MOVE;
                    default: return false;
                }
            }

            /// LearnSource4DP / Pt / HGSS GetIsTypeTutor (the starter ultimates and Draco Meteor).
            bool isTypeTutorGeneration4(uint16_t speciesId, uint16_t moveId) noexcept
            {
                const uint16_t *species = nullptr;
                int speciesCount = 0;
                switch (moveId)
                {
                    case BLAST_BURN_MOVE:
                        species = Pokemon::BLAST_BURN_TUTOR_SPECIES_GENERATION4;
                        speciesCount = Pokemon::BLAST_BURN_TUTOR_SPECIES_GENERATION4_COUNT;
                        break;
                    case HYDRO_CANNON_MOVE:
                        species = Pokemon::HYDRO_CANNON_TUTOR_SPECIES_GENERATION4;
                        speciesCount = Pokemon::HYDRO_CANNON_TUTOR_SPECIES_GENERATION4_COUNT;
                        break;
                    case FRENZY_PLANT_MOVE:
                        species = Pokemon::FRENZY_PLANT_TUTOR_SPECIES_GENERATION4;
                        speciesCount = Pokemon::FRENZY_PLANT_TUTOR_SPECIES_GENERATION4_COUNT;
                        break;
                    case DRACO_METEOR_MOVE:
                        species = Pokemon::DRACO_METEOR_TUTOR_SPECIES_GENERATION4;
                        speciesCount = Pokemon::DRACO_METEOR_TUTOR_SPECIES_GENERATION4_COUNT;
                        break;
                    default:
                        return false;
                }
                return indexOfMove(species, speciesCount, speciesId) >= 0;
            }

            /// LearnSource8SWSH.GetIsTR.
            bool isTechnicalRecord8SWSH(const LearnEntity &entity, const Evolutions::EvolutionCriteria &evolution, uint16_t moveId, LearnOption option) noexcept
            {
                const int recordIndex = indexOfMove(Pokemon::TR_MOVES_8SWSH, Pokemon::TR_COUNT_8SWSH, moveId);
                if (recordIndex == -1)
                {
                    return false;
                }
                if (!Pokemon::isTRPermitted8SWSH(evolution.speciesId, evolution.formId, recordIndex))
                {
                    return false;
                }
                if (entity.formatGroup == Enums::GameVersion::SWSH)
                {
                    if (entity.isTechnicalRecordFlagSet(recordIndex))
                    {
                        return true;
                    }
                    if (!isFlagCheckRequired(option))
                    {
                        return true;
                    }
                }
                else if (option != LearnOption::Current)
                {
                    return true;
                }
                return recordIndex == CALYREX_AGILITY_RECORD_INDEX && evolution.speciesId == CALYREX_SPECIES && evolution.formId == 0;
            }

            /// LearnSource9SV.GetIsTM.
            bool isTechnicalMachine9SV(const LearnEntity &entity, const Evolutions::EvolutionCriteria &evolution, uint16_t moveId, LearnOption option) noexcept
            {
                const int machineIndex = indexOfMove(Pokemon::TM_MOVES_9SV, Pokemon::TM_COUNT_9SV, moveId);
                if (machineIndex == -1)
                {
                    return false;
                }
                if (!Pokemon::isTMPermitted9SV(evolution.speciesId, evolution.formId, machineIndex))
                {
                    return false;
                }
                if (entity.formatGroup == Enums::GameVersion::SV)
                {
                    if (entity.isTechnicalMachineFlagSet(machineIndex))
                    {
                        return true;
                    }
                    if (!isFlagCheckRequired(option))
                    {
                        return true;
                    }
                }
                else if (option != LearnOption::Current)
                {
                    return true;
                }
                return false;
            }

            /// LearnSource9ZA.GetIsTM: a TM can be used at any time and sets no record flag.
            bool isTechnicalMachine9ZA(const Evolutions::EvolutionCriteria &evolution, uint16_t moveId) noexcept
            {
                const int machineIndex = indexOfMove(Pokemon::TM_MOVES_9LZA, Pokemon::TM_COUNT_9LZA, moveId);
                return machineIndex != -1 && Pokemon::isTMPermitted9LZA(evolution.speciesId, evolution.formId, machineIndex);
            }

            /// PersonalInfo8LA.GetIsLearnMoveShop.
            bool isMoveShop8LA(const Evolutions::EvolutionCriteria &evolution, uint16_t moveId) noexcept
            {
                const int shopIndex = indexOfMove(Pokemon::MOVE_SHOP_MOVES_8LA, Pokemon::MOVE_SHOP_COUNT_8LA, moveId);
                return shopIndex != -1 && ((Pokemon::getMoveShopPermissions8LA(evolution.speciesId, evolution.formId) >> shopIndex) & 1) != 0;
            }

            /// LearnSource8SWSH / 8BDSP / 9SV GetIsSharedEggMove: the egg moves of the species it hatches from.
            bool isSharedEggMove(LearnEnvironment environment, const Evolutions::EvolutionCriteria &evolution, uint16_t moveId) noexcept
            {
                uint16_t hatchSpeciesId = 0;
                uint8_t hatchFormId = 0;
                getHatchSpeciesForm(environment, evolution.speciesId, evolution.formId, hatchSpeciesId, hatchFormId);
                return containsMove(getEggMoves(environment, hatchSpeciesId, hatchFormId), moveId);
            }

            /// LearnSource9SV.GetIsReminderMove.
            bool isReminderMove9SV(const Evolutions::EvolutionCriteria &evolution, uint16_t moveId) noexcept
            {
                const Pokemon::LearnSourceTables &tables = getTables(LearnEnvironment::SV);
                const int formIndex = getFormIndex(tables, evolution.speciesId, evolution.formId);
                if (tables.reminderMoves == nullptr || formIndex >= tables.reminderCount)
                {
                    return false;
                }
                return containsMove(getEntryMoves(tables.reminderMoves, tables.reminderOffsets, formIndex), moveId);
            }

            MoveLearnInfo getCanLearnGeneration1(LearnEnvironment environment, const LearnEntity &entity, int rowIndex, const Evolutions::EvolutionCriteria &evolution, uint16_t moveId, uint16_t types) noexcept
            {
                if (moveId > MAX_MOVE_ID_1)
                {
                    return MoveLearnInfo{};
                }
                const Pokemon::LearnSourceTables &tables = getTables(environment);
                if (hasSourceType(types, MOVE_SOURCE_MACHINE) && readListPermission(tables, rowIndex, Pokemon::PERMISSION_MACHINE_RB, Pokemon::MACHINE_MOVES_1RBY, Pokemon::MACHINE_MOVES_1RBY_COUNT, moveId, MACHINE_BIT_COUNT_1RBY))
                {
                    return learnInfo(LearnMethod::TechnicalMachine, environment);
                }
                // Surf Pikachu, which only Stadium (Game Boy era) teaches.
                if (hasSourceType(types, MOVE_SOURCE_SPECIAL_TUTOR) && entity.allowsGameBoyCartridgeEra && moveId == SURF_MOVE && (evolution.speciesId == PIKACHU_SPECIES || evolution.speciesId == RAICHU_SPECIES))
                {
                    return learnInfo(LearnMethod::Tutor, environment);
                }
                if (hasSourceType(types, MOVE_SOURCE_LEVEL_UP))
                {
                    uint8_t level = 0;
                    if (getLearnset(environment, evolution.speciesId, 0).tryGetLevelLearnMove(moveId, level) && evolution.isInsideLevelRange(level))
                    {
                        return learnInfo(LearnMethod::LevelUp, environment, level);
                    }
                }
                return MoveLearnInfo{};
            }

            MoveLearnInfo getCanLearnGeneration2(LearnEnvironment environment, const LearnEntity &entity, int rowIndex, const Evolutions::EvolutionCriteria &evolution, uint16_t moveId, uint16_t types) noexcept
            {
                if (moveId > MAX_MOVE_ID_2)
                {
                    return MoveLearnInfo{};
                }
                const Pokemon::LearnSourceTables &tables = getTables(environment);
                if (hasSourceType(types, MOVE_SOURCE_MACHINE) && readListPermission(tables, rowIndex, Pokemon::PERMISSION_MACHINE_GS, Pokemon::MACHINE_MOVES_2GSC, Pokemon::MACHINE_MOVES_2GSC_COUNT, moveId, MACHINE_BIT_COUNT_2GSC))
                {
                    return learnInfo(LearnMethod::TechnicalMachine, environment);
                }
                if (environment == LearnEnvironment::C && hasSourceType(types, MOVE_SOURCE_SPECIAL_TUTOR) && !entity.isKorean)
                {
                    // Crystal's tutor flags live in the type-tutor bits after the machine flags.
                    const int tutorIndex = indexOfMove(Pokemon::TUTOR_MOVES_2GSC, Pokemon::TUTOR_MOVES_2GSC_COUNT, moveId);
                    if (tutorIndex != -1 && readPermission(getTables(LearnEnvironment::C), evolution.speciesId, Pokemon::PERMISSION_MACHINE_C, MACHINE_BIT_COUNT_2GSC + tutorIndex, MACHINE_BIT_COUNT_2GSC + TUTOR_BIT_COUNT_2GSC))
                    {
                        return learnInfo(LearnMethod::Tutor, environment);
                    }
                }
                if (hasSourceType(types, MOVE_SOURCE_LEVEL_UP))
                {
                    uint8_t level = 0;
                    if (getLearnset(environment, evolution.speciesId, evolution.formId).tryGetLevelLearnMove(moveId, level))
                    {
                        if (evolution.isInsideLevelRange(level))
                        {
                            return learnInfo(LearnMethod::LevelUp, environment, level);
                        }
                        // Gold/Silver: a level 1 move is an evolution move.
                        if (environment == LearnEnvironment::GS && level == 1 && hasSourceType(types, MOVE_SOURCE_EVOLVE))
                        {
                            return learnInfo(LearnMethod::Evolution, environment, level);
                        }
                    }
                }
                return MoveLearnInfo{};
            }

            MoveLearnInfo getCanLearnGeneration3(LearnEnvironment environment, const LearnEntity &entity, int rowIndex, const Evolutions::EvolutionCriteria &evolution, uint16_t moveId, uint16_t types) noexcept
            {
                const Pokemon::LearnSourceTables &tables = getTables(environment);
                uint8_t level = 0;
                if (hasSourceType(types, MOVE_SOURCE_LEVEL_UP) && tryLevelUpWithinMax(environment, evolution, moveId, level))
                {
                    return learnInfo(LearnMethod::LevelUp, environment, level);
                }
                if (hasSourceType(types, MOVE_SOURCE_MACHINE))
                {
                    const int machineBitCount = TECHNICAL_MACHINE_COUNT_GENERATION3 + Pokemon::HIDDEN_MACHINE_MOVES_GENERATION3_COUNT;
                    if (readListPermission(tables, rowIndex, Pokemon::PERMISSION_MACHINE_RS, Pokemon::TECHNICAL_MACHINE_MOVES_GENERATION3, Pokemon::TECHNICAL_MACHINE_MOVES_GENERATION3_COUNT, moveId, machineBitCount))
                    {
                        return learnInfo(LearnMethod::TechnicalMachine, environment);
                    }
                    // HM moves must be removed before transferring.
                    const int hiddenIndex = indexOfMove(Pokemon::HIDDEN_MACHINE_MOVES_GENERATION3, Pokemon::HIDDEN_MACHINE_MOVES_GENERATION3_COUNT, moveId);
                    if (entity.formatGeneration == 3 && hiddenIndex != -1 && readPermission(tables, rowIndex, Pokemon::PERMISSION_MACHINE_RS, TECHNICAL_MACHINE_COUNT_GENERATION3 + hiddenIndex, machineBitCount))
                    {
                        return learnInfo(LearnMethod::TechnicalMachine, environment);
                    }
                }
                if (hasSourceType(types, MOVE_SOURCE_SPECIAL_TUTOR))
                {
                    bool isTutor = false;
                    if (environment == LearnEnvironment::RS)
                    {
                        isTutor = isTutorColosseumXD(evolution.speciesId, moveId);
                    }
                    else if (environment == LearnEnvironment::E)
                    {
                        isTutor = readListPermission(getTables(LearnEnvironment::E), evolution.speciesId, Pokemon::PERMISSION_TUTOR_E, Pokemon::TUTOR_MOVES_3RSE, Pokemon::TUTOR_MOVES_3RSE_COUNT, moveId, Pokemon::TUTOR_MOVES_3RSE_COUNT);
                    }
                    else
                    {
                        isTutor = (moveId == BLAST_BURN_MOVE && evolution.speciesId == CHARIZARD_SPECIES) || (moveId == HYDRO_CANNON_MOVE && evolution.speciesId == BLASTOISE_SPECIES) || (moveId == FRENZY_PLANT_MOVE && evolution.speciesId == VENUSAUR_SPECIES);
                    }
                    if (isTutor)
                    {
                        return learnInfo(LearnMethod::Tutor, environment);
                    }
                }
                return MoveLearnInfo{};
            }

            MoveLearnInfo getCanLearnGeneration4(LearnEnvironment environment, const LearnEntity &entity, int rowIndex, const Evolutions::EvolutionCriteria &evolution, uint16_t moveId, uint16_t types, LearnOption option) noexcept
            {
                const Pokemon::LearnSourceTables &tables = getTables(environment);
                uint8_t level = 0;
                if (hasSourceType(types, MOVE_SOURCE_LEVEL_UP) && tryLevelUpWithinMax(environment, evolution, moveId, level))
                {
                    return learnInfo(LearnMethod::LevelUp, environment, level);
                }
                if (hasSourceType(types, MOVE_SOURCE_MACHINE))
                {
                    if (readListPermission(tables, rowIndex, Pokemon::PERMISSION_MACHINE_DP, Pokemon::TECHNICAL_MACHINE_MOVES_GENERATION4, Pokemon::TECHNICAL_MACHINE_MOVES_GENERATION4_COUNT, moveId, TECHNICAL_MACHINE_COUNT_GENERATION4))
                    {
                        return learnInfo(LearnMethod::TechnicalMachine, environment);
                    }
                    // Diamond/Pearl read the HM bit through GetIsLearnTM(92 + index), which its bounds check
                    // always refuses; Platinum and HeartGold/SoulSilver read the real HM bits.
                    const bool isHeartGold = environment == LearnEnvironment::HGSS;
                    const uint16_t keptHiddenMachine = isHeartGold ? WHIRLPOOL_MOVE : DEFOG_MOVE;
                    if ((moveId == keptHiddenMachine || entity.formatGeneration == 4) && environment != LearnEnvironment::DP)
                    {
                        const uint16_t *hiddenMoves = isHeartGold ? Pokemon::HIDDEN_MACHINE_MOVES_4HGSS : Pokemon::HIDDEN_MACHINE_MOVES_4DP;
                        const int hiddenIndex = indexOfMove(hiddenMoves, HIDDEN_MACHINE_COUNT_GENERATION4, moveId);
                        if (hiddenIndex != -1 && readPermission(tables, rowIndex, Pokemon::PERMISSION_MACHINE_DP, TECHNICAL_MACHINE_COUNT_GENERATION4 + hiddenIndex, TECHNICAL_MACHINE_COUNT_GENERATION4 + HIDDEN_MACHINE_COUNT_GENERATION4))
                        {
                            return learnInfo(LearnMethod::TechnicalMachine, environment);
                        }
                    }
                }
                if (hasSourceType(types, MOVE_SOURCE_TYPE_TUTOR) && isTypeTutorGeneration4(evolution.speciesId, moveId))
                {
                    return learnInfo(LearnMethod::Tutor, environment);
                }
                // Only HeartGold/SoulSilver's table carries tutor flags (PersonalTable.PopulateGen4Tutors).
                if (hasSourceType(types, MOVE_SOURCE_SPECIAL_TUTOR) && environment == LearnEnvironment::HGSS && readListPermission(tables, rowIndex, Pokemon::PERMISSION_TUTOR_HGSS, Pokemon::TUTOR_MOVES_GENERATION4, Pokemon::TUTOR_MOVES_GENERATION4_COUNT, moveId, Pokemon::TUTOR_MOVES_GENERATION4_COUNT))
                {
                    return learnInfo(LearnMethod::Tutor, environment);
                }
                if (hasSourceType(types, MOVE_SOURCE_ENHANCED_TUTOR) && environment != LearnEnvironment::DP && evolution.speciesId == ROTOM_SPECIES && isRotomFormMove(entity.formId, moveId, isPastOption(option)))
                {
                    return learnInfo(LearnMethod::Tutor, environment);
                }
                return MoveLearnInfo{};
            }

            MoveLearnInfo getCanLearnGeneration5(LearnEnvironment environment, const LearnEntity &entity, int rowIndex, const Evolutions::EvolutionCriteria &evolution, uint16_t moveId, uint16_t types, LearnOption option) noexcept
            {
                const Pokemon::LearnSourceTables &tables = getTables(environment);
                uint8_t level = 0;
                if (hasSourceType(types, MOVE_SOURCE_LEVEL_UP) && tryLevelUpWithinMax(environment, evolution, moveId, level))
                {
                    return learnInfo(LearnMethod::LevelUp, environment, level);
                }
                if (hasSourceType(types, MOVE_SOURCE_MACHINE))
                {
                    const int machineIndex = indexOfMove(Pokemon::MACHINE_MOVES_5BW, Pokemon::MACHINE_MOVES_5BW_COUNT, moveId);
                    const bool isAvailable = environment != LearnEnvironment::BW || machineIndex != UNAVAILABLE_MACHINE_INDEX_5BW;
                    if (isAvailable && readPermission(tables, rowIndex, Pokemon::PERMISSION_MACHINE_BW, machineIndex, MACHINE_BIT_COUNT_5BW))
                    {
                        return learnInfo(LearnMethod::TechnicalMachine, environment);
                    }
                }
                if (hasSourceType(types, MOVE_SOURCE_TYPE_TUTOR) && readListPermission(tables, rowIndex, Pokemon::PERMISSION_TYPE_TUTOR_BW, Pokemon::TYPE_TUTOR_MOVES_5BW, Pokemon::TYPE_TUTOR_MOVES_5BW_COUNT, moveId, TYPE_TUTOR_BIT_COUNT_5BW))
                {
                    return learnInfo(LearnMethod::Tutor, environment);
                }
                if (environment == LearnEnvironment::B2W2 && hasSourceType(types, MOVE_SOURCE_SPECIAL_TUTOR))
                {
                    const bool isTutor = readListPermission(tables, rowIndex, Pokemon::PERMISSION_TUTOR1_B2W2, Pokemon::TUTOR1_MOVES_5B2W2, Pokemon::TUTOR1_MOVES_5B2W2_COUNT, moveId, Pokemon::TUTOR1_MOVES_5B2W2_COUNT)
                        || readListPermission(tables, rowIndex, Pokemon::PERMISSION_TUTOR2_B2W2, Pokemon::TUTOR2_MOVES_5B2W2, Pokemon::TUTOR2_MOVES_5B2W2_COUNT, moveId, Pokemon::TUTOR2_MOVES_5B2W2_COUNT)
                        || readListPermission(tables, rowIndex, Pokemon::PERMISSION_TUTOR3_B2W2, Pokemon::TUTOR3_MOVES_5B2W2, Pokemon::TUTOR3_MOVES_5B2W2_COUNT, moveId, Pokemon::TUTOR3_MOVES_5B2W2_COUNT)
                        || readListPermission(tables, rowIndex, Pokemon::PERMISSION_TUTOR4_B2W2, Pokemon::TUTOR4_MOVES_5B2W2, Pokemon::TUTOR4_MOVES_5B2W2_COUNT, moveId, Pokemon::TUTOR4_MOVES_5B2W2_COUNT);
                    if (isTutor)
                    {
                        return learnInfo(LearnMethod::Tutor, environment);
                    }
                }
                if (hasSourceType(types, MOVE_SOURCE_ENHANCED_TUTOR) && isEnhancedTutorGeneration5And6(evolution.speciesId, entity.formId, moveId, option))
                {
                    return learnInfo(LearnMethod::Tutor, environment);
                }
                return MoveLearnInfo{};
            }

            MoveLearnInfo getCanLearnGeneration6(LearnEnvironment environment, const LearnEntity &entity, int rowIndex, const Evolutions::EvolutionCriteria &evolution, uint16_t moveId, uint16_t types, LearnOption option) noexcept
            {
                const Pokemon::LearnSourceTables &tables = getTables(environment);
                const bool isOmegaRuby = environment == LearnEnvironment::ORAS;
                uint8_t level = 0;
                if (hasSourceType(types, MOVE_SOURCE_LEVEL_UP) && tryLevelUpWithinMax(environment, evolution, moveId, level))
                {
                    return learnInfo(LearnMethod::LevelUp, environment, level);
                }
                if (hasSourceType(types, MOVE_SOURCE_MACHINE))
                {
                    const bool isMachine = isOmegaRuby
                        ? readListPermission(tables, rowIndex, Pokemon::PERMISSION_MACHINE_ORAS, Pokemon::MACHINE_MOVES_6ORAS, Pokemon::MACHINE_MOVES_6ORAS_COUNT, moveId, MACHINE_BIT_COUNT_6ORAS)
                        : readListPermission(tables, rowIndex, Pokemon::PERMISSION_MACHINE_XY, Pokemon::MACHINE_MOVES_6XY, Pokemon::MACHINE_MOVES_6XY_COUNT, moveId, MACHINE_BIT_COUNT_6XY);
                    if (isMachine)
                    {
                        return learnInfo(LearnMethod::TechnicalMachine, environment);
                    }
                }
                if (hasSourceType(types, MOVE_SOURCE_TYPE_TUTOR) && readListPermission(tables, rowIndex, Pokemon::PERMISSION_TYPE_TUTOR_XY, Pokemon::TYPE_TUTOR_MOVES_5BW, Pokemon::TYPE_TUTOR_MOVES_5BW_COUNT, moveId, isOmegaRuby ? TYPE_TUTOR_BIT_COUNT_6ORAS : TYPE_TUTOR_BIT_COUNT_6XY))
                {
                    return learnInfo(LearnMethod::Tutor, environment);
                }
                if (isOmegaRuby && hasSourceType(types, MOVE_SOURCE_SPECIAL_TUTOR))
                {
                    const bool isTutor = readListPermission(tables, rowIndex, Pokemon::PERMISSION_TUTOR1_ORAS, Pokemon::TUTOR1_MOVES_6ORAS, Pokemon::TUTOR1_MOVES_6ORAS_COUNT, moveId, Pokemon::TUTOR1_MOVES_6ORAS_COUNT)
                        || readListPermission(tables, rowIndex, Pokemon::PERMISSION_TUTOR2_ORAS, Pokemon::TUTOR2_MOVES_6ORAS, Pokemon::TUTOR2_MOVES_6ORAS_COUNT, moveId, Pokemon::TUTOR2_MOVES_6ORAS_COUNT)
                        || readListPermission(tables, rowIndex, Pokemon::PERMISSION_TUTOR3_ORAS, Pokemon::TUTOR3_MOVES_6ORAS, Pokemon::TUTOR3_MOVES_6ORAS_COUNT, moveId, Pokemon::TUTOR3_MOVES_6ORAS_COUNT)
                        || readListPermission(tables, rowIndex, Pokemon::PERMISSION_TUTOR4_ORAS, Pokemon::TUTOR4_MOVES_6ORAS, Pokemon::TUTOR4_MOVES_6ORAS_COUNT, moveId, Pokemon::TUTOR4_MOVES_6ORAS_COUNT);
                    if (isTutor)
                    {
                        return learnInfo(LearnMethod::Tutor, environment);
                    }
                }
                if (hasSourceType(types, MOVE_SOURCE_ENHANCED_TUTOR) && isEnhancedTutorGeneration5And6(evolution.speciesId, entity.formId, moveId, option))
                {
                    return learnInfo(LearnMethod::Tutor, environment);
                }
                return MoveLearnInfo{};
            }

            MoveLearnInfo getCanLearnGeneration7(LearnEnvironment environment, const LearnEntity &entity, int rowIndex, const Evolutions::EvolutionCriteria &evolution, uint16_t moveId, uint16_t types, LearnOption option) noexcept
            {
                const Pokemon::LearnSourceTables &tables = getTables(environment);
                if (hasSourceType(types, MOVE_SOURCE_LEVEL_UP))
                {
                    // The move reminder teaches at any level.
                    uint8_t level = 0;
                    if (getLearnset(environment, evolution.speciesId, evolution.formId).tryGetLevelLearnMove(moveId, level))
                    {
                        return learnInfo(LearnMethod::LevelUp, environment, level);
                    }
                }
                if (hasSourceType(types, MOVE_SOURCE_MACHINE) && readListPermission(tables, rowIndex, Pokemon::PERMISSION_MACHINE_SM, Pokemon::MACHINE_MOVES_7SM, Pokemon::MACHINE_MOVES_7SM_COUNT, moveId, MACHINE_BIT_COUNT_7SM))
                {
                    return learnInfo(LearnMethod::TechnicalMachine, environment);
                }
                if (hasSourceType(types, MOVE_SOURCE_TYPE_TUTOR) && readListPermission(tables, rowIndex, Pokemon::PERMISSION_TYPE_TUTOR_SM, Pokemon::TYPE_TUTOR_MOVES_5BW, Pokemon::TYPE_TUTOR_MOVES_5BW_COUNT, moveId, TYPE_TUTOR_BIT_COUNT_7SM))
                {
                    return learnInfo(LearnMethod::Tutor, environment);
                }
                if (environment == LearnEnvironment::USUM && hasSourceType(types, MOVE_SOURCE_SPECIAL_TUTOR) && readListPermission(tables, rowIndex, Pokemon::PERMISSION_SPECIAL_TUTOR_USUM, Pokemon::BATTLE_POINT_TUTOR_MOVES_7SM, Pokemon::BATTLE_POINT_TUTOR_MOVES_7SM_COUNT, moveId, Pokemon::BATTLE_POINT_TUTOR_MOVES_7SM_COUNT))
                {
                    return learnInfo(LearnMethod::Tutor, environment);
                }
                if (hasSourceType(types, MOVE_SOURCE_ENHANCED_TUTOR) && isEnhancedTutorGeneration7(evolution.speciesId, entity.formId, moveId, option))
                {
                    return learnInfo(LearnMethod::Tutor, environment);
                }
                return MoveLearnInfo{};
            }

            MoveLearnInfo getCanLearnLetsGo(int rowIndex, const Evolutions::EvolutionCriteria &evolution, uint16_t moveId, uint16_t types) noexcept
            {
                constexpr LearnEnvironment environment = LearnEnvironment::GG;
                if (hasSourceType(types, MOVE_SOURCE_LEVEL_UP))
                {
                    uint8_t level = 0;
                    if (getLearnset(environment, evolution.speciesId, evolution.formId).tryGetLevelLearnMove(moveId, level))
                    {
                        return learnInfo(LearnMethod::LevelUp, environment, level);
                    }
                }
                if (hasSourceType(types, MOVE_SOURCE_MACHINE) && readListPermission(getTables(environment), rowIndex, Pokemon::PERMISSION_MACHINE_GG, Pokemon::MACHINE_MOVES_7LGPE, Pokemon::MACHINE_MOVES_7LGPE_COUNT, moveId, MACHINE_BIT_COUNT_7LGPE))
                {
                    return learnInfo(LearnMethod::TechnicalMachine, environment);
                }
                if (hasSourceType(types, MOVE_SOURCE_ENHANCED_TUTOR))
                {
                    // The partner Pikachu (form 8) and Eevee (form 1) moves.
                    const bool isPartnerPikachuMove = evolution.speciesId == PIKACHU_SPECIES && evolution.formId == 8 && indexOfMove(Pokemon::PARTNER_PIKACHU_TUTOR_MOVES_7LGPE, Pokemon::PARTNER_PIKACHU_TUTOR_MOVES_7LGPE_COUNT, moveId) >= 0;
                    const bool isPartnerEeveeMove = evolution.speciesId == EEVEE_SPECIES && evolution.formId == 1 && indexOfMove(Pokemon::PARTNER_EEVEE_TUTOR_MOVES_7LGPE, Pokemon::PARTNER_EEVEE_TUTOR_MOVES_7LGPE_COUNT, moveId) >= 0;
                    if (isPartnerPikachuMove || isPartnerEeveeMove)
                    {
                        return learnInfo(LearnMethod::Tutor, environment);
                    }
                }
                return MoveLearnInfo{};
            }

            MoveLearnInfo getCanLearnSwordShield(const LearnEntity &entity, int rowIndex, const Evolutions::EvolutionCriteria &evolution, uint16_t moveId, uint16_t types, LearnOption option) noexcept
            {
                constexpr LearnEnvironment environment = LearnEnvironment::SWSH;
                const Pokemon::LearnSourceTables &tables = getTables(environment);
                uint8_t level = 0;
                if (hasSourceType(types, MOVE_SOURCE_LEVEL_UP) && tryLevelUpWithinMax(environment, evolution, moveId, level))
                {
                    return learnInfo(LearnMethod::LevelUp, environment, level);
                }
                if (hasSourceType(types, MOVE_SOURCE_SHARED_EGG_MOVE) && isSharedEggMove(environment, evolution, moveId))
                {
                    return learnInfo(LearnMethod::Shared, environment);
                }
                if (hasSourceType(types, MOVE_SOURCE_MACHINE) && readListPermission(tables, rowIndex, Pokemon::PERMISSION_MACHINE_SWSH, Pokemon::MACHINE_MOVES_8SWSH, Pokemon::MACHINE_MOVES_8SWSH_COUNT, moveId, MACHINE_BIT_COUNT_8SWSH))
                {
                    return learnInfo(LearnMethod::TechnicalMachine, environment);
                }
                if (hasSourceType(types, MOVE_SOURCE_TECHNICAL_RECORD) && isTechnicalRecord8SWSH(entity, evolution, moveId, option))
                {
                    return learnInfo(LearnMethod::TechnicalMachine, environment);
                }
                if (hasSourceType(types, MOVE_SOURCE_TYPE_TUTOR) && readListPermission(tables, rowIndex, Pokemon::PERMISSION_TYPE_TUTOR_SWSH, Pokemon::TYPE_TUTOR_MOVES_8SWSH, Pokemon::TYPE_TUTOR_MOVES_8SWSH_COUNT, moveId, TYPE_TUTOR_BIT_COUNT_8SWSH))
                {
                    return learnInfo(LearnMethod::Tutor, environment);
                }
                if (hasSourceType(types, MOVE_SOURCE_SPECIAL_TUTOR) && readListPermission(tables, rowIndex, Pokemon::PERMISSION_SPECIAL_TUTOR_SWSH, Pokemon::SPECIAL_TUTOR_MOVES_8SWSH, Pokemon::SPECIAL_TUTOR_MOVES_8SWSH_COUNT, moveId, Pokemon::SPECIAL_TUTOR_MOVES_8SWSH_COUNT))
                {
                    return learnInfo(LearnMethod::Tutor, environment);
                }
                if (hasSourceType(types, MOVE_SOURCE_ENHANCED_TUTOR))
                {
                    const bool isTutor = (evolution.speciesId == NECROZMA_SPECIES && isNecrozmaFusionMove(entity.formId, moveId, option)) || (evolution.speciesId == ROTOM_SPECIES && isRotomFormMove(entity.formId, moveId, isPastOption(option)));
                    if (isTutor)
                    {
                        return learnInfo(LearnMethod::Tutor, environment);
                    }
                }
                return MoveLearnInfo{};
            }

            MoveLearnInfo getCanLearnBrilliantDiamond(const LearnEntity &entity, int rowIndex, const Evolutions::EvolutionCriteria &evolution, uint16_t moveId, uint16_t types, LearnOption option) noexcept
            {
                constexpr LearnEnvironment environment = LearnEnvironment::BDSP;
                const Pokemon::LearnSourceTables &tables = getTables(environment);
                uint8_t level = 0;
                if (hasSourceType(types, MOVE_SOURCE_LEVEL_UP) && tryLevelUpWithinMax(environment, evolution, moveId, level))
                {
                    return learnInfo(LearnMethod::LevelUp, environment, level);
                }
                if (hasSourceType(types, MOVE_SOURCE_SHARED_EGG_MOVE) && isSharedEggMove(environment, evolution, moveId))
                {
                    return learnInfo(LearnMethod::Shared, environment);
                }
                if (hasSourceType(types, MOVE_SOURCE_MACHINE) && readListPermission(tables, rowIndex, Pokemon::PERMISSION_MACHINE_BDSP, Pokemon::MACHINE_MOVES_8BDSP, Pokemon::MACHINE_MOVES_8BDSP_COUNT, moveId, MACHINE_BIT_COUNT_8BDSP))
                {
                    return learnInfo(LearnMethod::TechnicalMachine, environment);
                }
                if (hasSourceType(types, MOVE_SOURCE_TYPE_TUTOR) && readListPermission(tables, rowIndex, Pokemon::PERMISSION_TYPE_TUTOR_BDSP, Pokemon::TYPE_TUTOR_MOVES_8BDSP, Pokemon::TYPE_TUTOR_MOVES_8BDSP_COUNT, moveId, TYPE_TUTOR_BIT_COUNT_8BDSP))
                {
                    return learnInfo(LearnMethod::Tutor, environment);
                }
                if (hasSourceType(types, MOVE_SOURCE_ENHANCED_TUTOR) && evolution.speciesId == ROTOM_SPECIES && isRotomFormMove(entity.formId, moveId, isPastOption(option)))
                {
                    return learnInfo(LearnMethod::Tutor, environment);
                }
                return MoveLearnInfo{};
            }

            MoveLearnInfo getCanLearnLegendsArceus(const LearnEntity &entity, const Evolutions::EvolutionCriteria &evolution, uint16_t moveId, uint16_t types, LearnOption option, bool isHome) noexcept
            {
                constexpr LearnEnvironment environment = LearnEnvironment::PLA;
                if (hasSourceType(types, MOVE_SOURCE_LEVEL_UP))
                {
                    uint8_t level = 0;
                    if (getLearnset(environment, evolution.speciesId, evolution.formId).tryGetLevelLearnMove(moveId, level) && (isHome || level <= evolution.levelMax))
                    {
                        return learnInfo(LearnMethod::LevelUp, environment, level);
                    }
                }
                if (hasSourceType(types, MOVE_SOURCE_MACHINE) && isMoveShop8LA(evolution, moveId))
                {
                    return learnInfo(LearnMethod::TechnicalMachine, environment);
                }
                if (hasSourceType(types, MOVE_SOURCE_ENHANCED_TUTOR) && evolution.speciesId == ROTOM_SPECIES && isRotomFormMove(entity.formId, moveId, isPastOption(option)))
                {
                    return learnInfo(LearnMethod::Tutor, environment);
                }
                return MoveLearnInfo{};
            }

            MoveLearnInfo getCanLearnScarletViolet(const LearnEntity &entity, const Evolutions::EvolutionCriteria &evolution, uint16_t moveId, uint16_t types, LearnOption option) noexcept
            {
                constexpr LearnEnvironment environment = LearnEnvironment::SV;
                uint8_t level = 0;
                if (hasSourceType(types, MOVE_SOURCE_LEVEL_UP) && tryLevelUpWithinMax(environment, evolution, moveId, level))
                {
                    return learnInfo(LearnMethod::LevelUp, environment, level);
                }
                if (hasSourceType(types, MOVE_SOURCE_SHARED_EGG_MOVE) && isSharedEggMove(environment, evolution, moveId))
                {
                    return learnInfo(LearnMethod::Shared, environment);
                }
                if (hasSourceType(types, MOVE_SOURCE_MACHINE) && isTechnicalMachine9SV(entity, evolution, moveId, option))
                {
                    return learnInfo(LearnMethod::TechnicalMachine, environment);
                }
                if (hasSourceType(types, MOVE_SOURCE_SPECIAL_TUTOR) && isReminderMove9SV(evolution, moveId))
                {
                    return learnInfo(LearnMethod::Tutor, environment);
                }
                if (hasSourceType(types, MOVE_SOURCE_ENHANCED_TUTOR))
                {
                    // Scarlet/Violet's Rotom moves follow the current form only.
                    const bool isTutor = (evolution.speciesId == NECROZMA_SPECIES && isNecrozmaFusionMove(entity.formId, moveId, option)) || (evolution.speciesId == ROTOM_SPECIES && isRotomFormMove(entity.formId, moveId, false));
                    if (isTutor)
                    {
                        return learnInfo(LearnMethod::Tutor, environment);
                    }
                }
                // Version 2.0.1 stopped teaching these by level-up; a Pokemon could have learned them before.
                if (hasSourceType(types, MOVE_SOURCE_LEVEL_UP))
                {
                    if (moveId == BUG_BITE_MOVE && evolution.speciesId == LARVESTA_SPECIES && evolution.formId == 0 && evolution.levelMax >= LARVESTA_BUG_BITE_LEVEL)
                    {
                        return learnInfo(LearnMethod::LevelUp, environment, LARVESTA_BUG_BITE_LEVEL);
                    }
                    // PKHeX reports level 28 for this one too.
                    if (moveId == SPITE_MOVE && evolution.speciesId == ZORUA_SPECIES && evolution.formId == 1 && evolution.levelMax >= ZORUA_HISUI_SPITE_LEVEL)
                    {
                        return learnInfo(LearnMethod::LevelUp, environment, LARVESTA_BUG_BITE_LEVEL);
                    }
                }
                return MoveLearnInfo{};
            }

            MoveLearnInfo getCanLearnLegendsZA(const Evolutions::EvolutionCriteria &evolution, uint16_t moveId, uint16_t types, bool isHome) noexcept
            {
                constexpr LearnEnvironment environment = LearnEnvironment::ZA;
                if (hasSourceType(types, MOVE_SOURCE_LEVEL_UP))
                {
                    uint8_t level = 0;
                    if (getLearnset(environment, evolution.speciesId, evolution.formId).tryGetLevelLearnMove(moveId, level) && (isHome || level <= evolution.levelMax))
                    {
                        return learnInfo(LearnMethod::LevelUp, environment, level);
                    }
                }
                if (hasSourceType(types, MOVE_SOURCE_MACHINE) && isTechnicalMachine9ZA(evolution, moveId))
                {
                    return learnInfo(LearnMethod::TechnicalMachine, environment);
                }
                return MoveLearnInfo{};
            }
        }

        LearnEnvironment getLearnEnvironment(Enums::GameVersion version) noexcept
        {
            using Enums::GameVersion;
            switch (version)
            {
                case GameVersion::RD:
                case GameVersion::GN:
                case GameVersion::BU:
                    return LearnEnvironment::RB;
                case GameVersion::YW:
                case GameVersion::RBY:
                    return LearnEnvironment::YW;
                case GameVersion::GD:
                case GameVersion::SI:
                    return LearnEnvironment::GS;
                case GameVersion::C:
                case GameVersion::GSC:
                    return LearnEnvironment::C;
                case GameVersion::RU:
                case GameVersion::SA:
                case GameVersion::RSE:
                    return LearnEnvironment::RS;
                case GameVersion::EM:
                case GameVersion::FRLG:
                    return LearnEnvironment::E;
                case GameVersion::FR: return LearnEnvironment::FR;
                case GameVersion::LG: return LearnEnvironment::LG;
                case GameVersion::D:
                case GameVersion::P:
                case GameVersion::DP:
                    return LearnEnvironment::DP;
                case GameVersion::Pt:
                case GameVersion::PT:
                    return LearnEnvironment::PT;
                case GameVersion::HG:
                case GameVersion::SS:
                case GameVersion::HGSS:
                    return LearnEnvironment::HGSS;
                case GameVersion::B:
                case GameVersion::W:
                case GameVersion::BW:
                    return LearnEnvironment::BW;
                case GameVersion::B2:
                case GameVersion::W2:
                case GameVersion::B2W2:
                    return LearnEnvironment::B2W2;
                case GameVersion::X:
                case GameVersion::Y:
                case GameVersion::XY:
                    return LearnEnvironment::XY;
                case GameVersion::AS:
                case GameVersion::OR:
                case GameVersion::ORAS:
                    return LearnEnvironment::ORAS;
                case GameVersion::SN:
                case GameVersion::MN:
                case GameVersion::SM:
                    return LearnEnvironment::SM;
                case GameVersion::US:
                case GameVersion::UM:
                case GameVersion::USUM:
                    return LearnEnvironment::USUM;
                case GameVersion::GP:
                case GameVersion::GE:
                case GameVersion::GG:
                    return LearnEnvironment::GG;
                case GameVersion::SW:
                case GameVersion::SH:
                case GameVersion::SWSH:
                    return LearnEnvironment::SWSH;
                case GameVersion::BD:
                case GameVersion::SP:
                case GameVersion::BDSP:
                    return LearnEnvironment::BDSP;
                case GameVersion::PLA: return LearnEnvironment::PLA;
                case GameVersion::SL:
                case GameVersion::VL:
                case GameVersion::SV:
                    return LearnEnvironment::SV;
                case GameVersion::ZA: return LearnEnvironment::ZA;
                default: break;
            }
            // Raw PKHeX versions PKSE has no enumerator for: Colosseum/XD (15), GO (34), the
            // Virtual Console FireRed/LeafGreen (56, 57).
            constexpr int VERSION_COLOSSEUM_XD = 15;
            constexpr int VERSION_POKEMON_GO = 34;
            constexpr int VERSION_FIRERED_VIRTUAL_CONSOLE = 56;
            constexpr int VERSION_LEAFGREEN_VIRTUAL_CONSOLE = 57;
            switch (static_cast<int>(version))
            {
                case VERSION_COLOSSEUM_XD: return LearnEnvironment::E;
                case VERSION_POKEMON_GO: return LearnEnvironment::GG;
                case VERSION_FIRERED_VIRTUAL_CONSOLE:
                case VERSION_LEAFGREEN_VIRTUAL_CONSOLE:
                    return LearnEnvironment::SWSH;
                default: return LearnEnvironment::None;
            }
        }

        bool tryGetPersonal(LearnEnvironment environment, uint16_t speciesId, uint8_t formId, int &rowIndex) noexcept
        {
            rowIndex = 0;
            if (!hasGameTables(environment))
            {
                return false;
            }
            const Pokemon::LearnSourceTables &tables = getTables(environment);
            const uint8_t generation = getEnvironmentGeneration(environment);
            if (speciesId > tables.maxSpecies || (generation <= 2 && formId != 0))
            {
                return false;
            }
            rowIndex = getFormIndex(tables, speciesId, formId);
            return true;
        }

        uint8_t getFormCount(LearnEnvironment environment, uint16_t speciesId) noexcept
        {
            if (!hasGameTables(environment))
            {
                return 1;
            }
            const Pokemon::LearnSourceTables &tables = getTables(environment);
            if (tables.formCounts == nullptr || speciesId > tables.maxSpecies)
            {
                return 1;
            }
            return tables.formCounts[speciesId];
        }

        Learnset getLearnset(LearnEnvironment environment, uint16_t speciesId, uint8_t formId) noexcept
        {
            if (!hasGameTables(environment))
            {
                return Learnset{};
            }
            const Pokemon::LearnSourceTables &tables = getTables(environment);
            int entryIndex = getEnvironmentGeneration(environment) <= 3 ? speciesId : getFormIndex(tables, speciesId, formId);
            if (entryIndex >= tables.levelUpCount)
            {
                entryIndex = 0;
            }
            const uint16_t start = tables.levelUpOffsets[entryIndex];
            const uint16_t end = tables.levelUpOffsets[entryIndex + 1];
            return Learnset{std::span<const uint16_t>(tables.levelUpMoves + start, tables.levelUpMoves + end), std::span<const uint8_t>(tables.levelUpLevels + start, tables.levelUpLevels + end)};
        }

        std::span<const uint16_t> getEggMoves(LearnEnvironment environment, uint16_t speciesId, uint8_t formId) noexcept
        {
            if (!hasGameTables(environment))
            {
                return {};
            }
            const Pokemon::LearnSourceTables &tables = getTables(environment);
            if (tables.eggMoves == nullptr)
            {
                return {};
            }
            // Sun/Moon, Ultra Sun/Ultra Moon, Sword/Shield and Scarlet/Violet index egg moves by form.
            const bool isFormIndexed = environment == LearnEnvironment::SM || environment == LearnEnvironment::USUM || environment == LearnEnvironment::SWSH || environment == LearnEnvironment::SV;
            const int entryIndex = isFormIndexed ? getFormIndex(tables, speciesId, formId) : speciesId;
            if (entryIndex >= tables.eggCount)
            {
                return {};
            }
            return getEntryMoves(tables.eggMoves, tables.eggOffsets, entryIndex);
        }

        bool canInheritMoves(uint16_t speciesId)
        {
            // EntityGender.IsFemaleOrDualGender reads the species' base ratio: anything but male-only
            // (0) and genderless (255).
            constexpr uint8_t GENDER_RATIO_MALE_ONLY = 0;
            constexpr uint8_t GENDER_RATIO_GENDERLESS = 255;
            const uint8_t genderRatio = Pokemon::getGenderRatio(speciesId, 0, Enums::GameVersion::SV);
            if (genderRatio != GENDER_RATIO_MALE_ONLY && genderRatio != GENDER_RATIO_GENDERLESS)
            {
                return true;
            }
            // Breeding.IsMixedGenderBreed.
            return speciesId == NIDORAN_FEMALE_SPECIES || speciesId == NIDORAN_MALE_SPECIES || speciesId == VOLBEAT_SPECIES || speciesId == ILLUMISE_SPECIES || speciesId == INDEEDEE_SPECIES;
        }

        std::span<const uint16_t> getInheritMoves(LearnEnvironment environment, uint16_t speciesId, uint8_t formId)
        {
            if (!canInheritMoves(speciesId))
            {
                return {};
            }
            return getLearnset(environment, speciesId, formId).moves;
        }

        MoveLearnInfo getCanLearn(LearnEnvironment environment, const LearnEntity &entity, int rowIndex, const Evolutions::EvolutionCriteria &evolution, uint16_t moveId, uint16_t types, LearnOption option)
        {
            switch (environment)
            {
                case LearnEnvironment::RB:
                case LearnEnvironment::YW:
                    return getCanLearnGeneration1(environment, entity, rowIndex, evolution, moveId, types);
                case LearnEnvironment::GS:
                case LearnEnvironment::C:
                    return getCanLearnGeneration2(environment, entity, rowIndex, evolution, moveId, types);
                case LearnEnvironment::Stadium2:
                    return getStadiumRelearn(evolution, moveId, types);
                case LearnEnvironment::RS:
                case LearnEnvironment::E:
                case LearnEnvironment::FR:
                case LearnEnvironment::LG:
                    return getCanLearnGeneration3(environment, entity, rowIndex, evolution, moveId, types);
                case LearnEnvironment::DP:
                case LearnEnvironment::PT:
                case LearnEnvironment::HGSS:
                    return getCanLearnGeneration4(environment, entity, rowIndex, evolution, moveId, types, option);
                case LearnEnvironment::BW:
                case LearnEnvironment::B2W2:
                    return getCanLearnGeneration5(environment, entity, rowIndex, evolution, moveId, types, option);
                case LearnEnvironment::XY:
                case LearnEnvironment::ORAS:
                    return getCanLearnGeneration6(environment, entity, rowIndex, evolution, moveId, types, option);
                case LearnEnvironment::SM:
                case LearnEnvironment::USUM:
                    return getCanLearnGeneration7(environment, entity, rowIndex, evolution, moveId, types, option);
                case LearnEnvironment::GG:
                    return getCanLearnLetsGo(rowIndex, evolution, moveId, types);
                case LearnEnvironment::SWSH:
                    return getCanLearnSwordShield(entity, rowIndex, evolution, moveId, types, option);
                case LearnEnvironment::BDSP:
                    return getCanLearnBrilliantDiamond(entity, rowIndex, evolution, moveId, types, option);
                case LearnEnvironment::PLA:
                    return getCanLearnLegendsArceus(entity, evolution, moveId, types, option, false);
                case LearnEnvironment::SV:
                    return getCanLearnScarletViolet(entity, evolution, moveId, types, option);
                case LearnEnvironment::ZA:
                    return getCanLearnLegendsZA(evolution, moveId, types, false);
                default:
                    return MoveLearnInfo{};
            }
        }

        MoveLearnInfo getCanLearnHome(LearnEnvironment environment, const LearnEntity &entity, const Evolutions::EvolutionCriteria &evolution, uint16_t moveId, uint16_t types)
        {
            int rowIndex = 0;
            // Scarlet/Violet and Z-A read the personal row without TryGetPersonal's species check.
            const bool hasPersonal = tryGetPersonal(environment, evolution.speciesId, evolution.formId, rowIndex);
            if (!hasPersonal && environment != LearnEnvironment::SV && environment != LearnEnvironment::ZA)
            {
                return MoveLearnInfo{};
            }
            if (environment == LearnEnvironment::PLA)
            {
                return getCanLearnLegendsArceus(entity, evolution, moveId, types, LearnOption::Home, true);
            }
            if (environment == LearnEnvironment::ZA)
            {
                return getCanLearnLegendsZA(evolution, moveId, types, true);
            }
            if (environment != LearnEnvironment::SWSH && environment != LearnEnvironment::BDSP && environment != LearnEnvironment::SV)
            {
                return MoveLearnInfo{};
            }
            if (hasSourceType(types, MOVE_SOURCE_LEVEL_UP))
            {
                uint8_t level = 0;
                if (getLearnset(environment, evolution.speciesId, evolution.formId).tryGetLevelLearnMove(moveId, level))
                {
                    return learnInfo(LearnMethod::LevelUp, environment, level);
                }
            }
            if (hasSourceType(types, MOVE_SOURCE_SHARED_EGG_MOVE) && isSharedEggMove(environment, evolution, moveId))
            {
                return learnInfo(LearnMethod::Shared, environment);
            }
            const Pokemon::LearnSourceTables &tables = getTables(environment);
            if (environment == LearnEnvironment::SWSH)
            {
                if (hasSourceType(types, MOVE_SOURCE_MACHINE) && readListPermission(tables, rowIndex, Pokemon::PERMISSION_MACHINE_SWSH, Pokemon::MACHINE_MOVES_8SWSH, Pokemon::MACHINE_MOVES_8SWSH_COUNT, moveId, MACHINE_BIT_COUNT_8SWSH))
                {
                    return learnInfo(LearnMethod::TechnicalMachine, environment);
                }
                if (hasSourceType(types, MOVE_SOURCE_TECHNICAL_RECORD) && isTechnicalRecord8SWSH(entity, evolution, moveId, LearnOption::Home))
                {
                    return learnInfo(LearnMethod::TechnicalMachine, environment);
                }
                return MoveLearnInfo{};
            }
            if (environment == LearnEnvironment::BDSP)
            {
                if (hasSourceType(types, MOVE_SOURCE_MACHINE) && readListPermission(tables, rowIndex, Pokemon::PERMISSION_MACHINE_BDSP, Pokemon::MACHINE_MOVES_8BDSP, Pokemon::MACHINE_MOVES_8BDSP_COUNT, moveId, MACHINE_BIT_COUNT_8BDSP))
                {
                    return learnInfo(LearnMethod::TechnicalMachine, environment);
                }
                return MoveLearnInfo{};
            }
            if (hasSourceType(types, MOVE_SOURCE_MACHINE) && isTechnicalMachine9SV(entity, evolution, moveId, LearnOption::Home))
            {
                return learnInfo(LearnMethod::TechnicalMachine, environment);
            }
            if (hasSourceType(types, MOVE_SOURCE_SPECIAL_TUTOR) && isReminderMove9SV(evolution, moveId))
            {
                return learnInfo(LearnMethod::Tutor, environment);
            }
            return MoveLearnInfo{};
        }

        bool isTutorFireRedLeafGreen(uint16_t speciesId, uint16_t moveId) noexcept
        {
            constexpr int TUTOR_COUNT_FIRERED_LEAFGREEN = 15;
            return readListPermission(getTables(LearnEnvironment::E), speciesId, Pokemon::PERMISSION_TUTOR_E, Pokemon::TUTOR_MOVES_3RSE, TUTOR_COUNT_FIRERED_LEAFGREEN, moveId, TUTOR_COUNT_FIRERED_LEAFGREEN);
        }

        bool isTutorColosseumXD(uint16_t speciesId, uint16_t moveId) noexcept
        {
            if (speciesId == MEW_SPECIES && indexOfMove(Pokemon::MEW_TUTOR_MOVES_3RSE, Pokemon::MEW_TUTOR_MOVES_3RSE_COUNT, moveId) >= 0)
            {
                return true;
            }
            switch (moveId)
            {
                case SELF_DESTRUCT_MOVE: return std::binary_search(Pokemon::SELF_DESTRUCT_TUTOR_SPECIES_3RSE, Pokemon::SELF_DESTRUCT_TUTOR_SPECIES_3RSE + Pokemon::SELF_DESTRUCT_TUTOR_SPECIES_3RSE_COUNT, speciesId);
                case SKY_ATTACK_MOVE: return std::binary_search(Pokemon::SKY_ATTACK_TUTOR_SPECIES_3RSE, Pokemon::SKY_ATTACK_TUTOR_SPECIES_3RSE + Pokemon::SKY_ATTACK_TUTOR_SPECIES_3RSE_COUNT, speciesId);
                case NIGHTMARE_MOVE: return std::binary_search(Pokemon::NIGHTMARE_TUTOR_SPECIES_3RSE, Pokemon::NIGHTMARE_TUTOR_SPECIES_3RSE + Pokemon::NIGHTMARE_TUTOR_SPECIES_3RSE_COUNT, speciesId);
                default: return false;
            }
        }

        bool isTutorEmerald(uint16_t speciesId, uint16_t moveId) noexcept
        {
            return readListPermission(getTables(LearnEnvironment::E), speciesId, Pokemon::PERMISSION_TUTOR_E, Pokemon::TUTOR_MOVES_3RSE, Pokemon::TUTOR_MOVES_3RSE_COUNT, moveId, Pokemon::TUTOR_MOVES_3RSE_COUNT);
        }

        bool isBreedingMachineMove(LearnEnvironment environment, int rowIndex, uint16_t moveId, bool includeUnavailableMachine) noexcept
        {
            if (!hasGameTables(environment))
            {
                return false;
            }
            const Pokemon::LearnSourceTables &tables = getTables(environment);
            switch (environment)
            {
                case LearnEnvironment::RB:
                case LearnEnvironment::YW:
                    return readListPermission(tables, rowIndex, Pokemon::PERMISSION_MACHINE_RB, Pokemon::MACHINE_MOVES_1RBY, Pokemon::MACHINE_MOVES_1RBY_COUNT, moveId, MACHINE_BIT_COUNT_1RBY);
                case LearnEnvironment::GS:
                case LearnEnvironment::C:
                    return moveId <= MAX_MOVE_ID_2 && readListPermission(tables, rowIndex, Pokemon::PERMISSION_MACHINE_GS, Pokemon::MACHINE_MOVES_2GSC, Pokemon::MACHINE_MOVES_2GSC_COUNT, moveId, MACHINE_BIT_COUNT_2GSC);
                case LearnEnvironment::RS:
                case LearnEnvironment::E:
                case LearnEnvironment::FR:
                case LearnEnvironment::LG:
                {
                    const int machineBitCount = TECHNICAL_MACHINE_COUNT_GENERATION3 + Pokemon::HIDDEN_MACHINE_MOVES_GENERATION3_COUNT;
                    if (readListPermission(tables, rowIndex, Pokemon::PERMISSION_MACHINE_RS, Pokemon::TECHNICAL_MACHINE_MOVES_GENERATION3, Pokemon::TECHNICAL_MACHINE_MOVES_GENERATION3_COUNT, moveId, machineBitCount))
                    {
                        return true;
                    }
                    const int hiddenIndex = indexOfMove(Pokemon::HIDDEN_MACHINE_MOVES_GENERATION3, Pokemon::HIDDEN_MACHINE_MOVES_GENERATION3_COUNT, moveId);
                    return hiddenIndex != -1 && readPermission(tables, rowIndex, Pokemon::PERMISSION_MACHINE_RS, TECHNICAL_MACHINE_COUNT_GENERATION3 + hiddenIndex, machineBitCount);
                }
                case LearnEnvironment::DP:
                case LearnEnvironment::PT:
                case LearnEnvironment::HGSS:
                {
                    if (readListPermission(tables, rowIndex, Pokemon::PERMISSION_MACHINE_DP, Pokemon::TECHNICAL_MACHINE_MOVES_GENERATION4, Pokemon::TECHNICAL_MACHINE_MOVES_GENERATION4_COUNT, moveId, TECHNICAL_MACHINE_COUNT_GENERATION4))
                    {
                        return true;
                    }
                    const uint16_t *hiddenMoves = environment == LearnEnvironment::HGSS ? Pokemon::HIDDEN_MACHINE_MOVES_4HGSS : Pokemon::HIDDEN_MACHINE_MOVES_4DP;
                    const int hiddenIndex = indexOfMove(hiddenMoves, HIDDEN_MACHINE_COUNT_GENERATION4, moveId);
                    return hiddenIndex != -1 && readPermission(tables, rowIndex, Pokemon::PERMISSION_MACHINE_DP, TECHNICAL_MACHINE_COUNT_GENERATION4 + hiddenIndex, TECHNICAL_MACHINE_COUNT_GENERATION4 + HIDDEN_MACHINE_COUNT_GENERATION4);
                }
                case LearnEnvironment::BW:
                case LearnEnvironment::B2W2:
                    // MoveBreed5 reads PersonalInfo5BW.MachineMoves[..^1]: TM96 is never available.
                    // ChainBreedLegality reads the whole list.
                    return readListPermission(tables, rowIndex, Pokemon::PERMISSION_MACHINE_BW, Pokemon::MACHINE_MOVES_5BW, includeUnavailableMachine ? Pokemon::MACHINE_MOVES_5BW_COUNT : Pokemon::MACHINE_MOVES_5BW_COUNT - 1, moveId, MACHINE_BIT_COUNT_5BW);
                default:
                    return false;
            }
        }

        bool isBreedingTutorCrystal(int rowIndex, uint16_t moveId) noexcept
        {
            const int tutorIndex = indexOfMove(Pokemon::TUTOR_MOVES_2GSC, Pokemon::TUTOR_MOVES_2GSC_COUNT, moveId);
            return tutorIndex != -1 && readPermission(getTables(LearnEnvironment::C), rowIndex, Pokemon::PERMISSION_MACHINE_C, MACHINE_BIT_COUNT_2GSC + tutorIndex, MACHINE_BIT_COUNT_2GSC + TUTOR_BIT_COUNT_2GSC);
        }

        MoveLearnInfo getStadiumRelearn(const Evolutions::EvolutionCriteria &evolution, uint16_t moveId, uint16_t types)
        {
            if (moveId > MAX_MOVE_ID_2 || !hasSourceType(types, MOVE_SOURCE_LEVEL_UP))
            {
                return MoveLearnInfo{};
            }
            const int entryIndex = evolution.speciesId < Pokemon::STADIUM2_REMINDER_SPECIES_COUNT ? evolution.speciesId : 0;
            const uint16_t start = Pokemon::STADIUM2_REMINDER_OFFSETS[entryIndex];
            const uint16_t end = Pokemon::STADIUM2_REMINDER_OFFSETS[entryIndex + 1];
            const StadiumLearnset learnset{std::span<const uint8_t>(Pokemon::STADIUM2_REMINDER_TUPLES + start * 3, Pokemon::STADIUM2_REMINDER_TUPLES + end * 3)};
            uint8_t level = 0;
            if (learnset.canRelearn(moveId, evolution.levelMax, level))
            {
                return learnInfo(LearnMethod::LevelUp, LearnEnvironment::Stadium2, level);
            }
            return MoveLearnInfo{};
        }

        void getHatchSpeciesForm(LearnEnvironment environment, uint16_t speciesId, uint8_t formId, uint16_t &hatchSpeciesId, uint8_t &hatchFormId) noexcept
        {
            hatchSpeciesId = 0;
            hatchFormId = 0;
            if (!hasGameTables(environment))
            {
                return;
            }
            const Pokemon::LearnSourceTables &tables = getTables(environment);
            if (tables.hatchSpecies == nullptr)
            {
                return;
            }
            // PKHeX PersonalTable.GetFormEntry: the row of the form, row 0 past the table.
            int rowIndex = getFormIndex(tables, speciesId, formId);
            if (rowIndex >= tables.personalRowCount)
            {
                rowIndex = 0;
            }
            hatchSpeciesId = tables.hatchSpecies[rowIndex];
            hatchFormId = tables.hatchForms[rowIndex];
        }

        void setEncounterMovesGeneration1(LearnEnvironment environment, uint16_t speciesId, uint8_t level, std::span<uint16_t, 4> encounterMoves) noexcept
        {
            int rowIndex = 0;
            if (!tryGetPersonal(environment, speciesId, 0, rowIndex))
            {
                return;
            }
            const Pokemon::LearnSourceTables &tables = getTables(environment);
            const int initialOffset = environment == LearnEnvironment::YW ? Pokemon::PERMISSION_INITIAL_MOVES_YW : Pokemon::PERMISSION_INITIAL_MOVES_RB;
            int lastFilled = -1;
            for (int slotIndex = 0; slotIndex < 4; ++slotIndex)
            {
                encounterMoves[slotIndex] = tables.permissions[rowIndex * tables.permissionRowSize + initialOffset + slotIndex];
                if (encounterMoves[slotIndex] != 0)
                {
                    lastFilled = slotIndex;
                }
            }
            getLearnset(environment, speciesId, 0).setEncounterMoves(level, encounterMoves, (lastFilled + 1) & 3);
        }

        bool isPresentInGame(LearnEnvironment environment, uint16_t speciesId, uint8_t formId) noexcept
        {
            if (!hasGameTables(environment))
            {
                return false;
            }
            const Pokemon::LearnSourceTables &tables = getTables(environment);
            if (speciesId > tables.maxSpecies)
            {
                return false;
            }
            const bool hasForm = formId != 0 && tables.formStatsIndex != nullptr && tables.formStatsIndex[speciesId] != 0 && formId < tables.formCounts[speciesId];
            switch (environment)
            {
                case LearnEnvironment::RB:
                case LearnEnvironment::YW:
                    return formId == 0;
                case LearnEnvironment::GS:
                case LearnEnvironment::C:
                    return formId == 0 || (speciesId == UNOWN_SPECIES && formId < 26);
                case LearnEnvironment::RS:
                case LearnEnvironment::E:
                case LearnEnvironment::FR:
                case LearnEnvironment::LG:
                    return formId == 0 || (speciesId == UNOWN_SPECIES && formId < 28) || (speciesId == CASTFORM_SPECIES && formId < 4) || (speciesId == DEOXYS_SPECIES && formId < 4);
                case LearnEnvironment::DP:
                case LearnEnvironment::PT:
                case LearnEnvironment::HGSS:
                    if (formId == 0 || hasForm)
                    {
                        return true;
                    }
                    switch (speciesId)
                    {
                        case PICHU_SPECIES: return formId == 1;
                        case UNOWN_SPECIES: return formId < 28;
                        case CASTFORM_SPECIES: return formId < 4;
                        case BURMY_SPECIES:
                        case MOTHIM_SPECIES: return formId < 3;
                        case CHERRIM_SPECIES: return formId == 1;
                        case SHELLOS_SPECIES:
                        case GASTRODON_SPECIES: return formId == 1;
                        case ARCEUS_SPECIES: return formId < 18 && formId != 9; // The Curse type does not exist legally.
                        default: return false;
                    }
                case LearnEnvironment::BW:
                case LearnEnvironment::B2W2:
                    if (formId == 0 || hasForm)
                    {
                        return true;
                    }
                    switch (speciesId)
                    {
                        case UNOWN_SPECIES: return formId < 28;
                        case BURMY_SPECIES:
                        case MOTHIM_SPECIES: return formId < 3;
                        case CHERRIM_SPECIES: return formId == 1;
                        case SHELLOS_SPECIES:
                        case GASTRODON_SPECIES: return formId == 1;
                        case ARCEUS_SPECIES: return formId < 17;
                        case DEERLING_SPECIES:
                        case SAWSBUCK_SPECIES: return formId < 4;
                        case GENESECT_SPECIES: return formId <= 4;
                        default: return false;
                    }
                case LearnEnvironment::XY:
                case LearnEnvironment::ORAS:
                    if (formId == 0 || hasForm)
                    {
                        return true;
                    }
                    switch (speciesId)
                    {
                        case UNOWN_SPECIES: return formId < 28;
                        case BURMY_SPECIES:
                        case MOTHIM_SPECIES: return formId < 3;
                        case SHELLOS_SPECIES:
                        case GASTRODON_SPECIES: return formId == 1;
                        case ARCEUS_SPECIES: return formId < 18;
                        case DEERLING_SPECIES:
                        case SAWSBUCK_SPECIES: return formId < 4;
                        case GENESECT_SPECIES: return formId < 5;
                        case SCATTERBUG_SPECIES:
                        case SPEWPA_SPECIES: return formId < 18;
                        case VIVILLON_SPECIES: return formId <= 19;
                        case FLABEBE_SPECIES:
                        case FLORGES_SPECIES: return formId < 5;
                        case FLOETTE_SPECIES: return formId <= 5;
                        case XERNEAS_SPECIES: return formId == 1;
                        default: return false;
                    }
                case LearnEnvironment::SM:
                case LearnEnvironment::USUM:
                    if (formId == 0 || hasForm)
                    {
                        return true;
                    }
                    switch (speciesId)
                    {
                        case UNOWN_SPECIES: return formId < 28;
                        case BURMY_SPECIES:
                        case MOTHIM_SPECIES: return formId < 3;
                        case ARCEUS_SPECIES: return formId < 18;
                        case GENESECT_SPECIES: return formId <= 4;
                        case SCATTERBUG_SPECIES:
                        case SPEWPA_SPECIES: return formId < 18;
                        case VIVILLON_SPECIES: return formId < 20;
                        case DEERLING_SPECIES:
                        case SAWSBUCK_SPECIES: return formId < 4;
                        case FLABEBE_SPECIES:
                        case FLORGES_SPECIES: return formId < 5;
                        case FLOETTE_SPECIES: return formId < 6;
                        case XERNEAS_SPECIES: return formId == 1;
                        case SILVALLY_SPECIES: return formId < 18;
                        default: return false;
                    }
                case LearnEnvironment::GG:
                    if (speciesId > MEW_SPECIES && speciesId != MELTAN_SPECIES && speciesId != MELMETAL_SPECIES)
                    {
                        return false;
                    }
                    if (formId == 0)
                    {
                        return true;
                    }
                    return speciesId == PIKACHU_SPECIES ? formId == 8 : hasForm;
                default:
                {
                    // Sword/Shield, BDSP, Legends: Arceus, Scarlet/Violet, Legends: Z-A: the row's own flag.
                    if (formId != 0 && !hasForm)
                    {
                        return false;
                    }
                    const int rowIndex = getFormIndex(tables, speciesId, formId);
                    return tables.breeding != nullptr && tables.breeding[rowIndex * Pokemon::BREEDING_ROW_SIZE + Pokemon::BREEDING_PRESENT] != 0;
                }
            }
        }

        BreedingInfo getBreedingInfo(LearnEnvironment environment, uint16_t speciesId, uint8_t formId) noexcept
        {
            BreedingInfo breedingInfo{};
            if (!hasGameTables(environment))
            {
                return breedingInfo;
            }
            const Pokemon::LearnSourceTables &tables = getTables(environment);
            if (tables.breeding == nullptr)
            {
                return breedingInfo;
            }
            const uint8_t *row = tables.breeding + getFormIndex(tables, speciesId, formId) * Pokemon::BREEDING_ROW_SIZE;
            breedingInfo.gender = row[Pokemon::BREEDING_GENDER];
            breedingInfo.eggGroup1 = row[Pokemon::BREEDING_EGG_GROUP1];
            breedingInfo.eggGroup2 = row[Pokemon::BREEDING_EGG_GROUP2];
            breedingInfo.hasData = true;
            return breedingInfo;
        }

        uint16_t getMaxSpecies(LearnEnvironment environment) noexcept
        {
            return hasGameTables(environment) ? getTables(environment).maxSpecies : 0;
        }
    }
}
