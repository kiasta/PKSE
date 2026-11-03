/**
 * One Pokemon XD Poke Spot slot. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen3/XD/EncounterSlot3XD.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERSLOT3XD_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERSLOT3XD_H

#include <cstdint>

#include "Legality/Encounters/EncounterRules3GC.h"
#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterArea3XD;

        /// PKHeX PokeSpotArea.
        enum class PokeSpotArea : uint8_t
        {
            Rock = 0,
            Oasis = 1,
            Cave = 2,
        };

        class EncounterSlot3XD final : public Encounterable, public EncounterMatchTemplate, public RandomCorrelationTemplate
        {
        public:
            constexpr EncounterSlot3XD(const EncounterArea3XD &area, uint16_t speciesId, uint8_t levelMin, uint8_t levelMax, uint8_t slotNumber) noexcept : parent(&area), slotSpeciesId(speciesId), slotLevelMin(levelMin), slotLevelMax(levelMax), slotNumberValue(slotNumber) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Slot3XD; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Slot3XD; }
            EncounterType encounterType() const noexcept override { return EncounterType::Wild; }
            std::string name() const override { return "Wild Encounter (XD)"; }
            std::string longName() const override;

            uint16_t speciesId() const noexcept override { return slotSpeciesId; }
            uint8_t formId() const noexcept override { return 0; }
            uint8_t levelMin() const noexcept override { return slotLevelMin; }
            uint8_t levelMax() const noexcept override { return slotLevelMax; }
            Enums::GameVersion version() const noexcept override { return GAME_VERSION_COLOSSEUM_XD; }
            uint8_t generation() const noexcept override { return 3; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::RSE; }
            uint16_t location() const noexcept override;
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return false; }
            bool isFatefulEncounter() const noexcept override { return true; }

            /// The area decides the match.
            bool isMatchExact(const Pokemon::Pokemon &, const Evolutions::EvolutionCriteria &) const override { return true; }
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &) const override { return EncounterMatchRating::Match; }
            RandomCorrelationRating isCompatible(RNG::PIDType type, const Pokemon::Pokemon &) const override { return type == RNG::PIDType::PokeSpot ? RandomCorrelationRating::Match : RandomCorrelationRating::Mismatch; }
            RNG::PIDType getSuggestedCorrelation() const noexcept override { return RNG::PIDType::PokeSpot; }

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            const RandomCorrelationTemplate *asRandomCorrelation() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterSlot3XD>(*this); }

            uint8_t slotNumber() const noexcept { return slotNumberValue; }
            const EncounterArea3XD &area() const noexcept { return *parent; }

        private:
            const EncounterArea3XD *parent;
            uint16_t slotSpeciesId;
            uint8_t slotLevelMin;
            uint8_t slotLevelMax;
            uint8_t slotNumberValue;
        };
    }
}

#endif
