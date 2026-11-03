/**
 * A Legends: Z-A in-game trade. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen9a/EncounterTrade9a.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERTRADE9A_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERTRADE9A_H

#include <array>
#include <cstdint>

#include "Legality/Encounters/EncounterLocations.h"
#include "Legality/Encounters/EncounterRules9a.h"
#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        /// One name per language id (0 .. 11, Latin American Spanish included); 0 and 6 are empty.
        using LanguageNames9a = std::array<const char16_t *, 12>;

        class EncounterTrade9a final : public Encounterable, public EncounterMatchTemplate, public MovesetTemplate, public FixedTrainerTemplate, public FixedNicknameTemplate
        {
        public:
            /// PKHeX EncounterTrade9a.Scale: every trade is scale 128.
            static constexpr uint8_t SCALE = 128;

            struct Properties
            {
                const LanguageNames9a *nicknames = nullptr;
                const LanguageNames9a *trainerNames = nullptr;
                uint16_t speciesId = 0;
                uint8_t formId = 0;
                uint8_t level = 0;
                uint32_t ID32 = 0;
                uint8_t trainerGender = 0;
                uint8_t gender = 0;
                uint8_t nature = NATURE_RANDOM;
                uint8_t flawlessIVCount = 0;
                RNG::IndividualValueSet individualValues{};
                Moveset moves{};
            };

            explicit constexpr EncounterTrade9a(const Properties &properties) noexcept : properties(properties) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Trade9a; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Trade9a; }
            EncounterType encounterType() const noexcept override { return EncounterType::Trade; }
            std::string name() const override { return "Trade Encounter"; }

            uint16_t speciesId() const noexcept override { return properties.speciesId; }
            uint8_t formId() const noexcept override { return properties.formId; }
            uint8_t levelMin() const noexcept override { return properties.level; }
            uint8_t levelMax() const noexcept override { return properties.level; }
            Enums::GameVersion version() const noexcept override { return Enums::GameVersion::ZA; }
            uint8_t generation() const noexcept override { return 9; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::ZA; }
            uint16_t location() const noexcept override { return LOCATION_LINK_TRADE6_NPC; }
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return false; }
            Enums::Ball fixedBall() const noexcept override { return Enums::Ball::Poke; }
            RNG::Shiny shiny() const noexcept override { return RNG::Shiny::Never; }
            uint8_t fixedGender() const noexcept override { return properties.gender; }
            uint8_t fixedNature() const noexcept override { return properties.nature; }
            RNG::IndividualValueSet fixedIVs() const noexcept override { return properties.individualValues; }
            uint8_t flawlessIVCount() const noexcept override { return properties.flawlessIVCount; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &pokemon) const override;
            Moveset moves() const noexcept override { return properties.moves; }
            bool isFixedTrainer() const noexcept override { return true; }
            bool isTrainerMatch(const Pokemon::Pokemon &pokemon, const std::u16string &trainerName, int language) const override;
            bool isFixedNickname() const noexcept override { return true; }
            bool isNicknameMatch(const Pokemon::Pokemon &pokemon, const std::u16string &nickname, int language) const override;
            std::u16string getNickname(int language) const override;

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            const MovesetTemplate *asMoveset() const noexcept override { return this; }
            const FixedTrainerTemplate *asFixedTrainer() const noexcept override { return this; }
            const FixedNicknameTemplate *asFixedNickname() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterTrade9a>(*this); }

            uint32_t ID32() const noexcept { return properties.ID32; }
            uint8_t trainerGender() const noexcept { return properties.trainerGender; }
            /// PKHeX GetParams.
            RNG::GenerateParam9a getParameters() const noexcept;
            /// PKHeX TryGetSeed: Success or Invalid, never Ignore.
            SeedCorrelationResult tryGetSeed(const Pokemon::Pokemon &pokemon) const noexcept;

        private:
            Properties properties;
        };
    }
}

#endif
