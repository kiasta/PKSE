/**
 * A Sword/Shield static encounter or gift. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen8/EncounterStatic8.cs and Crossover8.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERSTATIC8_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERSTATIC8_H

#include <array>
#include <cstdint>

#include "Legality/Encounters/EncounterRules8.h"
#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterStatic8 final : public Encounterable, public EncounterMatchTemplate, public MovesetTemplate, public RelearnTemplate
        {
        public:
            /// PKHeX's init properties, with PKHeX's defaults.
            struct Properties
            {
                Enums::GameVersion version = Enums::GameVersion::SWSH;
                uint16_t location = 0;
                uint16_t speciesId = 0;
                uint8_t formId = 0;
                uint8_t level = 0;
                Moveset moves{};
                Moveset relearnMoves{};
                RNG::IndividualValueSet individualValues{};
                /// PKHeX Crossover8: the other areas an overworld spawn can be met in (0 ends the list).
                std::array<uint8_t, 7> crossover{};
                uint16_t weather = WEATHER8_NORMAL;
                uint8_t dynamaxLevel = 0;
                uint8_t nature = NATURE_RANDOM;
                RNG::Shiny shiny = RNG::Shiny::Random;
                RNG::AbilityPermission ability = RNG::AbilityPermission::Any12;
                uint8_t gender = GENDER_RANDOM;
                Enums::Ball fixedBall = Enums::Ball::None;
                uint8_t flawlessIVCount = 0;
                bool isScriptedNoMarks = false;
                bool canGigantamax = false;
                bool isFatefulEncounter = false;
            };

            explicit constexpr EncounterStatic8(const Properties &properties) noexcept : properties(properties) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Static8; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Static8; }
            EncounterType encounterType() const noexcept override { return isGift() ? EncounterType::Gift : EncounterType::Static; }
            std::string name() const override { return "Static Encounter"; }

            uint16_t speciesId() const noexcept override { return properties.speciesId; }
            uint8_t formId() const noexcept override { return properties.formId; }
            uint8_t levelMin() const noexcept override { return properties.level; }
            uint8_t levelMax() const noexcept override { return properties.level; }
            Enums::GameVersion version() const noexcept override { return properties.version; }
            uint8_t generation() const noexcept override { return 8; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::SWSH; }
            uint16_t location() const noexcept override { return properties.location; }
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return false; }
            Enums::Ball fixedBall() const noexcept override { return properties.fixedBall; }
            RNG::Shiny shiny() const noexcept override { return properties.shiny; }
            RNG::AbilityPermission ability() const noexcept override { return properties.ability; }
            uint8_t fixedGender() const noexcept override { return properties.gender; }
            uint8_t fixedNature() const noexcept override { return properties.nature; }
            RNG::IndividualValueSet fixedIVs() const noexcept override { return properties.individualValues; }
            uint8_t flawlessIVCount() const noexcept override { return properties.flawlessIVCount; }
            bool isFatefulEncounter() const noexcept override { return properties.isFatefulEncounter; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &pokemon) const override;
            Moveset moves() const noexcept override { return properties.moves; }
            Moveset relearnMoves() const noexcept override { return properties.relearnMoves; }

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            const MovesetTemplate *asMoveset() const noexcept override { return this; }
            const RelearnTemplate *asRelearn() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterStatic8>(*this); }

            bool isGift() const noexcept { return properties.fixedBall != Enums::Ball::None; }
            uint16_t weather() const noexcept { return properties.weather; }
            uint8_t dynamaxLevel() const noexcept { return properties.dynamaxLevel; }
            bool canGigantamax() const noexcept { return properties.canGigantamax; }
            bool isScriptedNoMarks() const noexcept { return properties.isScriptedNoMarks; }
            /// PKHeX IsOverworldCorrelation: a saved overworld spawn, not a gift or a scripted battle.
            bool isOverworldCorrelation() const noexcept { return !isGift() && !properties.isScriptedNoMarks; }
            /// PKHeX IOverworldCorrelation8.
            OverworldCorrelation8Requirement getRequirement() const noexcept { return isOverworldCorrelation() ? OverworldCorrelation8Requirement::MustHave : OverworldCorrelation8Requirement::MustNotHave; }
            bool isOverworldCorrelationCorrect(const Pokemon::Pokemon &pokemon) const;

        private:
            bool isMatchLocation(const Pokemon::Pokemon &pokemon) const noexcept;
            bool isMatchLevel(const Pokemon::Pokemon &pokemon) const noexcept;

            Properties properties;
        };
    }
}

#endif
