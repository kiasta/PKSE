/**
 * A Gen 3 static encounter or gift (Ruby, Sapphire, Emerald, FireRed, LeafGreen). Ported from
 * PKHeX PKHeX.Core/Legality/Encounters/Templates/Gen3/EncounterStatic3.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERSTATIC3_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERSTATIC3_H

#include <cstdint>

#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterStatic3 final : public Encounterable, public EncounterMatchTemplate, public RandomCorrelationTemplate, public MovesetTemplate
        {
        public:
            /// PKHeX's constructor arguments and init properties, with PKHeX's defaults.
            struct Properties
            {
                uint16_t speciesId = 0;
                uint8_t level = 0;
                Enums::GameVersion version = Enums::GameVersion::Invalid;
                uint8_t location = 0;
                uint8_t formId = 0;
                Enums::Ball fixedBall = Enums::Ball::None;
                bool isRoaming = false;
                bool isFatefulEncounter = false;
                bool isEgg = false;
                Moveset moves{};
            };

            explicit constexpr EncounterStatic3(const Properties &properties) noexcept : properties(properties) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Static3; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Static3; }
            EncounterType encounterType() const noexcept override { return properties.fixedBall == Enums::Ball::Poke ? EncounterType::Gift : EncounterType::Static; }
            std::string name() const override { return "Static Encounter"; }

            uint16_t speciesId() const noexcept override { return properties.speciesId; }
            uint8_t formId() const noexcept override { return properties.formId; }
            uint8_t levelMin() const noexcept override { return properties.level; }
            uint8_t levelMax() const noexcept override { return properties.level; }
            Enums::GameVersion version() const noexcept override { return properties.version; }
            uint8_t generation() const noexcept override { return 3; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::RSE; }
            uint16_t location() const noexcept override { return properties.location; }
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return properties.isEgg; }
            Enums::Ball fixedBall() const noexcept override { return properties.fixedBall; }
            bool isFatefulEncounter() const noexcept override { return properties.isFatefulEncounter; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &pokemon) const override;
            RandomCorrelationRating isCompatible(RNG::PIDType type, const Pokemon::Pokemon &pokemon) const override;
            RNG::PIDType getSuggestedCorrelation() const noexcept override { return RNG::PIDType::Method1; }
            Moveset moves() const noexcept override { return properties.moves; }

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            const RandomCorrelationTemplate *asRandomCorrelation() const noexcept override { return this; }
            const MovesetTemplate *asMoveset() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterStatic3>(*this); }

            bool isRoaming() const noexcept { return properties.isRoaming; }
            /// PKHeX IsRoamingTruncatedIVs.
            bool isRoamingTruncatedIVs() const noexcept { return properties.isRoaming && properties.version != Enums::GameVersion::EM; }

        private:
            Properties properties;
        };
    }
}

#endif
