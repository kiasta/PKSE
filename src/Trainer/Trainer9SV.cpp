#include <algorithm>
#include <cstring>
#include <string>

#include "Trainer/Trainer9SV.h"
#include "Pokemon/SpeciesConverter9.h" // gen9NationalToInternal -- dex entries are keyed by internal id
#include "Pokemon/PersonalInfoTable.h" // getPersonalInfo -> presence + genderRatio
#include "Pokemon/DexTable9SV.h"        // getDexEntry9SV -- which regional dex a species+form is listed in
#include "Trainer/Inventory9SV.h"
#include "Names/ItemPouches.h" // getPouchItems -- per-pouch legal ids
#include "Utils/Logger.h"

using namespace Utils;

namespace Trainer
{

    void Trainer9SV::parseBlock(const Block &block)
    {
        switch (block.key)
        {
        case MY_STATUS9_SV:
            parseMyStatusBlock(block);
            break;
        case PARTY9_SV:
            parsePartyBlock(block);
            break;
        case MONEY9_SV:
            parseMoneyBlock(block);
            break;
        case ITEM9_SV:
            parseItemBlock(block);
            break;
        case BOX9_SV:
            parseBoxBlock(block);
            break;
        case BOX_LAYOUT9_SV:
            parseBoxLayoutBlock(block);
            break;
        case CURRENT_BOX9_SV:
            parseCurrentBoxBlock(block);
            break;
        default:
            break;
        }
    }

    void Trainer9SV::parseMyStatusBlock(const Block &block)
    {
        if (block.data.size() < 0x00 + 4)
        {
            logInfoToFile("Insufficient data for UInt32 at offset 0x00 in MY_STATUS block");
            return;
        }

        // The save names its own game at 0x04 (PKHeX MyStatus9.Game). Accepted only if it is one
        // of this group's two titles -- anything else is a misparse, not a discovery.
        if (block.data.size() > 0x04)
        {
            const GameVersion storedVersion = static_cast<GameVersion>(block.data[0x04]);
            if (storedVersion == GameVersion::SL || storedVersion == GameVersion::VL)
                this->gameVersion = storedVersion;
        }

        this->ID32 = readUInt32LittleEndian(&block.data[0x00]);
        this->TID16 = readUInt16LittleEndian(&block.data[0x00]);
        this->SID16 = readUInt16LittleEndian(&block.data[0x02]);
        this->TID = this->ID32 % 1000000;
        this->SID = this->ID32 / 1000000;
        // OT name is a 26-byte (0x1A) field at 0x10 -- PKHeX MyStatus9.OriginalTrainerTrash =
        // Data.Slice(0x10, 0x1A). The old min(0x10, 0x1A) mistakenly used the OFFSET (0x10 = 16 bytes =
        // 8 code units) as the length, truncating any trainer name longer than 8 characters.
        if (block.data.size() >= 0x10 + 0x1A)
            this->trainerName = utf16ToUtf8(getString(&block.data[0x10], 0x1A));
        this->trainerGender = block.data[0x05] & 1; // 0x05: gender (0=M, 1=F)
        logInfoToFile("Parsed Trainer Name", this->trainerName.c_str());
    }

    void Trainer9SV::parsePartyBlock(const Block &block)
    {
        /**
         * PARTY Block Structure (Pokemon Scarlet/Violet):
         * Pokemon stored with gaps between slots:
         * - Slot 0: offset 0 (SIZE_PARTY9_SV bytes of data + GAP_BOX_SLOT9_SV gap)
         * - Slot 1: offset PARTY_SLOT_SIZE9_SV (480 bytes)
         * - Slot 2: offset 2 * PARTY_SLOT_SIZE9_SV
         * - ... up to 6 slots
         *
         * Each slot spans PARTY_SLOT_SIZE9_SV bytes (480 bytes), but only the first
         * SIZE_PARTY9_SV bytes (344 bytes) contain Pokemon data. The remaining gap
         * (GAP_BOX_SLOT9_SV = 0x88 bytes) is unused/padding.
         */
        const std::span<const std::byte> blockSpan(reinterpret_cast<const std::byte *>(block.data.data()),
                                                   block.data.size());

        for (size_t slot = 0; slot < MAX_PARTY_SLOTS; ++slot)
        {
            // Calculate offset to this slot (packed for S/V, gapped for Z-A)
            const size_t offset = slot * partySlotStride;
            if (offset + SIZE_PARTY9_SV > block.data.size())
                break;

            std::span<const std::byte> slotSpan = blockSpan.subspan(offset, SIZE_PARTY9_SV);

            bool isEmptySlot = true;
            for (size_t slotSpanIndex = 0; slotSpanIndex < SIZE_PARTY9_SV && slotSpanIndex < slotSpan.size();
                 ++slotSpanIndex)
            {
                if (slotSpan[slotSpanIndex] != std::byte{0})
                {
                    isEmptySlot = false;
                    break;
                }
            }

            if (!isEmptySlot)
            {
                // Decrypt and create Pokemon9SV object as unique_ptr
                // Pokemon9SV constructor handles decryption automatically
                party.push_back(std::make_unique<Pokemon9SV>(slotSpan));
            }
        }
    }

