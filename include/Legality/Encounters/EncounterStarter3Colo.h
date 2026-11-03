/**
 * Pokemon Colosseum's starters, Espeon and Umbreon, always male. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen3/Colo/EncounterStarter3Colo.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERSTARTER3COLO_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERSTARTER3COLO_H

#include <cstdint>

#include "Legality/Encounters/EncounterRules3GC.h"
#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterStarter3Colo final : public Encounterable, public EncounterMatchTemplate, public RandomCorrelationTemplate, public MovesetTemplate
        {
        public:
            /// PKHeX Location: Outskirt Stand, where the game begins.
            static constexpr uint8_t LOCATION = 254;
            /// PKHeX Gender: both starters are male.
            static constexpr uint8_t GENDER = 0;

            constexpr EncounterStarter3Colo(uint16_t speciesId, uint8_t level, Moveset moves) noexcept : starterSpeciesId(speciesId), starterLevel(level), starterMoves(moves) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Starter3Colo; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Starter3Colo; }
            EncounterType encounterType() const noexcept override { return EncounterType::Gift; }
            std::string name() const override { return "Static Encounter"; }

            uint16_t speciesId() const noexcept override { return starterSpeciesId; }
            uint8_t formId() const noexcept override { return 0; }
            uint8_t levelMin() const noexcept override { return starterLevel; }
            uint8_t levelMax() const noexcept override { return starterLevel; }
            Enums::GameVersion version() const noexcept override { return GAME_VERSION_COLOSSEUM_XD; }
            uint8_t generation() const noexcept override { return 3; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::RSE; }
            uint16_t location() const noexcept override { return LOCATION; }
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return false; }
            Enums::Ball fixedBall() const noexcept override { return Enums::Ball::Poke; }
            RNG::Shiny shiny() const noexcept override { return RNG::Shiny::Never; }
            uint8_t fixedGender() const noexcept override { return GENDER; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &pokemon) const override;
            RandomCorrelationRating isCompatible(RNG::PIDType type, const Pokemon::Pokemon &) const override { return type == RNG::PIDType::ColosseumStarter ? RandomCorrelationRating::Match : RandomCorrelationRating::Mismatch; }
            RNG::PIDType getSuggestedCorrelation() const noexcept override { return RNG::PIDType::ColosseumStarter; }
            Moveset moves() const noexcept override { return starterMoves; }

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            const RandomCorrelationTemplate *asRandomCorrelation() const noexcept override { return this; }
            const MovesetTemplate *asMoveset() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterStarter3Colo>(*this); }

        private:
            uint16_t starterSpeciesId;
            uint8_t starterLevel;
            Moveset starterMoves;
        };
    }
}

#endif
