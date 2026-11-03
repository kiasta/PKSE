/**
 * A Legends: Arceus static encounter or gift: the starters, the legendaries, the Unown notes and the
 * scripted Alphas. Ported from PKHeX PKHeX.Core/Legality/Encounters/Templates/Gen8a/EncounterStatic8a.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERSTATIC8A_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERSTATIC8A_H

#include <cstdint>

#include "Legality/Encounters/EncounterRules9a.h"
#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterStatic8a final : public Encounterable, public EncounterMatchTemplate, public MovesetTemplate
        {
        public:
            /// PKHeX's NoScalar: no fixed height or weight.
            static constexpr uint8_t NO_SCALAR = 0;

            /// PKHeX's constructor arguments and init properties, with PKHeX's defaults.
            struct Properties
            {
                uint16_t speciesId = 0;
                uint8_t formId = 0;
                uint8_t level = 0;
                uint8_t heightScalar = NO_SCALAR;
                uint8_t weightScalar = NO_SCALAR;
                /// PKHeX LevelMax: the level unless the template gives a range.
                uint8_t levelMax = 0;
                uint8_t gender = GENDER_RANDOM;
                uint16_t location = 0;
                uint8_t flawlessIVCount = 0;
                RNG::Shiny shiny = RNG::Shiny::Never;
                bool isAlpha = false;
                bool isFatefulEncounter = false;
                Enums::Ball fixedBall = Enums::Ball::None;
                Moveset moves{};
            };

            explicit constexpr EncounterStatic8a(const Properties &properties) noexcept : properties(properties) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Static8a; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Static8a; }
            EncounterType encounterType() const noexcept override { return properties.fixedBall != Enums::Ball::None ? EncounterType::Gift : EncounterType::Static; }
            std::string name() const override { return "Static Encounter"; }

            uint16_t speciesId() const noexcept override { return properties.speciesId; }
            uint8_t formId() const noexcept override { return properties.formId; }
            uint8_t levelMin() const noexcept override { return properties.level; }
            uint8_t levelMax() const noexcept override { return properties.levelMax != 0 ? properties.levelMax : properties.level; }
            Enums::GameVersion version() const noexcept override { return Enums::GameVersion::PLA; }
            uint8_t generation() const noexcept override { return 8; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::PLA; }
            uint16_t location() const noexcept override { return properties.location; }
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return false; }
            Enums::Ball fixedBall() const noexcept override { return properties.fixedBall; }
            RNG::Shiny shiny() const noexcept override { return properties.shiny; }
            uint8_t fixedGender() const noexcept override { return properties.gender; }
            uint8_t flawlessIVCount() const noexcept override { return properties.flawlessIVCount; }
            bool isFatefulEncounter() const noexcept override { return properties.isFatefulEncounter; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &pokemon) const override;
            Moveset moves() const noexcept override { return properties.moves; }

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            const MovesetTemplate *asMoveset() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterStatic8a>(*this); }

            bool isAlpha() const noexcept { return properties.isAlpha; }
            bool hasFixedHeight() const noexcept { return properties.heightScalar != NO_SCALAR; }
            bool hasFixedWeight() const noexcept { return properties.weightScalar != NO_SCALAR; }
            /// PKHeX IsAlpha127: the three Alphas mistakenly given average sizes, which HOME 3.0.1 raises to 255.
            bool isAlpha127() const noexcept { return properties.isAlpha && properties.heightScalar == 127 && properties.weightScalar == 127; }
            /// PKHeX ISeedCorrelation64.TryGetSeed: a single-roll spawn seed reproduces the record.
            SeedCorrelationResult tryGetSeed(const Pokemon::Pokemon &pokemon, uint64_t &seed) const noexcept;
            /// PKHeX IMasteryInitialMoveShop8.IsForcedMasteryCorrect: the move-shop flags the encounter's
            /// initial moves were generated with, and a scripted Alpha's move.
            bool isForcedMasteryCorrect(const Pokemon::Pokemon &pokemon) const noexcept;

        private:
            bool isMatchLocation(const Pokemon::Pokemon &pokemon) const;
            bool isMatchSize(const Pokemon::Pokemon &pokemon) const;

            Properties properties;
        };
    }
}

#endif
