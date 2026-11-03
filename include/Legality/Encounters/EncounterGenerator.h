/**
 * Picks the generator for a Pokemon's origin and yields its candidate encounters in PKHeX's order.
 * Ported from PKHeX PKHeX.Core/Legality/Encounters/Generator/EncounterGenerator.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERGENERATOR_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERGENERATOR_H

#include <cstdint>

#include "Enums/GameVersion.h"
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
        struct EncounterGenerator
        {
            /// PKHeX EncounterGenerator.GetEncounters, dispatched on info.generation.
            static EncounterCandidates getEncounters(const Pokemon::Pokemon &pokemon, LegalityInfo &info);

        };
    }
}

#endif
