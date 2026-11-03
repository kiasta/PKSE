/**
 * The Sword/Shield encounter generator, and the Gen 8 dispatcher (PKHeX EncounterGenerator8X) that
 * routes a Gen 8 origin to Sword/Shield, Brilliant Diamond/Shining Pearl or Legends: Arceus. Ported
 * from PKHeX PKHeX.Core/Legality/Encounters/Generator/ByGeneration/EncounterGenerator8.cs and
 * Lump/EncounterGenerator8X.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERGENERATOR8_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERGENERATOR8_H

#include <span>

#include "Enums/GameVersion.h"
#include "Legality/Encounters/EncounterEgg8.h"
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
        struct EncounterGenerator8
        {
            /// PKHeX EncounterGenerator8.GetEncounters(pk, chain, info).
            static EncounterCandidates getEncounters(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain, LegalityInfo &info);

            /// PKHeX TryGetEgg: the egg the lowest stage of `chain` hatched from.
            static bool tryGetEgg(std::span<const Evolutions::EvolutionCriteria> chain, Enums::GameVersion version, EncounterEgg8 &result);

            /// PKHeX TryGetSplit: the split-breed alternative to `other`.
            static bool tryGetSplit(const EncounterEgg8 &other, std::span<const Evolutions::EvolutionCriteria> chain, EncounterEgg8 &result);
        };

        struct EncounterGenerator8X
        {
            /// PKHeX EncounterGenerator8X.GetEncounters(pk, info): picks the origin context, builds the
            /// chain and hands the record to its game's generator.
            static EncounterCandidates getEncounters(const Pokemon::Pokemon &pokemon, LegalityInfo &info);

        };
    }
}

#endif
