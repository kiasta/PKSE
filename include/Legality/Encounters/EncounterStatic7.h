/**
 * A Sun/Moon or Ultra Sun/Ultra Moon static encounter: gifts, legendaries, Totems and the Island Scan
 * Pokemon. Ported from PKHeX PKHeX.Core/Legality/Encounters/Templates/Gen7/EncounterStatic7.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERSTATIC7_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERSTATIC7_H

#include <cstdint>

#include "Legality/Encounters/EncounterUtil.h"
#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterStatic7 final : public Encounterable, public EncounterMatchTemplate, public MovesetTemplate, public RelearnTemplate
        {
        public:
            /// PKHeX's init properties, with PKHeX's defaults.
            struct Properties
            {
                Enums::GameVersion version = Enums::GameVersion::SM;
                uint16_t speciesId = 0;
                uint8_t formId = 0;
                uint8_t level = 0;
                uint16_t location = 0;
                uint16_t eggLocation = 0;
                Moveset relearnMoves{};
                RNG::IndividualValueSet individualValues{};
                bool isFatefulEncounter = false;
                uint8_t flawlessIVCount = 0;
                RNG::Shiny shiny = RNG::Shiny::Random;
                RNG::AbilityPermission ability = RNG::AbilityPermission::Any12;
                uint8_t gender = GENDER_RANDOM;
                uint8_t nature = NATURE_RANDOM;
                Enums::Ball fixedBall = Enums::Ball::None;
            };

            explicit constexpr EncounterStatic7(const Properties &properties) noexcept : properties(properties) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Static7; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Static7; }
            EncounterType encounterType() const noexcept override { return properties.fixedBall != Enums::Ball::None ? EncounterType::Gift : EncounterType::Static; }
            std::string name() const override { return "Static Encounter"; }

            uint16_t speciesId() const noexcept override { return properties.speciesId; }
            uint8_t formId() const noexcept override { return properties.formId; }
            uint8_t levelMin() const noexcept override { return properties.level; }
            uint8_t levelMax() const noexcept override { return properties.level; }
            Enums::GameVersion version() const noexcept override { return properties.version; }
            uint8_t generation() const noexcept override { return 7; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::SM; }
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
            bool isRandomUnspecificForm() const noexcept override { return properties.formId >= FORM_DYNAMIC; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &pokemon) const override;
            /// PKHeX's Moves is the Relearn set.
            Moveset moves() const noexcept override { return properties.relearnMoves; }
            Moveset relearnMoves() const noexcept override { return properties.relearnMoves; }

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            const MovesetTemplate *asMoveset() const noexcept override { return this; }
            const RelearnTemplate *asRelearn() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterStatic7>(*this); }

            /// PKHeX RibbonWishing: Magearna comes with the Wishing Ribbon.
            bool hasWishingRibbon() const noexcept;
            /// PKHeX IsTotem / IsTotemNoTransfer.
            bool isTotem() const noexcept;
            bool isTotemNoTransfer() const noexcept;

        private:
            bool isMatchLocation(const Pokemon::Pokemon &pokemon) const noexcept;
            bool isMatchEggLocationInternal(const Pokemon::Pokemon &pokemon) const noexcept;
            bool isMatchForm(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const noexcept;

            Properties properties;
        };
    }
}

#endif
