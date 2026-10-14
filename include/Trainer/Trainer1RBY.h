/**
 * The first save PKSE opens that does NOT come from a Switch title. There is no title id, no
 * backup directory and no `main` file -- it is a loose 0x8000-byte dump from an emulator or a
 * cartridge reader, picked with the file browser. Offsets are PKHeX's SAV1.cs + SAV1Offsets.cs.
 *
 * LOCALE CHANGES THE LAYOUT, NOT JUST THE TEXT. A Japanese save is not an international one with
 * different characters in it: the name fields are 6 bytes instead of 11, there are 8 boxes of 30
 * instead of 12 of 20, and every offset from the trainer block onward shifts. Nothing here may
 * assume international. The locale is not recorded anywhere in the file either -- it is inferred
 * from which offsets hold a well-formed Pokemon list, which is what `detect()` does.
 *
 * Four things about this format have no counterpart in any later generation:
 *
 *  1. THE CHECKSUM IS ONE BYTE. `~(sum of 0x2598 .. checksumOfs-1)`, stored at checksumOfs. It
 *     covers the trainer block and everything after it, so a party edit changes it.
 *  2. THE CURRENT BOX IS STORED TWICE. Boxes live in two banks (0x4000 and 0x6000), but whichever
 *     box the player last had open is ALSO mirrored at `currentBox`, and that copy is the fresh
 *     one -- the game writes there as you play and only flushes to the bank on a box change. Read
 *     the wrong copy and the player's most recent box reads as stale. Both must be written back.
 *  3. A BOX RECORD IS 33 BYTES, A PARTY RECORD 44. The missing 11 are level + the five battle
 *     stats, which the game recomputes when a Pokemon leaves the PC. Box mons are therefore grown
 *     to party size and their stats recalculated on load -- the same thing the game does, and the
 *     reason `Pokemon1RBY` always holds a party-size body.
 *  4. THERE ARE NO BOX NAMES. The games have none; PKHeX synthesises "BOX 1".. for display. So
 *     `supportsBoxNames()` is false and PKSE shows its own defaults.
 *
 * Money is 3-byte big-endian BCD, capped at 999999 -- not a plain integer. Items are Gen 1's own
 * id space with its own names (see `Pokemon::getItemNameGen1`); the modern table names every slot
 * of a Gen 1 bag wrongly.
 */
#ifndef TRAINER_TRAINER1_RBY_H
#define TRAINER_TRAINER1_RBY_H

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "Trainer/Trainer.h"
#include "Trainer/Inventory1RBY.h"
#include "Pokemon/Pokemon1RBY.h"

namespace Trainer
{

    /// A Gen 1 save is exactly 32 KiB. Anything else is not one (PKHeX SaveUtil.SIZE_G1RAW).
    inline constexpr size_t RBY_SAVE_SIZE = 0x8000;

    /**
     * The offsets that move between the international and Japanese layouts.
     *
     * Verbatim from PKHeX's SAV1Offsets. `ot` is the one field at the same place in both, which is
     * also why it is the checksum's start.
     */
    struct RBYOffsets
    {
        static constexpr uint16_t otNameOffset = 0x2598; // shared by both layouts
        uint16_t dexCaught, dexSeen, items, money, rival, options, badges, tid16;
        uint16_t pikaFriendship, pcItems, currentBoxIndex, coin, eventFlag, playTime;
        uint16_t daycare, party, currentBox, checksumOfs, starter;

        static constexpr RBYOffsets international()
        {
            return {0x25A3, 0x25B6, 0x25C9, 0x25F3, 0x25F6, 0x2601, 0x2602, 0x2605,
                    0x271C, 0x27E6, 0x284C, 0x2850, 0x29F3, 0x2CED,
                    0x2CF4, 0x2F2C, 0x30C0, 0x3523, 0x29C3};
        }
        static constexpr RBYOffsets japanese()
        {
            return {0x259E, 0x25B1, 0x25C4, 0x25EE, 0x25F1, 0x25F7, 0x25F8, 0x25FB,
                    0x2712, 0x27DC, 0x2842, 0x2846, 0x29E9, 0x2CA0,
                    0x2CA7, 0x2ED5, 0x302D, 0x3594, 0x29B9};
        }
    };

