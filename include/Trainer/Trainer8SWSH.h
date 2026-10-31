/**
 * Trainer8SWSH implements generation-specific logic for:
 * - PK8 Pokemon storage (party and boxes)
 * - Gen 8 block keys
 * - Gen 8 encryption (encryptArray8/decryptArray8)
 * - Gen 8-specific save file structure
 */

#ifndef TRAINER_TRAINER8_SWSH_H
#define TRAINER_TRAINER8_SWSH_H

#include <cstring>

#include "Trainer/Trainer.h"
#include "Trainer/Inventory8SWSH.h"
#include "Pokemon/Pokemon8SWSH.h"
#include "Encryption/Encryption8SWSH.h"
#include "Save/Block.h"

namespace Trainer
{

    constexpr size_t MY_STATUS8_SWSH = 0xf25c070e;    // Trainer Details
    constexpr size_t TEAM_INDEXES8_SWSH = 0x33F39467; // battle-team box pointers (TeamIndexes8)
    constexpr size_t PARTY8_SWSH = 0x2985fe5d;
    constexpr size_t MONEY8_SWSH = 0x1b882b09;        // Money/Misc
    constexpr size_t TRAINER_CARD8_SWSH = 0x874da6fa; // Trainer Card
    constexpr size_t PLAY_TIME8_SWSH = 0x8cbbfd90;
    constexpr size_t ITEM8_SWSH = 0x1177c2c4;
    constexpr size_t BOX8_SWSH = 0x0d66012c;          // Box Data
    constexpr size_t BOX_LAYOUT8_SWSH = 0x19722c89;   // Box Names
    constexpr size_t CURRENT_BOX8_SWSH = 0x017C3CBB;  // Current box index ("U32 Box Index")

    // DLC Detection Block Keys
    // These blocks are only present if the corresponding DLC has been played
    constexpr size_t SAVE_REVISION8_SWSH = 0x4716c404;    // Base game Pokedex (Galar)
    constexpr size_t SAVE_REVISION8_R1_SWSH = 0x3F936BA9; // Isle of Armor Pokedex (DLC 1)
    constexpr size_t SAVE_REVISION8_R2_SWSH = 0x3C9366F0; // Crown Tundra Pokedex (DLC 2)

    // DLC-specific blocks (for future use)
    // constexpr size_t RAID_SPAWN_LIST8_R1_SWSH = 0x158DA896;  // IoA Raid Data
    // constexpr size_t RAID_SPAWN_LIST8_R2_SWSH = 0x148DA703;  // CT Raid Data

    // Generation 8 constants
    constexpr size_t BOX_COUNT8_SWSH = 32;         // Number of boxes in Sword/Shield
    constexpr size_t BOX_NAME_LENGTH8_SWSH = 0x22; // 34 bytes per box name (UTF-16LE)
    // 34 bytes = 17 UTF-16 slots, one of which holds the null terminator (PKHeX: SAV6.LongStringLength / 2).
    constexpr size_t MAX_BOX_NAME_CHARS8_SWSH = BOX_NAME_LENGTH8_SWSH / 2 - 1; // 16

    class Trainer8SWSH final : public Trainer
    {
    public:

        explicit Trainer8SWSH(std::vector<Save::Block> blocks) : Trainer(std::move(blocks))
        {
            party.reserve(MAX_PARTY_SLOTS);
            boxes.resize(BOX_COUNT8_SWSH);
            boxNames.resize(BOX_COUNT8_SWSH);

            for (const auto &block : this->blocks)
            {
                parseBlock(block);
            }

            // Detect DLC version by checking for DLC Pokedex blocks
            detectSaveRevision();
        }

        ~Trainer8SWSH() override = default;

        Trainer8SWSH(const Trainer8SWSH &) = delete;
        Trainer8SWSH &operator=(const Trainer8SWSH &) = delete;

        Trainer8SWSH(Trainer8SWSH &&) noexcept = default;
        Trainer8SWSH &operator=(Trainer8SWSH &&) noexcept = default;

