/**
 * Trainer7LGPE implements generation-specific logic for:
 * - PB7 Pokemon storage (party and boxes)
 * - Gen 7 block keys (Let's Go format)
 * - Gen 7 encryption (encryptArray7LGPE/decryptArray7LGPE)
 * - Let's Go-specific save file structure
 */

#ifndef TRAINER_TRAINER7_LGPE_H
#define TRAINER_TRAINER7_LGPE_H

#include <cstring>

#include "Trainer/Trainer.h"
#include "Trainer/Inventory7LGPE.h"
#include "Pokemon/Pokemon7LGPE.h"
#include "Encryption/Encryption7LGPE.h"
#include "Save/Block.h"

namespace Trainer
{

    // Block keys == PKHeX BelugaBlockIndex values (used as unique map keys, not offsets).
    // See PKHeX.Core/Saves/Substructures/Gen7/LGPE/BelugaBlockIndex.cs.
    constexpr size_t MY_ITEM7_LGPE = 0x00;           // index 0  Inventory items
    constexpr size_t MY_STATUS7_LGPE = 0x02;         // index 2  Trainer info (name, ID, gender)
    constexpr size_t ZUKAN7_LGPE = 0x04;             // index 4  Pokedex
    constexpr size_t MISC7_LGPE = 0x05;              // index 5  Miscellaneous data (money)
    constexpr size_t POKE_LIST_HEADER7_LGPE = 0x08;  // index 8  Party header (pointers + starter + count)
    constexpr size_t POKE_LIST_POKEMON7_LGPE = 0x09; // index 9  Pokemon storage (party + boxes)
    constexpr size_t PLAY_TIME7_LGPE = 0x0A;         // index 10 Play time

    // Generation 7 Let's Go constants
    constexpr size_t SAVE_SIZE7_LGPE = 0xB8800;    // 757,760 bytes
    constexpr size_t BOX_COUNT7_LGPE = 40;         // Number of boxes
    constexpr size_t SLOTS_PER_BOX7_LGPE = 25;     // Slots per box (different from Gen 8's 30)
    constexpr size_t BOX_NAME_LENGTH7_LGPE = 0x22; // Box name length (UTF-16LE)
    constexpr size_t SIZE_PARTY7_LGPE = 0x104;     // 260 bytes per Pokemon

    class Trainer7LGPE final : public Trainer
    {
    public:

        explicit Trainer7LGPE(std::vector<Save::Block> blocks) : Trainer(std::move(blocks))
        {
            party.reserve(MAX_PARTY_SLOTS);
            boxes.resize(BOX_COUNT7_LGPE);
            boxNames.resize(BOX_COUNT7_LGPE);

            for (const auto &block : this->blocks)
            {
                parseBlock(block);
            }
        }

        ~Trainer7LGPE() override = default;

        Trainer7LGPE(const Trainer7LGPE &) = delete;
        Trainer7LGPE &operator=(const Trainer7LGPE &) = delete;

        Trainer7LGPE(Trainer7LGPE &&) noexcept = default;
        Trainer7LGPE &operator=(Trainer7LGPE &&) noexcept = default;

        /**
         * Updates the POKE_LIST_POKEMON block with modified party Pokemon data.
         * Uses Gen 7 encryption (encryptArray7LGPE).
         */
        void updatePartyBlock() override;

        /**
         * Updates the POKE_LIST_POKEMON block with modified box Pokemon data.
         * Uses Gen 7 encryption (encryptArray7LGPE).
         */
        void updateBoxBlock() override;

        /**
         * Updates the MY_ITEM block with modified inventory data.
         */
        void updateItemBlock() override;
        void updateTrainerInfoBlock() override; // money / OT name
        void updatePokedexBlock() override;     // Zukan: caught + seen/displayed/language flags

        /**
         * Creates a species-0, checksum-valid blank PB7 entity via the encrypt->decrypt round-trip:
         * zeroed SIZE_PARTY7_LGPE buffer -> encryptArray7LGPE(seed 0) -> Pokemon7LGPE. Starting point
         * for the Pokemon creator. (LGPE's own empty slots are the encrypted blank behind a zero
         * EC, which the read path skips; a live blank entity still needs a valid decrypted PB7.)
         */
        std::unique_ptr<::Pokemon::Pokemon> createBlankPokemon() const override;

        size_t getBoxCount() const noexcept override
        {
            return BOX_COUNT7_LGPE;
        }

        size_t getSlotsPerBox() const noexcept override
        {
            return SLOTS_PER_BOX7_LGPE;
        }

        size_t getPartySize() const noexcept override
        {
            return party.size();
        }

        GameVersion getGameGroup() const noexcept override
        {
            return GameVersion::GG;
        }

        /// Which of Let's Go Pikachu and Let's Go Eevee this save is, read from the save's own game byte
        /// (MyStatus7b +0x04) during parseMyStatusBlock. Defaults to the group representative
        /// so a save whose status block failed to parse still names a real title rather than
        /// reporting Invalid.
        GameVersion getGameVersion() const noexcept override { return gameVersion; }

        /// Let's Go only -- the Partner Pikachu/Eevee. No other format has such a slot.
        bool isStarterPokemon(size_t boxIndex, size_t slotIndex) const noexcept override
        {
            constexpr uint16_t SLOT_EMPTY = 1001;
            if (starterIndex == SLOT_EMPTY || starterIndex >= BOX_COUNT7_LGPE * SLOTS_PER_BOX7_LGPE)
            {
                return false;
            }
            size_t storageIndex = boxIndex * SLOTS_PER_BOX7_LGPE + slotIndex;
            return storageIndex == starterIndex;
        }

