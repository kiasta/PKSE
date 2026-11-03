/**
 * The Legends: Arceus encounter generator: hands a Legends: Arceus-origin record to the enumerator, in
 * its own format or in a later one after HOME. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Generator/ByGeneration/EncounterGenerator8a.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERGENERATOR8A_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERGENERATOR8A_H

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
        struct EncounterGenerator8a
        {
            /// PKHeX EncounterGenerator8a.GetEncounters(pk, chain, info). Legends: Arceus has no eggs.
            static EncounterCandidates getEncounters(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain, LegalityInfo &info);
        };
    }
}

#endif
