/**
 * A Sword/Shield in-game trade. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen8/EncounterTrade8.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERTRADE8_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERTRADE8_H

#include <array>
#include <cstdint>

#include "Legality/Encounters/EncounterLocations.h"
#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        /// One string per language id (0-10, through Traditional Chinese), the games' own text;
        /// nullptr where PKHeX has none.
        using LanguageNamesSwitch = std::array<const char16_t *, 11>;

        class EncounterTrade8 final : public Encounterable, public EncounterMatchTemplate, public FixedTrainerTemplate, public FixedNicknameTemplate, public RelearnTemplate
        {
        public:
            struct Properties
            {
                /// nullptr for a trade that keeps the species name (PKHeX IsFixedNickname false).
                const LanguageNamesSwitch *nicknames = nullptr;
                const LanguageNamesSwitch *trainerNames = nullptr;
                Enums::GameVersion version = Enums::GameVersion::SWSH;
                uint16_t speciesId = 0;
                uint8_t formId = 0;
                uint8_t level = 0;
                uint8_t trainerMemory = 0;
                uint16_t trainerMemoryVariable = 0;
                uint8_t trainerMemoryFeeling = 0;
                uint8_t trainerMemoryIntensity = 0;
                uint8_t dynamaxLevel = 0;
                uint8_t flawlessIVCount = 0;
                RNG::Shiny shiny = RNG::Shiny::Never;
                uint8_t nature = NATURE_RANDOM;
                uint32_t ID32 = 0;
                RNG::AbilityPermission ability = RNG::AbilityPermission::Any12;
                uint8_t gender = GENDER_RANDOM;
                uint8_t trainerGender = 0;
                RNG::IndividualValueSet individualValues{};
                Moveset relearnMoves{};
            };

            explicit constexpr EncounterTrade8(const Properties &properties) noexcept : properties(properties) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Trade8; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Trade8; }
            EncounterType encounterType() const noexcept override { return EncounterType::Trade; }
            std::string name() const override { return "In-game Trade"; }

            uint16_t speciesId() const noexcept override { return properties.speciesId; }
            uint8_t formId() const noexcept override { return properties.formId; }
            uint8_t levelMin() const noexcept override { return properties.level; }
            uint8_t levelMax() const noexcept override { return properties.level; }
            Enums::GameVersion version() const noexcept override { return properties.version; }
            uint8_t generation() const noexcept override { return 8; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::SWSH; }
            uint16_t location() const noexcept override { return LOCATION_LINK_TRADE6_NPC; }
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return false; }
            Enums::Ball fixedBall() const noexcept override { return Enums::Ball::Poke; }
            RNG::Shiny shiny() const noexcept override { return properties.shiny; }
            RNG::AbilityPermission ability() const noexcept override { return properties.ability; }
            uint8_t fixedGender() const noexcept override { return properties.gender; }
            uint8_t fixedNature() const noexcept override { return properties.nature; }
            RNG::IndividualValueSet fixedIVs() const noexcept override { return properties.individualValues; }
            uint8_t flawlessIVCount() const noexcept override { return properties.flawlessIVCount; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &) const override { return EncounterMatchRating::Match; }
            bool isFixedTrainer() const noexcept override { return true; }
            bool isTrainerMatch(const Pokemon::Pokemon &pokemon, const std::u16string &trainerName, int language) const override;
            bool isFixedNickname() const noexcept override { return properties.nicknames != nullptr; }
            bool isNicknameMatch(const Pokemon::Pokemon &pokemon, const std::u16string &nickname, int language) const override;
            std::u16string getNickname(int language) const override;
            Moveset relearnMoves() const noexcept override { return properties.relearnMoves; }

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            const FixedTrainerTemplate *asFixedTrainer() const noexcept override { return this; }
            const FixedNicknameTemplate *asFixedNickname() const noexcept override { return this; }
            const RelearnTemplate *asRelearn() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterTrade8>(*this); }

            uint32_t ID32() const noexcept { return properties.ID32; }
            uint8_t trainerGender() const noexcept { return properties.trainerGender; }
            uint8_t dynamaxLevel() const noexcept { return properties.dynamaxLevel; }
            uint8_t trainerMemory() const noexcept { return properties.trainerMemory; }
            uint16_t trainerMemoryVariable() const noexcept { return properties.trainerMemoryVariable; }
            uint8_t trainerMemoryFeeling() const noexcept { return properties.trainerMemoryFeeling; }
            uint8_t trainerMemoryIntensity() const noexcept { return properties.trainerMemoryIntensity; }

        private:
            Properties properties;
        };

        /// A LanguageNamesSwitch entry, or u"" past its end or where it is empty.
        std::u16string getLanguageName(const LanguageNamesSwitch *names, int language);
    }
}

#endif
