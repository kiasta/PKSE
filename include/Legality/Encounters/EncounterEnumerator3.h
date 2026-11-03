/**
 * Yields the Gen 3 (Ruby, Sapphire, Emerald, FireRed, LeafGreen) templates that could have produced a
 * Pokemon, in PKHeX's order, with the best partial match last. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Generator/Search/EncounterEnumerator3.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERENUMERATOR3_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERENUMERATOR3_H

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
        struct EncounterEnumerator3
        {
            /// `version` is the game to iterate (EncounterGenerator3's GetGameToIterate result).
            static MatchedEncounters enumerate(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain, Enums::GameVersion version);
        };
    }
}

#endif
