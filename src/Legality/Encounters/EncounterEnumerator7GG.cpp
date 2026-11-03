#include "Legality/Encounters/EncounterEnumerator7GG.h"

#include "Legality/Encounters/EncounterData7.h"
#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterEnumeratorSearch.h"
#include "Legality/Encounters/EncounterLocations.h"
#include "Legality/Encounters/EncounterSlot7b.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            using EnumeratorSearch::EnumeratorState;
            using EnumeratorSearch::yieldAreaMatches;
            using EnumeratorSearch::yieldMatches;
        }

        MatchedEncounters EncounterEnumerator7GG::enumerate(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain, Enums::GameVersion version)
        {
            if (chain.isEmpty() || pokemon.isEgg())
            {
                co_return;
            }
            EnumeratorState state;
            const bool isPikachu = version == Enums::GameVersion::GP;
            const bool isEevee = version == Enums::GameVersion::GE;
            const uint16_t met = pokemon.metLocation();

            if (met == LOCATION_LINK_TRADE6_NPC)
            {
                // PKHeX YieldState.TradeStart .. TradeShared: the version's trades, then the shared ones.
                if (!isPikachu && !isEevee)
                {
                    co_return;
                }
                for (const std::span<const EncounterTrade7b> trades : {isPikachu ? getTradeGift7bGP() : getTradeGift7bGE(), getTradeGift7bGG()})
                {
                    for (const MatchedEncounter &match : yieldMatches(trades, pokemon, chain, state))
                    {
                        co_yield match;
                    }
                }
                co_return;
            }

            if (pokemon.isFatefulEncounter())
            {
                for (const MatchedEncounter &match : yieldMatches(getMysteryGifts7b(), pokemon, chain, state))
                {
                    co_yield match;
                }
                // PKHeX EncounterEvent.EGDB_G7GG (the user's own event folder) follows; PKSE has none.
                if (state.hasYielded)
                {
                    co_return;
                }
            }

            // PKHeX YieldState.StaticStart: a version outside the pair skips to the fallback.
            if (isPikachu || isEevee)
            {
                for (const std::span<const EncounterStatic7b> statics : {isPikachu ? getStatic7bGP() : getStatic7bGE(), getStatic7bGG()})
                {
                    for (const MatchedEncounter &match : yieldMatches(statics, pokemon, chain, state))
                    {
                        co_yield match;
                    }
                }
                if (state.hasYielded)
                {
                    co_return;
                }

                // PKHeX YieldState.SlotStart .. SlotGE.
                if (!EncounterStateUtil::canBeWildEncounter(pokemon))
                {
                    co_return;
                }
                for (const MatchedEncounter &match : yieldAreaMatches(isPikachu ? getAreas7bGP() : getAreas7bGE(), met, pokemon, chain, state))
                {
                    co_yield match;
                }
                // PKHeX's Let's Go Eevee branch stops once anything yielded; the Pikachu branch always
                // reaches the fallback.
                if (isEevee && state.hasYielded)
                {
                    co_return;
                }
            }

            if (state.deferred)
            {
                co_yield MatchedEncounter{state.deferred, state.rating};
            }
        }
    }
}
