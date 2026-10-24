/**
 * Two families of table live here, and both fail the same quiet way: an id that resolves to nothing
 * still resolves to SOMETHING, and the something reads as a definite answer.
 *
 *   ITEMS. Every generation names every id, and the ids mean different things in each -- Gen 1's TMs
 *   are ordinary bag items at 201-250, which modern numbering has nothing at. Names::getItemNameFor
 *   is the single place that picks a table, so a wrong pick gives a confident wrong name rather than
 *   a blank, and nothing downstream can tell.
 *
 *   LEARNSETS. An EMPTY move pool does not read as "no data" anywhere: the legality checker calls
 *   every move illegal, and the bank's transfer sanitizer DELETES all four on the way into a save.
 *   That is the failure this table exists to prevent, so "the pool is not empty" is the check that
 *   matters most, per species, per game.
 *
 * The move-presence cross-check is the interesting one: a move a species can learn in a game must
 * also EXIST in that game. The two tables are generated separately, from different PKHeX resources,
 * and nothing else compares them -- so a disagreement means the sanitizer will strip a move the
 * learnset says is fine.
 */
#include <set>
#include <string>

#include "Test/TestSuite.h"
#include "Trainer/Trainer.h"
#include "Names/ItemNames.h"
#include "Names/ItemPouches.h"
#include "Names/MoveNames.h"
#include "Names/MoveInfo.h"
#include "Names/MovePresence.h"
#include "Pokemon/LearnsetTable.h"
#include "Pokemon/PersonalInfoTable.h"
#include "Pokemon/Gen1Tables.h"

namespace Test
{

    namespace
    {
        constexpr int SPECIES_PER_CHUNK = 192;
        constexpr int FIXED_CHUNK_COUNT = 1; // chunk 0 = items + pouches

        bool isNamed(const char *text)
        {
            return text != nullptr && text[0] != '\0';
        }

        std::string speciesLabel(uint16_t speciesId)
        {
            return "species=" + std::to_string(speciesId) + " (" + Trainer::getSpeciesName(speciesId) + ")";
        }

        /// Highest species this generation's dex could contain, for groups whose presence is not
        /// in the modern personal table's per-game bits (everything before Let's Go predates it).
        ///
        /// Gen 7's two entries differ on purpose: Sun/Moon stops at 802 and Ultra Sun/Moon adds
        /// the five Ultra Beasts plus Zeraora to 807. Collapsing them to one number is exactly the
        /// kind of off-by-a-handful that reads as "the table is fine" until a real save has one.
        uint16_t legacyDexMax(Enums::GameVersion group)
        {
            switch (group)
            {
            case Enums::GameVersion::RBY:
                return 151;
            case Enums::GameVersion::GSC:
                return 251;
            case Enums::GameVersion::RSE:
            case Enums::GameVersion::FRLG:
                return 386;
            case Enums::GameVersion::DP:
            case Enums::GameVersion::PT:
            case Enums::GameVersion::HGSS:
                return 493;
            case Enums::GameVersion::BW:
            case Enums::GameVersion::B2W2:
                return 649;
            case Enums::GameVersion::XY:
            case Enums::GameVersion::ORAS:
                return 721;
            case Enums::GameVersion::SM:
                return 802;
            case Enums::GameVersion::USUM:
                return 807;
            default:
                return 0;
            }
        }

        /// The presence bit for a modern group, or 0 when the group has none.
        uint8_t presenceBit(Enums::GameVersion group)
        {
            switch (group)
            {
            case Enums::GameVersion::GG:
                return Pokemon::PERSONAL_GAME_GG;
            case Enums::GameVersion::SWSH:
                return Pokemon::PERSONAL_GAME_SWSH;
            case Enums::GameVersion::BDSP:
                return Pokemon::PERSONAL_GAME_BDSP;
            case Enums::GameVersion::PLA:
                return Pokemon::PERSONAL_GAME_PLA;
            case Enums::GameVersion::SV:
                return Pokemon::PERSONAL_GAME_SV;
            case Enums::GameVersion::ZA:
                return Pokemon::PERSONAL_GAME_ZA;
            default:
                return 0;
            }
        }

        /// Does this generation contain this species+FORM? The form half matters as much as the
        /// species half: formCount counts every form across every supported game, so Charizard has
        /// three even in Red and Blue, and asking Gen 1 for a Mega Charizard learnset gets a null
        /// row that reads as a missing table. Legacy groups answer by dex ceiling and form 0 only
        /// (Gen 1 and Gen 3 predate the per-game presence bits, and their alternate forms share a
        /// learn pool with form 0 anyway); modern ones answer from the per-form presence byte.
        bool inThisGame(Enums::GameVersion group, uint16_t speciesId, uint8_t formId)
        {
            const uint16_t legacyDexCeiling = legacyDexMax(group);
            if (legacyDexCeiling != 0) return formId == 0 && speciesId <= legacyDexCeiling;
            const uint8_t gamePresenceBit = presenceBit(group);
            if (gamePresenceBit == 0) return false;
            return (Pokemon::getPersonalInfo(speciesId, formId).presence & gamePresenceBit) != 0;
        }

