/**
 * An egg bred in Brilliant Diamond/Shining Pearl. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen8b/EncounterEgg8b.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTEREGG8B_H
#define LEGALITY_ENCOUNTERS_ENCOUNTEREGG8B_H

#include <cstdint>

#include "Legality/Encounters/EncounterLocations.h"
#include "Legality/Encounters/EncounterLocations8.h"
#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterEgg8b final : public Encounterable, public EggTemplate
        {
        public:
            /// PKHeX EncounterEgg8b.Level: a BDSP egg hatches at level 1.
            static constexpr uint8_t LEVEL = 1;

            constexpr EncounterEgg8b(uint16_t speciesId, uint8_t formId, Enums::GameVersion version) noexcept : eggSpeciesId(speciesId), eggFormId(formId), eggVersion(version) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Egg8b; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Egg8b; }
            EncounterType encounterType() const noexcept override { return EncounterType::Egg; }
            std::string name() const override { return "Egg"; }

            uint16_t speciesId() const noexcept override { return eggSpeciesId; }
            uint8_t formId() const noexcept override { return eggFormId; }
            uint8_t levelMin() const noexcept override { return LEVEL; }
            uint8_t levelMax() const noexcept override { return LEVEL; }
            Enums::GameVersion version() const noexcept override { return eggVersion; }
            uint8_t generation() const noexcept override { return 8; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::BDSP; }
            uint16_t location() const noexcept override { return LOCATION_HATCH8B; }
            uint16_t eggLocation() const noexcept override { return LOCATION_DAYCARE8B; }
            bool isEgg() const noexcept override { return true; }
            RNG::AbilityPermission ability() const noexcept override;

            bool canHaveVoltTackle() const noexcept override;

            const EggTemplate *asEgg() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterEgg8b>(*this); }

            /// PKHeX `other with { Species = ..., Form = ... }`.
            EncounterEgg8b withSpeciesForm(uint16_t speciesId, uint8_t formId) const noexcept { return EncounterEgg8b(speciesId, formId, eggVersion); }

        private:
            uint16_t eggSpeciesId;
            uint8_t eggFormId;
            Enums::GameVersion eggVersion;
        };
    }
}

#endif
