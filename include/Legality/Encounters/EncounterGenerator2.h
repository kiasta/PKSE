/**
 * The Gen 2 encounter generator: yields EncounterEnumerator2's templates and builds the bred egg
 * templates. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Generator/ByGeneration/EncounterGenerator2.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERGENERATOR2_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERGENERATOR2_H

#include <span>

#include "Enums/GameVersion.h"
#include "Legality/Encounters/EncounterEgg2.h"
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
        struct EncounterGenerator2
        {
            /// PKHeX GetEncounters(pk): the chain is the Gen 2 origin chain.
            static EncounterCandidates getEncounters(const Pokemon::Pokemon &pokemon, const LegalityInfo &info);

            /// PKHeX TryGetEgg: the egg the lowest stage of `chain` hatched from.
            static bool tryGetEgg(std::span<const Evolutions::EvolutionCriteria> chain, Enums::GameVersion version, EncounterEgg2 &result);

            /// PKHeX TryGetEggCrystal: the same egg hatched in Crystal, which stores met data. The met
            /// data cannot tell the games apart, so both are offered.
            static bool tryGetEggCrystal(const Pokemon::Pokemon &pokemon, const EncounterEgg2 &egg, EncounterEgg2 &crystal);
        };
    }
}

#endif
