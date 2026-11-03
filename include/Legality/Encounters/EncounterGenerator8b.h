/**
 * The Brilliant Diamond/Shining Pearl encounter generator: hands a BDSP-origin record to the
 * enumerator, in its own format or in Sword/Shield after HOME. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Generator/ByGeneration/EncounterGenerator8b.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERGENERATOR8B_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERGENERATOR8B_H

#include <span>

#include "Enums/GameVersion.h"
#include "Legality/Encounters/EncounterEgg8b.h"
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
        struct EncounterGenerator8b
        {
            /// PKHeX EncounterGenerator8b.GetEncounters(pk, chain, info).
            static EncounterCandidates getEncounters(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain, LegalityInfo &info);

            /// PKHeX GetEncountersSWSH: a Sword/Shield record HOME remapped from `version` (BD or SP).
            static EncounterCandidates getEncountersSwordShield(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain, Enums::GameVersion version);

            /// PKHeX TryGetEgg: the egg the lowest stage of `chain` hatched from.
            static bool tryGetEgg(std::span<const Evolutions::EvolutionCriteria> chain, Enums::GameVersion version, EncounterEgg8b &result);

            /// PKHeX TryGetSplit: the split-breed alternative to `other`.
            static bool tryGetSplit(const EncounterEgg8b &other, std::span<const Evolutions::EvolutionCriteria> chain, EncounterEgg8b &result);
        };
    }
}

#endif