    void Trainer9SV::parseMoneyBlock(const Block &block)
    {
        /**
         * MONEY Block (Gen 9 Scarlet/Violet):
         * For Gen 9 Scarlet/Violet, the money value is stored directly as the block value.
         * 0x00: Money (4 bytes)
         */
        if (block.data.size() < 4)
        {
            return;
        }

        this->money = readUInt32LittleEndian(block.data.data());
    }

    void Trainer9SV::parseItemBlock(const Block &block)
    {
        /**
         * ITEM Block Structure (Gen 9 Scarlet/Violet):
         * Items are stored BY ITEM ID as index!
         * - Item at ID X is at offset (X * 0x10)
         * - Block size: 0xBB80 bytes (47,872 bytes)
         * - Each item: 0x10 bytes (16 bytes)
         *
         * Structure per item (16 bytes):
         * 0x00-0x03: Pouch ID (uint32) - which pouch this belongs to
         * 0x04-0x07: Count (int32) - quantity
         * 0x08-0x0B: Flags (uint32) - isNew, isFavorite, etc.
         * 0x0C-0x0F: Padding
         */

        items.resize(static_cast<size_t>(POUCH_COUNT9_SV));

        // For each pouch, iterate through valid item IDs
        for (int pouchIndex = 0; pouchIndex < static_cast<int>(POUCH_COUNT9_SV); pouchIndex++)
        {
            items[pouchIndex].clear();

            // Legal ids for this pouch, from the generated PKHeX-derived table. This replaced a
            // hand-written list that was byte-identical to the other Gen 9 game's and wrong for
            // both -- it bucketed vitamins under Battle Items and claimed pockets the
            // game does not have. Pouch membership is display-only: the write below is keyed on
            // item id, not pouch.
            const auto validIds = Names::getPouchItems(Enums::GameVersion::SV, static_cast<size_t>(pouchIndex));

            for (uint16_t itemId : validIds)
            {
                size_t offset = itemId * ITEM_SIZE9_SV;

                // Make sure we're within bounds
                if (offset + ITEM_SIZE9_SV > block.data.size())
                {
                    continue;
                }

                InventoryItem9SV item = InventoryItem9SV::fromBytes(itemId, &block.data[offset]);

                // Only add if count > 0
                if (item.count > 0)
                {
                    items[pouchIndex].push_back(item);
                }
            }
        }
    }

    void Trainer9SV::parseBoxBlock(const Block &block)
    {
        const std::span<const std::byte> blockSpan(
            reinterpret_cast<const std::byte *>(block.data.data()),
            block.data.size());

        for (size_t boxIndex = 0; boxIndex < BOX_COUNT9_SV; ++boxIndex)
        {
            for (size_t slot = 0; slot < BOX_SLOTS; ++slot)
            {
                // Calculate offset (packed for S/V, gapped for Z-A)
                const size_t offset = (boxIndex * BOX_SLOTS + slot) * boxSlotStride;
                if (offset + SIZE_PARTY9_SV > block.data.size())
                {
                    break;
                }

                std::span<const std::byte> slotSpan = blockSpan.subspan(offset, SIZE_PARTY9_SV);

                bool isEmptySlot = true;
                for (size_t slotSpanIndex = 0; slotSpanIndex < SIZE_PARTY9_SV && slotSpanIndex < slotSpan.size();
                     ++slotSpanIndex)
                {
                    if (slotSpan[slotSpanIndex] != std::byte{0})
                    {
                        isEmptySlot = false;
                        break;
                    }
                }

                if (!isEmptySlot)
                {
                    boxes[boxIndex][slot] = std::make_unique<Pokemon9SV>(slotSpan);
                }
                else
                {
                    boxes[boxIndex][slot] = nullptr;
                }
            }
        }
    }

    void Trainer9SV::parseBoxLayoutBlock(const Block &block)
    {
        for (size_t boxIndex = 0; boxIndex < BOX_COUNT9_SV; ++boxIndex)
        {
            size_t offset = boxIndex * BOX_NAME_LENGTH9_SV;
            if (offset + BOX_NAME_LENGTH9_SV <= block.data.size())
            {
                std::u16string boxNameU16 = getString(
                    block.data.data() + offset,
                    BOX_NAME_LENGTH9_SV);
                std::string boxName = utf16ToUtf8(boxNameU16);

                if (boxName.empty())
                {
                    boxName = "Box " + std::to_string(boxIndex + 1);
                }

                boxNames[boxIndex] = boxName;
            }
            else
            {
                boxNames[boxIndex] = "Box " + std::to_string(boxIndex + 1);
            }
        }
    }

