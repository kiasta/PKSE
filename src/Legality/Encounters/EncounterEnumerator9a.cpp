#include "Legality/Encounters/EncounterEnumerator9a.h"

#include <span>

#include "Legality/Encounters/EncounterData9a.h"
#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterLocations.h"
#include "Legality/Encounters/EncounterRules9.h"
#include "Legality/Encounters/EncounterSlot9a.h"
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

            /// PKHeX EncounterEnumerator9a.TryGetNext as a filter: true when `encounter` is a full Match
            /// to yield now; a better partial match is kept as the fallback.
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

            template <typename TemplateClass>
            MatchedEncounters yieldMatches(std::span<const TemplateClass> templates, const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionChain &chain, EnumeratorState &state)
            {
                for (const TemplateClass &encounter : templates)
                {
                    if (isNextMatch(encounter, pokemon, chain, state))
                    {
                        state.yielded = true;
                        co_yield MatchedEncounter{borrowEncounter(encounter), EncounterMatchRating::Match};
                    }
                }
            }

            /// PKHeX TryGetNext<TArea, TSlot>: only the areas at the met location (Z-A has no crossovers).
            MatchedEncounters yieldAreaMatches(std::span<const EncounterArea9a> areas, const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionChain &chain, EnumeratorState &state)
            {
                const uint16_t metLocation = pokemon.metLocation();
                for (const EncounterArea9a &area : areas)
                {
                    if (!area.isMatchLocation(metLocation))
                    {
                        continue;
                    }
                    for (uint16_t slotIndex = 0; slotIndex < area.slotCount(); slotIndex++)
                    {
                        const EncounterSlot9a &slot = area.slotAt(slotIndex);
                        if (isNextMatch(slot, pokemon, chain, state))
                        {
                            state.yielded = true;
                            co_yield MatchedEncounter{borrowEncounter(slot), EncounterMatchRating::Match};
                        }
                    }
                }
            }

            /// PKHeX YieldState.SlotStart .. SlotEnd: the standard slots, then Hyperspace.
            MatchedEncounters yieldSlots(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionChain &chain, EnumeratorState &state)
            {
                if (!EncounterStateUtil::canBeWildEncounter(pokemon))
                {
                    co_return;
                }
                for (const MatchedEncounter &match : yieldAreaMatches(getAreas9aStandard(), pokemon, chain, state))
                {
                    co_yield match;
                }
                for (const MatchedEncounter &match : yieldAreaMatches(getAreas9aHyperspace(), pokemon, chain, state))
                {
                    co_yield match;
                }
            }

            /// PKHeX YieldState.StaticStart .. StaticEnd: the statics, then the gifts.
            MatchedEncounters yieldStatics(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionChain &chain, EnumeratorState &state)
            {
                for (const MatchedEncounter &match : yieldMatches(getStatic9a(), pokemon, chain, state))
                {
                    co_yield match;
                }
                for (const MatchedEncounter &match : yieldMatches(getGifts9a(), pokemon, chain, state))
                {
                    co_yield match;
                }
            }
        }

        MatchedEncounters EncounterEnumerator9a::enumerate(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain)
        {
            if (chain.isEmpty())
            {
                co_return;
            }
            EnumeratorState state;

            if (pokemon.isFatefulEncounter())
            {
                for (const MatchedEncounter &match : yieldMatches(getGifts9aWA9(), pokemon, chain, state))
                {
                    co_yield match;
                }
                // PKHeX EncounterEvent.EGDB_G9A (the user's own event folder) follows; PKSE has none.
                if (state.yielded)
                {
                    co_return;
                }
            }

            if (pokemon.metLocation() == LOCATION_LINK_TRADE6_NPC)
            {
                for (const MatchedEncounter &match : yieldMatches(getTrade9a(), pokemon, chain, state))
                {
                    co_yield match;
                }
                if (state.yielded)
                {
                    co_return;
                }
            }

            // PKHeX StartCaptures: a record with an encounter mark was a wild slot, so its slots go first.
            if (readRibbonMarks9(pokemon).hasEncounterMark)
            {
                for (const MatchedEncounter &match : yieldSlots(pokemon, chain, state))
                {
                    co_yield match;
                }
                for (const MatchedEncounter &match : yieldStatics(pokemon, chain, state))
                {
                    co_yield match;
                }
            }
            else
            {
                for (const MatchedEncounter &match : yieldStatics(pokemon, chain, state))
                {
                    co_yield match;
                }
                for (const MatchedEncounter &match : yieldSlots(pokemon, chain, state))
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
