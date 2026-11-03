/**
 * A Black 2/White 2 in-game trade, including the hidden-ability trades with Yancy and Curtis.
 * Ported from PKHeX PKHeX.Core/Legality/Encounters/Templates/Gen5/EncounterTrade5B2W2.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERTRADE5B2W2_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERTRADE5B2W2_H

#include <cstdint>

#include "Legality/Encounters/EncounterLocations.h"
#include "Legality/Encounters/EncounterTrade4PID.h"
#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterTrade5B2W2 final : public Encounterable, public EncounterMatchTemplate, public FixedTrainerTemplate, public FixedNicknameTemplate
        {
        public:
            struct Properties
            {
                /// Null for the Yancy and Curtis trades, which keep the player's nickname.
                const LanguageNames4 *nicknames = nullptr;
                const LanguageNames4 *trainerNames = nullptr;
                /// B2, W2 or B2W2.
                Enums::GameVersion version = Enums::GameVersion::Invalid;
                bool isFixedNickname = true;
                uint16_t speciesId = 0;
                uint8_t level = 0;
                RNG::AbilityPermission ability = RNG::AbilityPermission::Any12;
                uint8_t trainerGender = 0;
                uint32_t ID32 = 0;
                uint8_t formId = 0;
                uint8_t gender = GENDER_RANDOM;
                RNG::IndividualValueSet individualValues{};
                uint8_t nature = NATURE_RANDOM;
            };

            explicit constexpr EncounterTrade5B2W2(const Properties &properties) noexcept : properties(properties) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Trade5B2W2; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Trade5B2W2; }
            EncounterType encounterType() const noexcept override { return EncounterType::Trade; }
            std::string name() const override { return "In-game Trade"; }

            uint16_t speciesId() const noexcept override { return properties.speciesId; }
            uint8_t formId() const noexcept override { return properties.formId; }
            uint8_t levelMin() const noexcept override { return properties.level; }
            uint8_t levelMax() const noexcept override { return properties.level; }
            Enums::GameVersion version() const noexcept override { return properties.version; }
            uint8_t generation() const noexcept override { return 5; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::BW; }
            uint16_t location() const noexcept override { return LOCATION_LINK_TRADE5_NPC; }
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return false; }
            Enums::Ball fixedBall() const noexcept override { return Enums::Ball::Poke; }
            RNG::Shiny shiny() const noexcept override { return RNG::Shiny::Never; }
            RNG::AbilityPermission ability() const noexcept override { return properties.ability; }
            uint8_t fixedGender() const noexcept override { return properties.gender; }
            uint8_t fixedNature() const noexcept override { return properties.nature; }
            RNG::IndividualValueSet fixedIVs() const noexcept override { return properties.individualValues; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &) const override { return EncounterMatchRating::Match; }
            bool isFixedTrainer() const noexcept override { return true; }
            bool isTrainerMatch(const Pokemon::Pokemon &pokemon, const std::u16string &trainerName, int language) const override;
            bool isFixedNickname() const noexcept override { return properties.isFixedNickname; }
            bool isNicknameMatch(const Pokemon::Pokemon &pokemon, const std::u16string &nickname, int language) const override;
            std::u16string getNickname(int language) const override;

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            const FixedTrainerTemplate *asFixedTrainer() const noexcept override { return this; }
            const FixedNicknameTemplate *asFixedNickname() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterTrade5B2W2>(*this); }

            uint32_t ID32() const noexcept { return properties.ID32; }
            uint16_t TID16() const noexcept { return static_cast<uint16_t>(properties.ID32); }
            uint16_t SID16() const noexcept { return static_cast<uint16_t>(properties.ID32 >> 16); }
            uint8_t trainerGender() const noexcept { return properties.trainerGender; }

        private:
            Properties properties;
        };
    }
}

#endif
