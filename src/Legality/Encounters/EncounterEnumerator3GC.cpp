#include "Legality/Encounters/EncounterEnumerator3GC.h"

#include "Enums/LanguageID.h"
#include "Legality/Encounters/EncounterData3GC.h"
#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterEnumeratorGameBoy.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        using EnumeratorSearch::EnumeratorState;
        using EnumeratorSearch::isNextMatch;
        using EnumeratorSearch::yieldAreaMatches;
        using EnumeratorSearch::yieldEveryAreaMatch;
        using EnumeratorSearch::yieldMatches;

        MatchedEncounters EncounterEnumerator3GC::enumerate(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain)
        {
            if (chain.isEmpty())
            {
                co_return;
            }
            if (!EncounterStateUtil::canBeWildEncounter(pokemon))
            {
                co_return;
            }
            EnumeratorState state;

            for (const MatchedEncounter &match : yieldMatches(getTrades3XD(), pokemon, chain, state))
            {
                co_yield match;
            }
            for (const MatchedEncounter &match : yieldMatches(getShadow3Colo(), pokemon, chain, state))
            {
                co_yield match;
            }
            for (const MatchedEncounter &match : yieldMatches(getStarters3Colo(), pokemon, chain, state))
            {
                co_yield match;
            }
            for (const MatchedEncounter &match : yieldMatches(getGifts3Colo(), pokemon, chain, state))
            {
                co_yield match;
            }
            for (const MatchedEncounter &match : yieldMatches(getShadow3XD(), pokemon, chain, state))
            {
                co_yield match;
            }
            for (const MatchedEncounter &match : yieldMatches(getGifts3XD(), pokemon, chain, state))
            {
                co_yield match;
            }

            // The e-Reader shadows were Japanese only. PKHeX yields the first match and then goes
            // straight to the fallback, skipping the Poke Spots.
            if (pokemon.language() == static_cast<uint8_t>(Enums::LanguageID::Japanese))
            {
                for (const EncounterShadow3Colo &shadow : getShadow3ColoEReader())
                {
                    if (isNextMatch(shadow, pokemon, chain, state))
                    {
                        co_yield MatchedEncounter{borrowEncounter(shadow), EncounterMatchRating::Match};
                        if (state.deferred)
                        {
                            co_yield MatchedEncounter{state.deferred, state.rating};
                        }
                        co_return;
                    }
                }
            }

            // Poke Spots, filtered by the met location while the record still has it.
            if (getFormatGeneration(pokemon) == 3)
            {
                for (const MatchedEncounter &match : yieldAreaMatches(getAreas3XD(), pokemon.metLocation(), pokemon, chain, state))
                {
                    co_yield match;
                }
            }
            else
            {
                for (const MatchedEncounter &match : yieldEveryAreaMatch(getAreas3XD(), pokemon, chain, state))
                {
                    co_yield match;
                }
            }

            if (state.deferred)
            {
                co_yield MatchedEncounter{state.deferred, state.rating};
            }
        }
    }
}
