/**
 * A Brilliant Diamond/Shining Pearl in-game trade. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen8b/EncounterTrade8b.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERTRADE8B_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERTRADE8B_H

#include <array>
#include <cstdint>

#include "Legality/Encounters/EncounterLocations.h"
#include "Legality/Encounters/EncounterTrade8.h"
#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterTrade8b final : public Encounterable, public EncounterMatchTemplate, public FixedTrainerTemplate, public FixedNicknameTemplate, public MovesetTemplate
        {
        public:
            struct Properties
            {
                const LanguageNamesSwitch *nicknames = nullptr;
                const LanguageNamesSwitch *trainerNames = nullptr;
                Enums::GameVersion version = Enums::GameVersion::BDSP;
                uint16_t speciesId = 0;
                uint32_t encryptionConstant = 0;
                uint32_t PID = 0;
                uint8_t level = 0;
                RNG::AbilityPermission ability = RNG::AbilityPermission::Any12;
                uint8_t gender = 0;
                uint8_t trainerGender = 0;
                uint16_t TID16 = 0;
                RNG::IndividualValueSet individualValues{};
                Moveset moves{};
                uint8_t heightScalar = 0;
                uint8_t weightScalar = 0;
                uint8_t nature = 0;
            };

            explicit constexpr EncounterTrade8b(const Properties &properties) noexcept : properties(properties) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Trade8b; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Trade8b; }
            EncounterType encounterType() const noexcept override { return EncounterType::Trade; }
            std::string name() const override { return "In-game Trade"; }

            uint16_t speciesId() const noexcept override { return properties.speciesId; }
            uint8_t formId() const noexcept override { return 0; }
            uint8_t levelMin() const noexcept override { return properties.level; }
            uint8_t levelMax() const noexcept override { return properties.level; }
            Enums::GameVersion version() const noexcept override { return properties.version; }
            uint8_t generation() const noexcept override { return 8; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::BDSP; }
            uint16_t location() const noexcept override { return LOCATION_LINK_TRADE6_NPC; }
            uint16_t eggLocation() const noexcept override { return LOCATION_DEFAULT8B_NONE; }
            bool isEgg() const noexcept override { return false; }
            Enums::Ball fixedBall() const noexcept override { return Enums::Ball::Poke; }
            RNG::Shiny shiny() const noexcept override { return RNG::Shiny::Never; }
            RNG::AbilityPermission ability() const noexcept override { return properties.ability; }
            uint8_t fixedGender() const noexcept override { return properties.gender; }
            uint8_t fixedNature() const noexcept override { return properties.nature; }
            RNG::IndividualValueSet fixedIVs() const noexcept override { return properties.individualValues; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &) const override { return EncounterMatchRating::Match; }
            bool isFixedTrainer() const noexcept override { return true; }
            bool isTrainerMatch(const Pokemon::Pokemon &pokemon, const std::u16string &trainerName, int language) const override;
            bool isFixedNickname() const noexcept override { return true; }
            bool isNicknameMatch(const Pokemon::Pokemon &pokemon, const std::u16string &nickname, int language) const override;
            std::u16string getNickname(int language) const override;
            Moveset moves() const noexcept override { return properties.moves; }

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            const FixedTrainerTemplate *asFixedTrainer() const noexcept override { return this; }
            const FixedNicknameTemplate *asFixedNickname() const noexcept override { return this; }
            const MovesetTemplate *asMoveset() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterTrade8b>(*this); }

            uint32_t ID32() const noexcept { return properties.TID16; }
            uint32_t encryptionConstant() const noexcept { return properties.encryptionConstant; }
            uint32_t PID() const noexcept { return properties.PID; }
            uint8_t trainerGender() const noexcept { return properties.trainerGender; }
            uint8_t heightScalar() const noexcept { return properties.heightScalar; }
            uint8_t weightScalar() const noexcept { return properties.weightScalar; }
            /// PKHeX IFixedOTFriendship: Chatot is received at 35, every other trade at 50.
            uint8_t originalTrainerFriendship() const noexcept;
            /// PKHeX IContestStatsReadOnly: Chatot's 20 in each condition, nothing for the others.
            std::array<uint8_t, 6> contestStats() const noexcept;
            /// PKHeX IsMagikarpJapaneseTradedBDSP: a Japanese Magikarp traded between players has its
            /// over-long trainer name replaced by the version's.
            bool isMagikarpJapaneseTradedBDSP(const Pokemon::Pokemon &pokemon) const;
            /// PKHeX IsPijako: the Switch's word filter reverted the French Chatot's nickname.
            bool isPijako(const Pokemon::Pokemon &pokemon) const noexcept;
            /// PKHeX DetectMeisterMagikarpLanguage; -1 when the names match no language.
            int detectMeisterMagikarpLanguage(const std::u16string &nickname, const std::u16string &trainerName, int currentLanguage) const;

        private:
            bool isMatchLocation(const Pokemon::Pokemon &pokemon) const;

            Properties properties;
        };
    }
}

#endif
