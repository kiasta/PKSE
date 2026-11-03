/**
 * A Let's Go wild slot. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen7b/EncounterSlot7b.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERSLOT7B_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERSLOT7B_H

#include <cstdint>

#include "Legality/Encounters/EncounterArea7b.h"
#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterSlot7b final : public Encounterable, public EncounterMatchTemplate
        {
        public:
            /// `crossoverFlags` bit 0 lets the slot cross into the area's first neighbour, bit 1 its second.
            constexpr EncounterSlot7b(const EncounterArea7b &parent, uint16_t speciesId, uint8_t crossoverFlags, uint8_t levelMin, uint8_t levelMax) noexcept : parent(&parent), slotSpeciesId(speciesId), slotCrossoverFlags(crossoverFlags), slotLevelMin(levelMin), slotLevelMax(levelMax) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Slot7b; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Slot7b; }
            EncounterType encounterType() const noexcept override { return EncounterType::Wild; }
            std::string name() const override;

            uint16_t speciesId() const noexcept override { return slotSpeciesId; }
            uint8_t formId() const noexcept override { return 0; }
            uint8_t levelMin() const noexcept override { return slotLevelMin; }
            uint8_t levelMax() const noexcept override { return slotLevelMax; }
            Enums::GameVersion version() const noexcept override { return parent->version(); }
            uint8_t generation() const noexcept override { return 7; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::GG; }
            uint16_t location() const noexcept override { return parent->location(); }
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return false; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &) const override { return EncounterMatchRating::Match; }

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterSlot7b>(*this); }

            const EncounterArea7b &area() const noexcept { return *parent; }
            uint8_t crossoverFlags() const noexcept { return slotCrossoverFlags; }

        private:
            bool isCrossoverAllowed(uint16_t metLocation) const noexcept;

            const EncounterArea7b *parent;
            uint16_t slotSpeciesId;
            uint8_t slotCrossoverFlags;
            uint8_t slotLevelMin;
            uint8_t slotLevelMax;
        };
    }
}

#endif
