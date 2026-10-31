#include "Pokemon/Trade.h"

#include <cstdint>

#include "Enums/GameVersion.h"
#include "Enums/LanguageID.h"
#include "Pokemon/PersonalRecord.h"
#include "Pokemon/Pokemon.h"

namespace Pokemon
{

    namespace
    {

        /// Return and Frustration read whichever friendship belongs to the ACTIVE handler, which is
        /// why Gen 6/7 carries the value across a handler switch instead of resetting it.
        constexpr uint16_t MOVE_ID_RETURN = 216;
        constexpr uint16_t MOVE_ID_FRUSTRATION = 218;

        /// PKHeX's SetTradeMemoryHT6: memory 4 is "Link trade to [VAR: General Location]" and
        /// variable 9 is the Pokecenter a link trade happens in. Intensity 1 is what it writes.
        constexpr uint8_t TRADE_MEMORY_ID = 4;
        constexpr uint16_t TRADE_MEMORY_VARIABLE_POKECENTER = 9;
        constexpr uint8_t TRADE_MEMORY_INTENSITY = 1;

        /**
         * The feelings a link-trade memory may carry, PKHeX's MemoryContext6Data.MemoryFeelings[4]
         * (0x04CBFD) decoded for the 0-19 range GetRandomFeeling6(4, 20) draws from. Ascending, so the
         * ones below a smaller limit are a prefix (linkTradeMemoryFeeling).
         *
         * **NOT EVERY VALUE BELOW 20 IS LEGAL.** Seven of them -- 1, 10, 12, 13, 16, 17 and 19 --
         * are not in the mask, so picking a feeling with a plain modulo 20 writes a memory the
         * MemoryVerifier rejects roughly a third of the time, and it reads as an ordinary number
         * rather than as an error.
         */
        constexpr uint8_t TRADE_MEMORY_FEELINGS[] = {0, 2, 3, 4, 5, 6, 7, 8, 9, 11, 14, 15, 18};

        /**
         * Which of the four trade behaviours a record follows.
         *
         * Decided by asking the ENTITY what it can store rather than by naming game groups, for the
         * reason every gate in PKSE is: one that names games is wrong about the next one. The
         * predicates separate the eras exactly, with nothing left over -- a handler language
         * is the thing Generation 8 introduced, and the geolocation history is the thing it dropped,
         * so a record with a handler and neither is Let's Go and can be nothing else.
         */
        enum class TradeEra
        {
            Gen6Style, ///< X/Y, OR/AS
            Gen7Style, ///< S/M, US/UM
            LetsGo,    ///< Let's Go Pikachu/Eevee
            Gen8Style, ///< Sw/Sh, BD/SP, Legends: Arceus, S/V, Legends: Z-A
        };

        TradeEra tradeEraFor(const Pokemon &pokemon) noexcept
        {
            if (pokemon.hasHandlerLanguage())
            {
                return TradeEra::Gen8Style;
            }
            // GEN 7 KEEPS THE 3DS RECORD BUT NOT ITS TRADE HABITS. Sun/Moon still stores memories and a
            // geolocation history, yet an in-game trade writes neither (PKHeX PK7.TradeHT: "No
            // geolocations are set in-game"), and its MemoryVerifier rejects a Gen 6 link-trade memory
            // made there. Hyper Training is what Gen 7 added to that record, so it is what tells the two
            // apart without naming a game.
            if (pokemon.hasGeolocation())
            {
                return pokemon.hasHyperTraining() ? TradeEra::Gen7Style : TradeEra::Gen6Style;
            }
            return TradeEra::LetsGo;
        }

        bool knowsFriendshipPoweredMove(const Pokemon &pokemon) noexcept
        {
            for (int moveSlot = 0; moveSlot < 4; ++moveSlot)
            {
                const uint16_t moveId = pokemon.move(moveSlot);
                if (moveId == MOVE_ID_RETURN || moveId == MOVE_ID_FRUSTRATION)
                {
                    return true;
                }
            }
            return false;
        }

