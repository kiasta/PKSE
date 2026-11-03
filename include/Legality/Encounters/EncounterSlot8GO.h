/**
 * A Pokemon GO Pokemon transferred to HOME, and the per species-form group of them the data packs
 * together. Ported from PKHeX PKHeX.Core/Legality/Encounters/Templates/GO/EncounterSlot8GO.cs and
 * EncounterArea8g.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERSLOT8GO_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERSLOT8GO_H

#include <cstdint>

#include "Legality/Encounters/EncounterSlot7GO.h"

namespace Legality
{
    namespace Encounters
    {
        /// PKHeX Locations.GO8: the met location HOME gives a GO transfer.
        constexpr uint16_t LOCATION_GO8 = 30012;

        class EncounterSlot8GO final : public Encounterable, public EncounterMatchTemplate, public PogoSlotTemplate
        {
        public:
            constexpr EncounterSlot8GO(uint16_t dayStart, uint16_t dayEnd, uint16_t speciesId, uint8_t formId, uint8_t levelMin, uint8_t minimumIV, RNG::Shiny shiny, uint8_t gender, PogoType type, PogoBallRestriction ballRestriction, PogoImportFormat originFormat, uint8_t flags) noexcept
                : slotDayStart(dayStart), slotDayEnd(dayEnd), slotSpeciesId(speciesId), slotFormId(formId), slotLevelMin(levelMin), slotMinimumIV(minimumIV), slotShiny(shiny), slotGender(gender), slotType(type), slotBallRestriction(ballRestriction), slotOriginFormat(originFormat), slotFlags(flags)
            {
            }

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Slot8GO; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Slot8GO; }
            EncounterType encounterType() const noexcept override { return EncounterType::Wild; }
            std::string name() const override { return "GO Encounter (GO)"; }
            std::string longName() const override { return getLongName(); }

            uint16_t speciesId() const noexcept override { return slotSpeciesId; }
            uint8_t formId() const noexcept override { return slotFormId; }
            uint8_t levelMin() const noexcept override { return slotLevelMin; }
            uint8_t levelMax() const noexcept override { return POGO_MAX_LEVEL; }
            Enums::GameVersion version() const noexcept override { return VERSION_POKEMON_GO; }
            uint8_t generation() const noexcept override { return 8; }
            /// PKHeX Context: the game HOME's import format places the Pokemon in.
            Enums::GameVersion contextGroup() const noexcept override;
            uint16_t location() const noexcept override { return LOCATION_GO8; }
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return false; }
            Enums::Ball fixedBall() const noexcept override { return getPogoFixedBall(slotBallRestriction); }
            RNG::Shiny shiny() const noexcept override { return slotShiny; }
            uint8_t fixedGender() const noexcept override { return slotGender == POGO_GENDER_RANDOM ? GENDER_RANDOM : slotGender; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &pokemon) const override;

            uint16_t dayStart() const noexcept override { return slotDayStart; }
            uint16_t dayEnd() const noexcept override { return slotDayEnd; }
            PogoType pogoType() const noexcept override { return slotType; }
            uint8_t minimumIV() const noexcept override { return slotMinimumIV; }
            uint8_t pogoFlags() const noexcept override { return slotFlags; }

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterSlot8GO>(*this); }

            PogoBallRestriction ballRestriction() const noexcept { return slotBallRestriction; }
            /// PKHeX OriginFormat: the format HOME created the Pokemon in, which sets its first moves.
            PogoImportFormat originFormat() const noexcept { return slotOriginFormat; }
            /// PKHeX OriginGroup: the game whose learnset and personal data the import used.
            Enums::GameVersion originGroup() const noexcept;
            /// PKHeX IsBallValid(ball, currentSpecies, pk).
            bool isBallValid(Enums::Ball ball, uint16_t currentSpeciesId, const Pokemon::Pokemon &pokemon) const noexcept;
            /// PKHeX IFixedOTFriendship.OriginalTrainerFriendship.
            uint8_t originalTrainerFriendship() const noexcept;

        private:
            bool isMatchPartial(const Pokemon::Pokemon &pokemon) const noexcept;
            bool isFormArgumentIncorrect(const Pokemon::Pokemon &pokemon) const noexcept;

            uint16_t slotDayStart;
            uint16_t slotDayEnd;
            uint16_t slotSpeciesId;
            uint8_t slotFormId;
            uint8_t slotLevelMin;
            uint8_t slotMinimumIV;
            RNG::Shiny slotShiny;
            uint8_t slotGender;
            PogoType slotType;
            PogoBallRestriction slotBallRestriction;
            PogoImportFormat slotOriginFormat;
            uint8_t slotFlags;
        };

        /// PKHeX EncounterArea8g: one species-form's HOME transfer slots.
        struct EncounterArea8g
        {
            uint16_t speciesId;
            uint8_t formId;
            const EncounterSlot8GO *slots;
            uint16_t slotCount;
        };
    }
}

#endif
