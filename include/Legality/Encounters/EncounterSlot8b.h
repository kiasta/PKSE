/**
 * A Brilliant Diamond/Shining Pearl wild slot: grass, surfing, fishing, Rock Smash, a honey tree, or
 * a Grand Underground spawn that knows one of its egg moves. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen8b/EncounterSlot8b.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERSLOT8B_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERSLOT8B_H

#include <cstdint>

#include "Legality/Encounters/EncounterArea8b.h"
#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterSlot8b final : public Encounterable, public EncounterMatchTemplate, public SingleMoveBonusTemplate
        {
        public:
            constexpr EncounterSlot8b(const EncounterArea8b &parent, uint16_t speciesId, uint8_t formId, uint8_t levelMin, uint8_t levelMax) noexcept : parent(&parent), slotSpeciesId(speciesId), slotFormId(formId), slotLevelMin(levelMin), slotLevelMax(levelMax) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Slot8b; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Slot8b; }
            EncounterType encounterType() const noexcept override { return EncounterType::Wild; }
            std::string name() const override;
            std::string longName() const override;

            uint16_t speciesId() const noexcept override { return slotSpeciesId; }
            uint8_t formId() const noexcept override { return slotFormId; }
            uint8_t levelMin() const noexcept override { return slotLevelMin; }
            uint8_t levelMax() const noexcept override { return slotLevelMax; }
            Enums::GameVersion version() const noexcept override { return parent->version(); }
            uint8_t generation() const noexcept override { return 8; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::BDSP; }
            uint16_t location() const noexcept override { return parent->location(); }
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return false; }
            Enums::Ball fixedBall() const noexcept override { return isMarsh() ? Enums::Ball::Safari : Enums::Ball::None; }
            RNG::AbilityPermission ability() const noexcept override { return canUseRadar() ? RNG::AbilityPermission::Any12H : RNG::AbilityPermission::Any12; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &pokemon) const override;

            /// PKHeX ISingleMoveBonus: an Underground spawn knows one of its base species' egg moves.
            bool isMoveBonusPossible() const noexcept override { return isUnderground(); }
            bool isMoveBonusRequired() const noexcept override { return true; }
            std::span<const uint16_t> getMoveBonusPossible() const noexcept override;
            bool isMoveBonus(uint16_t moveId) const noexcept override;

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            const SingleMoveBonusTemplate *asSingleMoveBonus() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterSlot8b>(*this); }

            const EncounterArea8b &area() const noexcept { return *parent; }
            SlotType8b type() const noexcept { return parent->type(); }
            bool isUnderground() const noexcept { return isUnderground8b(parent->location()); }
            bool isMarsh() const noexcept { return isMarsh8b(parent->location()); }
            /// PKHeX CanUseRadar: a Poke Radar chain, the only way to a Hidden Ability in the wild.
            bool canUseRadar() const noexcept;
            /// PKHeX IsInvalidMunchlaxTree.
            bool isInvalidMunchlaxTree(const Pokemon::Pokemon &pokemon) const noexcept;

        private:
            const EncounterArea8b *parent;
            uint16_t slotSpeciesId;
            uint8_t slotFormId;
            uint8_t slotLevelMin;
            uint8_t slotLevelMax;
        };
    }
}

#endif
