/**
 * A Scarlet/Violet in-game trade, including the Partner-Ribbon trades of the Academy staff. Ported
 * from PKHeX PKHeX.Core/Legality/Encounters/Templates/Gen9/EncounterTrade9.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERTRADE9_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERTRADE9_H

#include <cstdint>

#include "Legality/Encounters/EncounterLocations.h"
#include "Legality/Encounters/EncounterRules9.h"
#include "Legality/Encounters/EncounterTrade8.h"
#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterTrade9 final : public Encounterable, public EncounterMatchTemplate, public FixedTrainerTemplate, public FixedNicknameTemplate, public MovesetTemplate
        {
        public:
            struct Properties
            {
                /// nullptr for a trade that keeps the species name (the Partner-Ribbon trades).
                const LanguageNamesSwitch *nicknames = nullptr;
                const LanguageNamesSwitch *trainerNames = nullptr;
                Enums::GameVersion version = Enums::GameVersion::SV;
                uint16_t speciesId = 0;
                uint8_t formId = 0;
                uint8_t level = 0;
                RNG::Shiny shiny = RNG::Shiny::Never;
                Enums::Ball fixedBall = Enums::Ball::None;
                uint8_t nature = NATURE_RANDOM;
                uint32_t ID32 = 0;
                RNG::AbilityPermission ability = RNG::AbilityPermission::Any12;
                uint8_t gender = 0;
                uint8_t trainerGender = 0;
                RNG::IndividualValueSet individualValues{};
                Moveset moves{};
                bool evolveOnTrade = false;
                RNG::SizeType9 weight = RNG::SizeType9::RANDOM;
                RNG::SizeType9 scale = RNG::SizeType9::RANDOM;
                RNG::GemType teraType = RNG::GemType::Default;
                bool hasPartnerRibbon = false;
            };

            explicit constexpr EncounterTrade9(const Properties &properties) noexcept : properties(properties) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Trade9; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Trade9; }
            EncounterType encounterType() const noexcept override { return EncounterType::Trade; }
            std::string name() const override { return "In-game Trade"; }

            uint16_t speciesId() const noexcept override { return properties.speciesId; }
            uint8_t formId() const noexcept override { return properties.formId; }
            uint8_t levelMin() const noexcept override { return properties.level; }
            uint8_t levelMax() const noexcept override { return properties.level; }
            Enums::GameVersion version() const noexcept override { return properties.version; }
            uint8_t generation() const noexcept override { return 9; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::SV; }
            uint16_t location() const noexcept override { return LOCATION_LINK_TRADE6_NPC; }
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return false; }
            Enums::Ball fixedBall() const noexcept override { return properties.fixedBall; }
            RNG::Shiny shiny() const noexcept override { return properties.shiny; }
            RNG::AbilityPermission ability() const noexcept override { return properties.ability; }
            uint8_t fixedGender() const noexcept override { return properties.gender; }
            uint8_t fixedNature() const noexcept override { return properties.nature; }
            RNG::IndividualValueSet fixedIVs() const noexcept override { return properties.individualValues; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &) const override { return EncounterMatchRating::Match; }
            bool isFixedTrainer() const noexcept override { return true; }
            bool isTrainerMatch(const Pokemon::Pokemon &pokemon, const std::u16string &trainerName, int language) const override;
            bool isFixedNickname() const noexcept override { return properties.nicknames != nullptr; }
            bool isNicknameMatch(const Pokemon::Pokemon &pokemon, const std::u16string &nickname, int language) const override;
            std::u16string getNickname(int language) const override;
            Moveset moves() const noexcept override { return properties.moves; }

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            const FixedTrainerTemplate *asFixedTrainer() const noexcept override { return this; }
            const FixedNicknameTemplate *asFixedNickname() const noexcept override { return this; }
            const MovesetTemplate *asMoveset() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterTrade9>(*this); }

            uint32_t ID32() const noexcept { return properties.ID32; }
            uint8_t trainerGender() const noexcept { return properties.trainerGender; }
            bool evolveOnTrade() const noexcept { return properties.evolveOnTrade; }
            RNG::GemType teraType() const noexcept { return properties.teraType; }
            /// PKHeX IRibbonPartner.RibbonPartner.
            bool hasPartnerRibbon() const noexcept { return properties.hasPartnerRibbon; }

        private:
            Properties properties;
        };
    }
}

#endif
