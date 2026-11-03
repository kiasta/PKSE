#include "Legality/Encounters/EncounterEnumerator3.h"

#include <span>

#include "Enums/LanguageID.h"
#include "Legality/Encounters/EncounterData3.h"
#include "Legality/Encounters/EncounterEgg3.h"
#include "Legality/Encounters/EncounterEntity.h"
#include "Legality/Encounters/EncounterGenerator3.h"
#include "Legality/Encounters/EncounterSlot3.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            /// The enumerator's running state: PKHeX's Deferred / Rating members.
            struct DeferredEncounter
            {
                EncounterReference encounter;
                EncounterMatchRating rating = EncounterMatchRating::MaxNotMatch;
            };

            /// PKHeX EncounterEnumerator3.TryGetNext over a template array, as a filter: true when
            /// `encounter` is a full Match to yield now; a better partial match is kept as the fallback.
            template <typename TemplateClass>
            bool isNextMatch(const TemplateClass &encounter, const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionChain &chain, DeferredEncounter &deferred)
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
                    if (rating < deferred.rating)
                    {
                        deferred.encounter = borrowEncounter(encounter);
                        deferred.rating = rating;
                    }
                    return false;
                }
                return false;
            }

            template <typename TemplateClass>
            MatchedEncounters yieldMatches(std::span<const TemplateClass> templates, const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionChain &chain, DeferredEncounter &deferred)
            {
                for (const TemplateClass &encounter : templates)
                {
                    if (isNextMatch(encounter, pokemon, chain, deferred))
                    {
                        co_yield MatchedEncounter{borrowEncounter(encounter), EncounterMatchRating::Match};
                    }
                }
            }

            MatchedEncounters yieldSlotMatches(std::span<const EncounterArea3> areas, const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionChain &chain, DeferredEncounter &deferred, bool hasOriginalLocation, uint16_t metLocation)
            {
                for (const EncounterArea3 &area : areas)
                {
                    if (hasOriginalLocation && !area.isMatchLocation(metLocation))
                    {
                        continue;
                    }
                    for (uint16_t slotIndex = 0; slotIndex < area.slotCount(); slotIndex++)
                    {
                        const EncounterSlot3 &slot = area.slotAt(slotIndex);
                        if (isNextMatch(slot, pokemon, chain, deferred))
                        {
                            co_yield MatchedEncounter{borrowEncounter(slot), EncounterMatchRating::Match};
                        }
                    }
                }
            }

            std::span<const EncounterArea3> getVersionSlots(Enums::GameVersion version)
            {
                switch (version)
                {
                case Enums::GameVersion::RU:
                    return getAreas3R();
                case Enums::GameVersion::SA:
                    return getAreas3S();
                case Enums::GameVersion::EM:
                    return getAreas3E();
                case Enums::GameVersion::FR:
                    return getAreas3FR();
                case Enums::GameVersion::LG:
                    return getAreas3LG();
                default:
                    return {};
                }
            }

            std::span<const EncounterStatic3> getVersionStatics(Enums::GameVersion version)
            {
                switch (version)
                {
                case Enums::GameVersion::RU:
                    return getStatic3R();
                case Enums::GameVersion::SA:
                    return getStatic3S();
                case Enums::GameVersion::EM:
                    return getStatic3E();
                case Enums::GameVersion::FR:
                    return getStatic3FR();
                case Enums::GameVersion::LG:
                    return getStatic3LG();
                default:
                    return {};
                }
            }

            bool isHoennVersion(Enums::GameVersion version)
            {
                return version == Enums::GameVersion::RU || version == Enums::GameVersion::SA || version == Enums::GameVersion::EM;
            }

            bool isKantoVersion(Enums::GameVersion version)
            {
                return version == Enums::GameVersion::FR || version == Enums::GameVersion::LG;
            }

            /// PKHeX YieldState.SlotStart .. SlotEnd.
            MatchedEncounters yieldSlots(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionChain &chain, Enums::GameVersion version, DeferredEncounter &deferred, bool hasOriginalLocation, uint16_t metLocation)
            {
                const bool canBeWild = !pokemon.isEgg() && pokemon.eggLocation() == 0;
                if (!canBeWild)
                {
                    co_return;
                }
                if (isHoennVersion(version))
                {
                    // Swarms first: their special moves rank them ahead, and only one can match.
                    for (const MatchedEncounter &match : yieldSlotMatches(getAreas3SwarmRSE(), pokemon, chain, deferred, hasOriginalLocation, metLocation))
                    {
                        co_yield match;
                    }
                }
                else if (!isKantoVersion(version))
                {
                    co_return;
                }
                for (const MatchedEncounter &match : yieldSlotMatches(getVersionSlots(version), pokemon, chain, deferred, hasOriginalLocation, metLocation))
                {
                    co_yield match;
                }
            }

            /// PKHeX YieldState.StaticStart .. StaticEnd.
            MatchedEncounters yieldStatics(const Pokemon::Pokemon &pokemon, const Evolutions::EvolutionChain &chain, Enums::GameVersion version, DeferredEncounter &deferred)
            {
                if (!isHoennVersion(version) && !isKantoVersion(version))
                {
                    co_return;
                }
                for (const MatchedEncounter &match : yieldMatches(getVersionStatics(version), pokemon, chain, deferred))
                {
                    co_yield match;
                }
                const std::span<const EncounterStatic3> shared = isHoennVersion(version) ? getStatic3RSE() : getStatic3FRLG();
                for (const MatchedEncounter &match : yieldMatches(shared, pokemon, chain, deferred))
                {
                    co_yield match;
                }
            }
        }

        MatchedEncounters EncounterEnumerator3::enumerate(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain, Enums::GameVersion version)
        {
            if (chain.isEmpty())
            {
                co_return;
            }
            DeferredEncounter deferred;

            // Events
            for (const MatchedEncounter &match : yieldMatches(getColosseumGifts3R(), pokemon, chain, deferred))
            {
                co_yield match;
            }
            for (const MatchedEncounter &match : yieldMatches(getColosseumGifts3S(), pokemon, chain, deferred))
            {
                co_yield match;
            }
            for (const MatchedEncounter &match : yieldMatches(getGifts3WC3(), pokemon, chain, deferred))
            {
                co_yield match;
            }
            if (pokemon.language() == static_cast<uint8_t>(Enums::LanguageID::English))
            {
                for (const MatchedEncounter &match : yieldMatches(getGifts3PCNY(), pokemon, chain, deferred))
                {
                    co_yield match;
                }
            }
            else if (pokemon.language() == static_cast<uint8_t>(Enums::LanguageID::Japanese))
            {
                for (const MatchedEncounter &match : yieldMatches(getGifts3PCJP(), pokemon, chain, deferred))
                {
                    co_yield match;
                }
            }

            // Trades
            if (version == Enums::GameVersion::FR || version == Enums::GameVersion::LG)
            {
                const std::span<const EncounterTrade3> versionTrades = version == Enums::GameVersion::FR ? getTradeGift3FR() : getTradeGift3LG();
                for (const MatchedEncounter &match : yieldMatches(versionTrades, pokemon, chain, deferred))
                {
                    co_yield match;
                }
                for (const MatchedEncounter &match : yieldMatches(getTradeGift3FRLG(), pokemon, chain, deferred))
                {
                    co_yield match;
                }
            }
            else
            {
                if (version != Enums::GameVersion::EM)
                {
                    for (const MatchedEncounter &match : yieldMatches(getTradeGift3RS(), pokemon, chain, deferred))
                    {
                        co_yield match;
                    }
                }
                for (const MatchedEncounter &match : yieldMatches(getTradeGift3E(), pokemon, chain, deferred))
                {
                    co_yield match;
                }
            }

            // Captures. A Safari Ball is never a static encounter, so its slots go first.
            const uint16_t metLocation = pokemon.metLocation();
            const bool mustBeSlot = pokemon.ball() == static_cast<uint8_t>(Enums::Ball::Safari);
            const bool hasOriginalLocation = getFormatGeneration(pokemon) == 3;
            if (mustBeSlot)
            {
                for (const MatchedEncounter &match : yieldSlots(pokemon, chain, version, deferred, hasOriginalLocation, metLocation))
                {
                    co_yield match;
                }
                for (const MatchedEncounter &match : yieldStatics(pokemon, chain, version, deferred))
                {
                    co_yield match;
                }
            }
            else
            {
                for (const MatchedEncounter &match : yieldStatics(pokemon, chain, version, deferred))
                {
                    co_yield match;
                }
                for (const MatchedEncounter &match : yieldSlots(pokemon, chain, version, deferred, hasOriginalLocation, metLocation))
                {
                    co_yield match;
                }
            }

            // Bred
            EncounterEgg3 egg(0, version);
            if (EncounterGenerator3::tryGetEgg(chain.view(), version, egg))
            {
                co_yield MatchedEncounter{makeEncounter<EncounterEgg3>(egg), EncounterMatchRating::Match};
                EncounterEgg3 split(0, version);
                if (EncounterGenerator3::tryGetSplit(egg, chain.view(), split))
                {
                    co_yield MatchedEncounter{makeEncounter<EncounterEgg3>(split), EncounterMatchRating::Match};
                }
            }

            if (deferred.encounter)
            {
                co_yield MatchedEncounter{deferred.encounter, deferred.rating};
            }
        }
    }
}
