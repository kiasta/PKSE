#include "Legality/Encounters/EncounterEnumerator5.h"

#include <span>

#include "Legality/Encounters/EncounterData5.h"
#include "Legality/Encounters/EncounterEgg5.h"
#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterGenerator5.h"
#include "Legality/Encounters/EncounterLocations5.h"
#include "Legality/Encounters/EncounterSlot5.h"
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
                bool hasYielded = false;
            };

            /// PKHeX EncounterEnumerator5.TryGetNext over a template array, as a filter: true when
            /// `encounter` is a full Match to yield now; a better partial match is kept as the fallback.
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
                        state.hasYielded = true;
                        co_yield MatchedEncounter{borrowEncounter(encounter), EncounterMatchRating::Match};
                    }
                }
            }

            std::span<const EncounterArea5> getVersionSlots(Enums::GameVersion version)
            {
                switch (version)
                {
                case Enums::GameVersion::B:
                    return getAreas5B();
                case Enums::GameVersion::W:
                    return getAreas5W();
                case Enums::GameVersion::B2:
                    return getAreas5B2();
                case Enums::GameVersion::W2:
                    return getAreas5W2();
                default:
                    return {};
                }
            }

            bool isBlackWhite(Enums::GameVersion version)
            {
                return version == Enums::GameVersion::B || version == Enums::GameVersion::W;
            }

            /// PKHeX YieldState.SlotStart .. SlotEnd.
            MatchedEncounters yieldSlots(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionChain &chain, Enums::GameVersion version, EnumeratorState &state)
            {
                if (!EncounterStateUtil::canBeWildEncounter(pokemon))
                {
                    co_return;
                }
                const uint16_t metLocation = pokemon.metLocation();
                for (const EncounterArea5 &area : getVersionSlots(version))
                {
                    if (!area.isMatchLocation(metLocation))
                    {
                        continue;
                    }
                    for (uint16_t slotIndex = 0; slotIndex < area.slotCount(); slotIndex++)
                    {
                        const EncounterSlot5 &slot = area.slotAt(slotIndex);
                        if (isNextMatch(slot, pokemon, chain, state))
                        {
                            state.hasYielded = true;
                            co_yield MatchedEncounter{borrowEncounter(slot), EncounterMatchRating::Match};
                        }
                    }
                }
            }

            /// PKHeX YieldState.StaticStart .. StaticEntreeShared.
            MatchedEncounters yieldStatics(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionChain &chain, Enums::GameVersion version, EnumeratorState &state)
            {
                std::span<const EncounterStatic5> versionStatics;
                switch (version)
                {
                case Enums::GameVersion::W:
                    versionStatics = getStatic5W();
                    break;
                case Enums::GameVersion::B:
                    versionStatics = getStatic5B();
                    break;
                case Enums::GameVersion::W2:
                    versionStatics = getStatic5W2();
                    break;
                case Enums::GameVersion::B2:
                    versionStatics = getStatic5B2();
                    break;
                default:
                    co_return;
                }
                for (const MatchedEncounter &match : yieldMatches(versionStatics, pokemon, chain, state))
                {
                    co_yield match;
                }
                if (isBlackWhite(version))
                {
                    for (const MatchedEncounter &match : yieldMatches(getStatic5BW(), pokemon, chain, state))
                    {
                        co_yield match;
                    }
                    for (const MatchedEncounter &match : yieldMatches(getDreamWorld5BW(), pokemon, chain, state))
                    {
                        co_yield match;
                    }
                }
                else
                {
                    for (const MatchedEncounter &match : yieldMatches(getStatic5B2W2(), pokemon, chain, state))
                    {
                        co_yield match;
                    }
                    for (const MatchedEncounter &match : yieldMatches(getStatic5N(), pokemon, chain, state))
                    {
                        co_yield match;
                    }
                    for (const MatchedEncounter &match : yieldMatches(getDreamWorld5B2W2(), pokemon, chain, state))
                    {
                        co_yield match;
                    }
                    for (const MatchedEncounter &match : yieldMatches(getDreamRadar5(), pokemon, chain, state))
                    {
                        co_yield match;
                    }
                }
                for (const MatchedEncounter &match : yieldMatches(getDreamWorld5Common(), pokemon, chain, state))
                {
                    co_yield match;
                }
            }

            /// PKHeX YieldState.StartCaptures onward: statics, then wild slots, then the fallback.
            MatchedEncounters yieldCaptures(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionChain &chain, Enums::GameVersion version, EnumeratorState &state)
            {
                for (const MatchedEncounter &match : yieldStatics(pokemon, chain, version, state))
                {
                    co_yield match;
                }
                for (const MatchedEncounter &match : yieldSlots(pokemon, chain, version, state))
                {
                    co_yield match;
                }
                if (state.deferred)
                {
                    co_yield MatchedEncounter{state.deferred, state.rating};
                }
            }

            /// PKHeX YieldState.TradeStart .. TradeB2W2. False when a trade matched, which ends the search.
            MatchedEncounters yieldTrades(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionChain &chain, Enums::GameVersion version, EnumeratorState &state)
            {
                if (isBlackWhite(version))
                {
                    const std::span<const EncounterTrade5BW> versionTrades = version == Enums::GameVersion::W ? getTradeGift5W() : getTradeGift5B();
                    for (const MatchedEncounter &match : yieldMatches(versionTrades, pokemon, chain, state))
                    {
                        co_yield match;
                    }
                    if (state.hasYielded)
                    {
                        co_return;
                    }
                    for (const MatchedEncounter &match : yieldMatches(getTradeGift5BW(), pokemon, chain, state))
                    {
                        co_yield match;
                    }
                    co_return;
                }
                const std::span<const EncounterTrade5B2W2> versionTrades = version == Enums::GameVersion::W2 ? getTradeGift5W2() : getTradeGift5B2();
                for (const MatchedEncounter &match : yieldMatches(versionTrades, pokemon, chain, state))
                {
                    co_yield match;
                }
                if (state.hasYielded)
                {
                    co_return;
                }
                for (const MatchedEncounter &match : yieldMatches(getTradeGift5B2W2(), pokemon, chain, state))
                {
                    co_yield match;
                }
            }

            bool isGen5Version(Enums::GameVersion version)
            {
                return version == Enums::GameVersion::B || version == Enums::GameVersion::W || version == Enums::GameVersion::B2 || version == Enums::GameVersion::W2;
            }
        }

        MatchedEncounters EncounterEnumerator5::enumerate(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain, Enums::GameVersion version)
        {
            if (chain.isEmpty())
            {
                co_return;
            }
            // PKHeX throws ArgumentOutOfRangeException on a version outside Gen 5: no match.
            if (!isGen5Version(version))
            {
                co_return;
            }
            EnumeratorState state;

            // An in-game trade is searched alone: a match ends the search, no match falls through to captures.
            if (pokemon.metLocation() == LOCATION_LINK_TRADE5_NPC && !pokemon.isEgg())
            {
                for (const MatchedEncounter &match : yieldTrades(pokemon, chain, version, state))
                {
                    co_yield match;
                }
                if (state.hasYielded)
                {
                    co_return;
                }
                for (const MatchedEncounter &match : yieldCaptures(pokemon, chain, version, state))
                {
                    co_yield match;
                }
                co_return;
            }

            // Events. A fateful encounter that matched any gift is not searched further, and the
            // best partial gift is not offered either.
            if (pokemon.isFatefulEncounter())
            {
                for (const MatchedEncounter &match : yieldMatches(getMysteryGifts5(), pokemon, chain, state))
                {
                    co_yield match;
                }
                // PKHeX EncounterEvent.EGDB_G5 (the user's local gift folder) follows here; PKSE has none.
                if (state.hasYielded)
                {
                    co_return;
                }
            }

            // Bred
            if (isEggLocationBred5(pokemon.eggLocation()))
            {
                EncounterEgg5 egg(0, 0, version);
                if (EncounterGenerator5::tryGetEgg(chain.view(), version, egg))
                {
                    state.hasYielded = true;
                    co_yield MatchedEncounter{makeEncounter<EncounterEgg5>(egg), EncounterMatchRating::Match};
                    const bool isDaycare = pokemon.eggLocation() == LOCATION_DAYCARE5;
                    EncounterEgg5 split(0, 0, version);
                    if (EncounterGenerator5::tryGetSplit(egg, chain.view(), split))
                    {
                        co_yield MatchedEncounter{makeEncounter<EncounterEgg5>(split), EncounterMatchRating::Match};
                    }
                    // A Day Care egg has no other origin; PKHeX ends without the fallback.
                    if (isDaycare)
                    {
                        co_return;
                    }
                }
            }

            for (const MatchedEncounter &match : yieldCaptures(pokemon, chain, version, state))
            {
                co_yield match;
            }
        }
    }
}
