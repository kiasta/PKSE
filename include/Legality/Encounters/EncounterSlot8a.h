/**
 * A Legends: Arceus wild spawn: a spawner, distortion, landmark or mass outbreak slot. Ported from
 * PKHeX PKHeX.Core/Legality/Encounters/Templates/Gen8a/EncounterSlot8a.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERSLOT8A_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERSLOT8A_H

#include <cstdint>

#include "Legality/Encounters/EncounterArea8a.h"
#include "Legality/Encounters/EncounterRules9a.h"
#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterSlot8a final : public Encounterable, public EncounterMatchTemplate
        {
        public:
            constexpr EncounterSlot8a(const EncounterArea8a &parent, const EncounterSlotRecord8a &record) noexcept : parent(&parent), record(record) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Slot8a; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Slot8a; }
            EncounterType encounterType() const noexcept override { return EncounterType::Wild; }
            std::string name() const override { return "Wild Encounter (PLA)"; }
            std::string longName() const override { return name() + " " + getSlotTypeName8a(type()); }

            uint16_t speciesId() const noexcept override { return record.speciesId; }
            uint8_t formId() const noexcept override { return record.formId; }
            uint8_t levelMin() const noexcept override { return record.levelMin; }
            uint8_t levelMax() const noexcept override { return record.levelMax; }
            Enums::GameVersion version() const noexcept override { return Enums::GameVersion::PLA; }
            uint8_t generation() const noexcept override { return 8; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::PLA; }
            uint16_t location() const noexcept override { return parent->location(); }
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return false; }
            uint8_t fixedGender() const noexcept override { return record.gender == 2 ? GENDER_RANDOM : record.gender; }
            uint8_t flawlessIVCount() const noexcept override { return record.flawlessIVCount; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &pokemon) const override;

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterSlot8a>(*this); }

            SlotType8a type() const noexcept { return parent->type(); }
            /// PKHeX IAlphaReadOnly.IsAlpha: the slot can or must be an Alpha.
            bool isAlpha() const noexcept { return record.alphaType != 0; }
            /// PKHeX HasAlphaMove: an Alpha from anywhere but a landmark learns one move-shop move.
            bool hasAlphaMove() const noexcept { return isAlpha() && type() != SlotType8a::Landmark; }
            /// PKHeX ISeedCorrelation64.TryGetSeed: a single-roll spawn seed (plus the outbreak's
            /// bonus rolls) reproduces the record. Never Invalid: a multi-roll seed cannot be ruled out.
            SeedCorrelationResult tryGetSeed(const Pokemon::Pokemon &pokemon, uint64_t &seed) const noexcept;
            /// PKHeX IMasteryInitialMoveShop8.IsForcedMasteryCorrect: the move-shop flags the encounter's
            /// initial moves were generated with.
            bool isForcedMasteryCorrect(const Pokemon::Pokemon &pokemon) const noexcept;

        private:
            EncounterMatchRating getMoveCompatibility(const Pokemon::Pokemon &pokemon) const noexcept;

            const EncounterArea8a *parent;
            EncounterSlotRecord8a record;
        };
    }
}

#endif
