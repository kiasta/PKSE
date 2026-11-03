/**
 * Whether the Pokemon's current species is reachable from its matched encounter. Ported from PKHeX
 * PKHeX.Core/Legality/Encounters/Verifiers/EvolutionVerifier.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_EVOLUTIONVERIFIER_H
#define LEGALITY_ENCOUNTERS_EVOLUTIONVERIFIER_H

#include "Legality/Encounters/CheckResult.h"
#include "Legality/Encounters/LegalityInfo.h"

namespace Pokemon
{
    class Pokemon;
}

namespace Legality
{
    namespace Encounters
    {
        struct EvolutionVerifier
        {
            /// PKHeX VerifyEvolution.
            static CheckResult verifyEvolution(const Pokemon::Pokemon &pokemon, const LegalityInfo &info);
        };
    }
}

#endif
