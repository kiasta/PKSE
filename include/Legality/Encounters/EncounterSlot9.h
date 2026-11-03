/**
 * A Scarlet/Violet wild slot: an overworld spawn with the times of day and weather it appears in.
 * Ported from PKHeX PKHeX.Core/Legality/Encounters/Templates/Gen9/EncounterSlot9.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERSLOT9_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERSLOT9_H

#include <cstdint>

#include "Legality/Encounters/EncounterArea9.h"
#include "Legality/Encounters/EncounterRules9.h"
#include "Legality/Encounters/EncounterSlot4.h"
#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterSlot9 final : public Encounterable, public EncounterMatchTemplate
        {
        public:
            constexpr EncounterSlot9(const EncounterArea9 &parent, uint16_t speciesId, uint8_t formId, uint8_t levelMin, uint8_t levelMax, uint8_t gender, uint8_t time, uint8_t weather) noexcept : parent(&parent), slotSpeciesId(speciesId), slotFormId(formId), slotLevelMin(levelMin), slotLevelMax(levelMax), slotGender(gender), slotTime(time), slotWeather(weather) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Slot9; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Slot9; }
            EncounterType encounterType() const noexcept override { return EncounterType::Wild; }
            std::string name() const override;

            uint16_t speciesId() const noexcept override { return slotSpeciesId; }
            uint8_t formId() const noexcept override { return slotFormId; }
            uint8_t levelMin() const noexcept override { return slotLevelMin; }
            uint8_t levelMax() const noexcept override { return slotLevelMax; }
            Enums::GameVersion version() const noexcept override { return parent->version(); }
            uint8_t generation() const noexcept override { return 9; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::SV; }
            uint16_t location() const noexcept override { return parent->actualLocation(); }
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return false; }
            uint8_t fixedGender() const noexcept override { return slotGender; }
            bool isRandomUnspecificForm() const noexcept override { return slotFormId >= FORM_DYNAMIC; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &pokemon) const override;

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterSlot9>(*this); }

            const EncounterArea9 &area() const noexcept { return *parent; }
            uint8_t weather() const noexcept { return slotWeather; }
            /// PKHeX CanSpawnAtTime: bit N of the time byte set means "not at time N".
            bool canSpawnAtTime(int timeIndex) const noexcept { return (slotTime & (1 << timeIndex)) == 0; }

        private:
            const EncounterArea9 *parent;
            uint16_t slotSpeciesId;
            uint8_t slotFormId;
            uint8_t slotLevelMin;
            uint8_t slotLevelMax;
            uint8_t slotGender;
            uint8_t slotTime;
            uint8_t slotWeather;
        };
    }
}

#endif
