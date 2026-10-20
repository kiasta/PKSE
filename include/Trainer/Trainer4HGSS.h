/**
 * THE CONTAINER IS THE HARD PART OF GEN 4, and it is unlike anything else in PKSE.
 *
 * A Gen 4 save is 0x80000 bytes: TWO 0x40000 partitions, and the game alternates between them.
 * Inside each partition sit two independently-checksummed blocks -- General (trainer, party, bag,
 * flags) and Storage (18 boxes, names, wallpapers). The two are chosen INDEPENDENTLY: it is
 * entirely normal for the newest General block to be in partition 0 while the newest Storage block
 * is in partition 1.
 *
 * Selection compares a pair of counters in each block's footer, 0x14 from the block's end:
 *
 *     blockEnd - 0x14   u32  major counter     <- compared first
 *     blockEnd - 0x10   u32  minor counter     <- tiebreak
 *     blockEnd - 0x0C   u32  block size        == the block's own length (this is what identifies
 *                                                 DP from Pt from HGSS, since the sizes differ)
 *     blockEnd - 0x08   u32  SDK magic         0x20060623, or 0x20070903 for Korean
 *     blockEnd - 0x02   u16  CRC16-CCITT over [start, blockEnd - FooterSize)
 *
 * TWO CONSTANTS, TWO QUESTIONS. The counters are always 0x14 from the end; FooterSize is how much
 * of the tail the CRC EXCLUDES and is 0x14 for DP/Pt but 0x10 for HGSS. Unifying them corrupts
 * every HeartGold save.
 *
 * 0xFFFFFFFF IS "NEVER WRITTEN", NOT "NEWEST". A fresh save has one partition untouched, and its
 * counter reads larger than any real one -- a naive `a > b` picks the uninitialised garbage and
 * loads an empty save from a real file. compareCounters() reproduces PKHeX's handling exactly.
 *
 * SAVING writes back into the block that was LOADED (whichever partition that was) and refreshes
 * that block's CRC, plus a backup block's CRC only if that backup is initialised. It does NOT flip
 * partitions or bump counters -- PKHeX does not either, and not doing so is what keeps an unedited
 * save byte-identical, which is the bar a save editor has to clear.
 *
 * ONE container per SAVE-FORMAT GROUP, the way every Switch title has its own. PK4 is one record
 * format across all three Gen 4 groups, but the SAVES differ -- block sizes, box layout, bag base --
 * and the group is what the rest of PKSE dispatches on, so each group carries its own container and
 * its own record class.
 */
#ifndef TRAINER_TRAINER4_HGSS_H
#define TRAINER_TRAINER4_HGSS_H

#include <cstdint>
#include <string>
#include <vector>

#include "Trainer/Inventory.h"
#include "Trainer/Trainer.h"
#include "Trainer/Inventory4HGSS.h"
#include "Pokemon/Pokemon4HGSS.h"

namespace Trainer
{

    class Trainer4HGSS final : public Trainer
    {
    public:
        /// Every Gen 4 save is 0x80000: two 0x40000 partitions the game alternates between.
        /// Class members rather than namespace constants -- at namespace scope they collided the
        /// moment two Gen 4 group headers met in one translation unit, which is every file that
        /// dispatches over the loose formats.
        static constexpr size_t GEN4_SAVE_SIZE = 0x80000;
        static constexpr size_t GEN4_PARTITION = 0x40000;
        static constexpr uint32_t GEN4_MAGIC_INTL = 0x20060623u;
        static constexpr uint32_t GEN4_MAGIC_KOR = 0x20070903u;
        /// Bytes per box name -- 40, holding 20 UTF-16 units of which the game writes at most 8
        /// characters (PKHeX SAV4Sinnoh/SAV4HGSS BOX_NAME_LEN).
        static constexpr size_t GEN4_BOX_NAME_LEN = 40;

        Trainer4HGSS(std::vector<uint8_t> raw, std::string path);

        /// Applies every edit, refreshes the CRCs, and returns the whole 0x80000 image.
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

        GameVersion getGameGroup() const noexcept override { return GameVersion::HGSS; }
        /// HeartGold, claiming the pair. Same reasoning as Diamond/Pearl: there is no game byte
        /// anywhere in a Gen 4 save to tell HeartGold from SoulSilver.
        GameVersion getGameVersion() const noexcept override { return GameVersion::HG; }

