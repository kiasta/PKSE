#include "Legality/Encounters/EncounterStatic3XD.h"

#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            constexpr uint16_t SPECIES_UMBREON = 197;
        }

        // PKHeX EncounterStatic3XD.IsMatchExact
        bool EncounterStatic3XD::isMatchExact(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionCriteria &evolution) const
        {
            return isMatchExactGameCube(pokemon, evolution, properties.speciesId, properties.level, properties.location);
        }

        // PKHeX EncounterStatic3XD.GetMatchRating
        EncounterMatchRating EncounterStatic3XD::getMatchRating(const Pokemon::Pokemon &pokemon) const
        {
            return pokemon.ball() != static_cast<uint8_t>(Enums::Ball::Poke) ? EncounterMatchRating::PartialMatch : EncounterMatchRating::Match;
        }

        // PKHeX EncounterStatic3XD.IsCompatible
        RandomCorrelationRating EncounterStatic3XD::isCompatible(RNG::PIDType type, const Pokemon::Pokemon &pokemon) const
        {
            if (type == RNG::PIDType::ColosseumXD || type == RNG::PIDType::ColosseumXDAntishiny)
            {
                return RandomCorrelationRating::Match;
            }
            // An Umbreon can satisfy Colosseum's starter correlation too; the fateful flag rules it out later.
            if (type == RNG::PIDType::ColosseumStarter && pokemon.speciesID() == SPECIES_UMBREON)
            {
                return RandomCorrelationRating::Match;
            }
            return RandomCorrelationRating::Mismatch;
        }
    }
}