    void Trainer9SV::parseCurrentBoxBlock(const Block &block)
    {
        // "U32 Box Index" -- a scalar block; PKHeX reads it as a byte (0..31 fits one byte).
        if (block.data.empty())
            return;
        uint8_t box = block.data[0];
        if (box < BOX_COUNT9_SV)
            this->currentBox = box;
    }

    void Trainer9SV::updateCurrentBoxBlock()
    {
        // Inverse of parseCurrentBoxBlock: write the low byte and clear the rest of the scalar.
        for (auto &block : blocks)
        {
            if (block.key != CURRENT_BOX9_SV || block.data.empty())
                continue;
            block.data[0] = static_cast<uint8_t>(currentBox);
            for (size_t moveIndex = 1; moveIndex < block.data.size() && moveIndex < 4; ++moveIndex)
                block.data[moveIndex] = 0;
            break;
        }
    }

    void Trainer9SV::detectSaveRevision()
    {
        /**
         * Reports the game's LATEST content set, unconditionally -- the same decision as
         * Sword/Shield, for the same reason.
         *
         * Scarlet/Violet stores no save-revision value at all. The level was inferred from which
         * blocks the save happened to contain (a Blueberry-points block for The Indigo Disk, a DLC
         * tera-raid block for The Teal Mask), and block presence is exactly the proxy that proved
         * unreliable for Sword/Shield: it answers "has the game allocated this?", not "does the
         * player own this?".
         *
         * Nothing in PKSE gates content on the value -- it is the title-bar label and the event
         * log -- so this changes what is displayed and nothing else.
         */
        this->saveRevision = 2;
        this->saveRevisionString = "The Indigo Disk";
        this->gameVersionString = "v3.0";

        char buffer[128];
        snprintf(buffer, sizeof(buffer), "Save revision reported as: %d (%s, %s)",
                 this->saveRevision, this->saveRevisionString.c_str(), this->gameVersionString.c_str());
        logInfoToFile(buffer);
    }

