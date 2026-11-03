#include "Legality/Encounters/EncounterEnumerator1.h"

#include "Legality/Encounters/EncounterData1.h"
#include "Legality/Encounters/EncounterEnumeratorGameBoy.h"
#include "Legality/Encounters/EncounterSlot1.h"
#include "Legality/Encounters/GBRestrictions.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        using EnumeratorSearch::EnumeratorState;
        using EnumeratorSearch::yieldEveryAreaMatch;
        using EnumeratorSearch::yieldMatches;

        MatchedEncounters EncounterEnumerator1::enumerate(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain, const LegalityInfo &info)
        {
            if (chain.isEmpty())
            {
                co_return;
            }
            EnumeratorState state;
            const bool isJapanese = GameBoyEntity::isJapanese(pokemon);

            // Trades: Japanese Blue's first, which only a Japanese record can come from.
            if (isJapanese)
            {
                for (const MatchedEncounter &match : yieldMatches(getTradeGift1BU(), pokemon, chain, state))
                {
                    co_yield match;
                }
            }
            for (const MatchedEncounter &match : yieldMatches(getTradeGift1RB(), pokemon, chain, state))
            {
                co_yield match;
            }
            for (const MatchedEncounter &match : yieldMatches(getTradeGift1YW(), pokemon, chain, state))
            {
                co_yield match;
            }

            // Statics
            if (isJapanese)
            {
                for (const MatchedEncounter &match : yieldMatches(getStatic1BU(), pokemon, chain, state))
                {
                    co_yield match;
                }
            }
            for (const MatchedEncounter &match : yieldMatches(getStatic1RB(), pokemon, chain, state))
            {
                co_yield match;
            }
            for (const MatchedEncounter &match : yieldMatches(getStatic1YW(), pokemon, chain, state))
            {
                co_yield match;
            }
            for (const MatchedEncounter &match : yieldMatches(getStatic1RBY(), pokemon, chain, state))
            {
                co_yield match;
            }

            // Wild slots
            if (isJapanese)
            {
                for (const MatchedEncounter &match : yieldEveryAreaMatch(getAreas1BU(), pokemon, chain, state))
                {
                    co_yield match;
                }
            }
            for (const MatchedEncounter &match : yieldEveryAreaMatch(getAreas1RD(), pokemon, chain, state))
            {
                co_yield match;
            }
            for (const MatchedEncounter &match : yieldEveryAreaMatch(getAreas1GN(), pokemon, chain, state))
            {
                co_yield match;
            }
            for (const MatchedEncounter &match : yieldEveryAreaMatch(getAreas1YW(), pokemon, chain, state))
            {
                co_yield match;
            }

            // Events: the Virtual Console Mew, or the cartridge-era gifts when a Game Boy save is open.
            const bool allowVirtualConsole3DS = !info.allowEraCartGB;
            const std::span<const EncounterGift1> gifts = allowVirtualConsole3DS ? getGifts1VirtualConsole() : getGifts1GameBoyEra();
            for (const MatchedEncounter &match : yieldMatches(gifts, pokemon, chain, state))
            {
                co_yield match;
            }

            if (state.deferred)
            {
                co_yield MatchedEncounter{state.deferred, state.rating};
            }
        }
    }
}