        /**
         * Writes the link-trade memory, choosing a feeling from THE RECORD'S OWN encryption
         * constant rather than from a random number generator.
         *
         * The games roll it, but a save editor must not: a conversion that answered differently
         * every time could not be tested, and a user who performed a trade, undid it and performed
         * it again would get a different Pokemon each time. Seeding from a value the Pokemon
         * already carries gives variety between Pokemon and none within one -- the same rule the
         * Poke Transporter path follows.
         */
        void writeLinkTradeMemory(Pokemon &pokemon)
        {
            pokemon.setHTMemory(TRADE_MEMORY_ID);
            pokemon.setHTMemoryVariable(TRADE_MEMORY_VARIABLE_POKECENTER);
            pokemon.setHTMemoryIntensity(TRADE_MEMORY_INTENSITY);
            constexpr uint8_t tradeFeelingLimit = 20;
            pokemon.setHTMemoryFeeling(linkTradeMemoryFeeling(pokemon.encryptionConstant(), tradeFeelingLimit));
        }

        void clearHandlerMemory(Pokemon &pokemon)
        {
            pokemon.setHTMemory(0);
            pokemon.setHTMemoryVariable(0);
            pokemon.setHTMemoryIntensity(0);
            pokemon.setHTMemoryFeeling(0);
        }

        /**
         * Pushes the receiving trainer's console region into slot 0 and trickles the rest down one
         * place, which is what PKHeX's IGeoTrack.TradeGeoLocation does.
         *
         * WRITING SLOT 0 ALONE WOULD LOSE THE CHAIN. The games keep five, one per trade, so a
         * Pokemon that has changed hands twice carries both handlers' regions -- overwriting only
         * the newest silently discards a history the summary screen shows.
         */
        void pushGeolocation(Pokemon &pokemon, uint8_t country, uint8_t region)
        {
            for (int slotIndex = Pokemon::GEOLOCATION_SLOT_COUNT - 1; slotIndex > 0; --slotIndex)
            {
                pokemon.setGeolocationCountry(slotIndex, pokemon.geolocationCountry(slotIndex - 1));
                pokemon.setGeolocationRegion(slotIndex, pokemon.geolocationRegion(slotIndex - 1));
            }
            pokemon.setGeolocationCountry(0, country);
            pokemon.setGeolocationRegion(0, region);
        }

        /// What the receiving trainer writes (PKHeX TradeHT): the trade out, and the start of a
        /// transfer into another game, write the same handler block.
        void writeHandlerBlock(Pokemon &pokemon, const TradePartner &partner)
        {
            const TradeEra era = tradeEraFor(pokemon);
            const Enums::GameVersion gameGroup = pokemon.getGameGroup();

            pokemon.setHTName(partner.trainerName);
            pokemon.setHTGender(partner.gender);
            if (pokemon.hasHandlerLanguage())
            {
                // A LANGUAGE ID IS NOT PORTABLE BACKWARDS, and a donor save can be from a generation
                // that shipped in languages this one never did.
                pokemon.setHTLanguage(Enums::safeLanguageForGroup(gameGroup, partner.language));
            }

            // THE NEW HANDLER STARTS AT THE SPECIES' BASE FRIENDSHIP -- except in Let's Go, which
            // copies the current value instead, because its CP is computed from friendship and a reset
            // would visibly change the Pokemon's CP (PKHeX PB7.TradeHT says so in as many words).
            if (era == TradeEra::LetsGo)
            {
                pokemon.setHTFriendship(pokemon.friendship());
            }
            else
            {
                const PersonalRecord &personal = getPersonalRecord(gameGroup, pokemon.speciesID(), pokemon.form());
                pokemon.setHTFriendship(personal.baseFriendship);
            }

            if (pokemon.hasHandlerAffection())
            {
                pokemon.setHTAffection(0);
            }

            if (pokemon.hasHandlerMemories())
            {
                if (era == TradeEra::Gen6Style)
                {
                    // ONLY WHEN THERE IS NONE ALREADY. A Pokemon that has been traded before carries a
                    // memory its previous handler made, and overwriting it would erase something the
                    // games keep (PKHeX: "I'd rather not overwrite existing memories").
                    if (pokemon.htMemory() == 0)
                    {
                        writeLinkTradeMemory(pokemon);
                    }
                }
                else if (era == TradeEra::Gen7Style)
                {
                    // Neither written nor cleared: a Gen 7 trade leaves the memory fields as they were.
                }
                else
                {
                    // Gen 8 and 9 clear the old handler's memories and write none of their own --
                    // "Memories are deferred to the game. SW/SH does not immediately set memories."
                    clearHandlerMemory(pokemon);
                }
            }

            // ONLY WHEN THE PARTNER ACTUALLY SUPPLIES A REGION. PKHeX pushes a geolocation entry only
            // for a trainer that carries one (`if (tr is IRegionOriginReadOnly o)`), and pushing an
            // empty one here would be worse than doing nothing: the history is read in order and a
            // NON-EMPTY SLOT AFTER AN EMPTY ONE IS INVALID (IGeoTrack.GetValidity ->
            // CountryAfterPreviousEmpty), so shifting real entries down behind a blank slot 0 would
            // turn a legal record illegal. An all-zero history is legal; a gapped one is not.
            if (era == TradeEra::Gen6Style && partner.consoleCountry != 0)
            {
                pushGeolocation(pokemon, partner.consoleCountry, partner.consoleRegion);
            }

            // Return and Frustration read the ACTIVE handler's friendship, so Gen 6/7 carries the value
            // across the switch rather than letting those moves change power on a trade (PKHeX
            // G6PKM.UpdateHandler, shared by PK6 and PK7). Gen 8 dropped the clause along with the moves.
            if ((era == TradeEra::Gen6Style || era == TradeEra::Gen7Style) && knowsFriendshipPoweredMove(pokemon))
            {
                pokemon.setHTFriendship(pokemon.otFriendship());
            }
        }
    }

