// Port of PKHeX PKHeX.Core/Legality/Verifiers/MedalVerifier.cs: Super Training medals and the training bag.
#include "Legality/Verifiers/Verifiers.h"

#include <cstdint>
#include <string>

#include "Legality/Legality.h"
#include "Legality/Verifiers/NameRules.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace
    {
        /// PKHeX SuperTrainBag: None, the eighteen stat bags, six special bags, Reset and Soothing.
        constexpr uint8_t SUPER_TRAIN_BAG_SOOTHING = 26;
        constexpr uint32_t SUPER_TRAIN_UNUSED_FLAGS = 0b11;

        // PKHeX SuperTrainBagExtensions.Max: how often each bag can be hit.
        // clang-format off
        constexpr uint8_t SUPER_TRAIN_BAG_MAXIMUM_HITS[SUPER_TRAIN_BAG_SOOTHING + 1] = {
            0,
            2, 5, 10, // HP
            2, 5, 10, // Attack
            2, 5, 10, // Defense
            2, 5, 10, // Special Attack
            2, 5, 10, // Special Defense
            2, 5, 10, // Speed
            2, 2, 2, 2, 2, 2, // Strength, Toughen Up, Swiftness, Big Shot, Double Up, Team Flare
            20, // Reset
            50, // Soothing
        };
        // clang-format on

        void addSecretFlagFindings(const AnalysisContext &context)
        {
            if (context.pokemon.isSecretSuperTrainingUnlocked())
            {
                context.add(Severity::Invalid, CheckIdentifier::Training, ResultCode::SuperNoUnlocked);
            }
            if (context.pokemon.isSuperTrainSupremelyTrained())
            {
                context.add(Severity::Invalid, CheckIdentifier::Training, ResultCode::SuperNoComplete);
            }
        }

        // PKHeX MedalVerifier.VerifyMedalsRegular.
        void verifyRegularMedals(const AnalysisContext &context)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            const uint32_t superTrainFlags = pokemon.superTrainFlags();
            if ((superTrainFlags & SUPER_TRAIN_UNUSED_FLAGS) != 0)
            {
                context.add(Severity::Invalid, CheckIdentifier::Training, ResultCode::SuperUnused);
            }

            if (pokemon.isEgg())
            {
                if (superTrainFlags != 0)
                {
                    context.add(Severity::Invalid, CheckIdentifier::Training, ResultCode::SuperEgg);
                }
                addSecretFlagFindings(context);
                return;
            }

            const int originGeneration = getOriginGeneration(context);
            if (originGeneration >= 7 || originGeneration <= 2)
            {
                if (superTrainFlags != 0)
                {
                    context.add(Severity::Invalid, CheckIdentifier::Training, ResultCode::SuperUnavailable);
                }
                addSecretFlagFindings(context);
                return;
            }

            // Moving to Gen 7 clears both Secret flags.
            if (context.formatGeneration >= 7)
            {
                addSecretFlagFindings(context);
                return;
            }

            // The Secret Complete flag needs every medal, Secret ones included (PKHeX
            // RibbonRules.IsSuperTrainSupremelyTrained).
            const bool isActuallySupremelyTrained = (superTrainFlags & ~SUPER_TRAIN_UNUSED_FLAGS) == (0xFFFFFFFFu & ~SUPER_TRAIN_UNUSED_FLAGS);
            if (pokemon.isSuperTrainSupremelyTrained() != isActuallySupremelyTrained)
            {
                context.add(Severity::Invalid, CheckIdentifier::Training, ResultCode::SuperComplete);
            }
        }

        // PKHeX MedalVerifier.VerifyTrainingBag.
        void verifyTrainingBag(const AnalysisContext &context)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            const uint8_t trainingBag = pokemon.trainingBag();
            const uint8_t trainingBagHits = pokemon.trainingBagHits();
            if (pokemon.isEgg())
            {
                if (trainingBag != 0)
                {
                    context.add(Severity::Invalid, CheckIdentifier::Training, ResultCode::Gen6SuperTrainEggBag);
                }
                if (trainingBagHits != 0)
                {
                    context.add(Severity::Invalid, CheckIdentifier::Training, ResultCode::Gen6SuperTrainEggHits);
                }
                return;
            }
            if (trainingBag > SUPER_TRAIN_BAG_SOOTHING)
            {
                context.add(Severity::Invalid, CheckIdentifier::Training, ResultCode::Gen6SuperTrainBagInvalid, std::to_string(trainingBag));
                return;
            }
            const uint8_t maximumHits = SUPER_TRAIN_BAG_MAXIMUM_HITS[trainingBag];
            if (trainingBagHits > maximumHits)
            {
                // The template's third placeholder is the span's maximum, past what add() fills.
                std::string hitsText = getResultText(ResultCode::Gen6SuperTrainBagHitsInvalid, std::to_string(trainingBagHits), "0");
                const size_t maximumPlaceholderOffset = hitsText.find("{2}");
                if (maximumPlaceholderOffset != std::string::npos)
                {
                    hitsText.replace(maximumPlaceholderOffset, 3, std::to_string(maximumHits));
                }
                context.addText(Severity::Invalid, CheckIdentifier::Training, ResultCode::Gen6SuperTrainBagHitsInvalid, hitsText);
            }
        }
    }

    void verifyMedals(const AnalysisContext &context)
    {
        const Pokemon::Pokemon &pokemon = context.pokemon;
        if (!pokemon.hasSuperTraining())
        {
            return;
        }
        verifyRegularMedals(context);
        // Event regimens were never distributed.
        if (pokemon.distributionSuperTrainFlags() != 0)
        {
            context.add(Severity::Invalid, CheckIdentifier::Training, ResultCode::SuperDistribution);
        }
        if (pokemon.hasTrainingBag())
        {
            verifyTrainingBag(context);
        }
    }
}
