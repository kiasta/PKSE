/**
 * A Brilliant Diamond/Shining Pearl static encounter, gift, gift egg or roamer. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen8b/EncounterStatic8b.cs, IStaticCorrelation8b.cs and
 * StaticCorrelation8bRequirement.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERSTATIC8B_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERSTATIC8B_H

#include <cstdint>

#include "Legality/Encounters/EncounterLocations.h"
#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        /// PKHeX StaticCorrelation8bRequirement: whether a static's EC must be its roaming seed.
        enum class StaticCorrelation8bRequirement : uint8_t
        {
            CanBeEither,
            MustHave,
            MustNotHave,
        };

        class EncounterStatic8b final : public Encounterable, public EncounterMatchTemplate
        {
        public:
            /// PKHeX's init properties, with PKHeX's defaults.
            struct Properties
            {
                Enums::GameVersion version = Enums::GameVersion::BDSP;
                uint16_t speciesId = 0;
                uint16_t location = 0;
                uint16_t eggLocation = LOCATION_DEFAULT8B_NONE;
                uint8_t level = 0;
                Enums::Ball fixedBall = Enums::Ball::None;
                uint8_t flawlessIVCount = 0;
                bool isRoaming = false;
                RNG::AbilityPermission ability = RNG::AbilityPermission::Any12;
                RNG::Shiny shiny = RNG::Shiny::Random;
                bool isFatefulEncounter = false;
            };

            explicit constexpr EncounterStatic8b(const Properties &properties) noexcept : properties(properties) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Static8b; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Static8b; }
            EncounterType encounterType() const noexcept override { return properties.fixedBall != Enums::Ball::None ? EncounterType::Gift : EncounterType::Static; }
            std::string name() const override { return "Static Encounter"; }

            uint16_t speciesId() const noexcept override { return properties.speciesId; }
            uint8_t formId() const noexcept override { return 0; }
            uint8_t levelMin() const noexcept override { return properties.level; }
            uint8_t levelMax() const noexcept override { return properties.level; }
            Enums::GameVersion version() const noexcept override { return properties.version; }
            uint8_t generation() const noexcept override { return 8; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::BDSP; }
            uint16_t location() const noexcept override { return properties.location; }
            uint16_t eggLocation() const noexcept override { return properties.eggLocation; }
            bool isEgg() const noexcept override { return properties.eggLocation != LOCATION_DEFAULT8B_NONE; }
            Enums::Ball fixedBall() const noexcept override { return properties.fixedBall; }
            RNG::Shiny shiny() const noexcept override { return properties.shiny; }
            RNG::AbilityPermission ability() const noexcept override { return properties.ability; }
            uint8_t flawlessIVCount() const noexcept override { return properties.flawlessIVCount; }
            bool isFatefulEncounter() const noexcept override { return properties.isFatefulEncounter; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &) const override { return EncounterMatchRating::Match; }

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterStatic8b>(*this); }

            bool isRoaming() const noexcept { return properties.isRoaming; }
            /// PKHeX IStaticCorrelation8b.
            StaticCorrelation8bRequirement getRequirement(const Pokemon::Pokemon &pokemon) const noexcept;
            bool isStaticCorrelationCorrect(const Pokemon::Pokemon &pokemon) const noexcept;

        private:
            bool isMatchLocationExact(const Pokemon::Pokemon &pokemon) const noexcept;
            bool isMatchEggLocationExact(const Pokemon::Pokemon &pokemon) const noexcept;
            bool isMatchEggLocation(const Pokemon::Pokemon &pokemon) const;

            Properties properties;
        };
    }
}

#endif