    /**
     * What writing a Gen 1 save may move without an edit: the current box's flush (note 2 above).
     *
     * The live copy at `currentBox` is the fresh one, so writing brings the BANK copy of that box into
     * line with it -- PKHeX's SAV1.GetFinalData does the same -- and a save whose bank copy is stale
     * does not come back byte for byte. That is allowed on one condition: the change is confined to
     * the bank copy, the boxes-initialised bit and the checksum, and the bank copy afterwards EQUALS
     * the live one. Derived from the same geometry the writer uses, and public so a round trip can
     * prove that condition instead of assuming it -- the Gen 1 counterpart of Gen2Mirror.
     *
     * A BANK NEVER FLUSHED HOLDS UNINITIALISED MEMORY, which the reader does not trust, and the write
     * that first sets the initialised bit also initialises every other box list to EMPTY: a count of 0
     * and one 0xFF cap, the two bytes a list header has. `everyBankBoxOffset` names where those are.
     */
    struct Gen1BoxFlush
    {
        size_t liveBoxOffset;         // the copy at `currentBox`, the one the game reads
        size_t bankBoxOffset;         // the same box's copy in its bank
        size_t boxListLength;
        size_t initializedFlagOffset; // bit 0x80 says the bank has ever been flushed
        size_t checksumOffset;
        std::vector<size_t> everyBankBoxOffset;
    };

    class Trainer1RBY : public Trainer
    {
    public:
        /**
         * Is this a Gen 1 save, and which locale?
         *
         * The file records neither. PKHeX's test is the only one available: a Pokemon list has a
         * count byte followed by `count` species markers and then a 0xFF cap, so checking that
         * both the party and current-box offsets hold a well-formed list identifies the layout.
         * Japanese is tested FIRST, matching PKHeX -- the two candidate offsets are different, so
         * a save that satisfies the Japanese test is Japanese.
         */
        static bool detect(std::span<const uint8_t> data, bool &outJapanese) noexcept;

        /// `fileName` is the on-disk name the save was opened from; it is written back verbatim.
        explicit Trainer1RBY(std::vector<uint8_t> data, std::string fileName);

        bool isValid() const noexcept { return valid; }
        bool japanese() const noexcept { return saveIsJapanese; }
        Gen1BoxFlush currentBoxFlush() const
        {
            Gen1BoxFlush flush{offsets.currentBox, boxRawOffset(currentBoxIndex()), boxListSize(),
                               offsets.currentBoxIndex, offsets.checksumOfs, {}};
            for (size_t boxIndex = 0; boxIndex < boxCount(); ++boxIndex)
                flush.everyBankBoxOffset.push_back(boxRawOffset(boxIndex));
            return flush;
        }
        /// Red/Blue or Yellow. Not stored as a version byte -- inferred from the starter and the
        /// Pikachu-friendship field, which only Yellow uses (PKHeX SAV1.IsYellow).
        bool isYellow() const noexcept { return saveIsYellow; }

        /**
         * The one game name to show for this save -- the single place that decides it.
         *
         * Yellow is the only Gen 1 title a save can identify, because it is the only one that
         * writes the starter / Pikachu-friendship pair isYellow() reads. Red, Green and Blue share
         * an engine and an SRAM layout with no version byte anywhere in the 32 KiB, so nothing
         * tells them apart. Both references stop in the same place: PKHeX resolves a non-Yellow
         * save to the *pair* GameVersion.RB (its enum docs file RD/GN/BU under it), and PKSM
         * hardcodes RD outright, commenting "not even PKHeX tries to do better". So this names the
         * pair and invents no title. The filename is deliberately NOT a fallback -- a 3DS Virtual
         * Console backup is just `sav.dat`, and a guess that is right for one user's naming habit
         * is a wrong answer stated confidently for everyone else's.
         */
        const char *gameTitle() const noexcept;
        const std::string &fileName() const noexcept { return saveFileName; }

        /// The full save, checksum refreshed. This is what gets written back to the SD card.
        const std::vector<uint8_t> &serialize();

        void updatePartyBlock() override;
        void updateBoxBlock() override;
        void updateItemBlock() override;
        void updateTrainerInfoBlock() override;
        void updateCurrentBoxBlock() override;
        void updatePokedexBlock() override;