    void Trainer9SV::updatePartyBlock()
    {
        /**
         * Updates the PARTY block with modified Pokemon data.
         *
         * Empty party slots must be the game's encrypted "blank" (which DECRYPTS to species 0), NOT
         * literal zeros -- exactly like updateBoxBlock(). The game decrypts every slot and validates it,
         * so a zeroed slot decrypts to garbage and renders a BAD EGG on every empty slot. Confirmed on a
         * real Sword/Shield save, whose empty party slots hold 331 non-zero bytes of 344 and decrypt to
         * species 0; S/V shares the entity format and the crypto, and this game's own updateBoxBlock
         * already carries the same rule. The party is re-serialized on EVERY save, so a memset(0) here
         * corrupts the party even when the edit only touched a box.
         */
        for (auto &block : blocks)
        {
            if (block.key == PARTY9_SV)
            {
                // Ensure the block data is large enough (including gaps). Only ever grows, so a
                // real save's trailing party-count bytes survive.
                size_t requiredSize = MAX_PARTY_SLOTS * partySlotStride;
                if (block.data.size() < requiredSize)
                {
                    block.data.resize(requiredSize, 0);
                }

                // Which slots ALREADY read as empty, decided on the bytes in the file rather than on
                // party.size()? Those are left byte-for-byte alone below. There is no single canonical blank to
                // stamp: measured on a real save, the game's empty slot is not a zeroed entity and empty slots
                // differ from EACH OTHER too (stale bytes the game never cleared), so the only way to leave an
                // untouched party untouched is to not write to it.
                //
                // "Decrypts to species 0" is NOT sufficient on its own, and the reason is a trap: the cipher
                // seeds its PRNG with the encryption constant, a zeroed slot has EC 0, and the first PRNG word
                // from seed 0 is 0x0000 -- so an all-zero slot decrypts to species 0 as well, while its other
                // bytes are garbage the checksum rejects. A slot wrecked by a memset(0) would therefore look
                // "already empty" and be skipped, which is precisely the slot that needs repairing. A real blank
                // always carries non-zero bytes, so require that too.
                bool alreadyEmpty[MAX_PARTY_SLOTS] = {};
                for (size_t partySlotIndex = 0; partySlotIndex < MAX_PARTY_SLOTS; ++partySlotIndex)
                {
                    const size_t byteOffset = partySlotIndex * partySlotStride;
                    if (byteOffset + SIZE_PARTY9_SV > block.data.size())
                        break;

                    bool anyNonZero = false;
                    for (size_t byteIndex = 0; byteIndex < SIZE_PARTY9_SV && !anyNonZero; ++byteIndex)
                        anyNonZero = (block.data[byteOffset + byteIndex] != 0);
                    // zeroed by an older build -> repair it below
                    if (!anyNonZero) continue;

                    std::vector<std::byte> encryptedRecord(SIZE_PARTY9_SV);
                    std::memcpy(encryptedRecord.data(), &block.data[byteOffset], SIZE_PARTY9_SV);
                    std::byte *dec = decryptArray9SV(
                        std::span<const std::byte>(encryptedRecord.data(), SIZE_PARTY9_SV));
                    alreadyEmpty[partySlotIndex] = (readUInt16LittleEndian(
                                           reinterpret_cast<const uint8_t *>(dec) + 0x08) == 0);
                    delete[] dec;
                }

                // For a slot that must BECOME empty (a Pokemon was removed), and for repairing a
                // slot an older build zeroed into a Bad Egg: an all-zero PK9 encrypted with EC 0.
                std::vector<uint8_t> blankSlot;
                {
                    std::vector<std::byte> zero(SIZE_PARTY9_SV, std::byte{0});
                    std::byte *encryptedRecord = encryptArray9SV(
                        std::span<const std::byte>(zero.data(), SIZE_PARTY9_SV), 0);
                    blankSlot.assign(reinterpret_cast<const uint8_t *>(encryptedRecord),
                                     reinterpret_cast<const uint8_t *>(encryptedRecord) + SIZE_PARTY9_SV);
                    blankSlot.resize(partySlotStride, 0);
                    delete[] encryptedRecord;
                }

                // Keep the game's own bytes when the slot already reads as empty, otherwise lay
                // down the blank. Never zeros -- that is the Bad Egg.
                const auto clearSlot = [&](size_t slotIndex, size_t offset)
                {
                    if (slotIndex < MAX_PARTY_SLOTS && alreadyEmpty[slotIndex])
                        return;
                    std::memcpy(&block.data[offset], blankSlot.data(), partySlotStride);
                };

                for (size_t partySlotIndex = 0; partySlotIndex < party.size() && partySlotIndex < MAX_PARTY_SLOTS;
                     ++partySlotIndex)
                {
                    // Calculate offset (packed for S/V, gapped for Z-A)
                    const size_t offset = partySlotIndex * partySlotStride;

                    if (party[partySlotIndex] && party[partySlotIndex]->speciesID() != 0)
                    {
                        const ::Pokemon::Pokemon *pokemon = party[partySlotIndex].get();
                        uint32_t encryptionConstant = readUInt32LittleEndian(
                            reinterpret_cast<const uint8_t *>(pokemon->getData().data()));

                        std::span<const std::byte> decryptedSpan(
                            pokemon->getData().data(),
                            pokemon->getDataSize());

                        std::byte *encryptedData = encryptArray9SV(decryptedSpan, encryptionConstant);

                        // Write encrypted data to block (only SIZE_PARTY9_SV bytes)
                        std::memcpy(&block.data[offset], encryptedData, pokemon->getDataSize());

                        // Zero out the gap after the Pokemon data (0 bytes for S/V's packed layout)
                        std::memset(&block.data[offset + SIZE_PARTY9_SV], 0, slotGapZero);

                        delete[] encryptedData;
                    }
                    else
                    {
                        clearSlot(partySlotIndex, offset); // never zeros -- see the header note
                    }
                }

                // Remaining trailing slots are empty too.
                for (size_t partySlotIndex = party.size(); partySlotIndex < MAX_PARTY_SLOTS; ++partySlotIndex)
                {
                    clearSlot(partySlotIndex, partySlotIndex * partySlotStride);
                }

                break;
            }
        }
    }

    void Trainer9SV::updateBoxNameBlock()
    {
        // Inverse of parseBoxLayoutBlock. See Trainer8SWSH::updateBoxNameBlock for why the block is
        // bounds-checked rather than resized.
        for (auto &block : blocks)
        {
            if (block.key != BOX_LAYOUT9_SV)
                continue;
            for (size_t boxIndex = 0; boxIndex < BOX_COUNT9_SV && boxIndex < boxNames.size(); ++boxIndex)
            {
                // never persist a display default
                if (!isBoxNameDirty(boxIndex)) continue;
                const size_t offset = boxIndex * BOX_NAME_LENGTH9_SV;
                if (offset + BOX_NAME_LENGTH9_SV > block.data.size())
                    break;
                setString(block.data.data() + offset, BOX_NAME_LENGTH9_SV,
                          utf8ToUtf16(boxNames[boxIndex]), BOX_NAME_LENGTH9_SV / 2 - 1);
            }
            break;
        }
    }

