/**
 * A Gen 2 wild slot (Gold, Silver, Crystal), including headbutt trees and the Bug-Catching Contest.
 * Ported from PKHeX PKHeX.Core/Legality/Encounters/Templates/Gen2/EncounterSlot2.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERSLOT2_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERSLOT2_H

#include <cstdint>

#include "Legality/Encounters/EncounterArea2.h"
#include "Legality/Encounters/EncounterUtil.h"
#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterSlot2 final : public Encounterable, public EncounterMatchTemplate
        {
        public:
            constexpr EncounterSlot2(const EncounterArea2 &parent, uint16_t speciesId, uint8_t formId, uint8_t levelMin, uint8_t levelMax, uint8_t slotNumber) noexcept : parent(&parent), slotSpeciesId(speciesId), slotFormId(formId), slotLevelMin(levelMin), slotLevelMax(levelMax), slotNumberValue(slotNumber) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Slot2; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Slot2; }
            EncounterType encounterType() const noexcept override { return EncounterType::Wild; }
            std::string name() const override;
            std::string longName() const override;

            uint16_t speciesId() const noexcept override { return slotSpeciesId; }
            uint8_t formId() const noexcept override { return slotFormId; }
            uint8_t levelMin() const noexcept override { return slotLevelMin; }
            uint8_t levelMax() const noexcept override { return slotLevelMax; }
            Enums::GameVersion version() const noexcept override { return parent->version(); }
            uint8_t generation() const noexcept override { return 2; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::GSC; }
            uint16_t location() const noexcept override { return parent->location(); }
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return false; }
            Enums::Ball fixedBall() const noexcept override { return Enums::Ball::Poke; }
            RNG::AbilityPermission ability() const noexcept override;
            bool isRandomUnspecificForm() const noexcept override { return slotFormId >= FORM_DYNAMIC; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &pokemon) const override;

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterSlot2>(*this); }

            const EncounterArea2 &area() const noexcept { return *parent; }
            SlotType2 type() const noexcept { return parent->type(); }
            uint8_t slotNumber() const noexcept { return slotNumberValue; }
            /// PKHeX IsHeadbutt.
            bool isHeadbutt() const noexcept { return type() == SlotType2::Headbutt || type() == SlotType2::HeadbuttSpecial; }
            /// PKHeX IsTreeAvailable: the trainer id's last digit picks which trees hold this slot.
            bool isTreeAvailable(uint16_t trainerId) const noexcept;

        private:
            const EncounterArea2 *parent;
            uint16_t slotSpeciesId;
            uint8_t slotFormId;
            uint8_t slotLevelMin;
            uint8_t slotLevelMax;
            uint8_t slotNumberValue;
        };
    }
}

#endif