        void checkItems(Context &context)
        {
            const size_t pouchCount = Names::getPouchCount(context.group);
            if (!context.check(pouchCount > 0, "generation has no item pouches at all")) return;

            // NOTE: there is deliberately no "this item is in two pouches" check. Sharing an id
            // across pouches is the DESIGN, not a mistake -- Gen 1 offers the same item list for the
            // bag and the PC, and the modern games repeat items between a carry pouch and a storage
            // one. A check for it produced 429 failures on a correct build, which is the shape of a
            // test that is wrong rather than a codebase that is.
            size_t offeredItemCount = 0;
            for (size_t pouchIndex = 0; pouchIndex < pouchCount; ++pouchIndex)
            {
                const auto pouchItemIds = Names::getPouchItems(context.group, pouchIndex);
                context.check(!pouchItemIds.empty(), "item pouch is empty", "pouch=" + std::to_string(pouchIndex));

                std::set<uint16_t> withinPouch;
                for (uint16_t itemId : pouchItemIds)
                {
                    ++offeredItemCount;
                    const char *itemName = Names::getItemNameFor(context.group, itemId);
                    // "???" is getItemNameFor's out-of-range answer, so an id past the table fails.
                    context.check(isNamed(itemName) && std::string(itemName) != "???", "item offered by a pouch has no name in this generation", "pouch=" + std::to_string(pouchIndex) + " item=" + std::to_string(itemId));
                    context.check(withinPouch.insert(itemId).second, "item listed twice in the same pouch", "pouch=" + std::to_string(pouchIndex) + " item=" + std::to_string(itemId) + " name=\"" + (itemName ? itemName : "") + "\"");
                }
            }
            context.note("items offered across all pouches", "count=" + std::to_string(offeredItemCount));

            // KEY ITEMS, for Add All Items. An empty table reads as "nothing here is a key item", which
            // hands out every key item with key items switched off -- so "unknown" must not be possible.
            std::set<uint16_t> offeredItemIds;
            for (size_t pouchIndex = 0; pouchIndex < pouchCount; ++pouchIndex)
            {
                for (uint16_t itemId : Names::getPouchItems(context.group, pouchIndex))
                {
                    offeredItemIds.insert(itemId);
                }
            }
            size_t keyItemCount = 0;
            for (uint32_t itemId = 1; itemId <= 0xFFFF; ++itemId)
            {
                if (!Names::isKeyItem(context.group, static_cast<uint16_t>(itemId)))
                {
                    continue;
                }
                ++keyItemCount;
                const char *itemName = Names::getItemNameFor(context.group, static_cast<uint16_t>(itemId));
                context.check(isNamed(itemName) && std::string(itemName) != "???", "key item has no name in this generation", "item=" + std::to_string(itemId));
                // Checked by NAME, so a table emitted for the wrong game is caught: every other check
                // here asks the same table it is checking. Medicine is never a key item in any game
                // (Gen 2's SecretPotion is a key item, so the names are matched whole).
                const std::string keyItemName = itemName ? itemName : "";
                const bool isMedicine = keyItemName == "Potion" || keyItemName == "Super Potion" || keyItemName == "Hyper Potion" || keyItemName == "Max Potion" || keyItemName == "Revive" || keyItemName == "Rare Candy" || keyItemName == "Antidote" || keyItemName == "Full Heal";
                context.check(!isMedicine, "a medicine is listed as a key item, so the key-item table is another game's", "item=" + std::to_string(itemId) + " name=\"" + keyItemName + "\"");
                // Gen 1's list is the ROM's own, which also flags items no pouch can be given (the
                // Surfboard, the Pokedex, the badges); every other group's comes from PKHeX's pouch lists.
                if (context.group != Enums::GameVersion::RBY)
                {
                    context.check(offeredItemIds.count(static_cast<uint16_t>(itemId)) != 0, "key item is offered by no pouch, so the key-item table and the pouch tables disagree", "item=" + std::to_string(itemId) + " name=\"" + (itemName ? itemName : "") + "\"");
                }
            }
            context.check(keyItemCount > 0, "generation has no key-item table, so Add All Items would hand out every key item");
            if (context.group == Enums::GameVersion::RBY)
            {
                // Hand-written from KeyItemFlags, so pinned: the Bicycle and HM01 are key items, a Potion and TM01 are not.
                context.check(keyItemCount == 36, "Gen 1 key-item count changed from the ROM's 31 flagged items plus five HMs", "count=" + std::to_string(keyItemCount));
                context.check(Names::isKeyItem(context.group, 6) && Names::isKeyItem(context.group, 196), "Gen 1 Bicycle or HM01 is not a key item");
                context.check(!Names::isKeyItem(context.group, 20) && !Names::isKeyItem(context.group, 201), "Gen 1 Potion or TM01 is a key item");
            }

            // Gen 1 has its own id space and its own name table. getItemNameFor is the one place
            // that picks a table, so this pins the two together: if the router ever stops sending
            // RBY to the Gen 1 table, every id here starts naming a modern item instead.
            if (context.group == Enums::GameVersion::RBY)
            {
                for (uint16_t itemId = 1; itemId <= Pokemon::MAX_ITEM_GEN1; ++itemId)
                {
                    if (!Pokemon::isItemLegalGen1((uint8_t)itemId))
                    {
                        continue;
                    }
                    const char *viaRouter = Names::getItemNameFor(Enums::GameVersion::RBY, itemId);
                    const char *viaTable = Pokemon::getItemNameGen1((uint8_t)itemId);
                    context.check(isNamed(viaRouter) && isNamed(viaTable) && std::string(viaRouter) == std::string(viaTable), "Gen 1 item name router disagrees with the Gen 1 table", "item=" + std::to_string(itemId) + " router=\"" + (viaRouter ? viaRouter : "") + "\" table=\"" + (viaTable ? viaTable : "") + "\"");
                }

                // Move PP is the same shape of question, and the same trap. Gen 1 has its own PP
                // table and its own PP Up rule (min(7, base/5) per Up, not base/5 rounded), so the
                // shared Names::getMoveMaxPP router has to reach it -- the alternative is a modern
                // game's PP number written into a Gen 1 record, which the games will happily hold
                // and nothing else will flag.
                for (uint16_t moveId = 1; moveId <= Pokemon::MAX_MOVE_GEN1; ++moveId)
                {
                    // One finding per MOVE rather than per (move, PP Up) pair: four lines saying the
                    // same move has the wrong PP is three lines of noise, and the count at the top of
                    // the group is more useful when it counts moves.
                    std::string mismatch;
                    for (uint8_t ppUps = 0; ppUps <= 3 && mismatch.empty(); ++ppUps)
                    {
                        const uint8_t viaRouter = Names::getMoveMaxPP(moveId, ppUps, Enums::GameVersion::RBY);
                        const uint8_t viaTable = Pokemon::getMovePPGen1WithUps(moveId, ppUps);
                        if (viaRouter != viaTable)
                        {
                            mismatch = " ppUps=" + std::to_string(ppUps) + " router=" + std::to_string(viaRouter) + " table=" + std::to_string(viaTable);
                        }
                    }
                    context.check(mismatch.empty(), "Gen 1 move PP router disagrees with the Gen 1 table", "move=" + std::to_string(moveId) + " (" + Names::getMoveName(moveId) + ")" + mismatch);
                }
            }
        }

