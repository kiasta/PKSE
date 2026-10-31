/**
 * The shape is Gen 1's -- a flat SRAM image with no encryption, boxes split across two banks, a
 * summed checksum -- but almost every constant differs, and four of the differences are traps if
 * you carry a Gen 1 habit across:
 *
 *  - THE CHECKSUM IS 16-BIT AND NOT INVERTED. Gen 1's is one byte and `~sum`; Gen 2's is a
 *    wrapping u16 sum over [Trainer1, AccumulatedChecksumEnd] INCLUSIVE, stored twice, and stored
 *    LITTLE endian even though every entity field in the format is big endian.
 *  - THE CURRENT-BOX MIRROR IS THE STALE COPY, the opposite of Gen 1. PKHeX's own comment: "Don't
 *    treat the CurrentBox segment as valid; Stadium ignores it and will de-synchronize it." So
 *    read the BANK, write the BANK, and refresh the mirror afterwards as a courtesy.
 *  - THE BOX STRIDE IS THE LIST LENGTH PLUS 2. Boxes sit every `SIZE_BOX_LIST + 2` bytes, and the
 *    split between the two banks is after box 7 (6 in Japanese), not Gen 1's.
 *  - MONEY IS A 3-BYTE BIG-ENDIAN FIELD read as a u32 shifted right by 8, so the fourth byte
 *    belongs to something else and must be preserved.
 *
 * There is no version byte and no magic number: `detect()` probes two Pokemon-list headers per
 * candidate layout, exactly as PKHeX does, and the ORDER matters -- Japanese GS and Japanese
 * Crystal share one of their two probe offsets and are separated only by the other.
 *
 * SCOPE: International and Japanese. Korean GS is deliberately not claimed -- it needs the
 * double-width StringConverter2KOR table and a second, differently-shaped checksum, and it is the
 * one locale with no test save available. detect() returns false for it rather than mis-parsing a
 * file it half-recognises.
 */
#ifndef TRAINER_TRAINER2GSC_H
#define TRAINER_TRAINER2GSC_H

#include <cstdint>
#include <string>
#include <vector>

#include "Trainer/Trainer.h"
#include "Pokemon/Pokemon2GSC.h"

namespace Trainer
{

    /// The two file sizes PKHeX accepts (SIZE_G2RAW_U / SIZE_G2RAW_J). PKHeX names them after a
    /// locale, but the locale does not follow from the size: every layout is probed at both.
    inline constexpr size_t GEN2_SAVE_SIZE = 0x8000;
    inline constexpr size_t GEN2_SAVE_SIZE_DOUBLED = 0x10000;

    /// Which of the four (version x locale) layouts a file is. There is nothing in the file that
    /// says; it is inferred from where a well-formed Pokemon list sits.
    enum class Gen2Layout : uint8_t
    {
        GS_INTL,
        C_INTL,
        GS_JP,
        C_JP
    };

    /// Every offset SAV2Offsets.cs carries, for one layout. Grouped in one struct so a layout is
    /// a single table row rather than a virtual per field -- there are four layouts and ~20
    /// offsets, and a wrong one is invisible until a save is written.
    struct Gen2Offsets
    {
        bool japanese;
        bool crystal;
        uint16_t trainer1; // trainer block start; also the checksum region start
        uint16_t money;    // 3 bytes, big endian
        uint16_t timePlayed;
        uint16_t rival;
        uint16_t gender; // 0 when the version has none (GS)
        uint16_t currentBoxIndex;
        uint16_t boxNames;
        uint16_t party;
        uint16_t currentBox; // the STALE mirror -- see the header comment
        uint16_t otherCurrentBox;
        uint16_t accumulatedChecksumEnd;
        uint16_t overallChecksumPosition;
        uint16_t overallChecksumPosition2;
        uint16_t pouchTMHM, pouchItem, pouchKey, pouchBall, pouchPC;
    };

    const Gen2Offsets &gen2Offsets(Gen2Layout layout) noexcept;

    /**
     * One region serialize() REFRESHES from elsewhere in the save rather than composing fresh.
     *
     * Gen 2 keeps a second copy of the trainer block -- Crystal one contiguous copy, Gold/Silver
     * five separate pieces -- and the games read it back, so PKHeX writes it (SAV2.GetFinalData)
     * and so does PKSE. **A save whose backup has drifted from its primary therefore does not come
     * back byte for byte when written unedited**: the backup is brought into line with the primary.
     * That is the Gen 2 counterpart of Gen 1's current-box mirror flush and it meets the same bar --
     * confined to these ranges, and incapable of losing anything, because the primary is the source.
     *
     * Exposed rather than left as literals inside writeMirrors() so the writer and anything
     * verifying it read ONE list. Two lists that must agree is the shape that goes wrong silently.
     */
    struct Gen2Mirror
    {
        size_t sourceOffset;
        size_t length;
        size_t destinationOffset;
    };

    class Trainer2GSC final : public Trainer
    {
    public:
        Trainer2GSC(std::vector<uint8_t> raw, std::string path);