    TradePartner generateTradePartner(const Pokemon &pokemon)
    {
        TradePartner partner;
        // THE POKEMON'S OWN TRAINER, COPIED. Nothing here is invented: the name, the gender and the
        // language are the ones already on the record, so a generated handler claims a trade with
        // somebody who looks exactly like the original trainer rather than conjuring a person who
        // does not exist. That is the least this can assert and still say a trade happened, which is
        // all the handler block is for -- PKHeX reads a non-empty handler name as "traded"
        // (PKM.IsUntraded) and checks the handler against the SAVE's trainer, never against the OT.
        //
        // **THERE IS NO HANDLER ID TO MAKE IT A DIFFERENT PERSON.** Generations 6 and 7 store no
        // such field at all, and the one Generation 8 added (HandlingTrainerID, 0xC6 / 0xD6 on PA8)
        // is marked unused in PKHeX and written by nothing -- so filling it would be PKSE putting
        // data in a field the games leave alone.
        partner.trainerName = pokemon.otName();
        partner.gender = pokemon.otGender();
        partner.language = pokemon.language();
        // consoleCountry / consoleRegion stay 0: an empty geolocation history is legal, and a
        // country/region pair PKSE cannot verify is not.
        return partner;
    }

    bool applyTradeRoundTrip(Pokemon &pokemon, const TradePartner &partner)
    {
        if (!pokemon.hasHandler() || partner.trainerName.empty())
        {
            return false;
        }

        writeHandlerBlock(pokemon, partner);

        // ---- the return leg: the ONLY thing trading back writes ----
        // The handler block above stays exactly as it is. That residue is what says a trade
        // happened, and clearing it here would undo the whole point of the round trip.
        pokemon.setCurrentHandler(0);

        pokemon.refreshChecksum();
        return true;
    }

    bool applyReceivingTrainer(Pokemon &pokemon, const TradePartner &receiver)
    {
        if (!pokemon.hasHandler() || receiver.trainerName.empty())
        {
            return false;
        }

        // A HANDLER WHO IS ALREADY THE RECEIVER IS KEPT, memories and friendship included. PKHeX
        // compares the name and gender only (PKH.IsHandlerSame), so a Pokemon that has been in this
        // trainer's hands before comes back to the history it left with.
        if (pokemon.htName() != receiver.trainerName || pokemon.htGender() != receiver.gender)
        {
            writeHandlerBlock(pokemon, receiver);
        }
        pokemon.setCurrentHandler(1);

        pokemon.refreshChecksum();
        return true;
    }

    uint8_t linkTradeMemoryFeeling(uint32_t choice, uint8_t feelingLimit)
    {
        uint32_t permittedCount = 0;
        for (uint8_t feeling : TRADE_MEMORY_FEELINGS)
        {
            if (feeling < feelingLimit)
                ++permittedCount;
        }
        if (permittedCount == 0)
            return TRADE_MEMORY_FEELINGS[0];
        return TRADE_MEMORY_FEELINGS[choice % permittedCount];
    }
}
