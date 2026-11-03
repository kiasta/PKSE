/**
 * A Pokemon GO Pokemon brought into Let's Go through GO Park, and the per species-form group of them
 * the data packs together. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/GO/EncounterSlot7GO.cs and EncounterArea7g.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERSLOT7GO_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERSLOT7GO_H

#include <cstdint>

#include "Legality/Encounters/Encounterable.h"
#include "Legality/Encounters/EncounterLocations67.h"
#include "Legality/Encounters/PogoSlot.h"

namespace Legality
{
    namespace Encounters
    {
        /// PKHeX GameVersion.GO, which Enums::GameVersion has no member for.
        constexpr Enums::GameVersion VERSION_POKEMON_GO = static_cast<Enums::GameVersion>(34);
        /// PKHeX Gender.Random as the GO data stores it.
        constexpr uint8_t POGO_GENDER_RANDOM = 2;

        class EncounterSlot7GO final : public Encounterable, public EncounterMatchTemplate, public PogoSlotTemplate
        {
        public:
            constexpr EncounterSlot7GO(uint16_t dayStart, uint16_t dayEnd, uint16_t speciesId, uint8_t formId, uint8_t levelMin, uint8_t minimumIV, RNG::Shiny shiny, uint8_t gender, PogoType type, uint8_t flags) noexcept
                : slotDayStart(dayStart), slotDayEnd(dayEnd), slotSpeciesId(speciesId), slotFormId(formId), slotLevelMin(levelMin), slotMinimumIV(minimumIV), slotShiny(shiny), slotGender(gender), slotType(type), slotFlags(flags)
            {
            }

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Slot7GO; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Slot7GO; }
            EncounterType encounterType() const noexcept override { return EncounterType::Wild; }
            std::string name() const override { return "GO Encounter (GO)"; }
            std::string longName() const override { return getLongName(); }

            uint16_t speciesId() const noexcept override { return slotSpeciesId; }
            uint8_t formId() const noexcept override { return slotFormId; }
            uint8_t levelMin() const noexcept override { return slotLevelMin; }
            uint8_t levelMax() const noexcept override { return POGO_MAX_LEVEL; }
            Enums::GameVersion version() const noexcept override { return VERSION_POKEMON_GO; }
            uint8_t generation() const noexcept override { return 7; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::GG; }
            uint16_t location() const noexcept override { return LOCATION_GO7; }
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return false; }
            // No fixedBall(): GO Park can override the ball, so Let's Go's own capture rules apply.
            RNG::Shiny shiny() const noexcept override { return slotShiny; }
            uint8_t fixedGender() const noexcept override { return slotGender == POGO_GENDER_RANDOM ? GENDER_RANDOM : slotGender; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &pokemon) const override;

            uint16_t dayStart() const noexcept override { return slotDayStart; }
            uint16_t dayEnd() const noexcept override { return slotDayEnd; }
            PogoType pogoType() const noexcept override { return slotType; }
            uint8_t minimumIV() const noexcept override { return slotMinimumIV; }
            uint8_t pogoFlags() const noexcept override { return slotFlags; }

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterSlot7GO>(*this); }

        private:
            uint16_t slotDayStart;
            uint16_t slotDayEnd;
            uint16_t slotSpeciesId;
            uint8_t slotFormId;
            uint8_t slotLevelMin;
            uint8_t slotMinimumIV;
            RNG::Shiny slotShiny;
            uint8_t slotGender;
            PogoType slotType;
            uint8_t slotFlags;
        };

        /// PKHeX EncounterArea7g: one species-form's GO Park slots.
        struct EncounterArea7g
        {
            uint16_t speciesId;
            uint8_t formId;
            const EncounterSlot7GO *slots;
            uint16_t slotCount;
        };
    }
}

#endif
