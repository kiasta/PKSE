/**
 * A Gen 1 static encounter or gift (Red, Green, Blue, Yellow). Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen1/EncounterStatic1.cs.
 *
 * PKSE has no GameVersion for PKHeX's RB lump; tools/gen_encounters1.py emits it as RD, which every
 * rule here answers the same way (none asks for RB itself).
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERSTATIC1_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERSTATIC1_H

#include <cstdint>

#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        class EncounterStatic1 final : public Encounterable, public EncounterMatchTemplate
        {
        public:
            constexpr EncounterStatic1(uint16_t speciesId, uint8_t level, Enums::GameVersion version) noexcept : staticSpeciesId(speciesId), staticLevel(level), staticVersion(version) {}

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Static1; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Static1; }
            EncounterType encounterType() const noexcept override { return EncounterType::Static; }
            std::string name() const override { return "Static Encounter"; }

            uint16_t speciesId() const noexcept override { return staticSpeciesId; }
            uint8_t formId() const noexcept override { return 0; }
            uint8_t levelMin() const noexcept override { return staticLevel; }
            uint8_t levelMax() const noexcept override { return staticLevel; }
            Enums::GameVersion version() const noexcept override { return staticVersion; }
            uint8_t generation() const noexcept override { return 1; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::RBY; }
            uint16_t location() const noexcept override { return 0; }
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return false; }
            Enums::Ball fixedBall() const noexcept override { return Enums::Ball::Poke; }
            RNG::AbilityPermission ability() const noexcept override { return RNG::AbilityPermission::OnlyHidden; }

            bool isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const override;
            EncounterMatchRating getMatchRating(const Pokemon::Pokemon &pokemon) const override;

            const EncounterMatchTemplate *asEncounterMatch() const noexcept override { return this; }
            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterStatic1>(*this); }

            /// PKHeX IsStarterPikachu: Yellow's level 5 Pikachu, which holds a Light Ball.
            bool isStarterPikachu() const noexcept;

        private:
            bool isCatchRateValid(uint8_t rate) const noexcept;

            uint16_t staticSpeciesId;
            uint8_t staticLevel;
            Enums::GameVersion staticVersion;
        };
    }
}

#endif
