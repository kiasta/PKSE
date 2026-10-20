/**
 * THE ULTRA SUN/ULTRA MOON SAVE CONTAINER. Each of the four 3DS groups (XY, ORAS, SM, USUM) has its own
 * container class. The block layout is generated into Blocks7USUM.h as rows of BlockEntry3DS
 * (BlockTable.h); what this class supplies is the indices into that table and its own CRC.
 *
 * The file ends with a BEEF metadata chunk in its last 0x200 bytes, and block `id`'s checksum
 * lives at `fileSize - 0x200 + 0x14 + id*8 + 6`. Refreshing a block means recomputing its CRC over
 * its own data and storing it there -- there is no mirror, no counter and no partition, so an
 * unedited round trip is exact by construction.
 *
 * THE ONE THING THAT WILL BITE: GEN 6 USES CRC16-CCITT, GEN 7 USES CRC16Invert. They are different
 * algorithms of the same width over the same bytes, so using the wrong one produces a perfectly
 * well-formed 16-bit number that the game rejects. This container's `blockCrc()` is CRC16Invert, and
 * it carries no code for the other one: that is why the 3DS groups are separate classes rather
 * than one container with a generation switch.
 *
 * GEN 7 ALSO SIGNS ITS SAVES: a MemeCrypto signature over the checksum table, stored in block 36.
 * Any edit moves the checksum table, so sign() runs last in serialize(), after every checksum.
 *
 * Three layout facts that differ from every earlier generation:
 *
 *  - THERE ARE 31 BOXES IN GEN 6 AND 32 IN GEN 7, not 24 or 18.
 *  - THE PARTY COUNT IS AFTER THE RECORDS, at `party + 6*SIZE_PARTY`, not before them.
 *  - BOX NAMES ARE 0x22 BYTES (17 UTF-16 units) and the box layout's fields sit at different
 *    offsets in the two generations -- Gen 6's current box is at 0x43F, Gen 7's at 0x5E3.
 */
#ifndef TRAINER_TRAINER7_USUM_H
#define TRAINER_TRAINER7_USUM_H

#include <cstdint>
#include <string>
#include <vector>

#include "Trainer/Trainer.h"
#include "Trainer/Inventory7USUM.h"
#include "Pokemon/Pokemon7USUM.h"
#include "Trainer/Blocks7USUM.h"
#include "Pokemon/Pokemon6XY.h"
#include "Pokemon/Pokemon7SM.h"

namespace Trainer
{

    class Trainer7USUM final : public Trainer
    {
    public:
        Trainer7USUM(std::vector<uint8_t> raw, std::string path);

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

        GameVersion getGameGroup() const noexcept override { return GameVersion::USUM; }
        size_t getBoxCount() const noexcept override { return 32; }

        size_t getSlotsPerBox() const noexcept override { return 30; }
        size_t getPartySize() const noexcept override { return party.size(); }
        uint32_t getMaxMoney() const noexcept override { return 9999999; }
        size_t getMaxTrainerNameLength() const noexcept override { return 12; }
        /// PKHeX TrainerNameVerifier: five digits from Gen 6 on, four before.
        int getMaxTrainerNameDigits() const noexcept override { return 5; }
        bool supportsBoxNames() const noexcept override { return true; }
        /// The field is 0x22 bytes = 17 UTF-16 units, so 16 characters plus a terminator.
        size_t getMaxBoxNameLength() const noexcept override { return 16; }
        bool itemsAreIdIndexed() const noexcept override { return false; }
        size_t getItemPouchCapacity(int pouch) const override { return (pouch >= 0 && pouch < POUCH_COUNT7_USUM) ? POUCHES7_USUM[pouch].slots : 0; }
        int getItemMaxCount(int pouch) const override { return (pouch >= 0 && pouch < POUCH_COUNT7_USUM) ? POUCHES7_USUM[pouch].maxCount : 0; }
        const char *getItemPouchName(int pouch) const override { return (pouch >= 0 && pouch < POUCH_COUNT7_USUM) ? POUCHES7_USUM[pouch].name : nullptr; }
        bool compactStorage() override { return false; }
        bool canStoreBoxName(const std::string &name) const override;

