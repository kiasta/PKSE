/**
 * An egg bred in a Gen 5 game. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen5/EncounterEgg5.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTEREGG5_H
#define LEGALITY_ENCOUNTERS_ENCOUNTEREGG5_H

#include <cstdint>

#include "Legality/Encounters/EncounterLocations5.h"
#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterEgg5 final : public Encounterable, public EggTemplate, public RandomCorrelationTemplate
        {
        public:
            /// PKHeX EncounterEgg5.Level.
            static constexpr uint8_t LEVEL = 1;

            /// `version` is the stored version id of the game that bred it.
            constexpr EncounterEgg5(uint16_t speciesId, uint8_t formId, Enums::GameVersion version) noexcept : eggSpeciesId(speciesId), eggFormId(formId), eggVersion(version) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Egg5; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Egg5; }
            EncounterType encounterType() const noexcept override { return EncounterType::Egg; }
            std::string name() const override { return "Egg"; }

            uint16_t speciesId() const noexcept override { return eggSpeciesId; }
            uint8_t formId() const noexcept override { return eggFormId; }
            uint8_t levelMin() const noexcept override { return LEVEL; }
            uint8_t levelMax() const noexcept override { return LEVEL; }
            Enums::GameVersion version() const noexcept override { return eggVersion; }
            uint8_t generation() const noexcept override { return 5; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::BW; }
            uint16_t location() const noexcept override { return LOCATION_HATCH5; }
            uint16_t eggLocation() const noexcept override { return LOCATION_DAYCARE5; }
            bool isEgg() const noexcept override { return true; }
            Enums::Ball fixedBall() const noexcept override { return Enums::Ball::Poke; }
            RNG::AbilityPermission ability() const noexcept override;

            bool canHaveVoltTackle() const noexcept override;
            /// Gen 5 eggs have PID/IV correlations and RNG abuse; PKHeX assumes none.
            RandomCorrelationRating isCompatible(RNG::PIDType type, const Pokemon::Pokemon &) const override { return type == RNG::PIDType::None ? RandomCorrelationRating::Match : RandomCorrelationRating::Mismatch; }
            RNG::PIDType getSuggestedCorrelation() const noexcept override { return RNG::PIDType::None; }

            const EggTemplate *asEgg() const noexcept override { return this; }
            const RandomCorrelationTemplate *asRandomCorrelation() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterEgg5>(*this); }

            /// PKHeX `other with { Species = ... }`.
            EncounterEgg5 withSpecies(uint16_t speciesId) const noexcept { return EncounterEgg5(speciesId, eggFormId, eggVersion); }

        private:
            uint16_t eggSpeciesId;
            uint8_t eggFormId;
            Enums::GameVersion eggVersion;
        };
    }
}

#endif
