/**
 * A Scarlet/Violet 7-star (Mightiest Mark) Tera Raid encounter. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen9/EncounterMight9.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERMIGHT9_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERMIGHT9_H

#include <cstdint>

#include "Legality/Encounters/EncounterDist9.h"
#include "Legality/Encounters/EncounterRules9.h"
#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterMight9 final : public Encounterable, public EncounterMatchTemplate, public MovesetTemplate
        {
        public:
            struct Properties
            {
                Moveset moves{};
                RNG::IndividualValueSet individualValues{};
                uint16_t speciesId = 0;
                uint8_t formId = 0;
                uint8_t level = 0;
                uint8_t gender = GENDER_RANDOM;
                uint8_t flawlessIVCount = 0;
                RNG::AbilityPermission ability = RNG::AbilityPermission::Any12;
                RNG::Shiny shiny = RNG::Shiny::Random;
                uint8_t nature = NATURE_RANDOM;
                RNG::GemType teraType = RNG::GemType::Default;
                uint8_t raidIndex = 0;
                uint8_t stars = 0;
                uint8_t randomRate = 0;
                RNG::SizeType9 scaleType = RNG::SizeType9::RANDOM;
                uint8_t scale = 0;
                RaidStageRates9 rates{};
            };

            explicit constexpr EncounterMight9(const Properties &properties) noexcept : properties(properties) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Might9; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Might9; }
            EncounterType encounterType() const noexcept override { return EncounterType::Raid; }
            std::string name() const override { return "7-Star Tera Raid Encounter"; }

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
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterMight9>(*this); }

            RNG::GemType teraType() const noexcept { return properties.teraType; }
            uint8_t stars() const noexcept { return properties.stars; }
            uint8_t raidIndex() const noexcept { return properties.raidIndex; }
            bool isDistribution() const noexcept { return properties.raidIndex != 0; }
            const RaidStageRates9 &rates() const noexcept { return properties.rates; }
            bool canBeEncountered(uint32_t seed) const noexcept { return getRaidProgressMaximum9(properties.rates, properties.randomRate, seed) >= 0; }

        private:
            bool isMatchPartial(const Pokemon::Pokemon &pokemon) const;
            uint8_t getGenderRatio() const noexcept;

            Properties properties;
        };
    }
}

#endif
