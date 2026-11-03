/**
 * A Gen 3 gift from the Pokemon Center New York distribution machines (PCNY). Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen3/Gifts/EncounterGift3NY.cs and Distribution3NY.cs.
 *
 * PKSE has no GameVersion for PKHeX's RS lump; the template reports RSE, and the Ruby/Sapphire rule
 * is checked on the stored version.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERGIFT3NY_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERGIFT3NY_H

#include <cstdint>

#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        /// PKHeX Distribution3NY: which PCNY distribution the gift came from.
        enum class Distribution3NY : uint8_t
        {
            Evolution,
            Dragon,
            Monster,
            Halloween,
            EXDragon,
            UnknownSpring,
            Colosseum,
            Box,
            BabyTrade,
            SlitherSwim,
            AncientAliens,
            Sixth,
        };

        class EncounterGift3NY final : public Encounterable, public EncounterMatchTemplate, public MovesetTemplate, public RandomCorrelationTemplate, public FixedTrainerTemplate
        {
        public:
            struct Properties
            {
                uint16_t speciesId = 0;
                Distribution3NY distribution = Distribution3NY::Evolution;
                uint8_t level = 0;
                Moveset moves{};
            };

            /// PKHeX's Location: every PCNY gift is met at the event location.
            static constexpr uint16_t LOCATION = 255;

            explicit constexpr EncounterGift3NY(const Properties &properties) noexcept : properties(properties) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Gift3NY; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Gift3NY; }
            EncounterType encounterType() const noexcept override { return EncounterType::Event; }
            std::string name() const override { return "PCNY Gift"; }

            uint16_t speciesId() const noexcept override { return properties.speciesId; }
            uint8_t formId() const noexcept override { return 0; }
            uint8_t levelMin() const noexcept override { return properties.level; }
            uint8_t levelMax() const noexcept override { return properties.level; }
            Enums::GameVersion version() const noexcept override { return Enums::GameVersion::RSE; }
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
            Moveset moves() const noexcept override { return properties.moves; }
            RandomCorrelationRating isCompatible(RNG::PIDType type, const Pokemon::Pokemon &) const override { return type == RNG::PIDType::BACDUnrestrictedAntishinyXor ? RandomCorrelationRating::Match : RandomCorrelationRating::Mismatch; }
            RNG::PIDType getSuggestedCorrelation() const noexcept override { return RNG::PIDType::BACDUnrestrictedAntishinyXor; }
            bool hasReviseReset() const noexcept override { return true; }
            RandomCorrelationRating isCompatibleReviseReset(RNG::PIDIV &pidiv, const Pokemon::Pokemon &pokemon) const override;
            bool isFixedTrainer() const noexcept override { return true; }
            /// Checked in isMatchExact.
            bool isTrainerMatch(const Pokemon::Pokemon &, const std::u16string &, int) const override { return true; }

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            const MovesetTemplate *asMoveset() const noexcept override { return this; }
            const RandomCorrelationTemplate *asRandomCorrelation() const noexcept override { return this; }
            const FixedTrainerTemplate *asFixedTrainer() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterGift3NY>(*this); }

        private:
            Properties properties;
        };
    }
}

#endif
