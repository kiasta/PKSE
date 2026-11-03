/**
 * A Let's Go Wonder Card (WB7) that gives a Pokemon. PKSE keeps only the card fields PKHeX's
 * matcher and verifiers read, decoded by tools/gen_encounters7.py. Ported from PKHeX
 * PKHeX.Core/MysteryGifts/WB7.cs and MysteryGift.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_MYSTERYGIFTWB7_H
#define LEGALITY_ENCOUNTERS_MYSTERYGIFTWB7_H

#include <array>
#include <cstdint>

#include "Legality/Encounters/Encounterable.h"
// ShinyType6 is declared with the WC7 card, which shares it.
#include "Legality/Encounters/MysteryGiftWC7.h"
#include "Legality/Encounters/TemplateRibbons.h"

namespace Legality
{
    namespace Encounters
    {
        class MysteryGiftWB7 final : public Encounterable, public EncounterMatchTemplate, public MovesetTemplate, public RelearnTemplate
        {
        public:
            /// The card's nine languages, in PKHeX GetLanguageIndex order (Japanese .. Chinese Traditional).
            static constexpr int LANGUAGE_SLOT_COUNT = 9;

            struct Properties
            {
                uint16_t cardId = 0;
                /// WB7's version restriction; 0 permits both.
                uint8_t restrictVersion = 0;
                uint16_t TID16 = 0;
                uint16_t SID16 = 0;
                /// A stored version id, or 0 for "the redeeming game".
                uint32_t originGame = 0;
                uint32_t encryptionConstant = 0;
                uint8_t ball = 0;
                uint16_t heldItem = 0;
                Moveset moves{};
                uint16_t speciesId = 0;
                uint8_t formId = 0;
                /// A nature id, or 0xFF for random.
                uint8_t nature = 0xFF;
                /// 0-2, or 3 for random.
                uint8_t gender = 0;
                /// PKHeX AbilityType: the card's byte for a HOME gift (card 9000 and up), else 3.
                uint8_t abilityType = 3;
                ShinyType6 pidType = ShinyType6::FixedValue;
                uint16_t eggLocation = 0;
                uint16_t location = 0;
                uint8_t metLevel = 0;
                /// HP, Atk, Def, Spe, SpA, SpD; 0xFC-0xFE asks for that many flawless IVs, 0xFF is random.
                std::array<uint8_t, 6> individualValues{};
                /// 0, 1, or 3 for "the redeeming trainer's".
                uint8_t trainerGender = 0;
                uint8_t level = 0;
                bool isEgg = false;
                uint32_t PID = 0;
                Moveset relearnMoves{};
                /// HP, Atk, Def, Spe, SpA, SpD awakening values the card starts with.
                std::array<uint8_t, 6> awakeningValues{};
                /// The language each language slot is locked to; all zero for any language.
                std::array<uint8_t, LANGUAGE_SLOT_COUNT> languages{};
                /// The trainer name per language slot; u"" where the card leaves it to the player.
                std::array<const char16_t *, LANGUAGE_SLOT_COUNT> trainerNames{};
                /// The ribbons the card gives (PKHeX IRibbonSetEvent3/4, IRibbonSetCommon7 and IRibbonIndex).
                RibbonIndexSet ribbons{};
            };

            explicit constexpr MysteryGiftWB7(const Properties &properties) noexcept : properties(properties) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::MysteryGiftWB7; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::MysteryGiftWB7; }
            EncounterType encounterType() const noexcept override { return EncounterType::Event; }
            std::string name() const override { return "Event Gift"; }
            std::string longName() const override { return "Event Gift (WB7)"; }

            uint16_t speciesId() const noexcept override { return properties.speciesId; }
            uint8_t formId() const noexcept override { return properties.formId; }
            uint8_t levelMin() const noexcept override { return properties.level; }
            uint8_t levelMax() const noexcept override { return properties.level; }
            Enums::GameVersion version() const noexcept override { return Enums::GameVersion::GG; }
            uint8_t generation() const noexcept override { return 7; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::GG; }
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
            /// PKHeX MysteryGift.GetMatchRating: WB7's IsMatchPartial and IsMatchDeferred are both false.
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &) const override { return EncounterMatchRating::Match; }
            Moveset moves() const noexcept override { return properties.moves; }
            Moveset relearnMoves() const noexcept override { return properties.relearnMoves; }

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            const MovesetTemplate *asMoveset() const noexcept override { return this; }
            const RelearnTemplate *asRelearn() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<MysteryGiftWB7>(*this); }

            const Properties &card() const noexcept { return properties; }
            uint32_t ID32() const noexcept { return (static_cast<uint32_t>(properties.SID16) << 16) | properties.TID16; }
            /// PKHeX IsMainlandChinaGift: cards 1501-1503 went to the Simplified Chinese release only.
            bool isMainlandChinaGift() const noexcept { return properties.cardId > 1500 && properties.cardId <= 1503; }
            /// PKHeX CanBeAnyLanguage / CanHaveLanguage.
            bool canBeAnyLanguage() const noexcept;
            bool canHaveLanguage(int language) const noexcept;
            /// PKHeX GetOT: the trainer name for a language (English for a language the card lacks).
            std::u16string getTrainerName(int language) const;

        private:
            Properties properties;
        };
    }
}

#endif
