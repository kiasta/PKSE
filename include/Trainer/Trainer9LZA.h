/**
 * Generation 9 Legends: Z-A Trainer/Save File Data Management
 *
 * Trainer9LZA implements generation-specific logic for:
 * - PK9 Pokemon storage (party and boxes)
 * - Gen 9 block keys
 * - Gen 9 encryption (encryptArray9/decryptArray9)
 * - Gen 9-specific save file structure
 */

#ifndef TRAINER_TRAINER9_H
#define TRAINER_TRAINER9_H

#include <cstring>

#include "Trainer/Trainer.h"
#include "Pokemon/Pokemon9LZA.h"
#include "Encryption/Encryption9LZA.h"

using namespace Pokemon;
using namespace Encryption;

namespace Trainer
{

    constexpr size_t MY_STATUS9_LZA = 0xE3E89BD1;   // Trainer Details
    constexpr size_t TEAM_INDEXES9_LZA = 0x33F39467; // battle-team box pointers (TeamIndexes8)
    constexpr size_t PARTY9_LZA = 0x3AA1A9AD;
    constexpr size_t MONEY9_LZA = 0x4F35D0DD;       // Money (u32)
    constexpr size_t PLAY_TIME9_LZA = 0xEDAFF794;
    constexpr size_t ITEM9_LZA = 0x21C9BD44;
    constexpr size_t BOX9_LZA = 0x0d66012c;         // Box Data
    constexpr size_t BOX_LAYOUT9_LZA = 0x19722c89;  // Box Names
    constexpr size_t CURRENT_BOX9_LZA = 0x017C3CBB; // Current box index ("U32 Box Index")

    // Version detection block
    constexpr size_t SAVE_REVISION9_LZA = 0x0926555A; // Save Revision (u64)

    // Additional blocks (for future use)
    // Pokedex. ONE block, unlike Scarlet/Violet's pair -- Z-A has a single regional dex. Entries are
    // 0x84 bytes, keyed by the Gen 9 INTERNAL species id (PKHeX Zukan9a / PokeDexEntry9a).
    constexpr size_t POKEDEX9_LZA = 0x2D87BE5C;

    // Generation 9 constants
    constexpr size_t BOX_COUNT9_LZA = 32;         // Number of boxes in Legends: Z-A
    constexpr size_t BOX_NAME_LENGTH9_LZA = 0x22; // 34 bytes per box name (UTF-16LE)

    class Trainer9LZA final : public Trainer
    {
    public:

        // Legends: Z-A (PA9). Shares the Gen 9 SCBlock save + entity format with Scarlet/Violet, but
        // Z-A GAPS its box/party slots (see the slot-stride members below) where S/V packs them.
        // Trainer9SV is the Scarlet/Violet counterpart.
        explicit Trainer9LZA(std::vector<Block> blocks) : Trainer(std::move(blocks))
        {
            party.reserve(MAX_PARTY_SLOTS);
            boxes.resize(BOX_COUNT9_LZA);
            boxNames.resize(BOX_COUNT9_LZA);

            for (const auto &block : this->blocks)
            {
                parseBlock(block);
            }
        }

        ~Trainer9LZA() override = default;

        Trainer9LZA(const Trainer9LZA &) = delete;
        Trainer9LZA &operator=(const Trainer9LZA &) = delete;

        Trainer9LZA(Trainer9LZA &&) noexcept = default;
        Trainer9LZA &operator=(Trainer9LZA &&) noexcept = default;

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
        bool isBoxSlotLocked(size_t boxIndex, size_t slotIndex) const noexcept override { return battleTeamBlockHoldsSlot(TEAM_INDEXES9_LZA, boxIndex, slotIndex); }
        size_t getMaxBoxNameLength() const noexcept override { return BOX_NAME_LENGTH9_LZA / 2 - 1; }

        void updateItemBlock() override;
        void updateTrainerInfoBlock() override;                  // money / OT name
        void updatePokedexBlock() override;                      // Zukan9a: seen/caught/shiny/mega/alpha per form
        bool itemsAreIdIndexed() const override { return true; } // count at itemId * 0x10

        /**
         * Creates a species-0, checksum-valid blank PK9 entity (mirrors updateBoxBlock()'s
         * encrypted-blank fallback: zeroed SIZE_PARTY9_LZA buffer -> encryptArray9LZA(seed 0)
         * -> Pokemon9LZA). Starting point for the Pokemon creator.
         */
        std::unique_ptr<::Pokemon::Pokemon> createBlankPokemon() const override;

        size_t getBoxCount() const noexcept override
        {
            return BOX_COUNT9_LZA;
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
            return GameVersion::ZA;
        }

        /// Legends: Z-A is the only title in its group, so the group IS the title and there is
        /// no game byte to consult.
        GameVersion getGameVersion() const noexcept override { return GameVersion::ZA; }

    private:
        // Legends: Z-A GAPS its slots: each box/party slot holds SIZE_PARTY9_LZA bytes of pokemon data
        // followed by a GAP_BOX_SLOT9_LZA-byte gap. (Trainer9SV packs its slots — no gap.)
        size_t partySlotStride = PARTY_SLOT_SIZE9_LZA; // stride between party slots (gapped)
        size_t boxSlotStride = BOX_SLOT_SIZE9_LZA;     // stride between box slots (gapped)

        /// The footer bytes PKHeX writes with every slot it sets: the "present" flag on a revision != 0
        /// save, and (party only) PARTY_FOOTER_OFFSET9_LZA. The rest of the gap is left as the game wrote it.
        void writeSlotFooter(uint8_t *slotBytes, bool isPartySlot) const;
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
         * Parses the CURRENT_BOX block ("U32 Box Index") to extract which box the game was last
         * left on, so the editor opens on the same box.
         */
        void parseCurrentBoxBlock(const Block &block);

        /**
         * Parses the SAVE_REVISION block to detect DLC version.
         * Revision values:
         * - 0: Base game
         * - 1: Mega Dimension (MD) DLC
         */
        void parseSaveRevisionBlock(const Block &block);
    };
}

#endif
