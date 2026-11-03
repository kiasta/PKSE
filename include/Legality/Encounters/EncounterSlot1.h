/**
 * A Gen 1 wild slot (Red, Green, Blue, Yellow). Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen1/EncounterSlot1.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERSLOT1_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERSLOT1_H

#include <cstdint>

#include "Legality/Encounters/EncounterArea1.h"
#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterSlot1 final : public Encounterable, public EncounterMatchTemplate
        {
        public:
            constexpr EncounterSlot1(const EncounterArea1 &parent, uint16_t speciesId, uint8_t levelMin, uint8_t levelMax, uint8_t slotNumber) noexcept : parent(&parent), slotSpeciesId(speciesId), slotLevelMin(levelMin), slotLevelMax(levelMax), slotNumberValue(slotNumber) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Slot1; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Slot1; }
            EncounterType encounterType() const noexcept override { return EncounterType::Wild; }
            std::string name() const override;
            std::string longName() const override;

            uint16_t speciesId() const noexcept override { return slotSpeciesId; }
            uint8_t formId() const noexcept override { return 0; }
            uint8_t levelMin() const noexcept override { return slotLevelMin; }
            uint8_t levelMax() const noexcept override { return slotLevelMax; }
            Enums::GameVersion version() const noexcept override { return parent->version(); }
            uint8_t generation() const noexcept override { return 1; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::RBY; }
            uint16_t location() const noexcept override { return parent->location(); }
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return false; }
            Enums::Ball fixedBall() const noexcept override { return Enums::Ball::Poke; }
            RNG::AbilityPermission ability() const noexcept override;

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &) const override { return EncounterMatchRating::Match; }

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterSlot1>(*this); }

            const EncounterArea1 &area() const noexcept { return *parent; }
            SlotType1 type() const noexcept { return parent->type(); }
            uint8_t slotNumber() const noexcept { return slotNumberValue; }

        private:
            const EncounterArea1 *parent;
            uint16_t slotSpeciesId;
            uint8_t slotLevelMin;
            uint8_t slotLevelMax;
            uint8_t slotNumberValue;
        };
    }
}

#endif
