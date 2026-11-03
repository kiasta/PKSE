/**
 * Yields the Scarlet/Violet templates that could have produced a Pokemon, in PKHeX's order, with the
 * best partial match last. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Generator/Search/EncounterEnumerator9.cs and
 * Search/Dirtied/EncounterEnumerator9SWSH.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERENUMERATOR9_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERENUMERATOR9_H

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
        struct EncounterEnumerator9
        {
            /// `version` is the Pokemon's stored origin game (0 for an unhatched egg).
            static MatchedEncounters enumerate(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain, Enums::GameVersion version);

            /// PKHeX EncounterEnumerator9SWSH: a Scarlet/Violet origin stored as a Sword/Shield record,
            /// which kept no met location to filter on. `version` is SL or VL.
            static MatchedEncounters enumerateSWSH(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain, Enums::GameVersion version);
        };
    }
}

#endif
