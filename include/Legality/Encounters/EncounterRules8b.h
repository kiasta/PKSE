/**
 * Rules the Brilliant Diamond/Shining Pearl encounter templates share: the wild slot types, the
 * Underground and Great Marsh location ranges, and the HOME location remap a BDSP Pokemon moved to
 * Sword/Shield gets. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen8b/EncounterArea8b.cs (SlotType8b),
 * PKHeX.Core/Game/Locations/Locations8b.cs and PKHeX.Core/Game/Locations/LocationsHOME.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERRULES8B_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERRULES8B_H

#include <cstdint>

namespace Pokemon
{
    class Pokemon;
}

namespace Legality
{
    namespace Encounters
    {
        /// PKHeX SlotType8b.
        enum class SlotType8b : uint8_t
        {
            Any = 0,
            Grass = 1,
            Surf = 2,
            OldRod = 3,
            GoodRod = 4,
            SuperRod = 5,
            RockSmash = 6,
            Headbutt = 7,
            HoneyTree = 8,
        };

        /// PKHeX's SlotType8b.ToString() with '_' shown as a space, as its slot LongName prints it.
        const char *getSlotTypeName8b(SlotType8b type) noexcept;

        /// PKHeX Locations8b.IsUnderground: the Grand Underground's met locations.
        constexpr bool isUnderground8b(uint16_t location) noexcept { return location >= 508 && location <= 617; }
        /// PKHeX Locations8b.IsMarsh: the Great Marsh's met locations.
        constexpr bool isMarsh8b(uint16_t location) noexcept { return location >= 219 && location <= 224; }

        /// PKHeX LocationsHOME.IsValidMetBDSP (version is the stored version id).
        bool isValidMetBrilliantDiamondShiningPearl(uint16_t location, uint8_t version) noexcept;
        /// PKHeX LocationsHOME.IsLocationSWSHEgg: the egg location HOME writes for a BDSP egg template
        /// whose own egg location is `original`.
        bool isLocationSwordShieldEgg(uint8_t version, uint16_t metLocation, uint16_t eggLocation, uint16_t original) noexcept;
        /// The IsMatchLocation every BDSP template repeats: `isExactMatch` is its IsMatchLocationExact,
        /// and a record HOME moved to Sword/Shield carries the remapped location instead.
        bool isMatchLocation8b(const Pokemon::Pokemon &pokemon, bool isExactMatch, uint16_t templateLocation);
        /// The IsMatchEggLocation of the BDSP templates that are never eggs: `isExactMatch` is its
        /// IsMatchEggLocationExact, and HOME clears the egg location of a record it remaps to Sword/Shield.
        bool isMatchEggLocation8b(const Pokemon::Pokemon &pokemon, bool isExactMatch);
    }
}

#endif
