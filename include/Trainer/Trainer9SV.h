/**
 * Generation 9 Scarlet/Violet Trainer/Save File Data Management
 *
 * Trainer9SV implements Scarlet/Violet-specific logic for:
 * - PK9 Pokemon storage (party and boxes)
 * - Gen 9 block keys
 * - Gen 9 encryption (encryptArray9/decryptArray9)
 * - Gen 9-specific save file structure
 */

#ifndef TRAINER_TRAINER9SV_H
#define TRAINER_TRAINER9SV_H

#include <cstring>

#include "Trainer/Trainer.h"
#include "Pokemon/Pokemon9SV.h"
#include "Encryption/Encryption9SV.h"

using namespace Pokemon;
using namespace Encryption;

namespace Trainer
{

    constexpr size_t MY_STATUS9_SV = 0xE3E89BD1;   // Trainer Details
    constexpr size_t TEAM_INDEXES9_SV = 0x33F39467; // battle-team box pointers (TeamIndexes8)
    constexpr size_t PARTY9_SV = 0x3AA1A9AD;
    constexpr size_t MONEY9_SV = 0x4F35D0DD;       // Money (u32)
    constexpr size_t PLAY_TIME9_SV = 0xEDAFF794;
    constexpr size_t ITEM9_SV = 0x21C9BD44;
    constexpr size_t BOX9_SV = 0x0d66012c;         // Box Data
    constexpr size_t BOX_LAYOUT9_SV = 0x19722c89;  // Box Names
    constexpr size_t CURRENT_BOX9_SV = 0x017C3CBB; // Current box index ("U32 Box Index")

    // DLC-presence markers, read only by detectSaveRevision() (never written).
    constexpr size_t BLUEBERRY_POINTS9_SV = 0x66A33824; // u32 BP -- exists only with The Indigo Disk
    constexpr size_t TERA_RAID_DLC9_SV = 0x100B93DA;    // Kitakami + Blueberry raid dens (2 x 0xC80)

    // Additional blocks (for future use)
    // Pokedex. TWO blocks: the original Paldea one and the Kitakami one added by the DLC. From game
    // version 2.0.1 the developers dummied out the Paldea block and use Kitakami exclusively, so the
    // rule is "if the Kitakami block has data, it IS the dex" (PKHeX Zukan9). Their entry layouts
    // differ (0x18 vs 0x20), so which one is live decides how an entry is written.
    constexpr size_t ZUKAN9_SV_PALDEA = 0x0DEAAEBD;
    constexpr size_t ZUKAN9_SV_KITAKAMI = 0xF5D7C0E2;

    // Generation 9 constants
    constexpr size_t BOX_COUNT9_SV = 32;         // Number of boxes in Scarlet/Violet
    constexpr size_t BOX_NAME_LENGTH9_SV = 0x22; // 34 bytes per box name (UTF-16LE)

    class Trainer9SV final : public Trainer
    {
    public:

        // Scarlet/Violet (PK9). Shares the Gen 9 SCBlock save + entity format with Legends: Z-A, but
        // S/V PACKS its box/party slots (no inter-slot gap) where Z-A gaps them — see the slot-stride
        // members below. Trainer9LZA is the Z-A counterpart.
        explicit Trainer9SV(std::vector<Block> blocks) : Trainer(std::move(blocks))
        {
            party.reserve(MAX_PARTY_SLOTS);
            boxes.resize(BOX_COUNT9_SV);
            boxNames.resize(BOX_COUNT9_SV);

            for (const auto &block : this->blocks)
            {
                parseBlock(block);
            }
            detectSaveRevision(); // by block presence -- S/V stores no revision value
        }

        ~Trainer9SV() override = default;

        Trainer9SV(const Trainer9SV &) = delete;
        Trainer9SV &operator=(const Trainer9SV &) = delete;

        Trainer9SV(Trainer9SV &&) noexcept = default;
        Trainer9SV &operator=(Trainer9SV &&) noexcept = default;

        /**
         * Updates the PARTY_KEY block with modified Pokemon data.
         * Uses Gen 9 encryption (encryptArray9).
         */
        void updatePartyBlock() override;

