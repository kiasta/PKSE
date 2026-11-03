/**
 * The search loop every PKHeX EncounterEnumeratorN repeats: walk a template array, yield each full
 * Match, and keep the best partial match as the deferred fallback the enumerator yields last. Ported
 * from the TryGetNext / TryGetNextSub members of PKHeX
 * PKHeX.Core/Legality/Encounters/Generator/Search/EncounterEnumerator6.cs (identical in 7 and 7GG).
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERENUMERATORSEARCH_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERENUMERATORSEARCH_H

#include <span>

#include "Legality/Encounters/EncounterSequence.h"
#include "Legality/Encounters/Encounterable.h"
#include "Legality/Evolutions/EvolutionCriteria.h"

namespace Pokemon
{
    class Pokemon;
}

namespace Legality
{
    namespace Encounters
    {
        namespace EnumeratorSearch
        {
            /// PKHeX's Deferred / Rating / Yielded enumerator members.
            struct EnumeratorState
            {
                EncounterReference deferred;
                EncounterMatchRating rating = EncounterMatchRating::MaxNotMatch;
                bool hasYielded = false;
            };

            /// PKHeX TryGetNext for one template: true when it is a full Match to yield now; a better
            /// partial match is kept as the fallback. Only the first chain stage of its species counts.
            template <typename TemplateClass>
            bool isNextMatch(const TemplateClass &encounter, const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionChain &chain, EnumeratorState &state)
            {
                for (const Evolutions::EvolutionCriteria &evolution : chain.view())
                {
                    if (evolution.speciesId != encounter.speciesId())
                    {
                        continue;
                    }
                    if (!encounter.isMatchExact(pokemon, evolution))
                    {
                        return false;
                    }
                    const EncounterMatchRating rating = encounter.getMatchRating(pokemon);
                    if (rating == EncounterMatchRating::Match)
                    {
                        return true;
                    }
                    if (rating < state.rating)
                    {
                        state.deferred = borrowEncounter(encounter);
                        state.rating = rating;
                    }
                    return false;
                }
                return false;
            }

            /// Yields every full Match in `templates`, in order.
            template <typename TemplateClass>
            MatchedEncounters yieldMatches(std::span<const TemplateClass> templates, const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionChain &chain, EnumeratorState &state)
            {
                for (const TemplateClass &encounter : templates)
                {
                    if (isNextMatch(encounter, pokemon, chain, state))
                    {
                        state.hasYielded = true;
                        co_yield MatchedEncounter{borrowEncounter(encounter), EncounterMatchRating::Match};
                    }
                }
            }

            /// PKHeX TryGetNext over areas: the slots of every area met at `metLocation`, in order.
            template <typename AreaClass>
            MatchedEncounters yieldAreaMatches(std::span<const AreaClass> areas, uint16_t metLocation, const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionChain &chain, EnumeratorState &state)
            {
                for (const AreaClass &area : areas)
                {
                    if (!area.isMatchLocation(metLocation))
                    {
                        continue;
                    }
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