    void Trainer9SV::updateBoxBlock()
    {
        /**
         * Updates the BOX block with modified Pokemon data.
         *
         * Process similar to updatePartyBlock, but for all boxes:
         * 1. Find the BOX block
         * 2. Ensure block is large enough (32 boxes * 30 slots * BOX_SLOT_SIZE9_SV)
         * 3. For each box and slot:
         *    a. If Pokemon exists, encrypt and write
         *    b. If slot is empty, write zeros
         */
        for (auto &block : blocks)
        {
            if (block.key == BOX9_SV)
            {
                size_t requiredSize = BOX_COUNT9_SV * BOX_SLOTS * boxSlotStride;
                if (block.data.size() < requiredSize)
                {
                    block.data.resize(requiredSize, 0);
                }

                // Empty box slots in the real save are an ENCRYPTED blank that DECRYPTS to species 0 —
                // NOT literal zeros. The game decrypts every box slot and checks species; a zeroed slot
                // decrypts to garbage and renders a BAD EGG. So reuse the game's own blank: copy the raw
                // bytes of an existing empty (species-0) slot. If every box is full (no blank to copy),
                // synthesize one from an all-zero PK9 encrypted with EC 0 (decrypts back to species 0).
                std::vector<uint8_t> blankSlot;
                for (size_t boxIndex = 0; boxIndex < BOX_COUNT9_SV && blankSlot.empty(); ++boxIndex)
                {
                    for (size_t slotIndex = 0; slotIndex < BOX_SLOTS; ++slotIndex)
                    {
                        if (boxes[boxIndex][slotIndex] && boxes[boxIndex][slotIndex]->speciesID() == 0)
                        {
                            const size_t byteOffset = (boxIndex * BOX_SLOTS + slotIndex) * boxSlotStride;
                            if (byteOffset + boxSlotStride <= block.data.size())
                            {
                                blankSlot.assign(block.data.begin() + byteOffset,
                                                 block.data.begin() + byteOffset + boxSlotStride);
                                break;
                            }
                        }
                    }
                }
                if (blankSlot.empty())
                {
                    std::vector<std::byte> zero(SIZE_PARTY9_SV, std::byte{0});
                    std::byte *encryptedRecord = encryptArray9SV(
                        std::span<const std::byte>(zero.data(), SIZE_PARTY9_SV), 0);
                    blankSlot.assign(reinterpret_cast<const uint8_t *>(encryptedRecord),
                                     reinterpret_cast<const uint8_t *>(encryptedRecord) + SIZE_PARTY9_SV);
                    blankSlot.resize(boxSlotStride, 0); // pad the Z-A slot gap with zeros
                    delete[] encryptedRecord;
                }

                for (size_t boxIndex = 0; boxIndex < BOX_COUNT9_SV; ++boxIndex)
                {
                    for (size_t slot = 0; slot < BOX_SLOTS; ++slot)
                    {
                        // Calculate offset (packed for S/V, gapped for Z-A)
                        const size_t offset = (boxIndex * BOX_SLOTS + slot) * boxSlotStride;

                        // Gate on species, not just the pointer (matches updatePartyBlock). A slot left
                        // holding a non-null but blank/species-0 pokemon (e.g. after a bank move) must be
                        // zeroed to read as EMPTY in-game — re-encrypting a blank writes a bad egg.
                        if (boxes[boxIndex][slot] && boxes[boxIndex][slot]->speciesID() != 0)
                        {
                            const auto &pokemon = boxes[boxIndex][slot];

                            uint32_t encryptionConstant = readUInt32LittleEndian(
                                reinterpret_cast<const uint8_t *>(pokemon->getData().data()));

                            std::span<const std::byte> decryptedSpan(
                                pokemon->getData().data(),
                                pokemon->getDataSize());

                            std::byte *encryptedData = encryptArray9SV(decryptedSpan, encryptionConstant);

                            // Write encrypted data to block (only SIZE_PARTY9_SV bytes)
                            std::memcpy(&block.data[offset], encryptedData, pokemon->getDataSize());

                            // Zero out the gap after the Pokemon data (0 bytes for S/V's packed layout)
                            std::memset(&block.data[offset + SIZE_PARTY9_SV], 0, slotGapZero);

                            delete[] encryptedData;
                        }
                        else
                        {
                            // Empty/cleared slot: write the game's encrypted blank, NOT zeros. Zeros
                            // decrypt to garbage in-game and show as a BAD EGG in every empty slot.
                            std::memcpy(&block.data[offset], blankSlot.data(), boxSlotStride);
                        }
                    }
                }
                break;
            }
        }
    }

