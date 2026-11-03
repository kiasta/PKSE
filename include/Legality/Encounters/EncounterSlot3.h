/**
 * A Gen 3 wild slot (Ruby, Sapphire, Emerald, FireRed, LeafGreen), and the Ruby/Sapphire/Emerald
 * swarm slot that adds a fixed moveset. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen3/EncounterSlot3.cs and EncounterSlot3Swarm.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERSLOT3_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERSLOT3_H

#include <cstdint>

#include "Legality/Encounters/EncounterArea3.h"
#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterSlot3 : public Encounterable, public EncounterMatchTemplate, public EncounterSlot34Template, public RandomCorrelationTemplate
        {
        public:
            constexpr EncounterSlot3(const EncounterArea3 &parent, uint16_t speciesId, uint8_t formId, uint8_t levelMin, uint8_t levelMax, uint8_t slotNumber, uint8_t magnetPullIndex, uint8_t magnetPullCount, uint8_t staticIndex, uint8_t staticCount) noexcept : parent(&parent), slotSpeciesId(speciesId), slotFormId(formId), slotLevelMin(levelMin), slotLevelMax(levelMax), slotSlotNumber(slotNumber), slotMagnetPullIndex(magnetPullIndex), slotMagnetPullCount(magnetPullCount), slotStaticIndex(staticIndex), slotStaticCount(staticCount) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Slot3 || type == EncounterTemplateType::Slot3Swarm; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Slot3; }
            EncounterType encounterType() const noexcept override { return EncounterType::Wild; }
            std::string name() const override;
            std::string longName() const override;

            uint16_t speciesId() const noexcept override { return slotSpeciesId; }
            uint8_t formId() const noexcept override { return slotFormId; }
            uint8_t levelMin() const noexcept override { return slotLevelMin; }
            uint8_t levelMax() const noexcept override { return slotLevelMax; }
            Enums::GameVersion version() const noexcept override { return parent->version(); }
            uint8_t generation() const noexcept override { return 3; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::RSE; }
            uint16_t location() const noexcept override { return parent->location(); }
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return false; }
            Enums::Ball fixedBall() const noexcept override { return isSafari() ? Enums::Ball::Safari : Enums::Ball::None; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &pokemon) const override;

            uint8_t slotNumber() const noexcept override { return slotSlotNumber; }
            uint8_t magnetPullIndex() const noexcept override { return slotMagnetPullIndex; }
            uint8_t magnetPullCount() const noexcept override { return slotMagnetPullCount; }
            uint8_t staticIndex() const noexcept override { return slotStaticIndex; }
            uint8_t staticCount() const noexcept override { return slotStaticCount; }
            uint8_t pressureLevel() const noexcept override;
            uint8_t areaRate() const noexcept override { return parent->rate(); }
            uint8_t slotType() const noexcept override { return static_cast<uint8_t>(parent->type()); }

            RandomCorrelationRating isCompatible(RNG::PIDType type, const Pokemon::Pokemon &pokemon) const override;
            RNG::PIDType getSuggestedCorrelation() const noexcept override;

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            const EncounterSlot34Template *asEncounterSlot34() const noexcept override { return this; }
            const RandomCorrelationTemplate *asRandomCorrelation() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterSlot3>(*this); }

            const EncounterArea3 &area() const noexcept { return *parent; }
            bool isSafari() const noexcept;
            bool isSafariHoenn() const noexcept;
            /// The parameters R's frame checks (LeadFinder.getLeadInfo3) read.
            RNG::EncounterSlot3Parameters getSlotParameters() const noexcept;

        private:
            const EncounterArea3 *parent;
            uint16_t slotSpeciesId;
            uint8_t slotFormId;
            uint8_t slotLevelMin;
            uint8_t slotLevelMax;
            uint8_t slotSlotNumber;
            uint8_t slotMagnetPullIndex;
            uint8_t slotMagnetPullCount;
            uint8_t slotStaticIndex;
            uint8_t slotStaticCount;
        };

        class EncounterSlot3Swarm final : public EncounterSlot3, public MovesetTemplate
        {
        public:
            constexpr EncounterSlot3Swarm(const EncounterArea3 &parent, uint16_t speciesId, uint8_t levelMin, uint8_t levelMax, uint8_t slotNumber, Moveset moves) noexcept : EncounterSlot3(parent, speciesId, 0, levelMin, levelMax, slotNumber, 0, 0, 0, 0), swarmMoves(moves) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Slot3Swarm; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Slot3Swarm; }
            Moveset moves() const noexcept override { return swarmMoves; }
            const MovesetTemplate *asMoveset() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterSlot3Swarm>(*this); }

        private:
            Moveset swarmMoves;
        };
    }
}

#endif
