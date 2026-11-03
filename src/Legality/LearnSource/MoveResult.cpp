// Port of PKHeX PKHeX.Core/Legality/LearnSource/MoveResult.cs, MoveLearnInfo.cs, LearnEnvironment.cs and
// PKHeX.Core/Legality/Localization/MoveSourceLocalization.cs (English). See MoveResult.h.
#include "Legality/LearnSource/MoveResult.h"

#include "Names/MoveNames.h"

namespace Legality
{
    namespace LearnSource
    {
        uint8_t getEnvironmentGeneration(LearnEnvironment environment) noexcept
        {
            switch (environment)
            {
                case LearnEnvironment::RB:
                case LearnEnvironment::YW:
                    return 1;
                case LearnEnvironment::GS:
                case LearnEnvironment::C:
                case LearnEnvironment::Stadium2:
                    return 2;
                case LearnEnvironment::RS:
                case LearnEnvironment::E:
                case LearnEnvironment::FR:
                case LearnEnvironment::LG:
                    return 3;
                case LearnEnvironment::DP:
                case LearnEnvironment::PT:
                case LearnEnvironment::HGSS:
                    return 4;
                case LearnEnvironment::BW:
                case LearnEnvironment::B2W2:
                    return 5;
                case LearnEnvironment::XY:
                case LearnEnvironment::ORAS:
                    return 6;
                case LearnEnvironment::SM:
                case LearnEnvironment::USUM:
                case LearnEnvironment::GG:
                    return 7;
                case LearnEnvironment::SWSH:
                case LearnEnvironment::BDSP:
                case LearnEnvironment::PLA:
                    return 8;
                case LearnEnvironment::SV:
                case LearnEnvironment::ZA:
                    return 9;
                default:
                    return 0;
            }
        }

        Enums::GameVersion getEnvironmentContextGroup(LearnEnvironment environment) noexcept
        {
            using Enums::GameVersion;
            switch (environment)
            {
                case LearnEnvironment::RB:
                case LearnEnvironment::YW:
                    return GameVersion::RBY;
                case LearnEnvironment::GS:
                case LearnEnvironment::C:
                case LearnEnvironment::Stadium2:
                    return GameVersion::GSC;
                case LearnEnvironment::RS:
                case LearnEnvironment::E:
                case LearnEnvironment::FR:
                case LearnEnvironment::LG:
                    return GameVersion::RSE;
                case LearnEnvironment::DP:
                case LearnEnvironment::PT:
                case LearnEnvironment::HGSS:
                    return GameVersion::DP;
                case LearnEnvironment::BW:
                case LearnEnvironment::B2W2:
                    return GameVersion::BW;
                case LearnEnvironment::XY:
                case LearnEnvironment::ORAS:
                    return GameVersion::XY;
                case LearnEnvironment::SM:
                case LearnEnvironment::USUM:
                    return GameVersion::SM;
                case LearnEnvironment::GG: return GameVersion::GG;
                case LearnEnvironment::SWSH: return GameVersion::SWSH;
                case LearnEnvironment::PLA: return GameVersion::PLA;
                case LearnEnvironment::BDSP: return GameVersion::BDSP;
                case LearnEnvironment::SV: return GameVersion::SV;
                case LearnEnvironment::ZA: return GameVersion::ZA;
                default: return GameVersion::Invalid;
            }
        }

