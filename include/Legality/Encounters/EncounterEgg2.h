/**
 * An egg bred in a Gen 2 game. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen2/EncounterEgg2.cs.
 *
 * PKSE has no GameVersion for PKHeX's GS lump; a Gold/Silver egg stands as GD, and only Crystal (C) is
 * told apart, which is all PKHeX asks of the version.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTEREGG2_H
#define LEGALITY_ENCOUNTERS_ENCOUNTEREGG2_H

#include <cstdint>

#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterEgg2 final : public Encounterable, public EggTemplate
        {
        public:
            /// PKHeX EncounterEgg2.Level: a Gen 2 egg hatches at level 5.
            static constexpr uint8_t LEVEL = 5;

            constexpr EncounterEgg2(uint16_t speciesId, Enums::GameVersion version) noexcept : eggSpeciesId(speciesId), eggVersion(version) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Egg2; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Egg2; }
            EncounterType encounterType() const noexcept override { return EncounterType::Egg; }
            std::string name() const override { return "Egg"; }

            uint16_t speciesId() const noexcept override { return eggSpeciesId; }
            uint8_t formId() const noexcept override { return 0; }
            uint8_t levelMin() const noexcept override { return LEVEL; }
            uint8_t levelMax() const noexcept override { return LEVEL; }
            Enums::GameVersion version() const noexcept override { return eggVersion; }
            uint8_t generation() const noexcept override { return 2; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::GSC; }
            uint16_t location() const noexcept override { return 0; }
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return true; }
            Enums::Ball fixedBall() const noexcept override { return Enums::Ball::Poke; }
            RNG::AbilityPermission ability() const noexcept override { return RNG::AbilityPermission::Any12H; }

            bool canHaveVoltTackle() const noexcept override { return false; }

            const EggTemplate *asEgg() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterEgg2>(*this); }

        private:
            uint16_t eggSpeciesId;
            Enums::GameVersion eggVersion;
        };
    }
}

#endif
