/**
 * The Game Boy encounter generator: runs the Gen 1 and Gen 2 generators together and picks between
 * them, since a Game Boy record rarely says which generation it came from. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Generator/ByGeneration/Lump/EncounterGenerator12.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERGENERATOR12_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERGENERATOR12_H

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
        struct EncounterGenerator12
        {
            /// PKHeX GetEncounters(pk, info): updates info.generation to each candidate's generation
            /// as it is yielded (PKHeX LegalInfo.StoreMetadata).
            static EncounterCandidates getEncounters(const Pokemon::Pokemon &pokemon, LegalityInfo &info);
        };
    }
}

#endif
