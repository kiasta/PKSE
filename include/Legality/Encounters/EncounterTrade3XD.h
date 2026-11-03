/**
 * A Pokemon XD in-game trade (Duking's Meditite, Shuckle and Larvitar, and Hordel's Elekid). Ported
 * from PKHeX PKHeX.Core/Legality/Encounters/Templates/Gen3/XD/EncounterTrade3XD.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERTRADE3XD_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERTRADE3XD_H

#include <cstdint>

#include "Legality/Encounters/EncounterRules3GC.h"
#include "Legality/Encounters/EncounterTrade3.h"
#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterTrade3XD final : public Encounterable, public EncounterMatchTemplate, public RandomCorrelationTemplate, public FixedTrainerTemplate, public FixedNicknameTemplate, public MovesetTemplate
        {
        public:
            struct Properties
            {
                uint16_t speciesId = 0;
                uint8_t level = 0;
                const LanguageNames *trainerNames = nullptr;
                /// Null when the trade keeps the species name.
                const LanguageNames *nicknames = nullptr;
                uint8_t location = 0;
                uint16_t TID16 = 0;
                Moveset moves{};
            };

            explicit constexpr EncounterTrade3XD(const Properties &properties) noexcept : properties(properties) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Trade3XD; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Trade3XD; }
            EncounterType encounterType() const noexcept override { return EncounterType::Trade; }
            std::string name() const override { return "Trade Encounter"; }

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
            Enums::Ball fixedBall() const noexcept override { return Enums::Ball::Poke; }
            bool isFatefulEncounter() const noexcept override { return true; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &pokemon) const override;
            RandomCorrelationRating isCompatible(RNG::PIDType type, const Pokemon::Pokemon &) const override { return type == RNG::PIDType::ColosseumXD ? RandomCorrelationRating::Match : RandomCorrelationRating::Mismatch; }
            RNG::PIDType getSuggestedCorrelation() const noexcept override { return RNG::PIDType::ColosseumXD; }
            bool isFixedTrainer() const noexcept override { return true; }
            bool isTrainerMatch(const Pokemon::Pokemon &pokemon, const std::u16string &trainerName, int language) const override;
            bool isFixedNickname() const noexcept override { return properties.nicknames != nullptr; }
            bool isNicknameMatch(const Pokemon::Pokemon &pokemon, const std::u16string &nickname, int language) const override;
            std::u16string getNickname(int language) const override;
            Moveset moves() const noexcept override { return properties.moves; }

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            const RandomCorrelationTemplate *asRandomCorrelation() const noexcept override { return this; }
            const FixedTrainerTemplate *asFixedTrainer() const noexcept override { return this; }
            const FixedNicknameTemplate *asFixedNickname() const noexcept override { return this; }
            const MovesetTemplate *asMoveset() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterTrade3XD>(*this); }

            uint16_t TID16() const noexcept { return properties.TID16; }

        private:
            Properties properties;
        };
    }
}

#endif
