/**
 * The Colosseum and XD encounter generator: yields EncounterEnumerator3GC's templates whose PID/IV
 * correlation fits, checks each shadow Pokemon's locked team, and offers the first misfit last.
 * Ported from PKHeX PKHeX.Core/Legality/Encounters/Generator/ByGeneration/EncounterGenerator3GC.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERGENERATOR3GC_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERGENERATOR3GC_H

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
        struct EncounterGenerator3GC
        {
            /// PKHeX GetEncounters(pk, info): the chain is the Gen 3 origin chain.
            static EncounterCandidates getEncounters(const Pokemon::Pokemon &pokemon, LegalityInfo &info);

            /// PKHeX GetEncounters(pk, chain, info).
            static EncounterCandidates getEncounters(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain, LegalityInfo &info);
        };
    }
}

#endif
