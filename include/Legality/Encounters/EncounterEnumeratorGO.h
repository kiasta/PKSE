/**
 * Yields the Pokemon GO templates that could have produced a Pokemon, in PKHeX's order, with the best
 * partial match last. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Generator/Search/EncounterEnumerator7GO.cs and
 * EncounterEnumerator8GO.cs; PKHeX's EncounterGenerator7GO and EncounterGenerator8GO only forward
 * to them.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERENUMERATORGO_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERENUMERATORGO_H

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
        struct EncounterEnumeratorGO
        {
            /// PKHeX EncounterEnumerator7GO: GO Park transfers into Let's Go.
            static MatchedEncounters enumerateLetsGo(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain);
            /// PKHeX EncounterEnumerator8GO: GO transfers into HOME.
            static MatchedEncounters enumerateHome(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain);
        };
    }
}

#endif
