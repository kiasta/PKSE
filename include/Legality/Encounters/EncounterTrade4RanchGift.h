/**
 * A My Pokemon Ranch gift to Diamond, Pearl or Platinum: Hayley's trades and the Mew and Phione
 * gifts. Ported from PKHeX PKHeX.Core/Legality/Encounters/Templates/Gen4/EncounterTrade4RanchGift.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERTRADE4RANCHGIFT_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERTRADE4RANCHGIFT_H

#include <cstdint>

#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterTrade4RanchGift final : public Encounterable, public EncounterMatchTemplate, public FixedTrainerTemplate, public MovesetTemplate
        {
        public:
            /// PKHeX's constructor arguments and init properties. The PID-less constructor (Mew,
            /// Phione) is the one that sets isFatefulEncounter.
            struct Properties
            {
                uint32_t PID = 0;
                uint16_t speciesId = 0;
                uint8_t metLevel = 0;
                uint8_t level = 0;
                bool isFatefulEncounter = false;
                uint16_t location = 0;
                uint16_t eggLocation = 0;
                Enums::Ball fixedBall = Enums::Ball::Poke;
                Moveset moves{};
                uint16_t SID16 = 0;
                uint8_t trainerGender = 0;
                uint8_t gender = 0;
                RNG::AbilityPermission ability = RNG::AbilityPermission::Any12;
                uint8_t formId = 0;
            };

            /// Hayley's trainer id.
            static constexpr uint16_t RANCH_TID16 = 1000;

            explicit constexpr EncounterTrade4RanchGift(const Properties &properties) noexcept : properties(properties) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Trade4RanchGift; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Trade4RanchGift; }
            EncounterType encounterType() const noexcept override { return EncounterType::Trade; }
            std::string name() const override { return "My Pokémon Ranch - Trade"; }

            uint16_t speciesId() const noexcept override { return properties.speciesId; }
            uint8_t formId() const noexcept override { return properties.formId; }
            uint8_t levelMin() const noexcept override { return properties.level; }
            uint8_t levelMax() const noexcept override { return properties.level; }
            Enums::GameVersion version() const noexcept override { return Enums::GameVersion::D; }
            uint8_t generation() const noexcept override { return 4; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::DP; }
            uint16_t location() const noexcept override { return properties.location; }
            uint16_t eggLocation() const noexcept override { return properties.eggLocation; }
            bool isEgg() const noexcept override { return false; }
            Enums::Ball fixedBall() const noexcept override { return properties.fixedBall; }
            RNG::Shiny shiny() const noexcept override { return properties.isFatefulEncounter ? RNG::Shiny::Never : RNG::Shiny::FixedValue; }
            RNG::AbilityPermission ability() const noexcept override { return properties.ability; }
            uint8_t fixedGender() const noexcept override { return properties.gender; }
            uint8_t fixedNature() const noexcept override { return properties.isFatefulEncounter ? NATURE_RANDOM : static_cast<uint8_t>(properties.PID % 25); }
            bool isFatefulEncounter() const noexcept override { return properties.isFatefulEncounter; }
            bool hasMetLevel() const noexcept override { return true; }
            uint8_t metLevel() const noexcept override { return properties.metLevel; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &) const override { return EncounterMatchRating::Match; }
            bool isFixedTrainer() const noexcept override { return true; }
            bool isTrainerMatch(const Pokemon::Pokemon &pokemon, const std::u16string &trainerName, int language) const override;
            Moveset moves() const noexcept override { return properties.moves; }

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            const FixedTrainerTemplate *asFixedTrainer() const noexcept override { return this; }
            const MovesetTemplate *asMoveset() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterTrade4RanchGift>(*this); }

            uint32_t PID() const noexcept { return properties.PID; }
            uint16_t TID16() const noexcept { return RANCH_TID16; }
            uint16_t SID16() const noexcept { return properties.SID16; }
            uint32_t ID32() const noexcept { return static_cast<uint32_t>(RANCH_TID16) | (static_cast<uint32_t>(properties.SID16) << 16); }
            uint8_t trainerGender() const noexcept { return properties.trainerGender; }

            /// PKHeX GetTrainerName: Hayley's name in `language`, empty for one without a Ranch.
            static std::u16string getTrainerName(int language);

        private:
            Properties properties;
        };
    }
}

#endif
