/**
 * A Sun/Moon or Ultra Sun/Ultra Moon Wonder Card (WC7) that gives a Pokemon. PKSE keeps only the
 * card fields PKHeX's matcher and verifiers read, decoded by tools/gen_encounters7.py. Ported from
 * PKHeX PKHeX.Core/MysteryGifts/WC7.cs, WC7Full.cs, ShinyType6.cs and MysteryGift.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_MYSTERYGIFTWC7_H
#define LEGALITY_ENCOUNTERS_MYSTERYGIFTWC7_H

#include <array>
#include <cstdint>

#include "Legality/Encounters/Encounterable.h"
#include "Legality/Encounters/TemplateRibbons.h"

namespace Legality
{
    namespace Encounters
    {
        /// PKHeX ShinyType6 (the WC6 and WC7 PID type).
        enum class ShinyType6 : uint8_t
        {
            FixedValue = 0,
            Random = 1,
            Always = 2,
            Never = 3,
        };

        class MysteryGiftWC7 final : public Encounterable, public EncounterMatchTemplate, public MovesetTemplate, public RelearnTemplate
        {
        public:
            struct Properties
            {
                uint16_t cardId = 0;
                /// WC7Full's version and language restrictions; 0 permits all.
                uint8_t restrictVersion = 0;
                uint8_t restrictLanguage = 0;
                uint16_t TID16 = 0;
                uint16_t SID16 = 0;
                /// A stored version id, or 0 for "the redeeming game".
                uint8_t originGame = 0;
                uint32_t encryptionConstant = 0;
                uint8_t ball = 0;
                uint16_t heldItem = 0;
                Moveset moves{};
                uint16_t speciesId = 0;
                uint8_t formId = 0;
                /// 0 for "the redeeming game's".
                uint8_t language = 0;
                /// The card names the Pokemon (or it is an egg).
                bool isNicknamed = false;
                /// A nature id, or 0xFF for random.
                uint8_t nature = 0xFF;
                /// 0-2, or 3 for random.
                uint8_t gender = 0;
                uint8_t abilityType = 0;
                ShinyType6 pidType = ShinyType6::FixedValue;
                uint16_t eggLocation = 0;
                uint16_t location = 0;
                uint8_t metLevel = 0;
                /// Cool, Beauty, Cute, Smart, Tough, Sheen.
                std::array<uint8_t, 6> contestStats{};
                /// HP, Atk, Def, Spe, SpA, SpD; 0xFC-0xFE asks for that many flawless IVs, 0xFF is random.
                std::array<uint8_t, 6> individualValues{};
                /// 0, 1, or 3 for "the redeeming trainer's".
                uint8_t trainerGender = 0;
                /// u"" where the card leaves it to the player.
                const char16_t *trainerName = u"";
                uint8_t level = 0;
                bool isEgg = false;
                uint32_t PID = 0;
                Moveset relearnMoves{};
                uint8_t trainerMemoryIntensity = 0;
                uint8_t trainerMemory = 0;
                uint16_t trainerMemoryVariable = 0;
                uint8_t trainerMemoryFeeling = 0;
                /// The ribbons the card gives (PKHeX IRibbonSetEvent3/4, IRibbonSetCommon7 and IRibbonIndex).
                RibbonIndexSet ribbons{};
            };

            explicit constexpr MysteryGiftWC7(const Properties &properties) noexcept : properties(properties) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::MysteryGiftWC7; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::MysteryGiftWC7; }
            EncounterType encounterType() const noexcept override { return EncounterType::Event; }
            std::string name() const override { return "Event Gift"; }
            std::string longName() const override { return "Event Gift (WC7)"; }

            uint16_t speciesId() const noexcept override { return properties.speciesId; }
            uint8_t formId() const noexcept override { return properties.formId; }
            uint8_t levelMin() const noexcept override { return properties.level; }
            uint8_t levelMax() const noexcept override { return properties.level; }
            Enums::GameVersion version() const noexcept override;
            uint8_t generation() const noexcept override { return 7; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::SM; }
            uint16_t location() const noexcept override { return properties.location; }
            uint16_t eggLocation() const noexcept override { return properties.eggLocation; }
            bool isEgg() const noexcept override { return properties.isEgg; }
            Enums::Ball fixedBall() const noexcept override { return static_cast<Enums::Ball>(properties.ball); }
            RNG::Shiny shiny() const noexcept override;
            RNG::AbilityPermission ability() const noexcept override;
            uint8_t fixedGender() const noexcept override { return properties.gender == 3 ? GENDER_RANDOM : properties.gender; }
            uint8_t fixedNature() const noexcept override { return properties.nature == 0xFF ? NATURE_RANDOM : properties.nature; }
            bool isFatefulEncounter() const noexcept override { return true; }
            bool hasMetLevel() const noexcept override { return true; }
            uint8_t metLevel() const noexcept override { return properties.metLevel; }
            bool canBeReceivedByVersion(Enums::GameVersion version) const noexcept override;

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            /// PKHeX MysteryGift.GetMatchRating with WC7's IsMatchPartial (IsMatchDeferred is false).
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &pokemon) const override;
            Moveset moves() const noexcept override { return properties.moves; }
            Moveset relearnMoves() const noexcept override { return properties.relearnMoves; }

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            const MovesetTemplate *asMoveset() const noexcept override { return this; }
            const RelearnTemplate *asRelearn() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<MysteryGiftWC7>(*this); }

            const Properties &card() const noexcept { return properties; }
            uint32_t ID32() const noexcept { return (static_cast<uint32_t>(properties.SID16) << 16) | properties.TID16; }
            /// PKHeX IsAshPikachu / IsAshGreninja.
            bool isAshPikachu() const noexcept { return ID32() == 0x798B469B; }
            bool isAshGreninja() const noexcept { return ID32() == 0x79F57B49; }

        private:
            Properties properties;
        };
    }
}

#endif
