/**
 * An X/Y wild slot, including the Horde and Friend Safari slots. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen6/EncounterSlot6XY.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERSLOT6XY_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERSLOT6XY_H

#include <cstdint>

#include "Legality/Encounters/EncounterArea6XY.h"
#include "Legality/Encounters/EncounterUtil.h"
#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterSlot6XY final : public Encounterable, public EncounterMatchTemplate
        {
        public:
            /// `flawlessIVCount` is PKHeX's FlawlessIVCount, which tools/gen_encounters6.py works out
            /// from the X/Y personal table (an Undiscovered egg group gives 3, the Friend Safari 2).
            constexpr EncounterSlot6XY(const EncounterArea6XY &parent, uint16_t speciesId, uint8_t formId, uint8_t levelMin, uint8_t levelMax, uint8_t flawlessIVCount) noexcept : parent(&parent), slotSpeciesId(speciesId), slotFormId(formId), slotLevelMin(levelMin), slotLevelMax(levelMax), slotFlawlessIVCount(flawlessIVCount) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Slot6XY; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Slot6XY; }
            EncounterType encounterType() const noexcept override { return EncounterType::Wild; }
            std::string name() const override;
            std::string longName() const override;

            uint16_t speciesId() const noexcept override { return slotSpeciesId; }
            uint8_t formId() const noexcept override { return slotFormId; }
            uint8_t levelMin() const noexcept override { return slotLevelMin; }
            uint8_t levelMax() const noexcept override { return slotLevelMax; }
            Enums::GameVersion version() const noexcept override { return parent->version(); }
            uint8_t generation() const noexcept override { return 6; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::XY; }
            uint16_t location() const noexcept override { return parent->location(); }
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return false; }
            RNG::AbilityPermission ability() const noexcept override { return isHiddenAbilityPossible() ? RNG::AbilityPermission::Any12H : RNG::AbilityPermission::Any12; }
            uint8_t flawlessIVCount() const noexcept override { return slotFlawlessIVCount; }
            bool isRandomUnspecificForm() const noexcept override { return slotFormId >= FORM_DYNAMIC; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &pokemon) const override;

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterSlot6XY>(*this); }

            const EncounterArea6XY &area() const noexcept { return *parent; }
            bool isFriendSafari() const noexcept { return parent->type() == SlotType6::FriendSafari; }
            bool isHorde() const noexcept { return parent->type() == SlotType6::Horde; }

        private:
            /// PKHeX IsHiddenAbilitySlot: Possible for Hordes and the Friend Safari, Never otherwise.
            bool isHiddenAbilityPossible() const noexcept { return isHorde() || isFriendSafari(); }
            bool isValidOutOfBoundsForm(const Pokemon::Pokemon &pokemon) const noexcept;

            const EncounterArea6XY *parent;
            uint16_t slotSpeciesId;
            uint8_t slotFormId;
            uint8_t slotLevelMin;
            uint8_t slotLevelMax;
            uint8_t slotFlawlessIVCount;
        };
    }
}

#endif
