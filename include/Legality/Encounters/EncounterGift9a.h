/**
 * A Legends: Z-A gift Pokemon: starters, fossils, side-mission rewards and the gifts with a fixed
 * original trainer. Ported from PKHeX PKHeX.Core/Legality/Encounters/Templates/Gen9a/EncounterGift9a.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERGIFT9A_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERGIFT9A_H

#include <cstdint>

#include "Legality/Encounters/EncounterRules9a.h"
#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        /// PKHeX TrainerGift9a: whose Pokemon a gift is.
        enum class TrainerGift9a : uint8_t
        {
            None = 0,
            Lucario,
            Floette,
            Stunfisk,
            Gimmighoul,
            Magearna,
        };

        class EncounterGift9a final : public Encounterable, public EncounterMatchTemplate, public MovesetTemplate, public FixedTrainerTemplate, public FixedNicknameTemplate
        {
        public:
            /// PKHeX's constructor arguments and init properties, with PKHeX's defaults.
            struct Properties
            {
                uint16_t speciesId = 0;
                uint8_t formId = 0;
                uint8_t level = 0;
                /// The fixed scale; 0 when the scale is random.
                uint8_t scale = 0;
                uint16_t location = 0;
                uint8_t gender = GENDER_RANDOM;
                uint8_t nature = NATURE_RANDOM;
                uint8_t flawlessIVCount = 0;
                RNG::IndividualValueSet individualValues{};
                Moveset moves{};
                RNG::Shiny shiny = RNG::Shiny::Never;
                bool isAlpha = false;
                TrainerGift9a trainer = TrainerGift9a::None;
            };

            explicit constexpr EncounterGift9a(const Properties &properties) noexcept : properties(properties) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Gift9a; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Gift9a; }
            EncounterType encounterType() const noexcept override { return EncounterType::Gift; }
            std::string name() const override { return "Gift Encounter"; }

            uint16_t speciesId() const noexcept override { return properties.speciesId; }
            uint8_t formId() const noexcept override { return properties.formId; }
            uint8_t levelMin() const noexcept override { return properties.level; }
            uint8_t levelMax() const noexcept override { return properties.level; }
            Enums::GameVersion version() const noexcept override { return Enums::GameVersion::ZA; }
            uint8_t generation() const noexcept override { return 9; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::ZA; }
            uint16_t location() const noexcept override { return properties.location; }
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return false; }
            Enums::Ball fixedBall() const noexcept override { return Enums::Ball::Poke; }
            RNG::Shiny shiny() const noexcept override { return properties.shiny; }
            uint8_t fixedGender() const noexcept override { return properties.gender; }
            uint8_t fixedNature() const noexcept override { return properties.nature; }
            RNG::IndividualValueSet fixedIVs() const noexcept override { return properties.individualValues; }
            uint8_t flawlessIVCount() const noexcept override { return properties.flawlessIVCount; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &pokemon) const override;
            Moveset moves() const noexcept override { return properties.moves; }
            bool isFixedTrainer() const noexcept override { return properties.trainer != TrainerGift9a::None; }
            /// PKHeX reads the record's own language here, not `language`.
            bool isTrainerMatch(const Pokemon::Pokemon &pokemon, const std::u16string &trainerName, int language) const override;
            bool isFixedNickname() const noexcept override { return properties.trainer == TrainerGift9a::Gimmighoul; }
            bool isNicknameMatch(const Pokemon::Pokemon &pokemon, const std::u16string &nickname, int language) const override;
            /// The fixed nickname; empty for a gift that keeps its species name.
            std::u16string getNickname(int language) const override;

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            const MovesetTemplate *asMoveset() const noexcept override { return this; }
            const FixedTrainerTemplate *asFixedTrainer() const noexcept override { return this; }
            const FixedNicknameTemplate *asFixedNickname() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterGift9a>(*this); }

            bool isAlpha() const noexcept { return properties.isAlpha; }
            TrainerGift9a trainer() const noexcept { return properties.trainer; }
            /// PKHeX TryGetSeed.
            SeedCorrelationResult tryGetSeed(const Pokemon::Pokemon &pokemon) const noexcept;
            /// PKHeX GetParams.
            RNG::GenerateParam9a getParameters() const noexcept;

        private:
            Properties properties;
        };
    }
}

#endif
