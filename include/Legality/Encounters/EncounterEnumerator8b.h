/**
 * Yields the Brilliant Diamond/Shining Pearl templates that could have produced a Pokemon, in PKHeX's
 * order, with the best partial match last. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Generator/Search/EncounterEnumerator8b.cs and
 * Search/Dirtied/EncounterEnumerator8bSWSH.cs (a BDSP Pokemon HOME moved to Sword/Shield).
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERENUMERATOR8B_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERENUMERATOR8B_H

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
        struct EncounterEnumerator8b
        {
            /// `version` is the record's own game, Brilliant Diamond or Shining Pearl.
            static MatchedEncounters enumerate(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain, Enums::GameVersion version);
            /// PKHeX EncounterEnumerator8bSWSH: a Sword/Shield record HOME remapped from `version`
            /// (Brilliant Diamond or Shining Pearl).
            static MatchedEncounters enumerateSwordShield(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain, Enums::GameVersion version);
        };
    }
}

#endif
