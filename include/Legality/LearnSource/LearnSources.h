/**
 * Every game's learn source: whether a species can learn a move there, and by which method. Ported
 * from PKHeX PKHeX.Core/Legality/LearnSource/Sources (LearnSource1RB ... LearnSource9ZA,
 * LearnSource2Stadium, MoveTutor), PKHeX.Core/Legality/Moves/GameData.cs (GetLearnSource) and
 * PKHeX.Core/Legality/Breeding.cs (GetCanInheritMoves). The data is Pokemon/LearnSourceTable.
 *
 * A game is named by its LearnEnvironment. The personal row a species-form reads is PKHeX's
 * PersonalTable.GetFormIndex; tryGetPersonal answers PKHeX's TryGetPersonal.
 */
#ifndef LEGALITY_LEARNSOURCE_LEARNSOURCES_H
#define LEGALITY_LEARNSOURCE_LEARNSOURCES_H

#include <cstdint>
#include <span>

#include "Enums/GameVersion.h"
#include "Legality/Evolutions/EvolutionCriteria.h"
#include "Legality/LearnSource/LearnEntity.h"
#include "Legality/LearnSource/Learnset.h"
#include "Legality/LearnSource/MoveResult.h"

namespace Legality
{
    namespace LearnSource
    {
        /// PKHeX GameData.GetLearnSource(version).Environment; None for a version with no learn source.
        LearnEnvironment getLearnEnvironment(Enums::GameVersion version) noexcept;

        /// PKHeX ILearnSource.TryGetPersonal: the species is in the game's personal table. `rowIndex`
        /// is the row its form reads (PKHeX GetFormIndex).
        bool tryGetPersonal(LearnEnvironment environment, uint16_t speciesId, uint8_t formId, int &rowIndex) noexcept;
        /// PKHeX PersonalInfo.FormCount of the species' base row; 1 where the game has no form table.
        uint8_t getFormCount(LearnEnvironment environment, uint16_t speciesId) noexcept;

        /// PKHeX GetLearnset. An empty learnset for a game without one.
        Learnset getLearnset(LearnEnvironment environment, uint16_t speciesId, uint8_t formId) noexcept;
        /// PKHeX IEggSource.GetEggMoves. Empty where the game has none.
        std::span<const uint16_t> getEggMoves(LearnEnvironment environment, uint16_t speciesId, uint8_t formId) noexcept;
        /// PKHeX ILearnSource.GetInheritMoves: the level-up moves a parent can pass down, or none
        /// when the species cannot inherit (Breeding.GetCanInheritMoves).
        std::span<const uint16_t> getInheritMoves(LearnEnvironment environment, uint16_t speciesId, uint8_t formId);
        /// PKHeX Breeding.GetCanInheritMoves: a female (or mixed-gender) parent can pass moves down.
        bool canInheritMoves(uint16_t speciesId);

        /// PKHeX ILearnSource<T>.GetCanLearn. `rowIndex` comes from tryGetPersonal for the same game.
        MoveLearnInfo getCanLearn(LearnEnvironment environment, const LearnEntity &entity, int rowIndex, const Evolutions::EvolutionCriteria &evolution, uint16_t moveId, uint16_t types = MOVE_SOURCE_ALL, LearnOption option = LearnOption::Current);
        /// PKHeX IHomeSource.GetCanLearnHOME (Sword/Shield, BDSP, Legends: Arceus, Scarlet/Violet,
        /// Legends: Z-A).
        MoveLearnInfo getCanLearnHome(LearnEnvironment environment, const LearnEntity &entity, const Evolutions::EvolutionCriteria &evolution, uint16_t moveId, uint16_t types = MOVE_SOURCE_ALL);
        /// PKHeX LearnSource2Stadium.GetCanRelearn.
        MoveLearnInfo getStadiumRelearn(const Evolutions::EvolutionCriteria &evolution, uint16_t moveId, uint16_t types);
        /// PKHeX LearnSource3E.GetIsTutorFRLG: FireRed/LeafGreen's tutors are Emerald's first 15.
        bool isTutorFireRedLeafGreen(uint16_t speciesId, uint16_t moveId) noexcept;
        /// PKHeX LearnSource3E.GetIsSpecialTutor: Emerald's move tutors.
        bool isTutorEmerald(uint16_t speciesId, uint16_t moveId) noexcept;
        /// PKHeX LearnSource3RS.GetIsTutor: Colosseum/XD's tutors (XD's Mew tutor and three special tutors).
        bool isTutorColosseumXD(uint16_t speciesId, uint16_t moveId) noexcept;
        /// PKHeX MoveBreed2 ... MoveBreed5 MarkMovesForOrigin's FatherTM test, and ChainBreedLegality's
        /// machine tests: the personal row's TM or HM bit for `moveId` (Red/Blue and Yellow too). MoveBreed5
        /// leaves out TM96, which no game hands out; ChainBreedLegality reads it
        /// (`includeUnavailableMachine`).
        bool isBreedingMachineMove(LearnEnvironment environment, int rowIndex, uint16_t moveId, bool includeUnavailableMachine = false) noexcept;
        /// PKHeX MoveBreed2 MarkMovesForOrigin's Crystal tutor test (PersonalInfo2.GetIsLearnTutorType).
        bool isBreedingTutorCrystal(int rowIndex, uint16_t moveId) noexcept;

