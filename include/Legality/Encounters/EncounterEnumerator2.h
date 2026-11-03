/**
 * Yields the Gen 2 (Gold, Silver, Crystal) templates that could have produced a Pokemon, in PKHeX's
 * order, with the best partial match last. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Generator/Search/EncounterEnumerator2.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERENUMERATOR2_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERENUMERATOR2_H

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
        struct EncounterEnumerator2
        {
            /// `info` answers PKHeX's ParseSettings.AllowEraCartGB.
            static MatchedEncounters enumerate(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain, const LegalityInfo &info);
        };
    }
}

#endif
