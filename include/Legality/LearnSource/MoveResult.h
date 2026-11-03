/**
 * The verdict for one move slot: how the move was learned (or why it could not have been), in which
 * game, and at which evolution stage. Ported from PKHeX PKHeX.Core/Legality/LearnSource
 * (LearnMethod.cs, LearnEnvironment.cs, MoveLearnInfo.cs, MoveResult.cs, Sources/Shared/LearnOption.cs)
 * and PKHeX.Core/Legality/MoveSourceType.cs.
 */
#ifndef LEGALITY_LEARNSOURCE_MOVERESULT_H
#define LEGALITY_LEARNSOURCE_MOVERESULT_H

#include <cstdint>
#include <span>
#include <string>

#include "Enums/GameVersion.h"

namespace Legality
{
    namespace LearnSource
    {
        /// PKHeX LearnMethod. Everything before Empty is invalid.
        enum class LearnMethod : uint8_t
        {
            None,
            Duplicate,
            EmptyInvalid,
            Unobtainable,
            UnobtainableExpect,

            Empty,
            Initial,
            LevelUp,
            TechnicalMachine,
            Tutor,
            Sketch,
            Home,
            Encounter,
            Evolution,
            Shared,
            ShedinjaEvolution,

            Relearn,

            EggMove,
            InheritLevelUp,
            SpecialEgg,
        };

        constexpr bool isValidMethod(LearnMethod method) noexcept { return method >= LearnMethod::Empty; }
        constexpr bool isEggSourceMethod(LearnMethod method) noexcept { return method == LearnMethod::EggMove || method == LearnMethod::InheritLevelUp || method == LearnMethod::SpecialEgg; }

        /// PKHeX LearnEnvironment: the game whose learn data explains a move.
        enum class LearnEnvironment : uint8_t
        {
            None,
            RB,
            YW,
            GS,
            C,
            Stadium2,
            RS,
            E,
            FR,
            LG,
            DP,
            PT,
            HGSS,
            BW,
            B2W2,
            XY,
            ORAS,
            SM,
            USUM,
            GG,
            SWSH,
            BDSP,
            PLA,
            SV,
            ZA,
            Home,
        };

        /// PKHeX LearnEnvironment.Generation.
        uint8_t getEnvironmentGeneration(LearnEnvironment environment) noexcept;
        /// PKHeX LearnEnvironment.Context, as a context group (Legality::Evolutions::EVOLUTION_CONTEXT_GROUPS);
        /// Invalid for None and Home.
        Enums::GameVersion getEnvironmentContextGroup(LearnEnvironment environment) noexcept;
        /// PKHeX's enumerator name ("RB", "Pt", "HOME"...), which its move summary prints.
        const char *getEnvironmentName(LearnEnvironment environment) noexcept;

        /// PKHeX MoveSourceType: which kinds of source a check may use.
        enum MoveSourceType : uint16_t
        {
            MOVE_SOURCE_NONE = 0,
            MOVE_SOURCE_LEVEL_UP = 1 << 0,
            MOVE_SOURCE_RELEARN_MOVES = 1 << 1,
            MOVE_SOURCE_MACHINE = 1 << 2,
            MOVE_SOURCE_TYPE_TUTOR = 1 << 3,
            MOVE_SOURCE_SPECIAL_TUTOR = 1 << 4,
            MOVE_SOURCE_ENHANCED_TUTOR = 1 << 5,
            MOVE_SOURCE_SHARED_EGG_MOVE = 1 << 6,
            MOVE_SOURCE_TECHNICAL_RECORD = 1 << 7,
            MOVE_SOURCE_EVOLVE = 1 << 8,

            MOVE_SOURCE_ALL_TUTORS = MOVE_SOURCE_TYPE_TUTOR | MOVE_SOURCE_SPECIAL_TUTOR | MOVE_SOURCE_ENHANCED_TUTOR,
            MOVE_SOURCE_ALL_MACHINES = MOVE_SOURCE_MACHINE | MOVE_SOURCE_TECHNICAL_RECORD,
            MOVE_SOURCE_REMINDER = MOVE_SOURCE_LEVEL_UP | MOVE_SOURCE_RELEARN_MOVES | MOVE_SOURCE_TECHNICAL_RECORD | MOVE_SOURCE_EVOLVE,
            MOVE_SOURCE_ENCOUNTER = MOVE_SOURCE_LEVEL_UP | MOVE_SOURCE_RELEARN_MOVES,
            MOVE_SOURCE_EXTERNAL = MOVE_SOURCE_REMINDER | MOVE_SOURCE_ALL_MACHINES | MOVE_SOURCE_ALL_TUTORS,
            MOVE_SOURCE_ALL = MOVE_SOURCE_EXTERNAL | MOVE_SOURCE_SHARED_EGG_MOVE | MOVE_SOURCE_RELEARN_MOVES,
        };

