/**
 * The Gen 4 encounter generator: runs EncounterEnumerator4, checks each template's ground tile,
 * PID/IV correlation and ball, orders wild slots by the lead they need, and yields the best-ranked
 * deferral last. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Generator/ByGeneration/EncounterGenerator4.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERGENERATOR4_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERGENERATOR4_H

#include <span>

#include "Enums/GameVersion.h"
#include "Legality/Encounters/EncounterEgg4.h"
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
        struct EncounterGenerator4
        {
            /// PKHeX GetEncounters(pk, info): the chain is the Gen 4 origin chain.
            static EncounterCandidates getEncounters(const Pokemon::Pokemon &pokemon, LegalityInfo &info);

            /// PKHeX GetEncounters(pk, chain, info).
            static EncounterCandidates getEncounters(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain, LegalityInfo &info);

            /// PKHeX TryGetEgg: the egg the lowest stage of `chain` hatched from.
            static bool tryGetEgg(std::span<const Evolutions::EvolutionCriteria> chain, Enums::GameVersion version, EncounterEgg4 &result);

            /// PKHeX TryGetSplit: the incense-free split-breed alternative to `other`.
            static bool tryGetSplit(const EncounterEgg4 &other, std::span<const Evolutions::EvolutionCriteria> chain, EncounterEgg4 &result);
        };
    }
}

#endif
