/**
 * The basic compatibility check the finder runs on each candidate before the secondary checks.
 * Ported from PKHeX PKHeX.Core/Legality/Encounters/Verifiers/EncounterVerifier.cs.
 *
 * Per-generation ports fill in their own hatched/unhatched egg functions and template rules in
 * EncounterVerifier.cpp; a context whose rules are not ported yet answers Valid (unknown), never
 * Invalid.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERVERIFIER_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERVERIFIER_H

#include <cstdint>

#include "Legality/Encounters/CheckResult.h"
#include "Legality/Encounters/Encounterable.h"

namespace Pokemon
{
    class Pokemon;
}

namespace Legality
{
    namespace Encounters
    {
        struct LegalityInfo;

        struct EncounterVerifier
        {
            /// PKHeX GetEncounterVerifierMethod(generation) applied to (pk, enc). `info` supplies the
            /// ParseSettings era rules (AllowEraCartGBA / AllowEraSwitchGBA) and the entity facts.
            static CheckResult verifyEncounter(uint8_t generation, const Pokemon::Pokemon &pokemon, const Encounterable &encounter, const LegalityInfo &info);

            /// PKHeX VerifyEncounterEggHatched, for MysteryGift templates that hatch (VerifyEncounterEvent).
            static CheckResult verifyEncounterEggHatched(const Pokemon::Pokemon &pokemon, Enums::GameVersion contextGroup, const LegalityInfo &info);
        };
    }
}

#endif
