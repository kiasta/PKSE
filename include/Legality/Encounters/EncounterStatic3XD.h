/**
 * A Pokemon XD gift (the starter Eevee and the Johto starters). Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen3/XD/EncounterStatic3XD.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERSTATIC3XD_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERSTATIC3XD_H

#include <cstdint>

#include "Legality/Encounters/EncounterRules3GC.h"
#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterStatic3XD final : public Encounterable, public EncounterMatchTemplate, public RandomCorrelationTemplate, public MovesetTemplate
        {
        public:
            struct Properties
            {
                uint16_t speciesId = 0;
                uint8_t level = 0;
                uint8_t location = 0;
                Moveset moves{};
            };

            explicit constexpr EncounterStatic3XD(const Properties &properties) noexcept : properties(properties) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Static3XD; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Static3XD; }
            EncounterType encounterType() const noexcept override { return EncounterType::Gift; }
            std::string name() const override { return "Static Encounter"; }

            uint16_t speciesId() const noexcept override { return properties.speciesId; }
            uint8_t formId() const noexcept override { return 0; }
            uint8_t levelMin() const noexcept override { return properties.level; }
            uint8_t levelMax() const noexcept override { return properties.level; }
            Enums::GameVersion version() const noexcept override { return GAME_VERSION_COLOSSEUM_XD; }
            uint8_t generation() const noexcept override { return 3; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::RSE; }
            uint16_t location() const noexcept override { return properties.location; }
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return false; }
            Enums::Ball fixedBall() const noexcept override { return Enums::Ball::Poke; }
            bool isFatefulEncounter() const noexcept override { return true; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &pokemon) const override;
            RandomCorrelationRating isCompatible(RNG::PIDType type, const Pokemon::Pokemon &pokemon) const override;
            RNG::PIDType getSuggestedCorrelation() const noexcept override { return RNG::PIDType::ColosseumXD; }
            Moveset moves() const noexcept override { return properties.moves; }

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            const RandomCorrelationTemplate *asRandomCorrelation() const noexcept override { return this; }
            const MovesetTemplate *asMoveset() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterStatic3XD>(*this); }

        private:
            Properties properties;
        };
    }
}

#endif
