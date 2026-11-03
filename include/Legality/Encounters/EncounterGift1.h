/**
 * A Gen 1 event distribution (Pokemon Stadium gifts, the event Mews, the Virtual Console Mew). Ported
 * from PKHeX PKHeX.Core/Legality/Encounters/Templates/Gen1/EncounterGift1.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERGIFT1_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERGIFT1_H

#include <cstdint>

#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterGift1 final : public Encounterable, public EncounterMatchTemplate, public MovesetTemplate
        {
        public:
            /// PKHeX EncounterGift1.LanguageRestriction.
            enum class LanguageRestriction : uint8_t
            {
                Any = 0,
                Japanese = 1,
                International = 2,
            };

            /// PKHeX EncounterGift1.TrainerType.
            enum class TrainerType : uint8_t
            {
                Recipient = 0,
                VirtualConsoleMew = 1,
                Stadium = 2,
                EuropeTour = 3,
                JapanTour = 4,
            };

            /// PKHeX's constructor from the 8-byte event1.pkl record.
            constexpr EncounterGift1(uint16_t speciesId, uint8_t level, Moveset moves, uint8_t language, uint8_t trainer) noexcept : giftSpeciesId(speciesId), giftLevel(level), giftMoves(moves), giftLanguage(static_cast<LanguageRestriction>(language)), giftTrainer(static_cast<TrainerType>(trainer)) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Gift1; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Gift1; }
            EncounterType encounterType() const noexcept override { return EncounterType::Event; }
            std::string name() const override { return "Event Gift"; }

            uint16_t speciesId() const noexcept override { return giftSpeciesId; }
            uint8_t formId() const noexcept override { return 0; }
            uint8_t levelMin() const noexcept override { return giftLevel; }
            uint8_t levelMax() const noexcept override { return giftLevel; }
            /// PKHeX Stadium or RB. PKSE has neither, so both stand as RD (Red/Blue's rules); the
            /// Stadium case is told apart by isStadium().
            Enums::GameVersion version() const noexcept override { return Enums::GameVersion::RD; }
            uint8_t generation() const noexcept override { return 1; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::RBY; }
            uint16_t location() const noexcept override { return 0; }
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return false; }
            Enums::Ball fixedBall() const noexcept override { return Enums::Ball::Poke; }
            RNG::AbilityPermission ability() const noexcept override { return RNG::AbilityPermission::OnlyHidden; }
            RNG::IndividualValueSet fixedIVs() const noexcept override;

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &pokemon) const override;
            Moveset moves() const noexcept override { return giftMoves; }

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            const MovesetTemplate *asMoveset() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterGift1>(*this); }

            TrainerType trainer() const noexcept { return giftTrainer; }
            LanguageRestriction languageRestriction() const noexcept { return giftLanguage; }
            /// PKHeX `Version == GameVersion.Stadium`.
            bool isStadium() const noexcept { return giftTrainer == TrainerType::Stadium; }

        private:
            bool isTrainerNameValid(const Pokemon::Pokemon &pokemon) const;
            bool isTrainerIDValid(const Pokemon::Pokemon &pokemon) const noexcept;
            bool isCatchRateValid(uint8_t rate) const noexcept;

            uint16_t giftSpeciesId;
            uint8_t giftLevel;
            Moveset giftMoves;
            LanguageRestriction giftLanguage;
            TrainerType giftTrainer;
        };
    }
}

#endif
