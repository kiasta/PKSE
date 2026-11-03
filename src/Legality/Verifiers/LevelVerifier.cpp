// Port of PKHeX PKHeX.Core/Legality/Verifiers/LevelVerifier.cs: level, EXP and met level.
#include "Legality/Verifiers/Verifiers.h"

#include <cstdint>
#include <optional>
#include <string>

#include "Enums/GameVersion.h"
#include "Legality/Encounters/LegalityInfo.h"
#include "Legality/Encounters/MysteryGiftPCD.h"
#include "Legality/Encounters/MysteryGiftWC7.h"
#include "Pokemon/Experience.h"
#include "Pokemon/Pokemon.h"
#include "Trainer/Trainer.h"
#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Evolutions/EvolutionEntity.h"

namespace Legality
{
    namespace
    {
        constexpr uint8_t MAX_LEVEL = 100;
        /// Crystal's Odd Egg holds 125 EXP even where that is more than level 5 needs.
        constexpr uint32_t DIZZY_PUNCH_EGG_EXP = 125;

        // PKHeX EncounterStatic2.IsDizzyPunchEgg: the Odd Egg, the only Gen 2 egg that knows Dizzy Punch.
        bool isDizzyPunchEgg(const AnalysisContext &context)
        {
            constexpr uint16_t dizzyPunchMove = 146;
            constexpr int moveSlotCount = 4;
            if (context.formatGeneration != 2)
            {
                return false;
            }
            for (int moveIndex = 0; moveIndex < moveSlotCount; moveIndex++)
            {
                if (context.pokemon.move(moveIndex) == dizzyPunchMove)
                {
                    return true;
                }
            }
            return false;
        }

        // PKHeX LevelVerifier.VerifyG1.
        void verifyLevelGameBoy(const AnalysisContext &context, uint8_t currentLevel)
        {
            constexpr uint8_t gameBoyEggLevel = 5;
            const Pokemon::Pokemon &pokemon = context.pokemon;
            if (pokemon.isEgg())
            {
                if (currentLevel != gameBoyEggLevel)
                {
                    context.add(Severity::Invalid, CheckIdentifier::Level, ResultCode::EggFMetLevel, std::to_string(gameBoyEggLevel));
                }
                return;
            }
            // Only Crystal records where a Pokemon was caught.
            if (pokemon.hasMetData() && pokemon.metLocation() != 0 && currentLevel < pokemon.metLevel())
            {
                context.add(Severity::Invalid, CheckIdentifier::Level, ResultCode::LevelMetBelow, std::to_string(currentLevel));
            }

            // PKHeX LevelVerifier.IsTradeEvolutionRequired. Stadium 2 (a cartridge Game Boy save) can move a
            // Pokemon between games without a trade, and a save of a later generation skips the check too.
            const ActiveTrainer *activeTrainer = context.options.activeTrainer;
            const int activeTrainerGeneration = activeTrainer != nullptr ? getActiveTrainerGeneration(*activeTrainer) : 0;
            if (allowsGameBoyCartridgeEra(context.options) || (activeTrainer != nullptr && activeTrainerGeneration != 1 && activeTrainerGeneration != 2))
            {
                return;
            }
            const Encounters::Encounterable *encounterMatch = Encounters::getEncounterMatch(context);
            if (encounterMatch == nullptr || encounterMatch->speciesId() != pokemon.speciesID())
            {
                return;
            }
            constexpr uint16_t kadabraSpecies = 64;
            constexpr uint16_t machokeSpecies = 67;
            constexpr uint16_t gravelerSpecies = 75;
            constexpr uint16_t haunterSpecies = 93;
            const uint16_t encounterSpecies = encounterMatch->speciesId();
            if (encounterSpecies != kadabraSpecies && encounterSpecies != machokeSpecies && encounterSpecies != gravelerSpecies && encounterSpecies != haunterSpecies)
            {
                return;
            }
            const int encounterGeneration = encounterMatch->generation();
            // A Gen 2 encounter, and a Gen 1 one still in Gen 1, are judged by which generation each move
            // was learned in, which PKSE does not record; only a Gen 1 encounter now in Gen 2 is certain.
            if (encounterGeneration != 1 || context.formatGeneration == 1)
            {
                return;
            }
            context.add(Severity::Invalid, CheckIdentifier::Level, ResultCode::EvolutionTradeRequiredOutsider, Trainer::getSpeciesName(encounterSpecies), Trainer::getSpeciesName(static_cast<uint16_t>(encounterSpecies + 1)));
        }

