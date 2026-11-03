/**
 * A Gen 4 Wonder Card (PCD) that gives a Pokemon, and the raw gift (PGT) inside it. PKSE keeps
 * only the fields PKHeX's matcher and verifiers read, decoded from the gift's PK4 by
 * tools/gen_encounters4.py. Ported from PKHeX PKHeX.Core/MysteryGifts/PCD.cs, PGT.cs and
 * MysteryGift.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_MYSTERYGIFTPCD_H
#define LEGALITY_ENCOUNTERS_MYSTERYGIFTPCD_H

#include <array>
#include <cstdint>

#include "Legality/Encounters/Encounterable.h"
#include "Legality/Encounters/TemplateRibbons.h"

namespace Legality
{
    namespace Encounters
    {
        class MysteryGiftPCD final : public Encounterable, public EncounterMatchTemplate, public MovesetTemplate, public RandomCorrelationTemplate
        {
        public:
            /// The gift PK4's fields.
            struct Properties
            {
                uint16_t speciesId = 0;
                uint8_t formId = 0;
                /// PK4 met level (PKHeX PGT.Level).
                uint8_t metLevel = 0;
                /// PK4 party level (PKHeX wc.CurrentLevel).
                uint8_t currentLevel = 0;
                uint16_t TID16 = 0;
                uint16_t SID16 = 0;
                const char16_t *trainerName = u"";
                uint8_t trainerGender = 0;
                uint8_t language = 0;
                /// PK4 egg location: the event location index, which PKHeX offsets by 3000.
                uint16_t eventLocationIndex = 0;
                bool isEgg = false;
                uint8_t ball = 0;
                /// Cool, Beauty, Cute, Smart, Tough, Sheen.
                std::array<uint8_t, 6> contestStats{};
                uint32_t PID = 0;
                uint8_t gender = 0;
                bool isFatefulEncounter = false;
                /// Stored version id.
                uint8_t version = 0;
                Moveset moves{};
                uint16_t heldItem = 0;
                /// PK4 IV32 with the egg and nickname bits cleared; 0 when the IVs are random.
                uint32_t packedIVs = 0;
                /// PK4 IsShiny for the gift's own trainer id.
                bool isShiny = false;
                /// PK4 ability (PKHeX Gift.PK.Ability).
                uint16_t abilityId = 0;
                /// PK4 IsNicknamed (PKHeX Gift.PK.IsNicknamed).
                bool isNicknamed = false;
                /// The ribbons the card gives (PKHeX IRibbonSetEvent3/4, IRibbonSetCommon7 and IRibbonIndex).
                RibbonIndexSet ribbons{};
            };

            explicit constexpr MysteryGiftPCD(const Properties &properties) noexcept : properties(properties) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::MysteryGiftPCD; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::MysteryGiftPCD; }
            EncounterType encounterType() const noexcept override { return EncounterType::Event; }
            std::string name() const override { return "Event Gift"; }
            std::string longName() const override { return "Event Gift (PCD)"; }

            uint16_t speciesId() const noexcept override { return properties.speciesId; }
            uint8_t formId() const noexcept override { return properties.formId; }
            uint8_t levelMin() const noexcept override { return properties.metLevel; }
            uint8_t levelMax() const noexcept override { return properties.metLevel; }
            Enums::GameVersion version() const noexcept override { return static_cast<Enums::GameVersion>(properties.version); }
            uint8_t generation() const noexcept override { return 4; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::DP; }
            uint16_t location() const noexcept override { return properties.isEgg ? 0 : static_cast<uint16_t>(properties.eventLocationIndex + 3000); }
            uint16_t eggLocation() const noexcept override { return properties.isEgg ? static_cast<uint16_t>(properties.eventLocationIndex + 3000) : 0; }
            bool isEgg() const noexcept override { return properties.isEgg; }
            Enums::Ball fixedBall() const noexcept override { return static_cast<Enums::Ball>(properties.ball); }
            RNG::Shiny shiny() const noexcept override;
            RNG::AbilityPermission ability() const noexcept override { return (properties.PID & 1) != 0 ? RNG::AbilityPermission::OnlySecond : RNG::AbilityPermission::OnlyFirst; }
            RNG::IndividualValueSet fixedIVs() const noexcept override;
            bool isFatefulEncounter() const noexcept override { return properties.isFatefulEncounter; }
            /// PKHeX PCD.CanBeReceivedByVersion.
            bool canBeReceivedByVersion(Enums::GameVersion version) const noexcept override { return static_cast<uint8_t>(version) == properties.version; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &pokemon) const override;
            Moveset moves() const noexcept override { return properties.moves; }
            RandomCorrelationRating isCompatible(RNG::PIDType type, const Pokemon::Pokemon &pokemon) const override;
            RNG::PIDType getSuggestedCorrelation() const noexcept override { return RNG::PIDType::None; }

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            const MovesetTemplate *asMoveset() const noexcept override { return this; }
            const RandomCorrelationTemplate *asRandomCorrelation() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<MysteryGiftPCD>(*this); }

            const Properties &giftData() const noexcept { return properties; }
            uint16_t heldItem() const noexcept { return properties.heldItem; }
            /// PKHeX PCD.IsRandomPID: a PID of 1 means random and never shiny.
            bool isRandomPID() const noexcept { return properties.PID == 1; }

        private:
            Properties properties;
        };

        /// The raw-gift template PKHeX uses for a Pokemon Ranger Manaphy egg
        /// (EncounterGenerator4.RangerManaphy).
        class MysteryGiftPGT final : public Encounterable, public RandomCorrelationTemplate
        {
        public:
            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::MysteryGiftPGT; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::MysteryGiftPGT; }
            EncounterType encounterType() const noexcept override { return EncounterType::Event; }
            std::string name() const override { return "Event Gift"; }
            std::string longName() const override { return "Event Gift (PGT)"; }

            uint16_t speciesId() const noexcept override { return SPECIES_MANAPHY; }
            uint8_t formId() const noexcept override { return 0; }
            uint8_t levelMin() const noexcept override { return 1; }
            uint8_t levelMax() const noexcept override { return 1; }
            /// PKHeX GameVersion.Gen4: any Gen 4 game.
            Enums::GameVersion version() const noexcept override { return Enums::GameVersion::Any; }
            uint8_t generation() const noexcept override { return 4; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::DP; }
            uint16_t location() const noexcept override { return 0; }
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return true; }
            Enums::Ball fixedBall() const noexcept override { return Enums::Ball::Poke; }
            bool isFatefulEncounter() const noexcept override { return true; }

            RandomCorrelationRating isCompatible(RNG::PIDType type, const Pokemon::Pokemon &pokemon) const override;
            RNG::PIDType getSuggestedCorrelation() const noexcept override { return RNG::PIDType::Method1; }

            const RandomCorrelationTemplate *asRandomCorrelation() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<MysteryGiftPGT>(*this); }

            /// PKHeX PGT.IsRangerManaphy: the Pokemon came from the Pokemon Ranger Manaphy egg.
            static bool isRangerManaphy(const Pokemon::Pokemon &pokemon);

        private:
            static constexpr uint16_t SPECIES_MANAPHY = 490;
        };
    }
}

#endif
