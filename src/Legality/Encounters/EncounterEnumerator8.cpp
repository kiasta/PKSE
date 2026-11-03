#include "Legality/Encounters/EncounterEnumerator8.h"

#include <span>

#include "Legality/Encounters/EncounterData8.h"
#include "Legality/Encounters/EncounterEgg8.h"
#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterGenerator8.h"
#include "Legality/Encounters/EncounterLocations.h"
#include "Legality/Encounters/EncounterLocations8.h"
#include "Legality/Encounters/EncounterRules8.h"
#include "Legality/Encounters/EncounterSlot8.h"
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

            /// PKHeX EncounterEnumerator8.TryGetNext over a template array, as a filter: true when
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

            MatchedEncounters yieldSlotMatches(std::span<const EncounterArea8> areas, const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionChain &chain, EnumeratorState &state, uint16_t metLocation)
            {
                for (const EncounterArea8 &area : areas)
                {
                    if (!area.isMatchLocation(metLocation))
                    {
                        continue;
                    }
                    for (uint16_t slotIndex = 0; slotIndex < area.slotCount(); slotIndex++)
                    {
                        const EncounterSlot8 &slot = area.slotAt(slotIndex);
                        if (isNextMatch(slot, pokemon, chain, state))
                        {
                            state.yielded = true;
                            co_yield MatchedEncounter{borrowEncounter(slot), EncounterMatchRating::Match};
                        }
                    }
                }
            }

            /// PKHeX EncounterStateUtil.CanBeWildEncounter with IsMetAsEgg's PK8/PA8 case (a
            /// Brilliant Diamond/Shining Pearl record with an egg date was met as an egg).
            bool canBeWildEncounter(const Pokemon::Pokemon &pokemon)
            {
                if (pokemon.isEgg())
                {
                    return false;
                }
                if (pokemon.getGameGroup() == Enums::GameVersion::BDSP)
                {
                    return pokemon.eggLocation() == LOCATION_DEFAULT8B_NONE;
                }
                if (pokemon.eggLocation() != 0)
                {
                    return false;
                }
                const uint8_t originVersion = getOriginVersion(pokemon);
                const bool isBDSPOrigin = originVersion == static_cast<uint8_t>(Enums::GameVersion::BD) || originVersion == static_cast<uint8_t>(Enums::GameVersion::SP);
                const bool isFormatHoldingEggDate = pokemon.getGameGroup() == Enums::GameVersion::SWSH || pokemon.getGameGroup() == Enums::GameVersion::PLA;
                return !(isFormatHoldingEggDate && isBDSPOrigin && pokemon.eggDay() != 0);
            }

            /// PKHeX YieldState.SlotStart .. SlotEnd.
            MatchedEncounters yieldSlots(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionChain &chain, Enums::GameVersion version, EnumeratorState &state, uint16_t metLocation)
            {
                if (!canBeWildEncounter(pokemon))
                {
                    co_return;
                }
                const bool isSword = version == Enums::GameVersion::SW;
                for (const MatchedEncounter &match : yieldSlotMatches(isSword ? getAreas8SWSymbol() : getAreas8SHSymbol(), pokemon, chain, state, metLocation))
                {
                    co_yield match;
                }
                for (const MatchedEncounter &match : yieldSlotMatches(isSword ? getAreas8SWHidden() : getAreas8SHHidden(), pokemon, chain, state, metLocation))
                {
                    co_yield match;
                }
            }

            /// PKHeX YieldState.StaticStart .. StaticShared.
            MatchedEncounters yieldStatics(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionChain &chain, Enums::GameVersion version, EnumeratorState &state)
            {
                for (const MatchedEncounter &match : yieldMatches(getRaids8NestSW(), pokemon, chain, state))
                {
                    co_yield match;
                }
                for (const MatchedEncounter &match : yieldMatches(getRaids8NestSH(), pokemon, chain, state))
                {
                    co_yield match;
                }
                for (const MatchedEncounter &match : yieldMatches(getRaids8DistributionSW(), pokemon, chain, state))
                {
                    co_yield match;
                }
                for (const MatchedEncounter &match : yieldMatches(getRaids8DistributionSH(), pokemon, chain, state))
                {
                    co_yield match;
                }
                for (const MatchedEncounter &match : yieldMatches(getRaids8MaxLair(), pokemon, chain, state))
                {
                    co_yield match;
                }
                for (const MatchedEncounter &match : yieldMatches(getRaids8Crystal(), pokemon, chain, state))
                {
                    co_yield match;
                }
                for (const MatchedEncounter &match : yieldMatches(version == Enums::GameVersion::SW ? getStatic8SW() : getStatic8SH(), pokemon, chain, state))
                {
                    co_yield match;
                }
                for (const MatchedEncounter &match : yieldMatches(getStatic8SWSH(), pokemon, chain, state))
                {
                    co_yield match;
                }
            }
        }

        MatchedEncounters EncounterEnumerator8::enumerate(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain, Enums::GameVersion version)
        {
            if (chain.isEmpty())
            {
                co_return;
            }
            const bool isSwordOrShield = version == Enums::GameVersion::SW || version == Enums::GameVersion::SH;
            EnumeratorState state;

            if (pokemon.metLocation() == LOCATION_LINK_TRADE6_NPC)
            {
                // PKHeX YieldState.TradeStart: a version other than Sword or Shield stops here.
                if (!isSwordOrShield)
                {
                    co_return;
                }
                for (const MatchedEncounter &match : yieldMatches(version == Enums::GameVersion::SW ? getTrade8SW() : getTrade8SH(), pokemon, chain, state))
                {
                    co_yield match;
                }
                for (const MatchedEncounter &match : yieldMatches(getTrade8SWSH(), pokemon, chain, state))
                {
                    co_yield match;
                }
                if (state.yielded)
                {
                    co_return;
                }
            }
            else
            {
                if (pokemon.isFatefulEncounter())
                {
                    for (const MatchedEncounter &match : yieldMatches(getGifts8WC8(), pokemon, chain, state))
                    {
                        co_yield match;
                    }
                    // PKHeX EncounterEvent.EGDB_G8 (the user's own event folder) follows; PKSE has none.
                    if (state.yielded)
                    {
                        co_return;
                    }
                }

                // PKHeX YieldState.Bred: a bred egg yields the egg and its split, and stops.
                EncounterEgg8 egg(0, 0, version);
                if (isEggLocationBred6(pokemon.eggLocation()) && EncounterGenerator8::tryGetEgg(chain.view(), version, egg))
                {
                    co_yield MatchedEncounter{makeEncounter<EncounterEgg8>(egg), EncounterMatchRating::Match};
                    EncounterEgg8 split(0, 0, version);
                    if (EncounterGenerator8::tryGetSplit(egg, chain.view(), split))
                    {
                        co_yield MatchedEncounter{makeEncounter<EncounterEgg8>(split), EncounterMatchRating::Match};
                    }
                    co_return;
                }
            }

            // PKHeX YieldState.StartCaptures: a record with an encounter mark was a wild slot, so its
            // slots go first and the statics follow; otherwise the statics come first.
            const uint16_t metLocation = pokemon.metLocation();
            const bool mustBeSlot = readRibbonMarks8(pokemon).hasEncounterMark;
            if (mustBeSlot)
            {
                for (const MatchedEncounter &match : yieldSlots(pokemon, chain, version, state, metLocation))
                {
                    co_yield match;
                }
                for (const MatchedEncounter &match : yieldStatics(pokemon, chain, version, state))
                {
                    co_yield match;
                }
            }
            else
            {
                for (const MatchedEncounter &match : yieldStatics(pokemon, chain, version, state))
                {
                    co_yield match;
                }
                for (const MatchedEncounter &match : yieldSlots(pokemon, chain, version, state, metLocation))
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
