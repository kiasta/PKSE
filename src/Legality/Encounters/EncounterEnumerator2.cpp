#include "Legality/Encounters/EncounterEnumerator2.h"

#include "Legality/Encounters/EncounterData2.h"
#include "Legality/Encounters/EncounterEgg2.h"
#include "Legality/Encounters/EncounterEnumeratorGameBoy.h"
#include "Legality/Encounters/EncounterGenerator2.h"
#include "Legality/Encounters/EncounterSlot2.h"
#include "Legality/Encounters/GBRestrictions.h"
#include "Pokemon/Pokemon.h"

namespace Legality
{
    namespace Encounters
    {
        using EnumeratorSearch::EnumeratorState;
        using EnumeratorSearch::isNextMatch;
        using EnumeratorSearch::yieldAreaMatches;
        using EnumeratorSearch::yieldEveryAreaMatch;
        using EnumeratorSearch::yieldMatches;

        MatchedEncounters EncounterEnumerator2::enumerate(const Pokemon::Pokemon &pokemon, Evolutions::EvolutionChain chain, const LegalityInfo &info)
        {
            if (chain.isEmpty())
            {
                co_return;
            }

            // PKHeX's constructor: what the met data says about a Crystal origin.
            const bool isKorean = GameBoyEntity::isKorean(pokemon);
            bool canOriginateCrystal = false;
            bool hasOriginalMet = false;
            uint16_t metLocation = 0;
            if (!isKorean)
            {
                if (!GameBoyEntity::hasCaughtDataField(pokemon))
                {
                    canOriginateCrystal = true;
                }
                else if (GameBoyEntity::getCaughtData(pokemon) == 0)
                {
                    // A trip to Gen 1 wipes Crystal's met data.
                    canOriginateCrystal = GBRestrictions::canVisitGen1(chain.view()[0].speciesId);
                }
                else
                {
                    canOriginateCrystal = true;
                    hasOriginalMet = true;
                    metLocation = pokemon.metLocation();
                }
            }
            const bool isCrystalOnly = hasOriginalMet || pokemon.otGender() == 1;
            EnumeratorState state;

            for (const MatchedEncounter &match : yieldMatches(getTradeGift2GSC(), pokemon, chain, state))
            {
                co_yield match;
            }

            // Bred: a Gold/Silver egg, and the same egg hatched in Crystal.
            EncounterEgg2 egg(0, Enums::GameVersion::GD);
            if (EncounterGenerator2::tryGetEgg(chain.view(), Enums::GameVersion::GD, egg))
            {
                co_yield MatchedEncounter{makeEncounter<EncounterEgg2>(egg), EncounterMatchRating::Match};
                EncounterEgg2 crystal(0, Enums::GameVersion::C);
                if (!isKorean && EncounterGenerator2::tryGetEggCrystal(pokemon, egg, crystal))
                {
                    co_yield MatchedEncounter{makeEncounter<EncounterEgg2>(crystal), EncounterMatchRating::Match};
                }
            }

            // Statics. PKHeX ParseSettings.AllowGen2OddEgg.
            if (!GameBoyEntity::isJapanese(pokemon) || info.allowEraCartGB)
            {
                for (const MatchedEncounter &match : yieldMatches(getStatic2OddEggC(), pokemon, chain, state))
                {
                    co_yield match;
                }
            }
            bool skipGoldSilver = false;
            if (canOriginateCrystal)
            {
                for (const MatchedEncounter &match : yieldMatches(getStatic2C(), pokemon, chain, state))
                {
                    co_yield match;
                }
                skipGoldSilver = isCrystalOnly;
            }
            if (!skipGoldSilver)
            {
                for (const MatchedEncounter &match : yieldMatches(getStatic2GD(), pokemon, chain, state))
                {
                    co_yield match;
                }
                for (const MatchedEncounter &match : yieldMatches(getStatic2SI(), pokemon, chain, state))
                {
                    co_yield match;
                }
                for (const MatchedEncounter &match : yieldMatches(getStatic2GS(), pokemon, chain, state))
                {
                    co_yield match;
                }
            }
            for (const MatchedEncounter &match : yieldMatches(getStatic2GSC(), pokemon, chain, state))
            {
                co_yield match;
            }

            // Wild slots. Crystal's are filtered by the met location when the record kept one.
            skipGoldSilver = false;
            if (canOriginateCrystal)
            {
                if (hasOriginalMet)
                {
                    for (const MatchedEncounter &match : yieldAreaMatches(getAreas2C(), metLocation, pokemon, chain, state))
                    {
                        co_yield match;
                    }
                }
                else
                {
                    for (const MatchedEncounter &match : yieldEveryAreaMatch(getAreas2C(), pokemon, chain, state))
                    {
                        co_yield match;
                    }
                }
                skipGoldSilver = isCrystalOnly;
            }
            if (!skipGoldSilver)
            {
                for (const MatchedEncounter &match : yieldEveryAreaMatch(getAreas2GD(), pokemon, chain, state))
                {
                    co_yield match;
                }
                for (const MatchedEncounter &match : yieldEveryAreaMatch(getAreas2SI(), pokemon, chain, state))
                {
                    co_yield match;
                }
            }

            // Events: none for a Korean record; the Virtual Console Celebi, or the cartridge-era gifts
            // when a Game Boy save is open.
            if (!isKorean)
            {
                if (!info.allowEraCartGB)
                {
                    for (const EncounterStatic2 &celebi : getStatic2CelebiVirtualConsole())
                    {
                        if (isNextMatch(celebi, pokemon, chain, state))
                        {
                            co_yield MatchedEncounter{borrowEncounter(celebi), EncounterMatchRating::Match};
                        }
                    }
                }
                else
                {
                    for (const MatchedEncounter &match : yieldMatches(getGifts2GameBoyEra(), pokemon, chain, state))
                    {
                        co_yield match;
                    }
                }
            }

            if (state.deferred)
            {
                co_yield MatchedEncounter{state.deferred, state.rating};
            }
        }
    }
}
