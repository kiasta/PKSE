// Port of PKHeX PKHeX.Core/Legality/Verifiers/Misc/MiscVerifierPK8.cs, MiscVerifierPB8.cs and
// MiscVerifierPA8.cs: the Sword/Shield, Brilliant Diamond/Shining Pearl and Legends: Arceus formats.
#include "Legality/Verifiers/MiscVerifier.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

#include "Enums/GameVersion.h"
#include "Legality/Verifiers/NameRules.h"
#include "Names/MoveNames.h"
#include "Pokemon/MoveRecordTable.h"
#include "Pokemon/Pokemon8BDSP.h"
#include "Pokemon/Pokemon8LA.h"
#include "Pokemon/Pokemon8SWSH.h"
#include "Legality/Encounters/Encounterable.h"
#include "Legality/Encounters/LegalityInfo.h"

namespace Legality
{
    namespace
    {
        constexpr uint16_t PIKACHU_SPECIES = 25;
        constexpr uint16_t MEOWTH_SPECIES = 52;
        constexpr uint16_t SPINDA_SPECIES = 327;
        constexpr uint16_t NINCADA_SPECIES = 290;
        constexpr uint16_t ZACIAN_SPECIES = 888;
        constexpr uint16_t ZAMAZENTA_SPECIES = 889;
        constexpr uint16_t ETERNATUS_SPECIES = 890;
        constexpr uint16_t CALYREX_SPECIES = 898;
        constexpr uint16_t ENAMORUS_SPECIES = 905;

        constexpr uint8_t DYNAMAX_LEVEL_MAXIMUM = 10;
        constexpr uint32_t SOCIABILITY_MAXIMUM = 255;
        constexpr uint8_t BRILLIANT_DIAMOND_VERSION = 48;
        constexpr uint8_t SHINING_PEARL_VERSION = 49;
        constexpr uint8_t LEGENDS_ARCEUS_VERSION = 47;

        /// PKHeX WA8.EnamorusHeight / EnamorusWeight: the HOME gift's hand-entered absolutes.
        constexpr float ENAMORUS_GIFT_HEIGHT = 160.0f;
        constexpr float ENAMORUS_GIFT_WEIGHT = 479.2f;

        /// PKHeX Gigantamax.CanToggle(species): what Max Soup can switch.
        bool canToggleGigantamax(uint16_t speciesId)
        {
            switch (speciesId)
            {
                case 3:   // Venusaur
                case 6:   // Charizard
                case 9:   // Blastoise
                case 12:  // Butterfree
                case PIKACHU_SPECIES:
                case MEOWTH_SPECIES:
                case 68:  // Machamp
                case 94:  // Gengar
                case 99:  // Kingler
                case 131: // Lapras
                case 133: // Eevee
                case 143: // Snorlax
                case 569: // Garbodor
                case 812: // Rillaboom
                case 815: // Cinderace
                case 818: // Inteleon
                case 823: // Corviknight
                case 826: // Orbeetle
                case 834: // Drednaw
                case 839: // Coalossal
                case 841: // Flapple
                case 842: // Appletun
                case 844: // Sandaconda
                case 849: // Toxtricity
                case 851: // Centiskorch
                case 858: // Hatterene
                case 861: // Grimmsnarl
                case 869: // Alcremie
                case 879: // Copperajah
                case 884: // Duraludon
                case 892: // Urshifu
                    return true;
                default:
                    return false;
            }
        }

        /// PKHeX Gigantamax.CanToggle(currentSpecies, currentForm, originSpecies, originForm).
        bool canToggleGigantamax(uint16_t currentSpeciesId, uint8_t currentFormId, uint16_t originSpeciesId)
        {
            if (currentSpeciesId == MEOWTH_SPECIES || currentSpeciesId == PIKACHU_SPECIES)
            {
                return currentFormId == 0;
            }
            return canToggleGigantamax(currentSpeciesId) || (currentSpeciesId != originSpeciesId && canToggleGigantamax(originSpeciesId));
        }

