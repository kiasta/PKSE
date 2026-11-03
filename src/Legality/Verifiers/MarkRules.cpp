// Port of PKHeX PKHeX.Core/Legality/Verifiers/Ribbons/MarkRules.cs and RibbonVerifierMark9.cs:
// which marks an encounter can carry, the affixable range, and the Scarlet/Violet mark ribbons.
#include "Legality/Verifiers/RibbonRules.h"

#include <cstdint>
#include <optional>
#include <string_view>

#include "Enums/GameVersion.h"
#include "Legality/Encounters/EncounterArea8.h"
#include "Legality/Encounters/EncounterLocations8.h"
#include "Legality/Encounters/EncounterMight9.h"
#include "Legality/Encounters/EncounterOutbreak9.h"
#include "Legality/Encounters/EncounterRules8.h"
#include "Legality/Encounters/EncounterRules9.h"
#include "Legality/Encounters/EncounterSlot8.h"
#include "Legality/Encounters/EncounterSlot9.h"
#include "Legality/Encounters/EncounterStatic8.h"
#include "Legality/Encounters/EncounterStatic9.h"
#include "Legality/Encounters/MysteryGiftWC9.h"
#include "Legality/Encounters/TemplateRibbons.h"
#include "Legality/RNG/HomeQuirks.h"
#include "Legality/Verifiers/MiscVerifier.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace
    {
        constexpr uint16_t MEW_SPECIES = 151;
        constexpr uint16_t NINCADA_SPECIES = 290;
        constexpr uint16_t SHEDINJA_SPECIES = 292;

        /// PKHeX HomeQuirks.HasEnteredSetAlphaMark: only HOME sets the Alpha Mark.
        bool hasEnteredSetAlphaMark(const RibbonArguments &arguments)
        {
            return RNG::HomeQuirks::hasEnteredSetAlphaMark(arguments.context.pokemon);
        }

        /// PKHeX IAlphaReadOnly.IsAlpha of the encounter; empty when PKSE cannot tell.
        std::optional<bool> isAlphaEncounter(const RibbonArguments &arguments)
        {
            if (arguments.encounter.encounterMatch == nullptr)
            {
                return std::nullopt;
            }
            return isEncounterAlpha(arguments.context);
        }

        /// The size scalar the Jumbo and Mini Marks read (PKHeX IScaledSize3.Scale, else
        /// IScaledSize.HeightScalar); empty where PKSE does not read the format's scalar.
        std::optional<uint8_t> getSizeScalar(const Pokemon::Pokemon &pokemon)
        {
            if (pokemon.hasScale())
            {
                return pokemon.scale();
            }
            return std::nullopt;
        }

        int getRibbonIndex(const RibbonArguments &arguments, std::string_view spriteStem)
        {
            const Names::RibbonDefinition *ribbon = arguments.find(spriteStem);
            return ribbon != nullptr ? Names::getRibbonAffixIndex(*ribbon) : -1;
        }

        bool isMarkIndexBetween(const RibbonArguments &arguments, int markIndex, std::string_view firstStem, std::string_view lastStem)
        {
            return markIndex >= getRibbonIndex(arguments, firstStem) && markIndex <= getRibbonIndex(arguments, lastStem);
        }

        bool isMark(int markIndex, Encounters::RibbonIndex mark)
        {
            return markIndex == static_cast<int>(mark);
        }

        bool isMarkBetween(int markIndex, Encounters::RibbonIndex firstMark, Encounters::RibbonIndex lastMark)
        {
            return markIndex >= static_cast<int>(firstMark) && markIndex <= static_cast<int>(lastMark);
        }

        /// PKHeX RibbonIndex.GetWeather8.
        uint16_t getMarkWeather8(int markIndex)
        {
            using Encounters::RibbonIndex;
            switch (static_cast<RibbonIndex>(markIndex))
            {
            case RibbonIndex::MarkCloudy: return Encounters::WEATHER8_OVERCAST;
            case RibbonIndex::MarkRainy: return Encounters::WEATHER8_RAINING;
            case RibbonIndex::MarkStormy: return Encounters::WEATHER8_THUNDERSTORM;
            case RibbonIndex::MarkDry: return Encounters::WEATHER8_INTENSE_SUN;
            case RibbonIndex::MarkSnowy: return Encounters::WEATHER8_SNOWING;
            case RibbonIndex::MarkBlizzard: return Encounters::WEATHER8_SNOWSTORM;
            case RibbonIndex::MarkSandstorm: return Encounters::WEATHER8_SANDSTORM;
            case RibbonIndex::MarkMisty: return Encounters::WEATHER8_HEAVY_FOG;
            default: return Encounters::WEATHER8_NONE;
            }
        }

        /// PKHeX AreaWeather9Extensions.IsMarkCompatible.
        bool isWeatherMarkCompatible9(uint8_t weather, int markIndex)
        {
            using Encounters::RibbonIndex;
            switch (static_cast<RibbonIndex>(markIndex))
            {
            case RibbonIndex::MarkCloudy: return (weather & Encounters::WEATHER9_OVERCAST) != 0;
            case RibbonIndex::MarkRainy: return (weather & Encounters::WEATHER9_RAINING) != 0;
            case RibbonIndex::MarkStormy: return (weather & Encounters::WEATHER9_THUNDERSTORM) != 0;
            case RibbonIndex::MarkSnowy: return (weather & Encounters::WEATHER9_SNOWING) != 0;
            case RibbonIndex::MarkBlizzard: return (weather & Encounters::WEATHER9_SNOWSTORM) != 0;
            case RibbonIndex::MarkSandstorm: return (weather & Encounters::WEATHER9_SANDSTORM) != 0;
            case RibbonIndex::MarkMisty: return (weather & Encounters::WEATHER9_MIST) != 0;
            default: return false;
            }
        }

        /// PKHeX MarkRules.IsWeatherPermitted8 and IsSlotWeatherPermittedSWSH.
        bool isWeatherPermitted8(int markIndex, const Encounters::Encounterable &encounter)
        {
            const uint16_t permit = getMarkWeather8(markIndex);
            const auto hasFlag = [permit](uint16_t weather) { return (weather & permit) == permit; };
            if (const Encounters::EncounterSlot8 *slot = Encounters::encounterAs<Encounters::EncounterSlot8>(encounter))
            {
                const uint8_t location = slot->area().location();
                // A location outside the main table can have only Normal weather.
                if (hasFlag(Encounters::getAreaWeather8(location)))
                {
                    return true;
                }
                // Tree and fishing slots would have matched the area's own weather.
                if ((slot->weather() & (Encounters::WEATHER8_SHAKING_TREES | Encounters::WEATHER8_FISHING)) != 0)
                {
                    return false;
                }
                return Encounters::isWeatherBleedPossible8(slot->type(), permit, location);
            }
            if (const Encounters::EncounterStatic8 *fixed = Encounters::encounterAs<Encounters::EncounterStatic8>(encounter))
            {
                return hasFlag(fixed->weather());
            }
            return false;
        }

        /// PKHeX MarkRules.IsMarkAllowedSpecific8, against the engine's template.
        bool isMarkAllowedSpecific8Template(const RibbonArguments &arguments, int markIndex, const Encounters::Encounterable &encounter)
        {
            using Encounters::RibbonIndex;
            const Pokemon::Pokemon &pokemon = arguments.context.pokemon;
            const Encounters::EncounterSlot8 *slot = Encounters::encounterAs<Encounters::EncounterSlot8>(encounter);
            if (isMark(markIndex, RibbonIndex::MarkCurry))
            {
                // MarkRules.IsMarkAllowedCurry: a hidden-table slot, caught in a Poke, Great or Ultra Ball.
                return slot != nullptr && slot->canEncounterViaCurry() && static_cast<unsigned>(pokemon.ball() - 2) <= 2u;
            }
            if (isMark(markIndex, RibbonIndex::MarkFishing))
            {
                return slot != nullptr && slot->canEncounterViaFishing();
            }
            if (isMark(markIndex, RibbonIndex::MarkMisty) && encounter.generation() == 8 && pokemon.metLevel() < Encounters::BOOST_LEVEL8 && Encounters::isBoostedArea60Fog(pokemon.metLocation()))
            {
                return false;
            }
            if (isMark(markIndex, RibbonIndex::MarkDestiny))
            {
                return false; // only a Scarlet/Violet wild slot
            }
            if (isMarkBetween(markIndex, RibbonIndex::MarkCloudy, RibbonIndex::MarkMisty))
            {
                return isWeatherPermitted8(markIndex, encounter);
            }
            return true;
        }

        /// PKHeX MarkRules.IsMarkAllowedSpecific9 for an EncounterSlot9 (hasSpawnTimes) or an
        /// EncounterOutbreak9 (no time restrictions).
        bool isMarkAllowedSpecific9Template(int markIndex, uint8_t weather, const Encounters::EncounterSlot9 *slot)
        {
            using Encounters::RibbonIndex;
            if (isMark(markIndex, RibbonIndex::MarkCurry) || isMark(markIndex, RibbonIndex::MarkFishing))
            {
                return false;
            }
            if (isMark(markIndex, RibbonIndex::MarkDestiny))
            {
                return true; // caught on the trainer's birthday
            }
            if (isMarkBetween(markIndex, RibbonIndex::MarkLunchtime, RibbonIndex::MarkDawn))
            {
                return slot == nullptr || slot->canSpawnAtTime(markIndex - static_cast<int>(RibbonIndex::MarkLunchtime));
            }
            if (isMarkBetween(markIndex, RibbonIndex::MarkCloudy, RibbonIndex::MarkMisty))
            {
                return isWeatherMarkCompatible9(weather, markIndex);
            }
            return true;
        }

        /// PKHeX MarkRules.IsEncounterMarkValid, against the engine's template.
        bool isEncounterMarkValidTemplate(const RibbonArguments &arguments, int markIndex, const Encounters::Encounterable &encounter)
        {
            if (Encounters::encounterAs<Encounters::EncounterSlot8>(encounter))
            {
                return isMarkAllowedSpecific8Template(arguments, markIndex, encounter);
            }
            if (const Encounters::EncounterStatic8 *fixed = Encounters::encounterAs<Encounters::EncounterStatic8>(encounter))
            {
                if (!fixed->isGift() && !fixed->isScriptedNoMarks())
                {
                    return isMarkAllowedSpecific8Template(arguments, markIndex, encounter);
                }
                return false;
            }
            if (const Encounters::EncounterSlot9 *slot = Encounters::encounterAs<Encounters::EncounterSlot9>(encounter))
            {
                return isMarkAllowedSpecific9Template(markIndex, slot->weather(), slot);
            }
            if (const Encounters::EncounterStatic9 *fixed = Encounters::encounterAs<Encounters::EncounterStatic9>(encounter))
            {
                return isMark(markIndex, Encounters::RibbonIndex::MarkCrafty) && fixed->hasRequiredCraftyMark();
            }
            if (const Encounters::EncounterOutbreak9 *outbreak = Encounters::encounterAs<Encounters::EncounterOutbreak9>(encounter))
            {
                // The outbreak's ribbon is possible, not guaranteed.
                return outbreak->ribbon() == markIndex || isMarkAllowedSpecific9Template(markIndex, outbreak->weather(), nullptr);
            }
            if (const Encounters::MysteryGiftWC9 *card = Encounters::encounterAs<Encounters::MysteryGiftWC9>(encounter))
            {
                return card->card().ribbons.has(static_cast<Encounters::RibbonIndex>(markIndex));
            }
            return false;
        }

        /// PKHeX MarkRules.IsMarkAllowedSpecific8 for a Sword/Shield wild or static encounter.
        std::optional<bool> isMarkAllowedSpecific8(const RibbonArguments &arguments, int markIndex)
        {
            if (markIndex == getRibbonIndex(arguments, "ribbonmarkcurry"))
            {
                // Curry encounters are caught in a Poke, Great or Ultra Ball; which slots curry draws from is not in PKSE's rows.
                const uint8_t ballId = arguments.context.pokemon.ball();
                if (static_cast<unsigned>(ballId - 2) > 2u)
                {
                    return false;
                }
                return std::nullopt;
            }
            if (markIndex == getRibbonIndex(arguments, "ribbonmarkfishing"))
            {
                return std::nullopt;
            }
            if (markIndex == getRibbonIndex(arguments, "ribbonmarkdestiny"))
            {
                return false;
            }
            // Weather marks need each area's weather, which PKSE does not have.
            if (isMarkIndexBetween(arguments, markIndex, "ribbonmarkcloudy", "ribbonmarkmisty"))
            {
                return std::nullopt;
            }
            return true;
        }

        /// PKHeX MarkRules.IsMarkAllowedSpecific9 for a Scarlet/Violet wild encounter.
        std::optional<bool> isMarkAllowedSpecific9(const RibbonArguments &arguments, int markIndex)
        {
            if (markIndex == getRibbonIndex(arguments, "ribbonmarkcurry") || markIndex == getRibbonIndex(arguments, "ribbonmarkfishing"))
            {
                return false;
            }
            if (markIndex == getRibbonIndex(arguments, "ribbonmarkdestiny"))
            {
                return true;
            }
            // Time and weather marks need each slot's spawn times and weather, which PKSE does not have.
            if (isMarkIndexBetween(arguments, markIndex, "ribbonmarklunchtime", "ribbonmarkdawn") || isMarkIndexBetween(arguments, markIndex, "ribbonmarkcloudy", "ribbonmarkmisty"))
            {
                return std::nullopt;
            }
            return true;
        }
    }

    /// PKHeX RibbonVerifierMark9.Parse / ParseEgg.
    void parseRibbonSetMark9(const RibbonArguments &arguments, RibbonResultList &results, bool isEgg)
    {
        if (isEgg)
        {
            // Gourmand can be applied to an egg in the party; it just cannot be affixed.
            for (const std::string_view spriteStem : {"ribbonmarkalpha", "ribbonmarkitemfinder", "ribbonmarkjumbo", "ribbonmarkmightiest", "ribbonmarkmini", "ribbonmarkpartner", "ribbonmarktitan"})
            {
                flagRibbonIfSet(results, arguments, spriteStem);
            }
            return;
        }
        const Pokemon::Pokemon &pokemon = arguments.context.pokemon;
        const RibbonEncounter &encounter = arguments.encounter;
        const bool hasVisitedScarletViolet = arguments.history.hasVisited(Enums::GameVersion::SV);

        const std::optional<bool> wasAlpha = isAlphaEncounter(arguments);
        if (wasAlpha.has_value())
        {
            const bool hasAlphaMark = arguments.has("ribbonmarkalpha");
            const bool isAlphaValid = !*wasAlpha ? !hasAlphaMark : (!hasEnteredSetAlphaMark(arguments) || hasAlphaMark);
            if (!isAlphaValid)
            {
                addRibbonResult(results, arguments, "ribbonmarkalpha", !hasAlphaMark);
            }
        }
        // Eating food, finding items and the partner mark are all Scarlet/Violet's.
        if (arguments.has("ribbonmarkgourmand") && !hasVisitedScarletViolet)
        {
            addRibbonResult(results, arguments, "ribbonmarkgourmand");
        }
        if (arguments.has("ribbonmarkitemfinder") && !hasVisitedScarletViolet)
        {
            addRibbonResult(results, arguments, "ribbonmarkitemfinder");
        }
        const std::optional<uint8_t> sizeScalar = getSizeScalar(pokemon);
        if (arguments.has("ribbonmarkjumbo") && (!hasVisitedScarletViolet || (sizeScalar.has_value() && *sizeScalar != 255)))
        {
            addRibbonResult(results, arguments, "ribbonmarkjumbo");
        }

        // A capturable seven-star raid forces the Mightiest Mark; PKSE's raid rows do not say
        // which ones, and a Mystery Gift raid is not in them.
        const bool mayBeSevenStarRaid = (encounter.type == EncounterType::Raid && encounter.context == Enums::GameVersion::SV) || encounter.type == EncounterType::Event || encounter.type == EncounterType::Unchecked;
        if (encounter.encounterMatch != nullptr)
        {
            // PKHeX MarkRules.IsMarkValidMightiest: a capturable seven-star raid forces the mark.
            constexpr uint8_t MIGHTIEST_RAID_STARS = 7;
            const Encounters::EncounterMight9 *might = Encounters::encounterAs<Encounters::EncounterMight9>(*encounter.encounterMatch);
            const bool hasMightiestMark = arguments.has("ribbonmarkmightiest");
            bool isMightiestValid = !hasMightiestMark;
            if (might != nullptr && might->stars() == MIGHTIEST_RAID_STARS)
            {
                isMightiestValid = hasMightiestMark;
            }
            else if (encounter.encounterMatch->speciesId() == MEW_SPECIES && hasVisitedScarletViolet)
            {
                isMightiestValid = true; // Mew can earn it for battling Mewtwo
            }
            if (!isMightiestValid)
            {
                addRibbonResult(results, arguments, "ribbonmarkmightiest", !hasMightiestMark);
            }
        }
        else if (!mayBeSevenStarRaid)
        {
            const bool hasMightiestMark = arguments.has("ribbonmarkmightiest");
            const uint16_t encounterSpecies = encounter.speciesId != 0 ? encounter.speciesId : arguments.speciesId;
            // Mew can earn it for battling Mewtwo.
            const bool isMightiestValid = (encounterSpecies == MEW_SPECIES && hasVisitedScarletViolet) || !hasMightiestMark;
            if (!isMightiestValid)
            {
                addRibbonResult(results, arguments, "ribbonmarkmightiest");
            }
        }
        if (arguments.has("ribbonmarkmini") && (!hasVisitedScarletViolet || (sizeScalar.has_value() && *sizeScalar != 0)))
        {
            addRibbonResult(results, arguments, "ribbonmarkmini");
        }
        if (arguments.has("ribbonmarkpartner") && !hasVisitedScarletViolet)
        {
            addRibbonResult(results, arguments, "ribbonmarkpartner");
        }
        // Only Titans come with the Titan Mark; PKSE's static rows do not say which ones they are.
        const bool mayBeTitan = (encounter.type == EncounterType::Static && encounter.context == Enums::GameVersion::SV) || encounter.type == EncounterType::Unchecked;
        if (encounter.encounterMatch != nullptr)
        {
            // PKHeX MarkRules.IsMarkPresentTitan: exactly the Titans carry it.
            const Encounters::EncounterStatic9 *fixed = Encounters::encounterAs<Encounters::EncounterStatic9>(*encounter.encounterMatch);
            const bool isTitan = fixed != nullptr && fixed->isTitan();
            const bool hasTitanMark = arguments.has("ribbonmarktitan");
            if (hasTitanMark != isTitan)
            {
                addRibbonResult(results, arguments, "ribbonmarktitan", !hasTitanMark);
            }
        }
        else if (!mayBeTitan)
        {
            flagRibbonIfSet(results, arguments, "ribbonmarktitan");
        }
    }

    bool isEncounterMarkLost(const RibbonArguments &arguments)
    {
        return arguments.encounter.speciesId == NINCADA_SPECIES && arguments.context.pokemon.speciesID() == SHEDINJA_SPECIES;
    }

    bool isEncounterMarkAllowed(const RibbonArguments &arguments)
    {
        if (isEncounterMarkLost(arguments))
        {
            return false;
        }
        return arguments.encounter.context == Enums::GameVersion::SWSH || arguments.encounter.context == Enums::GameVersion::SV;
    }

    std::optional<bool> isEncounterMarkValid(const RibbonArguments &arguments, int markIndex)
    {
        const RibbonEncounter &encounter = arguments.encounter;
        if (encounter.encounterMatch != nullptr)
        {
            return isEncounterMarkValidTemplate(arguments, markIndex, *encounter.encounterMatch);
        }
        switch (encounter.type)
        {
        case EncounterType::Unchecked:
        case EncounterType::Unmatched:
            // PKSE's matcher misses encounters PKHeX finds (a Wild Area Pokemon boosted to level 60),
            // so its failure to match is not PKHeX's EncounterInvalid here.
            return std::nullopt;
        case EncounterType::Gift:
        case EncounterType::Trade:
        case EncounterType::Raid:
        case EncounterType::Egg:
            return false;
        default:
            break;
        }
        if (encounter.context == Enums::GameVersion::SWSH)
        {
            // Sword/Shield's Mystery Gifts never carry a mark.
            if (encounter.type == EncounterType::Event)
            {
                return false;
            }
            return isMarkAllowedSpecific8(arguments, markIndex);
        }
        if (encounter.context == Enums::GameVersion::SV)
        {
            if (encounter.type == EncounterType::Wild)
            {
                return isMarkAllowedSpecific9(arguments, markIndex);
            }
            // A Mystery Gift's marks, and which static encounters carry the Crafty Mark, are not in PKSE's rows.
            if (encounter.type == EncounterType::Event || markIndex == getRibbonIndex(arguments, "ribbonmarkcrafty"))
            {
                return std::nullopt;
            }
            return false;
        }
        return false;
    }

    int getMaxAffixValue(const RibbonArguments &arguments)
    {
        // PKHeX RibbonIndexExtensions.MAX_G9 and MAX_G8; Sword/Shield cannot affix Hisui or Twinkling Star.
        if (arguments.history.hasVisited(Enums::GameVersion::SV))
        {
            return getRibbonIndex(arguments, "ribbonpartner");
        }
        if (arguments.history.hasVisited(Enums::GameVersion::SWSH))
        {
            return getRibbonIndex(arguments, "ribbonmarkslump");
        }
        return -1;
    }
}
