/**
 * A Gen 2 event distribution (Pokemon Stadium 2 gifts, Pokemon Center New York, the GS Ball era). Ported
 * from PKHeX PKHeX.Core/Legality/Encounters/Templates/Gen2/EncounterGift2.cs.
 *
 * PKSE has no GameVersion for PKHeX's GS lump; a gift without a met location stands as GD, which every
 * rule here answers the same way.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERGIFT2_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERGIFT2_H

#include <cstdint>

#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterGift2 final : public Encounterable, public EncounterMatchTemplate, public MovesetTemplate
        {
        public:
            /// PKHeX EncounterGift2.LanguageRestriction.
            enum class LanguageRestriction : uint8_t
            {
                International = 0,
                Japanese = 1,
                English = 2,
                InternationalNotEnglish = 3,
            };

            /// PKHeX EncounterGift2.TrainerType.
            enum class TrainerType : uint8_t
            {
                Recipient = 0,
                GiftStadiumJPN = 1,
                GiftStadiumENG = 2,
                GiftStadiumINT = 3,
                PokemonCenterNewYork = 4,
            };

            /// PKHeX's constructor from the 12-byte event2.pkl record.
            constexpr EncounterGift2(uint16_t speciesId, uint8_t level, Moveset moves, uint8_t location, uint8_t currentLevel, uint8_t shinyFlag, uint8_t eggFlag, uint8_t language, uint8_t trainer) noexcept
                : giftSpeciesId(speciesId), giftLevel(level), giftMoves(moves), giftLocation(location), giftCurrentLevel(currentLevel),
                  giftShiny(shinyFlag == 0 ? RNG::Shiny::Random : RNG::Shiny::Always), giftEggCycles(eggFlag == 1 ? 10 : 0),
                  giftLanguage(static_cast<LanguageRestriction>(language)), giftTrainer(static_cast<TrainerType>(trainer))
            {
            }

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Gift2; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Gift2; }
            EncounterType encounterType() const noexcept override { return isEgg() ? EncounterType::Egg : EncounterType::Event; }
            std::string name() const override { return "GB Era Event Gift"; }

            uint16_t speciesId() const noexcept override { return giftSpeciesId; }
            uint8_t formId() const noexcept override { return 0; }
            uint8_t levelMin() const noexcept override { return giftLevel; }
            uint8_t levelMax() const noexcept override { return giftLevel; }
            Enums::GameVersion version() const noexcept override { return giftLocation != 0 ? Enums::GameVersion::C : Enums::GameVersion::GD; }
            uint8_t generation() const noexcept override { return 2; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::GSC; }
            uint16_t location() const noexcept override { return giftLocation; }
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return giftEggCycles != 0; }
            Enums::Ball fixedBall() const noexcept override { return Enums::Ball::Poke; }
            RNG::Shiny shiny() const noexcept override { return giftShiny; }
            RNG::AbilityPermission ability() const noexcept override { return RNG::AbilityPermission::OnlyHidden; }
            uint8_t eggCycles() const noexcept override { return giftEggCycles; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &) const override { return EncounterMatchRating::Match; }
            Moveset moves() const noexcept override { return giftMoves; }

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            const MovesetTemplate *asMoveset() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterGift2>(*this); }

            TrainerType trainer() const noexcept { return giftTrainer; }
            LanguageRestriction languageRestriction() const noexcept { return giftLanguage; }
            uint8_t currentLevel() const noexcept { return giftCurrentLevel; }

        private:
            bool isLanguageValid(int language) const noexcept;
            bool isTrainerNameValid(const Pokemon::Pokemon &pokemon) const;
            bool isTrainerIDValid(const Pokemon::Pokemon &pokemon) const noexcept;
            bool isMatchLocation(const Pokemon::Pokemon &pokemon) const;
            bool isMatchLevel(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const;

            uint16_t giftSpeciesId;
            uint8_t giftLevel;
            Moveset giftMoves;
            uint8_t giftLocation;
            uint8_t giftCurrentLevel;
            RNG::Shiny giftShiny;
            uint8_t giftEggCycles;
            LanguageRestriction giftLanguage;
            TrainerType giftTrainer;
        };
    }
}

#endif