        /**
         * Whether the encounter hands over a Pokemon that can Gigantamax (PKHeX IGigantamaxReadOnly). PKSE's
         * rows do not carry it: a wild, bred or traded Pokemon never can, an unmatched one has no template that
         * could, and the rest (raids, statics, gifts, events) stay unknown.
         */
        std::optional<bool> canEncounterGigantamax(const AnalysisContext &context)
        {
            switch (context.encounter.type)
            {
                case EncounterType::Unmatched:
                case EncounterType::Wild:
                case EncounterType::Egg:
                case EncounterType::Trade:
                    return false;
                default:
                    return std::nullopt;
            }
        }

        /// PKHeX MiscVerifierPK8.CanCalyrexFormLearn: a fused record stays flagged on Calyrex after it splits.
        bool canCalyrexFormLearn(uint8_t currentFormId, int recordIndex)
        {
            if (currentFormId != 1 && Pokemon::isTRPermitted8SWSH(CALYREX_SPECIES, 1, recordIndex))
            {
                return true;
            }
            return currentFormId != 2 && Pokemon::isTRPermitted8SWSH(CALYREX_SPECIES, 2, recordIndex);
        }

        /// PKHeX MiscVerifierPK8.VerifyTechRecordFlags, which PK8, PB8 and PA8 share: `recordCount` and `recordMoves`
        /// are the format's own permit list, while the permission is always Sword/Shield's.
        template <typename PokemonType>
        void verifyTechnicalRecordFlags(const AnalysisContext &context, const PokemonType &pokemon, int recordCount, const uint16_t *recordMoves)
        {
            const std::span<const Evolutions::EvolutionCriteria> stages = context.evolutionHistory().getChain(Enums::GameVersion::SWSH);
            const int stageCount = static_cast<int>(stages.size());
            for (int recordIndex = 0; recordIndex < recordCount; ++recordIndex)
            {
                if (!pokemon.isTRFlagSet(recordIndex))
                {
                    continue;
                }
                if (stageCount != 0)
                {
                    if (Pokemon::isTRPermitted8SWSH(stages[0].speciesId, stages[0].formId, recordIndex))
                    {
                        continue;
                    }
                    if (pokemon.speciesID() == CALYREX_SPECIES && canCalyrexFormLearn(pokemon.form(), recordIndex))
                    {
                        continue;
                    }
                }
                context.add(Severity::Invalid, CheckIdentifier::Misc, ResultCode::MoveTechRecordFlagMissing, Names::getMoveName(recordMoves[recordIndex]));
            }
        }

        /// What PB8 and PA8 share after their own checks: no Sword/Shield-only state may be present.
        template <typename PokemonType>
        void verifyNoSwordShieldState(const AnalysisContext &context, const PokemonType &pokemon)
        {
            if (!isBattleVersionValid(context, pokemon.battleVersion()))
            {
                context.add(Severity::Invalid, CheckIdentifier::Misc, ResultCode::StatBattleVersionInvalid);
            }
            if (pokemon.canGigantamax())
            {
                context.add(Severity::Invalid, CheckIdentifier::Misc, ResultCode::StatGigantamaxInvalid);
            }
            if (pokemon.dynamaxLevel() != 0)
            {
                context.add(Severity::Invalid, CheckIdentifier::Misc, ResultCode::StatDynamaxInvalid);
            }
            if (pokemon.hasAnyTRFlag() && !pokemon.isEgg())
            {
                context.add(Severity::Invalid, CheckIdentifier::Misc, ResultCode::EggRelearnFlags);
            }
        }

        uint32_t getFloatBits(float floatValue)
        {
            uint32_t floatBits = 0;
            std::memcpy(&floatBits, &floatValue, sizeof floatBits);
            return floatBits;
        }

