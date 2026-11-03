/**
 * A Scarlet/Violet distributed mass outbreak: one species over a set of met locations. Ported from
 * PKHeX PKHeX.Core/Legality/Encounters/Templates/Gen9/EncounterOutbreak9.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTEROUTBREAK9_H
#define LEGALITY_ENCOUNTERS_ENCOUNTEROUTBREAK9_H

#include <cstdint>

#include "Legality/Encounters/EncounterRules9.h"
#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterOutbreak9 final : public Encounterable, public EncounterMatchTemplate
        {
        public:
            /// PKHeX EncounterOutbreak9.Unset: the outbreak grants no ribbon.
            static constexpr uint8_t RIBBON_UNSET = 0xFF;

            struct Properties
            {
                uint16_t speciesId = 0;
                uint8_t formId = 0;
                uint8_t gender = GENDER_RANDOM;
                uint8_t levelMin = 0;
                uint8_t levelMax = 0;
                /// A RibbonIndex the outbreak always grants, or RIBBON_UNSET.
                uint8_t ribbon = RIBBON_UNSET;
                uint8_t weather = WEATHER9_NONE;
                bool isForcedScaleRange = false;
                uint8_t scaleMin = 0;
                uint8_t scaleMax = 0;
                bool isShiny = false;
                /// The met locations, as bit N of metFlagsLow/High meaning location metBase + N.
                uint8_t metBase = 0;
                uint64_t metFlagsLow = 0;
                uint64_t metFlagsHigh = 0;
            };

            explicit constexpr EncounterOutbreak9(const Properties &properties) noexcept : properties(properties) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Outbreak9; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Outbreak9; }
            EncounterType encounterType() const noexcept override { return EncounterType::Wild; }
            std::string name() const override { return "Distribution Outbreak Encounter"; }

            uint16_t speciesId() const noexcept override { return properties.speciesId; }
            uint8_t formId() const noexcept override { return properties.formId; }
            uint8_t levelMin() const noexcept override { return properties.levelMin; }
            uint8_t levelMax() const noexcept override { return properties.levelMax; }
            Enums::GameVersion version() const noexcept override { return Enums::GameVersion::SV; }
            uint8_t generation() const noexcept override { return 9; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::SV; }
            uint16_t location() const noexcept override;
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return false; }
            RNG::Shiny shiny() const noexcept override { return properties.isShiny ? RNG::Shiny::Always : RNG::Shiny::Random; }
            uint8_t fixedGender() const noexcept override { return properties.gender; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &pokemon) const override;

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterOutbreak9>(*this); }

            uint8_t ribbon() const noexcept { return properties.ribbon; }
            /// PKHeX EncounterOutbreak9.Weather (WEATHER9_* flags).
            uint8_t weather() const noexcept { return properties.weather; }
            bool isForcedScaleRange() const noexcept { return properties.isForcedScaleRange; }
            uint8_t scaleMin() const noexcept { return properties.scaleMin; }
            uint8_t scaleMax() const noexcept { return properties.scaleMax; }
            /// PKHeX IsMetLocationMatch.
            bool isMetLocationMatch(uint16_t metLocation) const noexcept;

        private:
            Properties properties;
        };
    }
}

#endif
