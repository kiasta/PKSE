#include "Legality/Encounters/EncounterRules9.h"

#include <span>
#include <string_view>

#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterLocations8.h"
#include "Legality/Encounters/EncounterRules8.h"
#include "Names/RibbonNames.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            constexpr uint8_t HIDDEN_ABILITY_NUMBER = 4;

            struct WeatherMarkStem
            {
                std::string_view spriteStem;
                WeatherMark mark;
            };

            constexpr WeatherMarkStem WEATHER_MARK_STEMS[] = {
                {"ribbonmarkcloudy", WeatherMark::Cloudy},
                {"ribbonmarkrainy", WeatherMark::Rainy},
                {"ribbonmarkstormy", WeatherMark::Stormy},
                {"ribbonmarksnowy", WeatherMark::Snowy},
                {"ribbonmarkblizzard", WeatherMark::Blizzard},
                {"ribbonmarkdry", WeatherMark::Dry},
                {"ribbonmarksandstorm", WeatherMark::Sandstorm},
                {"ribbonmarkmisty", WeatherMark::Misty},
            };
        }

        RibbonMarks9 readRibbonMarks9(const Pokemon::Pokemon &pokemon)
        {
            RibbonMarks9 marks;
            const std::span<const Names::RibbonDefinition> ribbons = Names::getRibbonDefinitions(pokemon.getGameGroup());
            const uint8_t *entityData = reinterpret_cast<const uint8_t *>(pokemon.getData().data());
            const Names::RibbonDefinition *firstMark = nullptr;
            const Names::RibbonDefinition *lastMark = nullptr;
            WeatherMark firstWeather = WeatherMark::None;
            for (const Names::RibbonDefinition &ribbon : ribbons)
            {
                if (ribbon.spriteStem == nullptr)
                {
                    continue;
                }
                const std::string_view spriteStem = ribbon.spriteStem;
                const bool isSet = Names::getRibbonValue(entityData, ribbon) != 0;
                if (spriteStem == "ribbonmarklunchtime")
                {
                    firstMark = &ribbon;
                    marks.isPresent = true;
                    marks.lunchtime = isSet;
                }
                else if (spriteStem == "ribbonmarkslump")
                {
                    lastMark = &ribbon;
                }
                else if (spriteStem == "ribbonmarksleepytime")
                {
                    marks.sleepyTime = isSet;
                }
                else if (spriteStem == "ribbonmarkdusk")
                {
                    marks.dusk = isSet;
                }
                else if (spriteStem == "ribbonmarkdawn")
                {
                    marks.dawn = isSet;
                }
                else if (spriteStem == "ribbonmarkcrafty")
                {
                    marks.crafty = isSet;
                }
                if (!isSet)
                {
                    continue;
                }
                for (const WeatherMarkStem &weatherStem : WEATHER_MARK_STEMS)
                {
                    // HasWeatherMark tests in a fixed order, so the earliest one in that order wins.
                    if (weatherStem.spriteStem == spriteStem && (firstWeather == WeatherMark::None || weatherStem.mark < firstWeather))
                    {
                        firstWeather = weatherStem.mark;
                    }
                }
            }
            marks.weather = firstWeather;
            if (firstMark == nullptr || lastMark == nullptr)
            {
                return marks;
            }
            const int firstMarkIndex = Names::getRibbonAffixIndex(*firstMark);
            const int lastMarkIndex = Names::getRibbonAffixIndex(*lastMark);
            for (const Names::RibbonDefinition &ribbon : ribbons)
            {
                const int ribbonIndex = Names::getRibbonAffixIndex(ribbon);
                if (ribbonIndex >= firstMarkIndex && ribbonIndex <= lastMarkIndex && Names::getRibbonValue(entityData, ribbon) != 0)
                {
                    marks.hasEncounterMark = true;
                    break;
                }
            }
            return marks;
        }

        // PKHeX AreaWeather9Extensions.IsMarkCompatible
        bool isMarkCompatible9(uint8_t weather, WeatherMark mark) noexcept
        {
            switch (mark)
            {
            case WeatherMark::Cloudy:
                return (weather & WEATHER9_OVERCAST) != 0;
            case WeatherMark::Rainy:
                return (weather & WEATHER9_RAINING) != 0;
            case WeatherMark::Stormy:
                return (weather & WEATHER9_THUNDERSTORM) != 0;
            case WeatherMark::Snowy:
                return (weather & WEATHER9_SNOWING) != 0;
            case WeatherMark::Blizzard:
                return (weather & WEATHER9_SNOWSTORM) != 0;
            case WeatherMark::Sandstorm:
                return (weather & WEATHER9_SANDSTORM) != 0;
            case WeatherMark::Misty:
                return (weather & WEATHER9_MIST) != 0;
            default:
                return false;
            }
        }

        // PKHeX LocationsHOME.GetRemapState
        LocationRemapState getRemapState(Enums::GameVersion originalContextGroup, Enums::GameVersion currentContextGroup) noexcept
        {
            if (currentContextGroup == originalContextGroup)
            {
                return LocationRemapState::Original;
            }
            if (currentContextGroup == Enums::GameVersion::SWSH)
            {
                return LocationRemapState::Remapped;
            }
            const int originalGeneration = Enums::getVersionGeneration(Enums::getGroupRepVersion(originalContextGroup));
            if (originalGeneration < 8)
            {
                return LocationRemapState::Original;
            }
            if (originalGeneration == 8)
            {
                return LocationRemapState::Either;
            }
            const bool isDownToGen8 = currentContextGroup == Enums::GameVersion::PLA || currentContextGroup == Enums::GameVersion::BDSP;
            return isDownToGen8 ? LocationRemapState::Either : LocationRemapState::Original;
        }

        bool isValidMetScarletViolet(uint16_t location, uint8_t version) noexcept
        {
            return (location == LOCATION_HOME_SHVL && version == static_cast<uint8_t>(Enums::GameVersion::SH)) || (location == LOCATION_HOME_SWSL && version == static_cast<uint8_t>(Enums::GameVersion::SW));
        }

        uint16_t getMetSwordShield(uint16_t location, uint8_t version) noexcept
        {
            // PKHeX GameVersion.FRX and LGX: the ids HOME writes for FireRed/LeafGreen on Switch.
            constexpr uint8_t versionFireRedSwitch = 56;
            constexpr uint8_t versionLeafGreenSwitch = 57;
            switch (version)
            {
            case static_cast<uint8_t>(Enums::GameVersion::PLA):
                return LOCATION_HOME_SWLA;
            case static_cast<uint8_t>(Enums::GameVersion::BD):
                return LOCATION_HOME_SWBD;
            case static_cast<uint8_t>(Enums::GameVersion::SP):
                return LOCATION_HOME_SHSP;
            case static_cast<uint8_t>(Enums::GameVersion::SL):
                return LOCATION_HOME_SWSL;
            case static_cast<uint8_t>(Enums::GameVersion::VL):
                return LOCATION_HOME_SHVL;
            case versionFireRedSwitch:
                return LOCATION_HOME_SWFR;
            case versionLeafGreenSwitch:
                return LOCATION_HOME_SHLG;
            default:
                return location;
            }
        }

        bool isMatchLocation9(const Pokemon::Pokemon &pokemon, bool isExactMatch, uint16_t templateLocation, Enums::GameVersion templateContextGroup)
        {
            const Enums::GameVersion currentContextGroup = getEntityContextGroup(pokemon);
            const LocationRemapState metState = getRemapState(templateContextGroup, currentContextGroup);
            if (metState == LocationRemapState::Original)
            {
                return isExactMatch;
            }
            const uint16_t metLocation = pokemon.metLocation();
            const uint8_t version = getOriginVersion(pokemon);
            bool isRemappedMatch = false;
            if (currentContextGroup == Enums::GameVersion::SWSH)
            {
                isRemappedMatch = isValidMetScarletViolet(metLocation, version);
            }
            else
            {
                isRemappedMatch = getMetSwordShield(templateLocation, version) == metLocation;
            }
            if (metState == LocationRemapState::Remapped)
            {
                return isRemappedMatch;
            }
            return isExactMatch || isRemappedMatch;
        }

        bool isMatchEggLocationInternal9(const Pokemon::Pokemon &pokemon, uint16_t templateEggLocation, Enums::GameVersion templateContextGroup)
        {
            const LocationRemapState metState = getRemapState(templateContextGroup, getEntityContextGroup(pokemon));
            const bool isRemappedMatch = pokemon.eggLocation() == 0;
            if (metState == LocationRemapState::Remapped)
            {
                return isRemappedMatch;
            }
            const bool isOriginalMatch = isMatchEggLocation(pokemon, templateEggLocation);
            if (metState == LocationRemapState::Original)
            {
                return isOriginalMatch;
            }
            return isOriginalMatch || isRemappedMatch;
        }

        EncounterMatchRating getAbilityDeferral9(const Pokemon::Pokemon &pokemon, RNG::AbilityPermission ability, uint16_t speciesId, uint8_t formId, Enums::GameVersion group)
        {
            if (ability == RNG::AbilityPermission::Any12H)
            {
                return EncounterMatchRating::Match;
            }
            // Hidden-only is a strict match; an Ability Capsule or Patch can change the others.
            const uint8_t abilityNumber = pokemon.abilityNumber();
            if (abilityNumber == HIDDEN_ABILITY_NUMBER)
            {
                if (ability != RNG::AbilityPermission::OnlyHidden && !canAbilityPatch(9, speciesId, formId, group, pokemon.speciesID()))
                {
                    return EncounterMatchRating::DeferredErrors;
                }
                return EncounterMatchRating::Match;
            }
            const int singleIndex = RNG::getSingleValue(ability);
            if (singleIndex < 0 || (1 << singleIndex) == abilityNumber)
            {
                return EncounterMatchRating::Match;
            }
            RNG::AbilityPermission effective = ability;
            if (effective == RNG::AbilityPermission::OnlyHidden)
            {
                if (!canAbilityPatch(9, speciesId, formId, group, pokemon.speciesID()))
                {
                    return EncounterMatchRating::DeferredErrors;
                }
                effective = abilityNumber == 1 ? RNG::AbilityPermission::OnlyFirst : RNG::AbilityPermission::OnlySecond;
            }
            const bool isFixedRegular = effective == RNG::AbilityPermission::OnlyFirst || effective == RNG::AbilityPermission::OnlySecond;
            if (isFixedRegular && !canAbilityCapsule(9, speciesId, formId, group))
            {
                return EncounterMatchRating::DeferredErrors;
            }
            return EncounterMatchRating::Match;
        }

        bool isShinyStateMismatch(RNG::Shiny shiny, const Pokemon::Pokemon &pokemon) noexcept
        {
            if (shiny == RNG::Shiny::Never)
            {
                return isShiny(pokemon);
            }
            if (shiny == RNG::Shiny::Always)
            {
                return !isShiny(pokemon);
            }
            return false;
        }

        bool isPartialMatchHiddenAbility(const Pokemon::Pokemon &pokemon, uint16_t templateSpeciesId) noexcept
        {
            return pokemon.abilityNumber() == HIDDEN_ABILITY_NUMBER && isPartialMatchHidden(pokemon.speciesID(), templateSpeciesId);
        }

        bool isSizeWithinRange(RNG::SizeType9 type, uint8_t sizeValue) noexcept
        {
            switch (type)
            {
            case RNG::SizeType9::RANDOM:
                return true;
            case RNG::SizeType9::XS:
                return sizeValue < 0x10;
            case RNG::SizeType9::S:
                return sizeValue >= 0x10 && sizeValue < 0x30;
            case RNG::SizeType9::M:
                return sizeValue >= 0x30 && sizeValue < 0xD0;
            case RNG::SizeType9::L:
                return sizeValue >= 0xD0 && sizeValue < 0xF0;
            case RNG::SizeType9::XL:
                return sizeValue >= 0xF0;
            default:
                return false;
            }
        }

        const char *getVersionName9(Enums::GameVersion version) noexcept
        {
            switch (version)
            {
            case Enums::GameVersion::SL:
                return "SL";
            case Enums::GameVersion::VL:
                return "VL";
            case Enums::GameVersion::ZA:
                return "ZA";
            default:
                return "SV";
            }
        }
    }
}
