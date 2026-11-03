/**
 * A gift from Pokemon Colosseum received in Ruby or Sapphire (the Japanese bonus disc's Pikachu
 * and Celebi, and Mt. Battle's Ho-Oh). Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen3/Colo/EncounterGift3Colo.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERGIFT3COLO_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERGIFT3COLO_H

#include <cstdint>

#include "Legality/Encounters/EncounterTrade3.h"
#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterGift3Colo final : public Encounterable, public EncounterMatchTemplate, public RandomCorrelationTemplate, public FixedTrainerTemplate, public MovesetTemplate
        {
        public:
            struct Properties
            {
                uint16_t speciesId = 0;
                uint8_t level = 0;
                const LanguageNames *trainerNames = nullptr;
                Enums::GameVersion version = Enums::GameVersion::Invalid;
                uint8_t location = 0;
                uint16_t TID16 = 0;
                uint8_t trainerGender = 0;
                Moveset moves{};
            };

            explicit constexpr EncounterGift3Colo(const Properties &properties) noexcept : properties(properties) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Gift3Colo; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Gift3Colo; }
            EncounterType encounterType() const noexcept override { return EncounterType::Gift; }
            std::string name() const override { return "Gift Encounter"; }

            uint16_t speciesId() const noexcept override { return properties.speciesId; }
            uint8_t formId() const noexcept override { return 0; }
            uint8_t levelMin() const noexcept override { return properties.level; }
            uint8_t levelMax() const noexcept override { return properties.level; }
            Enums::GameVersion version() const noexcept override { return properties.version; }
            uint8_t generation() const noexcept override { return 3; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::RSE; }
            uint16_t location() const noexcept override { return properties.location; }
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return false; }
            Enums::Ball fixedBall() const noexcept override { return Enums::Ball::Poke; }
            RNG::Shiny shiny() const noexcept override { return RNG::Shiny::Never; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &pokemon) const override;
            RandomCorrelationRating isCompatible(RNG::PIDType type, const Pokemon::Pokemon &) const override { return type == RNG::PIDType::ColosseumXD || type == RNG::PIDType::ColosseumXDAntishiny ? RandomCorrelationRating::Match : RandomCorrelationRating::Mismatch; }
            RNG::PIDType getSuggestedCorrelation() const noexcept override { return RNG::PIDType::ColosseumXD; }
            bool isFixedTrainer() const noexcept override { return true; }
            bool isTrainerMatch(const Pokemon::Pokemon &pokemon, const std::u16string &trainerName, int language) const override;
            Moveset moves() const noexcept override { return properties.moves; }

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            const RandomCorrelationTemplate *asRandomCorrelation() const noexcept override { return this; }
            const FixedTrainerTemplate *asFixedTrainer() const noexcept override { return this; }
            const MovesetTemplate *asMoveset() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterGift3Colo>(*this); }

            uint16_t TID16() const noexcept { return properties.TID16; }
            uint8_t trainerGender() const noexcept { return properties.trainerGender; }
            /// PKHeX IsJapaneseBonusDisk.
            bool isJapaneseBonusDisk() const noexcept { return properties.version == Enums::GameVersion::RU; }

        private:
            Properties properties;
        };
    }
}

#endif