        /// PKHeX PA8.GetScalarPercent: 80% to 120%, the multiply and the add rounded separately.
        float getScalarPercent(uint8_t sizeScalar)
        {
            const volatile float scaledPercent = (sizeScalar / 255.0f) * 0.40000004f;
            return scaledPercent + 0.8f;
        }

        /// PKHeX PA8.GetScalarPercentFused: HOME 4.0.0 onward fuses the multiply into the add.
        float getScalarPercentFused(uint8_t sizeScalar)
        {
            return std::fmaf(sizeScalar / 255.0f, 0.40000004f, 0.8f);
        }

        struct AbsoluteSize
        {
            float absoluteHeight = 0.0f;
            float absoluteWeight = 0.0f;
        };

        /// PKHeX PA8.GetHeightAbsolute / GetWeightAbsolute, or their HOME 4.0.0 fused forms.
        bool getAbsoluteSize(uint16_t speciesId, uint8_t formId, uint8_t heightScalar, uint8_t weightScalar, bool isFused, AbsoluteSize &absoluteSize)
        {
            uint16_t baseHeight = 0;
            uint16_t baseWeight = 0;
            if (!Pokemon::getBaseSize8LA(speciesId, formId, baseHeight, baseWeight))
            {
                return false;
            }
            const float heightPercent = isFused ? getScalarPercentFused(heightScalar) : getScalarPercent(heightScalar);
            const float weightPercent = isFused ? getScalarPercentFused(weightScalar) : getScalarPercent(weightScalar);
            absoluteSize.absoluteHeight = baseHeight * heightPercent;
            const volatile float sizePercent = heightPercent * weightPercent;
            absoluteSize.absoluteWeight = baseWeight * sizePercent;
            return true;
        }

        bool isAbsoluteSizeMatch(const Pokemon::Pokemon8LA &pokemon, uint16_t speciesId, uint8_t formId, uint8_t heightScalar, uint8_t weightScalar, bool isFused)
        {
            AbsoluteSize absoluteSize;
            if (!getAbsoluteSize(speciesId, formId, heightScalar, weightScalar, isFused, absoluteSize))
            {
                return false;
            }
            return pokemon.heightAbsolute() == absoluteSize.absoluteHeight && pokemon.weightAbsolute() == absoluteSize.absoluteWeight;
        }

        /// PKHeX MiscVerifierPA8.IsAbsoluteRecalculated: anything Legends: Arceus did to the Pokemon recomputes its sizes.
        bool isAbsoluteRecalculated(const AnalysisContext &context, const Pokemon::Pokemon8LA &pokemon)
        {
            if (!isStoredSlot(context, StorageSlotType::Box))
            {
                return true;
            }
            const bool hasUsedGrit = pokemon.gritHP() != 0 || pokemon.gritATK() != 0 || pokemon.gritDEF() != 0 || pokemon.gritSPE() != 0 || pokemon.gritSPA() != 0 || pokemon.gritSPD() != 0;
            return pokemon.hasAnyMoveShopPurchase() || pokemon.hasHisuiRibbon() || hasUsedGrit || pokemon.statusCondition() != 0;
        }

