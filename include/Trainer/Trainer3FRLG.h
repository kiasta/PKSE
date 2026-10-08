#ifndef TRAINER_TRAINER3_FRLG_H
#define TRAINER_TRAINER3_FRLG_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Trainer/Trainer.h"
#include "Trainer/Inventory3FRLG.h"
#include "Pokemon/Pokemon3FRLG.h"
#include "Encryption/Encryption3FRLG.h"

namespace Trainer
{
    // Two slots (A@0x0000, B@0xE000) of 14 rotated 0x1000 sectors. Each sector carries its logical id
    // in a footer (+0xFF4) and a save counter (+0xFFC); the ACTIVE slot is the one holding all 14 ids, or
    // when both do, the one whose id-0 sector has the greater counter. Logical blocks are concatenations
    // of their sectors' first 0xF80 data bytes:
    //   Small   = id 0        (trainer info + security key)
    //   Large   = id 1..4     (party / items / money)
    //   Storage = id 5..13    (14 PC boxes; an 80-byte box pokemon may STRADDLE a sector boundary)
    // Money + bag item-counts are XOR-obfuscated with the security key (Small+0xF20). Trainer3FRLG keeps
    // the full raw save and maps logical block offsets -> absolute file offsets through the active slot's
    // sector table, so straddling is handled transparently.
    constexpr size_t FRLG_SAVE_SIZE = 0x20000;
    constexpr size_t FRLG_SLOT_A = 0x00000;
    constexpr size_t FRLG_SLOT_B = 0x0E000;
    constexpr size_t FRLG_SECTOR_SIZE = 0x1000;
    /// Sector footer magic at +0xFF8. Identical across all five GBA games -- Trainer3RSE spells it
    /// RSE_SECTOR_MAGIC and the two must stay the same number; it is one fact about Gen 3, not two.
    constexpr uint32_t FRLG_SECTOR_MAGIC = 0x08012025;
    constexpr size_t FRLG_SECTOR_DATA = 0xF80; // usable data bytes per sector (footer follows)
    constexpr size_t FRLG_SECTORS = 14;        // sectors per slot
    constexpr size_t FRLG_BOX_COUNT = 14;
    constexpr size_t FRLG_BOX_SLOTS = 30;
    // Box names: 14 x 9 bytes at Storage+0x8344. Gen 3 text, 8 usable chars + a 0xFF terminator
    // (PKHeX SAV3.COUNT_BOXNAME = 8 + 1).
    constexpr size_t FRLG_BOX_NAME_OFFSET = 0x8344;
    constexpr size_t FRLG_BOX_NAME_BYTES = 9;
    constexpr size_t FRLG_BOX_NAME_CHARS = 8;

    class Trainer3FRLG : public Trainer
    {
    private:
        std::vector<uint8_t> saveData;          // full raw save (0x20000)
        std::string saveFileName;                 // discovered on-disk name (for write-back)
        size_t slotBaseOffset = 0;                  // active slot base offset
        size_t sectorOffset[FRLG_SECTORS] = {0}; // absolute offset of the sector holding logical id i
        uint32_t saveSecurityKey = 0;                     // security key (Small+0xF20)
        // Japanese or English, decided at parse by the width of the trainer-name field -- see
        // parseTrainer(). It is the table every name in this save is read and written through.
        uint8_t languageId = static_cast<uint8_t>(Enums::LanguageID::English);
        bool valid = false;

        // Logical-block base sector ids.
        static constexpr int SMALL_ID = 0;
        static constexpr int LARGE_ID = 1;   // ids 1..4
        static constexpr int STORAGE_ID = 5; // ids 5..13

        void selectActiveSlot();
        void detectLanguage(size_t smallBlockOffset);
        void parseTrainer();
        void parseParty();
        void parseBoxes();
        void parseBoxNames();
        void parseItems();

