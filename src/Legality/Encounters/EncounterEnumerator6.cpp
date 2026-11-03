#include "Legality/Encounters/EncounterEnumerator6.h"

#include "Legality/Encounters/EncounterData6.h"
#include "Legality/Encounters/EncounterEgg6.h"
#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterEnumeratorSearch.h"
#include "Legality/Encounters/EncounterGenerator6.h"
#include "Legality/Encounters/EncounterLocations.h"
#include "Legality/Encounters/EncounterLocations67.h"
#include "Legality/Encounters/EncounterLocations8.h"
#include "Legality/Encounters/EncounterSlot6AO.h"
#include "Legality/Encounters/EncounterSlot6XY.h"
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

            constexpr uint16_t SPECIES_TOGEPI = 175;
            constexpr uint16_t SPECIES_WYNAUT = 360;

            bool isVersionXY(Enums::GameVersion version) noexcept
            {
                return version == Enums::GameVersion::X || version == Enums::GameVersion::Y;
            }

            bool isVersionAO(Enums::GameVersion version) noexcept
            {
                return version == Enums::GameVersion::AS || version == Enums::GameVersion::OR;
            }

            /// PKHeX YieldState.StartCaptures: the version's statics, its pair's shared statics, then
            /// (PKHeX YieldState.SlotStart .. SlotEnd) the wild slots.
            MatchedEncounters yieldCaptures(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionChain &chain, Enums::GameVersion version, EnumeratorState &state)
            {
                std::span<const EncounterStatic6> versionStatics;
                std::span<const EncounterStatic6> sharedStatics;
                switch (version)
                {
                case Enums::GameVersion::AS:
                    versionStatics = getStatic6AS();
                    sharedStatics = getStatic6AO();
                    break;
                case Enums::GameVersion::OR:
                    versionStatics = getStatic6OR();
                    sharedStatics = getStatic6AO();
                    break;
                case Enums::GameVersion::X:
                    versionStatics = getStatic6X();
                    sharedStatics = getStatic6XY();
                    break;
                case Enums::GameVersion::Y:
                    versionStatics = getStatic6Y();
                    sharedStatics = getStatic6XY();
                    break;
                default:
                    // PKHeX throws for a version outside the four games.
                    co_return;
                }
                for (const std::span<const EncounterStatic6> statics : {versionStatics, sharedStatics})
                {
                    for (const MatchedEncounter &match : yieldMatches(statics, pokemon, chain, state))
                    {
                        co_yield match;
                    }
                }

                if (!EncounterStateUtil::canBeWildEncounter(pokemon))
                {
                    co_return;
                }
                const uint16_t met = pokemon.metLocation();
                if (isVersionAO(version))
                {
                    const std::span<const EncounterArea6AO> areas = version == Enums::GameVersion::AS ? getAreas6AS() : getAreas6OR();
                    for (const MatchedEncounter &match : yieldAreaMatches(areas, met, pokemon, chain, state))
                    {
                        co_yield match;
                    }
                }
                else
                {
                    const std::span<const EncounterArea6XY> areas = version == Enums::GameVersion::X ? getAreas6X() : getAreas6Y();
                    for (const MatchedEncounter &match : yieldAreaMatches(areas, met, pokemon, chain, state))
                    {
                        co_yield match;
                    }
                }
            }
        }

        MatchedEncounters EncounterEnumerator6::enumerate(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain, Enums::GameVersion version)
        {
            if (chain.isEmpty())
            {
                co_return;
            }
            EnumeratorState state;

            if (pokemon.metLocation() == LOCATION_LINK_TRADE6_NPC)
            {
                // PKHeX YieldState.TradeStart
                std::span<const EncounterTrade6> trades;
                if (isVersionXY(version))
                {
                    trades = getTradeGift6XY();
                }
                else if (isVersionAO(version))
                {
                    trades = getTradeGift6AO();
                }
                else
                {
                    co_return;
                }
                for (const MatchedEncounter &match : yieldMatches(trades, pokemon, chain, state))
                {
                    co_yield match;
                }
                if (state.hasYielded)
                {
                    co_return;
                }
            }
            else
            {
                if (pokemon.isFatefulEncounter() || pokemon.metLocation() == LOCATION_LINK_GIFT6)
                {
                    for (const MatchedEncounter &match : yieldMatches(getMysteryGifts6(), pokemon, chain, state))
                    {
                        co_yield match;
                    }
                    // PKHeX EncounterEvent.EGDB_G6 (the user's own event folder) follows; PKSE has none.
                    if (state.hasYielded)
                    {
                        co_return;
                    }
                }

                // PKHeX YieldState.Bred .. BredSplitTrade
                if (isEggLocationBred6(pokemon.eggLocation()))
                {
                    EncounterEgg6 egg(0, 0, version);
                    if (!EncounterGenerator6::tryGetEgg(chain.view(), version, egg))
                    {
                        co_return;
                    }
                    state.hasYielded = true;
                    co_yield MatchedEncounter{makeEncounter<EncounterEgg6>(egg), EncounterMatchRating::Match};
                    const bool isTradedEgg = pokemon.eggLocation() == LOCATION_LINK_TRADE6;
                    // PKHeX's Current: the split is built from the last egg yielded.
                    EncounterEgg6 current = egg;
                    if (isTradedEgg)
                    {
                        current = EncounterGenerator6::mutateEggTrade(egg);
                        co_yield MatchedEncounter{makeEncounter<EncounterEgg6>(current), EncounterMatchRating::Match};
                    }
                    const uint16_t lowestSpeciesId = chain.view().back().speciesId;
                    if (lowestSpeciesId != SPECIES_TOGEPI && lowestSpeciesId != SPECIES_WYNAUT)
                    {
                        EncounterEgg6 split(0, 0, version);
                        if (!EncounterGenerator6::tryGetSplit(current, chain.view(), split))
                        {
                            co_return;
                        }
                        co_yield MatchedEncounter{makeEncounter<EncounterEgg6>(split), EncounterMatchRating::Match};
                        if (isTradedEgg)
                        {
                            co_yield MatchedEncounter{makeEncounter<EncounterEgg6>(EncounterGenerator6::mutateEggTrade(split)), EncounterMatchRating::Match};
                        }
                    }
                }
            }

            for (const MatchedEncounter &match : yieldCaptures(pokemon, chain, version, state))
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