        const std::vector<uint8_t> &serialize();
        const std::vector<uint8_t> &getSaveData() const noexcept { return saveData; }
        const std::string &fileName() const noexcept { return savePath; }
        bool isValid() const noexcept { return valid; }
        Gen2Layout layout() const noexcept { return saveLayout; }
        bool japanese() const noexcept { return offsets->japanese; }
        bool crystal() const noexcept { return offsets->crystal; }

        void updatePartyBlock() override;
        void updateBoxBlock() override;
        void updateItemBlock() override;
        void updateTrainerInfoBlock() override;
        void updateBoxNameBlock() override;
        void updateCurrentBoxBlock() override;
        std::unique_ptr<::Pokemon::Pokemon> createBlankPokemon() const override;

        GameVersion getGameGroup() const noexcept override { return GameVersion::GSC; }
        /// Crystal when the save proves it, Gold otherwise -- claiming the pair, exactly as
        /// gameTitle() does. Crystal has its own layout and so is detectable; Gold and Silver
        /// share one and carry no version byte, the same situation as Red/Blue.
        GameVersion getGameVersion() const noexcept override;
        /// Gold/Silver and Crystal are distinguishable (their offset tables differ), unlike Gen 1's
        /// Red/Green/Blue -- so unlike Trainer1RBY this can name the game it actually is.
        std::string gameTitle() const;

        size_t getBoxCount() const noexcept override { return offsets->japanese ? 9 : 14; }
        size_t getSlotsPerBox() const noexcept override { return offsets->japanese ? 30 : 20; }
        size_t getPartySize() const noexcept override { return party.size(); }
        uint32_t getMaxMoney() const noexcept override { return 999999; }
        size_t getMaxTrainerNameLength() const noexcept override { return offsets->japanese ? 5 : 7; }
        int getMaxTrainerNameDigits() const noexcept override { return -1; } // no digit cap before generation 4 (PKHeX)
        bool supportsBoxNames() const noexcept override { return true; }
        size_t getMaxBoxNameLength() const noexcept override { return 8; }
        /// One flag for the whole save, but Gen 2's TM/HM pouch is positionally indexed while the
        /// other four are slot lists. The flag answers for the four; updateItemBlock() special-
        /// cases the TM pouch.
        bool itemsAreIdIndexed() const noexcept override { return false; }
        size_t getItemPouchCapacity(int pouch) const override;
        int getItemMaxCount(int pouch) const override;
        const char *getItemPouchName(int pouch) const override;
        /// Gen 2 box lists are counted and gapless (PKHeX PokeList2), so a vacated slot cannot
        /// exist in the file: this packs each box the way updateBoxBlock() will write it, so the
        /// view shows what a reload will show. Kept unmapped entries keep their place in that order.
        /// True only when something moved.
        bool compactStorage() override;
        /// A box slot whose counted entry was kept as an UnmappedListEntry (see Trainer).
        bool isBoxSlotUnmapped(size_t boxIndex, size_t slotIndex) const noexcept override;
        /// The party slots the kept unmapped party entries are written to (see Trainer).
        std::vector<size_t> getUnmappedPartySlots() const override;
        /// The regions writeMirrors() refreshes -- see Gen2Mirror. Public because verifying
        /// a round trip means knowing which bytes a write is allowed to move.
        std::vector<Gen2Mirror> mirrorRegions() const;

        bool canStoreBoxName(const std::string &name) const override;

        /// True when `bytes` is a Gen 2 save, and if so which layout. Probes two Pokemon-list
        /// headers per candidate, in PKHeX's order.
        static bool detect(const std::vector<uint8_t> &bytes, Gen2Layout *out = nullptr) noexcept;

    private:
        std::vector<uint8_t> saveData;
        std::string savePath;
        bool valid = false;
        Gen2Layout saveLayout = Gen2Layout::GS_INTL;
        const Gen2Offsets *offsets = nullptr;

        size_t nameWidth() const noexcept { return offsets->japanese ? 6 : 11; }
        size_t storedSize() const noexcept { return ::Pokemon::SIZE_2STORED; }
        size_t partySize() const noexcept { return ::Pokemon::SIZE_2PARTY; }
        size_t boxListSize() const noexcept;
        size_t boxOffset(size_t box) const noexcept;
        size_t boxNameWidth() const noexcept { return 9; } // international and Japanese alike

        void init();
        void parse();
        void parseTrainer();
        void parseParty();
        void parseBoxes();
        void parseItems();
        void parseBoxNames();

        /// A counted entry PKSE cannot map reads as null and is copied into `unmappedEntries`.
        void readList(size_t offset, size_t capacity, size_t bodySize, std::vector<std::unique_ptr<::Pokemon::Pokemon>> &out, std::vector<UnmappedListEntry> &unmappedEntries) const;
        void writeList(size_t offset, size_t capacity, size_t bodySize, const std::vector<::Pokemon::Pokemon *> &in, const std::vector<UnmappedListEntry> &unmappedEntries);
        std::vector<UnmappedListEntry> unmappedPartyEntries;
        std::vector<std::vector<UnmappedListEntry>> unmappedBoxEntries;
        void writeChecksums() noexcept;
        void writeMirrors() noexcept;
    };
}

#endif