        // Concrete here rather than virtual: this class serves exactly one layout.
        const BlockEntry3DS *blocks() const noexcept { return BLOCKS_7USUM; }
        size_t blockCount() const noexcept { return BLOCK_COUNT_7USUM; }
        size_t fileSize() const noexcept { return SAVE_SIZE_7USUM; }
        size_t blkItem() const noexcept { return BLOCK_ITEM_7USUM; }
        size_t blkBoxLayout() const noexcept { return BLOCK_BOX_LAYOUT_7USUM; }
        size_t blkStatus() const noexcept { return BLOCK_STATUS_7USUM; }
        size_t blkMisc() const noexcept { return BLOCK_MISC_7USUM; }
        size_t blkParty() const noexcept { return BLOCK_PARTY_7USUM; }
        size_t blkBox() const noexcept { return BLOCK_BOX_7USUM; }
        uint16_t blockCrc(const uint8_t *bytes, size_t byteCount) const noexcept;

        /// Bytes inside a block that must be ZEROED before that block's checksum is computed.
        ///
        /// Gen 7 has exactly one such region and this is measured, not assumed: SM and USUM store
        /// a MemeCrypto signature at `block36 + 0x100`, 0x80 bytes long, and the checksum recorded
        /// for block 36 is the one taken with that region blank. SM stores 0x44BE and USUM
        /// 0xE25C, which is what the CRC gives blanked and NOT what it gives over the block as it
        /// sits on disk.
        ///
        /// Returns false (nothing to blank) for every other block and generation.
        bool checksumBlank(size_t blockId, size_t &blockOffset, size_t &blockLength) const noexcept;

        /// Re-signs the save. Called LAST, after every checksum, because the signature
        /// covers them.
        void sign() noexcept;

        /// PKHeX SaveUtil.IsG6XY / IsG7SM and siblings, exactly: the file size plus the BEEF magic.
        /// Nothing stricter -- PKSE accepts what PKHeX accepts.
        static bool detectGen67(const std::vector<uint8_t> &bytes, size_t size) noexcept;

        /// TurtleSalmonSave -- block 36 in both SM and USUM. Holds the MemeCrypto
        /// signature at +0x100, and nothing else in PKSE cares about its contents.
        static constexpr size_t BLK_MEMECRYPTO = 36;
        static constexpr size_t SIG_OFS = 0x100; // within block 36 (PKHeX SaveFileSignatureOffset)
        static constexpr size_t SIG_LEN = 0x80;

        /// How much of the checksum table the signature covers. PKHeX
        /// ChecksumSignatureLength -- a constant per game, and signing over the wrong
        /// span produces a valid-looking, rejected save.
        size_t signatureSpan() const noexcept { return 0x150; }

        static bool detect(const std::vector<uint8_t> &bytes) noexcept
        {
            return detectGen67(bytes, SAVE_SIZE_7USUM);
        }

        /// Which of Ultra Sun and Ultra Moon this save is, read from MyStatus's own
        /// game byte (+0x04).
        GameVersion getGameVersion() const noexcept override;

    protected:
        std::vector<uint8_t> saveData;
        std::string savePath;
        bool valid = false;
        /// The packed word each parsed item was read from, in step with itemSlot, so the
        /// free-space sort index and the new flag survive a write.
        std::vector<std::vector<uint32_t>> itemRaw;
        /// A verbatim copy of the first empty box slot found at load: this game's own blank
        /// record, used when a slot has to be cleared. See parseBoxes().
        std::vector<uint8_t> emptyTemplate;
        /// The pouch slot each parsed item came out of. Gen 7 pouches are positional, not packed,
        /// so an item that was in the bag when it was read is written back to its own slot.
        std::vector<std::vector<ItemOrigin>> itemSlot;

        const BlockEntry3DS &blk(size_t blockIndex) const noexcept { return blocks()[blockIndex]; }
        uint8_t *at(size_t blockIndex) noexcept { return &saveData[blk(blockIndex).offset]; }
        const uint8_t *at(size_t blockIndex) const noexcept { return &saveData[blk(blockIndex).offset]; }
        size_t entitySize(bool partySlot) const noexcept;
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
    };

}

#endif
