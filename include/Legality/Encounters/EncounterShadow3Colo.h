/**
 * A Pokemon Colosseum shadow Pokemon (and the Japanese e-Reader shadows), snagged from a trainer whose team was
 * generated first under nature/gender locks. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen3/Colo/EncounterShadow3Colo.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERSHADOW3COLO_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERSHADOW3COLO_H

#include <cstdint>
#include <span>

#include "Legality/Encounters/EncounterRules3GC.h"
#include "Legality/Encounters/Encounterable.h"
#include "Legality/RNG/LockFinder.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterShadow3Colo final : public Encounterable, public EncounterMatchTemplate, public RandomCorrelationTemplate, public MovesetTemplate
        {
        public:
            struct Properties
            {
                /// PKHeX Index: the shadow Pokemon's number in the game's own list.
                uint8_t shadowIndex = 0;
                /// PKHeX Gauge: the heart gauge size.
                uint16_t heartGauge = 0;
                /// PKHeX PartyPrior: the locked teams that may have been generated before it.
                std::span<const RNG::TeamLock> partyPrior{};
                uint16_t speciesId = 0;
                uint8_t level = 0;
                uint8_t location = 0;
                Moveset moves{};
            };

            explicit constexpr EncounterShadow3Colo(const Properties &properties) noexcept : properties(properties) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Shadow3Colo; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Shadow3Colo; }
            EncounterType encounterType() const noexcept override { return EncounterType::Static; }
            std::string name() const override;

            uint16_t speciesId() const noexcept override { return properties.speciesId; }
            uint8_t formId() const noexcept override { return 0; }
            uint8_t levelMin() const noexcept override { return properties.level; }
            uint8_t levelMax() const noexcept override { return properties.level; }
            Enums::GameVersion version() const noexcept override { return GAME_VERSION_COLOSSEUM_XD; }
            uint8_t generation() const noexcept override { return 3; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::RSE; }
            uint16_t location() const noexcept override { return properties.location; }
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return false; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &pokemon) const override;
            RandomCorrelationRating isCompatible(RNG::PIDType type, const Pokemon::Pokemon &pokemon) const override;
            RNG::PIDType getSuggestedCorrelation() const noexcept override;
            Moveset moves() const noexcept override { return properties.moves; }

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            const RandomCorrelationTemplate *asRandomCorrelation() const noexcept override { return this; }
            const MovesetTemplate *asMoveset() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterShadow3Colo>(*this); }

            /// PKHeX IShadow3.PartyPrior.
            std::span<const RNG::TeamLock> partyPrior() const noexcept { return properties.partyPrior; }
            uint8_t shadowIndex() const noexcept { return properties.shadowIndex; }
            uint16_t heartGauge() const noexcept { return properties.heartGauge; }
            /// PKHeX IsEReader: met in the Card e Room, which only the Japanese game has.
            bool isEReader() const noexcept;

        private:
            Properties properties;
        };
    }
}

#endif
