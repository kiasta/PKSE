/**
 * A Legends: Z-A static encounter: legendaries, Rogue Mega Pokemon and side-mission battles. Ported
 * from PKHeX PKHeX.Core/Legality/Encounters/Templates/Gen9a/EncounterStatic9a.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERSTATIC9A_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERSTATIC9A_H

#include <cstdint>

#include "Legality/Encounters/EncounterRules9a.h"
#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterStatic9a final : public Encounterable, public EncounterMatchTemplate, public MovesetTemplate
        {
        public:
            /// PKHeX's constructor arguments and init properties, with PKHeX's defaults.
            struct Properties
            {
                uint16_t speciesId = 0;
                uint8_t formId = 0;
                uint8_t level = 0;
                /// The fixed scale; 0 when the scale is random.
                uint8_t scale = 0;
                uint16_t location = 0;
                uint8_t gender = GENDER_RANDOM;
                uint8_t nature = NATURE_RANDOM;
                uint8_t flawlessIVCount = 0;
                RNG::IndividualValueSet individualValues{};
                Moveset moves{};
                RNG::Shiny shiny = RNG::Shiny::Never;
                bool isAlpha = false;
            };

            explicit constexpr EncounterStatic9a(const Properties &properties) noexcept : properties(properties) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Static9a; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Static9a; }
            EncounterType encounterType() const noexcept override { return EncounterType::Static; }
            std::string name() const override { return "Static Encounter"; }

            uint16_t speciesId() const noexcept override { return properties.speciesId; }
            uint8_t formId() const noexcept override { return properties.formId; }
            uint8_t levelMin() const noexcept override { return properties.level; }
            uint8_t levelMax() const noexcept override { return properties.level; }
            Enums::GameVersion version() const noexcept override { return Enums::GameVersion::ZA; }
            uint8_t generation() const noexcept override { return 9; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::ZA; }
            uint16_t location() const noexcept override { return properties.location; }
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return false; }
            RNG::Shiny shiny() const noexcept override { return properties.shiny; }
            uint8_t fixedGender() const noexcept override { return properties.gender; }
            uint8_t fixedNature() const noexcept override { return properties.nature; }
            RNG::IndividualValueSet fixedIVs() const noexcept override { return properties.individualValues; }
            uint8_t flawlessIVCount() const noexcept override { return properties.flawlessIVCount; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &pokemon) const override;
            Moveset moves() const noexcept override { return properties.moves; }

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            const MovesetTemplate *asMoveset() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterStatic9a>(*this); }

            bool isAlpha() const noexcept { return properties.isAlpha; }
            /// PKHeX TryGetSeed.
            SeedCorrelationResult tryGetSeed(const Pokemon::Pokemon &pokemon) const noexcept;
            /// PKHeX GetParams(pi, shinyCharm, activeShinyPower).
            RNG::GenerateParam9a getParameters(bool hasShinyCharm, bool hasShinyPower) const noexcept;

        private:
            /// PKHeX IsHyperspaceShinyPossible: a non-locked Hyperspace legend can get the boosted rolls.
            bool isHyperspaceShinyPossible() const noexcept { return properties.shiny != RNG::Shiny::Never && properties.location == LOCATION_HYPERSPACE9A; }

            Properties properties;
        };
    }
}

#endif
