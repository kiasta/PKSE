/**
 * The template PKHeX records when no encounter explains a Pokemon: its own species, levels and
 * origin. Ported from PKHeX PKHeX.Core/Legality/Encounters/Templates/Shared/EncounterInvalid.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERINVALID_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERINVALID_H

#include <cstdint>

#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterInvalid final : public Encounterable
        {
        public:
            explicit EncounterInvalid(const Pokemon::Pokemon &pokemon);

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Invalid; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Invalid; }
            EncounterType encounterType() const noexcept override { return EncounterType::Unmatched; }
            std::string name() const override { return "Invalid"; }

            uint16_t speciesId() const noexcept override { return invalidSpeciesId; }
            uint8_t formId() const noexcept override { return invalidFormId; }
            uint8_t levelMin() const noexcept override { return invalidLevelMin; }
            uint8_t levelMax() const noexcept override { return invalidLevelMax; }
            Enums::GameVersion version() const noexcept override { return invalidVersion; }
            uint8_t generation() const noexcept override { return invalidGeneration; }
            Enums::GameVersion contextGroup() const noexcept override { return invalidContextGroup; }
            uint16_t location() const noexcept override { return 0; }
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return wasEgg; }
            RNG::AbilityPermission ability() const noexcept override { return RNG::AbilityPermission::Any12H; }

            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterInvalid>(*this); }

        private:
            uint16_t invalidSpeciesId = 0;
            uint8_t invalidFormId = 0;
            uint8_t invalidLevelMin = 0;
            uint8_t invalidLevelMax = 0;
            uint8_t invalidGeneration = 0;
            bool wasEgg = false;
            Enums::GameVersion invalidVersion = Enums::GameVersion::Invalid;
            Enums::GameVersion invalidContextGroup = Enums::GameVersion::Invalid;
        };
    }
}

#endif
