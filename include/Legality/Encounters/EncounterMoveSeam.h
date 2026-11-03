/**
 * Where the encounter search hands off to the move verifiers (PKHeX LearnVerifier and
 * LearnVerifierRelearn), which the move port implements in src/Legality/Encounters/EncounterMoveSeam.cpp
 * and stores in LegalityInfo's move results.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERMOVESEAM_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERMOVESEAM_H

#include "Legality/Encounters/LegalityInfo.h"

namespace Pokemon
{
    class Pokemon;
}

namespace Legality
{
    namespace Encounters
    {
        /// PKHeX LearnVerifierRelearn.Verify(info.Relearn, info.EncounterOriginal, pk); returns
        /// MoveResult.AllValid(info.Relearn).
        bool verifyRelearnMoves(LegalityInfo &info, const Pokemon::Pokemon &pokemon);
        /// PKHeX relearn.AsSpan().Fill(MoveResult.Relearn), for formats before Gen 6.
        void fillRelearnValid(LegalityInfo &info);
        /// PKHeX LearnVerifier.Verify(info.Moves, pk, info.EncounterMatch, info.EvoChainsAllGens);
        /// returns MoveResult.AllValid(info.Moves).
        bool verifyCurrentMoves(LegalityInfo &info, const Pokemon::Pokemon &pokemon);
        /// PKHeX MoveInfo.IsAnyFromGeneration(EntityContext.Gen2, info.Moves).
        bool hasGen2Move(const LegalityInfo &info);
        /// PKHeX EvolutionRestrictions.IsValidEvolutionWithMove(pk, info).
        bool isValidEvolutionWithMove(const LegalityInfo &info, const Pokemon::Pokemon &pokemon);
    }
}

#endif
