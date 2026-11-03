/**
 * A Sun/Moon or Ultra Sun/Ultra Moon wild slot, including the SOS slots. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen7/EncounterSlot7.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERSLOT7_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERSLOT7_H

#include <cstdint>

#include "Legality/Encounters/EncounterArea7.h"
#include "Legality/Encounters/EncounterUtil.h"
#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterSlot7 final : public Encounterable, public EncounterMatchTemplate
        {
        public:
            constexpr EncounterSlot7(const EncounterArea7 &parent, uint16_t speciesId, uint8_t formId, uint8_t levelMin, uint8_t levelMax) noexcept : parent(&parent), slotSpeciesId(speciesId), slotFormId(formId), slotLevelMin(levelMin), slotLevelMax(levelMax) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Slot7; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Slot7; }
            EncounterType encounterType() const noexcept override { return EncounterType::Wild; }
            std::string name() const override;
            std::string longName() const override;

            uint16_t speciesId() const noexcept override { return slotSpeciesId; }
            uint8_t formId() const noexcept override { return slotFormId; }
            uint8_t levelMin() const noexcept override { return slotLevelMin; }
            uint8_t levelMax() const noexcept override { return slotLevelMax; }
            Enums::GameVersion version() const noexcept override { return parent->version(); }
            uint8_t generation() const noexcept override { return 7; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::SM; }
            uint16_t location() const noexcept override { return parent->location(); }
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return false; }
            Enums::Ball fixedBall() const noexcept override;
            RNG::AbilityPermission ability() const noexcept override { return isSOS() ? RNG::AbilityPermission::Any12H : RNG::AbilityPermission::Any12; }
            bool isRandomUnspecificForm() const noexcept override { return slotFormId >= FORM_DYNAMIC; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &pokemon) const override;

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterSlot7>(*this); }

            const EncounterArea7 &area() const noexcept { return *parent; }
            bool isSOS() const noexcept { return parent->type() == SlotType7::SOS; }
            /// PKHeX IFlawlessIVCountConditional.GetFlawlessIVCount: a Hidden Ability SOS ally comes
            /// from a chain of 10 or more and has at least two perfect IVs.
            uint8_t getFlawlessIVCountMinimum(const Pokemon::Pokemon &pokemon) const noexcept;

        private:
            const EncounterArea7 *parent;
            uint16_t slotSpeciesId;
            uint8_t slotFormId;
            uint8_t slotLevelMin;
            uint8_t slotLevelMax;
        };
    }
}

#endif