        /**
         * Updates the PARTY_KEY block with modified Pokemon data.
         * Uses Gen 8 encryption (encryptArray8).
         */
        void updatePartyBlock() override;

        /**
         * Updates the BOX_KEY block with modified Pokemon data.
         * Uses Gen 8 encryption (encryptArray8).
         */
        void updateBoxBlock() override;
        void updateBoxNameBlock() override;
        void updateCurrentBoxBlock() override;
        bool supportsBoxNames() const noexcept override { return true; }
        /// A member of a battle team (PKHeX GetBoxSlotFlags: overwrite-protected).
        bool isBoxSlotLocked(size_t boxIndex, size_t slotIndex) const noexcept override { return battleTeamBlockHoldsSlot(TEAM_INDEXES8_SWSH, boxIndex, slotIndex); }
        size_t getMaxBoxNameLength() const noexcept override { return MAX_BOX_NAME_CHARS8_SWSH; }

        void updateItemBlock() override;
        void updateTrainerInfoBlock() override;
        void updatePokedexBlock() override; // Galar / Armor / Crown Zukan blocks

        /**
         * Creates a species-0, checksum-valid blank PK8 entity (mirrors updateBoxBlock()'s
         * encrypted-blank fallback: zeroed SIZE_PARTY8_SWSH buffer -> encryptArray8SWSH(seed 0)
         * -> Pokemon8SWSH). Starting point for the Pokemon creator.
         */
        std::unique_ptr<::Pokemon::Pokemon> createBlankPokemon() const override;

        size_t getBoxCount() const noexcept override
        {
            return BOX_COUNT8_SWSH;
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
            return GameVersion::SWSH;
        }

        /// Which of Sword and Shield this save is, read from the save's own game byte
        /// (MyStatus8 +0xA4) during parseMyStatusBlock. Defaults to the group representative
        /// so a save whose status block failed to parse still names a real title rather than
        /// reporting Invalid.
        GameVersion getGameVersion() const noexcept override { return gameVersion; }

    protected:
    private:
        /// Set from the save's game byte in parseMyStatusBlock; the group representative until
        /// then, so this never reports a title that is not one of the two.
        GameVersion gameVersion = GameVersion::SW;

        /// The pouch slot each parsed item came out of. Sword/Shield pouches are positional, not
        /// packed, so an item that has not changed is written back to its own slot.
        std::vector<std::vector<ItemOrigin>> itemSlot;

        void parseBlock(const Block &block);

        /** ID32 is at 0xA0. */
        void parseMyStatusBlock(const Block &block);

        /**
         * Parses the PARTY block to extract party Pokemon.
         * Format: 6 slots of SIZE_PARTY (344 bytes each)
         */
        void parsePartyBlock(const Block &block);

        /** Money is at 0x04. */
        void parseMoneyBlock(const Block &block);

        /**
         * Parses the TRAINER_CARD block to extract trainer name.
         * Location: Name at offset 0x00 (26 bytes, UTF-16LE)
         * Location: Trainer ID at offset 0x1C (4 bytes)
         */
        void parseTrainerCardBlock(const Block &block);

        void parseItemBlock(const Block &block);

        /**
         * Parses the BOX block to extract box Pokemon.
         * Format: 32 boxes * 30 slots * SIZE_PARTY (344 bytes)
         */
        void parseBoxBlock(const Block &block);

        /** 32 names of BOX_NAME_LENGTH bytes, UTF-16LE. */
        void parseBoxLayoutBlock(const Block &block);

        /** Parses the CURRENT_BOX block ("U32 Box Index") so the editor opens on the last box used. */
        void parseCurrentBoxBlock(const Block &block);

        /**
         * Detects the save revision (DLC version) by checking for DLC Pokedex blocks.
         * Revision values:
         * - 0: Base game
         * - 1: Isle of Armor (IoA)
         * - 2: Crown Tundra (CT)
         */
        void detectSaveRevision();
    };
}

#endif
