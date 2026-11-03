/**
 * An X/Y or Omega Ruby/Alpha Sapphire static encounter: gifts, legendaries, the eggs and the Cosplay
 * Pikachu. Ported from PKHeX PKHeX.Core/Legality/Encounters/Templates/Gen6/EncounterStatic6.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERSTATIC6_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERSTATIC6_H

#include <array>
#include <cstdint>

#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterStatic6 final : public Encounterable, public EncounterMatchTemplate, public MovesetTemplate
        {
        public:
            /// PKHeX's init properties, with PKHeX's defaults.
            struct Properties
            {
                Enums::GameVersion version = Enums::GameVersion::XY;
                uint16_t speciesId = 0;
                uint8_t formId = 0;
                uint8_t level = 0;
                uint16_t location = 0;
                uint16_t eggLocation = 0;
                Moveset moves{};
                RNG::IndividualValueSet individualValues{};
                bool isFatefulEncounter = false;
                uint8_t flawlessIVCount = 0;
                RNG::Shiny shiny = RNG::Shiny::Random;
                RNG::AbilityPermission ability = RNG::AbilityPermission::Any12;
                uint8_t gender = GENDER_RANDOM;
                uint8_t nature = NATURE_RANDOM;
                Enums::Ball fixedBall = Enums::Ball::None;
                uint8_t eggCycles = 0;
                /// Cool, Beauty, Cute, Smart, Tough, Sheen (PKHeX's ContestSheen is always 0).
                std::array<uint8_t, 6> contestStats{};
            };

            explicit constexpr EncounterStatic6(const Properties &properties) noexcept : properties(properties) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Static6; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Static6; }
            EncounterType encounterType() const noexcept override { return properties.fixedBall != Enums::Ball::None ? EncounterType::Gift : EncounterType::Static; }
            std::string name() const override { return "Static Encounter"; }

            uint16_t speciesId() const noexcept override { return properties.speciesId; }
            uint8_t formId() const noexcept override { return properties.formId; }
            uint8_t levelMin() const noexcept override { return properties.level; }
            uint8_t levelMax() const noexcept override { return properties.level; }
            Enums::GameVersion version() const noexcept override { return properties.version; }
            uint8_t generation() const noexcept override { return 6; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::XY; }
            uint16_t location() const noexcept override { return properties.location; }
            uint16_t eggLocation() const noexcept override { return properties.eggLocation; }
            bool isEgg() const noexcept override { return properties.eggLocation != 0; }
            Enums::Ball fixedBall() const noexcept override { return properties.fixedBall; }
            RNG::Shiny shiny() const noexcept override { return properties.shiny; }
            RNG::AbilityPermission ability() const noexcept override { return properties.ability; }
            uint8_t fixedGender() const noexcept override { return properties.gender; }
            uint8_t fixedNature() const noexcept override { return properties.nature; }
            RNG::IndividualValueSet fixedIVs() const noexcept override { return properties.individualValues; }
            uint8_t flawlessIVCount() const noexcept override { return properties.flawlessIVCount; }
            bool isFatefulEncounter() const noexcept override { return properties.isFatefulEncounter; }
            uint8_t eggCycles() const noexcept override { return properties.eggCycles; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &pokemon) const override;
            Moveset moves() const noexcept override { return properties.moves; }

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            const MovesetTemplate *asMoveset() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterStatic6>(*this); }

            const std::array<uint8_t, 6> &contestStats() const noexcept { return properties.contestStats; }

        private:
            bool isMatchLocation(const Pokemon::Pokemon &pokemon) const noexcept;
            bool isMatchEggLocationInternal(const Pokemon::Pokemon &pokemon) const noexcept;

            Properties properties;
        };
    }
}

#endif
