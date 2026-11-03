/**
 * A Gen 5 static encounter, gift or gift egg (Black, White, Black 2, White 2), roamers included.
 * Ported from PKHeX PKHeX.Core/Legality/Encounters/Templates/Gen5/EncounterStatic5.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERSTATIC5_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERSTATIC5_H

#include <cstdint>

#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterStatic5 final : public Encounterable, public EncounterMatchTemplate
        {
        public:
            /// PKHeX's constructor argument and init properties, with PKHeX's defaults.
            struct Properties
            {
                /// A single game, BW or B2W2.
                Enums::GameVersion version = Enums::GameVersion::Invalid;
                uint16_t speciesId = 0;
                uint8_t level = 0;
                uint8_t location = 0;
                uint8_t formId = 0;
                Enums::Ball fixedBall = Enums::Ball::None;
                RNG::AbilityPermission ability = RNG::AbilityPermission::Any12;
                RNG::Shiny shiny = RNG::Shiny::Random;
                uint16_t eggLocation = 0;
                uint8_t gender = GENDER_RANDOM;
                bool isRoaming = false;
            };

            explicit constexpr EncounterStatic5(const Properties &properties) noexcept : properties(properties) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Static5; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Static5; }
            EncounterType encounterType() const noexcept override { return isGift() ? EncounterType::Gift : EncounterType::Static; }
            std::string name() const override { return "Static Encounter"; }

            uint16_t speciesId() const noexcept override { return properties.speciesId; }
            uint8_t formId() const noexcept override { return properties.formId; }
            uint8_t levelMin() const noexcept override { return properties.level; }
            uint8_t levelMax() const noexcept override { return properties.level; }
            Enums::GameVersion version() const noexcept override { return properties.version; }
            uint8_t generation() const noexcept override { return 5; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::BW; }
            uint16_t location() const noexcept override { return properties.location; }
            uint16_t eggLocation() const noexcept override { return properties.eggLocation; }
            bool isEgg() const noexcept override { return properties.eggLocation != 0; }
            Enums::Ball fixedBall() const noexcept override { return properties.fixedBall; }
            RNG::Shiny shiny() const noexcept override { return properties.shiny; }
            RNG::AbilityPermission ability() const noexcept override { return properties.ability; }
            uint8_t fixedGender() const noexcept override { return properties.gender; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &pokemon) const override;

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterStatic5>(*this); }

            bool isRoaming() const noexcept { return properties.isRoaming; }
            bool isGift() const noexcept { return properties.fixedBall == Enums::Ball::Poke; }
            /// PKHeX IsWildCorrelationPID: the PID follows the wild ability/shiny correlation.
            bool isWildCorrelationPID() const noexcept { return !properties.isRoaming && properties.shiny == RNG::Shiny::Random && !isGift() && properties.ability != RNG::AbilityPermission::OnlyHidden; }

        private:
            bool isMatchPartial(const Pokemon::Pokemon &pokemon) const;
            bool isMatchLocation(const Pokemon::Pokemon &pokemon) const;
            bool isMatchEggLocationInternal(const Pokemon::Pokemon &pokemon) const;

            Properties properties;
        };
    }
}

#endif
