#include <algorithm>
#include <cstring>

#include "Trainer/Trainer9LZA.h"
#include "Pokemon/SpeciesConverter9.h" // gen9NationalToInternal -- dex entries are keyed by internal id
#include "Pokemon/PersonalInfoTable.h" // getPersonalInfo -> presence + genderRatio
#include "Pokemon/FormInfo.h"          // isMegaForm / hasMegaForm -- the entry's "seen mega" bits
#include "Trainer/Inventory9LZA.h"
#include "Names/ItemPouches.h" // getPouchItems -- per-pouch legal ids
#include "Utils/Logger.h"

using namespace Utils;

namespace Trainer
{

    void Trainer9LZA::parseBlock(const Block &block)
    {
        switch (block.key)
        {
        case MY_STATUS9_LZA:
            parseMyStatusBlock(block);
            break;
        case PARTY9_LZA:
            parsePartyBlock(block);
            break;
        case MONEY9_LZA:
            parseMoneyBlock(block);
            break;
        case ITEM9_LZA:
            parseItemBlock(block);
            break;
        case BOX9_LZA:
            parseBoxBlock(block);
            break;
        case BOX_LAYOUT9_LZA:
            parseBoxLayoutBlock(block);
            break;
        case CURRENT_BOX9_LZA:
            parseCurrentBoxBlock(block);
            break;
        case SAVE_REVISION9_LZA:
            parseSaveRevisionBlock(block);
            break;
        default:
            break;
        }
    }

