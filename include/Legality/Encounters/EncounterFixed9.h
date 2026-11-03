/**
 * A Scarlet/Violet fixed overworld spawn (a Tera-glowing Pokemon or a scripted symbol). Ported from
 * PKHeX PKHeX.Core/Legality/Encounters/Templates/Gen9/EncounterFixed9.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERFIXED9_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERFIXED9_H

#include <array>
#include <cstdint>

#include "Legality/Encounters/EncounterRules9.h"
#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterFixed9 final : public Encounterable, public EncounterMatchTemplate, public MovesetTemplate
        {
        public:
            struct Properties
            {
                uint16_t speciesId = 0;
                uint8_t formId = 0;
                uint8_t level = 0;
                uint8_t flawlessIVCount = 0;
                RNG::GemType teraType = RNG::GemType::Default;
                uint8_t gender = GENDER_RANDOM;
                RNG::AbilityPermission ability = RNG::AbilityPermission::Any12;
                Moveset moves{};
                /// The first is the template's location; the others are where else it can be met (0 ends the list).
                std::array<uint8_t, 4> locations{};
            };

            explicit constexpr EncounterFixed9(const Properties &properties) noexcept : properties(properties) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Fixed9; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Fixed9; }
            EncounterType encounterType() const noexcept override { return EncounterType::Static; }
            std::string name() const override { return "Fixed Behavior Static Encounter"; }

            uint16_t speciesId() const noexcept override { return properties.speciesId; }
            uint8_t formId() const noexcept override { return properties.formId; }
            uint8_t levelMin() const noexcept override { return properties.level; }
            uint8_t levelMax() const noexcept override { return properties.level; }
            Enums::GameVersion version() const noexcept override { return Enums::GameVersion::SV; }
            uint8_t generation() const noexcept override { return 9; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::SV; }
            uint16_t location() const noexcept override { return properties.locations[0]; }
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return false; }
            RNG::AbilityPermission ability() const noexcept override { return properties.ability; }
            uint8_t fixedGender() const noexcept override { return properties.gender; }
            uint8_t flawlessIVCount() const noexcept override { return properties.flawlessIVCount; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &pokemon) const override;
            Moveset moves() const noexcept override { return properties.moves; }

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            const MovesetTemplate *asMoveset() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterFixed9>(*this); }

            RNG::GemType teraType() const noexcept { return properties.teraType; }

        private:
            bool isMatchLocationExact(const Pokemon::Pokemon &pokemon) const noexcept;

            Properties properties;
        };
    }
}

#endif
