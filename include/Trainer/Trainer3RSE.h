#ifndef TRAINER_TRAINER3_RSE_H
#define TRAINER_TRAINER3_RSE_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Trainer/Trainer.h"
#include "Trainer/Inventory3RSE.h"
#include "Pokemon/Pokemon3RSE.h"
#include "Encryption/Encryption3RSE.h"

namespace Trainer
{
    // The container is the SAME as FireRed/LeafGreen's and Trainer3FRLG documents it in full: two
    // slots (A@0x0000, B@0xE000) of 14 rotated 0x1000 sectors, each carrying its logical id at
    // +0xFF4 and a save counter at +0xFFC; the active slot is the one whose sector table is complete
    // and whose counter is greater. Logical blocks concatenate their sectors' first 0xF80 bytes:
    //   Small   = id 0        (trainer info, Pokedex, security key)
    //   Large   = id 1..4     (party / money / bag)
    //   Storage = id 5..13    (14 PC boxes; an 80-byte record may STRADDLE a sector boundary)
    //
    // WHAT IS DIFFERENT FROM FIRERED, and it is only ever offsets:
    //
    //   field            Ruby/Sapphire   Emerald    FireRed/LeafGreen
    //   party count      Large 0x234     0x234      Large 0x034
    //   party buffer     Large 0x238     0x238      Large 0x038
    //   money            Large 0x490     0x490      Large 0x290
    //   bag              Large 0x498     0x498      Large 0x298
    //   security key     none            Small 0xAC Small 0xF20
    //   dex seen mirror  Large 0x938     0x988      Large 0x5F8
    //   dex seen mirror  Large 0x3A8C    0x3B24     Large 0x3A18
    //
    // Storage is identical for all of Gen 3 (PKHeX keeps it on the SAV3 base), so boxes, box names
    // at Storage+0x8344 and the current-box byte need no per-game handling at all.
    //
    // RUBY AND SAPPHIRE HAVE NO SECURITY KEY. PKHeX's SaveBlock3SmallRS.SecurityKey is a literal 0,
    // so their money and item counts are plaintext. Emerald keys both. Getting this backwards is
    // SILENT on Ruby -- XOR with 0 is a no-op -- and only shows up as six-figure item counts in
    // Emerald, which is why the layout is resolved once, at load, from the byte that decides it.
    //
    // Offsets are PKHeX's (SAV3, SaveBlock3Small/LargeRS, SaveBlock3Small/LargeE).
    constexpr size_t RSE_SAVE_SIZE = 0x20000;
    constexpr size_t RSE_SLOT_A = 0x00000;
    constexpr size_t RSE_SLOT_B = 0x0E000;
    constexpr size_t RSE_SECTOR_SIZE = 0x1000;
    constexpr size_t RSE_SECTOR_DATA = 0xF80; // usable data bytes per sector (footer follows)
    constexpr size_t RSE_SECTORS = 14;        // sectors per slot
    constexpr size_t RSE_BOX_COUNT = 14;
    constexpr size_t RSE_BOX_SLOTS = 30;
    constexpr size_t RSE_BOX_NAME_OFFSET = 0x8344; // Storage; PKHeX SAV3.GetBoxOffset(COUNT_BOX)
    constexpr size_t RSE_BOX_NAME_BYTES = 9;
    constexpr size_t RSE_BOX_NAME_CHARS = 8;
    /// Every sector's footer carries this, and all 14 must be present for a slot to be usable.
    constexpr uint32_t RSE_SECTOR_MAGIC = 0x08012025;

    class Trainer3RSE : public Trainer
    {
    private:
        std::vector<uint8_t> saveData;         // full raw save (0x20000)
        std::string saveFileName;                // discovered on-disk name (for write-back)
        size_t slotBaseOffset = 0;                 // active slot base offset
        size_t sectorOffset[RSE_SECTORS] = {0}; // absolute offset of the sector holding logical id i
        uint32_t saveSecurityKey = 0;                    // security key (Emerald only; 0 in Ruby/Sapphire)
        Gen3HoennLayout saveLayout = Gen3HoennLayout::RubySapphire;
        // Japanese or English, decided at parse by the width of the trainer-name field -- see
        // parseTrainer(). It is the table every name in this save is read and written through.
        uint8_t languageId = static_cast<uint8_t>(Enums::LanguageID::English);
        bool valid = false;