        /// Whether a game has a move. Names::isMovePresent has a presence table for the games that
        /// REMOVED moves (Gen 8 onward, plus Let's Go and the Gen 1-3 tables) and answers true for
        /// every other group. Before Gen 8 no game dropped a move, so there a game has exactly the
        /// moves up to its generation's last one -- PKHeX's MaxMoveID per game, used here so those
        /// ten groups are checked rather than waved through.
        bool moveExistsInGame(uint16_t moveId, Enums::GameVersion group)
        {
            using Enums::GameVersion;
            switch (group)
            {
            case GameVersion::GSC:
                return moveId <= 251;
            case GameVersion::DP:
            case GameVersion::PT:
            case GameVersion::HGSS:
                return moveId <= 467;
            case GameVersion::BW:
            case GameVersion::B2W2:
                return moveId <= 559;
            case GameVersion::XY:
                return moveId <= 617;
            case GameVersion::ORAS:
                return moveId <= 621;
            case GameVersion::SM:
                return moveId <= 719;
            case GameVersion::USUM:
                return moveId <= 728;
            default:
                return Names::isMovePresent(moveId, group);
            }
        }

        /// Does this group have a learnset table at ALL?
        ///
        /// The distinction matters and is the whole point of this function existing: "this game's
        /// table is missing a species" is a bug, while "this game has no table yet" is a known
        /// gap. Reporting the second as thousands of failures buries the first: 6079 identical
        /// FAIL lines, one per species per generation, in a report whose real findings were four
        /// lines long.
        bool groupHasLearnsets(Enums::GameVersion group)
        {
            // Every species, stopping at the first row that exists. Probing ONE species looks cheaper and
            // is wrong: Bulbasaur is absent from Legends: Arceus, so a Bulbasaur probe reports PLA -- which
            // has a perfectly good table -- as having none.
            for (uint16_t speciesId = 1; speciesId <= Pokemon::PERSONAL_MAX_SPECIES; ++speciesId)
            {
                if (Pokemon::getLearnableBits(speciesId, 0, group) != nullptr) return true;
            }
            return false;
        }

