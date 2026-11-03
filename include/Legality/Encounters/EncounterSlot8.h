/**
 * A Sword/Shield wild slot: an overworld (symbol) spawn or a hidden grass, tree, fishing or curry
 * encounter, with the weather it appears in. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen8/EncounterSlot8.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERSLOT8_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERSLOT8_H

#include <cstdint>

#include "Legality/Encounters/EncounterArea8.h"
#include "Legality/Encounters/EncounterRules8.h"
#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterSlot8 final : public Encounterable, public EncounterMatchTemplate
        {
        public:
            constexpr EncounterSlot8(const EncounterArea8 &parent, uint16_t speciesId, uint8_t formId, uint8_t levelMin, uint8_t levelMax, uint16_t weather, SlotType8 type) noexcept : parent(&parent), slotSpeciesId(speciesId), slotFormId(formId), slotLevelMin(levelMin), slotLevelMax(levelMax), slotWeather(weather), slotType(type) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Slot8; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Slot8; }
            EncounterType encounterType() const noexcept override { return EncounterType::Wild; }
            std::string name() const override;
            std::string longName() const override;

            uint16_t speciesId() const noexcept override { return slotSpeciesId; }
            uint8_t formId() const noexcept override { return slotFormId; }
            uint8_t levelMin() const noexcept override { return slotLevelMin; }
            uint8_t levelMax() const noexcept override { return slotLevelMax; }
            Enums::GameVersion version() const noexcept override { return parent->version(); }
            uint8_t generation() const noexcept override { return 8; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::SWSH; }
            uint16_t location() const noexcept override { return parent->location(); }
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return false; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &pokemon) const override;

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterSlot8>(*this); }

            const EncounterArea8 &area() const noexcept { return *parent; }
            uint16_t weather() const noexcept { return slotWeather; }
            SlotType8 type() const noexcept { return slotType; }
            /// PKHeX CanEncounterViaFishing / CanEncounterViaCurry.
            bool canEncounterViaFishing() const noexcept { return canEncounterViaFishing8(slotType, slotWeather); }
            bool canEncounterViaCurry() const noexcept;
            /// PKHeX IOverworldCorrelation8.
            OverworldCorrelation8Requirement getRequirement(const Pokemon::Pokemon &pokemon) const;
            bool isOverworldCorrelationCorrect(const Pokemon::Pokemon &pokemon) const;

        private:
            int getFlawlessIVCountForMetLevel(uint8_t metLevel) const noexcept;

            const EncounterArea8 *parent;
            uint16_t slotSpeciesId;
            uint8_t slotFormId;
            uint8_t slotLevelMin;
            uint8_t slotLevelMax;
            uint16_t slotWeather;
            SlotType8 slotType;
        };
    }
}

#endif
