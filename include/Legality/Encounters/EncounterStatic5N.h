/**
 * One of N's Pokemon released in Black 2/White 2, with a fixed PID, trainer and IVs. Ported from
 * PKHeX PKHeX.Core/Legality/Encounters/Templates/Gen5/EncounterStatic5N.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERSTATIC5N_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERSTATIC5N_H

#include <cstdint>

#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterStatic5N final : public Encounterable, public EncounterMatchTemplate, public FixedTrainerTemplate
        {
        public:
            struct Properties
            {
                uint32_t PID = 0;
                uint16_t speciesId = 0;
                uint8_t level = 0;
                uint8_t location = 0;
                uint8_t nature = NATURE_RANDOM;
                RNG::AbilityPermission ability = RNG::AbilityPermission::Any12;
            };

            /// PKHeX ID32: N's trainer id.
            static constexpr uint32_t ID32 = 2;
            /// PKHeX IV: every IV of N's Pokemon.
            static constexpr uint8_t INDIVIDUAL_VALUE = 30;

            explicit constexpr EncounterStatic5N(const Properties &properties) noexcept : properties(properties) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Static5N; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Static5N; }
            EncounterType encounterType() const noexcept override { return EncounterType::Static; }
            std::string name() const override { return "N's Pok\xC3\xA9mon"; }

            uint16_t speciesId() const noexcept override { return properties.speciesId; }
            uint8_t formId() const noexcept override { return 0; }
            uint8_t levelMin() const noexcept override { return properties.level; }
            uint8_t levelMax() const noexcept override { return properties.level; }
            Enums::GameVersion version() const noexcept override { return Enums::GameVersion::B2W2; }
            uint8_t generation() const noexcept override { return 5; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::BW; }
            uint16_t location() const noexcept override { return properties.location; }
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return false; }
            Enums::Ball fixedBall() const noexcept override;
            RNG::Shiny shiny() const noexcept override { return RNG::Shiny::FixedValue; }
            RNG::AbilityPermission ability() const noexcept override { return properties.ability; }
            uint8_t fixedNature() const noexcept override { return properties.nature; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &) const override { return EncounterMatchRating::Match; }
            bool isFixedTrainer() const noexcept override { return true; }
            bool isTrainerMatch(const Pokemon::Pokemon &pokemon, const std::u16string &trainerName, int language) const override;

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            const FixedTrainerTemplate *asFixedTrainer() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterStatic5N>(*this); }

            uint32_t PID() const noexcept { return properties.PID; }

        private:
            Properties properties;
        };
    }
}

#endif
