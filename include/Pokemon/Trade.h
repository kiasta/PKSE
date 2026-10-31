/**
 * Trade.h - Writing the handler block as a real trade would.
 *
 * WHAT A TRADE RECORDS. Before Generation 6 a trade writes NOTHING to the record: there is no
 * handler field, the receiving game simply runs its evolution check on arrival. From Gen 6 on the
 * record carries a handling trainer, and that block is the only evidence a trade ever happened.
 *
 * WHY A ROUND TRIP. `applyTradeRoundTrip` emulates source -> partner -> source, which is what a
 * player does to trade-evolve a Pokemon and keep it. The two halves are not symmetrical: the
 * outbound trade writes the whole handler block, and the return leg writes exactly one thing --
 * `CurrentHandler = 0`. **THE HANDLER BLOCK IS NOT CLEARED ON THE WAY BACK** (PKHeX's TradeOT does
 * nothing else), and that residue is the point: the Pokemon is home with its original trainer while
 * still carrying the other player's name. That is the state every legitimately trade-evolved
 * Pokemon is in, and the state `EvolutionMethod.ValidNotLevelUp` looks for when it decides whether
 * a trade evolution was possible.
 *
 * THERE ARE FOUR BEHAVIOURS, NOT ONE. PKHeX implements them as separate overrides and so does
 * this: see TradeEra in the implementation. Generation 6 writes a link-trade memory and a
 * geolocation history; Generation 7 stores both fields but writes neither on a trade; Let's Go has
 * neither and copies friendship rather than resetting it; Gen 8 and 9 store a handler language and
 * leave the memory to the game.
 */
#ifndef POKEMON_TRADE_H
#define POKEMON_TRADE_H

#include <cstdint>
#include <string>

namespace Pokemon
{

    class Pokemon;

    /**
     * The trainer on the other end of the trade -- everything the handler block can record about
     * them, and nothing else.
     *
     * A real donor save supplies all of it. Where a field is absent from the destination format it
     * is ignored rather than stored, so a Gen 8 donor can hand its language to a Gen 9 Pokemon and
     * have it dropped for a Gen 6 one, which has no such field.
     */
    struct TradePartner
    {
        std::u16string trainerName;
        uint8_t gender = 0;         ///< 0 = male, 1 = female, as the games store it
        uint8_t language = 0;       ///< Enums::LanguageID; clamped to one the destination shipped in
        uint8_t consoleCountry = 0; ///< Gen 6/7 geolocation only. 0 reads as "not recorded"
        uint8_t consoleRegion = 0;  ///< ...and is what a donor save that cannot supply one leaves
    };

    /**
     * Stamps `partner` into `pokemon`'s handler block as a trade away and back would leave it.
     *
     * Returns false and changes nothing when the format has no handler (Gens 1-5, where a trade is
     * genuinely invisible in the data) or when the partner has no name -- a nameless handler is
     * worse than none, because it reads as a field the game failed to write.
     *
     * Leaves the record checksummed.
     */
    bool applyTradeRoundTrip(Pokemon &pokemon, const TradePartner &partner);

    /**
     * Makes `receiver` the active handler of a Pokemon arriving from another game: the outbound
     * half of a trade, with no return leg.
     *
     * Every route into a game with a handler does this, the player's own Pokemon included.
     * Transporter, Bank and HOME each make the receiving player the handler (PKHeX
     * PK5.ConvertToPK6, PKH.UpdateHandler), and PKHeX reports a Pokemon from another game whose
     * original trainer is still the active handler as invalid (TransferHandlerFlagRequired).
     *
     * Same refusals as applyTradeRoundTrip. Leaves the record checksummed.
     */
    bool applyReceivingTrainer(Pokemon &pokemon, const TradePartner &receiver);

    /**
     * A trade partner PKSE invents, for when the user has no second save to point at.
     *
     * **THIS FABRICATES A PERSON, which nothing else in PKSE does** -- every other invented value
     * (a met location, an IV spread) is a claim about the POKEMON. So it invents as little as it
     * can: the name is the games' own generic trainer label, the same string PKSE already
     * substitutes for an in-game-trade OT coming out of Gen 1/2, rather than a plausible human
     * name that would read as a specific person who does not exist. The language is the RECORD's
     * own, because a partner playing in another language is a further claim with nothing behind it.
     *
     * Deterministic, seeded by the record's own encryption constant, for the reason every other
     * invented value in PKSE is: a result that differed every time could not be tested, and undoing
     * and redoing the same action would produce a different Pokemon.
     */
    TradePartner generateTradePartner(const Pokemon &pokemon);

    /**
     * A feeling a Gen 6/7 link-trade memory may carry, picked by `choice` from the legal ones below
     * `feelingLimit` -- PKHeX MemoryContext6.GetRandomFeeling6(4, limit), which draws below 20 for a
     * trade and below 10 for a Pokemon Bank transfer. A plain `choice % limit` lands on a feeling the
     * MemoryVerifier rejects (1, 10, 12, 13, 16, 17, 19 are not in the mask).
     */
    uint8_t linkTradeMemoryFeeling(uint32_t choice, uint8_t feelingLimit);
}

#endif  // POKEMON_TRADE_H
