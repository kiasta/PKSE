#include "Legality/Encounters/EncounterEnumerator4.h"

#include <span>

#include "Legality/Encounters/EncounterData4.h"
#include "Legality/Encounters/EncounterEgg4.h"
#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterGenerator4.h"
#include "Legality/Encounters/EncounterLocations4.h"
#include "Legality/Encounters/EncounterSlot4.h"
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

            /// PKHeX EncounterEnumerator4.TryGetNext over a template array, as a filter: true when
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

            std::span<const EncounterArea4> getVersionSlots(Enums::GameVersion version)
            {
                switch (version)
                {
                case Enums::GameVersion::D:
                    return getAreas4D();
                case Enums::GameVersion::P:
                    return getAreas4P();
                case Enums::GameVersion::Pt:
                    return getAreas4Pt();
                case Enums::GameVersion::HG:
                    return getAreas4HG();
                case Enums::GameVersion::SS:
                    return getAreas4SS();
                default:
                    return {};
                }
            }

            bool isGen4Version(Enums::GameVersion version)
            {
                return version == Enums::GameVersion::D || version == Enums::GameVersion::P || version == Enums::GameVersion::Pt || version == Enums::GameVersion::HG || version == Enums::GameVersion::SS;
            }

            /// PKHeX EncounterStateUtil.CanBeWildEncounter.
            bool canBeWildEncounter(const Pokemon::Pokemon &pokemon)
            {
                if (pokemon.isEgg())
                {
                    return false;
                }
                // Brilliant Diamond/Shining Pearl write "none" as 65535, not 0.
                const uint16_t noEggLocation = pokemon.getGameGroup() == Enums::GameVersion::BDSP ? LOCATION_DEFAULT8B_NONE : 0;
                return pokemon.eggLocation() == noEggLocation;
            }

            /// PKHeX YieldState.SlotStart .. SlotEnd.
            MatchedEncounters yieldSlots(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionChain &chain, Enums::GameVersion version, EnumeratorState &state, bool hasOriginalLocation, uint16_t metLocation)
            {
                if (!canBeWildEncounter(pokemon))
                {
                    co_return;
                }
                for (const EncounterArea4 &area : getVersionSlots(version))
                {
                    if (hasOriginalLocation && !area.isMatchLocation(metLocation))
                    {
                        continue;
                    }
                    for (uint16_t slotIndex = 0; slotIndex < area.slotCount(); slotIndex++)
                    {
                        const EncounterSlot4 &slot = area.slotAt(slotIndex);
                        if (isNextMatch(slot, pokemon, chain, state))
                        {
                            state.hasYielded = true;
                            co_yield MatchedEncounter{borrowEncounter(slot), EncounterMatchRating::Match};
                        }
                    }
                }
            }

            /// PKHeX YieldState.StaticStart .. StaticEnd.
            MatchedEncounters yieldStatics(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionChain &chain, Enums::GameVersion version, EnumeratorState &state)
            {
                std::span<const EncounterStatic4> versionStatics;
                std::span<const EncounterStatic4> sharedStatics;
                std::span<const EncounterStatic4> generationStatics;
                switch (version)
                {
                case Enums::GameVersion::D:
                    versionStatics = getStatic4D();
                    sharedStatics = getStatic4DP();
                    generationStatics = getStatic4DPPt();
                    break;
                case Enums::GameVersion::P:
                    versionStatics = getStatic4P();
                    sharedStatics = getStatic4DP();
                    generationStatics = getStatic4DPPt();
                    break;
                case Enums::GameVersion::Pt:
                    versionStatics = getStatic4Pt();
                    generationStatics = getStatic4DPPt();
                    break;
                case Enums::GameVersion::HG:
                    versionStatics = getStatic4HG();
                    sharedStatics = getStatic4HGSS();
                    break;
                case Enums::GameVersion::SS:
                    versionStatics = getStatic4SS();
                    sharedStatics = getStatic4HGSS();
                    break;
                default:
                    co_return;
                }
                for (const std::span<const EncounterStatic4> statics : {versionStatics, sharedStatics, generationStatics})
                {
                    for (const MatchedEncounter &match : yieldMatches(statics, pokemon, chain, state))
                    {
                        co_yield match;
                    }
                }
                if (version == Enums::GameVersion::HG || version == Enums::GameVersion::SS)
                {
                    for (const MatchedEncounter &match : yieldMatches(getPokewalker4(), pokemon, chain, state))
                    {
                        co_yield match;
                    }
                }
            }
        }

        MatchedEncounters EncounterEnumerator4::enumerate(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain, Enums::GameVersion version)
        {
            if (chain.isEmpty())
            {
                co_return;
            }
            EnumeratorState state;

            // Events. A fateful encounter that matched any gift is not searched further, and the
            // best partial gift is not offered either.
            if (pokemon.isFatefulEncounter())
            {
                if (MysteryGiftPGT::isRangerManaphy(pokemon))
                {
                    co_yield MatchedEncounter{makeEncounter<MysteryGiftPGT>(), EncounterMatchRating::Match};
                    co_return;
                }
                for (const MatchedEncounter &match : yieldMatches(getMysteryGifts4(), pokemon, chain, state))
                {
                    co_yield match;
                }
                // PKHeX EncounterEvent.EGDB_G4 (the user's local gift folder) follows here; PKSE has none.
                if (state.hasYielded)
                {
                    co_return;
                }
            }

            // Bred
            if (isEggLocationBred4(pokemon.eggLocation(), static_cast<uint8_t>(version)))
            {
                EncounterEgg4 egg(0, version);
                if (EncounterGenerator4::tryGetEgg(chain.view(), version, egg))
                {
                    state.hasYielded = true;
                    co_yield MatchedEncounter{makeEncounter<EncounterEgg4>(egg), EncounterMatchRating::Match};
                    EncounterEgg4 split(0, version);
                    if (EncounterGenerator4::tryGetSplit(egg, chain.view(), split))
                    {
                        co_yield MatchedEncounter{makeEncounter<EncounterEgg4>(split), EncounterMatchRating::Match};
                    }
                }
            }

            // Trades
            if (version == Enums::GameVersion::HG || version == Enums::GameVersion::SS)
            {
                for (const MatchedEncounter &match : yieldMatches(getTradeGift4HGSS(), pokemon, chain, state))
                {
                    co_yield match;
                }
            }
            else
            {
                if (version == Enums::GameVersion::D)
                {
                    for (const MatchedEncounter &match : yieldMatches(getRanchGifts4(), pokemon, chain, state))
                    {
                        co_yield match;
                    }
                }
                for (const MatchedEncounter &match : yieldMatches(getTradeGift4DPPt(), pokemon, chain, state))
                {
                    co_yield match;
                }
            }

            if (!isGen4Version(version))
            {
                co_return;
            }

            // Captures. A Safari or Sport Ball is never a static encounter, so its slots go first,
            // the statics after them are a courtesy to a bad ball, and no fallback follows.
            const uint16_t metLocation = pokemon.metLocation();
            const uint8_t ball = pokemon.ball();
            const bool mustBeSlot = pokemon.eggLocation() == 0 && (ball == static_cast<uint8_t>(Enums::Ball::Sport) || ball == static_cast<uint8_t>(Enums::Ball::Safari));
            const bool hasOriginalLocation = getFormatGeneration(pokemon) == 4;
            if (mustBeSlot)
            {
                for (const MatchedEncounter &match : yieldSlots(pokemon, chain, version, state, hasOriginalLocation, metLocation))
                {
                    co_yield match;
                }
                for (const MatchedEncounter &match : yieldStatics(pokemon, chain, version, state))
                {
                    co_yield match;
                }
                co_return;
            }
            for (const MatchedEncounter &match : yieldStatics(pokemon, chain, version, state))
            {
                co_yield match;
            }
            for (const MatchedEncounter &match : yieldSlots(pokemon, chain, version, state, hasOriginalLocation, metLocation))
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
