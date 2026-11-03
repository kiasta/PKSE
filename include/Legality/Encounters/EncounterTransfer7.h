/**
 * The encounter Poke Transporter gives a Virtual Console Red/Blue/Yellow or Gold/Silver/Crystal
 * Pokemon when it moves to Sun/Moon. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen7/EncounterTransfer7.cs,
 * PKHeX.Core/PKM/Util/Conversion/TransporterLogic.cs and EncounterGenerator7.GetVCStaticTransferEncounter.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERTRANSFER7_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERTRANSFER7_H

#include <cstdint>
#include <span>

#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        /// PKHeX TransporterLogic.
        struct TransporterLogic
        {
            /// PKHeX IsHiddenDisallowedVC1 / VC2: the families Poke Transporter gives their first
            /// ability instead of their Hidden one.
            static bool isHiddenDisallowedVC1(uint16_t speciesId) noexcept;
            static bool isHiddenDisallowedVC2(uint16_t speciesId) noexcept;
        };

        class EncounterTransfer7 final : public Encounterable
        {
        public:
            /// `version` is Enums::GameVersion::RBY or GSC.
            constexpr EncounterTransfer7(Enums::GameVersion version, uint16_t speciesId, uint8_t level, uint16_t location, RNG::Shiny shiny, RNG::AbilityPermission ability, bool isFatefulEncounter, uint8_t flawlessIVCount) noexcept : transferVersion(version), transferSpeciesId(speciesId), transferLevel(level), transferLocation(location), transferShiny(shiny), transferAbility(ability), transferIsFatefulEncounter(isFatefulEncounter), transferFlawlessIVCount(flawlessIVCount) {}

            /// PKHeX GetVC1 / GetVC2.
            static EncounterTransfer7 getVC1(uint16_t speciesId, uint8_t metLevel) noexcept;
            static EncounterTransfer7 getVC2(uint16_t speciesId, uint8_t metLevel) noexcept;

            static bool isTemplateType(EncounterTemplateType type) noexcept { return type == EncounterTemplateType::Transfer7; }
            EncounterTemplateType templateType() const noexcept override { return EncounterTemplateType::Transfer7; }
            EncounterType encounterType() const noexcept override { return EncounterType::Static; }
            std::string name() const override;

            uint16_t speciesId() const noexcept override { return transferSpeciesId; }
            uint8_t formId() const noexcept override { return 0; }
            uint8_t levelMin() const noexcept override { return transferLevel; }
            uint8_t levelMax() const noexcept override { return transferLevel; }
            Enums::GameVersion version() const noexcept override { return transferVersion; }
            uint8_t generation() const noexcept override { return 7; }
            Enums::GameVersion contextGroup() const noexcept override { return Enums::GameVersion::SM; }
            uint16_t location() const noexcept override { return transferLocation; }
            uint16_t eggLocation() const noexcept override { return 0; }
            bool isEgg() const noexcept override { return false; }
            Enums::Ball fixedBall() const noexcept override { return Enums::Ball::Poke; }
            RNG::Shiny shiny() const noexcept override { return transferShiny; }
            RNG::AbilityPermission ability() const noexcept override { return transferAbility; }
            uint8_t flawlessIVCount() const noexcept override { return transferFlawlessIVCount; }
            bool isFatefulEncounter() const noexcept override { return transferIsFatefulEncounter; }

            std::unique_ptr<Encounterable> clone() const override { return std::make_unique<EncounterTransfer7>(*this); }

        private:
            Enums::GameVersion transferVersion;
            uint16_t transferSpeciesId;
            uint8_t transferLevel;
            uint16_t transferLocation;
            RNG::Shiny transferShiny;
            RNG::AbilityPermission transferAbility;
            bool transferIsFatefulEncounter;
            uint8_t transferFlawlessIVCount;
        };

        /// PKHeX EncounterGenerator7.GetVCStaticTransferEncounter: the transfer template for a
        /// Virtual Console Pokemon whose Game Boy encounter is `encounterSpeciesId`. `chain` is the
        /// Gen 7 evolution chain (EvoChainsAllGens.Gen7), most evolved first.
        EncounterTransfer7 getVirtualConsoleTransferEncounter(const Pokemon::Pokemon &pokemon, uint16_t encounterSpeciesId, std::span<const Evolutions::EvolutionCriteria> chain);
    }
}

#endif