        void checkLearnsets(Context &context, uint16_t firstSpeciesId, uint16_t lastSpeciesId)
        {
            if (!groupHasLearnsets(context.group))
            {
                // Once per group, not once per species range -- this suite is called six times per
                // game and the same failure six times over says nothing the first one did not.
                // Every group has a learnset table, so one going missing is a regeneration that dropped
                // a game -- a failure, not a note.
                if (firstSpeciesId > 1) return;
                context.fail("no learnset table for this generation -- move learnability is UNKNOWN here; the move picker highlights nothing and the legality analyzer skips its learnability check", Enums::getGameVersionName(context.group));
                return;
            }
            int presentSpeciesCount = 0;
            int missingMoveCount = 0;
            for (uint16_t speciesId = firstSpeciesId; speciesId <= lastSpeciesId && speciesId <= Pokemon::PERSONAL_MAX_SPECIES; ++speciesId)
            {
                if (!inThisGame(context.group, speciesId, 0))
                {
                    continue;
                }
                ++presentSpeciesCount;

                const Pokemon::PersonalInfo &personalInfo = Pokemon::getPersonalInfo(speciesId, 0);
                for (uint8_t formId = 0; formId < personalInfo.formCount; ++formId)
                {
                    if (!inThisGame(context.group, speciesId, formId))
                    {
                        continue;
                    }
                    const uint8_t *learnableBits = Pokemon::getLearnableBits(speciesId, formId, context.group);
                    const std::string where = speciesLabel(speciesId) + " form=" + std::to_string(formId);
                    if (!context.check(learnableBits != nullptr, "species in this game has no learnset row", where))
                    {
                        continue;
                    }

                    int learnableMoveCount = 0;
                    for (uint16_t moveId = 1; moveId <= Pokemon::LEARN_MAX_MOVE_ID; ++moveId)
                    {
                        if (!((learnableBits[moveId >> 3] >> (moveId & 7)) & 1))
                        {
                            continue;
                        }
                        ++learnableMoveCount;
                        // A learnable move that the game does not have. The move tables and the
                        // learnset tables are generated from different PKHeX resources, and nothing
                        // else compares them -- so this is the only place the disagreement surfaces.
                        if (!moveExistsInGame(moveId, context.group))
                        {
                            ++missingMoveCount;
                            context.fail("learnset offers a move the game does not have", where + " move=" + std::to_string(moveId) + " (" + Names::getMoveName(moveId) + ")");
                        }
                    }
                    // Every form that reaches here is one the presence byte says this game HAS, so
                    // an empty pool is never correct: it is the table saying the Pokemon exists and
                    // can learn nothing.
                    context.check(learnableMoveCount > 0, "species in this game has an EMPTY move pool (every move reads as illegal; the transfer sanitizer deletes all four)", where);
                }
            }
            // Silent for a range this generation has nothing in: six "count=0" lines per game is
            // noise that pushes the ranges that DID have something off the sample cap.
            if (presentSpeciesCount > 0)
            {
                context.note("species checked in this generation's dex", "count=" + std::to_string(presentSpeciesCount) + " range=" + std::to_string(firstSpeciesId) + ".." + std::to_string(lastSpeciesId) + " movegaps=" + std::to_string(missingMoveCount));
            }
        }
    }

    int generationTablesPlan(Context &context)
    {
        if (context.group == Enums::GameVersion::Invalid) return 0;
        const int speciesChunks = (Pokemon::PERSONAL_MAX_SPECIES + SPECIES_PER_CHUNK - 1) / SPECIES_PER_CHUNK;
        return FIXED_CHUNK_COUNT + speciesChunks;
    }

    void generationTablesRun(Context &context, int chunk)
    {
        if (chunk == 0)
        {
            checkItems(context);
            return;
        }
        const uint16_t firstSpeciesId = (uint16_t)((chunk - FIXED_CHUNK_COUNT) * SPECIES_PER_CHUNK + 1);
        const uint16_t lastSpeciesId = (uint16_t)(firstSpeciesId + SPECIES_PER_CHUNK - 1);
        checkLearnsets(context, firstSpeciesId, lastSpeciesId);
    }
}