        // Map a logical offset within a block (whose first sector is logical id `blockBaseId`) to the
        // absolute file offset, honoring sector rotation; read/write cross sector boundaries byte-wise.
        void readBlock(int blockBaseId, size_t logical, uint8_t *destination, size_t length) const;
        void writeBlock(int blockBaseId, size_t logical, const uint8_t *source, size_t length);
        // Decrypt a PK3 record of `size` bytes at a logical block offset into a Pokemon3FRLG.
        std::unique_ptr<::Pokemon::Pokemon> readMon(int blockBaseId, size_t logical, size_t size) const;

    public:
        // `fileName` is the save's on-disk basename in the backup dir (e.g. "FireRed_e.sav"); it is
        // reused verbatim when writing back, so we round-trip whatever name we loaded rather than
        // leaving a second save beside it under a name of our own choosing.
        explicit Trainer3FRLG(std::vector<uint8_t> data, std::string fileName);

        void updatePartyBlock() override;
        void updateBoxBlock() override;
        void updateBoxNameBlock() override;
        void updateCurrentBoxBlock() override; // u8 at Storage logical offset 0
        void updatePokedexBlock() override;    // seen (x3) + caught flag arrays
        bool supportsBoxNames() const noexcept override { return true; }
        size_t getMaxBoxNameLength() const noexcept override { return FRLG_BOX_NAME_CHARS; }
        bool canStoreBoxName(const std::string &name) const override;
        void updateItemBlock() override;
        void updateTrainerInfoBlock() override;                                // money / OT name
        uint32_t getMaxMoney() const noexcept override { return 999999; }      // PKHeX SAV3.MaxMoney
        size_t getMaxTrainerNameLength() const noexcept override { return 7; } // PKHeX SAV3.MaxStringLengthTrainer
        int getMaxTrainerNameDigits() const noexcept override { return -1; }   // no digit cap before generation 4

        std::unique_ptr<::Pokemon::Pokemon> createBlankPokemon() const override;

        size_t getBoxCount() const noexcept override { return FRLG_BOX_COUNT; }
        size_t getSlotsPerBox() const noexcept override { return FRLG_BOX_SLOTS; }
        size_t getPartySize() const noexcept override { return party.size(); }
        GameVersion getGameGroup() const noexcept override { return GameVersion::FRLG; }
        /// FireRed, claiming the pair. A GBA save cannot say which of the two it is -- PKHeX's
        /// SAV3FRLG has Version as a settable property defaulting to FR for exactly this reason.
        ///
        /// An installed title's id does say, and every caller that needs the real answer for one
        /// reads it from Enums::getGameVersion(titleId). A GBA file has no id, so it is named as the
        /// pair, exactly as a Ruby/Sapphire file is.
        GameVersion getGameVersion() const noexcept override { return GameVersion::FR; }

        /// True when these bytes are a FireRed/LeafGreen save, judged from CONTENT alone.
        ///
        /// The sibling of Trainer3RSE::detect(), and the same test with its last comparison
        /// inverted: fourteen well-formed sectors in either slot, then the fixed 1 at Small+0xAC
        /// that FR/LG write and Ruby/Sapphire/Emerald do not.
        ///
        /// Two callers. The loose probe chain (LOOSE_FORMATS), which opens a GBA file. And the save
        /// picker, for a TITLE ID it does not recognise: FireRed and LeafGreen ship a separate title
        /// per language, so a hand-kept id list silently hides every localisation nobody has
        /// reported yet. See the note on Enums::getGameVersion.
        static bool detect(const std::vector<uint8_t> &bytes) noexcept;

        uint8_t language() const noexcept override { return languageId; }

        bool isValid() const noexcept { return valid; }
        const std::string &fileName() const noexcept { return saveFileName; }
        const std::vector<uint8_t> &getSaveData() const noexcept { return saveData; }
        uint32_t securityKey() const noexcept { return saveSecurityKey; }

        // Recompute all 14 active-slot sector checksums into their footers (call before writing to disk).
        void finalizeChecksums();

        /// Apply every edit into the raw save and refresh the sector checksums, then hand back the
        /// bytes to write. Both writers need it: saveTrainerInfoFRLG for an installed title, and
        /// saveExternalSave for a GBA file.
        const std::vector<uint8_t> &serialize();
    };
}

#endif // TRAINER_TRAINER3_FRLG_H
