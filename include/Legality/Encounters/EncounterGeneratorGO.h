/**
 * The Pokemon GO encounter generators. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Generator/ByGeneration/EncounterGenerator7GO.cs,
 * EncounterGenerator8GO.cs and Lump/EncounterGeneratorGO.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERGENERATORGO_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERGENERATORGO_H

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
        struct EncounterGeneratorGO
        {
            /// PKHeX EncounterGenerator7GO.GetEncounters: GO Park.
            static EncounterCandidates getEncountersLetsGo(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain);
            /// PKHeX EncounterGenerator8GO.GetEncounters: GO to HOME.
            static EncounterCandidates getEncountersHome(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain);
            /// PKHeX EncounterGeneratorGO.GetEncounters: picks the route by met location.
            static EncounterCandidates getEncounters(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain);
        };
    }
}

#endif
