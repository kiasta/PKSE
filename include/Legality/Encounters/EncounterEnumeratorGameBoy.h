/**
 * The area search the Gen 1 and Gen 2 enumerators share: PKHeX's TryGetNext over areas WITHOUT a met
 * location filter (a Game Boy record rarely stores one). Ported from the TryGetNext<TArea, TSlot>
 * member of PKHeX PKHeX.Core/Legality/Encounters/Generator/Search/EncounterEnumerator1.cs (identical
 * in EncounterEnumerator2.cs). The filtered search is EnumeratorSearch::yieldAreaMatches.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERENUMERATORGAMEBOY_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERENUMERATORGAMEBOY_H

#include <span>

#include "Legality/Encounters/EncounterEnumeratorSearch.h"

namespace Legality
{
    namespace Encounters
    {
        namespace EnumeratorSearch
        {
            /// Every slot of every area, in order.
            template <typename AreaClass>
            MatchedEncounters yieldEveryAreaMatch(std::span<const AreaClass> areas, const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionChain &chain, EnumeratorState &state)
            {
                for (const AreaClass &area : areas)
                {
                    for (uint16_t slotIndex = 0; slotIndex < area.slotCount(); slotIndex++)
                    {
                        const auto &slot = area.slotAt(slotIndex);
                        if (isNextMatch(slot, pokemon, chain, state))
                        {
                            state.hasYielded = true;
                            co_yield MatchedEncounter{borrowEncounter(slot), EncounterMatchRating::Match};
                        }
                    }
                }
            }
        }
    }
}

#endif