        /// PKHeX LevelVerifier.IsMetLevelMatchEncounter.
        bool isMetLevelMatchGift(const AnalysisContext &context, const Encounters::Encounterable &gift)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            if (gift.levelMin() == pokemon.metLevel() || !Encounters::hasOriginalMetLevel(context.pokemon))
            {
                return true;
            }
            if (Encounters::encounterAs<Encounters::MysteryGiftWC7>(gift) != nullptr && gift.metLevel() == pokemon.metLevel())
            {
                return true;
            }
            // The only raw gift PKHeX matches is the Pokemon Ranger Manaphy egg.
            return Encounters::encounterAs<Encounters::MysteryGiftPGT>(gift) != nullptr && pokemon.metLevel() == 0;
        }

        // PKHeX LevelVerifier.Verify.
        void verifyLevelCommon(const AnalysisContext &context, uint8_t currentLevel, uint8_t growthRate)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            const Encounters::Encounterable *encounterOriginal = Encounters::getEncounterOriginal(context);
            if (encounterOriginal != nullptr && Encounters::isMysteryGift(encounterOriginal->templateType()))
            {
                const uint8_t giftLevel = encounterOriginal->levelMin();
                if (!isMetLevelMatchGift(context, *encounterOriginal))
                {
                    context.add(Severity::Invalid, CheckIdentifier::Level, ResultCode::LevelMetGift);
                    return;
                }
                if (giftLevel > currentLevel)
                {
                    context.add(Severity::Invalid, CheckIdentifier::Level, ResultCode::LevelMetGiftFail, std::to_string(giftLevel));
                    return;
                }
            }

            if (pokemon.isEgg())
            {
                const Encounters::Encounterable *encounterMatch = Encounters::getEncounterMatch(context);
                if (encounterMatch == nullptr)
                {
                    return;
                }
                const uint8_t eggLevel = encounterMatch->levelMin();
                if (currentLevel != eggLevel)
                {
                    context.add(Severity::Invalid, CheckIdentifier::Level, ResultCode::EggFMetLevel, std::to_string(eggLevel));
                    return;
                }
                const uint32_t requiredExp = isDizzyPunchEgg(context) ? DIZZY_PUNCH_EGG_EXP : Pokemon::getExpForLevel(eggLevel, growthRate);
                if (requiredExp != pokemon.exp())
                {
                    context.add(Severity::Invalid, CheckIdentifier::Level, ResultCode::EggEXP, std::to_string(requiredExp));
                }
                return;
            }

            if (currentLevel >= MAX_LEVEL)
            {
                const uint32_t maximumExp = Pokemon::getExpForLevel(MAX_LEVEL, growthRate);
                if (pokemon.exp() != maximumExp)
                {
                    context.add(Severity::Invalid, CheckIdentifier::Level, ResultCode::LevelEXPTooHigh, std::to_string(maximumExp));
                }
            }

            if (currentLevel < pokemon.metLevel())
            {
                context.add(Severity::Invalid, CheckIdentifier::Level, ResultCode::LevelMetBelow, std::to_string(currentLevel));
                return;
            }
            if (currentLevel == MAX_LEVEL || pokemon.exp() != Pokemon::getExpForLevel(currentLevel, growthRate))
            {
                return;
            }
            if (encounterOriginal != nullptr && !encounterOriginal->isWithinEncounterRange(pokemon))
            {
                context.add(Severity::Warning, CheckIdentifier::Level, ResultCode::LevelEXPThreshold);
            }
        }
    }

    void verifyLevel(const AnalysisContext &context)
    {
        const Pokemon::Pokemon &pokemon = context.pokemon;
        const uint8_t growthRate = Pokemon::getGrowthRate(pokemon.speciesID());
        const uint8_t currentLevel = Evolutions::getEntityCurrentLevel(pokemon);
        // A Game Boy format runs PKHeX's Verify and then VerifyG1 (LegalityAnalysis.ParsePK1).
        verifyLevelCommon(context, currentLevel, growthRate);
        if (context.formatGeneration < 3)
        {
            verifyLevelGameBoy(context, currentLevel);
        }
    }
}
