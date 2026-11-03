/**
 * Yields the Let's Go templates that could have produced a Pokemon, in PKHeX's order, with the best
 * partial match last. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Generator/Search/EncounterEnumerator7GG.cs; PKHeX's
 * EncounterGenerator7GG only forwards to it.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERENUMERATOR7GG_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERENUMERATOR7GG_H

#include "Enums/GameVersion.h"
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
        struct EncounterEnumerator7GG
        {
            /// `version` is the Pokemon's stored origin game.
            static MatchedEncounters enumerate(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain, Enums::GameVersion version);
        };
    }
}

#endif
