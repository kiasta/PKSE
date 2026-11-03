/**
 * A Sun/Moon or Ultra Sun/Ultra Moon in-game trade. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen7/EncounterTrade7.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERTRADE7_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERTRADE7_H

#include <cstdint>

#include "Legality/Encounters/EncounterLocations.h"
#include "Legality/Encounters/EncounterTrade8.h"
#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterTrade7 final : public Encounterable, public EncounterMatchTemplate, public FixedTrainerTemplate, public FixedNicknameTemplate
        {
        public:
            /// PKHeX IMemoryOTReadOnly: every Gen 7 trade carries the same memory.
            static constexpr uint8_t TRAINER_MEMORY = 1;
            static constexpr uint8_t TRAINER_MEMORY_INTENSITY = 3;
            static constexpr uint8_t TRAINER_MEMORY_FEELING = 5;
            static constexpr uint16_t TRAINER_MEMORY_VARIABLE = 40;

            struct Properties
            {
                const LanguageNamesSwitch *nicknames = nullptr;
                const LanguageNamesSwitch *trainerNames = nullptr;
                Enums::GameVersion version = Enums::GameVersion::SM;
                uint16_t speciesId = 0;
                uint8_t formId = 0;
                uint8_t level = 0;
                uint8_t nature = NATURE_RANDOM;
                uint32_t ID32 = 0;
                RNG::AbilityPermission ability = RNG::AbilityPermission::Any12;
                uint8_t gender = 0;
                uint8_t trainerGender = 0;
                RNG::IndividualValueSet individualValues{};
                bool evolveOnTrade = false;
            };

            explicit constexpr EncounterTrade7(const Properties &properties) noexcept : properties(properties) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Trade7; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Trade7; }
            EncounterType encounterType() const noexcept override { return EncounterType::Trade; }
            std::string name() const override { return "In-game Trade"; }

            uint16_t speciesId() const noexcept override { return properties.speciesId; }
            uint8_t formId() const noexcept override { return properties.formId; }
            uint8_t levelMin() const noexcept override { return properties.level; }
            uint8_t levelMax() const noexcept override { return properties.level; }
            Enums::GameVersion version() const noexcept override { return properties.version; }
            uint8_t generation() const noexcept override { return 7; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::SM; }
            uint16_t location() const noexcept override { return LOCATION_LINK_TRADE6_NPC; }
            uint16_t eggLocation() const noexcept override { return 0; }
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

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            const FixedTrainerTemplate *asFixedTrainer() const noexcept override { return this; }
            const FixedNicknameTemplate *asFixedNickname() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterTrade7>(*this); }

            uint32_t ID32() const noexcept { return properties.ID32; }
            uint8_t trainerGender() const noexcept { return properties.trainerGender; }
            bool evolveOnTrade() const noexcept { return properties.evolveOnTrade; }

        private:
            Properties properties;
        };

        /// PKHeX's fixed-trainer and fixed-nickname matching against one language table (shared by
        /// the Gen 6, Gen 7 and Let's Go trades): false past the table's end.
        bool isLanguageNameMatch(const LanguageNamesSwitch *names, const std::u16string &text, int language);
        /// PKHeX GetNickname: the language's entry, or language 0's past the table's end.
        std::u16string getLanguageNameOrFirst(const LanguageNamesSwitch *names, int language);
    }
}

#endif
