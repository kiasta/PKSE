#include "Legality/Encounters/EncounterEnumerator8a.h"

#include <span>

#include "Legality/Encounters/EncounterData8a.h"
#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterRules8a.h"
#include "Legality/Encounters/EncounterSlot8a.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            /// The enumerator's running state: PKHeX's Deferred / Rating / Yielded members.
            struct EnumeratorState
            {
                EncounterReference deferred;
                EncounterMatchRating rating = EncounterMatchRating::MaxNotMatch;
                bool yielded = false;
            };

            /// PKHeX TryGetNext over one template: true with its rating when `encounter` matches the
            /// chain's first stage of its species.
            template <typename TemplateClass>
            bool isNextMatch(const TemplateClass &encounter, const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionChain &chain, EncounterMatchRating &rating)
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
                    rating = encounter.getMatchRating(pokemon);
                    return true;
                }
                return false;
            }

            template <typename TemplateClass>
            MatchedEncounters yieldMatches(std::span<const TemplateClass> templates, const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionChain &chain, EnumeratorState &state)
            {
                for (const TemplateClass &encounter : templates)
                {
                    EncounterMatchRating rating = EncounterMatchRating::MaxNotMatch;
                    if (!isNextMatch(encounter, pokemon, chain, rating))
                    {
                        continue;
                    }
                    if (rating == EncounterMatchRating::Match)
                    {
                        state.yielded = true;
                        co_yield MatchedEncounter{borrowEncounter(encounter), EncounterMatchRating::Match};
                    }
                    else if (rating < state.rating)
                    {
                        state.deferred = borrowEncounter(encounter);
                        state.rating = rating;
                    }
                }
            }

            /// PKHeX TryGetNext over the areas. PKHeX's slot stage reuses the sub-index the static
            /// stage left at the end of StaticLA and resets it only when it moves past an area, so the
            /// first area, when it is a candidate, skips its first StaticLA-length slots.
            MatchedEncounters yieldSlotMatches(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionChain &chain, EnumeratorState &state)
            {
                const uint16_t metLocation = pokemon.metLocation();
                const bool hasOriginalMet = hasOriginalMetLocation8a(pokemon);
                size_t firstSlotIndex = getStatic8aLA().size();
                for (const EncounterArea8a &area : getAreas8aLA())
                {
                    const size_t startSlotIndex = firstSlotIndex;
                    firstSlotIndex = 0;
                    if (hasOriginalMet && !area.isMatchLocation(metLocation))
                    {
                        continue;
                    }
                    for (size_t slotIndex = startSlotIndex; slotIndex < area.slotCount(); slotIndex++)
                    {
                        const EncounterSlot8a slot = area.slotAt(static_cast<uint16_t>(slotIndex));
                        EncounterMatchRating rating = EncounterMatchRating::MaxNotMatch;
                        if (!isNextMatch(slot, pokemon, chain, rating))
                        {
                            continue;
                        }
                        if (rating == EncounterMatchRating::Match)
                        {
                            state.yielded = true;
                            co_yield MatchedEncounter{makeEncounter<EncounterSlot8a>(slot), EncounterMatchRating::Match};
                        }
                        else if (rating < state.rating)
                        {
                            state.deferred = makeEncounter<EncounterSlot8a>(slot);
                            state.rating = rating;
                        }
                    }
                }
            }
        }

        MatchedEncounters EncounterEnumerator8a::enumerate(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain)
        {
            if (chain.isEmpty() || pokemon.isEgg())
            {
                co_return;
            }
            EnumeratorState state;

            if (pokemon.isFatefulEncounter())
            {
                for (const MatchedEncounter &match : yieldMatches(getGifts8aWA8(), pokemon, chain, state))
                {
                    co_yield match;
                }
                // PKHeX EncounterEvent.EGDB_G8A (the user's own event folder) follows; PKSE has none.
                if (state.yielded)
                {
                    co_return;
                }
            }

            // A static can collide with a wild spawn, so a yielded static does not end the search.
            for (const MatchedEncounter &match : yieldMatches(getStatic8aLA(), pokemon, chain, state))
            {
                co_yield match;
            }
            if (EncounterStateUtil::canBeWildEncounter(pokemon))
            {
                for (const MatchedEncounter &match : yieldSlotMatches(pokemon, chain, state))
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
