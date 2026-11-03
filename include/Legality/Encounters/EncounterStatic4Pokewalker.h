/**
 * A Pokemon caught on the Pokewalker and sent to HeartGold or SoulSilver. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen4/EncounterStatic4Pokewalker.cs and
 * PokewalkerCourse4.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERSTATIC4POKEWALKER_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERSTATIC4POKEWALKER_H

#include <cstdint>

#include "Legality/Encounters/EncounterLocations4.h"
#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterStatic4Pokewalker final : public Encounterable, public EncounterMatchTemplate, public MovesetTemplate, public RandomCorrelationTemplate
        {
        public:
            /// `courseIndex` is PKHeX's PokewalkerCourse4 value.
            constexpr EncounterStatic4Pokewalker(uint8_t courseIndex, uint16_t speciesId, uint8_t level, uint8_t gender, Moveset moves) noexcept : walkerCourseIndex(courseIndex), walkerSpeciesId(speciesId), walkerLevel(level), walkerGender(gender), walkerMoves(moves) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Static4Pokewalker; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Static4Pokewalker; }
            EncounterType encounterType() const noexcept override { return EncounterType::Static; }
            std::string name() const override;

            uint16_t speciesId() const noexcept override { return walkerSpeciesId; }
            uint8_t formId() const noexcept override { return 0; }
            uint8_t levelMin() const noexcept override { return walkerLevel; }
            uint8_t levelMax() const noexcept override { return walkerLevel; }
            Enums::GameVersion version() const noexcept override { return Enums::GameVersion::HGSS; }
            uint8_t generation() const noexcept override { return 4; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::DP; }
            uint16_t location() const noexcept override { return LOCATION_POKEWALKER4; }
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return false; }
            Enums::Ball fixedBall() const noexcept override { return Enums::Ball::Poke; }
            RNG::Shiny shiny() const noexcept override { return RNG::Shiny::Never; }
            uint8_t fixedGender() const noexcept override { return walkerGender; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &pokemon) const override;
            Moveset moves() const noexcept override { return walkerMoves; }
            RandomCorrelationRating isCompatible(RNG::PIDType type, const Pokemon::Pokemon &pokemon) const override;
            RNG::PIDType getSuggestedCorrelation() const noexcept override { return RNG::PIDType::Pokewalker; }

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            const MovesetTemplate *asMoveset() const noexcept override { return this; }
            const RandomCorrelationTemplate *asRandomCorrelation() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterStatic4Pokewalker>(*this); }

            uint8_t courseIndex() const noexcept { return walkerCourseIndex; }

        private:
            uint8_t walkerCourseIndex;
            uint16_t walkerSpeciesId;
            uint8_t walkerLevel;
            uint8_t walkerGender;
            Moveset walkerMoves;
        };
    }
}

#endif