    std::unique_ptr<::Pokemon::Pokemon> Trainer9SV::createBlankPokemon() const
    {
        // Mirror updateBoxBlock()'s encrypted-blank fallback: a zeroed *decrypted* PK9 buffer
        // encrypted with EC/seed 0, then fed to the ctor (which decrypts it straight back to zeros)
        // -> a clean species-0, Sanity-0, checksum-valid entity. Raw zeros in the ctor would decrypt
        // to garbage (BAD EGG); the encrypt->decrypt round-trip is what makes the blank valid.
        std::vector<std::byte> zero(SIZE_PARTY9_SV, std::byte{0});
        std::byte *encryptedRecord = encryptArray9SV(
            std::span<const std::byte>(zero.data(), SIZE_PARTY9_SV), 0);
        auto clone = std::make_unique<Pokemon9SV>(
            std::span<const std::byte>(encryptedRecord, SIZE_PARTY9_SV));
        delete[] encryptedRecord;
        return clone;
    }

    //
    // Entries are indexed by the Gen 9 INTERNAL species id, not the National number -- the same
    // conversion the entity's species field needs (SpeciesConverter9). Two blocks exist and only one
    // is live: from game 2.0.1 the Paldea block was dummied out in favour of Kitakami, so "Kitakami
    // has data" means Kitakami is the dex. Their entry layouts are different, not nested.
    //
    // Kitakami entry, 0x20 bytes (PKHeX PokeDexEntry9Kitakami):
    //   0x00 u32 forms obtained (bit per form)   0x04 u32 forms seen   0x08 u32 forms heard
    //   0x0C u32 forms checked                   0x10 u16 language flags
    //   0x12 u8 genders seen (1 male, 2 female, 4 genderless)
    //   0x13 u8 models seen  (1 regular, 2 shiny)
    //   0x14/0x18/0x1C  displayed form/gender/shiny for the Paldea / Kitakami / Blueberry dexes
    //
    // Paldea entry, 0x18 bytes (PKHeX PokeDexEntry9Paldea):
    //   0x00 u32 state (0 hidden / 1 known-of / 2 seen / 3 obtained)   0x04 u32 forms obtained
    //   0x08 u16 genders seen   0x0A u16 language   0x0C u8 shiny obtained   0x10 u16 display form
    //   0x14 u8 display gender  0x15 u8 display shiny
    namespace
    {
        constexpr size_t SV_ENTRY_KITAKAMI = 0x20;
        constexpr size_t SV_ENTRY_PALDEA = 0x18;

        int svLangSlot(uint8_t language)
        { // slot 6 unused, so 7+ shift down by two
            if (language == 0 || language == 6 || language > 10)
                return -1;
            return (language >= 7) ? language - 2 : language - 1;
        }

        // Which "genders seen" bits a species implies: a dual-gender species marks both, a fixed one
        // marks only its own. Mirrors PKHeX SetSeen (IsDualGender ? 3 : bit for FixedGender()).
        uint8_t svGenderSeenBits(uint8_t genderRatio)
        {
            if (genderRatio == 255) return 0x04;
            // female only
            if (genderRatio == 254) return 0x02;
            // male only
            if (genderRatio == 0) return 0x01;
            return 0x03;     // both
        }

        inline uint32_t rdU32(const std::vector<uint8_t> &d, size_t offset)
        {
            return static_cast<uint32_t>(d[offset]) | (static_cast<uint32_t>(d[offset + 1]) << 8) |
                   (static_cast<uint32_t>(d[offset + 2]) << 16) | (static_cast<uint32_t>(d[offset + 3]) << 24);
        }
        inline void wrU32(std::vector<uint8_t> &d, size_t offset, uint32_t value)
        {
            d[offset] = static_cast<uint8_t>(value);
            d[offset + 1] = static_cast<uint8_t>(value >> 8);
            d[offset + 2] = static_cast<uint8_t>(value >> 16);
            d[offset + 3] = static_cast<uint8_t>(value >> 24);
        }
        inline uint16_t rdU16(const std::vector<uint8_t> &d, size_t offset)
        {
            return static_cast<uint16_t>(d[offset] | (d[offset + 1] << 8));
        }
        inline void wrU16(std::vector<uint8_t> &d, size_t offset, uint16_t value)
        {
            d[offset] = static_cast<uint8_t>(value);
            d[offset + 1] = static_cast<uint8_t>(value >> 8);
        }
    }

