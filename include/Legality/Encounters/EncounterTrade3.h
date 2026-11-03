/**
 * A Gen 3 in-game trade. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen3/EncounterTrade3.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERTRADE3_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERTRADE3_H

#include <array>
#include <cstdint>

#include "Legality/Encounters/EncounterLocations.h"
#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        /// One string per language id (0-7), the games' own text; nullptr where PKHeX has none.
        using LanguageNames = std::array<const char16_t *, 8>;

        class EncounterTrade3 final : public Encounterable, public EncounterMatchTemplate, public FixedTrainerTemplate, public FixedNicknameTemplate
        {
        public:
            struct Properties
            {
                const LanguageNames *nicknames = nullptr;
                const LanguageNames *trainerNames = nullptr;
                Enums::GameVersion version = Enums::GameVersion::Invalid;
                uint32_t PID = 0;
                uint16_t speciesId = 0;
                uint8_t level = 0;
                RNG::AbilityPermission ability = RNG::AbilityPermission::Any12;
                uint16_t TID16 = 0;
                uint16_t SID16 = 0;
                uint8_t trainerGender = 0;
                uint8_t gender = 0;
                RNG::IndividualValueSet individualValues{};
                /// Cool, Beauty, Cute, Smart, Tough, Sheen.
                std::array<uint8_t, 6> contestStats{};
            };

            explicit constexpr EncounterTrade3(const Properties &properties) noexcept : properties(properties) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Trade3; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Trade3; }
            EncounterType encounterType() const noexcept override { return EncounterType::Trade; }
            std::string name() const override { return "In-game Trade"; }

            uint16_t speciesId() const noexcept override { return properties.speciesId; }
            uint8_t formId() const noexcept override { return 0; }
            uint8_t levelMin() const noexcept override { return properties.level; }
            uint8_t levelMax() const noexcept override { return 100; }
            Enums::GameVersion version() const noexcept override { return properties.version; }
            uint8_t generation() const noexcept override { return 3; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::RSE; }
            uint16_t location() const noexcept override { return LOCATION_LINK_TRADE3_NPC; }
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return false; }
            Enums::Ball fixedBall() const noexcept override { return Enums::Ball::Poke; }
            RNG::Shiny shiny() const noexcept override { return RNG::Shiny::FixedValue; }
            RNG::AbilityPermission ability() const noexcept override { return properties.ability; }
            uint8_t fixedGender() const noexcept override { return properties.gender; }
            uint8_t fixedNature() const noexcept override { return static_cast<uint8_t>(properties.PID % 25); }
            RNG::IndividualValueSet fixedIVs() const noexcept override { return properties.individualValues; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &) const override { return EncounterMatchRating::Match; }
            bool isFixedTrainer() const noexcept override { return true; }
            bool isTrainerMatch(const Pokemon::Pokemon &pokemon, const std::u16string &trainerName, int language) const override;
            bool isFixedNickname() const noexcept override { return true; }
            bool isNicknameMatch(const Pokemon::Pokemon &pokemon, const std::u16string &nickname, int language) const override;
            std::u16string getNickname(int language) const override;

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            const FixedTrainerTemplate *asFixedTrainer() const noexcept override { return this; }
            const FixedNicknameTemplate *asFixedNickname() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterTrade3>(*this); }

            uint32_t PID() const noexcept { return properties.PID; }
            uint16_t TID16() const noexcept { return properties.TID16; }
            uint16_t SID16() const noexcept { return properties.SID16; }
            uint8_t trainerGender() const noexcept { return properties.trainerGender; }
            const std::array<uint8_t, 6> &contestStats() const noexcept { return properties.contestStats; }

        private:
            Properties properties;
        };

        /// PKHeX Legal.GetIsFixedIVSequenceValidSkipRand(IndividualValueSet, pk): IVs outside 0-31 are random.
        bool isFixedIVSequenceValidSkipRandom(const RNG::IndividualValueSet &individualValues, const Pokemon::Pokemon &pokemon) noexcept;
        /// PKHeX StringConverter345.TransferGlyphs34 for text: Pal Park reads Gen 3 kana glyphs as Japanese.
        std::u16string transferGlyphs34(const std::u16string &text, int language);
        /// PKHeX StringConverter345.TransferGlyphs56: the apostrophe Gen 6 writes.
        std::u16string transferGlyphs56(std::u16string text);
    }
}

#endif
