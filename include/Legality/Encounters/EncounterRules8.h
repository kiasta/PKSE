/**
 * Rules the Gen 8 encounter templates (Sword/Shield, Brilliant Diamond/Shining Pearl, Legends:
 * Arceus) share: the overworld weather flags, the encounter marks a record carries, and the small
 * PKHeX extension methods their matchers call. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Templates/Gen8/AreaWeather8.cs, SlotType8.cs,
 * PKHeX.Core/Legality/Encounters/Templates/Interfaces/IEncounterMatch.cs,
 * PKHeX.Core/Legality/Encounters/Templates/Enums/Shiny.cs, PKHeX.Core/Legality/Encounters/Data/EncounterUtil.cs,
 * PKHeX.Core/Legality/Verifiers/Ability/AbilityVerifier.cs and PKHeX.Core/Legality/Tables/Gigantamax.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERRULES8_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERRULES8_H

#include <cstdint>
#include <string>

#include "Enums/GameVersion.h"
#include "Legality/RNG/GenerateParameters.h"

namespace Pokemon
{
    class Pokemon;
}

namespace Legality
{
    namespace Encounters
    {
        // PKHeX AreaWeather8 (flags).
        constexpr uint16_t WEATHER8_NONE = 0;
        constexpr uint16_t WEATHER8_NORMAL = 1;
        constexpr uint16_t WEATHER8_OVERCAST = 1 << 1;
        constexpr uint16_t WEATHER8_RAINING = 1 << 2;
        constexpr uint16_t WEATHER8_THUNDERSTORM = 1 << 3;
        constexpr uint16_t WEATHER8_INTENSE_SUN = 1 << 4;
        constexpr uint16_t WEATHER8_SNOWING = 1 << 5;
        constexpr uint16_t WEATHER8_SNOWSTORM = 1 << 6;
        constexpr uint16_t WEATHER8_SANDSTORM = 1 << 7;
        constexpr uint16_t WEATHER8_HEAVY_FOG = 1 << 8;
        constexpr uint16_t WEATHER8_ALL = 0x1FF;
        constexpr uint16_t WEATHER8_STORMY = WEATHER8_RAINING | WEATHER8_THUNDERSTORM;
        constexpr uint16_t WEATHER8_ICY = WEATHER8_SNOWING | WEATHER8_SNOWSTORM;
        constexpr uint16_t WEATHER8_ALL_ISLE_OF_ARMOR = WEATHER8_NORMAL | WEATHER8_OVERCAST | WEATHER8_STORMY | WEATHER8_INTENSE_SUN | WEATHER8_SANDSTORM | WEATHER8_HEAVY_FOG;
        constexpr uint16_t WEATHER8_ALL_CROWN_TUNDRA = WEATHER8_NORMAL | WEATHER8_OVERCAST | WEATHER8_STORMY | WEATHER8_INTENSE_SUN | WEATHER8_ICY | WEATHER8_HEAVY_FOG;
        constexpr uint16_t WEATHER8_NO_SUN_SAND = WEATHER8_NORMAL | WEATHER8_OVERCAST | WEATHER8_STORMY | WEATHER8_ICY | WEATHER8_HEAVY_FOG;
        constexpr uint16_t WEATHER8_ALL_BALLIMERE = WEATHER8_NORMAL | WEATHER8_OVERCAST | WEATHER8_STORMY | WEATHER8_INTENSE_SUN | WEATHER8_SNOWING | WEATHER8_HEAVY_FOG;
        constexpr uint16_t WEATHER8_SHAKING_TREES = 1 << 9;
        constexpr uint16_t WEATHER8_FISHING = 1 << 10;

        /// PKHeX OverworldCorrelation8Requirement: whether an overworld spawn's 32-bit seed must explain the record.
        enum class OverworldCorrelation8Requirement : uint8_t
        {
            CanBeEither,
            MustHave,
            MustNotHave,
        };

        /// PKHeX's AreaWeather8.ToString() with '_' removed, as its slot LongName prints it.
        std::string getWeatherName8(uint16_t weather);

        /// The Gen 8 mark ribbons of a record (PKHeX IRibbonSetMark8 and IRibbonSetAffixed).
        struct RibbonMarks8
        {
            /// The record's format has the Gen 8 marks (PKHeX `pk is IRibbonSetMark8`).
            bool isPresent = false;
            bool curry = false;
            bool fishing = false;
            bool cloudy = false;
            bool rainy = false;
            bool stormy = false;
            bool snowy = false;
            bool blizzard = false;
            bool dry = false;
            bool sandstorm = false;
            bool misty = false;
            /// PKHeX HasEncounterMark / HasMarkEncounter8: any mark from Lunchtime to Slump.
            bool hasEncounterMark = false;
            /// PKHeX IRibbonSetAffixed.AffixedRibbon as a RibbonIndex; -1 for none or a format without it.
            int affixedRibbon = -1;
            /// The affixed ribbon is the Curry mark / any Gen 8 encounter mark.
            bool isAffixedCurry = false;
            bool isAffixedEncounterMark8 = false;
        };

        RibbonMarks8 readRibbonMarks8(const Pokemon::Pokemon &pokemon);
        /// PKHeX AreaWeather8Extensions.IsMarkCompatible.
        bool isMarkCompatible(uint16_t weather, const RibbonMarks8 &marks) noexcept;

        /// PKHeX EncounterMatchExtensions.IsPartialMatchHidden.
        bool isPartialMatchHidden(uint16_t currentSpeciesId, uint16_t originalSpeciesId) noexcept;
        /// PKHeX ShinyExtensions.IsValid(Shiny, PKM).
        bool isShinyValid(RNG::Shiny shiny, const Pokemon::Pokemon &pokemon) noexcept;
        /// PKHeX PKM.ShinyXor.
        uint32_t getShinyXorValue(const Pokemon::Pokemon &pokemon) noexcept;
        /// PKHeX PKM.FlawlessIVCount.
        int getFlawlessIVCount(const Pokemon::Pokemon &pokemon) noexcept;
        /// PKHeX EncounterUtil.IsMatchEggLocation: a BDSP record stores "none" as 65535.
        bool isMatchEggLocation(const Pokemon::Pokemon &pokemon, uint16_t templateEggLocation) noexcept;

        /// True when the record is a Sword/Shield PK8, which alone stores the Dynamax level and
        /// Gigantamax flag the templates check (PKHeX `pk is PK8`).
        bool isFormatSwordShield(const Pokemon::Pokemon &pokemon) noexcept;
        uint8_t getDynamaxLevel(const Pokemon::Pokemon &pokemon) noexcept;
        bool canGigantamax(const Pokemon::Pokemon &pokemon) noexcept;
        /// PKHeX Gigantamax.CanToggle(currentSpecies, currentForm, originSpecies, originForm).
        bool canToggleGigantamax(uint16_t currentSpeciesId, uint8_t currentFormId, uint16_t originSpeciesId) noexcept;

        /// PKHeX AbilityVerifier.CanAbilityCapsule / CanAbilityPatch for a format, read from the
        /// personal row of `group` (the encounter's own game).
        bool canAbilityCapsule(uint8_t formatGeneration, uint16_t speciesId, uint8_t formId, Enums::GameVersion group);
        bool canAbilityPatch(uint8_t formatGeneration, uint16_t speciesId, uint8_t formId, Enums::GameVersion group, uint16_t currentSpeciesId);

        /// PKHeX GameVersion names for the Gen 8 games ("SW", "SH", "BD", "SP", "PLA").
        const char *getVersionName8(Enums::GameVersion version) noexcept;
    }
}

#endif
