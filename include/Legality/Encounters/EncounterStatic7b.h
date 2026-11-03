/**
 * A Let's Go static encounter: the legendaries, Snorlax and the gifts. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen7b/EncounterStatic7b.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERSTATIC7B_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERSTATIC7B_H

#include <cstdint>

#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterStatic7b final : public Encounterable, public EncounterMatchTemplate
        {
        public:
            /// PKHeX's init properties, with PKHeX's defaults.
            struct Properties
            {
                Enums::GameVersion version = Enums::GameVersion::GG;
                uint16_t speciesId = 0;
                uint8_t formId = 0;
                uint8_t level = 0;
                uint16_t location = 0;
                RNG::IndividualValueSet individualValues{};
                uint8_t flawlessIVCount = 0;
                RNG::Shiny shiny = RNG::Shiny::Random;
                Enums::Ball fixedBall = Enums::Ball::None;
            };

            explicit constexpr EncounterStatic7b(const Properties &properties) noexcept : properties(properties) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Static7b; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Static7b; }
            EncounterType encounterType() const noexcept override { return properties.fixedBall != Enums::Ball::None ? EncounterType::Gift : EncounterType::Static; }
            std::string name() const override { return "Static Encounter"; }

            uint16_t speciesId() const noexcept override { return properties.speciesId; }
            uint8_t formId() const noexcept override { return properties.formId; }
            uint8_t levelMin() const noexcept override { return properties.level; }
            uint8_t levelMax() const noexcept override { return properties.level; }
            Enums::GameVersion version() const noexcept override { return properties.version; }
            uint8_t generation() const noexcept override { return 7; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::GG; }
            uint16_t location() const noexcept override { return properties.location; }
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return false; }
            Enums::Ball fixedBall() const noexcept override { return properties.fixedBall; }
            RNG::Shiny shiny() const noexcept override { return properties.shiny; }
            RNG::IndividualValueSet fixedIVs() const noexcept override { return properties.individualValues; }
            uint8_t flawlessIVCount() const noexcept override { return properties.flawlessIVCount; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &) const override { return EncounterMatchRating::Match; }

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterStatic7b>(*this); }

        private:
            Properties properties;
        };
    }
}

#endif
