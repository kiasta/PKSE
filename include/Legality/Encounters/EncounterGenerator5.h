/**
 * The Gen 5 encounter generator: yields EncounterEnumerator5's templates and builds the bred egg
 * templates. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Generator/ByGeneration/EncounterGenerator5.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERGENERATOR5_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERGENERATOR5_H

#include <span>

#include "Enums/GameVersion.h"
#include "Legality/Encounters/EncounterEgg5.h"
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
        struct EncounterGenerator5
        {
            /// PKHeX GetEncounters(pk, info): the chain is the Gen 5 origin chain.
            static EncounterCandidates getEncounters(const Pokemon::Pokemon &pokemon, LegalityInfo &info);

            /// PKHeX GetEncounters(pk, chain, info).
            static EncounterCandidates getEncounters(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain, LegalityInfo &info);

            /// PKHeX TryGetEgg: the egg the lowest stage of `chain` hatched from.
            static bool tryGetEgg(std::span<const Evolutions::EvolutionCriteria> chain, Enums::GameVersion version, EncounterEgg5 &result);

            /// PKHeX TryGetSplit: the incense-free split-breed alternative to `other`.
            static bool tryGetSplit(const EncounterEgg5 &other, std::span<const Evolutions::EvolutionCriteria> chain, EncounterEgg5 &result);
        };
    }
}

#endif
