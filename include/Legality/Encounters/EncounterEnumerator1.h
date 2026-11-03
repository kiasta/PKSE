/**
 * Yields the Gen 1 (Red, Green, Blue, Yellow) templates that could have produced a Pokemon, in
 * PKHeX's order, with the best partial match last. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Generator/Search/EncounterEnumerator1.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERENUMERATOR1_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERENUMERATOR1_H

#include "Legality/Encounters/EncounterSequence.h"
#include "Legality/Encounters/LegalityInfo.h"
#include "Legality/Evolutions/EvolutionCriteria.h"

namespace Pokemon
{
    class Pokemon;
}

namespace Legality
{
    namespace Encounters
    {
        struct EncounterEnumerator1
        {
            /// `info` answers PKHeX's ParseSettings.AllowGBVirtualConsole3DS.
            static MatchedEncounters enumerate(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain, const LegalityInfo &info);
        };
    }
}

#endif
