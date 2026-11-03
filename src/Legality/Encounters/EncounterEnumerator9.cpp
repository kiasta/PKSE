#include "Legality/Encounters/EncounterEnumerator9.h"

#include <span>

#include "Legality/Encounters/EncounterData9.h"
#include "Legality/Encounters/EncounterEgg9.h"
#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterGenerator9.h"
#include "Legality/Encounters/EncounterLocations.h"
#include "Legality/Encounters/EncounterLocations8.h"
#include "Legality/Encounters/EncounterRules9.h"
#include "Legality/Encounters/EncounterSlot9.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            /// PKHeX EggStateLegality.EggMetLevel: the met level a hatched Gen 8+ egg records.
            constexpr uint8_t EGG_MET_LEVEL = 1;

            /// The enumerator's running state: PKHeX's Deferred / Rating / Yielded members.
            struct EnumeratorState
            {
                EncounterReference deferred;
                EncounterMatchRating rating = EncounterMatchRating::MaxNotMatch;
                bool yielded = false;
            };

            /// PKHeX EncounterEnumerator9.TryGetNext over a template array, as a filter: true when
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
                        state.yielded = true;
                        co_yield MatchedEncounter{borrowEncounter(encounter), EncounterMatchRating::Match};
                    }
                }
            }

            /// `isLocationFiltered` is false for a Sword/Shield record, which kept no Paldea location.
            MatchedEncounters yieldSlotMatches(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionChain &chain, EnumeratorState &state, bool isLocationFiltered)
            {
                const uint16_t metLocation = pokemon.metLocation();
                for (const EncounterArea9 &area : getAreas9SV())
                {
                    if (isLocationFiltered && !area.isMatchLocation(metLocation))
                    {
                        continue;
                    }
                    for (uint16_t slotIndex = 0; slotIndex < area.slotCount(); slotIndex++)
                    {
                        const EncounterSlot9 &slot = area.slotAt(slotIndex);
                        if (isNextMatch(slot, pokemon, chain, state))
                        {
                            state.yielded = true;
                            co_yield MatchedEncounter{borrowEncounter(slot), EncounterMatchRating::Match};
                        }
                    }
                }
            }

            /// PKHeX YieldState.SlotStart .. SlotEnd. The Scarlet/Violet enumerator checks the
            /// outbreaks with the slots; the Sword/Shield one checks them with the statics.
            MatchedEncounters yieldSlots(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionChain &chain, EnumeratorState &state, bool isSwordShieldRecord)
            {
                if (!EncounterStateUtil::canBeWildEncounter(pokemon))
                {
                    co_return;
                }
                for (const MatchedEncounter &match : yieldSlotMatches(pokemon, chain, state, !isSwordShieldRecord))
                {
                    co_yield match;
                }
                if (isSwordShieldRecord)
                {
                    co_return;
                }
                for (const MatchedEncounter &match : yieldMatches(getOutbreak9(), pokemon, chain, state))
                {
                    co_yield match;
                }
            }

            /// PKHeX YieldState.StaticShared .. StaticMight.
            MatchedEncounters yieldSharedStatics(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionChain &chain, EnumeratorState &state, bool isSwordShieldRecord)
            {
                for (const MatchedEncounter &match : yieldMatches(getStatic9SV(), pokemon, chain, state))
                {
                    co_yield match;
                }
                for (const MatchedEncounter &match : yieldMatches(getFixed9(), pokemon, chain, state))
                {
                    co_yield match;
                }
                for (const MatchedEncounter &match : yieldMatches(getTera9Base(), pokemon, chain, state))
                {
                    co_yield match;
                }
                for (const MatchedEncounter &match : yieldMatches(getTera9Kitakami(), pokemon, chain, state))
                {
                    co_yield match;
                }
                for (const MatchedEncounter &match : yieldMatches(getTera9Blueberry(), pokemon, chain, state))
                {
                    co_yield match;
                }
                for (const MatchedEncounter &match : yieldMatches(getDist9(), pokemon, chain, state))
                {
                    co_yield match;
                }
                if (isSwordShieldRecord)
                {
                    for (const MatchedEncounter &match : yieldMatches(getOutbreak9(), pokemon, chain, state))
                    {
                        co_yield match;
                    }
                }
                for (const MatchedEncounter &match : yieldMatches(getMight9(), pokemon, chain, state))
                {
                    co_yield match;
                }
            }

            /// PKHeX YieldState.StaticVersion: only a Scarlet or Violet origin has statics to check;
            /// any other version goes straight to the fallback.
            MatchedEncounters yieldStatics(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionChain &chain, Enums::GameVersion version, EnumeratorState &state, bool isSwordShieldRecord)
            {
                if (version != Enums::GameVersion::SL && version != Enums::GameVersion::VL)
                {
                    co_return;
                }
                for (const MatchedEncounter &match : yieldMatches(version == Enums::GameVersion::SL ? getStatic9SL() : getStatic9VL(), pokemon, chain, state))
                {
                    co_yield match;
                }
                for (const MatchedEncounter &match : yieldSharedStatics(pokemon, chain, state, isSwordShieldRecord))
                {
                    co_yield match;
                }
            }

            /// PKHeX YieldState.StartCaptures onward: a record with an encounter mark was a wild slot,
            /// so its slots go first and the statics follow; otherwise the statics come first. A
            /// version with no statics skips the slots too when they would come second.
            MatchedEncounters yieldCaptures(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionChain &chain, Enums::GameVersion version, EnumeratorState &state, bool isSwordShieldRecord)
            {
                const bool mustBeSlot = readRibbonMarks9(pokemon).hasEncounterMark;
                if (mustBeSlot)
                {
                    for (const MatchedEncounter &match : yieldSlots(pokemon, chain, state, isSwordShieldRecord))
                    {
                        co_yield match;
                    }
                    for (const MatchedEncounter &match : yieldStatics(pokemon, chain, version, state, isSwordShieldRecord))
                    {
                        co_yield match;
                    }
                    co_return;
                }
                if (version != Enums::GameVersion::SL && version != Enums::GameVersion::VL)
                {
                    co_return;
                }
                for (const MatchedEncounter &match : yieldStatics(pokemon, chain, version, state, isSwordShieldRecord))
                {
                    co_yield match;
                }
                for (const MatchedEncounter &match : yieldSlots(pokemon, chain, state, isSwordShieldRecord))
                {
                    co_yield match;
                }
            }

            // PKHeX EncounterEnumerator9SWSH.WasBredEggSWSH
            bool wasBredEggSwordShield(const Pokemon::Pokemon &pokemon)
            {
                if (pokemon.metLevel() != EGG_MET_LEVEL)
                {
                    return false;
                }
                const uint16_t eggLocation = pokemon.eggLocation();
                if (eggLocation == LOCATION_HOME_SWSH_EGG)
                {
                    return true;
                }
                if (eggLocation == LOCATION_HOME_SWSL || eggLocation == LOCATION_HOME_SHVL)
                {
                    return pokemon.metLocation() == eggLocation;
                }
                return false;
            }
        }

        MatchedEncounters EncounterEnumerator9::enumerate(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain, Enums::GameVersion version)
        {
            if (chain.isEmpty())
            {
                co_return;
            }
            EnumeratorState state;

            if (pokemon.isFatefulEncounter())
            {
                for (const MatchedEncounter &match : yieldMatches(getGifts9WC9(), pokemon, chain, state))
                {
                    co_yield match;
                }
                // PKHeX EncounterEvent.EGDB_G9 (the user's own event folder) follows; PKSE has none.
                if (state.yielded)
                {
                    co_return;
                }
            }

            // PKHeX YieldState.Bred: the egg is yielded and the search goes on.
            EncounterEgg9 egg(0, 0, version);
            if (isEggLocationBred9(pokemon.eggLocation()) && EncounterGenerator9::tryGetEgg(pokemon, chain.view(), version, egg))
            {
                state.yielded = true;
                co_yield MatchedEncounter{makeEncounter<EncounterEgg9>(egg), EncounterMatchRating::Match};
            }
            if (pokemon.isEgg())
            {
                // An unhatched egg goes straight to the shared statics (the gift eggs), then the
                // slots, which an egg never matches.
                for (const MatchedEncounter &match : yieldSharedStatics(pokemon, chain, state, false))
                {
                    co_yield match;
                }
            }
            else
            {
                bool isTradeDone = false;
                if (pokemon.metLocation() == LOCATION_LINK_TRADE6_NPC)
                {
                    for (const MatchedEncounter &match : yieldMatches(getTrade9SV(), pokemon, chain, state))
                    {
                        co_yield match;
                    }
                    isTradeDone = state.yielded;
                }
                if (isTradeDone)
                {
                    co_return;
                }
                for (const MatchedEncounter &match : yieldCaptures(pokemon, chain, version, state, false))
                {
                    co_yield match;
                }
            }

            if (state.deferred)
            {
                co_yield MatchedEncounter{state.deferred, state.rating};
            }
        }

        MatchedEncounters EncounterEnumerator9::enumerateSWSH(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain, Enums::GameVersion version)
        {
            if (chain.isEmpty())
            {
                co_return;
            }
            EnumeratorState state;

            if (pokemon.isFatefulEncounter())
            {
                for (const MatchedEncounter &match : yieldMatches(getGifts9WC9(), pokemon, chain, state))
                {
                    co_yield match;
                }
                if (state.yielded)
                {
                    co_return;
                }
            }

            EncounterEgg9 egg(0, 0, version);
            if (wasBredEggSwordShield(pokemon) && EncounterGenerator9::tryGetEgg(pokemon, chain.view(), version, egg))
            {
                state.yielded = true;
                co_yield MatchedEncounter{makeEncounter<EncounterEgg9>(egg), EncounterMatchRating::Match};
            }

            // PKHeX skips the trades here: HOME rewrote the met location they would be found by.
            for (const MatchedEncounter &match : yieldCaptures(pokemon, chain, version, state, true))
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