        std::unique_ptr<::Pokemon::Pokemon> createBlankPokemon() const override
        {
            return std::make_unique<::Pokemon::Pokemon1RBY>(saveIsJapanese);
        }
        size_t getBoxCount() const noexcept override { return saveIsJapanese ? 8u : 12u; }
        size_t getSlotsPerBox() const noexcept override { return saveIsJapanese ? 30u : 20u; }
        size_t getPartySize() const noexcept override { return 6; }
        GameVersion getGameGroup() const noexcept override { return GameVersion::RBY; }
        /// Yellow when the save proves it, Red otherwise -- claiming the pair, exactly as
        /// gameTitle() does and for the same reason: Red, Green and Blue share an engine and an
        /// SRAM layout with NO version byte anywhere in the 32 KiB, so nothing tells them apart.
        /// Yellow is the one Gen 1 title that IS detectable (it writes a starter/Pikachu-friendship
        /// pair nothing else does), which is what saveIsYellow records.
        GameVersion getGameVersion() const noexcept override
        {
            return saveIsYellow ? GameVersion::YW : GameVersion::RD;
        }
        bool supportsBoxNames() const noexcept override { return false; } // the games have none
        uint32_t getMaxMoney() const noexcept override { return 999999; } // 3-byte BCD ceiling
        size_t getMaxTrainerNameLength() const noexcept override { return saveIsJapanese ? 5u : 7u; }
        size_t getItemPouchCapacity(int pouch) const override
        {
            const PouchType1RBY pouchType = (pouch == static_cast<int>(PouchType1RBY::PCItems)) ? PouchType1RBY::PCItems : PouchType1RBY::Items;
            return static_cast<size_t>(getPouchInfo1RBY(pouchType).maxSlots);
        }

        uint8_t badgeFlags = 0; // 8 badge bits
        uint16_t coins = 0;     // Game Corner coins (BCD, max 9999)
        uint8_t playedHours = 0, playedMinutes = 0, playedSeconds = 0;
        std::string rivalName;
        uint16_t dexOwned = 0, dexSeen = 0; // counts, for the trainer panel

    private:
        std::vector<uint8_t> saveData;
        std::string saveFileName;
        RBYOffsets offsets{};
        bool saveIsJapanese = false;
        bool valid = false;
        bool saveIsYellow = false;

        // Derived geometry. All of it depends on the locale, so none of it is a constant.
        size_t strLen() const noexcept { return saveIsJapanese ? 6u : 11u; }
        size_t boxSlotCount() const noexcept { return saveIsJapanese ? 30u : 20u; }
        size_t boxCount() const noexcept { return saveIsJapanese ? 8u : 12u; }
        /// A stored (box) list: count + capacity+1 markers + bodies + two name blocks.
        size_t boxListSize() const noexcept { return ((strLen() * 2) + 33 + 1) * boxSlotCount() + 2; }
        size_t partyListSize() const noexcept { return ((strLen() * 2) + 44 + 1) * 6 + 2; }
        /// Where box `i` lives in the raw file. Two banks, split down the middle.
        size_t boxRawOffset(size_t boxIndex) const noexcept
        {
            const size_t half = boxCount() / 2;
            return boxIndex < half ? 0x4000 + boxIndex * boxListSize() : 0x6000 + (boxIndex - half) * boxListSize();
        }
        uint8_t currentBoxIndex() const noexcept { return saveData[offsets.currentBoxIndex] & 0x7F; }
        bool boxesInitialized() const noexcept { return (saveData[offsets.currentBoxIndex] & 0x80) != 0; }

        void parseTrainer();
        void parseParty();
        void parseBoxes();
        void parseItems();

        /// Pull entry `index` out of a multi-entry PokeList and wrap it as a single-entry list,
        /// which is the only form Pokemon1RBY accepts. Mirrors PKHeX's PokeList1.Unpack.
        std::unique_ptr<::Pokemon::Pokemon> readListEntry(size_t listOffset, size_t capacity, bool isParty, size_t index) const;
        /// Write `mons` back into a multi-entry PokeList at `listOffset`. Mirrors PokeList1.WriteToList.
        void writeList(size_t listOffset, size_t capacity, bool isParty, const std::vector<::Pokemon::Pokemon *> &mons);
        void readPouch(size_t offset, size_t capacity, std::vector<InventoryItem> &out) const;
        void writePouch(size_t offset, size_t capacity, const std::vector<InventoryItem> &in);
        void refreshChecksum();
    };
}

#endif // TRAINER_TRAINER1_RBY_H