        /// PKHeX MoveSourceType.HasFlag: every bit of `flag` is set in `types`.
        constexpr bool hasSourceType(uint16_t types, uint16_t flag) noexcept { return (types & flag) == flag; }

        /// PKHeX LearnOption.
        enum class LearnOption : uint8_t
        {
            Current,
            AtAnyTime,
            Home,
            AtAnyTimeChain,
        };

        constexpr bool isPastOption(LearnOption option) noexcept { return option != LearnOption::Current; }
        constexpr bool isFlagCheckRequired(LearnOption option) noexcept { return option != LearnOption::Home; }

        /// PKHeX MoveLearnInfo. `argument` is the level for a level-up move.
        struct MoveLearnInfo
        {
            LearnMethod method = LearnMethod::None;
            LearnEnvironment environment = LearnEnvironment::None;
            uint8_t argument = 0;

            constexpr bool operator==(const MoveLearnInfo &other) const noexcept = default;
            /// PKHeX compares a MoveLearnInfo with `default` to ask "found nothing".
            constexpr bool isEmpty() const noexcept { return method == LearnMethod::None && environment == LearnEnvironment::None && argument == 0; }
        };

        /// PKHeX MoveResult. A default-constructed result is "not parsed yet".
        struct MoveResult
        {
            MoveLearnInfo info{};
            uint8_t evolutionStage = 0;
            /// PKHeX MoveResult.Context, as a context group; Invalid for PKHeX's 0.
            Enums::GameVersion contextGroup = Enums::GameVersion::Invalid;
            /// The move the slot should hold, for LearnMethod::UnobtainableExpect.
            uint16_t expectedMove = 0;

            constexpr bool operator==(const MoveResult &other) const noexcept = default;
            constexpr bool isParsed() const noexcept { return !(*this == MoveResult{}); }
            constexpr bool isValid() const noexcept { return isValidMethod(info.method); }
            constexpr bool isRelearn() const noexcept { return info.method == LearnMethod::Relearn; }

            /// PKHeX new MoveResult(info, stage, context).
            static MoveResult make(const MoveLearnInfo &info, uint8_t evolutionStage, Enums::GameVersion contextGroup) noexcept;
            /// PKHeX new MoveResult(method, environment): the environment's own context.
            static MoveResult make(LearnMethod method, LearnEnvironment environment) noexcept;
            static MoveResult initial(LearnEnvironment environment) noexcept { return make(LearnMethod::Initial, environment); }
            static constexpr MoveResult withMethod(LearnMethod method) noexcept { return MoveResult{MoveLearnInfo{method, LearnEnvironment::None, 0}, 0, Enums::GameVersion::Invalid, 0}; }
            static constexpr MoveResult relearn() noexcept { return withMethod(LearnMethod::Relearn); }
            static constexpr MoveResult empty() noexcept { return withMethod(LearnMethod::Empty); }
            static constexpr MoveResult duplicate() noexcept { return withMethod(LearnMethod::Duplicate); }
            static constexpr MoveResult emptyInvalid() noexcept { return withMethod(LearnMethod::EmptyInvalid); }
            static constexpr MoveResult sketch() noexcept { return withMethod(LearnMethod::Sketch); }
            static constexpr MoveResult unobtainable() noexcept { return withMethod(LearnMethod::Unobtainable); }
            static constexpr MoveResult unobtainableExpect(uint16_t expectedMove) noexcept
            {
                MoveResult result = withMethod(LearnMethod::UnobtainableExpect);
                result.expectedMove = expectedMove;
                return result;
            }
        };

        /// PKHeX MoveResult.AllValid / AllParsed.
        bool areAllValid(std::span<const MoveResult> results) noexcept;
        bool areAllParsed(std::span<const MoveResult> results) noexcept;
        /// PKHeX MoveInfo.IsAnyFromGeneration: any result was learned in that context.
        bool isAnyFromContext(Enums::GameVersion contextGroup, std::span<const MoveResult> results) noexcept;

        /// PKHeX MoveSourceLocalization (English): "Learned by Level-up." and its siblings.
        const char *getMethodSummary(LearnMethod method) noexcept;
        /**
         * PKHeX MoveResult.Summary: the environment and method ("SV-Learned by TM/HM."), the level
         * for a level-up move, and "Expected: <move>" when a specific move was due. The evolution
         * stage suffix is filled from `stageSpeciesName` when it names a different species.
         */
        std::string summarizeResult(const MoveResult &result, const std::string &stageSpeciesName);
    }
}

#endif
