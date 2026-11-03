/**
 * A Scarlet/Violet Wonder Card (WC9) that gives a Pokemon, including the HOME gifts. PKSE keeps only
 * the card fields PKHeX's matcher reads, decoded by tools/gen_encounters9.py. Ported from PKHeX
 * PKHeX.Core/MysteryGifts/WC9.cs and MysteryGift.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_MYSTERYGIFTWC9_H
#define LEGALITY_ENCOUNTERS_MYSTERYGIFTWC9_H

#include <array>
#include <cstdint>

#include "Legality/Encounters/Encounterable.h"
#include "Legality/Encounters/MysteryGiftWC8.h"
#include "Legality/Encounters/TemplateRibbons.h"

namespace Legality
{
    namespace Encounters
    {
        class MysteryGiftWC9 final : public Encounterable, public EncounterMatchTemplate, public MovesetTemplate, public RelearnTemplate
        {
        public:
            /// The number of language slots a WC9 stores (Japanese through Traditional Chinese, no 6).
            static constexpr int LANGUAGE_SLOT_COUNT = WONDER_CARD_LANGUAGE_SLOT_COUNT;

            struct Properties
            {
                uint16_t cardId = 0;
                uint8_t restrictVersion = 0;
                /// The fixed scale, or 256 for a random one (cards from S/V 1.2.0 on).
                uint16_t scale = 0;
                /// The raw ID32 field; before S/V 2.0.1 the game subtracted 1000000 * cardId from it.
                uint32_t ID32 = 0;
                /// A stored version id, or 0 for "the redeeming game".
                int32_t originGame = 0;
                uint32_t encryptionConstant = 0;
                uint32_t PID = 0;
                uint16_t eggLocation = 0;
                uint16_t location = 0;
                uint8_t ball = 0;
                Moveset moves{};
                Moveset relearnMoves{};
                uint16_t speciesId = 0;
                uint8_t formId = 0;
                /// 0-2, or 3 for random.
                uint8_t gender = 0;
                uint8_t level = 0;
                bool isEgg = false;
                /// A nature id, or 0xFF for random.
                uint8_t nature = 0xFF;
                uint8_t abilityType = 0;
                ShinyType8 shinyType = ShinyType8::Never;
                uint8_t metLevel = 0;
                uint8_t teraTypeOriginal = 0;
                int16_t heightValue = 0;
                int16_t weightValue = 0;
                /// HP, Atk, Def, Spe, SpA, SpD; 0xFC-0xFE asks for that many flawless IVs, above 31 is random.
                std::array<uint8_t, 6> individualValues{};
                /// 0, 1, or 2-3 for "the redeeming trainer's".
                uint8_t trainerGender = 0;
                /// The 16-bit language word of each language slot.
                std::array<uint16_t, LANGUAGE_SLOT_COUNT> languageWords{};
                /// The trainer name of each language slot; u"" where the card leaves it to the player.
                std::array<const char16_t *, LANGUAGE_SLOT_COUNT> trainerNames{};
                /// Bit N: language slot N carries a nickname.
                uint16_t nicknamedSlots = 0;
                /// The ribbons the card gives (PKHeX IRibbonSetEvent3/4, IRibbonSetCommon7 and IRibbonIndex).
                RibbonIndexSet ribbons{};
            };

            explicit constexpr MysteryGiftWC9(const Properties &properties) noexcept : properties(properties) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::MysteryGiftWC9; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::MysteryGiftWC9; }
            EncounterType encounterType() const noexcept override { return EncounterType::Event; }
            std::string name() const override { return "Event Gift"; }
            std::string longName() const override { return "Event Gift (WC9)"; }

            uint16_t speciesId() const noexcept override { return properties.speciesId; }
            uint8_t formId() const noexcept override { return properties.formId; }
            uint8_t levelMin() const noexcept override { return properties.level; }
            uint8_t levelMax() const noexcept override { return properties.level; }
            Enums::GameVersion version() const noexcept override { return properties.originGame != 0 ? static_cast<Enums::GameVersion>(properties.originGame) : Enums::GameVersion::SV; }
            uint8_t generation() const noexcept override { return 9; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::SV; }
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
            /// PKHeX MysteryGift.GetMatchRating with WC9's IsMatchPartial (a Tera type mismatch).
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &pokemon) const override;
            Moveset moves() const noexcept override { return properties.moves; }
            Moveset relearnMoves() const noexcept override { return properties.relearnMoves; }

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            const MovesetTemplate *asMoveset() const noexcept override { return this; }
            const RelearnTemplate *asRelearn() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<MysteryGiftWC9>(*this); }

            const Properties &card() const noexcept { return properties; }
            /// PKHeX ID32Old: the ID32 a card redeemed before S/V 2.0.1 gave.
            uint32_t ID32Old() const noexcept { return properties.ID32 - (1000000u * properties.cardId); }
            /// PKHeX IsHOMEGift: card ids from 9000 on.
            bool isHomeGift() const noexcept { return properties.cardId >= 9000; }
            /// PKHeX CanBeReceivedByVersion(pk): HOME's Sword/Shield met locations count for their Gen 9 game.
            bool canBeReceivedBy(const Pokemon::Pokemon &pokemon) const noexcept;
            /// PKHeX GetOT(language) and GetIsNicknamed(language).
            std::u16string getTrainerName(int language) const;
            bool isNicknamed(int language) const noexcept;
            /// PKHeX CanBeAnyLanguage / CanHaveLanguage.
            bool canBeAnyLanguage() const noexcept;
            bool canHaveLanguage(int language) const noexcept;

        private:
            uint32_t getExpectedPID(const Pokemon::Pokemon &pokemon, ShinyType8 type) const noexcept;
            bool isMatchSize(const Pokemon::Pokemon &pokemon) const;

            Properties properties;
        };
    }
}

#endif
