#include "Legality/Encounters/EncounterStatic8b.h"

#include <algorithm>
#include <array>

#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterRules8.h"
#include "Legality/Encounters/EncounterRules8b.h"
#include "Legality/Encounters/EncounterRules9.h"
#include "Legality/RNG/Roaming8bRNG.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            // PKHeX EncounterStatic8b.RoamingLocations: where a roamer can be met (mvpoke in the game's data).
            constexpr std::array<uint16_t, 29> ROAMING_LOCATIONS8B = {197, 201, 354, 355, 356, 357, 358, 359, 361, 362, 364, 365, 367, 373, 375, 377, 378, 379, 383, 385, 392, 394, 395, 397, 400, 403, 404, 407, 485};

            bool isRoamingLocation(uint16_t location) noexcept
            {
                return std::find(ROAMING_LOCATIONS8B.begin(), ROAMING_LOCATIONS8B.end(), location) != ROAMING_LOCATIONS8B.end();
            }
        }

        StaticCorrelation8bRequirement EncounterStatic8b::getRequirement(const Pokemon::Pokemon &pokemon) const noexcept
        {
            (void)pokemon;
            return properties.isRoaming ? StaticCorrelation8bRequirement::MustHave : StaticCorrelation8bRequirement::MustNotHave;
        }

        bool EncounterStatic8b::isStaticCorrelationCorrect(const Pokemon::Pokemon &pokemon) const noexcept
        {
            const RNG::Shiny shiny = properties.shiny == RNG::Shiny::Random ? RNG::Shiny::FixedValue : properties.shiny;
            return RNG::Roaming8bRNG::validateRoamingEncounter(pokemon, shiny, properties.flawlessIVCount);
        }

        // PKHeX EncounterStatic8b.IsMatchLocationExact
        bool EncounterStatic8b::isMatchLocationExact(const Pokemon::Pokemon &pokemon) const noexcept
        {
            const uint16_t metLocation = pokemon.metLocation();
            if (isEgg())
            {
                return !pokemon.isEgg() || metLocation == properties.location || metLocation == LOCATION_LINK_TRADE6_NPC;
            }
            if (properties.isRoaming)
            {
                return isRoamingLocation(metLocation);
            }
            return metLocation == properties.location;
        }

        // PKHeX EncounterStatic8b.IsMatchEggLocationExact
        bool EncounterStatic8b::isMatchEggLocationExact(const Pokemon::Pokemon &pokemon) const noexcept
        {
            const uint16_t eggLocation = pokemon.eggLocation();
            if (!isEgg())
            {
                return eggLocation == properties.eggLocation;
            }
            if (!pokemon.isEgg())
            {
                return eggLocation == properties.eggLocation || eggLocation == LOCATION_LINK_TRADE6_NPC;
            }
            // An unhatched gift egg still holds its own egg location and no met location (or the link trade one).
            if (eggLocation != properties.eggLocation)
            {
                return false;
            }
            const uint16_t metLocation = pokemon.metLocation();
            return metLocation == LOCATION_DEFAULT8B_NONE || metLocation == LOCATION_LINK_TRADE6_NPC;
        }

        // PKHeX EncounterStatic8b.IsMatchEggLocation and IsMatchEggLocationRemapped
        bool EncounterStatic8b::isMatchEggLocation(const Pokemon::Pokemon &pokemon) const
        {
            const LocationRemapState metState = getRemapState(Enums::GameVersion::BDSP, getEntityContextGroup(pokemon));
            if (metState == LocationRemapState::Original)
            {
                return isMatchEggLocationExact(pokemon);
            }
            bool isRemappedMatch = pokemon.eggLocation() == 0;
            if (isEgg())
            {
                isRemappedMatch = isLocationSwordShieldEgg(getOriginVersion(pokemon), pokemon.metLocation(), pokemon.eggLocation(), properties.eggLocation);
            }
            if (metState == LocationRemapState::Remapped)
            {
                return isRemappedMatch;
            }
            return isMatchEggLocationExact(pokemon) || isRemappedMatch;
        }

        // PKHeX EncounterStatic8b.IsMatchExact
        bool EncounterStatic8b::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            if (pokemon.metLevel() != properties.level)
            {
                return false;
            }
            if (!isMatchLocation8b(pokemon, isMatchLocationExact(pokemon), properties.location))
            {
                return false;
            }
            if (evolution.formId != 0 && !isFormChangeable(properties.speciesId, 0, pokemon.form(), Enums::GameVersion::BDSP, getEntityContextGroup(pokemon)))
            {
                return false;
            }
            if (properties.flawlessIVCount != 0 && getFlawlessIVCount(pokemon) < properties.flawlessIVCount)
            {
                return false;
            }
            return isMatchEggLocation(pokemon);
        }
    }
}
