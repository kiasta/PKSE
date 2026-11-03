// Port of PKHeX PKHeX.Core/Legality/Verifiers/HyperTrainingVerifier.cs: Hyper Training.
#include "Legality/Verifiers/Verifiers.h"

#include <cstdint>
#include <string>

#include "Enums/GameVersion.h"
#include "Pokemon/Pokemon.h"
#include "Legality/Evolutions/EvolutionEntity.h"

namespace Legality
{
    namespace
    {
        constexpr uint8_t HYPER_TRAINED_ALL_FLAGS = 0x3F;
        constexpr uint8_t HYPER_TRAIN_MINIMUM_LEVEL_GENERATION_EIGHT = 100;
        constexpr uint8_t HYPER_TRAIN_MINIMUM_LEVEL_GENERATION_NINE = 50;

        uint8_t getStoredIV(const Pokemon::Pokemon &pokemon, int statIndex)
        {
            switch (statIndex)
            {
            case Pokemon::Pokemon::HT_HP:
                return pokemon.ivHP();
            case Pokemon::Pokemon::HT_ATK:
                return pokemon.ivATK();
            case Pokemon::Pokemon::HT_DEF:
                return pokemon.ivDEF();
            case Pokemon::Pokemon::HT_SPA:
                return pokemon.ivSPA();
            case Pokemon::Pokemon::HT_SPD:
                return pokemon.ivSPD();
            default:
                return pokemon.ivSPE();
            }
        }

        /// PKHeX IHyperTrain.GetHyperTrainMinLevel: 50 once the Pokemon has been in Scarlet/Violet or
        /// Legends: Z-A, 100 otherwise and always in Brilliant Diamond/Shining Pearl, whose duplicate
        /// checker rejects less. Zero when it is unknown: a HOME-tracked Sword/Shield or Legends: Arceus
        /// Pokemon may have visited a Gen 9 game, and PKSE does not build PKHeX's evolution history.
        uint8_t getHyperTrainMinimumLevel(const AnalysisContext &context)
        {
            const Enums::GameVersion formatGroup = context.formatGroup;
            if (formatGroup == Enums::GameVersion::BDSP)
            {
                return HYPER_TRAIN_MINIMUM_LEVEL_GENERATION_EIGHT;
            }
            if (formatGroup == Enums::GameVersion::SV || formatGroup == Enums::GameVersion::ZA)
            {
                return HYPER_TRAIN_MINIMUM_LEVEL_GENERATION_NINE;
            }
            if (context.pokemon.hasHomeTracker() && context.pokemon.homeTracker() != 0)
            {
                return 0;
            }
            return HYPER_TRAIN_MINIMUM_LEVEL_GENERATION_EIGHT;
        }
    }

    void verifyHyperTraining(const AnalysisContext &context)
    {
        const Pokemon::Pokemon &pokemon = context.pokemon;
        if (!pokemon.hasHyperTraining())
        {
            return;
        }
        const uint8_t hyperTrainFlags = pokemon.hyperTrainFlags();
        if (hyperTrainFlags == 0)
        {
            return;
        }
        if (!pokemon.isHyperTrainingAvailable())
        {
            context.add(Severity::Invalid, CheckIdentifier::Training, ResultCode::HyperPerfectUnavailable);
            return;
        }

        const uint8_t minimumLevel = getHyperTrainMinimumLevel(context);
        if (minimumLevel != 0 && Evolutions::getEntityCurrentLevel(pokemon) < minimumLevel)
        {
            context.add(Severity::Invalid, CheckIdentifier::Training, ResultCode::HyperTrainLevelAtLeast, std::to_string(minimumLevel));
            return;
        }

        const uint8_t maximumIV = pokemon.maxIV();
        const int individualValueTotal = pokemon.ivHP() + pokemon.ivATK() + pokemon.ivDEF() + pokemon.ivSPA() + pokemon.ivSPD() + pokemon.ivSPE();
        if (individualValueTotal == maximumIV * 6)
        {
            context.add(Severity::Invalid, CheckIdentifier::Training, ResultCode::HyperPerfectAll);
            return;
        }

        // Let's Go's Gold Bottle Cap trains every stat, a perfect one included.
        if (hyperTrainFlags == HYPER_TRAINED_ALL_FLAGS && context.formatGroup == Enums::GameVersion::GG)
        {
            return;
        }

        for (int statIndex = 0; statIndex < 6; ++statIndex)
        {
            if (getStoredIV(pokemon, statIndex) == maximumIV && pokemon.isHyperTrained(statIndex))
            {
                context.add(Severity::Invalid, CheckIdentifier::Training, ResultCode::HyperPerfectOne);
                return;
            }
        }
    }
}
