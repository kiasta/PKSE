/**
 * An egg bred in X/Y or Omega Ruby/Alpha Sapphire. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen6/EncounterEgg6.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTEREGG6_H
#define LEGALITY_ENCOUNTERS_ENCOUNTEREGG6_H

#include <cstdint>

#include "Legality/Encounters/EncounterLocations.h"
#include "Legality/Encounters/EncounterLocations67.h"
#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterEgg6 final : public Encounterable, public EggTemplate
        {
        public:
            /// PKHeX EncounterEgg6.Level: a Gen 6 egg hatches at level 1.
            static constexpr uint8_t LEVEL = 1;

            constexpr EncounterEgg6(uint16_t speciesId, uint8_t formId, Enums::GameVersion version) noexcept : eggSpeciesId(speciesId), eggFormId(formId), eggVersion(version) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Egg6; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Egg6; }
            EncounterType encounterType() const noexcept override { return EncounterType::Egg; }
            std::string name() const override { return "Egg"; }

            uint16_t speciesId() const noexcept override { return eggSpeciesId; }
            uint8_t formId() const noexcept override { return eggFormId; }
            uint8_t levelMin() const noexcept override { return LEVEL; }
            uint8_t levelMax() const noexcept override { return LEVEL; }
            Enums::GameVersion version() const noexcept override { return eggVersion; }
            uint8_t generation() const noexcept override { return 6; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::XY; }
            uint16_t location() const noexcept override { return eggVersion == Enums::GameVersion::AS || eggVersion == Enums::GameVersion::OR ? LOCATION_HATCH6AO : LOCATION_HATCH6XY; }
            uint16_t eggLocation() const noexcept override { return LOCATION_DAYCARE5; }
            bool isEgg() const noexcept override { return true; }
            RNG::AbilityPermission ability() const noexcept override;

            bool canHaveVoltTackle() const noexcept override;

            const EggTemplate *asEgg() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterEgg6>(*this); }

            /// PKHeX `other with { Species = ..., Form = ... }`.
            EncounterEgg6 withSpeciesForm(uint16_t speciesId, uint8_t formId) const noexcept { return EncounterEgg6(speciesId, formId, eggVersion); }
            /// PKHeX `egg with { Version = ... }`.
            EncounterEgg6 withVersion(Enums::GameVersion version) const noexcept { return EncounterEgg6(eggSpeciesId, eggFormId, version); }

        private:
            uint16_t eggSpeciesId;
            uint8_t eggFormId;
            Enums::GameVersion eggVersion;
        };
    }
}

#endif
