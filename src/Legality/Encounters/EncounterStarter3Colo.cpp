#include "Legality/Encounters/EncounterStarter3Colo.h"

#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        // PKHeX EncounterStarter3Colo.IsMatchExact
        bool EncounterStarter3Colo::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            // PKHeX checks the gender between the level and the form; neither of those reads it.
            if (pokemon.gender() != GENDER)
            {
                return false;
            }
            return isMatchExactGameCube(pokemon, evolution, starterSpeciesId, starterLevel, LOCATION);
        }

        // PKHeX EncounterStarter3Colo.GetMatchRating
        EncounterMatchRating EncounterStarter3Colo::getMatchRating(const Pokemon::Pokemon &pokemon) const
        {
            if (pokemon.ball() != static_cast<uint8_t>(Enums::Ball::Poke))
            {
                return EncounterMatchRating::PartialMatch;
            }
            // XD's starter Eevee is the fateful one.
            if (pokemon.isFatefulEncounter())
            {
                return EncounterMatchRating::DeferredErrors;
            }
            return EncounterMatchRating::Match;
        }
    }
}