    void Trainer9LZA::parseMyStatusBlock(const Block &block)
    {
        if (block.data.size() < 0x00 + 4)
        {
            logInfoToFile("Insufficient data for UInt32 at offset 0x00 in MY_STATUS block");
            return;
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

    void Trainer9LZA::parsePartyBlock(const Block &block)
    {
        /**
         * PARTY Block Structure (Pokemon Legends Z-A):
         * Pokemon stored with gaps between slots:
         * - Slot 0: offset 0 (SIZE_PARTY9_LZA bytes of data + GAP_BOX_SLOT9_LZA gap)
         * - Slot 1: offset PARTY_SLOT_SIZE9_LZA (480 bytes)
         * - Slot 2: offset 2 * PARTY_SLOT_SIZE9_LZA
         * - ... up to 6 slots
         *
         * Each slot spans PARTY_SLOT_SIZE9_LZA bytes (480 bytes), but only the first
         * SIZE_PARTY9_LZA bytes (344 bytes) contain Pokemon data. The remaining gap
         * (GAP_BOX_SLOT9_LZA = 0x88 bytes) is unused/padding.
         */
        const std::span<const std::byte> blockSpan(reinterpret_cast<const std::byte *>(block.data.data()), block.data.size());

        for (size_t slot = 0; slot < MAX_PARTY_SLOTS; ++slot)
        {
            // Calculate offset to this slot (packed for S/V, gapped for Z-A)
            const size_t offset = slot * partySlotStride;
            if (offset + SIZE_PARTY9_LZA > block.data.size())
                break;

            std::span<const std::byte> slotSpan = blockSpan.subspan(offset, SIZE_PARTY9_LZA);

            bool isEmptySlot = true;
            for (size_t slotSpanIndex = 0; slotSpanIndex < SIZE_PARTY9_LZA && slotSpanIndex < slotSpan.size(); ++slotSpanIndex)
            {
                if (slotSpan[slotSpanIndex] != std::byte{0})
                {
                    isEmptySlot = false;
                    break;
                }
            }

            if (!isEmptySlot)
            {
                // Decrypt and create Pokemon9LZA object as unique_ptr
                // Pokemon9LZA constructor handles decryption automatically
                party.push_back(std::make_unique<Pokemon9LZA>(slotSpan));
            }
        }
    }

    void Trainer9LZA::parseMoneyBlock(const Block &block)
    {
        /**
         * MONEY Block (Gen 9 Legends Z-A):
         * For Gen 9 Legends Z-A, the money value is stored directly as the block value.
         * 0x00: Money (4 bytes)
         */
        if (block.data.size() < 4)
        {
            return;
        }

        this->money = readUInt32LittleEndian(block.data.data());
    }

    void Trainer9LZA::parseItemBlock(const Block &block)
    {
        /**
         * ITEM Block Structure (Gen 9 Legends Z-A):
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

        items.resize(static_cast<size_t>(POUCH_COUNT9_LZA));

        // For each pouch, iterate through valid item IDs
        for (int pouchIndex = 0; pouchIndex < static_cast<int>(POUCH_COUNT9_LZA); pouchIndex++)
        {
            items[pouchIndex].clear();

            // Legal ids for this pouch, from the generated PKHeX-derived table. This replaced a
            // hand-written list that was byte-identical to the other Gen 9 game's and wrong for
            // both -- it bucketed vitamins under Battle Items and claimed pockets the
            // game does not have. Pouch membership is display-only: the write below is keyed on
            // item id, not pouch.
            const auto validIds = Names::getPouchItems(Enums::GameVersion::ZA, static_cast<size_t>(pouchIndex));

            for (uint16_t itemId : validIds)
            {
                size_t offset = itemId * ITEM_SIZE9_LZA;

                // Make sure we're within bounds
                if (offset + ITEM_SIZE9_LZA > block.data.size())
                {
                    continue;
                }

                InventoryItem9LZA item = InventoryItem9LZA::fromBytes(itemId, &block.data[offset]);

                // Only add if count > 0
                if (item.count > 0)
                {
                    items[pouchIndex].push_back(item);
                }
            }
        }
    }

    void Trainer9LZA::parseBoxBlock(const Block &block)
    {
        const std::span<const std::byte> blockSpan(reinterpret_cast<const std::byte *>(block.data.data()), block.data.size());

        for (size_t boxIndex = 0; boxIndex < BOX_COUNT9_LZA; ++boxIndex)
        {
            for (size_t slot = 0; slot < BOX_SLOTS; ++slot)
            {
                // Calculate offset (packed for S/V, gapped for Z-A)
                const size_t offset = (boxIndex * BOX_SLOTS + slot) * boxSlotStride;
                if (offset + SIZE_PARTY9_LZA > block.data.size())
                {
                    break;
                }

                std::span<const std::byte> slotSpan = blockSpan.subspan(offset, SIZE_PARTY9_LZA);

                bool isEmptySlot = true;
                for (size_t slotSpanIndex = 0; slotSpanIndex < SIZE_PARTY9_LZA && slotSpanIndex < slotSpan.size(); ++slotSpanIndex)
                {
                    if (slotSpan[slotSpanIndex] != std::byte{0})
                    {
                        isEmptySlot = false;
                        break;
                    }
                }

                if (!isEmptySlot)
                {
                    boxes[boxIndex][slot] = std::make_unique<Pokemon9LZA>(slotSpan);
                }
                else
                {
                    boxes[boxIndex][slot] = nullptr;
                }
            }
        }
    }

    void Trainer9LZA::parseBoxLayoutBlock(const Block &block)
    {
        for (size_t boxIndex = 0; boxIndex < BOX_COUNT9_LZA; ++boxIndex)
        {
            size_t offset = boxIndex * BOX_NAME_LENGTH9_LZA;
            if (offset + BOX_NAME_LENGTH9_LZA <= block.data.size())
            {
                std::u16string boxNameU16 = getString(block.data.data() + offset, BOX_NAME_LENGTH9_LZA);
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

    void Trainer9LZA::parseCurrentBoxBlock(const Block &block)
    {
        // "U32 Box Index" -- a scalar block; PKHeX reads it as a byte (0..31 fits one byte).
        if (block.data.empty())
            return;
        uint8_t box = block.data[0];
        if (box < BOX_COUNT9_LZA)
            this->currentBox = box;
    }

    void Trainer9LZA::updateCurrentBoxBlock()
    {
        // Inverse of parseCurrentBoxBlock: write the low byte and clear the rest of the scalar
        // (PKHeX stores it via SetValue<byte>, so the upper bytes are 0).
        for (auto &block : blocks)
        {
            if (block.key != CURRENT_BOX9_LZA || block.data.empty())
                continue;
            block.data[0] = static_cast<uint8_t>(currentBox);
            for (size_t moveIndex = 1; moveIndex < block.data.size() && moveIndex < 4; ++moveIndex)
                block.data[moveIndex] = 0;
            break;
        }
    }

    void Trainer9LZA::parseSaveRevisionBlock(const Block &block)
    {
        /**
         * SAVE_REVISION Block Structure (Pokemon Legends Z-A):
         * Contains a u64 (8 bytes) value indicating the save revision:
         * - 0: Base game (version 1.0.x)
         * - 1: Mega Dimension DLC (version 2.0.0+)
         *
         * This value is critical for determining:
         * - Which Pokemon species are available
         * - Which moves are legal
         * - Which items exist
         * - Block key compatibility
         */
        if (block.data.size() < 8)
        {
            // Default to base game if block is missing or too small
            this->saveRevision = 0;
            this->saveRevisionString = "Base";
            this->gameVersionString = "v1.0";
            logInfoToFile("SAVE_REVISION block too small, defaulting to Base");
            return;
        }

        uint64_t revision = readUInt64LittleEndian(block.data.data());
        this->saveRevision = static_cast<int>(revision);

        switch (this->saveRevision)
        {
        case 0:
            this->saveRevisionString = "Base";
            this->gameVersionString = "v1.0"; // Base game v1.0.x
            break;
        case 1:
            this->saveRevisionString = "Mega Dimension";
            this->gameVersionString = "v2.0"; // Mega Dimension DLC requires v2.0.0+
            break;
        case 2:
            // PKHeX labels it "-EOL" (End of Life): written by 2.0.1 and later.
            this->saveRevisionString = "End of Life";
            this->gameVersionString = "v2.0.1+";
            break;
        default:
            // Future DLC/updates
            this->saveRevisionString = "Rev " + std::to_string(this->saveRevision);
            this->gameVersionString = "v" + std::to_string(this->saveRevision + 1) + ".0";
            break;
        }

        char buffer[128];
        snprintf(buffer, sizeof(buffer), "Detected save revision: %d (%s, %s)", this->saveRevision, this->saveRevisionString.c_str(), this->gameVersionString.c_str());
        logInfoToFile(buffer);
    }

    void Trainer9LZA::updatePartyBlock()
    {
        /**
         * Updates the PARTY block with modified Pokemon data.
         *
         * Empty party slots must be the game's encrypted "blank" (which DECRYPTS to species 0), NOT
         * literal zeros -- exactly like updateBoxBlock(). The game decrypts every slot and validates it,
         * so a zeroed slot decrypts to garbage and renders a BAD EGG on every empty slot. Confirmed on a
         * real Sword/Shield save, whose empty party slots hold 331 non-zero bytes of 344 and decrypt to
         * species 0; Z-A shares the entity format and the crypto, and this game's own updateBoxBlock
         * already carries the same rule. The party is re-serialized on EVERY save, so a memset(0) here
         * corrupts the party even when the edit only touched a box.
         *
         * Z-A GAPS its party slots (stride PARTY_SLOT_SIZE9_LZA > SIZE_PARTY9_LZA). A newly emptied slot
         * is cleared whole and gets the blank plus the footer, as PKHeX's Party9a.WriteEmpty does; an
         * occupied slot writes the entity and the footer and leaves the rest of the gap alone.
         */
        for (auto &block : blocks)
        {
            if (block.key == PARTY9_LZA)
            {
                // Ensure the block data is large enough (including gaps). Only ever grows, so a
                // real save's trailing party-count bytes survive.
                size_t requiredSize = MAX_PARTY_SLOTS * partySlotStride;
                if (block.data.size() < requiredSize)
                {
                    block.data.resize(requiredSize, 0);
                }

                // Which slots ALREADY read as empty, decided on the bytes in the file rather than on
                // party.size()? Those are left byte-for-byte alone below -- gap included. There is no single canonical blank to
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
                    if (byteOffset + SIZE_PARTY9_LZA > block.data.size())
                        break;

                    bool anyNonZero = false;
                    for (size_t byteIndex = 0; byteIndex < SIZE_PARTY9_LZA && !anyNonZero; ++byteIndex)
                        anyNonZero = (block.data[byteOffset + byteIndex] != 0);
                    // zeroed by an older build -> repair it below
                    if (!anyNonZero) continue;

                    std::vector<std::byte> encryptedRecord(SIZE_PARTY9_LZA);
                    std::memcpy(encryptedRecord.data(), &block.data[byteOffset], SIZE_PARTY9_LZA);
                    std::byte *dec = decryptArray9LZA(std::span<const std::byte>(encryptedRecord.data(), SIZE_PARTY9_LZA));
                    alreadyEmpty[partySlotIndex] = (readUInt16LittleEndian(reinterpret_cast<const uint8_t *>(dec) + 0x08) == 0);
                    delete[] dec;
                }

                // For a slot that must BECOME empty (a Pokemon was removed), and for repairing a
                // slot an older build zeroed into a Bad Egg: an all-zero PK9 encrypted with EC 0,
                // padded out to the gapped stride with zeros.
                std::vector<uint8_t> blankSlot;
                {
                    std::vector<std::byte> zero(SIZE_PARTY9_LZA, std::byte{0});
                    std::byte *encryptedRecord = encryptArray9LZA(std::span<const std::byte>(zero.data(), SIZE_PARTY9_LZA), 0);
                    blankSlot.assign(reinterpret_cast<const uint8_t *>(encryptedRecord), reinterpret_cast<const uint8_t *>(encryptedRecord) + SIZE_PARTY9_LZA);
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
                    writeSlotFooter(&block.data[offset], true);
                };

                for (size_t partySlotIndex = 0; partySlotIndex < party.size() && partySlotIndex < MAX_PARTY_SLOTS; ++partySlotIndex)
                {
                    // Calculate offset (packed for S/V, gapped for Z-A)
                    const size_t offset = partySlotIndex * partySlotStride;
                    if (slotHoldsLoadedRecord(party[partySlotIndex].get(), &block.data[offset], SIZE_PARTY9_LZA, decryptArray9LZA))
                        continue;

                    if (party[partySlotIndex] && party[partySlotIndex]->speciesID() != 0)
                    {
                        const ::Pokemon::Pokemon *pokemon = party[partySlotIndex].get();
                        uint32_t encryptionConstant = readUInt32LittleEndian(reinterpret_cast<const uint8_t *>(pokemon->getData().data()));

                        std::span<const std::byte> decryptedSpan(pokemon->getData().data(), pokemon->getDataSize());

                        std::byte *encryptedData = encryptArray9LZA(decryptedSpan, encryptionConstant);

                        std::memcpy(&block.data[offset], encryptedData, SIZE_PARTY9_LZA);
                        writeSlotFooter(&block.data[offset], true);

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

    void Trainer9LZA::updateBoxNameBlock()
    {
        // Inverse of parseBoxLayoutBlock. See Trainer8SWSH::updateBoxNameBlock for why the block is
        // bounds-checked rather than resized.
        for (auto &block : blocks)
        {
            if (block.key != BOX_LAYOUT9_LZA)
                continue;
            for (size_t boxIndex = 0; boxIndex < BOX_COUNT9_LZA && boxIndex < boxNames.size(); ++boxIndex)
            {
                // never persist a display default
                if (!isBoxNameDirty(boxIndex)) continue;
                const size_t offset = boxIndex * BOX_NAME_LENGTH9_LZA;
                if (offset + BOX_NAME_LENGTH9_LZA > block.data.size())
                    break;
                setString(block.data.data() + offset, BOX_NAME_LENGTH9_LZA, utf8ToUtf16(boxNames[boxIndex]), BOX_NAME_LENGTH9_LZA / 2 - 1);
            }
            break;
        }
    }

    void Trainer9LZA::updateBoxBlock()
    {
        /**
         * Updates the BOX block with modified Pokemon data.
         *
         * Process similar to updatePartyBlock, but for all boxes:
         * 1. Find the BOX block
         * 2. Ensure block is large enough (32 boxes * 30 slots * BOX_SLOT_SIZE9_LZA)
         * 3. Each slot is decided from its POSITION in the file, as PKHeX writes only the slot it
         *    sets: bytes that still hold this slot's in-memory record, or an empty slot whose bytes
         *    already read as empty, are left alone; an occupied slot is written; anything else (a
         *    Pokemon moved or removed from here, a zeroed slot) gets a synthesized blank. Never
         *    zeros (a Bad Egg in-game), and never one slot's bytes harvested as "the" blank: after
         *    a Move the first empty slot in memory can still hold the moved Pokemon in the file.
         *    A written slot keeps its gap, apart from the footer PKHeX writes (writeSlotFooter).
         */
        for (auto &block : blocks)
        {
            if (block.key == BOX9_LZA)
            {
                size_t requiredSize = BOX_COUNT9_LZA * BOX_SLOTS * boxSlotStride;
                if (block.data.size() < requiredSize)
                {
                    block.data.resize(requiredSize, 0);
                }

                // An all-zero PK9 encrypted with EC 0 (== PKHeX's BlankPKM).
                std::vector<uint8_t> blankSlot;
                {
                    std::vector<std::byte> zero(SIZE_PARTY9_LZA, std::byte{0});
                    std::byte *encryptedRecord = encryptArray9LZA(std::span<const std::byte>(zero.data(), SIZE_PARTY9_LZA), 0);
                    blankSlot.assign(reinterpret_cast<const uint8_t *>(encryptedRecord), reinterpret_cast<const uint8_t *>(encryptedRecord) + SIZE_PARTY9_LZA);
                    delete[] encryptedRecord;
                }

                for (size_t boxIndex = 0; boxIndex < BOX_COUNT9_LZA; ++boxIndex)
                {
                    for (size_t slot = 0; slot < BOX_SLOTS; ++slot)
                    {
                        const size_t offset = (boxIndex * BOX_SLOTS + slot) * boxSlotStride;
                        const auto &pokemon = boxes[boxIndex][slot];
                        if (slotHoldsLoadedRecord(pokemon.get(), &block.data[offset], SIZE_PARTY9_LZA, decryptArray9LZA))
                            continue;

                        if (pokemon && pokemon->speciesID() != 0)
                        {
                            uint32_t encryptionConstant = readUInt32LittleEndian(reinterpret_cast<const uint8_t *>(pokemon->getData().data()));
                            std::span<const std::byte> decryptedSpan(pokemon->getData().data(), pokemon->getDataSize());
                            std::byte *encryptedData = encryptArray9LZA(decryptedSpan, encryptionConstant);
                            std::memcpy(&block.data[offset], encryptedData, SIZE_PARTY9_LZA);
                            delete[] encryptedData;
                            writeSlotFooter(&block.data[offset], false);
                        }
                        else if (!slotHoldsGameBlank(&block.data[offset], SIZE_PARTY9_LZA, RECORD_SPECIES_OFFSET, decryptArray9LZA))
                        {
                            std::memcpy(&block.data[offset], blankSlot.data(), SIZE_PARTY9_LZA);
                            writeSlotFooter(&block.data[offset], false);
                        }
                    }
                }
                break;
            }
        }
    }

    void Trainer9LZA::writeSlotFooter(uint8_t *slotBytes, bool isPartySlot) const
    {
        if (saveRevision != 0)
            slotBytes[PRESENT_FLAG_OFFSET9_LZA] = 1;
        if (isPartySlot)
            slotBytes[PARTY_FOOTER_OFFSET9_LZA] = 1;
    }

    std::unique_ptr<::Pokemon::Pokemon> Trainer9LZA::createBlankPokemon() const
    {
        // Mirror updateBoxBlock()'s encrypted-blank fallback: a zeroed *decrypted* PK9 buffer
        // encrypted with EC/seed 0, then fed to the ctor (which decrypts it straight back to zeros)
        // -> a clean species-0, Sanity-0, checksum-valid entity. Raw zeros in the ctor would decrypt
        // to garbage (BAD EGG); the encrypt->decrypt round-trip is what makes the blank valid.
        std::vector<std::byte> zero(SIZE_PARTY9_LZA, std::byte{0});
        std::byte *encryptedRecord = encryptArray9LZA(std::span<const std::byte>(zero.data(), SIZE_PARTY9_LZA), 0);
        auto clone = std::make_unique<Pokemon9LZA>(std::span<const std::byte>(encryptedRecord, SIZE_PARTY9_LZA));
        delete[] encryptedRecord;
        return clone;
    }

    //
    // Z-A has ONE dex block, not Scarlet/Violet's pair, and a much bigger entry: 0x84 bytes against
    // S/V's 0x20. It is still keyed by the Gen 9 INTERNAL species id (SpeciesConverter9), and it still
    // uses per-form bitfields -- but it records more per form, because Z-A is the Mega game.
    //
    // Entry, 0x84 bytes (PKHeX PokeDexEntry9a):
    //   0x00 u32 forms caught        0x04 u32 forms seen      0x08 u16 language flags
    //   0x0A u8  is-new ("NEW" badge -- Z-A has a real flag for this; S/V does not)
    //   0x0B u8  genders seen (1 male, 2 female, 4 genderless)
    //   0x0C u32 forms seen SHINY (per form, unlike S/V's single models-seen byte)
    //   0x10 u8  megas seen (bit per mega slot)   0x11 u8 alpha seen
    //   0x5A/0x5B/0x5C  displayed form / gender / shiny      (0x12-0x59 and 0x5D+ are unused)
    //
    // The displayed GENDER is not the raw gender byte: it is PKHeX's DisplayGender9a, where 3 means
    // "gendered but the two look the same". Only species with a visible gender difference store 1/2.
    namespace
    {
        constexpr size_t ZA_ENTRY_SIZE = 0x84;

        int zaLangSlot(uint8_t language)
        { // slot 6 unused, so 7+ shift down by two; Latin American Spanish (11) is the last, slot 9
          // (PKHeX PokeDexEntry9a.GetDexLangFlag, MaxLanguageID = SpanishL)
            if (language == 0 || language == 6 || language > 11)
                return -1;
            return (language >= 7) ? language - 2 : language - 1;
        }

        inline uint32_t rdU32(const std::vector<uint8_t> &d, size_t offset)
        {
            return static_cast<uint32_t>(d[offset]) | (static_cast<uint32_t>(d[offset + 1]) << 8) | (static_cast<uint32_t>(d[offset + 2]) << 16) | (static_cast<uint32_t>(d[offset + 3]) << 24);
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

        // Species that get a SECOND mega slot (bit 1 of the megas-seen byte), regardless of which form
        // was registered. PKHeX Zukan9a.IsMegaFormXY / IsMegaFormZA plus its two explicit extras.
        //
        // Only Charizard and Mewtwo have their second Mega in the base game; the other four pairs and
        // the two odd ones arrive with Mega Dimension, so they are gated on the save revision.
        //
        // PKHeX sets Meowstic and Magearna UNCONDITIONALLY, and that is wrong against a real save: a
        // revision-0 Z-A save with Meowstic forms 0 and 1 registered has this byte at 0x00, not 0x02.
        // Its own GetFallbackBitsDLC agrees, listing Meowstic only in the DLC branch. Magearna is not
        // in the test save, but its Megas are Mega Dimension content too, so it is gated the same way.
        bool zaSecondMegaSlot(uint16_t species, int saveRevision)
        {
            switch (species)
            {
            case 6:   // Charizard  Mega X / Y   -- base game
            case 150: // Mewtwo     Mega X / Y   -- base game
                return true;
            case 26:  // Raichu     Mega X / Y
            case 359: // Absol      Mega + Mega Z
            case 445: // Garchomp   Mega + Mega Z
            case 448: // Lucario    Mega + Mega Z
            case 678: // Meowstic   gendered Megas
            case 801: // Magearna   Mega + Original Color
                return saveRevision != 0;
            default:
                return false;
            }
        }

        // Forms the game treats as one unit: registering any of them reveals the others, because the
        // pokemon shifts between them freely. PKHeX Zukan9a.GetFormExtraFlags.
        uint32_t zaFormExtraFlags(uint16_t species)
        {
            switch (species)
            {
            case 676:
                return 0x01; // Furfrou    base trim
            case 681:
                return 0x03; // Aegislash  Shield + Blade
            case 479:
                return 0x3F; // Rotom      all 5 appliances + base
            case 648:
                return 0x03; // Meloetta   Aria + Pirouette
            case 649:
                return 0x1F; // Genesect   all 4 drives + base
            case 778:
                return 0x03; // Mimikyu    Disguised + Busted
            case 877:
                return 0x03; // Morpeko    Full Belly + Hangry
            case 720:
                return 0x03; // Hoopa      Confined + Unbound
            case 718:
                return 0x10; // Zygarde    Complete
            default:
                return 0;
            }
        }

        // The same idea for SHINY flags, which Z-A stores per form. Wider than the above because a
        // shiny reveals the shiny model of every form it can take -- including Megas, which is why a
        // plain shiny Charizard marks its Mega X/Y shiny as seen. PKHeX Zukan9a.GetFormExtraFlagsShinySeen.
        uint32_t zaShinyExtraFlags(uint16_t species, uint8_t form, int saveRevision)
        {
            switch (species)
            {
            case 676:
                return 0x01; // Furfrou
            case 681:
                return 0x03; // Aegislash
            case 664:
            case 665:           // Scatterbug / Spewpa -> all 20 Vivillon patterns.
                return 0xFFFFF; //   (deliberately NOT Vivillon itself)
            case 479:
                return 0x3F; // Rotom
            case 648:
                return 0x03; // Meloetta
            case 649:
                return 0x1F; // Genesect
            case 778:
                return 0x03; // Mimikyu
            case 877:
                return 0x03; // Morpeko
            case 720:
                return 0x03; // Hoopa
            case 978:
                return (form <= 2) ? (8u << form) : 0u; // Tatsugiri per-form Mega
            case 801:
                return (form == 1) ? 8u : (form == 0 ? 4u : 0u); // Magearna Mega
            case 718:
                return 0x3F; // Zygarde   all forms + Mega
            case 658:
                return (form != 2) ? 0x0B : 0u; // Greninja  Mega + Ash/base
            case 670:
                return (form == 5) ? 0x20 : 0u; // Floette   Mega (not form 1)
            default:
                break;
            }
            // Fallback: reveal the species' Mega slots. Two for the Mega X/Y and Mega Z species, one
            // for everything else that has a Mega at all.
            if (zaSecondMegaSlot(species, saveRevision))
            {
                switch (species)
                {
                case 678:
                    return 0x04; // Meowstic  Mega only (M, F, Mega)
                case 26:
                    return 0x0C; // Raichu    X and Y (base, Alolan, X, Y)
                default:
                    return 0x06; // base, Mega X, Mega Y  /  Mega + Mega Z
                }
            }
            return ::Pokemon::hasMegaForm(species) ? 0x02u : 0u;
        }

        // PKHeX PokeDexEntry9a.GetDisplayGender -> DisplayGender9a.
        // 0 genderless, 1 male, 2 female, 3 gendered but the sexes look identical.
        // Only species with a real visual difference store 1/2 -- everything else stores 3.
        uint8_t zaDisplayGender(uint8_t gender, uint16_t species, uint8_t genderRatio)
        {
            if (gender == 2 || genderRatio == 255) return 0;
            // male only
            if (genderRatio == 0) return 1;
            // female only
            if (genderRatio == 254) return 2;
            switch (species)
            { // PKHeX BiGender + BiGenderDLC -- species with distinct models
            case 3:
            case 25:
            case 26:
            case 41:
            case 42:
            case 64:
            case 65:
            case 123:
            case 129:
            case 130:
            case 133:
            case 154:
            case 208:
            case 212:
            case 214:
            case 229:
            case 252:
            case 253:
            case 254:
            case 255:
            case 256:
            case 257:
            case 258:
            case 259:
            case 260:
            case 307:
            case 308:
            case 315:
            case 322:
            case 323:
            case 396:
            case 397:
            case 398:
            case 407:
            case 443:
            case 444:
            case 445:
            case 449:
            case 450:
            case 459:
            case 460:
            case 485:
            case 668:
                return (gender == 0) ? 1 : 2;
            default:
                return 3; // gendered, no visible difference
            }
        }
    }

    void Trainer9LZA::updatePokedexBlock()
    {
        std::vector<uint8_t> *dex = nullptr;
        for (auto &block : blocks)
        {
            if (block.key == POKEDEX9_LZA)
            {
                dex = &block.data;
                break;
            }
        }
        if (!dex || dex->empty())
            return;
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
            if ((pi.presence & ::Pokemon::PERSONAL_GAME_ZA) == 0) return;
            // the form bitfields are u32; nothing real reaches this
            if (form > 31) return;

            const uint16_t internalId = ::Pokemon::gen9NationalToInternal(species);
            const size_t base = static_cast<size_t>(internalId) * ZA_ENTRY_SIZE;
            if (base + ZA_ENTRY_SIZE > dex->size())
            {
                ++skippedOutOfRange;
                return;
            }

            const bool shiny = pokemon->isShiny(pokemon->id32(), pokemon->species());
            const uint8_t gender = pokemon->gender(); // 0 male, 1 female, 2 genderless
            const int lang = zaLangSlot(pokemon->language());
            const uint32_t formBit = 1u << form;

            // "Was this species already caught?" decides whether the displayed variant gets set, so it
            // has to be read BEFORE the caught flags are written.
            const bool alreadyCaught = rdU32(*dex, base + 0x00) != 0;

            (*dex)[base + 0x0B] |= static_cast<uint8_t>(1u << (gender > 2 ? 2 : gender));
            wrU32(*dex, base + 0x04, rdU32(*dex, base + 0x04) | formBit); // seen
            wrU32(*dex, base + 0x00, rdU32(*dex, base + 0x00) | formBit); // caught

            if (::Pokemon::isMegaForm(species, form))
                (*dex)[base + 0x10] |= 0x01;
            if (zaSecondMegaSlot(species, saveRevision))
                (*dex)[base + 0x10] |= 0x02;
            else if (species == 978 /*Tatsugiri*/)
                (*dex)[base + 0x10] |= static_cast<uint8_t>(1u << (form < 3 ? form : (form - 3) & 0x03));

            if (lang >= 0)
                wrU16(*dex, base + 0x08, static_cast<uint16_t>(rdU16(*dex, base + 0x08) | (1u << lang)));

            if (shiny)
            {
                wrU32(*dex, base + 0x0C, rdU32(*dex, base + 0x0C) | formBit | zaShinyExtraFlags(species, form, saveRevision));
            }

            if (!alreadyCaught)
            {
                (*dex)[base + 0x5A] = form;
                (*dex)[base + 0x5B] = zaDisplayGender(gender, species, pi.genderRatio);
                (*dex)[base + 0x5C] = shiny ? 1 : 0;
                const uint32_t extra = zaFormExtraFlags(species);
                if (extra)
                {
                    wrU32(*dex, base + 0x04, rdU32(*dex, base + 0x04) | extra);
                    wrU32(*dex, base + 0x00, rdU32(*dex, base + 0x00) | extra);
                }
                (*dex)[base + 0x0A] = 1; // the "NEW" badge
            }

            if (pokemon->getGameGroup() == Enums::GameVersion::ZA && static_cast<const Pokemon9LZA *>(pokemon)->isAlpha())
                (*dex)[base + 0x11] = 1;
        };

        for (const auto &pokemon : party)
            registerMon(pokemon.get());
        for (const auto &box : boxes)
            for (const auto &pokemon : box)
                registerMon(pokemon.get());

        // An entry landing past the end of the block means the layout is not what this code assumes.
        // Without saying so it reads as "the Pokedex just didn't update" -- the silent-skip failure.
        if (skippedOutOfRange != 0)
        {
            logErrorToFile("Pokedex: entries fell outside the Zukan block and were skipped", (std::to_string(skippedOutOfRange) + " skipped; size=" + std::to_string(dex->size()) + " entry=" + std::to_string(ZA_ENTRY_SIZE)).c_str());
        }
    }

    void Trainer9LZA::updateTrainerInfoBlock()
    {
        // Write money / OT name back to the blocks parse reads them from. encrypt() re-hashes.
        for (auto &block : blocks)
        {
            if (block.key == MY_STATUS9_LZA)
            {
                if (block.data.size() >= 0x10 + 0x1A)
                    setString(&block.data[0x10], 0x1A, utf8ToUtf16(trainerName), 12);
            }
            else if (block.key == MONEY9_LZA)
            {
                // MONEY9_LZA is a u32 scalar block
                if (block.data.size() >= 4) writeUInt32LittleEndian(block.data.data(), money);
            }
        }
    }

    void Trainer9LZA::updateItemBlock()
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
            if (block.key != ITEM9_LZA)
                continue;

            // Size-preserving in-place write. Gen 9 stores every item at a fixed index (itemId * 0x10)
            // as {pouchId@0x00, count@0x04 (int32 LE), flags@0x08 (isNew=bit0, isFavorite=bit1)}.
            //
            // PKSE places each item in a pouch by legal-list membership (getPouchItems, mirroring
            // PKHeX's spans); the GAME instead keys the bag off the pouchId in each record. A freshly
            // CREATED item (add or change-type) lands in a never-held slot whose pouchId is
            // the "none" sentinel, so the game hides it even with the count set -- exactly the "Canari
            // Bread shows in PKSE but not in-game" report. We stamp the pouch's canonical pouchId
            // (PKHeX InventoryItem9a.Pouch*) on every present item -- a no-op for items the game already
            // had, the fix for new ones. Index by PouchType9LZA. (never resize/rebuild.)
            static const uint32_t POUCH_ID9_LZA[POUCH_COUNT9_LZA] = {0, 1, 5, 2, 6, 7, 3, 4};
            const size_t blockSize = block.data.size();
            for (int pouchIndex = 0; pouchIndex < static_cast<int>(POUCH_COUNT9_LZA); pouchIndex++)
            {
                for (const auto &item : items[pouchIndex])
                {
                    const size_t offset = static_cast<size_t>(item.itemId) * ITEM_SIZE9_LZA;
                    if (offset + ITEM_SIZE9_LZA > blockSize)
                        continue;

                    const int32_t count = static_cast<int32_t>(item.count);
                    block.data[offset + 4] = static_cast<uint8_t>(count & 0xFF);
                    block.data[offset + 5] = static_cast<uint8_t>((count >> 8) & 0xFF);
                    block.data[offset + 6] = static_cast<uint8_t>((count >> 16) & 0xFF);
                    block.data[offset + 7] = static_cast<uint8_t>((count >> 24) & 0xFF);

                    if (item.count > 0)
                    {
                        const uint32_t pidValue = POUCH_ID9_LZA[pouchIndex];
                        block.data[offset + 0] = static_cast<uint8_t>(pidValue & 0xFF);
                        block.data[offset + 1] = static_cast<uint8_t>((pidValue >> 8) & 0xFF);
                        block.data[offset + 2] = static_cast<uint8_t>((pidValue >> 16) & 0xFF);
                        block.data[offset + 3] = static_cast<uint8_t>((pidValue >> 24) & 0xFF);
                        // isNew = flags bit 0. Only SET (freshly-added items); never clear, so the
                        // game's own "new" markers on existing items survive round-trips.
                        if (item.isNew)
                            block.data[offset + 8] = static_cast<uint8_t>(block.data[offset + 8] | 0x01);
                        // IsNewNotify (bit 2) means "never held": the game shows its first-pickup
                        // flyout while it is set, and it is always clear once any quantity is held
                        // (PKHeX InventoryPouch9a.EnsureFlagsConsistent). An item never held before
                        // still carries it, so it is cleared on every held item; an unedited save
                        // has none set and stays byte for byte.
                        block.data[offset + 8] = static_cast<uint8_t>(block.data[offset + 8] & ~0x04);
                    }
                }
            }
            break;
        }
    }
}
