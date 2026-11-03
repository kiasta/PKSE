/**
 * The Gen 3 encounter generator: runs EncounterEnumerator3, checks each template's PID/IV
 * correlation and ball, orders wild slots by the lead they need, and yields the best-ranked
 * deferral last. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Generator/ByGeneration/EncounterGenerator3.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERGENERATOR3_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERGENERATOR3_H

#include <span>

#include "Enums/GameVersion.h"
#include "Legality/Encounters/EncounterEgg3.h"
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
        struct EncounterGenerator3
        {
            /// PKHeX GetEncounters(pk, info): the chain is the Gen 3 origin chain.
            static EncounterCandidates getEncounters(const Pokemon::Pokemon &pokemon, LegalityInfo &info);

            /// PKHeX GetEncounters(pk, chain, info, version).
            static EncounterCandidates getEncounters(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain, LegalityInfo &info, Enums::GameVersion version);

            /// PKHeX GetGameToIterate: the Virtual Console ids search as the cartridge they emulate.
            static Enums::GameVersion getGameToIterate(Enums::GameVersion version) noexcept;

            /// PKHeX TryGetEgg: the egg the lowest stage of `chain` hatched from.
            static bool tryGetEgg(std::span<const Evolutions::EvolutionCriteria> chain, Enums::GameVersion version, EncounterEgg3 &result);

            /// PKHeX TryGetSplit: the incense-free split-breed alternative to `other`.
            static bool tryGetSplit(const EncounterEgg3 &other, std::span<const Evolutions::EvolutionCriteria> chain, EncounterEgg3 &result);
        };
    }
}

#endif