        int getPartyPosition(size_t boxIndex, size_t slotIndex) const noexcept override
        {
            constexpr uint16_t SLOT_EMPTY = 1001;
            size_t storageIndex = boxIndex * SLOTS_PER_BOX7_LGPE + slotIndex;

            for (size_t partySlotIndex = 0; partySlotIndex < MAX_PARTY_SLOTS; ++partySlotIndex)
            {
                if (partyIndices[partySlotIndex] != SLOT_EMPTY && partyIndices[partySlotIndex] == storageIndex)
                {
                    return static_cast<int>(partySlotIndex + 1); // Return 1-based position
                }
            }
            return 0; // Not in party
        }

        bool isPartyPokemonStarter(size_t partyIndex) const noexcept override
        {
            constexpr uint16_t SLOT_EMPTY = 1001;
            if (partyIndex >= MAX_PARTY_SLOTS || starterIndex == SLOT_EMPTY)
            {
                return false;
            }
            return partyIndices[partyIndex] == starterIndex;
        }

        /**
         * Swaps two box slots AND updates the LGPE storage-index pointers (party members and
         * the starter reference slots by index box*25+slot), so the party/starter follow their
         * Pokemon to the new slot. Storage stays gapless (both slots remain occupied).
         */
        void swapBoxSlots(size_t firstBox, size_t firstSlot, size_t secondBox, size_t secondSlot) override
        {
            const uint16_t firstFlatSlot = static_cast<uint16_t>(firstBox * SLOTS_PER_BOX7_LGPE + firstSlot);
            const uint16_t secondFlatSlot = static_cast<uint16_t>(secondBox * SLOTS_PER_BOX7_LGPE + secondSlot);
            for (size_t partySlotIndex = 0; partySlotIndex < MAX_PARTY_SLOTS; ++partySlotIndex)
            {
                if (partyIndices[partySlotIndex] == firstFlatSlot)
                    partyIndices[partySlotIndex] = secondFlatSlot;
                else if (partyIndices[partySlotIndex] == secondFlatSlot)
                    partyIndices[partySlotIndex] = firstFlatSlot;
            }
            if (starterIndex == firstFlatSlot)
                starterIndex = secondFlatSlot;
            else if (starterIndex == secondFlatSlot)
                starterIndex = firstFlatSlot;
            Trainer::swapBoxSlots(firstBox, firstSlot, secondBox, secondSlot);
        }

        /**
         * Re-packs the gapless storage list in memory and remaps the party/partner pointers, so the
         * editor shows what will actually be written. See Trainer::compactStorage.
         */
        bool compactStorage() override;

        /**
         * Mirrors an edited party member's box slot into its party copy (and the reverse). LGPE keeps
         * a party member as BOTH a box/storage slot and an independent party copy, and updateBoxBlock()
         * overlays the party copy onto the box slot on save ("party wins") — so a box-slot edit is
         * silently lost unless propagated here. Copies the full decrypted buffer (both are PK7b, same
         * size), so the edit's already-recomputed stats/checksum come along. No-op if the slot isn't a
         * party member. See Trainer::mirrorPartyMemberFromBox.
         */
        void mirrorPartyMemberFromBox(size_t boxIndex, size_t slotIndex) override;
        void mirrorPartyMemberFromParty(size_t partyIndex) override;

    private:
        /// Set from the save's game byte in parseMyStatusBlock; the group representative until
        /// then, so this never reports a title that is not one of the two.
        GameVersion gameVersion = GameVersion::GP;

        void parseBlock(const Block &block);

        /**
         * Parses the MY_STATUS block to extract trainer info.
         * Location: Name at offset 0x00 (26 bytes, UTF-16LE)
         * Location: ID32 at offset 0xA0 (4 bytes)
         */
        void parseMyStatusBlock(const Block &block);

        /**
         * Parses the POKE_LIST_HEADER block to get party count.
         * Location: Party count at offset 0x00 (1 byte)
         */
        void parsePokeListHeaderBlock(const Block &block);

        /**
         * Parses the POKE_LIST_POKEMON block to extract party and box Pokemon.
         * Format: 6 party slots + 40 boxes * 25 slots of SIZE_PARTY7_LGPE (260 bytes each)
         */
        void parsePokeListPokemonBlock(const Block &block);

        /** Money is at 0x04. */
        void parseMiscBlock(const Block &block);

        /**
         * Parses the PLAY_TIME block to extract play time.
         * Location: Hours at offset 0x00 (2 bytes)
         * Location: Minutes at offset 0x02 (1 byte)
         * Location: Seconds at offset 0x03 (1 byte)
         */
        void parsePlayTimeBlock(const Block &block);

        /**
         * Parses the MY_ITEM block to extract inventory items.
         * Items are organized into pouches (Medicine, TMs, Candy, etc.)
         */
        void parseMyItemBlock(const Block &block);

        /// Party count from POKE_LIST_HEADER block
        uint8_t partyCount = 0;

        /// Party indices - which storage slots are party members (LGPE uses index-based party)
        /// SLOT_EMPTY (1001) marks empty positions
        uint16_t partyIndices[MAX_PARTY_SLOTS] = {1001, 1001, 1001, 1001, 1001, 1001};

        /// Starter Pokemon index in storage
        uint16_t starterIndex = 1001;
    };

    std::vector<Save::Block> createBlocksFromSaveData7LGPE(const std::vector<uint8_t> &saveData);

    void writeBlocksToSaveData7LGPE(std::vector<uint8_t> &raw, const std::vector<Save::Block> &blocks);
}

#endif
