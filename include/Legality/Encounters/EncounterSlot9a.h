/**
 * A Legends: Z-A wild slot, in Lumiose or Hyperspace Lumiose, Alpha or not. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen9a/EncounterSlot9a.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERSLOT9A_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERSLOT9A_H

#include <cstdint>

#include "Legality/Encounters/EncounterArea9a.h"
#include "Legality/Encounters/EncounterRules9a.h"
#include "Legality/Encounters/EncounterSlot4.h"
#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterSlot9a final : public Encounterable, public EncounterMatchTemplate
        {
        public:
            constexpr EncounterSlot9a(const EncounterArea9a &parent, uint16_t speciesId, uint8_t formId, uint8_t levelMin, uint8_t levelMax, bool isAlpha, uint8_t gender, RNG::Shiny shiny) noexcept : parent(&parent), slotSpeciesId(speciesId), slotFormId(formId), slotLevelMin(levelMin), slotLevelMax(levelMax), slotIsAlpha(isAlpha), slotGender(gender), slotShiny(shiny) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Slot9a; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Slot9a; }
            EncounterType encounterType() const noexcept override { return EncounterType::Wild; }
            std::string name() const override { return "Wild Encounter"; }
            std::string longName() const override;

            uint16_t speciesId() const noexcept override { return slotSpeciesId; }
            uint8_t formId() const noexcept override { return slotFormId; }
            uint8_t levelMin() const noexcept override { return slotLevelMin; }
            uint8_t levelMax() const noexcept override { return slotLevelMax; }
            Enums::GameVersion version() const noexcept override { return Enums::GameVersion::ZA; }
            uint8_t generation() const noexcept override { return 9; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::ZA; }
            uint16_t location() const noexcept override { return parent->location(); }
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return false; }
            RNG::Shiny shiny() const noexcept override { return slotShiny; }
            uint8_t fixedGender() const noexcept override { return slotGender; }
            uint8_t flawlessIVCount() const noexcept override { return slotIsAlpha ? 3 : 0; }
            bool isRandomUnspecificForm() const noexcept override { return slotFormId >= FORM_DYNAMIC; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &pokemon) const override;

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterSlot9a>(*this); }

            const EncounterArea9a &area() const noexcept { return *parent; }
            bool isAlpha() const noexcept { return slotIsAlpha; }
            /// PKHeX TryGetSeed.
            SeedCorrelationResult tryGetSeed(const Pokemon::Pokemon &pokemon) const noexcept;
            /// PKHeX GetParams(pi, shinyCharm, activeShinyPower).
            RNG::GenerateParam9a getParameters(bool hasShinyCharm, bool hasShinyPower) const noexcept;

        private:
            const EncounterArea9a *parent;
            uint16_t slotSpeciesId;
            uint8_t slotFormId;
            uint8_t slotLevelMin;
            uint8_t slotLevelMax;
            bool slotIsAlpha;
            uint8_t slotGender;
            RNG::Shiny slotShiny;
        };
    }
}

#endif
