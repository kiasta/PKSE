/**
 * Evolution.h - Performing a trade evolution on a record PKSE is holding.
 *
 * A TRADE EVOLUTION IS A LOCAL RECORD MUTATION. The trade is only the trigger: the receiving game
 * runs its ordinary evolution check when the Pokemon arrives, the check passes because a trade is
 * what happened, and the record is rewritten in place. Nothing in the save records that a trade
 * took place, no second party appears in the data, and there is nothing to reconcile afterwards --
 * so no second save file is needed to produce the same bytes the games would have produced.
 *
 * What the games change, and what this does:
 *   SPECIES + FORM   the destination pair. A form of "keep what it had" is already resolved in the
 *                    table, so Alolan Graveler becomes Alolan Golem and a Small Pumpkaboo becomes a
 *                    Small Gourgeist.
 *   ABILITY          the same SLOT, re-read for the new species -- a hidden-ability Pokemon stays
 *                    hidden. Identical to what the species picker does (applySpeciesChange).
 *   HELD ITEM        CONSUMED when the Pokemon is holding the one the evolution asks for. The
 *                    games destroy it; it does not return to the bag. Not holding it is not a
 *                    refusal -- see TradeEvolutionOffer.
 *   NICKNAME         follows the species, but ONLY when it was the species name to begin with.
 *   STATS            recalculated from the new base stats.
 *
 * WHAT IT DELIBERATELY DOES NOT TOUCH. **EXP and level stay exactly as they are.** The games do not
 * award or reset experience on evolution, and every one of the twenty-six trade-evolution pairs
 * shares a growth rate with its pre-evolution, so the stored EXP still means the same level
 * afterwards. That is the one place this must NOT reuse applySpeciesChange, which re-derives EXP
 * from the level because an arbitrary species swap can cross growth rates -- doing that here would
 * silently discard progress toward the next level. Friendship is not touched either: a TRADE
 * changes friendship, an evolution does not.
 *
 * FROM GENERATION 6 ON A TRADE LEAVES A TRACE, AND THIS IS NOT WHAT WRITES IT. Such a record carries
 * a handling trainer, and PKHeX reads an empty one as "never traded" (EvolutionMethod.ValidNotLevelUp
 * rejects a trade evolution on an untraded Pokemon). Stamping that handler belongs to
 * `Pokemon/Trade.h`, and the two run in that order -- the round trip first, then this -- because the
 * new handler's friendship is seeded from the species, which at that moment is still the
 * PRE-evolution. Gens 1-5 have no such field, so there the evolution is the whole of it.
 */
#ifndef POKEMON_EVOLUTION_H
#define POKEMON_EVOLUTION_H

#include <cstdint>
#include <string>

#include "Enums/GameVersion.h"
#include "Pokemon/EvolutionTable.h"

namespace Pokemon
{

    class Pokemon;

    /** A species name as the record's own game and language would have stored it: upper case
     *  before Gen 5 outside Japanese and Korean (PKHeX SpeciesName.GetSpeciesName1234). */
    std::u16string speciesNameAsStored(uint16_t speciesId, Enums::GameVersion gameGroup, uint8_t languageId);

    /**
     * The trade evolutions a Pokemon's species+form has in its own game, and which one -- if any --
     * its held item names.
     *
     * **THE HELD ITEM IS A SELECTOR, NOT A GATE.** Refusing to evolve a Scyther that is not holding
     * a Metal Coat enforced nothing: the Held row is two rows further down the same page, so the
     * refusal was a step to work around rather than a rule, and working around it produced exactly
     * the same bytes. What the item genuinely decides is WHICH evolution happens, and that is a
     * real question for exactly one species -- Clamperl, whose Deep Sea Tooth and Deep Sea Scale
     * lead to different Pokemon. So the button is enabled wherever there is a trade evolution at
     * all, and an item that IS held is still consumed, because that is what the games do with it.
     */
    struct TradeEvolutionOffer
    {
        int candidateCount = 0;
        TradeEvolution candidates[TRADE_EVOLUTIONS_MAX_PER_SPECIES]{};
        /// Index into `candidates` of the one whose required item the Pokemon is holding right
        /// now, or -1 when it holds none of them. This is not permission -- see resolvedIndex().
        int heldItemIndex = -1;

        /// True when this species+form trade-evolves in this game at all -- the test for showing
        /// the button, and, since the item stopped being a gate, for enabling it too.
        bool hasCandidate() const noexcept { return candidateCount > 0; }

        /// The candidate a press performs with nothing further to ask: the one whose item is held,
        /// or the only one on offer. **-1 means the answer is genuinely unknown** -- more than one
        /// destination and no held item naming which -- and a caller must ASK rather than pick.
        /// That is Clamperl holding neither of its two items, and nothing else in any game.
        int resolvedIndex() const noexcept
        {
            if (heldItemIndex >= 0)
                return heldItemIndex;
            return (candidateCount == 1) ? 0 : -1;
        }

        /// Clamps rather than reading out of bounds, so a caller holding a stale index gets
        /// candidate 0 instead of UB.
        const TradeEvolution &evolutionAt(int candidateIndex) const noexcept
        {
            return candidates[(candidateIndex > 0 && candidateIndex < candidateCount) ? candidateIndex : 0];
        }
    };

    /**
     * What `pokemon` would become if it were traded, read against ITS OWN game group rather than
     * the open save's -- a bank slot holds a foreign Pokemon, and the evolutions available to it
     * are the ones its own game has.
     */
    TradeEvolutionOffer getTradeEvolutionOffer(const Pokemon &pokemon);

    /**
     * Performs candidate `candidateIndex` of the offer, in place. Returns false and changes
     * nothing when the Pokemon has no trade evolution, or when the index names none of them.
     *
     * **THE HELD ITEM IS CONSUMED ONLY WHEN IT IS THE ONE THE ROW ASKS FOR.** A Scyther evolving
     * without a Metal Coat keeps whatever it was carrying: destroying an unrelated Leftovers
     * because the row happens to be a held-item row is a silent loss, and the games only ever
     * destroy the item the evolution actually used.
     *
     * Leaves the record checksummed and its stats recalculated, so the caller does not repeat
     * either.
     */
    bool applyTradeEvolution(Pokemon &pokemon, int candidateIndex);
}

#endif  // POKEMON_EVOLUTION_H
