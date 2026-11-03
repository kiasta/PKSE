/**
 * A Gen 5 Mystery Gift (PGF) that gives a Pokemon. PKSE keeps only the fields PKHeX's matcher and
 * verifiers read, decoded by tools/gen_encounters5.py. Ported from PKHeX
 * PKHeX.Core/MysteryGifts/PGF.cs and MysteryGift.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_MYSTERYGIFTPGF_H
#define LEGALITY_ENCOUNTERS_MYSTERYGIFTPGF_H

#include <array>
#include <cstdint>

#include "Legality/Encounters/Encounterable.h"
#include "Legality/Encounters/TemplateRibbons.h"

namespace Legality
{
    namespace Encounters
    {
        class MysteryGiftPGF final : public Encounterable, public EncounterMatchTemplate, public MovesetTemplate
        {
        public:
            /// The PGF's fields, plus the receivability byte PKHeX packs after the card list.
            struct Properties
            {
                uint16_t TID16 = 0;
                uint16_t SID16 = 0;
                /// Stored version id; 0 when any game may receive it.
                uint8_t originGame = 0;
                uint32_t PID = 0;
                uint8_t ball = 0;
                uint16_t heldItem = 0;
                Moveset moves{};
                uint16_t speciesId = 0;
                uint8_t formId = 0;
                uint8_t language = 0;
                /// Empty when the gift is not nicknamed.
                const char16_t *nickname = u"";
                /// 0xFF when random.
                uint8_t nature = 0xFF;
                /// 2 when random.
                uint8_t gender = 2;
                /// 0 first, 1 second, 2 hidden, 3 either of the first two, 4 any.
                uint8_t abilityType = 0;
                /// 0 never shiny, 1 random, 2 always shiny.
                uint8_t PIDType = 0;
                uint16_t eggLocation = 0;
                uint16_t location = 0;
                uint8_t metLevel = 0;
                /// Cool, Beauty, Cute, Smart, Tough, Sheen.
                std::array<uint8_t, 6> contestStats{};
                /// HP, Atk, Def, Spe, SpA, SpD; above 31 means random.
                std::array<uint8_t, 6> individualValues{};
                const char16_t *trainerName = u"";
                /// 3 or more when the receiver's gender is used.
                uint8_t trainerGender = 0;
                uint8_t level = 0;
                bool isEgg = false;
                /// PKHeX RestrictVersion: bit 0 White, 1 Black, 2 White 2, 3 Black 2; 0 for no data.
                uint8_t restrictVersion = 0;
                /// PKHeX RestrictLanguage: 0 for any.
                uint8_t restrictLanguage = 0;
                /// The ribbons the card gives (PKHeX IRibbonSetEvent3/4, IRibbonSetCommon7 and IRibbonIndex).
                RibbonIndexSet ribbons{};
            };

            explicit constexpr MysteryGiftPGF(const Properties &properties) noexcept : properties(properties) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::MysteryGiftPGF; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::MysteryGiftPGF; }
            EncounterType encounterType() const noexcept override { return EncounterType::Event; }
            std::string name() const override { return "Event Gift"; }
            std::string longName() const override { return "Event Gift (PGF)"; }

            uint16_t speciesId() const noexcept override { return properties.speciesId; }
            uint8_t formId() const noexcept override { return properties.formId; }
            uint8_t levelMin() const noexcept override { return properties.level; }
            uint8_t levelMax() const noexcept override { return properties.level; }
            Enums::GameVersion version() const noexcept override;
            uint8_t generation() const noexcept override { return 5; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::BW; }
            uint16_t location() const noexcept override { return properties.location; }
            uint16_t eggLocation() const noexcept override { return properties.eggLocation; }
            bool isEgg() const noexcept override { return properties.isEgg; }
            Enums::Ball fixedBall() const noexcept override { return static_cast<Enums::Ball>(properties.ball); }
            RNG::Shiny shiny() const noexcept override;
            RNG::AbilityPermission ability() const noexcept override;
            uint8_t fixedGender() const noexcept override { return properties.gender; }
            uint8_t fixedNature() const noexcept override { return properties.nature; }
            bool isFatefulEncounter() const noexcept override { return true; }
            bool hasMetLevel() const noexcept override { return true; }
            uint8_t metLevel() const noexcept override { return properties.metLevel; }
            bool canBeReceivedByVersion(Enums::GameVersion version) const noexcept override;

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &pokemon) const override;
            Moveset moves() const noexcept override { return properties.moves; }

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            const MovesetTemplate *asMoveset() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<MysteryGiftPGF>(*this); }

            const Properties &giftData() const noexcept { return properties; }
            uint16_t heldItem() const noexcept { return properties.heldItem; }
            /// PKHeX IsShiny: the gift is always shiny.
            bool isShinyGift() const noexcept { return properties.PIDType == 2; }
            /// PKHeX IsNicknamed.
            bool isNicknamed() const noexcept { return properties.nickname[0] != u'\0'; }
            uint8_t restrictLanguage() const noexcept { return properties.restrictLanguage; }

        private:
            bool isMatchPartial(const Pokemon::Pokemon &pokemon) const;

            Properties properties;
        };
    }
}

#endif
