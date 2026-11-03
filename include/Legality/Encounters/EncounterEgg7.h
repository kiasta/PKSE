/**
 * An egg bred in Sun/Moon or Ultra Sun/Ultra Moon. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen7/EncounterEgg7.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTEREGG7_H
#define LEGALITY_ENCOUNTERS_ENCOUNTEREGG7_H

#include <cstdint>

#include "Legality/Encounters/EncounterLocations.h"
#include "Legality/Encounters/EncounterLocations67.h"
#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterEgg7 final : public Encounterable, public EggTemplate
        {
        public:
            /// PKHeX EncounterEgg7.Level: a Gen 7 egg hatches at level 1.
            static constexpr uint8_t LEVEL = 1;

            constexpr EncounterEgg7(uint16_t speciesId, uint8_t formId, Enums::GameVersion version) noexcept : eggSpeciesId(speciesId), eggFormId(formId), eggVersion(version) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Egg7; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Egg7; }
            EncounterType encounterType() const noexcept override { return EncounterType::Egg; }
            std::string name() const override { return "Egg"; }

            uint16_t speciesId() const noexcept override { return eggSpeciesId; }
            uint8_t formId() const noexcept override { return eggFormId; }
            uint8_t levelMin() const noexcept override { return LEVEL; }
            uint8_t levelMax() const noexcept override { return LEVEL; }
            Enums::GameVersion version() const noexcept override { return eggVersion; }
            uint8_t generation() const noexcept override { return 7; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::SM; }
            uint16_t location() const noexcept override { return LOCATION_HATCH7; }
            uint16_t eggLocation() const noexcept override { return LOCATION_DAYCARE5; }
            bool isEgg() const noexcept override { return true; }
            RNG::AbilityPermission ability() const noexcept override;

            bool canHaveVoltTackle() const noexcept override;

            const EggTemplate *asEgg() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterEgg7>(*this); }

            /// PKHeX `other with { Species = ..., Form = ... }`.
            EncounterEgg7 withSpeciesForm(uint16_t speciesId, uint8_t formId) const noexcept { return EncounterEgg7(speciesId, formId, eggVersion); }
            /// PKHeX `egg with { Version = ... }`.
            EncounterEgg7 withVersion(Enums::GameVersion version) const noexcept { return EncounterEgg7(eggSpeciesId, eggFormId, version); }

        private:
            uint16_t eggSpeciesId;
            uint8_t eggFormId;
            Enums::GameVersion eggVersion;
        };
    }
}

#endif
