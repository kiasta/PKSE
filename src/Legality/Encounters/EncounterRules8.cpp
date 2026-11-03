#include "Legality/Encounters/EncounterRules8.h"

#include <array>
#include <string_view>

#include "Conversion/Convert.h"
#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterLocations8.h"
#include "Names/RibbonNames.h"
#include "Pokemon/AbilityInfo.h"
#include "Pokemon/Pokemon.h"
#include "Pokemon/Pokemon8SWSH.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            constexpr uint16_t SPECIES_METAPOD = 11;
            constexpr uint16_t SPECIES_KAKUNA = 14;
            constexpr uint16_t SPECIES_MEOWTH = 52;
            constexpr uint16_t SPECIES_PIKACHU = 25;
            constexpr uint16_t SPECIES_PUPITAR = 247;
            constexpr uint16_t SPECIES_SILCOON = 266;
            constexpr uint16_t SPECIES_CASCOON = 268;
            constexpr uint16_t SPECIES_VIBRAVA = 329;
            constexpr uint16_t SPECIES_FLYGON = 330;
            constexpr int MAX_INDIVIDUAL_VALUE = 31;

            struct WeatherName
            {
                uint16_t weather;
                const char *weatherName;
            };

            // PKHeX AreaWeather8's members, largest value first: .NET's flags ToString() takes every
            // member whose bits are all still unclaimed, then prints the taken ones smallest first.
            constexpr std::array<WeatherName, 19> WEATHER_NAMES = {{
                {WEATHER8_SHAKING_TREES | WEATHER8_FISHING, "NotWeather"},
                {WEATHER8_FISHING, "Fishing"},
                {WEATHER8_SHAKING_TREES, "ShakingTrees"},
                {WEATHER8_ALL, "All"},
                {WEATHER8_ALL_ISLE_OF_ARMOR, "AllIoA"},
                {WEATHER8_ALL_CROWN_TUNDRA, "AllCT"},
                {WEATHER8_NO_SUN_SAND, "NoSunSand"},
                {WEATHER8_ALL_BALLIMERE, "AllBallimere"},
                {WEATHER8_HEAVY_FOG, "HeavyFog"},
                {WEATHER8_SANDSTORM, "Sandstorm"},
                {WEATHER8_ICY, "Icy"},
                {WEATHER8_SNOWSTORM, "Snowstorm"},
                {WEATHER8_SNOWING, "Snowing"},
                {WEATHER8_INTENSE_SUN, "IntenseSun"},
                {WEATHER8_STORMY, "Stormy"},
                {WEATHER8_THUNDERSTORM, "Thunderstorm"},
                {WEATHER8_RAINING, "Raining"},
                {WEATHER8_OVERCAST, "Overcast"},
                {WEATHER8_NORMAL, "Normal"},
            }};

            bool isPartialMatchHiddenSpecies(uint16_t speciesId) noexcept
            {
                return speciesId == SPECIES_METAPOD || speciesId == SPECIES_KAKUNA || speciesId == SPECIES_PUPITAR || speciesId == SPECIES_SILCOON || speciesId == SPECIES_CASCOON || speciesId == SPECIES_VIBRAVA || speciesId == SPECIES_FLYGON;
            }

            // PKHeX Gigantamax.CanToggle(species): what Max Soup can switch.
            bool canToggleGigantamaxSpecies(uint16_t speciesId) noexcept
            {
                constexpr std::array<uint16_t, 31> toggleSpecies = {3, 6, 9, 12, SPECIES_PIKACHU, SPECIES_MEOWTH, 68, 94, 99, 131, 133, 143, 569, 812, 815, 818, 823, 826, 834, 839, 841, 842, 844, 849, 851, 858, 861, 869, 879, 884, 892};
                for (const uint16_t toggleSpeciesId : toggleSpecies)
                {
                    if (toggleSpeciesId == speciesId)
                    {
                        return true;
                    }
                }
                return false;
            }

            // PKHeX AbilityChangeRules.IsFormChangeDifferentHidden.
            bool isFormChangeDifferentHidden(uint16_t speciesId) noexcept
            {
                constexpr std::array<uint16_t, 5> formChangeSpecies = {487, 641, 642, 645, 905};
                for (const uint16_t formChangeSpeciesId : formChangeSpecies)
                {
                    if (formChangeSpeciesId == speciesId)
                    {
                        return true;
                    }
                }
                return false;
            }
        }

        std::string getWeatherName8(uint16_t weather)
        {
            if (weather == WEATHER8_NONE)
            {
                return "None";
            }
            std::array<bool, WEATHER_NAMES.size()> isTaken{};
            uint16_t remaining = weather;
            for (size_t nameIndex = 0; nameIndex < WEATHER_NAMES.size(); nameIndex++)
            {
                const uint16_t memberWeather = WEATHER_NAMES[nameIndex].weather;
                if ((remaining & memberWeather) == memberWeather)
                {
                    remaining = static_cast<uint16_t>(remaining & ~memberWeather);
                    isTaken[nameIndex] = true;
                }
            }
            if (remaining != 0)
            {
                return std::to_string(weather);
            }
            std::string weatherText;
            for (size_t nameIndex = WEATHER_NAMES.size(); nameIndex-- > 0;)
            {
                if (!isTaken[nameIndex])
                {
                    continue;
                }
                if (!weatherText.empty())
                {
                    weatherText += ", ";
                }
                weatherText += WEATHER_NAMES[nameIndex].weatherName;
            }
            return weatherText;
        }

        RibbonMarks8 readRibbonMarks8(const Pokemon::Pokemon &pokemon)
        {
            RibbonMarks8 marks;
            const std::span<const Names::RibbonDefinition> ribbons = Names::getRibbonDefinitions(pokemon.getGameGroup());
            const uint8_t *entityData = reinterpret_cast<const uint8_t *>(pokemon.getData().data());
            const Names::RibbonDefinition *firstMark = nullptr;
            const Names::RibbonDefinition *lastMark = nullptr;
            int curryIndex = -1;
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
                }
                else if (spriteStem == "ribbonmarkslump")
                {
                    lastMark = &ribbon;
                }
                else if (spriteStem == "ribbonmarkcurry")
                {
                    marks.isPresent = true;
                    marks.curry = isSet;
                    curryIndex = Names::getRibbonAffixIndex(ribbon);
                }
                else if (spriteStem == "ribbonmarkfishing")
                {
                    marks.fishing = isSet;
                }
                else if (spriteStem == "ribbonmarkcloudy")
                {
                    marks.cloudy = isSet;
                }
                else if (spriteStem == "ribbonmarkrainy")
                {
                    marks.rainy = isSet;
                }
                else if (spriteStem == "ribbonmarkstormy")
                {
                    marks.stormy = isSet;
                }
                else if (spriteStem == "ribbonmarksnowy")
                {
                    marks.snowy = isSet;
                }
                else if (spriteStem == "ribbonmarkblizzard")
                {
                    marks.blizzard = isSet;
                }
                else if (spriteStem == "ribbonmarkdry")
                {
                    marks.dry = isSet;
                }
                else if (spriteStem == "ribbonmarksandstorm")
                {
                    marks.sandstorm = isSet;
                }
                else if (spriteStem == "ribbonmarkmisty")
                {
                    marks.misty = isSet;
                }
            }
            if (!marks.isPresent || firstMark == nullptr || lastMark == nullptr)
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
            const size_t affixedOffset = Conversion::affixedRibbonOffset(pokemon.getGameGroup());
            if (affixedOffset != 0 && affixedOffset < pokemon.getData().size())
            {
                const uint8_t affixedValue = static_cast<uint8_t>(pokemon.getData()[affixedOffset]);
                if (affixedValue != Conversion::AFFIXED_RIBBON_NONE)
                {
                    marks.affixedRibbon = affixedValue;
                    marks.isAffixedCurry = marks.affixedRibbon == curryIndex;
                    marks.isAffixedEncounterMark8 = marks.affixedRibbon >= firstMarkIndex && marks.affixedRibbon <= lastMarkIndex;
                }
            }
            return marks;
        }

        // PKHeX AreaWeather8Extensions.IsMarkCompatible
        bool isMarkCompatible(uint16_t weather, const RibbonMarks8 &marks) noexcept
        {
            if (marks.cloudy)
            {
                return (weather & WEATHER8_OVERCAST) != 0;
            }
            if (marks.rainy)
            {
                return (weather & WEATHER8_RAINING) != 0;
            }
            if (marks.stormy)
            {
                return (weather & WEATHER8_THUNDERSTORM) != 0;
            }
            if (marks.snowy)
            {
                return (weather & WEATHER8_SNOWING) != 0;
            }
            if (marks.blizzard)
            {
                return (weather & WEATHER8_SNOWSTORM) != 0;
            }
            if (marks.dry)
            {
                return (weather & WEATHER8_INTENSE_SUN) != 0;
            }
            if (marks.sandstorm)
            {
                return (weather & WEATHER8_SANDSTORM) != 0;
            }
            if (marks.misty)
            {
                return (weather & WEATHER8_HEAVY_FOG) != 0;
            }
            return true;
        }

        bool isPartialMatchHidden(uint16_t currentSpeciesId, uint16_t originalSpeciesId) noexcept
        {
            if (currentSpeciesId == originalSpeciesId)
            {
                return false;
            }
            if (!isPartialMatchHiddenSpecies(originalSpeciesId))
            {
                return false;
            }
            return isPartialMatchHiddenSpecies(currentSpeciesId);
        }

        uint32_t getShinyXorValue(const Pokemon::Pokemon &pokemon) noexcept
        {
            const uint32_t PID = pokemon.pid();
            const uint32_t ID32 = pokemon.id32();
            return ((PID >> 16) ^ (PID & 0xFFFF) ^ (ID32 >> 16) ^ (ID32 & 0xFFFF)) & 0xFFFF;
        }

        bool isShinyValid(RNG::Shiny shiny, const Pokemon::Pokemon &pokemon) noexcept
        {
            switch (shiny)
            {
            case RNG::Shiny::Always:
                return isShiny(pokemon);
            case RNG::Shiny::Never:
                return !isShiny(pokemon);
            case RNG::Shiny::AlwaysSquare:
                return getShinyXorValue(pokemon) == 0;
            case RNG::Shiny::AlwaysStar:
                return getShinyXorValue(pokemon) == 1;
            default:
                return true;
            }
        }

        int getFlawlessIVCount(const Pokemon::Pokemon &pokemon) noexcept
        {
            const std::array<uint8_t, 6> individualValues = {pokemon.ivHP(), pokemon.ivATK(), pokemon.ivDEF(), pokemon.ivSPE(), pokemon.ivSPA(), pokemon.ivSPD()};
            int flawlessCount = 0;
            for (const uint8_t individualValue : individualValues)
            {
                if (individualValue == MAX_INDIVIDUAL_VALUE)
                {
                    flawlessCount++;
                }
            }
            return flawlessCount;
        }

        bool isMatchEggLocation(const Pokemon::Pokemon &pokemon, uint16_t templateEggLocation) noexcept
        {
            const uint16_t expectedEggLocation = pokemon.getGameGroup() == Enums::GameVersion::BDSP ? getLocationLocal8b(templateEggLocation) : templateEggLocation;
            return expectedEggLocation == pokemon.eggLocation();
        }

        bool isFormatSwordShield(const Pokemon::Pokemon &pokemon) noexcept
        {
            return pokemon.getGameGroup() == Enums::GameVersion::SWSH;
        }

        uint8_t getDynamaxLevel(const Pokemon::Pokemon &pokemon) noexcept
        {
            if (!isFormatSwordShield(pokemon))
            {
                return 0;
            }
            return static_cast<const Pokemon::Pokemon8SWSH &>(pokemon).dynamaxLevel();
        }

        bool canGigantamax(const Pokemon::Pokemon &pokemon) noexcept
        {
            if (!isFormatSwordShield(pokemon))
            {
                return false;
            }
            return static_cast<const Pokemon::Pokemon8SWSH &>(pokemon).canGigantamax();
        }

        bool canToggleGigantamax(uint16_t currentSpeciesId, uint8_t currentFormId, uint16_t originSpeciesId) noexcept
        {
            if (currentSpeciesId == SPECIES_MEOWTH || currentSpeciesId == SPECIES_PIKACHU)
            {
                return currentFormId == 0;
            }
            return canToggleGigantamaxSpecies(currentSpeciesId) || (currentSpeciesId != originSpeciesId && canToggleGigantamaxSpecies(originSpeciesId));
        }

        bool canAbilityCapsule(uint8_t formatGeneration, uint16_t speciesId, uint8_t formId, Enums::GameVersion group)
        {
            if (formatGeneration < 6)
            {
                return false;
            }
            const Pokemon::AbilitySlots abilitySlots = Pokemon::getAbilitySlots(speciesId, formId, group);
            return abilitySlots.slot[0] != abilitySlots.slot[1];
        }

        bool canAbilityPatch(uint8_t formatGeneration, uint16_t speciesId, uint8_t formId, Enums::GameVersion group, uint16_t currentSpeciesId)
        {
            if (formatGeneration < 8)
            {
                return false;
            }
            const Pokemon::AbilitySlots abilitySlots = Pokemon::getAbilitySlots(speciesId, formId, group);
            if (abilitySlots.slot[0] != abilitySlots.slot[2] || abilitySlots.slot[1] != abilitySlots.slot[2])
            {
                return true;
            }
            return isFormChangeDifferentHidden(currentSpeciesId);
        }

        const char *getVersionName8(Enums::GameVersion version) noexcept
        {
            switch (version)
            {
            case Enums::GameVersion::SW:
                return "SW";
            case Enums::GameVersion::SH:
                return "SH";
            case Enums::GameVersion::BD:
                return "BD";
            case Enums::GameVersion::SP:
                return "SP";
            case Enums::GameVersion::PLA:
                return "PLA";
            case Enums::GameVersion::SWSH:
                return "SWSH";
            case Enums::GameVersion::BDSP:
                return "BDSP";
            default:
                return "Any";
            }
        }
    }
}
