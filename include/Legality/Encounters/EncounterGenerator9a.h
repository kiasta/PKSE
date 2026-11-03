/**
 * The Legends: Z-A encounter generator. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Generator/ByGeneration/EncounterGenerator9a.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERGENERATOR9A_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERGENERATOR9A_H

#include "Legality/Encounters/EncounterSequence.h"
#include "Legality/Encounters/LegalityInfo.h"
#include "Legality/Evolutions/EvolutionCriteria.h"

namespace Legality
{
    namespace Encounters
    {
        struct EncounterGenerator9a
        {
            /// PKHeX EncounterGenerator9a.GetEncounters(pk, chain, info).
            static EncounterCandidates getEncounters(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain, LegalityInfo &info);
        };
    }
}

#endif
