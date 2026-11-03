/**
 * The X/Y and Omega Ruby/Alpha Sapphire encounter generator. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Generator/ByGeneration/EncounterGenerator6.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERGENERATOR6_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERGENERATOR6_H

#include <span>

#include "Enums/GameVersion.h"
#include "Legality/Encounters/EncounterEgg6.h"
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
        struct EncounterGenerator6
        {
            /// PKHeX GetEncounters(pk, info).
            static EncounterCandidates getEncounters(const Pokemon::Pokemon &pokemon, LegalityInfo &info);

            /// PKHeX TryGetEgg: the egg the lowest stage of `chain` hatched from.
            static bool tryGetEgg(std::span<const Evolutions::EvolutionCriteria> chain, Enums::GameVersion version, EncounterEgg6 &result);
            /// PKHeX MutateEggTrade: the same egg bred in the other version pair (X <-> Alpha Sapphire, Y <-> Omega Ruby).
            static EncounterEgg6 mutateEggTrade(const EncounterEgg6 &egg) noexcept;
            /// PKHeX TryGetSplit: the split-breed alternative to `other`.
            static bool tryGetSplit(const EncounterEgg6 &other, std::span<const Evolutions::EvolutionCriteria> chain, EncounterEgg6 &result);

        };
    }
}

#endif
