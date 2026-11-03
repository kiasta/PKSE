/**
 * A Pokemon Dream Radar gift sent to Black 2/White 2. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen5/EncounterStatic5Radar.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERSTATIC5RADAR_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERSTATIC5RADAR_H

#include <cstdint>

#include "Legality/Encounters/EncounterLocations5.h"
#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterStatic5Radar final : public Encounterable, public EncounterMatchTemplate
        {
        public:
            constexpr EncounterStatic5Radar(uint16_t speciesId, uint8_t formId, RNG::AbilityPermission ability = RNG::AbilityPermission::OnlyHidden) noexcept : radarSpeciesId(speciesId), radarFormId(formId), radarAbility(ability) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Static5Radar; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Static5Radar; }
            EncounterType encounterType() const noexcept override { return EncounterType::Gift; }
            std::string name() const override { return "Dream Radar Encounter"; }

            uint16_t speciesId() const noexcept override { return radarSpeciesId; }
            uint8_t formId() const noexcept override { return radarFormId; }
            uint8_t levelMin() const noexcept override { return 5; }
            uint8_t levelMax() const noexcept override { return 40; }
            Enums::GameVersion version() const noexcept override { return Enums::GameVersion::B2W2; }
            uint8_t generation() const noexcept override { return 5; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::BW; }
            uint16_t location() const noexcept override { return LOCATION_DREAM_RADAR5; }
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return false; }
            Enums::Ball fixedBall() const noexcept override { return Enums::Ball::Dream; }
            RNG::Shiny shiny() const noexcept override { return RNG::Shiny::Never; }
            RNG::AbilityPermission ability() const noexcept override { return radarAbility; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &) const override { return EncounterMatchRating::Match; }

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterStatic5Radar>(*this); }

            /// PKHeX IsMatchLevel: 5 to 40 in steps of 5, by the number of badges.
            static constexpr bool isMatchLevel(uint8_t metLevel) noexcept { return metLevel % 5 == 0 && metLevel >= 5 && metLevel <= 40; }

        private:
            uint16_t radarSpeciesId;
            uint8_t radarFormId;
            RNG::AbilityPermission radarAbility;
        };
    }
}

#endif