        /// PKHeX MiscVerifierPA8.IsAbsoluteEdgeCase.
        bool isAbsoluteEdgeCase(const AnalysisContext &context, const Pokemon::Pokemon8LA &pokemon)
        {
            const uint16_t speciesId = pokemon.speciesID();
            const uint8_t formId = pokemon.form();
            // HOME 3.0.1 raised the 127-scalar static Alphas to 255 without recomputing the absolutes. PKSE's rows
            // do not mark those statics (EncounterStatic8a.IsAlpha127), so any Alpha encounter may match them.
            const std::optional<bool> encounterAlpha = isEncounterAlpha(context);
            if (encounterAlpha.value_or(true) && context.encounter.type != EncounterType::Wild && isAbsoluteSizeMatch(pokemon, speciesId, formId, 127, 127, false))
            {
                return true;
            }
            if (context.encounter.type == EncounterType::Event && speciesId == ENAMORUS_SPECIES)
            {
                return !isAbsoluteRecalculated(context, pokemon) && pokemon.heightAbsolute() == ENAMORUS_GIFT_HEIGHT && pokemon.weightAbsolute() == ENAMORUS_GIFT_WEIGHT;
            }
            const uint8_t heightScalar = pokemon.heightScalar();
            const uint8_t weightScalar = pokemon.weightScalar();
            if (getOriginContextGroup(context) != Enums::GameVersion::PLA && !isAbsoluteRecalculated(context, pokemon) && isAbsoluteSizeMatch(pokemon, speciesId, formId, heightScalar, weightScalar, true))
            {
                return true;
            }
            if (pokemon.homeTracker() == 0 || !isStoredSlot(context, StorageSlotType::Box))
            {
                return false;
            }
            const std::span<const Evolutions::EvolutionCriteria> stages = context.evolutionHistory().getChain(Enums::GameVersion::PLA);
            const int stageCount = static_cast<int>(stages.size());
            const bool isFromLegendsArceus = pokemon.originGame() == LEGENDS_ARCEUS_VERSION;
            for (int stageIndex = 1; stageIndex < stageCount; ++stageIndex)
            {
                const Evolutions::EvolutionCriteria &stage = stages[stageIndex];
                if (isAbsoluteSizeMatch(pokemon, stage.speciesId, stage.formId, heightScalar, weightScalar, false))
                {
                    return true;
                }
                if (!isFromLegendsArceus && isAbsoluteSizeMatch(pokemon, stage.speciesId, stage.formId, heightScalar, weightScalar, true))
                {
                    return true;
                }
            }
            return false;
        }

        /// PKHeX MiscVerifierPA8.VerifyAbsoluteSizes.
        void verifyAbsoluteSizes(const AnalysisContext &context, const Pokemon::Pokemon8LA &pokemon)
        {
            AbsoluteSize expectedSize;
            if (!getAbsoluteSize(pokemon.speciesID(), pokemon.form(), pokemon.heightScalar(), pokemon.weightScalar(), false, expectedSize))
            {
                return;
            }
            if (pokemon.heightAbsolute() == expectedSize.absoluteHeight && pokemon.weightAbsolute() == expectedSize.absoluteWeight)
            {
                return;
            }
            if (isAbsoluteEdgeCase(context, pokemon))
            {
                return;
            }
            if (pokemon.heightAbsolute() != expectedSize.absoluteHeight)
            {
                context.add(Severity::Invalid, CheckIdentifier::Encounter, ResultCode::StatIncorrectHeight, std::to_string(getFloatBits(expectedSize.absoluteHeight)));
            }
            if (pokemon.weightAbsolute() != expectedSize.absoluteWeight)
            {
                context.add(Severity::Invalid, CheckIdentifier::Encounter, ResultCode::StatIncorrectWeight, std::to_string(getFloatBits(expectedSize.absoluteWeight)));
            }
        }
    }

