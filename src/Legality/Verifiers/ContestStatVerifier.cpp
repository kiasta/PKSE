// Port of PKHeX PKHeX.Core/Legality/Verifiers/ContestStatVerifier.cs and Verifiers/Misc/ContestStatInfo.cs:
// contest stats and sheen.
#include "Legality/Verifiers/Verifiers.h"

#include <algorithm>
#include <cstdint>
#include <string>

#include "Enums/GameVersion.h"
#include "Legality/Verifiers/NameRules.h"
#include "Pokemon/EvolutionTable.h"
#include "Pokemon/PersonalInfoTable.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace
    {
        constexpr int CONTEST_CONDITION_COUNT = 5;
        constexpr int CONTEST_SHEEN_INDEX = 5;
        constexpr int CONTEST_STAT_COUNT = 6;
        constexpr int MAX_CONTEST_STAT = 255;

        // PKHeX ContestStatInfo's lowest and best sheen per method.
        constexpr int LOWEST_FEEL_BLOCK_GEN3 = 1;
        constexpr int LOWEST_FEEL_POFFIN_GEN4 = 11;
        constexpr int LOWEST_FEEL_POFFIN_BDSP = 7;
        constexpr int HIGHEST_FEEL_POFFIN_BDSP = 79;
        constexpr int BEST_SHEEN_STAT_GEN3 = 214;
        constexpr int BEST_SHEEN_STAT_BDSP = 120;

        /// PKHeX ContestStatGranting.
        enum class ContestStatGranting : uint8_t
        {
            None,
            CorrelateSheen,
            NoSheen,
            Mixed,
        };

        /// PKHeX ContestStatGrantingSheen.
        enum class SheenMethod : uint8_t
        {
            Gen3,
            Gen4,
            BrilliantDiamondShiningPearl,
        };

        /// Cool, Beauty, Cute, Smart, Tough, then sheen.
        struct ContestStats
        {
            int values[CONTEST_STAT_COUNT] = {};
        };

        /// The stat a nature raises or lowers for each condition, as an index into the nature's
        /// Attack, Defense, Speed, Special Attack, Special Defense order: Cool is Spicy (Attack), Beauty
        /// Dry (Special Attack), Cute Sweet (Speed), Smart Bitter (Special Defense), Tough Sour (Defense).
        constexpr int CONDITION_NATURE_STAT[CONTEST_CONDITION_COUNT] = {0, 3, 2, 4, 1};

        // PKHeX NatureAmp.GetAmps for one stat: +1 raised, -1 lowered, 0 neither (an out-of-range
        // nature reads as neutral).
        int getNatureAmp(uint8_t natureId, int natureStatIndex)
        {
            constexpr uint8_t natureCount = 25;
            if (natureId >= natureCount)
            {
                return 0;
            }
            const int raisedStat = natureId / 5;
            const int loweredStat = natureId % 5;
            if (raisedStat == loweredStat)
            {
                return 0;
            }
            if (natureStatIndex == raisedStat)
            {
                return 1;
            }
            return natureStatIndex == loweredStat ? -1 : 0;
        }

        bool isContestEqual(const ContestStats &stats, const ContestStats &initial)
        {
            return std::equal(stats.values, stats.values + CONTEST_STAT_COUNT, initial.values);
        }

        bool isAnyContestStatMax(const ContestStats &stats)
        {
            return std::any_of(stats.values, stats.values + CONTEST_CONDITION_COUNT, [](int statValue) { return statValue == MAX_CONTEST_STAT; });
        }

        // PKHeX ContestStatInfo.GetStatAdjustment: undo the favored-flavor factor.
        int getStatAdjustment(int gain, int natureAmp)
        {
            const int undoFactor = natureAmp == 1 ? 11 : 9;
            int boost = gain / undoFactor;
            if (gain % undoFactor >= 5)
            {
                ++boost;
            }
            return natureAmp == -1 ? boost : -boost;
        }

        // PKHeX ContestStatInfo.GetGainedSum.
        int getGainedSum(const ContestStats &stats, uint8_t natureId, const ContestStats &initial)
        {
            int gainedSum = 0;
            for (int conditionIndex = 0; conditionIndex < CONTEST_CONDITION_COUNT; conditionIndex++)
            {
                const int gain = stats.values[conditionIndex] - initial.values[conditionIndex];
                if (gain <= 0)
                {
                    continue;
                }
                const int natureAmp = getNatureAmp(natureId, CONDITION_NATURE_STAT[conditionIndex]);
                gainedSum += natureAmp == 0 ? gain : gain + getStatAdjustment(gain, natureAmp);
            }
            return gainedSum;
        }

        // PKHeX ContestStatInfo.GetAverageFeel: the gained sum over five, rounded up.
        int getAverageFeel(const ContestStats &stats, uint8_t natureId, const ContestStats &initial)
        {
            const int gainedSum = getGainedSum(stats, natureId, initial);
            return gainedSum <= 0 ? gainedSum / 5 : (gainedSum + 4) / 5;
        }

        // PKHeX ContestStatInfo.CalculateMinimumSheen8b / 3 / 4, which differ only in their bounds.
        int calculateMinimumSheen(const ContestStats &stats, const ContestStats &initial, uint8_t natureId, SheenMethod method)
        {
            if (isContestEqual(stats, initial))
            {
                return initial.values[CONTEST_SHEEN_INDEX];
            }
            const int rawAverage = getAverageFeel(stats, 0, initial);
            int bestSheen = MAX_CONTEST_STAT;
            int lowestSheen = LOWEST_FEEL_POFFIN_GEN4;
            if (method == SheenMethod::BrilliantDiamondShiningPearl)
            {
                bestSheen = BEST_SHEEN_STAT_BDSP;
                lowestSheen = LOWEST_FEEL_POFFIN_BDSP;
            }
            else if (method == SheenMethod::Gen3)
            {
                bestSheen = BEST_SHEEN_STAT_GEN3;
                lowestSheen = LOWEST_FEEL_BLOCK_GEN3;
            }
            if (rawAverage == MAX_CONTEST_STAT)
            {
                return bestSheen;
            }
            int average = std::max(1, natureId % 6 == 0 ? rawAverage : getAverageFeel(stats, natureId, initial));
            average = std::min(rawAverage, average);
            if (method != SheenMethod::Gen4)
            {
                average = (bestSheen * average) / MAX_CONTEST_STAT;
            }
            return static_cast<uint8_t>(std::clamp(average, lowestSheen, bestSheen));
        }

        // PKHeX ContestStatInfo.CalculateMaximumSheen3.
        int calculateMaximumSheenGen3(const ContestStats &stats, uint8_t natureId, const ContestStats &initial)
        {
            const int gainedSum = getGainedSum(stats, natureId, initial);
            if (gainedSum == 0)
            {
                return 0;
            }
            int conditionsGainedTwo = 0;
            for (int conditionIndex = 0; conditionIndex < CONTEST_CONDITION_COUNT; conditionIndex++)
            {
                if (stats.values[conditionIndex] - initial.values[conditionIndex] >= 2)
                {
                    ++conditionsGainedTwo;
                }
            }
            // A bad black block (+2 to three or more conditions, 83 feel) beats the berry blocks' ratio.
            const int permittedSheen = conditionsGainedTwo >= 3 ? (gainedSum * 83 / 6) : (gainedSum * 19 / 9);
            return static_cast<uint8_t>(std::clamp(permittedSheen, LOWEST_FEEL_BLOCK_GEN3, MAX_CONTEST_STAT));
        }

        // PKHeX ContestStatInfo.CalculateMaximumSheen.
        int calculateMaximumSheen(const ContestStats &stats, uint8_t natureId, const ContestStats &initial, bool isPokeBlockGen3)
        {
            if (isAnyContestStatMax(stats))
            {
                return MAX_CONTEST_STAT;
            }
            if (isContestEqual(stats, initial))
            {
                return initial.values[CONTEST_SHEEN_INDEX];
            }
            if (isPokeBlockGen3)
            {
                return calculateMaximumSheenGen3(stats, natureId, initial);
            }
            const int average = getAverageFeel(stats, natureId, initial);
            if (average <= 0)
            {
                return initial.values[CONTEST_SHEEN_INDEX];
            }
            if (average <= 2)
            {
                return 59;
            }
            // Burnt and spilled poffins make low-feel batches possible on purpose.
            return static_cast<uint8_t>(std::min(MAX_CONTEST_STAT, average * HIGHEST_FEEL_POFFIN_BDSP));
        }

        // PKHeX EvolutionHistory.HasVisitedBDSP. A Pokemon reaches Brilliant Diamond/Shining Pearl only
        // through HOME, which needs a HOME tracker unless it already sits outside the HOME games' own
        // formats (Legends: Z-A), and only as a species those games have; Nincada and Spinda are barred.
        bool hasVisitedBrilliantDiamondShiningPearl(const AnalysisContext &context)
        {
            constexpr uint16_t nincadaSpecies = 290;
            constexpr uint16_t spindaSpecies = 327;
            const Pokemon::Pokemon &pokemon = context.pokemon;
            if (context.formatGroup == Enums::GameVersion::BDSP)
            {
                return true;
            }
            const Enums::GameVersion originVersion = static_cast<Enums::GameVersion>(pokemon.originGame());
            if (originVersion == Enums::GameVersion::BD || originVersion == Enums::GameVersion::SP)
            {
                return true;
            }
            const bool hasHomeTracker = pokemon.hasHomeTracker() && pokemon.homeTracker() != 0;
            if (context.formatGeneration < 8 || !(hasHomeTracker || context.formatGroup == Enums::GameVersion::ZA))
            {
                return false;
            }
            if (pokemon.speciesID() == nincadaSpecies || pokemon.speciesID() == spindaSpecies)
            {
                return false;
            }
            uint16_t chainSpecies[Pokemon::EVO_MAX_CHAIN + 1];
            uint8_t chainForms[Pokemon::EVO_MAX_CHAIN + 1];
            chainSpecies[0] = pokemon.speciesID();
            chainForms[0] = pokemon.form();
            const int chainLength = 1 + Pokemon::getPreEvolutionChain(context.formatGroup, pokemon.speciesID(), pokemon.form(), chainSpecies + 1, chainForms + 1, Pokemon::EVO_MAX_CHAIN);
            for (int chainIndex = 0; chainIndex < chainLength; chainIndex++)
            {
                if ((Pokemon::getPersonalInfo(chainSpecies[chainIndex], chainForms[chainIndex]).presence & Pokemon::PERSONAL_GAME_BDSP) != 0)
                {
                    return true;
                }
            }
            return false;
        }

        // PKHeX ContestStatInfo.GetContestStatRestriction.
        ContestStatGranting getContestStatRestriction(const AnalysisContext &context, int originGeneration, bool hasVisitedBrilliantDiamondShiningPearl)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            switch (originGeneration)
            {
            case 3:
                // The Nintendo Switch FireRed/LeafGreen has no contests.
                if (context.formatGeneration == 3 && !allowsGameBoyAdvanceCrossTransfer(context))
                {
                    return ContestStatGranting::None;
                }
                return context.formatGeneration < 6 ? ContestStatGranting::CorrelateSheen : ContestStatGranting::Mixed;
            case 4:
                return context.formatGeneration < 6 ? ContestStatGranting::CorrelateSheen : ContestStatGranting::Mixed;
            case 5:
                if (context.formatGeneration < 6)
                {
                    return ContestStatGranting::None;
                }
                return hasVisitedBrilliantDiamondShiningPearl ? ContestStatGranting::Mixed : ContestStatGranting::NoSheen;
            case 6:
            {
                // PKHeX PKM.AO and G6PKM.IsUntraded: an X/Y Pokemon that never left its first trainer
                // never saw an Omega Ruby/Alpha Sapphire contest.
                const Enums::GameVersion originVersion = static_cast<Enums::GameVersion>(pokemon.originGame());
                const bool isFromOmegaRubyAlphaSapphire = originVersion == Enums::GameVersion::AS || originVersion == Enums::GameVersion::OR;
                const bool isUntraded = context.formatGeneration == 6 && pokemon.htName().empty();
                if (!isFromOmegaRubyAlphaSapphire && isUntraded)
                {
                    return ContestStatGranting::None;
                }
                return hasVisitedBrilliantDiamondShiningPearl ? ContestStatGranting::Mixed : ContestStatGranting::NoSheen;
            }
            default:
                return hasVisitedBrilliantDiamondShiningPearl ? ContestStatGranting::CorrelateSheen : ContestStatGranting::None;
            }
        }

        // Whether PKHeX's encounter may carry contest stats of its own (IContestStatsReadOnly), which then
        // stand in for the zero baseline. PKSE's rows do not record them, so for such an encounter, or
        // one PKSE could not search, the baseline is unknown.
        bool mayEncounterCarryContestStats(const AnalysisContext &context, int originGeneration)
        {
            const bool isFromBrilliantDiamondShiningPearl = context.encounter.originGroup == Enums::GameVersion::BDSP;
            switch (context.encounter.type)
            {
            case EncounterType::Unchecked:
            {
                // A Virtual Console transfer's Game Boy encounter and a Pokemon GO encounter carry none.
                constexpr uint8_t pokemonGoVersion = 34;
                constexpr uint8_t firstVirtualConsoleVersion = 35;
                constexpr uint8_t lastVirtualConsoleVersion = 41;
                const uint8_t originVersion = context.pokemon.hasOriginGame() ? context.pokemon.originGame() : 0;
                const bool isVirtualConsoleOrigin = originVersion >= firstVirtualConsoleVersion && originVersion <= lastVirtualConsoleVersion;
                return !(isVirtualConsoleOrigin || originVersion == pokemonGoVersion);
            }
            case EncounterType::Trade:
                // EncounterTrade3, EncounterTrade4PID, EncounterTrade8b.
                return originGeneration == 3 || originGeneration == 4 || isFromBrilliantDiamondShiningPearl;
            case EncounterType::Static:
            case EncounterType::Gift:
                // EncounterStatic6.
                return originGeneration == 6;
            case EncounterType::Event:
                // PCD, PGF, WC6, WC7, WB8.
                return (originGeneration >= 4 && originGeneration <= 7 && context.encounter.originGroup != Enums::GameVersion::GG) || isFromBrilliantDiamondShiningPearl;
            default:
                return false;
            }
        }
    }

    void verifyContestStats(const AnalysisContext &context)
    {
        const Pokemon::Pokemon &pokemon = context.pokemon;
        if (!pokemon.hasContestStats())
        {
            return;
        }
        ContestStats stats;
        bool hasContestStats = false;
        for (int statIndex = 0; statIndex < CONTEST_STAT_COUNT; statIndex++)
        {
            stats.values[statIndex] = pokemon.contestStat(statIndex);
            hasContestStats = hasContestStats || stats.values[statIndex] != 0;
        }
        if (!hasContestStats)
        {
            return;
        }

        const int originGeneration = getOriginGeneration(context);
        // An encounter that carries contest stats would be compared with them first, and they would be
        // the baseline for everything below; not knowing them, PKSE reports nothing rather than measure
        // from the wrong baseline.
        if (mayEncounterCarryContestStats(context, originGeneration))
        {
            return;
        }
        const ContestStats initial;
        const uint8_t natureId = pokemon.nature();
        const int sheen = stats.values[CONTEST_SHEEN_INDEX];
        const bool hasVisitedBDSP = hasVisitedBrilliantDiamondShiningPearl(context);
        const bool isGen3 = originGeneration == 3;
        switch (getContestStatRestriction(context, originGeneration, hasVisitedBDSP))
        {
        case ContestStatGranting::None:
            context.add(Severity::Invalid, CheckIdentifier::Memory, ResultCode::ContestZero);
            break;
        case ContestStatGranting::NoSheen:
            if (sheen != 0)
            {
                context.add(Severity::Invalid, CheckIdentifier::Memory, ResultCode::ContestZeroSheen);
            }
            break;
        case ContestStatGranting::CorrelateSheen:
        {
            const SheenMethod method = isGen3 ? SheenMethod::Gen3 : (hasVisitedBDSP ? SheenMethod::BrilliantDiamondShiningPearl : SheenMethod::Gen4);
            const int minimumSheen = calculateMinimumSheen(stats, initial, natureId, method);
            if (sheen < minimumSheen)
            {
                context.add(Severity::Invalid, CheckIdentifier::Memory, ResultCode::ContestSheenAtLeast, std::to_string(minimumSheen));
            }
            const int maximumSheen = calculateMaximumSheen(stats, natureId, initial, isGen3);
            if (sheen > maximumSheen)
            {
                context.add(Severity::Invalid, CheckIdentifier::Memory, ResultCode::ContestSheenAtMost, std::to_string(maximumSheen));
            }
            break;
        }
        case ContestStatGranting::Mixed:
        {
            const int maximumSheen = calculateMaximumSheen(stats, natureId, initial, isGen3);
            if (sheen > maximumSheen)
            {
                context.add(Severity::Invalid, CheckIdentifier::Memory, ResultCode::ContestSheenAtMost, std::to_string(maximumSheen));
            }
            break;
        }
        }
    }
}
