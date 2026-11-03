/**
 * A Scarlet/Violet distributed (BCAT) Tera Raid encounter. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen9/EncounterDist9.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERDIST9_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERDIST9_H

#include <array>
#include <cstdint>

#include "Legality/Encounters/EncounterRules9.h"
#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        /// The opponent weights of a distributed raid at each of the four story-progress stages.
        struct RaidStageRates9
        {
            std::array<uint16_t, 4> minimumScarlet{};
            std::array<uint16_t, 4> minimumViolet{};
            std::array<uint16_t, 4> totalScarlet{};
            std::array<uint16_t, 4> totalViolet{};
        };

        /// PKHeX GetProgressMaximum: the highest stage whose roll of `seed` selects the raid; -1 for none.
        int getRaidProgressMaximum9(const RaidStageRates9 &rates, uint8_t randomRate, uint32_t seed) noexcept;

        class EncounterDist9 final : public Encounterable, public EncounterMatchTemplate, public MovesetTemplate
        {
        public:
            struct Properties
            {
                uint16_t speciesId = 0;
                uint8_t formId = 0;
                uint8_t gender = GENDER_RANDOM;
                RNG::AbilityPermission ability = RNG::AbilityPermission::Any12;
                uint8_t flawlessIVCount = 0;
                RNG::Shiny shiny = RNG::Shiny::Random;
                uint8_t nature = NATURE_RANDOM;
                uint8_t level = 0;
                Moveset moves{};
                RNG::IndividualValueSet individualValues{};
                RNG::GemType teraType = RNG::GemType::Default;
                uint8_t raidIndex = 0;
                uint8_t stars = 0;
                uint8_t randomRate = 0;
                RNG::SizeType9 scaleType = RNG::SizeType9::RANDOM;
                uint8_t scale = 0;
                RaidStageRates9 rates{};
            };

            explicit constexpr EncounterDist9(const Properties &properties) noexcept : properties(properties) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Dist9; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Dist9; }
            EncounterType encounterType() const noexcept override { return EncounterType::Raid; }
            std::string name() const override { return "Distribution Tera Raid Encounter"; }

            uint16_t speciesId() const noexcept override { return properties.speciesId; }
            uint8_t formId() const noexcept override { return properties.formId; }
            uint8_t levelMin() const noexcept override { return properties.level; }
            uint8_t levelMax() const noexcept override { return properties.level; }
            Enums::GameVersion version() const noexcept override { return Enums::GameVersion::SV; }
            uint8_t generation() const noexcept override { return 9; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::SV; }
            uint16_t location() const noexcept override { return LOCATION_TERA_CAVERN9; }
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return false; }
            RNG::Shiny shiny() const noexcept override { return properties.shiny; }
            RNG::AbilityPermission ability() const noexcept override { return properties.ability; }
            uint8_t fixedGender() const noexcept override { return properties.gender; }
            uint8_t fixedNature() const noexcept override { return properties.nature; }
            RNG::IndividualValueSet fixedIVs() const noexcept override { return properties.individualValues; }
            uint8_t flawlessIVCount() const noexcept override { return properties.flawlessIVCount; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &pokemon) const override;
            Moveset moves() const noexcept override { return properties.moves; }

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            const MovesetTemplate *asMoveset() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterDist9>(*this); }

            RNG::GemType teraType() const noexcept { return properties.teraType; }
            uint8_t stars() const noexcept { return properties.stars; }
            uint8_t raidIndex() const noexcept { return properties.raidIndex; }
            bool isDistribution() const noexcept { return properties.raidIndex != 0; }
            const RaidStageRates9 &rates() const noexcept { return properties.rates; }
            /// PKHeX CanBeEncountered.
            bool canBeEncountered(uint32_t seed) const noexcept { return getRaidProgressMaximum9(properties.rates, properties.randomRate, seed) >= 0; }

        private:
            bool isMatchPartial(const Pokemon::Pokemon &pokemon) const;

            Properties properties;
        };
    }
}

#endif
