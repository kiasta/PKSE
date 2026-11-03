/**
 * Yields the Gen 4 (Diamond, Pearl, Platinum, HeartGold, SoulSilver) templates that could have
 * produced a Pokemon, in PKHeX's order, with the best partial match last. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Generator/Search/EncounterEnumerator4.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERENUMERATOR4_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERENUMERATOR4_H

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
        struct EncounterEnumerator4
        {
            /// `version` is the Pokemon's stored origin game.
            static MatchedEncounters enumerate(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain, Enums::GameVersion version);
        };
    }
}

#endif
