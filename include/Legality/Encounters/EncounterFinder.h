/**
 * Finds the encounter that explains a Pokemon and runs the checks that pick between candidates.
 * Ported from PKHeX PKHeX.Core/Legality/Encounters/Generator/EncounterFinder.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERFINDER_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERFINDER_H

#include "Legality/Encounters/LegalityInfo.h"

namespace Pokemon
{
    class Pokemon;
}

namespace Legality
{
    namespace Encounters
    {
        struct EncounterFinder
        {
            /// PKHeX FindVerifiedEncounter: fills info's match, PIDIV, move results and parse.
            static void findVerifiedEncounter(const Pokemon::Pokemon &pokemon, LegalityInfo &info);

            /// PKHeX LegalityAnalysis.UpdateVCTransferInfo (from ParsePK7): for a Gen 7 format Virtual
            /// Console Pokemon, keeps the Game Boy match as encounterOriginalGameBoy and swaps in its
            /// Gen 7 transfer template, which discards the findings made for the Game Boy match. PKHeX's VerifyVCEncounter and
            /// VerifyTransferLegalityG12 follow in TransferVerifier.
            static void updateVirtualConsoleTransferInfo(const Pokemon::Pokemon &pokemon, LegalityInfo &info);
        };
    }
}

#endif
