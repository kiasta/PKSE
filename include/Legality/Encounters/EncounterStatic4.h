/**
 * A Gen 4 static encounter, gift or gift egg (Diamond, Pearl, Platinum, HeartGold, SoulSilver),
 * roamers included. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen4/EncounterStatic4.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERSTATIC4_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERSTATIC4_H

#include <cstdint>

#include "Legality/Encounters/Encounterable.h"
#include "Legality/Encounters/GroundTileAllowed.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterStatic4 final : public Encounterable, public EncounterMatchTemplate, public MovesetTemplate, public GroundTypeTileTemplate, public RandomCorrelationTemplate
        {
        public:
            /// PKHeX's constructor argument and init properties, with PKHeX's defaults.
            struct Properties
            {
                /// A single game, DP or HGSS; Any stands for PKHeX's DPPt lump, which PKSE has no group for.
                Enums::GameVersion version = Enums::GameVersion::Invalid;
                uint16_t speciesId = 0;
                uint8_t level = 0;
                uint8_t location = 0;
                uint8_t formId = 0;
                Enums::Ball fixedBall = Enums::Ball::None;
                bool isFatefulEncounter = false;
                RNG::AbilityPermission ability = RNG::AbilityPermission::Any12;
                RNG::Shiny shiny = RNG::Shiny::Random;
                uint16_t eggLocation = 0;
                uint8_t gender = GENDER_RANDOM;
                bool isRoaming = false;
                uint32_t groundTile = GROUND_TILE_NONE;
                uint16_t heldItem = 0;
                uint8_t nature = NATURE_RANDOM;
                Moveset moves{};
            };

            explicit constexpr EncounterStatic4(const Properties &properties) noexcept : properties(properties) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Static4; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Static4; }
            EncounterType encounterType() const noexcept override { return isGift() ? EncounterType::Gift : EncounterType::Static; }
            std::string name() const override { return "Static Encounter"; }

            uint16_t speciesId() const noexcept override { return properties.speciesId; }
            uint8_t formId() const noexcept override { return properties.formId; }
            uint8_t levelMin() const noexcept override { return properties.level; }
            uint8_t levelMax() const noexcept override { return properties.level; }
            Enums::GameVersion version() const noexcept override { return properties.version; }
            uint8_t generation() const noexcept override { return 4; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::DP; }
            uint16_t location() const noexcept override { return properties.location; }
            uint16_t eggLocation() const noexcept override { return properties.eggLocation; }
            bool isEgg() const noexcept override { return properties.eggLocation != 0; }
            Enums::Ball fixedBall() const noexcept override { return properties.fixedBall; }
            RNG::Shiny shiny() const noexcept override { return properties.shiny; }
            RNG::AbilityPermission ability() const noexcept override { return properties.ability; }
            uint8_t fixedGender() const noexcept override { return properties.gender; }
            uint8_t fixedNature() const noexcept override { return properties.nature; }
            bool isFatefulEncounter() const noexcept override { return properties.isFatefulEncounter; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &pokemon) const override;
            Moveset moves() const noexcept override { return properties.moves; }
            uint32_t groundTile() const noexcept override { return properties.groundTile; }
            RandomCorrelationRating isCompatible(RNG::PIDType type, const Pokemon::Pokemon &pokemon) const override;
            RNG::PIDType getSuggestedCorrelation() const noexcept override;

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            const MovesetTemplate *asMoveset() const noexcept override { return this; }
            const GroundTypeTileTemplate *asGroundTypeTile() const noexcept override { return this; }
            const RandomCorrelationTemplate *asRandomCorrelation() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterStatic4>(*this); }

            bool isRoaming() const noexcept { return properties.isRoaming; }
            uint16_t heldItem() const noexcept { return properties.heldItem; }
            bool isGift() const noexcept { return properties.fixedBall == Enums::Ball::Poke; }

            /// PKHeX IsMatchRoamerLocation: `location` is one of the bits of `permit` counted from `first`.
            static bool isMatchRoamerLocation(uint64_t permit, uint16_t location, int first) noexcept;

        private:
            bool isMatchLocation(const Pokemon::Pokemon &pokemon) const;
            bool isMatchEggLocationInternal(const Pokemon::Pokemon &pokemon) const;
            bool isMatchLevel(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const;

            Properties properties;
        };
    }
}

#endif
