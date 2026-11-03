/**
 * A Gen 1 in-game trade, whose catch rate is the received species' own. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen1/EncounterTrade1.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERTRADE1_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERTRADE1_H

#include <cstdint>

#include "Legality/Encounters/EncounterTrade3.h"
#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterTrade1 final : public Encounterable, public EncounterMatchTemplate, public FixedTrainerTemplate, public FixedNicknameTemplate
        {
        public:
            constexpr EncounterTrade1(const LanguageNames *nicknames, uint16_t speciesId, Enums::GameVersion version, uint8_t levelMinRBY, uint8_t levelMinGSC, bool evolveOnTrade) noexcept : nicknames(nicknames), tradeSpeciesId(speciesId), tradeVersion(version), tradeLevelMinRBY(levelMinRBY), tradeLevelMinGSC(levelMinGSC), tradeEvolveOnTrade(evolveOnTrade) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Trade1; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Trade1; }
            EncounterType encounterType() const noexcept override { return EncounterType::Trade; }
            std::string name() const override { return "In-game Trade"; }

            uint16_t speciesId() const noexcept override { return tradeSpeciesId; }
            uint8_t formId() const noexcept override { return 0; }
            uint8_t levelMin() const noexcept override { return canObtainMinGSC() ? tradeLevelMinGSC : tradeLevelMinRBY; }
            uint8_t levelMax() const noexcept override { return 100; }
            Enums::GameVersion version() const noexcept override { return tradeVersion; }
            uint8_t generation() const noexcept override { return 1; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::RBY; }
            uint16_t location() const noexcept override { return 0; }
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return false; }
            Enums::Ball fixedBall() const noexcept override { return Enums::Ball::Poke; }
            RNG::AbilityPermission ability() const noexcept override;

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &pokemon) const override;
            bool isFixedTrainer() const noexcept override { return true; }
            bool isTrainerMatch(const Pokemon::Pokemon &pokemon, const std::u16string &trainerName, int language) const override;
            bool isFixedNickname() const noexcept override { return true; }
            bool isNicknameMatch(const Pokemon::Pokemon &pokemon, const std::u16string &nickname, int language) const override;
            std::u16string getNickname(int language) const override;

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            const FixedTrainerTemplate *asFixedTrainer() const noexcept override { return this; }
            const FixedNicknameTemplate *asFixedNickname() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterTrade1>(*this); }

            bool evolveOnTrade() const noexcept { return tradeEvolveOnTrade; }
            uint8_t levelMinRBY() const noexcept { return tradeLevelMinRBY; }
            uint8_t levelMinGSC() const noexcept { return tradeLevelMinGSC; }

        private:
            /// PKHeX CanObtainMinGSC (ParseSettings.AllowGBStadium2 is PKHeX's default, false).
            bool canObtainMinGSC() const noexcept;
            bool isMatchLevel(const Pokemon::Pokemon &pokemon, uint8_t level) const noexcept;
            bool isMatchPartial(const Pokemon::Pokemon &pokemon) const;
            bool isNicknameValid(const Pokemon::Pokemon &pokemon, const std::u16string &nickname) const;

            const LanguageNames *nicknames;
            uint16_t tradeSpeciesId;
            Enums::GameVersion tradeVersion;
            uint8_t tradeLevelMinRBY;
            uint8_t tradeLevelMinGSC;
            bool tradeEvolveOnTrade;
        };
    }
}

#endif
