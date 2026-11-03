#include "Legality/Encounters/EncounterEnumerator8b.h"

#include <span>

#include "Enums/Ball.h"
#include "Legality/Encounters/EncounterData8b.h"
#include "Legality/Encounters/EncounterEgg8b.h"
#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterGenerator8b.h"
#include "Legality/Encounters/EncounterLocations.h"
#include "Legality/Encounters/EncounterLocations8.h"
#include "Legality/Encounters/EncounterRules9.h"
#include "Legality/Encounters/EncounterSlot8b.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            /// PKHeX EggStateLegality.EggMetLevel: a Gen 8 egg is met at level 1 when it hatches.
            constexpr uint8_t EGG_MET_LEVEL8 = 1;

            /// The enumerator's running state: PKHeX's Deferred / Rating / Yielded members, and the wild
            /// location its InitializeWildLocationInfo works out.
            struct EnumeratorState
            {
                EncounterReference deferred;
                EncounterMatchRating rating = EncounterMatchRating::MaxNotMatch;
                bool yielded = false;
                uint16_t metLocation = 0;
                bool hasOriginalLocation = true;
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

            /// PKHeX TryGetNext over a game's areas. The slot templates are built from the area's
            /// records, so a match is yielded as an owned copy.
            MatchedEncounters yieldSlotMatches(std::span<const EncounterArea8b> areas, const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionChain &chain, EnumeratorState &state)
            {
                for (const EncounterArea8b &area : areas)
                {
                    if (state.hasOriginalLocation && !area.isMatchLocation(state.metLocation))
                    {
                        continue;
                    }
                    for (uint16_t slotIndex = 0; slotIndex < area.slotCount(); slotIndex++)
                    {
                        const EncounterSlot8b slot = area.slotAt(slotIndex);
                        EncounterMatchRating rating = EncounterMatchRating::MaxNotMatch;
                        if (!isNextMatch(slot, pokemon, chain, rating))
                        {
                            continue;
                        }
                        if (rating == EncounterMatchRating::Match)
                        {
                            state.yielded = true;
                            co_yield MatchedEncounter{makeEncounter<EncounterSlot8b>(slot), EncounterMatchRating::Match};
                        }
                        else if (rating < state.rating)
                        {
                            state.deferred = makeEncounter<EncounterSlot8b>(slot);
                            state.rating = rating;
                        }
                    }
                }
            }

            /// PKHeX InitializeWildLocationInfo: a record HOME remapped to Sword/Shield may hold the
            /// remapped location instead of its own, and then every area of the game is a candidate.
            void initializeWildLocationInfo(const Pokemon::Pokemon &pokemon, Enums::GameVersion version, EnumeratorState &state)
            {
                state.metLocation = pokemon.metLocation();
                state.hasOriginalLocation = true;
                const LocationRemapState remap = getRemapState(Enums::GameVersion::BDSP, getEntityContextGroup(pokemon));
                if (remap == LocationRemapState::Remapped || remap == LocationRemapState::Either)
                {
                    state.hasOriginalLocation = state.metLocation != getMetSwordShield(state.metLocation, static_cast<uint8_t>(version));
                }
            }

            /// PKHeX YieldState.SlotStart .. SlotEnd.
            MatchedEncounters yieldSlots(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionChain &chain, Enums::GameVersion version, EnumeratorState &state)
            {
                if (!EncounterStateUtil::canBeWildEncounter(pokemon))
                {
                    co_return;
                }
                // PKHeX throws for any other version; nothing can match it.
                if (version != Enums::GameVersion::BD && version != Enums::GameVersion::SP)
                {
                    co_return;
                }
                for (const MatchedEncounter &match : yieldSlotMatches(version == Enums::GameVersion::BD ? getAreas8bBD() : getAreas8bSP(), pokemon, chain, state))
                {
                    co_yield match;
                }
            }

            /// PKHeX YieldState.StaticVersion .. StaticShared. `skipVersionStatics` reproduces the Sword/Shield
            /// enumerator, whose slot stage leaves its index past the end of the version table.
            MatchedEncounters yieldStatics(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionChain &chain, Enums::GameVersion version, EnumeratorState &state, bool skipVersionStatics)
            {
                if (version != Enums::GameVersion::BD && version != Enums::GameVersion::SP)
                {
                    co_return;
                }
                if (!skipVersionStatics)
                {
                    for (const MatchedEncounter &match : yieldMatches(version == Enums::GameVersion::BD ? getStatic8bBD() : getStatic8bSP(), pokemon, chain, state))
                    {
                        co_yield match;
                    }
                }
                for (const MatchedEncounter &match : yieldMatches(getStatic8bBDSP(), pokemon, chain, state))
                {
                    co_yield match;
                }
            }

            /// PKHeX YieldState.StartCaptures onward: a Safari Ball means a Great Marsh slot, so the
            /// slots go first; otherwise the statics come first. Ends with the fallback.
            MatchedEncounters yieldCaptures(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionChain &chain, Enums::GameVersion version, EnumeratorState &state, bool isSwordShieldEnumerator)
            {
                initializeWildLocationInfo(pokemon, version, state);
                const bool mustBeSlot = pokemon.ball() == static_cast<uint8_t>(Enums::Ball::Safari);
                if (mustBeSlot)
                {
                    for (const MatchedEncounter &match : yieldSlots(pokemon, chain, version, state))
                    {
                        co_yield match;
                    }
                    const bool skipVersionStatics = isSwordShieldEnumerator && EncounterStateUtil::canBeWildEncounter(pokemon);
                    for (const MatchedEncounter &match : yieldStatics(pokemon, chain, version, state, skipVersionStatics))
                    {
                        co_yield match;
                    }
                }
                else
                {
                    for (const MatchedEncounter &match : yieldStatics(pokemon, chain, version, state, false))
                    {
                        co_yield match;
                    }
                    for (const MatchedEncounter &match : yieldSlots(pokemon, chain, version, state))
                    {
                        co_yield match;
                    }
                }
                if (state.deferred)
                {
                    co_yield MatchedEncounter{state.deferred, state.rating};
                }
            }

            /// PKHeX YieldState.Trade: the first trade that matches ends the search.
            bool tryGetTrade(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionChain &chain, EnumeratorState &state, EncounterReference &trade)
            {
                for (const EncounterTrade8b &encounter : getTrade8bBDSP())
                {
                    EncounterMatchRating rating = EncounterMatchRating::MaxNotMatch;
                    if (!isNextMatch(encounter, pokemon, chain, rating))
                    {
                        continue;
                    }
                    if (rating == EncounterMatchRating::Match)
                    {
                        trade = borrowEncounter(encounter);
                        return true;
                    }
                    if (rating < state.rating)
                    {
                        state.deferred = borrowEncounter(encounter);
                        state.rating = rating;
                    }
                }
                return false;
            }

            /// PKHeX EncounterEnumerator8bSWSH.WasBredEggBDSP: HOME rewrites a BDSP egg's egg location.
            bool wasBredEggBrilliantDiamondShiningPearl(const Pokemon::Pokemon &pokemon)
            {
                if (pokemon.metLevel() != EGG_MET_LEVEL8)
                {
                    return false;
                }
                switch (pokemon.eggLocation())
                {
                case LOCATION_HOME_SWSH_EGG:
                    // The regular hatch location (not a link trade).
                    return true;
                case LOCATION_HOME_SWBD:
                    // A link-traded egg moved over must match its met location.
                    return pokemon.metLocation() == LOCATION_HOME_SWBD;
                case LOCATION_HOME_SHSP:
                    return pokemon.metLocation() == LOCATION_HOME_SHSP;
                default:
                    return false;
                }
            }
        }

        MatchedEncounters EncounterEnumerator8b::enumerate(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain, Enums::GameVersion version)
        {
            if (chain.isEmpty())
            {
                co_return;
            }
            EnumeratorState state;

            if (pokemon.isFatefulEncounter())
            {
                for (const MatchedEncounter &match : yieldMatches(getGifts8bWB8(), pokemon, chain, state))
                {
                    co_yield match;
                }
                // PKHeX EncounterEvent.EGDB_G8B (the user's own event folder) follows; PKSE has none.
                if (state.yielded)
                {
                    co_return;
                }
            }

            // PKHeX YieldState.Bred / BredSplit: a split-less egg ends the search, as does a Day Care egg.
            EncounterEgg8b egg(0, 0, version);
            if (isEggLocationBred8b(pokemon.eggLocation()) && EncounterGenerator8b::tryGetEgg(chain.view(), version, egg))
            {
                state.yielded = true;
                co_yield MatchedEncounter{makeEncounter<EncounterEgg8b>(egg), EncounterMatchRating::Match};
                EncounterEgg8b split(0, 0, version);
                if (!EncounterGenerator8b::tryGetSplit(egg, chain.view(), split))
                {
                    co_return;
                }
                co_yield MatchedEncounter{makeEncounter<EncounterEgg8b>(split), EncounterMatchRating::Match};
                if (pokemon.eggLocation() == LOCATION_DAYCARE8B)
                {
                    co_return;
                }
                for (const MatchedEncounter &match : yieldCaptures(pokemon, chain, version, state, false))
                {
                    co_yield match;
                }
                co_return;
            }

            // PKHeX YieldState.TradeStart.
            if (pokemon.metLocation() == LOCATION_LINK_TRADE6_NPC)
            {
                EncounterReference trade;
                if (tryGetTrade(pokemon, chain, state, trade))
                {
                    co_yield MatchedEncounter{trade, EncounterMatchRating::Match};
                    co_return;
                }
            }

            for (const MatchedEncounter &match : yieldCaptures(pokemon, chain, version, state, false))
            {
                co_yield match;
            }
        }

        MatchedEncounters EncounterEnumerator8b::enumerateSwordShield(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain, Enums::GameVersion version)
        {
            if (chain.isEmpty())
            {
                co_return;
            }
            EnumeratorState state;

            if (pokemon.isFatefulEncounter())
            {
                for (const MatchedEncounter &match : yieldMatches(getGifts8bWB8(), pokemon, chain, state))
                {
                    co_yield match;
                }
                if (state.yielded)
                {
                    co_return;
                }
            }

            // PKHeX YieldState.Bred / BredSplit: an egg with a split ends the search; without one the
            // trades and captures follow.
            EncounterEgg8b egg(0, 0, version);
            if (wasBredEggBrilliantDiamondShiningPearl(pokemon) && EncounterGenerator8b::tryGetEgg(chain.view(), version, egg))
            {
                state.yielded = true;
                co_yield MatchedEncounter{makeEncounter<EncounterEgg8b>(egg), EncounterMatchRating::Match};
                EncounterEgg8b split(0, 0, version);
                if (EncounterGenerator8b::tryGetSplit(egg, chain.view(), split))
                {
                    co_yield MatchedEncounter{makeEncounter<EncounterEgg8b>(split), EncounterMatchRating::Match};
                    co_return;
                }
            }

            // PKHeX YieldState.TradeStart goes straight to the trades: HOME rewrote the met location.
            EncounterReference trade;
            if (tryGetTrade(pokemon, chain, state, trade))
            {
                co_yield MatchedEncounter{trade, EncounterMatchRating::Match};
                co_return;
            }

            for (const MatchedEncounter &match : yieldCaptures(pokemon, chain, version, state, true))
            {
                co_yield match;
            }
        }
    }
}
