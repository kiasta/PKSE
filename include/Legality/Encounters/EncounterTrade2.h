/**
 * A Gen 2 in-game trade. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen2/EncounterTrade2.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERTRADE2_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERTRADE2_H

#include <cstdint>

#include "Legality/Encounters/EncounterLocations.h"
#include "Legality/Encounters/EncounterTrade4PID.h"
#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterTrade2 final : public Encounterable, public EncounterMatchTemplate, public FixedTrainerTemplate, public FixedNicknameTemplate
        {
        public:
            struct Properties
            {
                const LanguageNames4 *nicknames = nullptr;
                const LanguageNames4 *trainerNames = nullptr;
                uint16_t speciesId = 0;
                uint8_t level = 0;
                uint16_t TID16 = 0;
                RNG::Shiny shiny = RNG::Shiny::Never;
                uint8_t gender = 0;
                uint8_t trainerGender = 0;
                RNG::IndividualValueSet individualValues{};
            };

            explicit constexpr EncounterTrade2(const Properties &properties) noexcept : properties(properties) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Trade2; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Trade2; }
            EncounterType encounterType() const noexcept override { return EncounterType::Trade; }
            std::string name() const override { return "In-game Trade"; }

            uint16_t speciesId() const noexcept override { return properties.speciesId; }
            uint8_t formId() const noexcept override { return 0; }
            uint8_t levelMin() const noexcept override { return properties.level; }
            uint8_t levelMax() const noexcept override { return 100; }
            Enums::GameVersion version() const noexcept override { return Enums::GameVersion::GSC; }
            uint8_t generation() const noexcept override { return 2; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::GSC; }
            uint16_t location() const noexcept override { return LOCATION_LINK_TRADE2_NPC; }
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return false; }
            Enums::Ball fixedBall() const noexcept override { return Enums::Ball::Poke; }
            RNG::Shiny shiny() const noexcept override { return properties.shiny; }
            RNG::AbilityPermission ability() const noexcept override { return RNG::AbilityPermission::OnlyHidden; }
            uint8_t fixedGender() const noexcept override { return properties.gender; }
            RNG::IndividualValueSet fixedIVs() const noexcept override { return properties.individualValues; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &pokemon) const override;
            bool isFixedTrainer() const noexcept override { return true; }
            bool isTrainerMatch(const Pokemon::Pokemon &pokemon, const std::u16string &trainerName, int language) const override;
            bool isFixedNickname() const noexcept override { return true; }
            bool isNicknameMatch(const Pokemon::Pokemon &pokemon, const std::u16string &nickname, int language) const override;
            std::u16string getNickname(int language) const override;

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            const FixedTrainerTemplate *asFixedTrainer() const noexcept override { return this; }
            const FixedNicknameTemplate *asFixedNickname() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterTrade2>(*this); }

            uint16_t TID16() const noexcept { return properties.TID16; }
            uint8_t trainerGender() const noexcept { return properties.trainerGender; }

        private:
            int detectLanguage(const Pokemon::Pokemon &pokemon, const std::u16string &trainerName, const std::u16string &nickname) const;
            int detectLanguageNickname(const Pokemon::Pokemon &pokemon, const std::u16string &nickname) const;
            int detectLanguageTrainer(const Pokemon::Pokemon &pokemon, const std::u16string &trainerName) const;
            bool isTrainerMatchExact(const Pokemon::Pokemon &pokemon, const std::u16string &trainerName, int language) const;

            Properties properties;
        };
    }
}

#endif
