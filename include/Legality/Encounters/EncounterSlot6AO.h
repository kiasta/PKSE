/**
 * An Omega Ruby/Alpha Sapphire wild slot, including the Horde slots and the DexNav, which can add
 * one egg move and raise the level. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen6/EncounterSlot6AO.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERSLOT6AO_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERSLOT6AO_H

#include <cstdint>
#include <span>

#include "Legality/Encounters/EncounterArea6AO.h"
#include "Legality/Encounters/EncounterUtil.h"
#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterSlot6AO final : public Encounterable, public EncounterMatchTemplate, public SingleMoveBonusTemplate
        {
        public:
            /// PKHeX's flute and DexNav level boosts.
            static constexpr uint8_t FLUTE_BOOST_MIN = 4;
            static constexpr uint8_t FLUTE_BOOST_MAX = 4;
            static constexpr uint8_t DEXNAV_BOOST = 29 + FLUTE_BOOST_MAX;

            constexpr EncounterSlot6AO(const EncounterArea6AO &parent, uint16_t speciesId, uint8_t formId, uint8_t levelMin, uint8_t levelMax) noexcept : parent(&parent), slotSpeciesId(speciesId), slotFormId(formId), slotLevelMin(levelMin), slotLevelMax(levelMax) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Slot6AO; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Slot6AO; }
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
            bool isRandomUnspecificForm() const noexcept override { return slotFormId >= FORM_DYNAMIC; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &pokemon) const override;

            /// PKHeX ISingleMoveBonus: a DexNav encounter may know one of its base species' egg moves.
            bool isMoveBonusPossible() const noexcept override { return canDexNav(); }
            bool isMoveBonusRequired() const noexcept override { return false; }
            std::span<const uint16_t> getMoveBonusPossible() const noexcept override;
            bool isMoveBonus(uint16_t moveId) const noexcept override;

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            const SingleMoveBonusTemplate *asSingleMoveBonus() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterSlot6AO>(*this); }

            const EncounterArea6AO &area() const noexcept { return *parent; }
            bool canDexNav() const noexcept { return parent->type() != SlotType6::Rock_Smash; }
            bool isHorde() const noexcept { return parent->type() == SlotType6::Horde; }
            /// PKHeX IEncounterDownlevel.GetDownleveledMin: the White Flute lowers the level.
            uint8_t getDownleveledMin() const noexcept { return static_cast<uint8_t>(slotLevelMin - FLUTE_BOOST_MIN); }

        private:
            /// PKHeX IsHiddenAbilitySlot: Possible for a DexNav or Horde slot, Never otherwise.
            bool isHiddenAbilityPossible() const noexcept { return isMoveBonusPossible() || isHorde(); }
            bool isDeferredWurmple(const Pokemon::Pokemon &pokemon) const noexcept;

            const EncounterArea6AO *parent;
            uint16_t slotSpeciesId;
            uint8_t slotFormId;
            uint8_t slotLevelMin;
            uint8_t slotLevelMax;
        };
    }
}

#endif
