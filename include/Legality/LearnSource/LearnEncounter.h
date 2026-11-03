/**
 * The facts about the matched encounter template that PKHeX's move verifiers read (PKHeX
 * IEncounterTemplate plus the IMoveset, IRelearn, IEncounterEgg, ISingleMoveBonus queries and the
 * EncounterSlot6AO / EncounterSlot8GO / EncounterStatic8N details). The encounter search fills one
 * from its template (src/Legality/Encounters/EncounterMoveSeam.cpp), so the learn code never
 * depends on the template classes.
 */
#ifndef LEGALITY_LEARNSOURCE_LEARNENCOUNTER_H
#define LEGALITY_LEARNSOURCE_LEARNENCOUNTER_H

#include <array>
#include <cstdint>
#include <span>

#include "Enums/GameVersion.h"

namespace Legality
{
    namespace LearnSource
    {
        /// PKHeX PogoImportFormat, for the two formats whose moves HOME keeps.
        enum class GoImportFormat : uint8_t
        {
            Other,
            PK7,
            PB7,
        };

        struct LearnEncounter
        {
            uint16_t speciesId = 0;
            uint8_t formId = 0;
            uint8_t levelMin = 0;
            uint8_t levelMax = 0;
            /// PKHeX IEncounterTemplate.Version: one game, or a PKSE group for a shared template.
            Enums::GameVersion version = Enums::GameVersion::Invalid;
            uint8_t generation = 0;
            /// PKHeX IEncounterTemplate.Context, as a context group.
            Enums::GameVersion contextGroup = Enums::GameVersion::Invalid;
            /// PKHeX EncounterInvalid.
            bool isInvalid = false;

            /// PKHeX IEncounterEgg (EncounterEgg2 ... EncounterEgg9) and its CanHaveVoltTackle.
            bool isEggTemplate = false;
            bool canHaveVoltTackle = false;

            /// PKHeX IMoveset.Moves (all zero when the template fixes none).
            std::array<uint16_t, 4> moves{};
            /// PKHeX IRelearn.Relearn.
            std::array<uint16_t, 4> relearnMoves{};

            /// PKHeX ISingleMoveBonus.
            bool isMoveBonusPossible = false;
            bool isMoveBonusRequired = false;
            std::span<const uint16_t> moveBonusPossible;
            /// PKHeX `enc is EncounterSlot6AO`: a DexNav slot, whose bonus move LearnGroup6 accepts.
            bool isDexNavSlot = false;

            /// PKHeX EncounterSlot8GO.OriginFormat and GetInitialMoves(pk.MetLevel).
            GoImportFormat goImportFormat = GoImportFormat::Other;
            std::array<uint16_t, 4> goInitialMoves{};

            /// PKHeX EncounterStatic8N.IsDownLeveled(pk), and the template's own LevelMax it was
            /// reduced from.
            bool isDownLeveledRaid = false;
            uint8_t raidLevelMax = 0;

            /**
             * The template fixes moves PKSE cannot see (today only EncounterStatic8N, whose raid
             * moves come from a table PKSE does not carry). An Unobtainable or unparsed verdict then
             * means "unknown", which the verifier keeps to itself rather than reporting.
             */
            bool hasUnknownMoves = false;

            bool hasMoves() const noexcept { return moves[0] != 0; }
            bool hasRelearnMoves() const noexcept { return relearnMoves[0] != 0; }
            /// PKHeX ISingleMoveBonus.IsMoveBonus.
            bool isMoveBonus(uint16_t moveId) const noexcept
            {
                for (const uint16_t bonusMove : moveBonusPossible)
                {
                    if (bonusMove == moveId)
                    {
                        return true;
                    }
                }
                return false;
            }
        };
    }
}

#endif
