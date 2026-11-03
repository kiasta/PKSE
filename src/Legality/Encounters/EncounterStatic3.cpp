#include "Legality/Encounters/EncounterStatic3.h"

#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterLocations.h"
#include "Legality/RNG/MethodFinder.h"
#include "Pokemon/Pokemon.h"
#include "Legality/Evolutions/EvolutionEntity.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            constexpr uint16_t SPECIES_TOGEPI = 175;
            /// PKHeX EggStateLegality.EggMetLevel34.
            constexpr uint8_t EGG_MET_LEVEL34 = 0;
            /// The level a Gen 3 egg hatches at.
            constexpr uint8_t EGG_LEVEL3 = 5;
        }

        // PKHeX EncounterStatic3.IsMatchExact and the IsMatchEggLocation / IsMatchLocation /
        // IsMatchLevel helpers.
        bool EncounterStatic3::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            if (pokemon.eggLocation() != 0)
            {
                return false;
            }
            const bool isFormat3 = getFormatGeneration(pokemon) == 3;
            if (isFormat3)
            {
                const uint16_t metLocation = pokemon.metLocation();
                bool isLocationMatch = false;
                if (properties.isEgg)
                {
                    isLocationMatch = !pokemon.isEgg() || metLocation == properties.location;
                }
                else if (!properties.isRoaming)
                {
                    isLocationMatch = properties.location == metLocation;
                }
                else if (static_cast<uint8_t>(properties.version) <= static_cast<uint8_t>(Enums::GameVersion::EM) || properties.version == Enums::GameVersion::RSE)
                {
                    // Route 101-138
                    isLocationMatch = metLocation >= 16 && metLocation <= 49;
                }
                else
                {
                    // Route 1-25, in grass or on water
                    isLocationMatch = metLocation >= 101 && metLocation <= 125;
                }
                if (!isLocationMatch)
                {
                    return false;
                }
            }
            bool isLevelMatch = false;
            if (!isFormat3)
            {
                isLevelMatch = evolution.levelMax >= properties.level;
            }
            else if (!properties.isEgg)
            {
                isLevelMatch = pokemon.metLevel() == properties.level;
            }
            else
            {
                isLevelMatch = pokemon.metLevel() == EGG_MET_LEVEL34 && Evolutions::getEntityCurrentLevel(pokemon) >= EGG_LEVEL3;
            }
            if (!isLevelMatch)
            {
                return false;
            }
            if (properties.formId != evolution.formId && !isFormChangeable(properties.speciesId, properties.formId, pokemon.form(), Enums::GameVersion::RSE, getEntityContextGroup(pokemon)))
            {
                return false;
            }
            return true;
        }

        // PKHeX EncounterStatic3.GetMatchRating / IsMatchPartial
        EncounterMatchRating EncounterStatic3::getMatchRating(const Pokemon::Pokemon &pokemon) const
        {
            const bool isSafariBall = pokemon.ball() == static_cast<uint8_t>(Enums::Ball::Safari);
            if (isSafariBall != isSafariZoneLocation3(properties.location))
            {
                return EncounterMatchRating::PartialMatch;
            }
            const bool isGift = properties.fixedBall == Enums::Ball::Poke;
            if (isGift && pokemon.ball() != static_cast<uint8_t>(properties.fixedBall))
            {
                return EncounterMatchRating::PartialMatch;
            }
            if (properties.isFatefulEncounter != pokemon.isFatefulEncounter())
            {
                return EncounterMatchRating::PartialMatch;
            }
            return EncounterMatchRating::Match;
        }

        // PKHeX EncounterStatic3.IsCompatible and IsRoamerPIDIV
        RandomCorrelationRating EncounterStatic3::isCompatible(RNG::PIDType type, const Pokemon::Pokemon &pokemon) const
        {
            using RNG::PIDType;
            const uint8_t version = getOriginVersion(pokemon);
            if (version == static_cast<uint8_t>(Enums::GameVersion::EM))
            {
                return type == PIDType::Method1 ? RandomCorrelationRating::Match : RandomCorrelationRating::Mismatch;
            }
            if (properties.isRoaming)
            {
                // A Method 1 Roamer spread is also a valid Method 1 spread, which MethodFinder reports first:
                // only 8 bits of the IVs are stored (5 HP, 3 Attack).
                bool isRoamerPIDIV = type == PIDType::Method1Roamer;
                if (!isRoamerPIDIV && type == PIDType::Method1)
                {
                    isRoamerPIDIV = RNG::MethodFinder::getPackedIVs(pokemon) <= 0xFF;
                }
                return isRoamerPIDIV ? RandomCorrelationRating::Match : RandomCorrelationRating::Mismatch;
            }
            if (type == PIDType::Method1)
            {
                return RandomCorrelationRating::Match;
            }
            // Ruby/Sapphire: RSBox software emulation can yield Method 4.
            if (version == static_cast<uint8_t>(Enums::GameVersion::RU) || version == static_cast<uint8_t>(Enums::GameVersion::SA))
            {
                return type == PIDType::Method4 ? RandomCorrelationRating::NotIdeal : RandomCorrelationRating::Mismatch;
            }
            // FireRed/LeafGreen: the Togepi gift can be Method 4 via PID modulo VBlank abuse.
            return type == PIDType::Method4 && properties.speciesId == SPECIES_TOGEPI ? RandomCorrelationRating::NotIdeal : RandomCorrelationRating::Mismatch;
        }
    }
}
