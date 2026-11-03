/**
 * An egg bred in a Gen 3 game. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen3/EncounterEgg3.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTEREGG3_H
#define LEGALITY_ENCOUNTERS_ENCOUNTEREGG3_H

#include <cstdint>

#include "Legality/Encounters/EncounterLocations.h"
#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterEgg3 final : public Encounterable, public EggTemplate, public RandomCorrelationTemplate
        {
        public:
            /// PKHeX EncounterEgg3.Level: a Gen 3 egg hatches at level 5.
            static constexpr uint8_t LEVEL = 5;

            /// `version` is the stored version id of the game that bred it.
            constexpr EncounterEgg3(uint16_t speciesId, Enums::GameVersion version) noexcept : eggSpeciesId(speciesId), eggVersion(version) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Egg3; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Egg3; }
            EncounterType encounterType() const noexcept override { return EncounterType::Egg; }
            std::string name() const override { return "Egg"; }

            uint16_t speciesId() const noexcept override { return eggSpeciesId; }
            uint8_t formId() const noexcept override { return 0; }
            uint8_t levelMin() const noexcept override { return LEVEL; }
            uint8_t levelMax() const noexcept override { return LEVEL; }
            Enums::GameVersion version() const noexcept override { return eggVersion; }
            uint8_t generation() const noexcept override { return 3; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::RSE; }
            uint16_t location() const noexcept override { return eggVersion == Enums::GameVersion::FR || eggVersion == Enums::GameVersion::LG ? LOCATION_HATCH_FRLG : LOCATION_HATCH_RSE; }
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return true; }
            Enums::Ball fixedBall() const noexcept override { return Enums::Ball::Poke; }

            bool canHaveVoltTackle() const noexcept override;
            RandomCorrelationRating isCompatible(RNG::PIDType type, const Pokemon::Pokemon &) const override { return type == RNG::PIDType::None ? RandomCorrelationRating::Match : RandomCorrelationRating::Mismatch; }
            RNG::PIDType getSuggestedCorrelation() const noexcept override { return RNG::PIDType::None; }

            const EggTemplate *asEgg() const noexcept override { return this; }
            const RandomCorrelationTemplate *asRandomCorrelation() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterEgg3>(*this); }

            /// PKHeX `other with { Species = ... }`.
            EncounterEgg3 withSpecies(uint16_t speciesId) const noexcept { return EncounterEgg3(speciesId, eggVersion); }

        private:
            uint16_t eggSpeciesId;
            Enums::GameVersion eggVersion;
        };
    }
}

#endif
