/**
 * A Gen 4 wild slot (Diamond, Pearl, Platinum, HeartGold, SoulSilver), including the Safari Zone,
 * Great Marsh, Bug-Catching Contest, Headbutt and honey tree slots. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen4/EncounterSlot4.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERSLOT4_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERSLOT4_H

#include <cstdint>

#include "Legality/Encounters/EncounterArea4.h"
#include "Legality/Encounters/EncounterUtil.h"
#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterSlot4 final : public Encounterable, public EncounterMatchTemplate, public EncounterSlot34Template, public RandomCorrelationTemplate, public GroundTypeTileTemplate
        {
        public:
            constexpr EncounterSlot4(const EncounterArea4 &parent, uint16_t speciesId, uint8_t formId, uint8_t levelMin, uint8_t levelMax, uint8_t slotNumber, uint8_t magnetPullIndex, uint8_t magnetPullCount, uint8_t staticIndex, uint8_t staticCount) noexcept : parent(&parent), slotSpeciesId(speciesId), slotFormId(formId), slotLevelMin(levelMin), slotLevelMax(levelMax), slotSlotNumber(slotNumber), slotMagnetPullIndex(magnetPullIndex), slotMagnetPullCount(magnetPullCount), slotStaticIndex(staticIndex), slotStaticCount(staticCount) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Slot4; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Slot4; }
            EncounterType encounterType() const noexcept override { return EncounterType::Wild; }
            std::string name() const override;
            std::string longName() const override;

            uint16_t speciesId() const noexcept override { return slotSpeciesId; }
            uint8_t formId() const noexcept override { return slotFormId; }
            uint8_t levelMin() const noexcept override { return slotLevelMin; }
            uint8_t levelMax() const noexcept override { return slotLevelMax; }
            Enums::GameVersion version() const noexcept override { return parent->version(); }
            uint8_t generation() const noexcept override { return 4; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::DP; }
            uint16_t location() const noexcept override { return parent->location(); }
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return false; }
            Enums::Ball fixedBall() const noexcept override;
            bool isRandomUnspecificForm() const noexcept override { return slotFormId >= FORM_DYNAMIC; }

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
            RNG::PIDType getSuggestedCorrelation() const noexcept override { return RNG::PIDType::Method1; }
            uint32_t groundTile() const noexcept override { return parent->groundTile(); }

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            const EncounterSlot34Template *asEncounterSlot34() const noexcept override { return this; }
            const RandomCorrelationTemplate *asRandomCorrelation() const noexcept override { return this; }
            const GroundTypeTileTemplate *asGroundTypeTile() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterSlot4>(*this); }

            const EncounterArea4 &area() const noexcept { return *parent; }
            /// PKHeX CanUseRadar: a Diamond/Pearl/Platinum grass slot outside the Great Marsh.
            bool canUseRadar() const noexcept;
            /// PKHeX IsRerollMinimum31: the Bug-Catching Contest and Safari Zone reroll for a 31 IV.
            bool isRerollMinimum31() const noexcept;
            /// PKHeX IsInvalidMunchlaxTree.
            bool isInvalidMunchlaxTree(const Pokemon::Pokemon &pokemon) const noexcept;
            /// The parameters MethodJ and MethodK's frame checks (LeadFinder.getLeadInfo4) read.
            RNG::EncounterSlot4Parameters getSlotParameters() const noexcept;

        private:
            const EncounterArea4 *parent;
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
    }
}

#endif
