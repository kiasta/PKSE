/**
 * Trainer5BW.h - the Black and White save container.
 *
 * AFTER GEN 4 THIS IS A RELIEF. No partitions, no counters, no "which half is newer". A Gen 5
 * save is a flat list of fixed blocks (BlockEntryNDSs.h, generated from PKHeX), each carrying a
 * CRC16-CCITT over its own data. Round-tripping an unedited save is therefore trivially exact --
 * there is nothing to choose and nothing to bump.
 *
 * The one rule that bites: EVERY BLOCK'S CRC IS STORED TWICE. Once just past the block's data,
 * once in the trailing checksum block. Writing only one leaves the save looking corrupt to the
 * game. And the checksum block CHECKSUMS ITSELF, so it must be stamped last, after every mirror
 * has landed inside it.
 *
 * ONE container per SAVE-FORMAT GROUP, the way every Switch title has its own. Black/White and
 * Black 2/White 2 differ in the block table and the main size (0x24000 vs 0x26000); the block
 * INDICES the save layer reaches for are the same in both, so what a group's container carries is
 * its own table. The file itself is 0x80000 either way; everything past the main size is slack.
 *
 * Two layout notes that are easy to get wrong:
 *
 *  - A BOX BLOCK IS 0xFF0 LONG BUT THE STRIDE IS 0x1000. The 16 bytes of slack at the end of each
 *    box are OUTSIDE the CRC, so corrupting them fails no checksum -- it just shows up as a
 *    round-trip diff. Never write past a box's 30 records.
 *  - THE BOX-NAME STRIDE IS 0x28 BUT ONLY 0x14 IS THE NAME. The other 20 bytes belong to
 *    something else; slice 0x14 and leave the rest alone.
 */
#ifndef TRAINER_TRAINER5_BW_H
#define TRAINER_TRAINER5_BW_H

#include <cstdint>
#include <string>
#include <vector>

#include "Trainer/Inventory.h"
#include "Trainer/Trainer.h"
#include "Trainer/Inventory5BW.h"
#include "Trainer/Blocks5BW.h"
#include "Pokemon/Pokemon5BW.h"

namespace Trainer
{

    class Trainer5BW final : public Trainer
    {
    public:
        /// The file is 0x80000 in both Gen 5 groups; everything past mainSize() is slack. A class
        /// member rather than a namespace constant, so the two group headers can meet.
        static constexpr size_t GEN5_FILE_SIZE = 0x80000;

        Trainer5BW(std::vector<uint8_t> raw, std::string path);

        const std::vector<uint8_t> &serialize();
        const std::vector<uint8_t> &getSaveData() const noexcept { return saveData; }
        const std::string &fileName() const noexcept { return savePath; }
        bool isValid() const noexcept { return valid; }

        void updatePartyBlock() override;
        void updateBoxBlock() override;
        void updateItemBlock() override;
        void updateTrainerInfoBlock() override;
        void updateBoxNameBlock() override;
        void updateCurrentBoxBlock() override;
        std::unique_ptr<::Pokemon::Pokemon> createBlankPokemon() const override;

        GameVersion getGameGroup() const noexcept override { return GameVersion::BW; }

        size_t getBoxCount() const noexcept override { return 24; }
        size_t getSlotsPerBox() const noexcept override { return 30; }
        size_t getPartySize() const noexcept override { return party.size(); }
        uint32_t getMaxMoney() const noexcept override { return 9999999; }
        size_t getMaxTrainerNameLength() const noexcept override { return 7; }
        int getMaxTrainerNameDigits() const noexcept override { return 4; }
        bool supportsBoxNames() const noexcept override { return true; }
        /// Gen 5 is the outlier: every other generation here caps box names at 8. The field is
        /// 0x14 bytes = 10 UTF-16 units, so 9 characters plus a terminator fit.
        size_t getMaxBoxNameLength() const noexcept override { return 9; }
        bool itemsAreIdIndexed() const noexcept override { return false; }
        size_t getItemPouchCapacity(int pouch) const override { return (pouch >= 0 && pouch < POUCH_COUNT5_BW) ? POUCHES5_BW[pouch].slots : 0; }
        int getItemMaxCount(int pouch) const override { return (pouch >= 0 && pouch < POUCH_COUNT5_BW) ? POUCHES5_BW[pouch].maxCount : 0; }
        const char *getItemPouchName(int pouch) const override { return (pouch >= 0 && pouch < POUCH_COUNT5_BW) ? POUCHES5_BW[pouch].name : nullptr; }
        bool compactStorage() override { return false; }
        bool canStoreBoxName(const std::string &name) const override;

        // Concrete here rather than virtual: this class serves exactly one layout. The
        // block INDICES the save layer reaches for are the same in both Gen 5 groups; it
        // is where those blocks SIT that differs, which is what the table carries.
        const BlockEntryNDS *blocks() const noexcept { return BLOCKS_5BW; }
        size_t blockCount() const noexcept { return BLOCK_COUNT_5BW; }
        size_t mainSize() const noexcept { return SAVE_SIZE_5BW; }

        /// Mirrors PKHeX SaveUtil.IsValidFooter5: the trailing checksum block checksums itself,
        /// so validating it identifies the layout. Gen 4 and Gen 5 saves are both 0x80000 bytes,
        /// which is why this has to be a real probe and not a size test.
        static bool detectGen5(const std::vector<uint8_t> &bytes, size_t mainSize, size_t infoLen) noexcept;

        static bool detect(const std::vector<uint8_t> &bytes) noexcept
        {
            return detectGen5(bytes, SAVE_SIZE_5BW, CHECKSUM_BLOCK_LENGTH_5BW);
        }

    protected:
        std::vector<uint8_t> saveData;
        std::string savePath;
        bool valid = false;
        /// The pouch slot each item came out of -- Gen 5 bags are positional, not packed.
        std::vector<std::vector<ItemOrigin>> itemSlot;

        const BlockEntryNDS &blk(size_t blockIndex) const noexcept { return blocks()[blockIndex]; }
        uint8_t *at(size_t blockIndex) noexcept { return &saveData[blk(blockIndex).offset]; }
        const uint8_t *at(size_t blockIndex) const noexcept { return &saveData[blk(blockIndex).offset]; }
        /// Index of the Misc block, which holds money at its offset 0. Block 52 in both games --
        /// the same index, a different absolute offset, which is exactly what the table is for.
        static constexpr size_t BLK_MISC = 52;

        /// Builds a PK5 from an on-disk record, growing a BOX record to party size.
        std::unique_ptr<::Pokemon::Pokemon> makeEntity(const uint8_t *bytes, size_t byteCount) const;

        void init();
        void writeChecksums() noexcept;
        void parse();

    private:
        void parseTrainer();
        void parseParty();
        void parseBoxes();
        void parseItems();
        void parseBoxNames();

    public:
        /// Which of Black and White this save is, read from the save's own game byte
        /// (PlayerData5 +0x1F). Gen 5 is the first generation whose save names its own game.
        GameVersion getGameVersion() const noexcept override;
    };

}

#endif
