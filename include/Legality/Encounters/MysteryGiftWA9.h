/**
 * A Legends: Z-A Wonder Card (WA9) that gives a Pokemon, including the HOME gifts. PKSE keeps only the
 * card fields PKHeX's matcher reads, decoded by tools/gen_encounters9a.py. Ported from PKHeX
 * PKHeX.Core/MysteryGifts/WA9.cs and MysteryGift.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_MYSTERYGIFTWA9_H
#define LEGALITY_ENCOUNTERS_MYSTERYGIFTWA9_H

#include <array>
#include <cstdint>

#include "Legality/Encounters/EncounterRules9a.h"
#include "Legality/Encounters/Encounterable.h"
#include "Legality/Encounters/MysteryGiftWC8.h"
#include "Legality/Encounters/TemplateRibbons.h"

namespace Legality
{
    namespace Encounters
    {
        class MysteryGiftWA9 final : public Encounterable, public EncounterMatchTemplate, public MovesetTemplate, public RelearnTemplate
        {
        public:
            /// A WA9 stores ten name slots (Japanese through Latin American Spanish, no 6); PKHeX's
            /// CanBeAnyLanguage reads only the first nine language words.
            static constexpr int LANGUAGE_SLOT_COUNT = 10;

            struct Properties
            {
                uint16_t cardId = 0;
                /// The stored ID32; the redeemed Pokemon gets ID32Old().
                uint32_t ID32 = 0;
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
                uint8_t gender = 0;
                uint8_t level = 0;
                bool isEgg = false;
                /// A nature id, or 0xFF (or anything from 25) for a random one.
                uint8_t nature = 0xFF;
                uint8_t abilityType = 0;
                ShinyType8 shinyType = ShinyType8::Never;
                uint8_t metLevel = 0;
                /// HP, Atk, Def, Spe, SpA, SpD as stored; 0xFC-0xFE in HP mean 1-3 flawless IVs.
                std::array<uint8_t, 6> individualValues{};
                uint8_t trainerGender = 0;
                /// The fixed scale, or 256 for a random one.
                uint16_t scale = 0;
                bool isAlpha = false;
                std::array<uint16_t, LANGUAGE_SLOT_COUNT> languageWords{};
                std::array<const char16_t *, LANGUAGE_SLOT_COUNT> trainerNames{};
                uint16_t nicknamedSlots = 0;
                /// The ribbons the card gives (PKHeX IRibbonSetEvent3/4, IRibbonSetCommon7 and IRibbonIndex).
                RibbonIndexSet ribbons{};
            };

            explicit constexpr MysteryGiftWA9(const Properties &properties) noexcept : properties(properties) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::MysteryGiftWA9; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::MysteryGiftWA9; }
            EncounterType encounterType() const noexcept override { return EncounterType::Event; }
            std::string name() const override { return "Event Gift"; }
            std::string longName() const override { return "Event Gift (WA9)"; }

            uint16_t speciesId() const noexcept override { return properties.speciesId; }
            uint8_t formId() const noexcept override { return properties.formId; }
            uint8_t levelMin() const noexcept override { return properties.level; }
            uint8_t levelMax() const noexcept override { return properties.level; }
            /// PKHeX WA9.Version: SV when the card names no origin game, as PKHeX has it.
            Enums::GameVersion version() const noexcept override { return properties.originGame != 0 ? static_cast<Enums::GameVersion>(properties.originGame) : Enums::GameVersion::SV; }
            uint8_t generation() const noexcept override { return 9; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::ZA; }
            uint16_t location() const noexcept override { return properties.location; }
            uint16_t eggLocation() const noexcept override { return properties.eggLocation; }
            bool isEgg() const noexcept override { return properties.isEgg; }
            Enums::Ball fixedBall() const noexcept override { return static_cast<Enums::Ball>(properties.ball); }
            RNG::Shiny shiny() const noexcept override;
            RNG::AbilityPermission ability() const noexcept override;
            uint8_t fixedGender() const noexcept override { return properties.gender == 3 ? GENDER_RANDOM : properties.gender; }
            uint8_t fixedNature() const noexcept override { return properties.nature >= NATURE_RANDOM ? NATURE_RANDOM : properties.nature; }
            uint8_t flawlessIVCount() const noexcept override;
            bool isFatefulEncounter() const noexcept override { return true; }
            bool hasMetLevel() const noexcept override { return true; }
            uint8_t metLevel() const noexcept override { return properties.metLevel; }
            bool canBeReceivedByVersion(Enums::GameVersion version) const noexcept override { return version == Enums::GameVersion::ZA; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            /// PKHeX MysteryGift.GetMatchRating with WA9's IsMatchPartial (no seed for a non-HOME card).
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &pokemon) const override;
            Moveset moves() const noexcept override { return properties.moves; }
            Moveset relearnMoves() const noexcept override { return properties.relearnMoves; }

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            const MovesetTemplate *asMoveset() const noexcept override { return this; }
            const RelearnTemplate *asRelearn() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<MysteryGiftWA9>(*this); }

            const Properties &card() const noexcept { return properties; }
            /// PKHeX ID32Old, which WA9 always uses (IsOldIDFormat).
            uint32_t ID32Old() const noexcept { return properties.ID32 - (1000000u * properties.cardId); }
            /// PKHeX IsHOMEGift: card ids from 9000 on.
            bool isHomeGift() const noexcept { return properties.cardId >= 9000; }
            bool isAlpha() const noexcept { return properties.isAlpha; }
            /// PKHeX GetOT(language) and GetIsNicknamed(language).
            std::u16string getTrainerName(int language) const;
            bool isNicknamed(int language) const noexcept;
            /// PKHeX CanBeAnyLanguage / CanHaveLanguage.
            bool canBeAnyLanguage() const noexcept;
            bool canHaveLanguage(int language) const noexcept;
            /// PKHeX GetParams.
            RNG::GenerateParam9a getParameters() const noexcept;
            /// PKHeX WA9.TryGetSeed: Success or Invalid, never Ignore.
            SeedCorrelationResult tryGetSeed(const Pokemon::Pokemon &pokemon) const noexcept;

        private:
            uint32_t getExpectedPID(const Pokemon::Pokemon &pokemon, ShinyType8 type) const noexcept;

            Properties properties;
        };
    }
}

#endif
