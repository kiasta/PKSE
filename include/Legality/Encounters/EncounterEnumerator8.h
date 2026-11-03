/**
 * Yields the Sword/Shield templates that could have produced a Pokemon, in PKHeX's order, with the
 * best partial match last. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Generator/Search/EncounterEnumerator8.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERENUMERATOR8_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERENUMERATOR8_H

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
        struct EncounterEnumerator8
        {
            /// `version` is the record's own game, Sword or Shield.
            static MatchedEncounters enumerate(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain, Enums::GameVersion version);
        };
    }
}

#endif
