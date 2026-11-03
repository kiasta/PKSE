/**
 * Yields the Legends: Z-A templates that could have produced a Pokemon, in PKHeX's order, with the best
 * partial match last. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Generator/Search/EncounterEnumerator9a.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERENUMERATOR9A_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERENUMERATOR9A_H

#include "Legality/Encounters/EncounterSequence.h"
#include "Legality/Evolutions/EvolutionCriteria.h"

namespace Pokemon
{
    class Pokemon;
}

namespace Legality
{
    namespace Encounters
    {
        struct EncounterEnumerator9a
        {
            static MatchedEncounters enumerate(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain);
        };
    }
}

#endif
