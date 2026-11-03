// Port of PKHeX PKHeX.Core/Legality/Verifiers/EffortValueVerifier.cs and EffortExpLegality.cs: effort values.
#include "Legality/Verifiers/Verifiers.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>

#include "Legality/Encounters/Encounterable.h"
#include "Legality/Encounters/LegalityInfo.h"
#include "Pokemon/Experience.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace
    {
        constexpr int STAT_COUNT = 6;
        constexpr int EFFORT_VALUE_STAT_MAXIMUM = 252;
        constexpr int EFFORT_VALUE_FIELD_MAXIMUM = 255;
        constexpr int EFFORT_VALUE_TOTAL_MAXIMUM = 510;
        /// The highest total that still has every point in use: 510 leaves two points over after two 252s.
        constexpr int EFFORT_VALUE_TOTAL_EFFECTIVE = 508;
        constexpr int VITAMIN_MAXIMUM_GENERATION_THREE_FOUR = 100;
        constexpr int VITAMIN_STEP_GENERATION_THREE_FOUR = 10;
        constexpr int VITAMIN_BOOST_GAME_BOY = 2560;
        constexpr int VITAMIN_MAXIMUM_GAME_BOY = VITAMIN_BOOST_GAME_BOY * 10;
        constexpr uint8_t MAXIMUM_LEVEL = 100;
        /// EffortExpLegality's unreachable state.
        constexpr int UNREACHABLE_EXP = 0x7FFFFFFF / 4;

        /// One way to train a stat: the effort values gained for the EXP it costs.
        struct TrainingOption
        {
            int effortValueGain;
            int expCost;
        };

        /// PKHeX EffortValueVerifier.VerifyEVsGB. Stat Experience comes from the defeated Pokemon's base
        /// stats, all at once, so a stat trained past what vitamins give means none can be left at 0.
        void verifyGameBoyEffortValues(const AnalysisContext &context)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            constexpr int gameBoyStatCount = 5;
            bool hasGainedFromBattle = false;
            bool hasZeroStat = false;
            for (int statIndex = 0; statIndex < gameBoyStatCount; ++statIndex)
            {
                const int statExperience = pokemon.evWide(statIndex);
                if (statExperience > VITAMIN_MAXIMUM_GAME_BOY || statExperience % VITAMIN_BOOST_GAME_BOY != 0)
                {
                    hasGainedFromBattle = true;
                }
                if (statExperience == 0)
                {
                    hasZeroStat = true;
                }
            }
            if (hasGainedFromBattle && hasZeroStat)
            {
                context.add(Severity::Invalid, CheckIdentifier::EffortValues, ResultCode::EffortShouldBeZero);
            }
        }

        /// PKHeX EffortExpLegality.SolveExact: the least EXP that yields exactly `requiredEffortValue`.
        int solveMinimumExp(const TrainingOption *options, int optionCount, int requiredEffortValue)
        {
            if (requiredEffortValue <= 0)
            {
                return 0;
            }
            int minimumExpByEffortValue[EFFORT_VALUE_FIELD_MAXIMUM + 1];
            std::fill(minimumExpByEffortValue, minimumExpByEffortValue + EFFORT_VALUE_FIELD_MAXIMUM + 1, UNREACHABLE_EXP);
            minimumExpByEffortValue[0] = 0;
            for (int effortValue = 0; effortValue <= requiredEffortValue; ++effortValue)
            {
                const int currentExp = minimumExpByEffortValue[effortValue];
                if (currentExp >= UNREACHABLE_EXP)
                {
                    continue;
                }
                for (int optionIndex = 0; optionIndex < optionCount; ++optionIndex)
                {
                    const int nextEffortValue = effortValue + options[optionIndex].effortValueGain;
                    if (nextEffortValue > requiredEffortValue)
                    {
                        continue;
                    }
                    const int candidateExp = currentExp + options[optionIndex].expCost;
                    if (candidateExp < minimumExpByEffortValue[nextEffortValue])
                    {
                        minimumExpByEffortValue[nextEffortValue] = candidateExp;
                    }
                }
            }
            return minimumExpByEffortValue[requiredEffortValue];
        }

        /// PKHeX EffortExpLegality.GetMinimumEXP with AddGen4Options / AddGen3Options. `statIndex` is in
        /// PKHeX's order: HP, Attack, Defense, Speed, Special Attack, Special Defense.
        int getMinimumTrainingExp(int statIndex, int requiredEffortValue, bool hasPokerus, bool canTrainGenerationThree, bool canTrainGenerationFour)
        {
            TrainingOption options[12];
            int optionCount = 0;
            if (canTrainGenerationFour)
            {
                // HG/SS have level 2 wild Pokemon yielding 1 EV of every stat for 1 EXP, and the Power items add 4.
                options[optionCount++] = {1, 1};
                options[optionCount++] = {5, 1};
                if (hasPokerus)
                {
                    options[optionCount++] = {2, 1};
                    options[optionCount++] = {10, 1};
                }
            }
            if (canTrainGenerationThree)
            {
                // The GBA games' cheapest donors per stat, diluted to the EXP a benched Pokemon receives.
                switch (statIndex)
                {
                case 0:
                case 1:
                case 3:
                    options[optionCount++] = {1, 1};
                    options[optionCount++] = {2, 1};
                    if (hasPokerus)
                    {
                        options[optionCount++] = {4, 1};
                    }
                    break;
                case 2:
                    options[optionCount++] = {1, 4};
                    options[optionCount++] = {2, 3};
                    options[optionCount++] = {4, 3};
                    if (hasPokerus)
                    {
                        options[optionCount++] = {8, 3};
                    }
                    break;
                case 4:
                    options[optionCount++] = {1, 3};
                    options[optionCount++] = {2, 3};
                    if (hasPokerus)
                    {
                        options[optionCount++] = {4, 3};
                    }
                    break;
                default:
                    options[optionCount++] = {1, 2};
                    options[optionCount++] = {2, 2};
                    if (hasPokerus)
                    {
                        options[optionCount++] = {4, 2};
                    }
                    break;
                }
            }
            return solveMinimumExp(options, optionCount, requiredEffortValue);
        }

        /// PKHeX EffortExpLegality.GetRequiredEffortEXP: positive is the EXP still missing for these
        /// effort values, zero or negative is enough. Vitamins (10 at a time, up to 100) are free.
        int getRequiredEffortExp(const int *effortValues, uint32_t gainedExp, bool hasPokerus, int originGeneration, int formatGeneration)
        {
            const bool canTrainGenerationThree = originGeneration == 3;
            const bool canTrainGenerationFour = formatGeneration >= 4 && originGeneration <= 4;
            if (!canTrainGenerationThree && !canTrainGenerationFour)
            {
                return 0;
            }
            int remainingEffortValues[STAT_COUNT];
            int totalRemaining = 0;
            for (int statIndex = 0; statIndex < STAT_COUNT; ++statIndex)
            {
                const int effortValue = effortValues[statIndex];
                const int vitaminEffortValue = std::min((effortValue / VITAMIN_STEP_GENERATION_THREE_FOUR) * VITAMIN_STEP_GENERATION_THREE_FOUR, VITAMIN_MAXIMUM_GENERATION_THREE_FOUR);
                remainingEffortValues[statIndex] = effortValue - vitaminEffortValue;
                totalRemaining += remainingEffortValues[statIndex];
            }
            if (totalRemaining == 0)
            {
                return gainedExp > 0x7FFFFFFFu ? 0 : -static_cast<int>(gainedExp);
            }
            int neededExp = 0;
            for (int statIndex = 0; statIndex < STAT_COUNT; ++statIndex)
            {
                if (remainingEffortValues[statIndex] == 0)
                {
                    continue;
                }
                neededExp += getMinimumTrainingExp(statIndex, remainingEffortValues[statIndex], hasPokerus, canTrainGenerationThree, canTrainGenerationFour);
            }
            // PKHeX narrows the gained EXP to a signed 32-bit value, so an EXP below the met level's
            // wraps to a negative gain and reads as missing EXP.
            return neededExp - static_cast<int32_t>(gainedExp);
        }

        /// PKHeX EffortValueVerifier.IsWithinVitaminRange34.
        bool isWithinVitaminRange(const int *effortValues)
        {
            for (int statIndex = 0; statIndex < STAT_COUNT; ++statIndex)
            {
                const int effortValue = effortValues[statIndex];
                if (effortValue == 0)
                {
                    continue;
                }
                if (effortValue > VITAMIN_MAXIMUM_GENERATION_THREE_FOUR || effortValue % VITAMIN_STEP_GENERATION_THREE_FOUR != 0)
                {
                    return false;
                }
            }
            return true;
        }

        /// PKHeX EffortValueVerifier.VerifyGainedEVs34: effort values beyond vitamins need the EXP of the
        /// battles that gave them.
        void verifyGainedEffortValues(const AnalysisContext &context, const int *effortValues)
        {
            if (isWithinVitaminRange(effortValues))
            {
                return;
            }
            // The check needs the encounter's lowest level, generation and species.
            const Encounters::Encounterable *encounterMatch = Encounters::getEncounterMatch(context);
            if (encounterMatch == nullptr)
            {
                return;
            }
            const uint8_t encounterLevelMin = encounterMatch->levelMin();
            const int encounterGeneration = encounterMatch->generation();
            const Pokemon::Pokemon &pokemon = context.pokemon;
            if (encounterLevelMin == MAXIMUM_LEVEL)
            {
                context.add(Severity::Invalid, CheckIdentifier::EffortValues, ResultCode::EffortCap100);
                return;
            }
            uint8_t metLevel = encounterGeneration == context.formatGeneration ? pokemon.metLevel() : encounterLevelMin;
            if (metLevel < encounterLevelMin)
            {
                metLevel = encounterLevelMin;
            }
            const uint32_t baseExp = Pokemon::getExpForLevel(metLevel, Pokemon::getGrowthRate(encounterMatch->speciesId()));
            if (baseExp == pokemon.exp())
            {
                context.add(Severity::Invalid, CheckIdentifier::EffortValues, ResultCode::EffortUntrainedCap, std::to_string(VITAMIN_MAXIMUM_GENERATION_THREE_FOUR));
                return;
            }
            const uint32_t gainedExp = pokemon.exp() - baseExp;
            const bool hasPokerus = pokemon.isPokerusInfected() || pokemon.isPokerusCured();
            const int missingExp = getRequiredEffortExp(effortValues, gainedExp, hasPokerus, encounterGeneration, context.formatGeneration);
            if (missingExp > 0)
            {
                context.add(Severity::Invalid, CheckIdentifier::EffortValues, ResultCode::EffortUntrainedMoreEXP, std::to_string(missingExp));
            }
        }
    }

    void verifyEffortValues(const AnalysisContext &context)
    {
        const Pokemon::Pokemon &pokemon = context.pokemon;
        int effortValueTotal = 0;
        for (int statIndex = 0; statIndex < STAT_COUNT; ++statIndex)
        {
            effortValueTotal += pokemon.evWide(statIndex);
        }
        if (pokemon.isEgg())
        {
            if (effortValueTotal != 0)
            {
                context.add(Severity::Invalid, CheckIdentifier::EffortValues, ResultCode::EffortEgg);
            }
            return;
        }
        if (context.formatGeneration < 3)
        {
            verifyGameBoyEffortValues(context);
            return;
        }
        if (effortValueTotal > EFFORT_VALUE_TOTAL_MAXIMUM)
        {
            context.add(Severity::Invalid, CheckIdentifier::EffortValues, ResultCode::EffortAbove510);
            return;
        }

        // PKHeX's stat order: HP, Attack, Defense, Speed, Special Attack, Special Defense.
        const int effortValues[STAT_COUNT] = {pokemon.evHP(), pokemon.evATK(), pokemon.evDEF(), pokemon.evSPE(), pokemon.evSPA(), pokemon.evSPD()};
        bool isAnyAboveStatMaximum = false;
        for (const int effortValue : effortValues)
        {
            isAnyAboveStatMaximum = isAnyAboveStatMaximum || effortValue > EFFORT_VALUE_STAT_MAXIMUM;
        }
        if (context.formatGeneration >= 6 && isAnyAboveStatMaximum)
        {
            context.add(Severity::Invalid, CheckIdentifier::EffortValues, ResultCode::EffortAbove252);
        }
        else if (context.formatGeneration < 5)
        {
            verifyGainedEffortValues(context, effortValues);
        }
        // PKHeX's Legends: Arceus rule (IsUnableToGainEVs) is disabled there, so nothing is checked for Gen 5+.

        bool areAllEqual = true;
        for (const int effortValue : effortValues)
        {
            areAllEqual = areAllEqual && effortValue == effortValues[0];
        }
        if (effortValueTotal == 0)
        {
            const Encounters::Encounterable *encounterMatch = Encounters::getEncounterMatch(context);
            if (encounterMatch != nullptr && !encounterMatch->isWithinEncounterRange(context.pokemon))
            {
                context.add(Severity::Warning, CheckIdentifier::EffortValues, ResultCode::EffortEXPIncreased);
            }
        }
        else if (effortValueTotal == EFFORT_VALUE_TOTAL_EFFECTIVE)
        {
            context.add(Severity::Warning, CheckIdentifier::EffortValues, ResultCode::Effort2Remaining);
        }
        else if (effortValues[0] != 0 && areAllEqual)
        {
            context.add(Severity::Warning, CheckIdentifier::EffortValues, ResultCode::EffortAllEqual);
        }
    }
}
