/**
 * An X/Y or Omega Ruby/Alpha Sapphire in-game trade. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen6/EncounterTrade6.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERTRADE6_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERTRADE6_H

#include <cstdint>

#include "Legality/Encounters/EncounterLocations.h"
#include "Legality/Encounters/EncounterTrade8.h"
#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterTrade6 final : public Encounterable, public EncounterMatchTemplate, public FixedTrainerTemplate, public FixedNicknameTemplate
        {
        public:
            struct Properties
            {
                const LanguageNamesSwitch *nicknames = nullptr;
                const LanguageNamesSwitch *trainerNames = nullptr;
                Enums::GameVersion version = Enums::GameVersion::XY;
                /// PKHeX IMemoryOTReadOnly: each trade carries its own memory.
                uint8_t trainerMemory = 0;
                uint8_t trainerMemoryIntensity = 0;
                uint8_t trainerMemoryFeeling = 0;
                uint16_t trainerMemoryVariable = 0;
                uint16_t speciesId = 0;
                uint8_t level = 0;
                uint8_t nature = NATURE_RANDOM;
                uint16_t TID16 = 0;
                RNG::AbilityPermission ability = RNG::AbilityPermission::Any12;
                uint8_t gender = 0;
                uint8_t trainerGender = 0;
                RNG::IndividualValueSet individualValues{};
                bool isFixedNickname = true;
            };

            explicit constexpr EncounterTrade6(const Properties &properties) noexcept : properties(properties) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Trade6; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Trade6; }
            EncounterType encounterType() const noexcept override { return EncounterType::Trade; }
            std::string name() const override { return "In-game Trade"; }

            uint16_t speciesId() const noexcept override { return properties.speciesId; }
            uint8_t formId() const noexcept override { return 0; }
            uint8_t levelMin() const noexcept override { return properties.level; }
            uint8_t levelMax() const noexcept override { return properties.level; }
            Enums::GameVersion version() const noexcept override { return properties.version; }
            uint8_t generation() const noexcept override { return 6; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::XY; }
            uint16_t location() const noexcept override { return LOCATION_LINK_TRADE6_NPC; }
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
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterTrade6>(*this); }

            uint32_t ID32() const noexcept { return properties.TID16; }
            uint8_t trainerGender() const noexcept { return properties.trainerGender; }
            uint8_t trainerMemory() const noexcept { return properties.trainerMemory; }
            uint8_t trainerMemoryIntensity() const noexcept { return properties.trainerMemoryIntensity; }
            uint8_t trainerMemoryFeeling() const noexcept { return properties.trainerMemoryFeeling; }
            uint16_t trainerMemoryVariable() const noexcept { return properties.trainerMemoryVariable; }

        private:
            Properties properties;
        };
    }
}

#endif