        const char *getEnvironmentName(LearnEnvironment environment) noexcept
        {
            switch (environment)
            {
                case LearnEnvironment::RB: return "RB";
                case LearnEnvironment::YW: return "YW";
                case LearnEnvironment::GS: return "GS";
                case LearnEnvironment::C: return "C";
                case LearnEnvironment::Stadium2: return "Stadium2";
                case LearnEnvironment::RS: return "RS";
                case LearnEnvironment::E: return "E";
                case LearnEnvironment::FR: return "FR";
                case LearnEnvironment::LG: return "LG";
                case LearnEnvironment::DP: return "DP";
                case LearnEnvironment::PT: return "Pt";
                case LearnEnvironment::HGSS: return "HGSS";
                case LearnEnvironment::BW: return "BW";
                case LearnEnvironment::B2W2: return "B2W2";
                case LearnEnvironment::XY: return "XY";
                case LearnEnvironment::ORAS: return "ORAS";
                case LearnEnvironment::SM: return "SM";
                case LearnEnvironment::USUM: return "USUM";
                case LearnEnvironment::GG: return "GG";
                case LearnEnvironment::SWSH: return "SWSH";
                case LearnEnvironment::BDSP: return "BDSP";
                case LearnEnvironment::PLA: return "PLA";
                case LearnEnvironment::SV: return "SV";
                case LearnEnvironment::ZA: return "ZA";
                case LearnEnvironment::Home: return "HOME";
                default: return "None";
            }
        }

        MoveResult MoveResult::make(const MoveLearnInfo &info, uint8_t evolutionStage, Enums::GameVersion contextGroup) noexcept
        {
            return MoveResult{info, evolutionStage, contextGroup, 0};
        }

        MoveResult MoveResult::make(LearnMethod method, LearnEnvironment environment) noexcept
        {
            return MoveResult{MoveLearnInfo{method, environment, 0}, 0, getEnvironmentContextGroup(environment), 0};
        }

        bool areAllValid(std::span<const MoveResult> results) noexcept
        {
            for (const MoveResult &result : results)
            {
                if (!result.isValid())
                {
                    return false;
                }
            }
            return true;
        }

        bool areAllParsed(std::span<const MoveResult> results) noexcept
        {
            for (const MoveResult &result : results)
            {
                if (!result.isParsed())
                {
                    return false;
                }
            }
            return true;
        }

        bool isAnyFromContext(Enums::GameVersion contextGroup, std::span<const MoveResult> results) noexcept
        {
            for (const MoveResult &result : results)
            {
                if (result.contextGroup == contextGroup)
                {
                    return true;
                }
            }
            return false;
        }

        const char *getMethodSummary(LearnMethod method) noexcept
        {
            switch (method)
            {
                case LearnMethod::Empty:
                case LearnMethod::EmptyInvalid:
                    return "Empty Move.";
                case LearnMethod::Relearn: return "Relearnable Move.";
                case LearnMethod::Initial: return "Default move.";
                case LearnMethod::LevelUp: return "Learned by Level-up.";
                case LearnMethod::TechnicalMachine: return "Learned by TM/HM.";
                case LearnMethod::Tutor: return "Learned by Move Tutor.";
                case LearnMethod::Sketch:
                case LearnMethod::Shared:
                    return "Shared Non-Relearn Move.";
                case LearnMethod::EggMove: return "Base Egg move.";
                case LearnMethod::InheritLevelUp: return "Inherited Egg move.";
                case LearnMethod::Home:
                case LearnMethod::Evolution:
                case LearnMethod::Encounter:
                case LearnMethod::SpecialEgg:
                case LearnMethod::ShedinjaEvolution:
                    return "Special Non-Relearn Move.";
                case LearnMethod::Duplicate: return "Duplicate Move.";
                default: return "Invalid Move.";
            }
        }

        std::string summarizeResult(const MoveResult &result, const std::string &stageSpeciesName)
        {
            std::string summary;
            if (result.info.environment != LearnEnvironment::None)
            {
                summary += getEnvironmentName(result.info.environment);
                summary += '-';
            }
            summary += getMethodSummary(result.info.method);
            if (result.info.method == LearnMethod::LevelUp)
            {
                summary += " @ lv" + std::to_string(result.info.argument);
            }
            if (result.info.method == LearnMethod::UnobtainableExpect)
            {
                summary += " Expected: ";
                summary += Names::getMoveName(result.expectedMove);
                return summary;
            }
            if (!stageSpeciesName.empty())
            {
                summary += ' ';
                summary += stageSpeciesName;
            }
            return summary;
        }
    }
}
