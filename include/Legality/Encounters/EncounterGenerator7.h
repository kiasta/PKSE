/**
 * The Sun/Moon and Ultra Sun/Ultra Moon encounter generator, and PKHeX's Gen 7 lump that routes a
 * Gen 7 origin to it, to Let's Go or to GO. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Generator/ByGeneration/EncounterGenerator7.cs and
 * PKHeX.Core/Legality/Encounters/Generator/ByGeneration/Lump/EncounterGenerator7X.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERGENERATOR7_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERGENERATOR7_H

#include <span>

#include "Enums/GameVersion.h"
#include "Legality/Encounters/EncounterEgg7.h"
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
        struct EncounterGenerator7
        {
            /// PKHeX GetEncounters(pk, chain, info).
            static EncounterCandidates getEncounters(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain, LegalityInfo &info);

            /// PKHeX TryGetEgg: the egg the lowest stage of `chain` hatched from.
            static bool tryGetEgg(std::span<const Evolutions::EvolutionCriteria> chain, Enums::GameVersion version, EncounterEgg7 &result);
            /// PKHeX MutateEggTrade: the same egg bred in the other version pair (Sun <-> Ultra Sun, Moon <-> Ultra Moon).
            static EncounterEgg7 mutateEggTrade(const EncounterEgg7 &egg) noexcept;
            /// PKHeX TryGetSplit: the split-breed alternative to `other`.
            static bool tryGetSplit(const EncounterEgg7 &other, std::span<const Evolutions::EvolutionCriteria> chain, EncounterEgg7 &result);
        };

        struct EncounterGenerator7X
        {
            /// PKHeX EncounterGenerator7X.GetEncounters(pk, info).
            static EncounterCandidates getEncounters(const Pokemon::Pokemon &pokemon, LegalityInfo &info);


        private:
            /// PKHeX EncounterGenerator7GG.GetEncounters (Let's Go Pikachu and Eevee).
            static EncounterCandidates getEncountersLetsGo(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain);
        };
    }
}

#endif
