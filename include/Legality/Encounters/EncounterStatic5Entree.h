/**
 * A Gen 5 Entree Forest encounter: a Pokemon brought from the Dream World or a Pokemon Global
 * Link promotion. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen5/EncounterStatic5Entree.cs and DreamWorldEntry.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERSTATIC5ENTREE_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERSTATIC5ENTREE_H

#include <cstdint>

#include "Legality/Encounters/EncounterLocations5.h"
#include "Legality/Encounters/Encounterable.h"
#include "Legality/Encounters/GlobalLinkPromotion.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterStatic5Entree final : public Encounterable, public EncounterMatchTemplate, public MovesetTemplate
        {
        public:
            /// One DreamWorldEntry split per special move, as PKHeX's DreamWorldEntry.GetArray does.
            struct Properties
            {
                /// BW, B2W2, or Any for PKHeX's Gen5 lump (DreamWorld_Common).
                Enums::GameVersion version = Enums::GameVersion::Invalid;
                uint16_t speciesId = 0;
                uint8_t level = 0;
                uint8_t formId = 0;
                uint8_t gender = GENDER_RANDOM;
                /// OnlyHidden when the Black 2/White 2 personal row has a hidden ability, else OnlyFirst.
                RNG::AbilityPermission ability = RNG::AbilityPermission::OnlyFirst;
                /// The special move; 0 when the entry has none.
                uint16_t moveId = 0;
                GlobalLinkPromotion promotion = GlobalLinkPromotion::NotPromotion;
            };

            explicit constexpr EncounterStatic5Entree(const Properties &properties) noexcept : properties(properties) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Static5Entree; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Static5Entree; }
            EncounterType encounterType() const noexcept override { return EncounterType::Static; }
            std::string name() const override;

            uint16_t speciesId() const noexcept override { return properties.speciesId; }
            uint8_t formId() const noexcept override { return properties.formId; }
            uint8_t levelMin() const noexcept override { return properties.level; }
            uint8_t levelMax() const noexcept override { return properties.level; }
            Enums::GameVersion version() const noexcept override { return properties.version; }
            uint8_t generation() const noexcept override { return 5; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::BW; }
            uint16_t location() const noexcept override { return LOCATION_ENTREE_FOREST5; }
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return false; }
            RNG::Shiny shiny() const noexcept override { return RNG::Shiny::Never; }
            RNG::AbilityPermission ability() const noexcept override { return properties.ability; }
            uint8_t fixedGender() const noexcept override { return properties.gender; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &) const override { return EncounterMatchRating::Match; }
            Moveset moves() const noexcept override { return Moveset{{properties.moveId, 0, 0, 0}}; }

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            const MovesetTemplate *asMoveset() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterStatic5Entree>(*this); }

            GlobalLinkPromotion promotion() const noexcept { return properties.promotion; }

        private:
            Properties properties;
        };
    }
}

#endif