    void Trainer9SV::updatePokedexBlock()
    {
        std::vector<uint8_t> *paldea = nullptr;
        std::vector<uint8_t> *kitakami = nullptr;
        for (auto &block : blocks)
        {
            if (block.key == ZUKAN9_SV_PALDEA)
                paldea = &block.data;
            else if (block.key == ZUKAN9_SV_KITAKAMI)
                kitakami = &block.data;
        }
        const bool useKitakami = (kitakami && !kitakami->empty());
        std::vector<uint8_t> *dex = useKitakami ? kitakami : paldea;
        if (!dex || dex->empty())
            return;
        const size_t entrySize = useKitakami ? SV_ENTRY_KITAKAMI : SV_ENTRY_PALDEA;
        size_t skippedOutOfRange = 0;

        auto registerMon = [&](const ::Pokemon::Pokemon *pokemon)
        {
            if (!pokemon || pokemon->isEgg())
                return;
            const uint16_t species = pokemon->speciesID();
            if (species == 0)
                return;
            const uint8_t form = pokemon->form();
            const ::Pokemon::PersonalInfo &pi = ::Pokemon::getPersonalInfo(species, form);
            // not in this game
            if ((pi.presence & ::Pokemon::PERSONAL_GAME_SV) == 0) return;
            // the form bitfields are u32; nothing real reaches this
            if (form > 31) return;

            // Entries are keyed by the INTERNAL id, which diverges from the National number at #917.
            const uint16_t internalId = ::Pokemon::gen9NationalToInternal(species);
            const size_t base = static_cast<size_t>(internalId) * entrySize;
            if (base + entrySize > dex->size())
            {
                ++skippedOutOfRange;
                return;
            }

            const bool shiny = pokemon->isShiny(pokemon->id32(), pokemon->species());
            const uint8_t gender = pokemon->gender(); // 0 male, 1 female, 2 genderless
            const int lang = svLangSlot(pokemon->language());
            const uint32_t formBit = 1u << form;

            if (useKitakami)
            {
                wrU32(*dex, base + 0x00, rdU32(*dex, base + 0x00) | formBit); // obtained
                wrU32(*dex, base + 0x04, rdU32(*dex, base + 0x04) | formBit); // seen
                wrU32(*dex, base + 0x08, rdU32(*dex, base + 0x08) | formBit); // heard of
                if (lang >= 0)
                    wrU16(*dex, base + 0x10, static_cast<uint16_t>(rdU16(*dex, base + 0x10) | (1u << lang)));
                (*dex)[base + 0x12] |= svGenderSeenBits(pi.genderRatio);
                (*dex)[base + 0x13] |= static_cast<uint8_t>(0x01 | (shiny ? 0x02 : 0x00)); // models seen
                // Displayed variant, one slot per regional dex (Paldea / Kitakami / Blueberry). A slot
                // is written only for a dex this species+FORM is actually listed in, and only where
                // nothing is displayed yet -- so a species first obtained as an alternate form shows
                // that form, and an entry the player already has keeps what it was showing.
                //
                // Membership is per form, not per species: Kantonian Diglett is a Paldea entry while
                // Alolan Diglett is a Blueberry one, and Alolan Meowth is in NO S/V dex at all. Writing
                // all three slots unconditionally made the regular Pokedex draw a regional variant it
                // can never list. PKHeX Zukan9 SetLocalStates does the same check.
                const ::Pokemon::DexEntry9SV &regional = ::Pokemon::getDexEntry9SV(species, form);
                const uint16_t listedIn[3] = {regional.paldea, regional.kitakami, regional.blueberry};
                const size_t slotAt[3] = {0x14, 0x18, 0x1C};
                for (int index = 0; index < 3; ++index)
                {
                    // not in that regional dex
                    if (listedIn[index] == 0) continue;
                    // already showing something
                    if ((*dex)[base + slotAt[index]] != 0) continue;
                    (*dex)[base + slotAt[index]] = form;
                    (*dex)[base + slotAt[index] + 1] = gender;
                    (*dex)[base + slotAt[index] + 2] = shiny ? 1 : 0;
                }
            }
            else
            {
                // state: obtained
                if (rdU32(*dex, base + 0x00) < 3) wrU32(*dex, base + 0x00, 3);
                wrU32(*dex, base + 0x04, rdU32(*dex, base + 0x04) | formBit); // forms obtained
                (*dex)[base + 0x08] |= svGenderSeenBits(pi.genderRatio);
                if (lang >= 0)
                    wrU16(*dex, base + 0x0A, static_cast<uint16_t>(rdU16(*dex, base + 0x0A) | (1u << lang)));
                if (shiny)
                    (*dex)[base + 0x0C] = 1;
                if (rdU16(*dex, base + 0x10) == 0 && (*dex)[base + 0x14] == 0)
                {
                    wrU16(*dex, base + 0x10, form);
                    (*dex)[base + 0x14] = gender;
                    (*dex)[base + 0x15] = shiny ? 1 : 0;
                }
            }
        };

        for (const auto &pokemon : party)
            registerMon(pokemon.get());
        for (const auto &box : boxes)
            for (const auto &pokemon : box)
                registerMon(pokemon.get());

        // An entry landing past the end of the block means the layout is not what this code assumes
        // (wrong live block, wrong entry size, or an internal id beyond what the save allocates).
        // Without saying so it reads as "the Pokedex just didn't update" -- the silent-skip failure.
        if (skippedOutOfRange != 0)
        {
            logErrorToFile("Pokedex: entries fell outside the Zukan block and were skipped",
                           (std::to_string(skippedOutOfRange) +
                            " skipped; block=" + std::string(useKitakami ? "Kitakami" : "Paldea") +
                            " size=" + std::to_string(dex->size()) + " entry=" + std::to_string(entrySize))
                               .c_str());
        }
    }

