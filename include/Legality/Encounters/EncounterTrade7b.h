/**
 * A Let's Go in-game trade (the Alolan forms). Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen7b/EncounterTrade7b.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERTRADE7B_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERTRADE7B_H

#include <cstdint>

#include "Legality/Encounters/EncounterLocations.h"
#include "Legality/Encounters/EncounterTrade8.h"
#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterTrade7b final : public Encounterable, public EncounterMatchTemplate, public FixedTrainerTemplate
        {
        public:
            struct Properties
            {
                const LanguageNamesSwitch *trainerNames = nullptr;
                Enums::GameVersion version = Enums::GameVersion::GG;
                uint16_t speciesId = 0;
                uint8_t formId = 0;
                uint8_t level = 0;
                uint32_t ID32 = 0;
                uint8_t trainerGender = 0;
                RNG::IndividualValueSet individualValues{};
            };

            explicit constexpr EncounterTrade7b(const Properties &properties) noexcept : properties(properties) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Trade7b; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Trade7b; }
            EncounterType encounterType() const noexcept override { return EncounterType::Trade; }
            std::string name() const override { return "In-game Trade"; }

            uint16_t speciesId() const noexcept override { return properties.speciesId; }
            uint8_t formId() const noexcept override { return properties.formId; }
            uint8_t levelMin() const noexcept override { return properties.level; }
            uint8_t levelMax() const noexcept override { return properties.level; }
            Enums::GameVersion version() const noexcept override { return properties.version; }
            uint8_t generation() const noexcept override { return 7; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::GG; }
            uint16_t location() const noexcept override { return LOCATION_LINK_TRADE6_NPC; }
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return false; }
            Enums::Ball fixedBall() const noexcept override { return Enums::Ball::Poke; }
            RNG::IndividualValueSet fixedIVs() const noexcept override { return properties.individualValues; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &) const override { return EncounterMatchRating::Match; }
            bool isFixedTrainer() const noexcept override { return true; }
            bool isTrainerMatch(const Pokemon::Pokemon &pokemon, const std::u16string &trainerName, int language) const override;

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            const FixedTrainerTemplate *asFixedTrainer() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterTrade7b>(*this); }

            uint32_t ID32() const noexcept { return properties.ID32; }
            uint8_t trainerGender() const noexcept { return properties.trainerGender; }

        private:
            Properties properties;
        };
    }
}

#endif
