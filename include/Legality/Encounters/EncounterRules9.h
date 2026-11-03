/**
 * Rules the Scarlet/Violet and Legends: Z-A encounter templates share: the overworld weather flags,
 * the Gen 9 marks a record carries, the HOME location remap a Gen 9 Pokemon moved to Sword/Shield
 * gets, and the ability deferral every Gen 9 static template repeats. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen9/AreaWeather9.cs, PKHeX.Core/Game/Locations/LocationsHOME.cs,
 * PKHeX.Core/Game/Locations/Locations.cs and the IsMatchDeferred / IsMatchPartial bodies of
 * PKHeX.Core/Legality/Encounters/Templates/Gen9/EncounterStatic9.cs and its siblings.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERRULES9_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERRULES9_H

#include <cstdint>

#include "Enums/GameVersion.h"
#include "Legality/Encounters/Encounterable.h"
#include "Legality/RNG/GenerateParameters.h"

namespace Pokemon
{
    class Pokemon;
}

namespace Legality
{
    namespace Encounters
    {
        // PKHeX AreaWeather9 (flags).
        constexpr uint8_t WEATHER9_NONE = 0;
        constexpr uint8_t WEATHER9_NORMAL = 1;
        constexpr uint8_t WEATHER9_OVERCAST = 1 << 1;
        constexpr uint8_t WEATHER9_RAINING = 1 << 2;
        constexpr uint8_t WEATHER9_THUNDERSTORM = 1 << 3;
        constexpr uint8_t WEATHER9_MIST = 1 << 4;
        constexpr uint8_t WEATHER9_SNOWING = 1 << 5;
        constexpr uint8_t WEATHER9_SNOWSTORM = 1 << 6;
        constexpr uint8_t WEATHER9_SANDSTORM = 1 << 7;

        // PKHeX Locations: the Gen 9 met and egg locations the templates name.
        constexpr uint16_t LOCATION_PICNIC9 = 30023;
        constexpr uint16_t LOCATION_TERA_CAVERN9 = 30024;
        /// PKHeX Locations.HatchLocation9: South Province (Area One).
        constexpr uint16_t LOCATION_HATCH9 = 6;
        /// PKHeX Locations.IsEggLocationBred9.
        constexpr bool isEggLocationBred9(uint16_t location) noexcept { return location == LOCATION_PICNIC9 || location == 30002; }

        /// The weather mark a record carries, in the order PKHeX's HasWeatherMark tests them.
        enum class WeatherMark : uint8_t
        {
            None,
            Cloudy,
            Rainy,
            Stormy,
            Snowy,
            Blizzard,
            Dry,
            Sandstorm,
            Misty,
        };

        /// The marks a Gen 9 template reads (PKHeX IRibbonSetMark8 and IRibbonIndex).
        struct RibbonMarks9
        {
            /// The record's format has the Gen 8 marks (PKHeX `pk is IRibbonSetMark8`).
            bool isPresent = false;
            bool lunchtime = false;
            bool sleepyTime = false;
            bool dusk = false;
            bool dawn = false;
            bool crafty = false;
            /// PKHeX HasWeatherMark: the first weather mark set.
            WeatherMark weather = WeatherMark::None;
            /// PKHeX HasMarkEncounter8 / IRibbonIndex.HasEncounterMark: any mark from Lunchtime to Slump.
            bool hasEncounterMark = false;
        };

        RibbonMarks9 readRibbonMarks9(const Pokemon::Pokemon &pokemon);
        /// PKHeX AreaWeather9Extensions.IsMarkCompatible.
        bool isMarkCompatible9(uint8_t weather, WeatherMark mark) noexcept;

        /// PKHeX LocationRemapState.
        enum class LocationRemapState : uint8_t
        {
            None = 0,
            Original = 1,
            Remapped = 2,
            Either = 3,
        };

        /// PKHeX LocationsHOME.GetRemapState, both contexts as context groups.
        LocationRemapState getRemapState(Enums::GameVersion originalContextGroup, Enums::GameVersion currentContextGroup) noexcept;
        /// PKHeX LocationsHOME.IsValidMetSV (version is the stored version id).
        bool isValidMetScarletViolet(uint16_t location, uint8_t version) noexcept;
        /// PKHeX LocationsHOME.GetMetSWSH (version is the stored version id).
        uint16_t getMetSwordShield(uint16_t location, uint8_t version) noexcept;
        /// The IsMatchLocation every Gen 9 template repeats: `isExactMatch` is its IsMatchLocationExact,
        /// and a record HOME moved to Sword/Shield carries the remapped location instead.
        bool isMatchLocation9(const Pokemon::Pokemon &pokemon, bool isExactMatch, uint16_t templateLocation, Enums::GameVersion templateContextGroup);
        /// The IsMatchEggLocationInternal of the Gen 9 trades and gifts: HOME clears the egg location
        /// of a record it remaps to Sword/Shield.
        bool isMatchEggLocationInternal9(const Pokemon::Pokemon &pokemon, uint16_t templateEggLocation, Enums::GameVersion templateContextGroup);

        /// The IsMatchDeferred ability rule of the Gen 9 statics: a fixed ability the record does not
        /// have is only explained by an Ability Capsule or Patch. `group` is the template's own game.
        EncounterMatchRating getAbilityDeferral9(const Pokemon::Pokemon &pokemon, RNG::AbilityPermission ability, uint16_t speciesId, uint8_t formId, Enums::GameVersion group);
        /// The IsMatchPartial shiny rule: a never-shiny template that is shiny, or the reverse.
        bool isShinyStateMismatch(RNG::Shiny shiny, const Pokemon::Pokemon &pokemon) noexcept;
        /// PKHeX `pk is { AbilityNumber: 4 } && enc.IsPartialMatchHidden(pk.Species, Species)`.
        bool isPartialMatchHiddenAbility(const Pokemon::Pokemon &pokemon, uint16_t templateSpeciesId) noexcept;
        /// PKHeX SizeType9Extensions.IsWithinRange; false for VALUE and unknown types, where PKHeX throws.
        bool isSizeWithinRange(RNG::SizeType9 type, uint8_t sizeValue) noexcept;
        /// PKHeX GameVersion names for the Gen 9 games ("SL", "VL", "SV", "ZA").
        const char *getVersionName9(Enums::GameVersion version) noexcept;
    }
}

#endif
