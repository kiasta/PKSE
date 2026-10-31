/**
 * Layers 1 and 2 (see Legality.h) ask whether a Pokemon's fields are internally consistent.
 * Layer 3 asks a different question: is there a real encounter in the game it claims to come from that could have
 * produced it? This species, in this form, at this met location, at this met level, with this shiny state, ball, gender
 * and nature?
 *
 * Three things make that answerable without porting PKHeX's RNG machinery:
 *
 *  1. The tables are keyed by the pokemon's ORIGIN version, not by the save it currently exists in.
 *     A met location only means anything in its own game's namespace.
 *     Z-A and Scarlet/Violet are both Gen 9 and share none of theirs so a Sword Pokemon in a Violet box is checked
 * against Sword's tables, which is also what PKSE's location display already does.
 *  2. Evolution is walked backwards. A Charizard was caught as a Charmander, so the
 *     match runs over the whole pre-evolution chain (Pokemon::getPreEvolutionChain).
 *  3. Matching is ANY-of. A row only has to exist; a constraint is reported violated
 *     only when EVERY otherwise-matching row violates it.
 *
 * It stays informational, and deliberately errs toward silence:
 *   - The Mystery Gift distributions PKHeX archives (its mgdb pickles, for the games that have them) ARE in the tables,
 * but a card PKHeX never archived, or a game with no pickle at all, is not -- so a fateful encounter that matches
 * nothing has its verdict softened to a Warning.
 *   - An origin game PKSE has no tables for (Pokemon GO, Colosseum/XD, a version byte PKSE does not know) produces
 * one Info note and no findings.
 *   - A Pokemon that left Gen 3 or Gen 4 is not judged: Pal Park and Poke Transfer replaced its met location.
 */

#ifndef LEGALITY_ENCOUNTER_MATCH_H
#define LEGALITY_ENCOUNTER_MATCH_H

#include "Legality/Legality.h"

namespace Pokemon
{
    class Pokemon;
}

namespace Legality
{

    /**
     * Run Layer 3 against `pk` and append what it finds to `r`.
     *
     * Takes no game argument on purpose: the tables are selected by the Pokemon's own
     * origin version, and the save it currently sits in has no say in whether its met
     * location was reachable.
     */
    void checkEncounter(const Pokemon::Pokemon &pokemon, Report &report);
}

#endif // LEGALITY_ENCOUNTER_MATCH_H