        /// The (species, form index) whose egg moves a species-form shares (PKHeX PersonalInfo
        /// HatchSpecies with HatchFormIndexEverstone, or BDSP's HatchFormIndex).
        void getHatchSpeciesForm(LearnEnvironment environment, uint16_t speciesId, uint8_t formId, uint16_t &hatchSpeciesId, uint8_t &hatchFormId) noexcept;

        /// PKHeX IPersonalTable.IsPresentInGame for the game's personal table.
        bool isPresentInGame(LearnEnvironment environment, uint16_t speciesId, uint8_t formId) noexcept;

        /// PKHeX PersonalInfo.RatioMagicGenderless / RatioMagicFemale / RatioMagicMale.
        constexpr uint8_t GENDER_RATIO_GENDERLESS = 255;
        constexpr uint8_t GENDER_RATIO_FEMALE_ONLY = 254;
        constexpr uint8_t GENDER_RATIO_MALE_ONLY = 0;

        /// The breeding fields of a personal row (PKHeX PersonalInfo Gender, EggGroup1, EggGroup2).
        struct BreedingInfo
        {
            /// PKHeX PersonalInfo.Gender: 0 male only, 254 female only, 255 genderless.
            uint8_t gender = 0;
            uint8_t eggGroup1 = 0;
            uint8_t eggGroup2 = 0;
            /// False for a game with no breeding fields (Red/Blue, Yellow, Stadium 2, HOME).
            bool hasData = false;

            bool isGenderless() const noexcept { return gender == GENDER_RATIO_GENDERLESS; }
            bool isOnlyFemale() const noexcept { return gender == GENDER_RATIO_FEMALE_ONLY; }
            bool isOnlyMale() const noexcept { return gender == GENDER_RATIO_MALE_ONLY; }
            /// PKHeX IPersonalEgg.IsEggGroup.
            bool isEggGroup(uint8_t eggGroup) const noexcept { return eggGroup1 == eggGroup || eggGroup2 == eggGroup; }
            /// PKHeX PersonalEggExtensions.SharesCommonEggGroup.
            bool sharesCommonEggGroup(const BreedingInfo &other) const noexcept { return isEggGroup(other.eggGroup1) || isEggGroup(other.eggGroup2); }
        };

        /// PKHeX IPersonalTable[species, form]'s breeding fields (the row PersonalTable.GetFormIndex picks).
        BreedingInfo getBreedingInfo(LearnEnvironment environment, uint16_t speciesId, uint8_t formId) noexcept;
        /// PKHeX IPersonalTable.MaxSpeciesID; 0 for a game with no table.
        uint16_t getMaxSpecies(LearnEnvironment environment) noexcept;

        /// PKHeX LearnSource1RB/1YW.SetEncounterMoves: the personal table's starting moves, then the
        /// level-up moves to `level`.
        void setEncounterMovesGeneration1(LearnEnvironment environment, uint16_t speciesId, uint8_t level, std::span<uint16_t, 4> encounterMoves) noexcept;
    }
}

#endif
