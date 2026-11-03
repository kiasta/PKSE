#include "Legality/Encounters/EncounterTera9.h"

#include <string>

#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterRules8.h"
#include "Legality/RNG/Encounter9RNG.h"
#include "Legality/RNG/Tera9RNG.h"
#include "Pokemon/PersonalRecord.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            /// The star glyph PKHeX's raid names end with, in UTF-8.
            constexpr const char *STAR_GLYPH = "\xE2\x98\x85";

            // PKHeX EncounterTera9.GetRateTotalBaseSL / BaseVL / KitakamiSL / KitakamiVL / Blueberry
            int16_t getRateTotalBase(int stars, bool isScarlet) noexcept
            {
                switch (stars)
                {
                case 1:
                    return 5800;
                case 2:
                    return 5300;
                case 3:
                    return 7400;
                case 4:
                    // Scarlet has one more encounter at four stars.
                    return isScarlet ? 8800 : 8700;
                case 5:
                    return 9100;
                case 6:
                    return 6500;
                default:
                    return 0;
                }
            }

            int16_t getRateTotalKitakami(int stars, bool isScarlet) noexcept
            {
                switch (stars)
                {
                case 1:
                case 2:
                    return 1500;
                case 3:
                    return 2500;
                case 4:
                    return 2100;
                case 5:
                    return 2250;
                case 6:
                    return isScarlet ? 2475 : 2574;
                default:
                    return 0;
                }
            }

            int16_t getRateTotalBlueberry(int stars) noexcept
            {
                switch (stars)
                {
                case 1:
                case 2:
                    return 1100;
                case 3:
                    return 2000;
                case 4:
                    return 1900;
                case 5:
                    return 2100;
                case 6:
                    return 2600;
                default:
                    return 0;
                }
            }

            int16_t getRateTotal(int stars, TeraRaidMapParent map, bool isScarlet) noexcept
            {
                switch (map)
                {
                case TeraRaidMapParent::Paldea:
                    return getRateTotalBase(stars, isScarlet);
                case TeraRaidMapParent::Kitakami:
                    return getRateTotalKitakami(stars, isScarlet);
                case TeraRaidMapParent::Blueberry:
                    return getRateTotalBlueberry(stars);
                default:
                    return 0;
                }
            }
        }

        bool isRaidSeedMismatch9(const Pokemon::Pokemon &pokemon, uint32_t seed, RNG::GemType teraType, uint16_t speciesId, uint8_t formId)
        {
            return pokemon.hasTeraType() && !RNG::Tera9RNG::isMatchTeraType(seed, teraType, speciesId, formId, pokemon.teraTypeOriginal());
        }

        int16_t EncounterTera9::getRateTotalScarlet(int stars, TeraRaidMapParent map) noexcept
        {
            return getRateTotal(stars, map, true);
        }

        int16_t EncounterTera9::getRateTotalViolet(int stars, TeraRaidMapParent map) noexcept
        {
            return getRateTotal(stars, map, false);
        }

        std::string EncounterTera9::name() const
        {
            const std::string indexText = properties.raidIndex == 0 ? std::string("Base") : std::to_string(properties.raidIndex);
            return "Tera Raid Encounter [" + indexText + "] " + std::to_string(properties.stars) + STAR_GLYPH;
        }

        bool EncounterTera9::canBeEncountered(uint32_t seed) const noexcept
        {
            const uint16_t totalScarlet = static_cast<uint16_t>(getRateTotalScarlet(properties.stars, properties.map));
            const uint16_t totalViolet = static_cast<uint16_t>(getRateTotalViolet(properties.stars, properties.map));
            return RNG::Tera9RNG::isMatchStarChoice(seed, properties.stars, properties.randomRate, properties.randomRateMinimumScarlet, properties.randomRateMinimumViolet, totalScarlet, totalViolet);
        }

        // PKHeX EncounterTera9.IsMatchExact
        bool EncounterTera9::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            if (!isLevelWithinRange(pokemon.metLevel()))
            {
                return false;
            }
            if (properties.gender != GENDER_RANDOM && pokemon.gender() != properties.gender)
            {
                return false;
            }
            if (!isMatchEggLocation(pokemon, 0))
            {
                return false;
            }
            if (!isMatchLocation9(pokemon, pokemon.metLocation() == LOCATION_TERA_CAVERN9, LOCATION_TERA_CAVERN9, Enums::GameVersion::SV))
            {
                return false;
            }
            if (properties.formId != evolution.formId && !isRandomUnspecificForm() && !isFormChangeable(properties.speciesId, properties.formId, pokemon.form(), Enums::GameVersion::SV, getEntityContextGroup(pokemon)))
            {
                return false;
            }
            return true;
        }

        // PKHeX EncounterTera9.GetMatchRating and IsMatchDeferred
        EncounterMatchRating EncounterTera9::getMatchRating(const Pokemon::Pokemon &pokemon) const
        {
            if (isMatchPartial(pokemon))
            {
                return EncounterMatchRating::PartialMatch;
            }
            if (properties.shiny != RNG::Shiny::Random && !isShinyValid(properties.shiny, pokemon))
            {
                return EncounterMatchRating::DeferredErrors;
            }
            return getAbilityDeferral9(pokemon, properties.ability, properties.speciesId, properties.formId, Enums::GameVersion::SV);
        }

        // PKHeX EncounterTera9.IsMatchPartial
        bool EncounterTera9::isMatchPartial(const Pokemon::Pokemon &pokemon) const
        {
            if (isShinyStateMismatch(properties.shiny, pokemon))
            {
                return true;
            }
            const uint32_t seed = RNG::Tera9RNG::getOriginalSeed(pokemon);
            if (isRaidSeedMismatch9(pokemon, seed, properties.teraType, properties.speciesId, properties.formId))
            {
                return true;
            }
            if (!canBeEncountered(seed))
            {
                return true;
            }
            RNG::GenerateParam9 parameters;
            parameters.species = properties.speciesId;
            parameters.genderRatio = Pokemon::getPersonalRecord(Enums::GameVersion::SV, properties.speciesId, properties.formId).genderRatio;
            parameters.flawlessIVs = properties.flawlessIVCount;
            parameters.rollCount = 1;
            parameters.ability = properties.ability;
            parameters.shiny = properties.shiny;
            if (!RNG::Encounter9RNG::isMatch(pokemon, parameters, seed))
            {
                return true;
            }
            return isPartialMatchHiddenAbility(pokemon, properties.speciesId);
        }
    }
}
