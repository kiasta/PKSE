#include "Legality/Encounters/EncounterSlot8.h"

#include "Legality/Encounters/EncounterData8.h"
#include "Legality/Encounters/EncounterLocations8.h"
#include "Legality/RNG/Overworld8RNG.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            constexpr uint16_t SPECIES_ROTOM = 479;
            constexpr uint16_t SPECIES_SHEDINJA = 292;
            constexpr uint8_t HIDDEN_ABILITY_NUMBER = 4;
            /// Galar Mine and Galar Mine No. 2, whose hidden slots are reached only by curry or fishing.
            constexpr uint8_t LOCATION_GALAR_MINE = 30;
            constexpr uint8_t LOCATION_GALAR_MINE2 = 54;
        }

        const char *getSlotTypeName8(SlotType8 type) noexcept
        {
            switch (type)
            {
            case SlotType8::SymbolMain:
                return "SymbolMain";
            case SlotType8::SymbolMain2:
                return "SymbolMain2";
            case SlotType8::SymbolMain3:
                return "SymbolMain3";
            case SlotType8::HiddenMain:
                return "HiddenMain";
            case SlotType8::HiddenMain2:
                return "HiddenMain2";
            case SlotType8::Surfing:
                return "Surfing";
            case SlotType8::Surfing2:
                return "Surfing2";
            case SlotType8::Sky:
                return "Sky";
            case SlotType8::Sky2:
                return "Sky2";
            case SlotType8::Ground:
                return "Ground";
            case SlotType8::Ground2:
                return "Ground2";
            case SlotType8::Sharpedo:
                return "Sharpedo";
            case SlotType8::OnlyFishing:
                return "OnlyFishing";
            case SlotType8::Inaccessible:
                return "Inaccessible";
            }
            return "";
        }

        bool canCrossover8(SlotType8 type) noexcept
        {
            return type != SlotType8::HiddenMain && type != SlotType8::HiddenMain2 && type != SlotType8::OnlyFishing;
        }

        bool canEncounterViaFishing8(SlotType8 type, uint16_t weather) noexcept
        {
            return type == SlotType8::OnlyFishing || (weather & WEATHER8_FISHING) != 0;
        }

        bool canEncounterViaCurry8(SlotType8 type) noexcept
        {
            return type == SlotType8::HiddenMain || type == SlotType8::HiddenMain2;
        }

        uint16_t getAreaWeather8(uint8_t location) noexcept
        {
            constexpr uint16_t ISLAND_COAST = WEATHER8_NORMAL | WEATHER8_OVERCAST | WEATHER8_STORMY | WEATHER8_INTENSE_SUN | WEATHER8_HEAVY_FOG;
            constexpr uint16_t TUNDRA_SLOPE = WEATHER8_NORMAL | WEATHER8_OVERCAST | WEATHER8_INTENSE_SUN | WEATHER8_ICY | WEATHER8_HEAVY_FOG;
            switch (location)
            {
            case 68: return WEATHER8_INTENSE_SUN; // Route 6
            case 88: // Route 8 (Steamdrift Way)
            case 90: // Route 9
            case 92: return WEATHER8_SNOWING; // Route 9 (Circhester Bay)
            case 94: return WEATHER8_OVERCAST; // Route 9 (Outer Spikemuth)
            case 106: return WEATHER8_SNOWSTORM; // Route 10
            case 122: case 124: case 126: case 128: case 130: case 132: case 134: case 136: case 138: case 140: case 142: case 144:
            case 146: case 148: case 150: case 152: case 154:
                return WEATHER8_ALL; // the Wild Area, Rolling Fields to Lake of Outrage
            case 164: // Fields of Honor
            case 166: // Soothing Wetlands
            case 170: // Challenge Beach
            case 178: // Loop Lagoon
            case 186: // Workout Sea
            case 188: // Stepping-Stone Sea
            case 190: // Insular Sea
            case 192: // Honeycalm Sea
            case 194: // Honeycalm Island
                return ISLAND_COAST;
            case 168: // Forest of Focus
            case 174: // Challenge Road
            case 180: // Training Lowlands
                return WEATHER8_ALL_ISLE_OF_ARMOR;
            case 184: return WEATHER8_NORMAL | WEATHER8_OVERCAST | WEATHER8_RAINING | WEATHER8_SANDSTORM | WEATHER8_INTENSE_SUN | WEATHER8_HEAVY_FOG; // Potbottom Desert
            case 204: // Slippery Slope
            case 208: // Frostpoint Field
            case 214: // Snowslide Slope
            case 218: // Path to the Peak
                return TUNDRA_SLOPE;
            case 210: // Giant's Bed
            case 212: // Old Cemetery
            case 222: // Giant's Foot
            case 228: // Three-Point Pass
                return WEATHER8_ALL_CROWN_TUNDRA;
            case 216: // Tunnel to the Top
            case 224: // Roaring-Sea Caves
            case 232: // Lakeside Cave
                return WEATHER8_OVERCAST;
            case 226: return WEATHER8_NO_SUN_SAND; // Frigid Sea
            case 230: return WEATHER8_ALL_BALLIMERE; // Ballimere Lake
            default: return WEATHER8_NORMAL;
            }
        }

        namespace
        {
            /// PKHeX EncounterArea8.GetWeatherBleedSymbol.
            uint16_t getWeatherBleedSymbol8(uint8_t location) noexcept
            {
                switch (location)
                {
                case 166: // Soothing Wetlands from Forest of Focus
                case 170: // Challenge Beach from Forest of Focus
                case 182: // Warm-Up Tunnel from Training Lowlands
                    return WEATHER8_ALL_ISLE_OF_ARMOR;
                case 208: // Frostpoint Field from Giant's Bed
                case 224: // Roaring-Sea Caves from Three-Point Pass
                case 230: // Ballimere Lake from Giant's Bed
                    return WEATHER8_ALL_CROWN_TUNDRA;
                case 216: return WEATHER8_NORMAL | WEATHER8_OVERCAST | WEATHER8_INTENSE_SUN | WEATHER8_ICY | WEATHER8_HEAVY_FOG; // Tunnel to the Top from Path to the Peak
                case 232: return WEATHER8_ALL_BALLIMERE; // Lakeside Cave from Ballimere Lake
                default: return WEATHER8_NONE;
                }
            }

            /// PKHeX EncounterArea8.GetWeatherBleedHiddenGrass.
            uint16_t getWeatherBleedHiddenGrass8(uint8_t location) noexcept
            {
                switch (location)
                {
                case 166: // Soothing Wetlands from Forest of Focus
                case 170: // Challenge Beach from Forest of Focus
                    return WEATHER8_ALL_ISLE_OF_ARMOR;
                case 208: // Frostpoint Field from Giant's Bed
                case 230: // Ballimere Lake from Giant's Bed
                    return WEATHER8_ALL_CROWN_TUNDRA;
                default: return WEATHER8_NONE;
                }
            }

            /// PKHeX EncounterArea8.GetWeatherBleedSymbolSurfing / GetWeatherBleedSymbolSharpedo.
            uint16_t getWeatherBleedSurfing8(uint8_t location, bool isSharpedo) noexcept
            {
                if (location == 192) // Honeycalm Sea from Training Lowlands
                {
                    return WEATHER8_ALL_ISLE_OF_ARMOR;
                }
                if (location == 224 && !isSharpedo) // Roaring-Sea Caves from Giant's Foot
                {
                    return WEATHER8_ALL_CROWN_TUNDRA;
                }
                return WEATHER8_NONE;
            }
        }

        bool isWeatherBleedPossible8(SlotType8 type, uint16_t permit, uint8_t location) noexcept
        {
            uint16_t weather = WEATHER8_NONE;
            switch (type)
            {
            case SlotType8::SymbolMain:
            case SlotType8::SymbolMain2:
            case SlotType8::SymbolMain3:
                weather = getWeatherBleedSymbol8(location);
                break;
            case SlotType8::HiddenMain:
            case SlotType8::HiddenMain2:
                weather = getWeatherBleedHiddenGrass8(location);
                break;
            case SlotType8::Surfing:
                weather = getWeatherBleedSurfing8(location, false);
                break;
            case SlotType8::Sharpedo:
                weather = getWeatherBleedSurfing8(location, true);
                break;
            default:
                break;
            }
            return (weather & permit) == permit; // PKHeX Enum.HasFlag
        }

        const EncounterSlot8 &EncounterArea8::slotAt(uint16_t slotIndex) const noexcept
        {
            return areaSlots[slotIndex];
        }

        std::span<const uint8_t> EncounterArea8::getAreasCanWanderTo(uint8_t location) noexcept
        {
            return getAreasCanWanderTo8(location);
        }

        // PKHeX EncounterArea8.IsMatchLocation
        bool EncounterArea8::isMatchLocation(uint16_t location) const noexcept
        {
            if (areaLocation == location)
            {
                return true;
            }
            if (!areaPermitCrossover)
            {
                return false;
            }
            if (location > UINT8_MAX)
            {
                return false;
            }
            for (const uint8_t destination : getAreasCanWanderTo(areaLocation))
            {
                if (destination == location)
                {
                    return true;
                }
            }
            return false;
        }

        std::string EncounterSlot8::name() const
        {
            return std::string("Wild Encounter (") + getVersionName8(version()) + ")";
        }

        std::string EncounterSlot8::longName() const
        {
            return name() + " [" + getSlotTypeName8(slotType) + "] - " + getWeatherName8(slotWeather);
        }

        // PKHeX EncounterSlot8.CanEncounterViaCurry
        bool EncounterSlot8::canEncounterViaCurry() const noexcept
        {
            if (!canEncounterViaCurry8(slotType))
            {
                return false;
            }
            if ((slotWeather & WEATHER8_ALL) == 0)
            {
                return false;
            }
            return !isWildAreaSwordShield(location());
        }

        // PKHeX EncounterSlot8.GetRequirement
        OverworldCorrelation8Requirement EncounterSlot8::getRequirement(const Pokemon::Pokemon &pokemon) const
        {
            if (parent->permitCrossover())
            {
                return OverworldCorrelation8Requirement::MustHave;
            }
            const RibbonMarks8 marks = readRibbonMarks8(pokemon);
            const bool isCurry = marks.curry || (pokemon.speciesID() == SPECIES_SHEDINJA && marks.isAffixedCurry);
            if (isCurry)
            {
                return OverworldCorrelation8Requirement::MustNotHave;
            }
            // Tree encounters are generated from the global seed, not the 32-bit spawn seed.
            if ((slotWeather & WEATHER8_SHAKING_TREES) != 0)
            {
                return slotWeather == WEATHER8_SHAKING_TREES ? OverworldCorrelation8Requirement::MustNotHave : OverworldCorrelation8Requirement::CanBeEither;
            }
            return OverworldCorrelation8Requirement::MustHave;
        }

        bool EncounterSlot8::isOverworldCorrelationCorrect(const Pokemon::Pokemon &pokemon) const
        {
            return RNG::Overworld8RNG::validateOverworldEncounter(pokemon, RNG::Shiny::FixedValue, getFlawlessIVCountForMetLevel(pokemon.metLevel()));
        }

        // PKHeX EncounterSlot8.GetFlawlessIVCount
        int EncounterSlot8::getFlawlessIVCountForMetLevel(uint8_t metLevel) const noexcept
        {
            constexpr int noFlawless = 0;
            constexpr int anyOf023 = -1;
            // Brilliant encounters are boosted to the slot's maximum level.
            if (metLevel < slotLevelMax)
            {
                return noFlawless;
            }
            if (parent->permitCrossover())
            {
                return anyOf023;
            }
            if ((slotWeather & WEATHER8_FISHING) != 0)
            {
                return anyOf023;
            }
            return noFlawless;
        }

        // PKHeX EncounterSlot8.IsMatchExact
        bool EncounterSlot8::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            if (slotFormId != evolution.formId && slotSpeciesId != SPECIES_ROTOM)
            {
                return false;
            }
            const uint16_t metLocation = pokemon.metLocation();
            if (location() != metLocation && !EncounterArea8::canCrossoverTo(slotType))
            {
                return false;
            }
            const uint8_t metLevel = pokemon.metLevel();
            if (metLevel == BOOST_LEVEL8 && isBoostedArea60(location()))
            {
                return true;
            }
            if (!isLevelWithinRange(metLevel))
            {
                return false;
            }
            // Heavy Fog is post-game only, which the level 60 boost above already accepts.
            if (slotWeather == WEATHER8_HEAVY_FOG && isWildArea8(location()))
            {
                return false;
            }
            return true;
        }

        // PKHeX EncounterSlot8.GetMatchRating
        EncounterMatchRating EncounterSlot8::getMatchRating(const Pokemon::Pokemon &pokemon) const
        {
            const bool isHidden = pokemon.abilityNumber() == HIDDEN_ABILITY_NUMBER;
            if (isHidden && isPartialMatchHidden(pokemon.speciesID(), slotSpeciesId))
            {
                return EncounterMatchRating::PartialMatch;
            }
            const RibbonMarks8 marks = readRibbonMarks8(pokemon);
            if (marks.isPresent)
            {
                if (marks.curry && (slotWeather & WEATHER8_ALL) == 0)
                {
                    return EncounterMatchRating::DeferredErrors;
                }
                if (marks.fishing && (slotWeather & WEATHER8_FISHING) == 0)
                {
                    return EncounterMatchRating::DeferredErrors;
                }
                // Tree and fishing slots are deferred here and checked later.
                if (!isMarkCompatible(slotWeather, marks))
                {
                    return EncounterMatchRating::DeferredErrors;
                }
                const bool isGalarMine = location() == LOCATION_GALAR_MINE || location() == LOCATION_GALAR_MINE2;
                if (isGalarMine && slotType == SlotType8::HiddenMain && !marks.curry && !canEncounterViaFishing())
                {
                    return EncounterMatchRating::DeferredErrors;
                }
            }
            const OverworldCorrelation8Requirement requirement = getRequirement(pokemon);
            if (requirement == OverworldCorrelation8Requirement::MustHave && !isOverworldCorrelationCorrect(pokemon))
            {
                return EncounterMatchRating::DeferredErrors;
            }
            if (requirement == OverworldCorrelation8Requirement::MustNotHave && isOverworldCorrelationCorrect(pokemon))
            {
                return EncounterMatchRating::DeferredErrors;
            }
            return EncounterMatchRating::Match;
        }
    }
}
