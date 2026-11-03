/**
 * The Gen 1 encounter generator: yields EncounterEnumerator1's templates. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Generator/ByGeneration/EncounterGenerator1.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERGENERATOR1_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERGENERATOR1_H

#include "Legality/Encounters/EncounterSequence.h"
#include "Legality/Encounters/LegalityInfo.h"

namespace Pokemon
{
    class Pokemon;
}

namespace Legality
{
    namespace Encounters
    {
        struct EncounterGenerator1
        {
            /// PKHeX GetEncounters(pk): the chain is the Gen 1 origin chain.
            static EncounterCandidates getEncounters(const Pokemon::Pokemon &pokemon, const LegalityInfo &info);
        };
    }
}

#endif