        size_t getBoxCount() const noexcept override { return 18; }
        size_t getSlotsPerBox() const noexcept override { return 30; }
        size_t getPartySize() const noexcept override { return party.size(); }
        uint32_t getMaxMoney() const noexcept override { return 999999; }
        size_t getMaxTrainerNameLength() const noexcept override { return 7; }
        /// PKHeX TrainerNameVerifier: four digits in gens 4-5, five from generalBlock 6.
        int getMaxTrainerNameDigits() const noexcept override { return 4; }
        bool supportsBoxNames() const noexcept override { return true; }
        size_t getMaxBoxNameLength() const noexcept override { return 8; }
        bool itemsAreIdIndexed() const noexcept override { return false; }
        size_t getItemPouchCapacity(int pouch) const override { return (pouch >= 0 && pouch < POUCH_COUNT4_HGSS) ? POUCHES4_HGSS[pouch].slots : 0; }
        int getItemMaxCount(int pouch) const override { return (pouch >= 0 && pouch < POUCH_COUNT4_HGSS) ? POUCHES4_HGSS[pouch].maxCount : 0; }
        const char *getItemPouchName(int pouch) const override { return (pouch >= 0 && pouch < POUCH_COUNT4_HGSS) ? POUCHES4_HGSS[pouch].name : nullptr; }
        /// Gen 4 storage is POSITIONAL -- every slot has a fixed home and an empty one stays empty.
        /// Re-packing would shuffle Pokemon the player arranged deliberately.
        bool compactStorage() override { return false; }
        bool canStoreBoxName(const std::string &name) const override;

        // Concrete here rather than virtual: this class serves exactly one layout, so
        // there is nothing to dispatch on.
        size_t generalSize() const noexcept { return 0xF628; }
        size_t storageSize() const noexcept { return 0x12310; }
        /// HGSS leaves 0xD8 of slack between the two blocks; DP and Pt have none.
        size_t storageStart() const noexcept { return 0xF628 + 0xD8; }
        /// 0x10 here, 0x14 in DP/Pt. This is the CRC exclusion, NOT the counter offset.
        size_t footerSize() const noexcept { return 0x10; }
        size_t ofsTrainer() const noexcept { return 0x64; }
        size_t ofsParty() const noexcept { return 0x98; }
        size_t ofsBagBase() const noexcept { return 0x644; }
        // SAV4HGSS: box data starts at 0, each box PADDED to 0x1000, currentBox AFTER the boxes.
        size_t ofsBox(size_t boxIndex) const noexcept { return boxIndex * 0x1000; }
        size_t ofsBoxNames() const noexcept { return 0x12008; }
        size_t ofsCurrentBox() const noexcept { return 18 * 0x1000; }

        /// True when `bytes` is a Gen 4 save whose General block is `gsize`. Mirrors PKHeX
        /// SaveUtil.IsValidGeneralFooter2: it checks the block in the SECOND partition, because a
        /// game's first save goes to the latter half of the binary.
        static bool detectGeneral(const std::vector<uint8_t> &bytes, size_t generalBlockSize) noexcept;

        static bool detect(const std::vector<uint8_t> &bytes) noexcept
        {
            return detectGeneral(bytes, 0xF628);
        }

    protected:
        std::vector<uint8_t> saveData;
        std::string savePath;
        bool valid = false;
        size_t generalBlockOffset = 0, storageBlockOffset = 0;       // absolute offsets of the ACTIVE blocks
        size_t generalBackupOffset = 0, storageBackupOffset = 0; // the other partition's copies
        /// The pouch slot each item came out of -- Gen 4 bags are positional, not packed.
        std::vector<std::vector<ItemOrigin>> itemSlot;

        uint8_t *generalBlock() noexcept { return &saveData[generalBlockOffset]; }
        const uint8_t *generalBlock() const noexcept { return &saveData[generalBlockOffset]; }
        uint8_t *storageBlock() noexcept { return &saveData[storageBlockOffset]; }
        const uint8_t *storageBlock() const noexcept { return &saveData[storageBlockOffset]; }

        /// Selects the live blocks and parses them. Safe from the constructor now that the
        /// geometry is concrete -- when it was virtual, calling one during the base constructor
        /// aborted the process with "pure virtual method called".
        void init();

        /// Builds a PK4 from an on-disk record, growing a BOX record to party size. See the
        /// definition -- a stored-size PK4 reports level 0 and cannot safely reach the party.
        std::unique_ptr<::Pokemon::Pokemon> makeEntity(const uint8_t *bytes, size_t byteCount) const;

        void selectBlocks() noexcept;
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
