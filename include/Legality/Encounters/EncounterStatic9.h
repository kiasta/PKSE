/**
 * A Scarlet/Violet static encounter or gift. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen9/EncounterStatic9.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERSTATIC9_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERSTATIC9_H

#include <cstdint>

#include "Legality/Encounters/EncounterRules9.h"
#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterStatic9 final : public Encounterable, public EncounterMatchTemplate, public MovesetTemplate
        {
        public:
            /// PKHeX's init properties, with PKHeX's defaults.
            struct Properties
            {
                Enums::GameVersion version = Enums::GameVersion::SV;
                Enums::Ball fixedBall = Enums::Ball::None;
                uint16_t location = 0;
                uint16_t eggLocation = 0;
                uint16_t speciesId = 0;
                uint8_t level = 0;
                uint8_t formId = 0;
                uint8_t gender = GENDER_RANDOM;
                RNG::AbilityPermission ability = RNG::AbilityPermission::Any12;
                uint8_t flawlessIVCount = 0;
                RNG::Shiny shiny = RNG::Shiny::Random;
                Moveset moves{};
                RNG::IndividualValueSet individualValues{};
                uint8_t nature = NATURE_RANDOM;
                RNG::GemType teraType = RNG::GemType::Default;
                /// The fixed size scalar; 0 when the size is random.
                uint8_t scalarValue = 0;
                bool isTitan = false;
                bool isFatefulEncounter = false;
            };

            explicit constexpr EncounterStatic9(const Properties &properties) noexcept : properties(properties) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Static9; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Static9; }
            EncounterType encounterType() const noexcept override { return isGift() ? EncounterType::Gift : EncounterType::Static; }
            std::string name() const override { return "Static Encounter"; }

            uint16_t speciesId() const noexcept override { return properties.speciesId; }
            uint8_t formId() const noexcept override { return properties.formId; }
            uint8_t levelMin() const noexcept override { return properties.level; }
            uint8_t levelMax() const noexcept override { return properties.level; }
            Enums::GameVersion version() const noexcept override { return properties.version; }
            uint8_t generation() const noexcept override { return 9; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::SV; }
            uint16_t location() const noexcept override { return properties.location; }
            uint16_t eggLocation() const noexcept override { return properties.eggLocation; }
            bool isEgg() const noexcept override { return properties.eggLocation != 0; }
            Enums::Ball fixedBall() const noexcept override { return properties.fixedBall; }
            RNG::Shiny shiny() const noexcept override { return properties.shiny; }
            RNG::AbilityPermission ability() const noexcept override { return properties.ability; }
            uint8_t fixedGender() const noexcept override { return properties.gender; }
            uint8_t fixedNature() const noexcept override { return properties.nature; }
            RNG::IndividualValueSet fixedIVs() const noexcept override { return properties.individualValues; }
            uint8_t flawlessIVCount() const noexcept override { return properties.flawlessIVCount; }
            bool isFatefulEncounter() const noexcept override { return properties.isFatefulEncounter; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &pokemon) const override;
            Moveset moves() const noexcept override { return properties.moves; }

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            const MovesetTemplate *asMoveset() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterStatic9>(*this); }

            bool isGift() const noexcept { return properties.fixedBall != Enums::Ball::None; }
            RNG::GemType teraType() const noexcept { return properties.teraType; }
            uint8_t scalarValue() const noexcept { return properties.scalarValue; }
            bool isTitan() const noexcept { return properties.isTitan; }
            /// PKHeX ScriptedYungoos: the level 2 Yungoos of Inlet Grotto, which writes no HOME language memory.
            bool isScriptedYungoos() const noexcept;
            /// PKHeX GiftWithLanguage: every gift but the scripted Yungoos sets the handling language too.
            bool isGiftWithLanguage() const noexcept { return isGift() && !isScriptedYungoos(); }
            /// PKHeX StarterBoxLegend: the ride Koraidon or Miraidon.
            bool isStarterBoxLegend() const noexcept;
            /// PKHeX RibbonMarkCrafty / IsMissingExtraMark: the shiny Munchlax always carries the Crafty Mark.
            bool hasRequiredCraftyMark() const noexcept;

        private:
            bool isMatchEggLocationInternal(const Pokemon::Pokemon &pokemon) const;
            bool isMatchLocationExact(const Pokemon::Pokemon &pokemon) const noexcept;
            bool isMatchPartial(const Pokemon::Pokemon &pokemon) const;

            Properties properties;
        };
    }
}

#endif