    void verifyMisc8SWSH(const AnalysisContext &context)
    {
        const auto &pokemon = static_cast<const Pokemon::Pokemon8SWSH &>(context.pokemon);
        verifyStatAlignment(context);
        verifyTechnicalRecordFlags(context, pokemon, Pokemon::TR_COUNT_8SWSH, Pokemon::TR_MOVES_8SWSH);
        verifyFullness(context, pokemon.fullness(), pokemon.enjoyment());

        const uint32_t sociability = pokemon.sociability();
        if (pokemon.isEgg())
        {
            if (sociability != 0)
            {
                context.add(Severity::Invalid, CheckIdentifier::Encounter, ResultCode::MemorySocialZero);
            }
        }
        else if (sociability > SOCIABILITY_MAXIMUM)
        {
            context.add(Severity::Invalid, CheckIdentifier::Encounter, ResultCode::MemoryStatSocialAtMost, "0");
        }

        if (!isBattleVersionValid(context, pokemon.battleVersion()))
        {
            context.add(Severity::Invalid, CheckIdentifier::Misc, ResultCode::StatBattleVersionInvalid);
        }

        const std::optional<bool> encounterGigantamax = canEncounterGigantamax(context);
        if (encounterGigantamax.has_value() && *encounterGigantamax != pokemon.canGigantamax())
        {
            const Encounters::Encounterable *encounterMatch = Encounters::getEncounterMatch(context);
            const uint16_t encounterSpeciesId = encounterMatch != nullptr ? encounterMatch->speciesId() : pokemon.speciesID();
            if (pokemon.isEgg() || !canToggleGigantamax(pokemon.speciesID(), pokemon.form(), encounterSpeciesId))
            {
                context.add(Severity::Invalid, CheckIdentifier::Misc, ResultCode::StatGigantamaxInvalid);
            }
        }

        const uint8_t dynamaxLevel = pokemon.dynamaxLevel();
        if (dynamaxLevel != 0)
        {
            const uint16_t speciesId = pokemon.speciesID();
            const bool canHaveDynamaxLevel = !pokemon.isEgg() && speciesId != ZACIAN_SPECIES && speciesId != ZAMAZENTA_SPECIES && speciesId != ETERNATUS_SPECIES;
            if (!canHaveDynamaxLevel || dynamaxLevel > DYNAMAX_LEVEL_MAXIMUM)
            {
                context.add(Severity::Invalid, CheckIdentifier::Misc, ResultCode::StatDynamaxInvalid);
            }
        }
    }

    void verifyMisc8BDSP(const AnalysisContext &context)
    {
        const auto &pokemon = static_cast<const Pokemon::Pokemon8BDSP &>(context.pokemon);
        verifyStatAlignment(context);
        verifyTechnicalRecordFlags(context, pokemon, Pokemon::TR_COUNT_8SWSH, Pokemon::TR_MOVES_8SWSH);
        verifyFullness(context, pokemon.fullness(), pokemon.enjoyment());

        if (pokemon.sociability() != 0)
        {
            context.add(Severity::Invalid, CheckIdentifier::Encounter, ResultCode::MemorySocialZero);
        }
        if (pokemon.hasBDSPIllegalFlag())
        {
            context.add(Severity::Invalid, CheckIdentifier::Misc, ResultCode::TransferFlagIllegal);
        }
        const uint16_t speciesId = pokemon.speciesID();
        const bool isFromBDSP = pokemon.originGame() == BRILLIANT_DIAMOND_VERSION || pokemon.originGame() == SHINING_PEARL_VERSION;
        if ((speciesId == SPINDA_SPECIES || speciesId == NINCADA_SPECIES) && !isFromBDSP)
        {
            context.add(Severity::Invalid, CheckIdentifier::Misc, ResultCode::TransferNotPossible);
        }
        if (speciesId == SPINDA_SPECIES && pokemon.homeTracker() != 0)
        {
            context.add(Severity::Invalid, CheckIdentifier::Misc, ResultCode::TransferTrackerShouldBeZero);
        }
        verifyNoSwordShieldState(context, pokemon);
    }

    void verifyMisc8LA(const AnalysisContext &context)
    {
        const auto &pokemon = static_cast<const Pokemon::Pokemon8LA &>(context.pokemon);
        verifyLegendsArceus(context);
        verifyAbsoluteSizes(context, pokemon);
        verifyStatAlignment(context);
        verifyTechnicalRecordFlags(context, pokemon, Pokemon::MOVE_SHOP_COUNT_8LA, Pokemon::MOVE_SHOP_MOVES_8LA);
        verifyFullness(context, pokemon.fullness(), pokemon.enjoyment());

        if (pokemon.sociability() != 0)
        {
            context.add(Severity::Invalid, CheckIdentifier::Encounter, ResultCode::MemorySocialZero);
        }
        verifyNoSwordShieldState(context, pokemon);
    }
}
