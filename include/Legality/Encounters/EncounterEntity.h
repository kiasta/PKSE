/**
 * The PKHeX PKM properties encounter templates read that PKSE's Pokemon does not answer directly
 * (PKHeX PKHeX.Core/PKM/PKM.cs).
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERENTITY_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERENTITY_H

#include <array>
#include <cstdint>

#include "Enums/GameVersion.h"

namespace Pokemon
{
    class Pokemon;
}

namespace Legality
{
    namespace Encounters
    {
        /// PKHeX PKM.Format.
        uint8_t getFormatGeneration(const Pokemon::Pokemon &pokemon) noexcept;
        /// PKHeX PKM.Generation.
        uint8_t getOriginGeneration(const Pokemon::Pokemon &pokemon);
        /// PKHeX PKM.Version as stored; 0 for a format that stores none.
        uint8_t getOriginVersion(const Pokemon::Pokemon &pokemon) noexcept;
        /// PKHeX PKM.WasEgg.
        bool wasEgg(const Pokemon::Pokemon &pokemon) noexcept;
        /// PKHeX PKM.HasOriginalMetLevel.
        bool hasOriginalMetLevel(const Pokemon::Pokemon &pokemon);
        /// PKHeX PKM.IsShiny.
        bool isShiny(const Pokemon::Pokemon &pokemon) noexcept;
        /// PKHeX PKM.E / FRLG / RS checks on the stored version.
        bool isVersion(const Pokemon::Pokemon &pokemon, Enums::GameVersion version) noexcept;
        /// PKHeX PKM.Context as a context group.
        Enums::GameVersion getEntityContextGroup(const Pokemon::Pokemon &pokemon) noexcept;
        /// PKHeX's GameVersion enum name for a version or group ("R", "FR", "RSE"), as PKHeX's
        /// template names print it; "Any" for one it has no name for here.
        const char *getVersionSourceName(Enums::GameVersion version) noexcept;
        /// PKHeX FormInfo.IsFormChangeable, with both contexts as context groups.
        bool isFormChangeable(uint16_t speciesId, uint8_t oldFormId, uint8_t newFormId, Enums::GameVersion originContextGroup, Enums::GameVersion currentContextGroup) noexcept;
        /// PKHeX EncounterStateUtil. A struct, so callers qualify the name: two enumerators keep
        /// file-local copies of canBeWildEncounter that an unqualified call would clash with.
        struct EncounterStateUtil
        {
            /// PKHeX EncounterStateUtil.CanBeWildEncounter: not an egg, and not met as one (IsMetAsEgg).
            static bool canBeWildEncounter(const Pokemon::Pokemon &pokemon) noexcept;
        };

        /// PKHeX IContestStatsReadOnly extensions, qualified for the same reason.
        struct ContestStatsReadOnly
        {
            /// PKHeX IsContestBelow: any contest stat (Cool, Beauty, Cute, Smart, Tough, Sheen) is
            /// below the template's; false for a format without contest stats.
            static bool isContestBelow(const Pokemon::Pokemon &pokemon, const std::array<uint8_t, 6> &initialStats) noexcept;
        };
    }
}

#endif
