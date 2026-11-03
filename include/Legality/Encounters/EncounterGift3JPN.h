/**
 * A Gen 3 gift from the Japanese Pokemon Center distribution machines (PCJP). Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen3/Gifts/EncounterGift3JPN.cs and Distribution3JPN.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERGIFT3JPN_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERGIFT3JPN_H

#include <cstdint>

#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        /// PKHeX Distribution3JPN: which PCJP distribution the gift came from.
        enum class Distribution3JPN : uint8_t
        {
            First,
            Second,
            Third,
            Fourth,
            Fifth,
            Sixth,
        };

        class EncounterGift3JPN final : public Encounterable, public EncounterMatchTemplate, public RandomCorrelationTemplate, public FixedTrainerTemplate
        {
        public:
            struct Properties
            {
                uint16_t speciesId = 0;
                Distribution3JPN distribution = Distribution3JPN::First;
            };

            /// PKHeX's Level and Location: every PCJP gift is a level 10 met at the event location.
            static constexpr uint8_t LEVEL = 10;
            static constexpr uint16_t LOCATION = 255;

            explicit constexpr EncounterGift3JPN(const Properties &properties) noexcept : properties(properties) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Gift3JPN; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Gift3JPN; }
            EncounterType encounterType() const noexcept override { return EncounterType::Event; }
            std::string name() const override { return "PCJP Gift"; }

            uint16_t speciesId() const noexcept override { return properties.speciesId; }
            uint8_t formId() const noexcept override { return 0; }
            uint8_t levelMin() const noexcept override { return LEVEL; }
            uint8_t levelMax() const noexcept override { return LEVEL; }
            Enums::GameVersion version() const noexcept override { return Enums::GameVersion::RU; }
            uint8_t generation() const noexcept override { return 3; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::RSE; }
            uint16_t location() const noexcept override { return LOCATION; }
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return false; }
            Enums::Ball fixedBall() const noexcept override { return Enums::Ball::Poke; }
            RNG::Shiny shiny() const noexcept override { return RNG::Shiny::Never; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            /// Everything is checked in isMatchExact.
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &) const override { return EncounterMatchRating::Match; }
            RandomCorrelationRating isCompatible(RNG::PIDType type, const Pokemon::Pokemon &) const override { return type == RNG::PIDType::BACDRestrictedAntishiny ? RandomCorrelationRating::Match : RandomCorrelationRating::Mismatch; }
            RNG::PIDType getSuggestedCorrelation() const noexcept override { return RNG::PIDType::BACDRestrictedAntishiny; }
            bool hasReviseReset() const noexcept override { return true; }
            RandomCorrelationRating isCompatibleReviseReset(RNG::PIDIV &pidiv, const Pokemon::Pokemon &pokemon) const override;
            bool isFixedTrainer() const noexcept override { return true; }
            /// Checked in isMatchExact.
            bool isTrainerMatch(const Pokemon::Pokemon &, const std::u16string &, int) const override { return true; }

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            const RandomCorrelationTemplate *asRandomCorrelation() const noexcept override { return this; }
            const FixedTrainerTemplate *asFixedTrainer() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterGift3JPN>(*this); }

        private:
            Properties properties;
        };
    }
}

#endif
