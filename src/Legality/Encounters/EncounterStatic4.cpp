#include "Legality/Encounters/EncounterStatic4.h"

#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterLocations4.h"
#include "Legality/RNG/CuteCharm4.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            constexpr uint16_t SPECIES_PICHU = 172;

            // PKHeX EncounterStatic4: each roamer's routes as a bitmask counted from the region's
            // first route location (Sinnoh, Johto, Kanto), on grass and on water.
            constexpr int FIRST_ROUTE_SINNOH = 16;
            constexpr uint64_t PERMIT_GRASS_SINNOH = 0x2'8033FFFF;
            constexpr uint64_t PERMIT_WATER_SINNOH = 0x2'803E3B9E;
            constexpr int FIRST_ROUTE_JOHTO = 177;
            constexpr uint64_t PERMIT_GRASS_JOHTO = 0x0003E7FF;
            constexpr uint64_t PERMIT_WATER_JOHTO = 0x0001E06E;
            constexpr int FIRST_ROUTE_KANTO = 149;
            constexpr uint64_t PERMIT_GRASS_KANTO = 0x0AB3FFFF;
            constexpr uint64_t PERMIT_WATER_KANTO = 0x0ABC1B28;

            bool isMatchLocationGrass(uint16_t location, uint16_t metLocation) noexcept
            {
                switch (location)
                {
                case FIRST_ROUTE_SINNOH:
                    return EncounterStatic4::isMatchRoamerLocation(PERMIT_GRASS_SINNOH, metLocation, FIRST_ROUTE_SINNOH);
                case FIRST_ROUTE_JOHTO:
                    return EncounterStatic4::isMatchRoamerLocation(PERMIT_GRASS_JOHTO, metLocation, FIRST_ROUTE_JOHTO);
                case FIRST_ROUTE_KANTO:
                    return EncounterStatic4::isMatchRoamerLocation(PERMIT_GRASS_KANTO, metLocation, FIRST_ROUTE_KANTO);
                default:
                    return false;
                }
            }

            bool isMatchLocationWater(uint16_t location, uint16_t metLocation) noexcept
            {
                switch (location)
                {
                case FIRST_ROUTE_SINNOH:
                    return EncounterStatic4::isMatchRoamerLocation(PERMIT_WATER_SINNOH, metLocation, FIRST_ROUTE_SINNOH);
                case FIRST_ROUTE_JOHTO:
                    return EncounterStatic4::isMatchRoamerLocation(PERMIT_WATER_JOHTO, metLocation, FIRST_ROUTE_JOHTO);
                case FIRST_ROUTE_KANTO:
                    return EncounterStatic4::isMatchRoamerLocation(PERMIT_WATER_KANTO, metLocation, FIRST_ROUTE_KANTO);
                default:
                    return false;
                }
            }
        }

        bool EncounterStatic4::isMatchRoamerLocation(uint64_t permit, uint16_t location, int first) noexcept
        {
            const int bitIndex = location - first;
            if (bitIndex < 0 || bitIndex >= 64)
            {
                return false;
            }
            return (permit & (1ull << bitIndex)) != 0;
        }

        // PKHeX EncounterStatic4.IsMatchExact
        bool EncounterStatic4::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            if (!isMatchEggLocationInternal(pokemon))
            {
                return false;
            }
            if (!isMatchLocation(pokemon))
            {
                return false;
            }
            if (!isMatchLevel(pokemon, evolution))
            {
                return false;
            }
            if (properties.gender != GENDER_RANDOM && pokemon.gender() != properties.gender)
            {
                return false;
            }
            if (properties.formId != evolution.formId && !isFormChangeable(properties.speciesId, properties.formId, pokemon.formID(), Enums::GameVersion::DP, getEntityContextGroup(pokemon)))
            {
                return false;
            }
            return true;
        }

        bool EncounterStatic4::isMatchLocation(const Pokemon::Pokemon &pokemon) const
        {
            // The met location is lost on transfer.
            if (getFormatGeneration(pokemon) != 4)
            {
                return true;
            }
            const uint16_t metLocation = pokemon.metLocation();
            if (isEgg())
            {
                return !pokemon.isEgg() || metLocation == properties.location || metLocation == LOCATION_LINK_TRADE4;
            }
            if (!properties.isRoaming)
            {
                return metLocation == properties.location;
            }
            switch (pokemon.groundTile())
            {
            case GROUND_TILE_TYPE_GRASS:
                return isMatchLocationGrass(properties.location, metLocation);
            case GROUND_TILE_TYPE_WATER:
                return isMatchLocationWater(properties.location, metLocation);
            default:
                return false;
            }
        }

        bool EncounterStatic4::isMatchEggLocationInternal(const Pokemon::Pokemon &pokemon) const
        {
            const uint16_t eggLocation = pokemon.eggLocation();
            if (!isEgg())
            {
                return eggLocation == properties.eggLocation;
            }
            // Moving to Gen 5 clears a Platinum/HeartGold/SoulSilver egg location to Faraway Place.
            if (getFormatGeneration(pokemon) != 4)
            {
                if (eggLocation == LOCATION_LINK_TRADE4)
                {
                    return true;
                }
                const uint16_t expected = isPtHGSSLocationEgg(properties.eggLocation) ? LOCATION_FARAWAY4 : properties.eggLocation;
                return eggLocation == expected;
            }
            if (!pokemon.isEgg())
            {
                return eggLocation == properties.eggLocation || eggLocation == LOCATION_LINK_TRADE4;
            }
            return eggLocation == properties.eggLocation;
        }

        bool EncounterStatic4::isMatchLevel(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            // The met level is lost on transfer to Gen 5.
            if (getFormatGeneration(pokemon) != 4)
            {
                return properties.level <= evolution.levelMax;
            }
            return pokemon.metLevel() == (isEgg() ? 0 : properties.level);
        }

        // PKHeX EncounterStatic4.GetMatchRating
        EncounterMatchRating EncounterStatic4::getMatchRating(const Pokemon::Pokemon &pokemon) const
        {
            if (isGift() && pokemon.ball() != static_cast<uint8_t>(properties.fixedBall))
            {
                return EncounterMatchRating::PartialMatch;
            }
            return EncounterMatchRating::Match;
        }

        // PKHeX EncounterStatic4.IsCompatible
        RandomCorrelationRating EncounterStatic4::isCompatible(RNG::PIDType type, const Pokemon::Pokemon &pokemon) const
        {
            if (properties.speciesId == SPECIES_PICHU)
            {
                return type == RNG::PIDType::Pokewalker ? RandomCorrelationRating::Match : RandomCorrelationRating::Mismatch;
            }
            if (properties.shiny == RNG::Shiny::Always)
            {
                return type == RNG::PIDType::ChainShiny ? RandomCorrelationRating::Match : RandomCorrelationRating::Mismatch;
            }
            if (type == RNG::PIDType::Method1)
            {
                return RandomCorrelationRating::Match;
            }
            if (type == RNG::PIDType::CuteCharm)
            {
                return RNG::CuteCharm4::isValid(properties.speciesId, pokemon) ? RandomCorrelationRating::Match : RandomCorrelationRating::Mismatch;
            }
            return RandomCorrelationRating::Mismatch;
        }

        RNG::PIDType EncounterStatic4::getSuggestedCorrelation() const noexcept
        {
            if (properties.speciesId == SPECIES_PICHU)
            {
                return RNG::PIDType::Pokewalker;
            }
            if (properties.shiny == RNG::Shiny::Always)
            {
                return RNG::PIDType::ChainShiny;
            }
            return RNG::PIDType::Method1;
        }
    }
}
