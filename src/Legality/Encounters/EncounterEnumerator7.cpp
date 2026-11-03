#include "Legality/Encounters/EncounterEnumerator7.h"

#include "Legality/Encounters/EncounterData7.h"
#include "Legality/Encounters/EncounterEgg7.h"
#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterEnumeratorSearch.h"
#include "Legality/Encounters/EncounterGenerator7.h"
#include "Legality/Encounters/EncounterLocations.h"
#include "Legality/Encounters/EncounterLocations8.h"
#include "Legality/Encounters/EncounterSlot7.h"
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

            constexpr uint16_t SPECIES_EEVEE = 133;

            bool isUltraVersion(Enums::GameVersion version) noexcept
            {
                return version == Enums::GameVersion::US || version == Enums::GameVersion::UM;
            }

            bool isSunMoonVersion(Enums::GameVersion version) noexcept
            {
                return version == Enums::GameVersion::SN || version == Enums::GameVersion::MN;
            }

            std::span<const EncounterArea7> getVersionSlots(Enums::GameVersion version)
            {
                switch (version)
                {
                case Enums::GameVersion::SN:
                    return getAreas7SN();
                case Enums::GameVersion::MN:
                    return getAreas7MN();
                case Enums::GameVersion::US:
                    return getAreas7US();
                case Enums::GameVersion::UM:
                    return getAreas7UM();
                default:
                    return {};
                }
            }

            /// PKHeX YieldState.SlotStart .. SlotEnd.
            MatchedEncounters yieldSlots(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionChain &chain, Enums::GameVersion version, EnumeratorState &state)
            {
                if (!EncounterStateUtil::canBeWildEncounter(pokemon))
                {
                    co_return;
                }
                for (const MatchedEncounter &match : yieldAreaMatches(getVersionSlots(version), pokemon.metLocation(), pokemon, chain, state))
                {
                    co_yield match;
                }
            }

            /// PKHeX YieldState.StaticStart .. StaticSharedSM: the version's own statics, then its pair's.
            MatchedEncounters yieldStatics(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionChain &chain, Enums::GameVersion version, EnumeratorState &state)
            {
                std::span<const EncounterStatic7> versionStatics;
                std::span<const EncounterStatic7> sharedStatics;
                switch (version)
                {
                case Enums::GameVersion::US:
                    versionStatics = getStatic7US();
                    sharedStatics = getStatic7USUM();
                    break;
                case Enums::GameVersion::UM:
                    versionStatics = getStatic7UM();
                    sharedStatics = getStatic7USUM();
                    break;
                case Enums::GameVersion::SN:
                    versionStatics = getStatic7SN();
                    sharedStatics = getStatic7SM();
                    break;
                case Enums::GameVersion::MN:
                    versionStatics = getStatic7MN();
                    sharedStatics = getStatic7SM();
                    break;
                default:
                    co_return;
                }
                for (const std::span<const EncounterStatic7> statics : {versionStatics, sharedStatics})
                {
                    for (const MatchedEncounter &match : yieldMatches(statics, pokemon, chain, state))
                    {
                        co_yield match;
                    }
                }
            }
        }

        MatchedEncounters EncounterEnumerator7::enumerate(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain, Enums::GameVersion version)
        {
            if (chain.isEmpty())
            {
                co_return;
            }
            EnumeratorState state;
            // PKHeX YieldState.StartCaptures runs the statics and then the slots; the Eevee egg path
            // re-enters at the shared Ultra Sun/Ultra Moon statics instead.
            bool isEeveeEgg = false;

            if (pokemon.metLocation() == LOCATION_LINK_TRADE6_NPC)
            {
                // PKHeX YieldState.TradeStart
                if (isSunMoonVersion(version))
                {
                    for (const MatchedEncounter &match : yieldMatches(getTradeGift7SM(), pokemon, chain, state))
                    {
                        co_yield match;
                    }
                }
                else if (isUltraVersion(version))
                {
                    for (const MatchedEncounter &match : yieldMatches(getTradeGift7USUM(), pokemon, chain, state))
                    {
                        co_yield match;
                    }
                }
                else
                {
                    co_return;
                }
                if (state.hasYielded)
                {
                    co_return;
                }
            }
            else
            {
                if (pokemon.isFatefulEncounter())
                {
                    for (const MatchedEncounter &match : yieldMatches(getMysteryGifts7(), pokemon, chain, state))
                    {
                        co_yield match;
                    }
                    // PKHeX EncounterEvent.EGDB_G7 (the user's own event folder) follows; PKSE has none.
                    if (state.hasYielded)
                    {
                        co_return;
                    }
                }

                // PKHeX YieldState.Bred .. BredSplitTrade: an egg ends the search, except Eevee's.
                EncounterEgg7 egg(0, 0, version);
                if (isEggLocationBred6(pokemon.eggLocation()) && EncounterGenerator7::tryGetEgg(chain.view(), version, egg))
                {
                    state.hasYielded = true;
                    co_yield MatchedEncounter{makeEncounter<EncounterEgg7>(egg), EncounterMatchRating::Match};
                    const bool isTradedEgg = pokemon.eggLocation() == LOCATION_LINK_TRADE6;
                    // PKHeX's Current: the split is built from the last egg yielded.
                    EncounterEgg7 current = egg;
                    if (isTradedEgg)
                    {
                        current = EncounterGenerator7::mutateEggTrade(egg);
                        co_yield MatchedEncounter{makeEncounter<EncounterEgg7>(current), EncounterMatchRating::Match};
                    }
                    if (chain.view().back().speciesId == SPECIES_EEVEE)
                    {
                        isEeveeEgg = true;
                    }
                    else
                    {
                        EncounterEgg7 split(0, 0, version);
                        if (!EncounterGenerator7::tryGetSplit(current, chain.view(), split))
                        {
                            co_return;
                        }
                        co_yield MatchedEncounter{makeEncounter<EncounterEgg7>(split), EncounterMatchRating::Match};
                        if (isTradedEgg)
                        {
                            co_yield MatchedEncounter{makeEncounter<EncounterEgg7>(EncounterGenerator7::mutateEggTrade(split)), EncounterMatchRating::Match};
                        }
                        co_return;
                    }
                }
            }

            if (isEeveeEgg)
            {
                for (const MatchedEncounter &match : yieldMatches(getStatic7USUM(), pokemon, chain, state))
                {
                    co_yield match;
                }
            }
            else
            {
                if (!isSunMoonVersion(version) && !isUltraVersion(version))
                {
                    // PKHeX throws for a version outside the four games.
                    co_return;
                }
                for (const MatchedEncounter &match : yieldStatics(pokemon, chain, version, state))
                {
                    co_yield match;
                }
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
    }
}
