/**
 * A Gen 5 wild slot (Black, White, Black 2, White 2), including swarms and hidden grottoes.
 * Ported from PKHeX PKHeX.Core/Legality/Encounters/Templates/Gen5/EncounterSlot5.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERSLOT5_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERSLOT5_H

#include <cstdint>

#include "Legality/Encounters/EncounterArea5.h"
#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterSlot5 final : public Encounterable, public EncounterMatchTemplate
        {
        public:
            constexpr EncounterSlot5(const EncounterArea5 &parent, uint16_t speciesId, uint8_t formId, uint8_t levelMin, uint8_t levelMax) noexcept : parent(&parent), slotSpeciesId(speciesId), slotFormId(formId), slotLevelMin(levelMin), slotLevelMax(levelMax) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Slot5; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Slot5; }
            EncounterType encounterType() const noexcept override { return EncounterType::Wild; }
            std::string name() const override;
            std::string longName() const override;

            uint16_t speciesId() const noexcept override { return slotSpeciesId; }
            uint8_t formId() const noexcept override { return slotFormId; }
            uint8_t levelMin() const noexcept override { return slotLevelMin; }
            uint8_t levelMax() const noexcept override { return slotLevelMax; }
            Enums::GameVersion version() const noexcept override { return parent->version(); }
            uint8_t generation() const noexcept override { return 5; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::BW; }
            uint16_t location() const noexcept override { return parent->location(); }
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return false; }
            RNG::Shiny shiny() const noexcept override { return isHiddenGrotto() ? RNG::Shiny::Never : RNG::Shiny::Random; }
            RNG::AbilityPermission ability() const noexcept override { return isHiddenGrotto() ? RNG::AbilityPermission::OnlyHidden : RNG::AbilityPermission::Any12; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &pokemon) const override;

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterSlot5>(*this); }

            const EncounterArea5 &area() const noexcept { return *parent; }
            SlotType5 type() const noexcept { return parent->type(); }
            /// PKHeX IsHiddenGrotto.
            bool isHiddenGrotto() const noexcept { return parent->type() == SlotType5::HiddenGrotto; }

        private:
            const EncounterArea5 *parent;
            uint16_t slotSpeciesId;
            uint8_t slotFormId;
            uint8_t slotLevelMin;
            uint8_t slotLevelMax;
        };
    }
}

#endif
