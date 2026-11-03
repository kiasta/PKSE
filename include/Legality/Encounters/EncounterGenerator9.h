/**
 * The Scarlet/Violet encounter generator, and the Gen 9 dispatcher (PKHeX EncounterGenerator9X) that
 * routes a Gen 9 origin to Scarlet/Violet or Legends: Z-A. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Generator/ByGeneration/EncounterGenerator9.cs and
 * Lump/EncounterGenerator9X.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERGENERATOR9_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERGENERATOR9_H

#include <span>

#include "Enums/GameVersion.h"
#include "Legality/Encounters/EncounterEgg9.h"
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
        struct EncounterGenerator9
        {
            /// PKHeX EncounterGenerator9.GetEncounters(pk, chain, info).
            static EncounterCandidates getEncounters(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain, LegalityInfo &info);

            /// PKHeX GetEncountersSWSH: a Scarlet/Violet origin HOME moved to Sword/Shield (met at the
            /// SWSL or SHVL location). `version` is SL or VL; anything but a Sword/Shield record yields
            /// nothing.
            static EncounterCandidates getEncountersSWSH(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain, Enums::GameVersion version);

            /// PKHeX TryGetEgg(pk, chain, version): an unhatched egg with no version is a Scarlet egg.
            static bool tryGetEgg(const Pokemon::Pokemon &pokemon, std::span<const Evolutions::EvolutionCriteria> chain, Enums::GameVersion version, EncounterEgg9 &result);

            /// PKHeX TryGetEgg(chain, version): the egg the lowest stage of `chain` hatched from.
            static bool tryGetEgg(std::span<const Evolutions::EvolutionCriteria> chain, Enums::GameVersion version, EncounterEgg9 &result);
        };

        struct EncounterGenerator9X
        {
            /// PKHeX EncounterGenerator9X.GetEncounters(pk, info).
            static EncounterCandidates getEncounters(const Pokemon::Pokemon &pokemon, LegalityInfo &info);

        };
    }
}

#endif