        // Logical-block base sector ids. Identical to FireRed's -- this part of Gen 3 is shared.
        static constexpr int SMALL_ID = 0;
        static constexpr int LARGE_ID = 1;   // ids 1..4
        static constexpr int STORAGE_ID = 5; // ids 5..13

        // Large-block offsets that do NOT vary between Ruby/Sapphire and Emerald.
        static constexpr size_t OFS_PARTY_COUNT = 0x234;
        static constexpr size_t OFS_PARTY = 0x238;
        static constexpr size_t OFS_MONEY = 0x490;

        void selectActiveSlot();
        void resolveLayout();
        void detectLanguage(size_t smallBlockOffset);
        void parseTrainer();
        void parseParty();
        void parseBoxes();
        void parseBoxNames();
        void parseItems();

        void readBlock(int blockBaseId, size_t logical, uint8_t *destination, size_t length) const;
        void writeBlock(int blockBaseId, size_t logical, const uint8_t *source, size_t length);
        std::unique_ptr<::Pokemon::Pokemon> readMon(int blockBaseId, size_t logical, size_t size) const;

    public:
        explicit Trainer3RSE(std::vector<uint8_t> data, std::string fileName);

        /// True when `bytes` is a Ruby, Sapphire or Emerald save.
        ///
        /// MUST BE TRIED BEFORE FireRed/LeafGreen in the loose-save probe chain: both are 128 KiB
        /// with the same sector layout, and only the word at Small+0xAC separates them.
        static bool detect(const std::vector<uint8_t> &bytes) noexcept;

        void updatePartyBlock() override;
        void updateBoxBlock() override;
        void updateBoxNameBlock() override;
        void updateCurrentBoxBlock() override;
        void updatePokedexBlock() override;
        bool supportsBoxNames() const noexcept override { return true; }
        size_t getMaxBoxNameLength() const noexcept override { return RSE_BOX_NAME_CHARS; }
        bool canStoreBoxName(const std::string &name) const override;
        void updateItemBlock() override;
        void updateTrainerInfoBlock() override;
        uint32_t getMaxMoney() const noexcept override { return 999999; }      // PKHeX SAV3.MaxMoney
        size_t getMaxTrainerNameLength() const noexcept override { return 7; } // PKHeX SAV3.MaxStringLengthTrainer
        int getMaxTrainerNameDigits() const noexcept override { return -1; }   // no digit cap before generation 4

        std::unique_ptr<::Pokemon::Pokemon> createBlankPokemon() const override;

        size_t getBoxCount() const noexcept override { return RSE_BOX_COUNT; }
        size_t getSlotsPerBox() const noexcept override { return RSE_BOX_SLOTS; }
        size_t getPartySize() const noexcept override { return party.size(); }
        GameVersion getGameGroup() const noexcept override { return GameVersion::RSE; }

        /// Emerald names itself; Ruby claims the Ruby/Sapphire pair.
        ///
        /// Emerald is genuinely detectable -- it is the only one of the three with a security key,
        /// and Small+0xAC is that key. Ruby and Sapphire write a 0 there and are otherwise identical
        /// byte for byte, exactly like Red/Blue and Diamond/Pearl, so nothing in the file will ever
        /// say which -- only where the file came from can, which is outside this class. This reports
        /// the first of the pair the way getGroupRepVersion() does everywhere else.
        GameVersion getGameVersion() const noexcept override
        {
            return saveLayout == Gen3HoennLayout::Emerald ? GameVersion::EM : GameVersion::RU;
        }

        /// Which of the two layouts this save is, for the bag and the dex mirrors.
        Gen3HoennLayout layout() const noexcept { return saveLayout; }

        uint8_t language() const noexcept override { return languageId; }

        bool isValid() const noexcept { return valid; }
        const std::string &fileName() const noexcept { return saveFileName; }
        const std::vector<uint8_t> &getSaveData() const noexcept { return saveData; }
        uint32_t securityKey() const noexcept { return saveSecurityKey; }

        void finalizeChecksums();

        /// Apply every edit into the raw save and refresh the sector checksums, then hand back the
        /// bytes to write. The order matters: the block writers all mutate sector DATA, so every one
        /// of them has to run before the checksums are stamped over the result. Reached through
        /// saveExternalSave, which needs the whole image in one call.
        const std::vector<uint8_t> &serialize();
    };
}

#endif // TRAINER_TRAINER3_RSE_H
