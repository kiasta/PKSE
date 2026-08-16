/**
 * EncounterMatch.h - PKSE legality Layer 3: encounter matching.
 *
 * Layers 1 and 2 (see Legality.h) ask whether a Pokemon's fields are internally consistent.
 * Layer 3 asks a different question: is there a real encounter in the game it claims to come from that could have produced it?
 * This species, in this form, at this met location, at this met level, with this shiny state, ball, gender and nature?
 *
 * Three things make that answerable without porting PKHeX's RNG machinery:
 *
 *  1. The tables are keyed by the mon's ORIGIN version, not by the save it currently exists in.
 *     A met location only means anything in its own game's namespace.
 *     Z-A and Scarlet/Violet are both Gen 9 and share none of theirs so a Sword Pokemon in a Violet box is checked against Sword's tables,
 *     which is also what PKSE's location display already does.
 *  2. Evolution is walked backwards. A Charizard was caught as a Charmander, so the
 *     match runs over the whole pre-evolution chain (Pokemon::getPreEvolutionChain).
 *  3. Matching is ANY-of. A row only has to exist; a constraint is reported violated
 *     only when EVERY otherwise-matching row violates it.
 *
 * It stays informational, and deliberately errs toward silence:
 *   - Event / Mystery Gift distributions are not in the tables at all, so a Pokemon flagged as a fateful encounter has its verdict softened to a Warning.
 *   - An origin game PKSE has no tables for (anything pre-Gen 3, Pokemon GO, a version byte PKSE does not know) produces one Info note and no findings.
 *   - Cross-game transfers are NOT judged.
 *     PKSE's own bank moves Pokemon along routes no official transfer offers, and flagging its output would be flagging the feature;
 *     per docs/FUTURE_VERSIONS.md the met location is the concern, not the origin marker.
 */

#ifndef LEGALITY_ENCOUNTER_MATCH_H
#define LEGALITY_ENCOUNTER_MATCH_H

#include "Legality/Legality.h"

namespace Pokemon { class Pokemon; }

namespace Legality {
    void checkEncounter(const Pokemon::Pokemon& pk, Report& r);
}

#endif // LEGALITY_ENCOUNTER_MATCH_H