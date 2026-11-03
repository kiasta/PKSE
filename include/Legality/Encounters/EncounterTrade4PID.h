/**
 * A Gen 4 in-game trade or NPC gift with a fixed PID (Diamond, Pearl, Platinum, HeartGold,
 * SoulSilver). Ported from PKHeX PKHeX.Core/Legality/Encounters/Templates/Gen4/EncounterTrade4PID.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERTRADE4PID_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERTRADE4PID_H

#include <array>
#include <cstdint>

#include "Legality/Encounters/EncounterLocations.h"
#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        /// One string per language id 0-8 (Korean included), the games' own text; empty where
        /// PKHeX has none (language ids 0 and 6).
        using LanguageNames4 = std::array<const char16_t *, 9>;

        class EncounterTrade4PID final : public Encounterable, public EncounterMatchTemplate, public FixedTrainerTemplate, public FixedNicknameTemplate, public MovesetTemplate
        {
        public:
            struct Properties
            {
                const LanguageNames4 *nicknames = nullptr;
                const LanguageNames4 *trainerNames = nullptr;
                /// DP or HGSS; Any stands for PKHeX's DPPt lump, which PKSE has no group for.
                Enums::GameVersion version = Enums::GameVersion::Invalid;
                uint32_t PID = 0;
                uint16_t speciesId = 0;
                uint8_t level = 0;
                RNG::AbilityPermission ability = RNG::AbilityPermission::Any12;
                uint16_t TID16 = 0;
                uint16_t SID16 = 0;
                uint8_t trainerGender = 0;
                uint8_t gender = 0;
                RNG::IndividualValueSet individualValues{};
                Moveset moves{};
                /// Set only for the HeartGold/SoulSilver Spearow and Shuckle gifts.
                uint8_t metLocation = 0;
                /// Every contest stat but Sheen starts at this value.
                uint8_t contest = 0;
            };

            explicit constexpr EncounterTrade4PID(const Properties &properties) noexcept : properties(properties) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Trade4PID; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Trade4PID; }
            EncounterType encounterType() const noexcept override { return EncounterType::Trade; }
            std::string name() const override { return "In-game Trade"; }

            uint16_t speciesId() const noexcept override { return properties.speciesId; }
            uint8_t formId() const noexcept override { return 0; }
            uint8_t levelMin() const noexcept override { return properties.level; }
            uint8_t levelMax() const noexcept override { return isMetUnset() ? properties.level : 100; }
            Enums::GameVersion version() const noexcept override { return properties.version; }
            uint8_t generation() const noexcept override { return 4; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::DP; }
            uint16_t location() const noexcept override { return isMetUnset() ? LOCATION_LINK_TRADE4_NPC : properties.metLocation; }
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return false; }
            Enums::Ball fixedBall() const noexcept override { return Enums::Ball::Poke; }
            RNG::Shiny shiny() const noexcept override { return RNG::Shiny::FixedValue; }
            RNG::AbilityPermission ability() const noexcept override { return properties.ability; }
            uint8_t fixedGender() const noexcept override { return properties.gender; }
            uint8_t fixedNature() const noexcept override { return static_cast<uint8_t>(properties.PID % 25); }
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
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterTrade4PID>(*this); }

            uint32_t PID() const noexcept { return properties.PID; }
            uint16_t TID16() const noexcept { return properties.TID16; }
            uint16_t SID16() const noexcept { return properties.SID16; }
            uint32_t ID32() const noexcept { return static_cast<uint32_t>(properties.TID16) | (static_cast<uint32_t>(properties.SID16) << 16); }
            uint8_t trainerGender() const noexcept { return properties.trainerGender; }
            /// Cool, Beauty, Cute, Smart, Tough, Sheen.
            std::array<uint8_t, 6> contestStats() const noexcept { return {properties.contest, properties.contest, properties.contest, properties.contest, properties.contest, 0}; }

            /// PKHeX IsLanguageSwap: the trainer's language is a foreign one.
            bool isLanguageSwap() const noexcept;
            /// PKHeX DetectOriginalLanguage: the language whose strings the trade should match.
            int detectOriginalLanguage(const Pokemon::Pokemon &pokemon) const;
            /// PKHeX IsIncorrectEnglish: English Diamond/Pearl kept the Japanese language id.
            bool isIncorrectEnglish(const Pokemon::Pokemon &pokemon) const noexcept;

        private:
            bool isMetUnset() const noexcept { return properties.metLocation == 0; }
            int detectTradeLanguage(const std::u16string &trainerName, int currentLanguage) const;

            Properties properties;
        };

        /// PKHeX Legal.GetIsFixedIVSequenceValidNoRand: every IV equals the template's. Shared with
        /// the Gen 8 in-game trades.
        bool isFixedIVSequenceValidNoRandom(const RNG::IndividualValueSet &individualValues, const Pokemon::Pokemon &pokemon) noexcept;
    }
}

#endif