    void Trainer9SV::updateTrainerInfoBlock()
    {
        // Write money / OT name back to the blocks parse reads them from. encrypt() re-hashes.
        for (auto &block : blocks)
        {
            if (block.key == MY_STATUS9_SV)
            {
                if (block.data.size() >= 0x10 + 0x1A)
                    setString(&block.data[0x10], 0x1A, utf8ToUtf16(trainerName), 12);
            }
            else if (block.key == MONEY9_SV)
            {
                // MONEY9_SV is a u32 scalar block
                if (block.data.size() >= 4) writeUInt32LittleEndian(block.data.data(), money);
            }
        }
    }

    void Trainer9SV::updateItemBlock()
    {
        /**
         * Updates the ITEM block with modified inventory data.
         *
         * Process:
         * 1. Find the ITEM block
         * 2. Ensure block is large enough (0xBB80 bytes)
         * 3. Write items to their indexed positions (itemID * 0x10)
         *
         * Gen 9 stores items BY ITEM ID as index
         * Block size: 0xBB80 bytes (47,872 bytes)
         * Item size: 0x10 bytes (16 bytes)
         */

        for (auto &block : blocks)
        {
            if (block.key != ITEM9_SV)
                continue;

            // Size-preserving in-place write. Gen 9 stores every item at a fixed index (itemId * 0x10)
            // as {pouchId@0x00, count@0x04 (int32 LE), flags@0x08 (isNew=bit0, isFavorite=bit1, obtained=bit2)}.
            //
            // PKSE places each item in a pouch by legal-list membership (getPouchItems, mirroring
            // PKHeX's spans); the GAME instead keys the bag off the pouchId in each record. A freshly
            // CREATED item (add or change-type) lands in a never-held slot whose pouchId is
            // the "none" sentinel, so the game hides it even with the count set. We therefore stamp the
            // pouch's canonical pouchId (PKHeX InventoryItem9.Pouch*) on every present item -- a no-op
            // for items the game already had (same value), the fix for new ones. Index by PouchType9SV.
            // (Earlier this wrote count only, from before item creation existed; the whole
            // block must NOT be resized/rebuilt.)
            static const uint32_t POUCH_ID9_SV[POUCH_COUNT9_SV] = {0, 1, 2, 3, 4, 5, 9, 6, 7, 8};
            const size_t blockSize = block.data.size();
            for (int pouchIndex = 0; pouchIndex < static_cast<int>(POUCH_COUNT9_SV); pouchIndex++)
            {
                for (const auto &item : items[pouchIndex])
                {
                    const size_t offset = static_cast<size_t>(item.itemId) * ITEM_SIZE9_SV;
                    if (offset + ITEM_SIZE9_SV > blockSize)
                        continue;

                    const int32_t count = static_cast<int32_t>(item.count);
                    block.data[offset + 4] = static_cast<uint8_t>(count & 0xFF);
                    block.data[offset + 5] = static_cast<uint8_t>((count >> 8) & 0xFF);
                    block.data[offset + 6] = static_cast<uint8_t>((count >> 16) & 0xFF);
                    block.data[offset + 7] = static_cast<uint8_t>((count >> 24) & 0xFF);

                    if (item.count > 0)
                    {
                        const uint32_t pidValue = POUCH_ID9_SV[pouchIndex];
                        block.data[offset + 0] = static_cast<uint8_t>(pidValue & 0xFF);
                        block.data[offset + 1] = static_cast<uint8_t>((pidValue >> 8) & 0xFF);
                        block.data[offset + 2] = static_cast<uint8_t>((pidValue >> 16) & 0xFF);
                        block.data[offset + 3] = static_cast<uint8_t>((pidValue >> 24) & 0xFF);
                        // A record the save never held has no OBTAINED bit, and every item the game holds
                        // carries one; PKHeX sets it for any non-zero count. A no-op on game-written items.
                        block.data[offset + 8] = static_cast<uint8_t>(block.data[offset + 8] | ITEM_OBTAINED_FLAG9_SV);
                        // isNew = flags bit 0. Only SET (for freshly-added items); never clear, so the
                        // game's own "new" markers on existing items survive round-trips.
                        if (item.isNew)
                            block.data[offset + 8] = static_cast<uint8_t>(block.data[offset + 8] | ITEM_NEW_FLAG9_SV);
                    }
                }
            }
            break;
        }
    }
}