        /**
         * Updates the BOX_KEY block with modified Pokemon data.
         * Uses Gen 9 encryption (encryptArray9).
         */
        void updateBoxBlock() override;
        void updateBoxNameBlock() override;
        void updateCurrentBoxBlock() override;
        bool supportsBoxNames() const noexcept override { return true; }
        /// A member of a battle team (PKHeX GetBoxSlotFlags: overwrite-protected).
        bool isBoxSlotLocked(size_t boxIndex, size_t slotIndex) const noexcept override { return battleTeamBlockHoldsSlot(TEAM_INDEXES9_SV, boxIndex, slotIndex); }
        size_t getMaxBoxNameLength() const noexcept override { return BOX_NAME_LENGTH9_SV / 2 - 1; }

        void updateItemBlock() override;
        void updateTrainerInfoBlock() override;
        void updatePokedexBlock() override;                      // Paldea / Kitakami Zukan blocks
        bool itemsAreIdIndexed() const override { return true; } // count at itemId * 0x10

        /**
         * Creates a species-0, checksum-valid blank PK9 entity (mirrors updateBoxBlock()'s
         * encrypted-blank fallback: zeroed SIZE_PARTY9_SV buffer -> encryptArray9SV(seed 0)
         * -> Pokemon9SV). Starting point for the Pokemon creator.
         */
        std::unique_ptr<::Pokemon::Pokemon> createBlankPokemon() const override;

        size_t getBoxCount() const noexcept override
        {
            return BOX_COUNT9_SV;
        }

        size_t getSlotsPerBox() const noexcept override
        {
            return BOX_SLOTS;
        }

        size_t getPartySize() const noexcept override
        {
            return party.size();
        }

        GameVersion getGameGroup() const noexcept override
        {
            return GameVersion::SV;
        }

        /// Which of Scarlet and Violet this save is, read from the save's own game byte
        /// (MyStatus9 +0x04) during parseMyStatusBlock. Defaults to the group representative
        /// so a save whose status block failed to parse still names a real title rather than
        /// reporting Invalid.
        GameVersion getGameVersion() const noexcept override { return gameVersion; }

    private:
        /// Set from the save's game byte in parseMyStatusBlock; the group representative until
        /// then, so this never reports a title that is not one of the two.
        GameVersion gameVersion = GameVersion::SL;

        // Scarlet/Violet PACKS its slots: box and party slots are exactly SIZE_PARTY9_SV bytes with no
        // inter-slot gap. (Named members mirror Trainer9LZA — which gaps its slots — so the shared
        // parse/serialize logic in the .cpp reads identically across the two classes.)
        size_t partySlotStride = SIZE_PARTY9_SV; // packed: no gap between party slots
        size_t boxSlotStride = SIZE_PARTY9_SV;   // packed: no gap between box slots
        size_t slotGapZero = 0;                  // no gap bytes after pokemon data
        void parseBlock(const Block &block);

        /** ID32 is at 0xA0. */
        void parseMyStatusBlock(const Block &block);

        /**
         * Parses the PARTY block to extract party Pokemon.
         * Format: 6 slots of SIZE_8PARTY (344 bytes each)
         */
        void parsePartyBlock(const Block &block);

        /** Money is at 0x04. */
        void parseMoneyBlock(const Block &block);

        // THERE'S NO TRAINER CARD PARSING IN GEN 9

        void parseItemBlock(const Block &block);

        /**
         * Parses the BOX block to extract box Pokemon.
         * Format: 32 boxes * 30 slots * SIZE_9PARTY (344 bytes)
         */
        void parseBoxBlock(const Block &block);

        /** 32 names of BOX_NAME_LENGTH bytes, UTF-16LE. */
        void parseBoxLayoutBlock(const Block &block);

        /**
         * Parses the CURRENT_BOX block ("U32 Box Index") so the editor opens on the box the game
         * was last left on.
         */
        void parseCurrentBoxBlock(const Block &block);

        /**
         * Detects the DLC level from which blocks the save contains -- S/V has no revision value
         * to read. Revision values:
         * - 0: Base game
         * - 1: The Teal Mask (TM)
         * - 2: The Indigo Disk (ID)
         */
        void detectSaveRevision();
    };
}

#endif
