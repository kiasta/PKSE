/**
 * The facts about a Pokemon that PKHeX's move verifiers read from its PKM, read once so the learn
 * code never branches on the save format. The record flags (Sword/Shield Technical Records,
 * Scarlet/Violet Technical Machines) are asked of the Pokemon itself.
 */
#ifndef LEGALITY_LEARNSOURCE_LEARNENTITY_H
#define LEGALITY_LEARNSOURCE_LEARNENTITY_H

#include <array>
#include <cstdint>

#include "Enums/GameVersion.h"

namespace Pokemon
{
    class Pokemon;
}

namespace Legality
{
    namespace LearnSource
    {
        struct LearnEntity
        {
            const Pokemon::Pokemon *pokemon = nullptr;
            uint16_t speciesId = 0;
            uint8_t formId = 0;
            /// The save-format group (Pokemon::getGameGroup) and PKHeX PKM.Format.
            Enums::GameVersion formatGroup = Enums::GameVersion::Invalid;
            int formatGeneration = 0;
            /// PKHeX PKM.Context, as a context group.
            Enums::GameVersion contextGroup = Enums::GameVersion::Invalid;
            /// PKHeX PKM.Version as stored; 0 when the format stores none.
            uint8_t originVersion = 0;
            uint8_t metLevel = 0;
            uint16_t metLocation = 0;
            bool isEgg = false;
            /// PKHeX PKM.Korean.
            bool isKorean = false;
            /// PKHeX PKM.IsUntraded.
            bool isUntraded = false;
            /// PKHeX IHomeTrack.HasTracker.
            bool hasHomeTracker = false;
            /// PKHeX PK8.BattleVersion (0 everywhere else).
            uint8_t battleVersion = 0;
            /// PKHeX PKM.VC1 / VC3.
            bool isVirtualConsoleGeneration1 = false;
            bool isVirtualConsoleGeneration3 = false;
            /// PKHeX PK1.CatchRate (Gen 1 format only), which holds a Gen 2 item after a tradeback.
            uint8_t catchRate = 0;
            /// PKHeX ICaughtData2.CaughtData (Crystal's caught data word).
            uint16_t caughtData = 0;
            std::array<uint16_t, 4> moves{};
            std::array<uint16_t, 4> relearnMoves{};
            /// PKHeX ParseSettings.AllowEraCartGB (Stadium 2) and AllowGBACrossTransferRSE.
            bool allowsGameBoyCartridgeEra = false;
            bool allowsGameBoyAdvanceCrossTransfer = true;

            /// PKHeX PKM.IsOriginalMovesetDeleted (Legal.cs).
            bool isOriginalMovesetDeleted() const noexcept;
            /// PKHeX PK8.GetMoveRecordFlag; false for every other format.
            bool isTechnicalRecordFlagSet(int recordIndex) const noexcept;
            /// PKHeX PK9.GetMoveRecordFlag; false for every other format.
            bool isTechnicalMachineFlagSet(int machineIndex) const noexcept;
        };

        /// Reads the facts from a stored Pokemon. `allowsGameBoyCartridgeEra` and
        /// `allowsGameBoyAdvanceCartridgeEra` are ParseSettings' era rules for the open save.
        LearnEntity getLearnEntity(const Pokemon::Pokemon &pokemon, bool allowsGameBoyCartridgeEra, bool allowsGameBoyAdvanceCartridgeEra);
    }
}

#endif
