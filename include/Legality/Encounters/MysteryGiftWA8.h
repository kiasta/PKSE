/**
 * A Legends: Arceus Wonder Card (WA8) that gives a Pokemon, including the HOME gifts. PKSE keeps only
 * the card fields PKHeX's matcher reads, decoded by tools/gen_encounters8a.py.
 * Ported from PKHeX PKHeX.Core/MysteryGifts/WA8.cs and MysteryGift.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_MYSTERYGIFTWA8_H
#define LEGALITY_ENCOUNTERS_MYSTERYGIFTWA8_H

#include <array>
#include <cstdint>

#include "Legality/Encounters/Encounterable.h"
#include "Legality/Encounters/MysteryGiftWC8.h"
#include "Legality/Encounters/TemplateRibbons.h"

namespace Legality
{
    namespace Encounters
    {
        class MysteryGiftWA8 final : public Encounterable, public EncounterMatchTemplate, public MovesetTemplate, public RelearnTemplate
        {
        public:
            struct Properties
            {
                uint16_t cardId = 0;
                uint16_t TID16 = 0;
                uint16_t SID16 = 0;
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
                /// The raw PIDType byte (PKHeX ShinyType8 values).
                uint8_t shinyType = 0;
                uint8_t metLevel = 0;
                /// HP, Atk, Def, Spe, SpA, SpD; 0xFC-0xFE asks for that many flawless IVs, 0xFF is random.
                std::array<uint8_t, 6> individualValues{};
                /// 0, 1, or 2-3 for "the redeeming trainer's".
                uint8_t trainerGender = 0;
                uint8_t dynamaxLevel = 0;
                /// The minimum grit (Ganbaru) values: HP, Atk, Def, Spe, SpA, SpD.
                std::array<uint8_t, 6> gritValues{};
                /// The 16-bit language word of each language slot (PKHeX reads it as Int16).
                std::array<uint16_t, WONDER_CARD_LANGUAGE_SLOT_COUNT> languageWords{};
                /// The trainer name of each language slot; u"" where the card leaves it to the player.
                std::array<const char16_t *, WONDER_CARD_LANGUAGE_SLOT_COUNT> trainerNames{};
                /// Bit N: language slot N carries a nickname.
                uint16_t nicknamedSlots = 0;
                /// The ribbons the card gives (PKHeX IRibbonSetEvent3/4, IRibbonSetCommon7 and IRibbonIndex).
                RibbonIndexSet ribbons{};
            };

            explicit constexpr MysteryGiftWA8(const Properties &properties) noexcept : properties(properties) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::MysteryGiftWA8; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::MysteryGiftWA8; }
            EncounterType encounterType() const noexcept override { return EncounterType::Event; }
            std::string name() const override { return "Event Gift"; }
            std::string longName() const override { return "Event Gift (WA8)"; }

            uint16_t speciesId() const noexcept override { return properties.speciesId; }
            uint8_t formId() const noexcept override { return properties.formId; }
            uint8_t levelMin() const noexcept override { return properties.level; }
            uint8_t levelMax() const noexcept override { return properties.level; }
            Enums::GameVersion version() const noexcept override { return properties.originGame != 0 ? static_cast<Enums::GameVersion>(properties.originGame) : Enums::GameVersion::PLA; }
            uint8_t generation() const noexcept override { return 8; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::PLA; }
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

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            /// PKHeX MysteryGift.GetMatchRating with WA8's IsMatchPartial / IsMatchDeferred, both false.
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &) const override { return EncounterMatchRating::Match; }
            Moveset moves() const noexcept override { return properties.moves; }
            Moveset relearnMoves() const noexcept override { return properties.relearnMoves; }

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            const MovesetTemplate *asMoveset() const noexcept override { return this; }
            const RelearnTemplate *asRelearn() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<MysteryGiftWA8>(*this); }

            const Properties &card() const noexcept { return properties; }
            uint32_t ID32() const noexcept { return (static_cast<uint32_t>(properties.SID16) << 16) | properties.TID16; }
            /// PKHeX IsHOMEGift: card ids from 9000 on.
            bool isHomeGift() const noexcept { return properties.cardId >= 9000; }
            /// PKHeX IEncounterServerDate.IsDateRestricted: every WA8.
            bool isDateRestricted() const noexcept { return true; }
            /// PKHeX IsDateLockJapanese: HOME's Hisui starters were Japanese-only until 2022-05-20.
            bool isDateLockJapanese() const noexcept { return properties.cardId == 9018 || properties.cardId == 9019 || properties.cardId == 9020; }
            /// PKHeX CanBeReceivedByVersion(version, pk), renamed so it does not hide the base overload:
            /// Legends: Arceus, or Sword for a PK8 after HOME.
            bool canRecordBeReceivedByVersion(Enums::GameVersion version, const Pokemon::Pokemon &pokemon) const noexcept;
            /// PKHeX GetOT(language) and GetIsNicknamed(language).
            std::u16string getTrainerName(int language) const;
            bool isNicknamed(int language) const noexcept;

        private:
            uint32_t getExpectedPID(const Pokemon::Pokemon &pokemon, uint8_t type) const noexcept;
            bool isMatchBall(const Pokemon::Pokemon &pokemon) const noexcept;

            Properties properties;
        };
    }
}

#endif
