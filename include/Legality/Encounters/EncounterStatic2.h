/**
 * A Gen 2 static encounter or gift (Gold, Silver, Crystal), including Crystal's Odd Egg. Ported from
 * PKHeX PKHeX.Core/Legality/Encounters/Templates/Gen2/EncounterStatic2.cs.
 *
 * PKSE has no GameVersion for PKHeX's GS lump; tools/gen_encounters2.py emits it as GD, which every
 * rule here answers the same way (none asks for GS itself).
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERSTATIC2_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERSTATIC2_H

#include <cstdint>

#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterStatic2 final : public Encounterable, public EncounterMatchTemplate, public MovesetTemplate
        {
        public:
            struct Properties
            {
                uint16_t speciesId = 0;
                uint8_t level = 0;
                Enums::GameVersion version = Enums::GameVersion::Invalid;
                RNG::Shiny shiny = RNG::Shiny::Random;
                uint8_t location = 0;
                uint8_t gender = GENDER_RANDOM;
                RNG::IndividualValueSet individualValues{};
                Moveset moves{};
                bool isEgg = false;
            };

            explicit constexpr EncounterStatic2(const Properties &properties) noexcept : properties(properties) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Static2; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Static2; }
            EncounterType encounterType() const noexcept override { return properties.isEgg ? EncounterType::Egg : EncounterType::Static; }
            std::string name() const override { return "Static Encounter"; }

            uint16_t speciesId() const noexcept override { return properties.speciesId; }
            uint8_t formId() const noexcept override { return 0; }
            uint8_t levelMin() const noexcept override { return properties.level; }
            uint8_t levelMax() const noexcept override { return properties.level; }
            Enums::GameVersion version() const noexcept override { return properties.version; }
            uint8_t generation() const noexcept override { return 2; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::GSC; }
            uint16_t location() const noexcept override { return properties.location; }
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return properties.isEgg; }
            Enums::Ball fixedBall() const noexcept override { return Enums::Ball::Poke; }
            RNG::Shiny shiny() const noexcept override { return properties.shiny; }
            RNG::AbilityPermission ability() const noexcept override;
            uint8_t fixedGender() const noexcept override { return properties.gender; }
            RNG::IndividualValueSet fixedIVs() const noexcept override { return properties.individualValues; }
            uint8_t eggCycles() const noexcept override { return isDizzyPunchEgg() ? 20 : 0; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &) const override { return EncounterMatchRating::Match; }
            Moveset moves() const noexcept override { return properties.moves; }

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            const MovesetTemplate *asMoveset() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterStatic2>(*this); }

            /// PKHeX IsDizzyPunchEgg: the Odd Egg, the only Gen 2 egg with fixed moves.
            bool isDizzyPunchEgg() const noexcept { return properties.isEgg && properties.moves.hasMoves(); }
            /// PKHeX IsRoaming.
            bool isRoaming() const noexcept;

        private:
            bool isMatchEggLocationInternal(const Pokemon::Pokemon &pokemon) const;
            bool isMatchLocation(const Pokemon::Pokemon &pokemon) const;
            bool isMatchLevel(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const;

            Properties properties;
        };

        /// PKHeX EncounterStatic2 / EncounterGift2 IsMatchEggLocationInternal: the record's Gen 2 met
        /// data against a template that is or is not an egg. A gift egg also caps an unhatched egg's
        /// friendship (its remaining hatch cycles) at `maximumEggFriendship`; -1 skips that rule.
        bool isMatchEggLocationGameBoy(const Pokemon::Pokemon &pokemon, bool templateIsEgg, int maximumEggFriendship);
    }
}

#endif
