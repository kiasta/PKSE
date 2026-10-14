#include <algorithm>
#include <cstring>
#include <vector>

#include "Trainer/Trainer7USUM.h"
#include "Trainer/Inventory7USUM.h"
#include "Encryption/Encryption7USUM.h"
#include "Utils/CRC16.h"
#include "Utils/Utf16Text.h"
#include "Utils/HelperUtilities.h"
#include "Utils/StringHelpers.h"
#include "Utils/Logger.h"
#include "Utils/SHA256.h"

extern "C"
{
#include "memecrypto.h"
}

using namespace Utils;

namespace Trainer
{
    uint16_t Trainer7USUM::blockCrc(const uint8_t *bytes, size_t byteCount) const noexcept
    {
        return crc16Invert(bytes, byteCount);
    }

    namespace
    {
        constexpr uint32_t BEEF_MAGIC = 0x42454546u; // read as a u32 LE, per PKHeX
        constexpr size_t BEEF_MAGIC_OFS = 0x1F0;     // from the END of the file

        // MyStatus: identical in both generations except where the OT name sits.
        constexpr size_t ST_ID32 = 0x00;
        constexpr size_t ST_GAME = 0x04; // PKHeX MyStatus6/MyStatus7.Game -- a GameVersion id
        constexpr size_t ST_GENDER = 0x05;
        constexpr size_t ST_OT = 0x38;
        constexpr size_t ST_OT_BYTES = 0x1A; // 13 UTF-16 units

        // Misc: money is at 0x08 in Gen 6 (Misc6XY / Misc6AO) and 0x04 in Gen 7 (Misc7).
        constexpr size_t MISC_MONEY = 0x04;

        // BoxLayout. Names are 0x22 bytes each from offset 0 in both; everything after them moves.
        constexpr size_t BL_NAME_BYTES = 0x22;
        constexpr size_t BL_CURRENT = 0x5E3;
    }

    bool Trainer7USUM::detectGen67(const std::vector<uint8_t> &bytes, size_t size) noexcept
    {
        return bytes.size() == size && readUInt32LittleEndian(&bytes[size - BEEF_MAGIC_OFS]) == BEEF_MAGIC;
    }

        
    Trainer7USUM::Trainer7USUM(std::vector<uint8_t> raw, std::string path)
        : saveData(std::move(raw)), savePath(std::move(path))
    {
        init();
    }

    void Trainer7USUM::init()
    {
        if (saveData.size() != fileSize())
            return;
        boxes.resize(getBoxCount());
        boxNames.resize(getBoxCount());
        parse();
        valid = true;
    }

    size_t Trainer7USUM::entitySize(bool partySlot) const noexcept
    {
        return partySlot ? Encryption::SIZE_PARTY7_USUM : Encryption::SIZE_STORED7_USUM;
    }

    /**
     * Builds a PK6/PK7 from an on-disk record, GROWING a box record to party size.
     *
     * A box record is 0xE8 bytes and a party record 0x104; the extra 0x1C hold the level and the
     * six battle stats, which the game recomputes when a Pokemon leaves the PC. The accessors
     * index straight into that tail, so a stored-size entity reports LEVEL 0 AND ZERO STATS to
     * everything that asks, and moving one into the party made the party writer encrypt a
     * party-length span over a box-length allocation -- a heap over-read.
     *
     * The stored bytes are untouched (recalculateStats() writes only the tail, and the checksum
     * covers 0x08..0xE8), so writing the record back is still byte-exact.
     */
    std::unique_ptr<::Pokemon::Pokemon> Trainer7USUM::makeEntity(const uint8_t *bytes, size_t byteCount) const
    {
        const std::span<const std::byte> source(reinterpret_cast<const std::byte *>(bytes), byteCount);
        auto build = [&](std::span<const std::byte> recordBytes) -> std::unique_ptr<::Pokemon::Pokemon>
        {
            return std::make_unique<::Pokemon::Pokemon7USUM>(recordBytes);
        };
        if (byteCount >= Encryption::SIZE_PARTY7_USUM)
            return build(source);

        std::byte *decryptedRecord = Encryption::decryptArray7USUM(source);
        std::vector<std::byte> full(Encryption::SIZE_PARTY7_USUM, std::byte{0});
        std::memcpy(full.data(), decryptedRecord, byteCount);
        delete[] decryptedRecord;
        std::byte *encryptedRecord = Encryption::encryptArray7USUM(full);
        auto pokemon = build(std::span<const std::byte>(encryptedRecord, Encryption::SIZE_PARTY7_USUM));
        delete[] encryptedRecord;
        // Fills level + the six battle stats. A species-0 slot has no personal data and is left
        // alone, which is what keeps an empty slot byte-exact.
        pokemon->recalculateStats();
        return pokemon;
    }

    void Trainer7USUM::writeChecksums() noexcept
    {
        for (size_t blockIndex = 0; blockIndex < blockCount(); ++blockIndex)
        {
            const BlockEntry3DS &b = blk(blockIndex);
            size_t blankOfs = 0, blankLen = 0;
            if (!checksumBlank(blockIndex, blankOfs, blankLen))
            {
                writeUInt16LittleEndian(&saveData[blockChecksumOffset3DS(fileSize(), blockIndex)],
                                        blockCrc(&saveData[b.offset], b.length));
                continue;
            }
            // Gen 7's signature block: the checksum the game records is the one taken with the
            // signature region BLANK. Copy, blank, checksum -- the live bytes stay put, because
            // sign() still needs the signature that is there to recover its plaintext.
            std::vector<uint8_t> blankedBlock(&saveData[b.offset], &saveData[b.offset] + b.length);
            if (blankOfs + blankLen <= blankedBlock.size())
                std::memset(&blankedBlock[blankOfs], 0, blankLen);
            writeUInt16LittleEndian(&saveData[blockChecksumOffset3DS(fileSize(), blockIndex)],
                                    blockCrc(blankedBlock.data(), blankedBlock.size()));
        }
    }

    bool Trainer7USUM::checksumBlank(size_t blockId, size_t &offset, size_t &length) const noexcept
    {
        if (blockId != BLK_MEMECRYPTO)
            return false;
        offset = SIG_OFS;
        length = SIG_LEN;
        return true;
    }

    void Trainer7USUM::sign() noexcept
    {
        // Runs LAST, and that is the whole point: the signature covers the checksum table, so it
        // can only be computed once every checksum is final.
        const size_t table = fileSize() - 0x200;
        const size_t signatureOffset = blk(BLK_MEMECRYPTO).offset + SIG_OFS;
        if (signatureOffset + SIG_LEN > saveData.size() || table + signatureSpan() > saveData.size())
            return;

        // Recover the CURRENT signature's plaintext and keep everything but the hash. Only the
        // first 0x20 bytes are ours -- the rest is leftover console memory the game happened to
        // sign, and preserving it is what makes an unedited save reproduce byte for byte.
        // memecrypto_verify is used rather than reverseCrypt(): reverseCrypt is written for a
        // 0x60 payload, reads past its own buffer at 0x80, and is not the inverse of signing one.
        uint8_t plain[SIG_LEN];
        std::memset(plain, 0, sizeof(plain));
        if (!memecrypto_verify(&saveData[signatureOffset], plain, static_cast<int>(SIG_LEN)))
        {
            // Nothing to recover -- a save from a key PKSE does not have, or a signature that was
            // never valid. Sign over zeros instead, which is what PKHeX always does and which the
            // game accepts; the only thing lost is byte-exactness against the original.
            std::memset(plain, 0, sizeof(plain));
            logErrorToFile("Gen 7: could not recover the existing MemeCrypto signature; "
                           "signing over a blank plaintext instead");
        }

        // Overwrite the hash with a SHA-256 of the checksum table. memecrypto_sign fills in the
        // SHA-1 tail itself, AES-encrypts and RSA-signs.
        uint8_t hash[Utils::PKSE_SHA256_HASH_SIZE];
        Utils::SHA256 hasher;
        hasher.update(&saveData[table], signatureSpan());
        hasher.finalize(hash);
        std::memcpy(plain, hash, sizeof(hash));

        memecrypto_sign(plain, &saveData[signatureOffset], static_cast<int>(SIG_LEN));
    }

    void Trainer7USUM::parse()
    {
        parseTrainer();
        parseParty();
        parseBoxes();
        parseItems();
        parseBoxNames();
    }

    void Trainer7USUM::parseTrainer()
    {
        const uint8_t *sourceBytes = at(blkStatus());
        const size_t otOfs = ST_OT;
        trainerName = utf16ToUtf8(readUtf16Field(sourceBytes + otOfs, ST_OT_BYTES / 2, false));
        ID32 = readUInt32LittleEndian(sourceBytes + ST_ID32);
        TID16 = readUInt16LittleEndian(sourceBytes + ST_ID32);
        SID16 = readUInt16LittleEndian(sourceBytes + ST_ID32 + 2);
        TID = TID16;
        SID = SID16;
        trainerGender = sourceBytes[ST_GENDER] & 1;
        money = readUInt32LittleEndian(at(blkMisc()) +
                                       MISC_MONEY);
        currentBox = at(blkBoxLayout())[BL_CURRENT];
    }

    GameVersion Trainer7USUM::getGameVersion() const noexcept
    {
        // MyStatus +0x04 holds a GameVersion id in both generations (PKHeX MyStatus6.Game /
        // MyStatus7.Game), so X, Y, Omega Ruby, Alpha Sapphire, Sun, Moon, Ultra Sun and Ultra
        // Moon each name themselves rather than being reported as their group's first title.
        //
        // The byte is accepted only if it belongs to THIS save's group: a value that says "X"
        // inside an ORAS layout is a misparse, not a discovery, and the group representative is
        // the honest answer in that case.
        if (saveData.size() != fileSize())
            return static_cast<GameVersion>(Enums::getGroupRepVersion(getGameGroup()));
        const GameVersion stored = static_cast<GameVersion>(at(blkStatus())[ST_GAME]);
        if (stored == GameVersion::US || stored == GameVersion::UM)
            return stored;
        return static_cast<GameVersion>(Enums::getGroupRepVersion(getGameGroup()));
    }

    void Trainer7USUM::parseParty()
    {
        const uint8_t *bytes = at(blkParty());
        // The count is AFTER the six records, not before them.
        const uint8_t storedCount = bytes[MAX_PARTY_SLOTS * Encryption::SIZE_PARTY7_USUM];
        if (storedCount > MAX_PARTY_SLOTS)
            return;
        for (uint8_t index = 0; index < storedCount; ++index)
            party.push_back(makeEntity(bytes + index * Encryption::SIZE_PARTY7_USUM, Encryption::SIZE_PARTY7_USUM));
    }

    void Trainer7USUM::parseBoxes()
    {
        const uint8_t *bytes = at(blkBox());
        for (size_t box = 0; box < getBoxCount(); ++box)
        {
            for (size_t slotIndex = 0; slotIndex < BOX_SLOTS; ++slotIndex)
            {
                const uint8_t *offset = bytes + (box * BOX_SLOTS + slotIndex) * Encryption::SIZE_STORED7_USUM;
                boxes[box][slotIndex] = makeEntity(offset, Encryption::SIZE_STORED7_USUM);
                // Keep the first empty slot verbatim as the template for clearing a slot later.
                // A Gen 6 game's blank is NOT encrypt(zeros): it is a species-0 record whose
                // nickname field reads "Egg", with its own valid checksum. Inventing a blank
                // rewrites all 930 slots of an untouched save; copying the one the file already
                // contains is both exact and, by definition, what this game writes.
                if (emptyTemplate.empty() && boxes[box][slotIndex] && boxes[box][slotIndex]->speciesID() == 0)
                    emptyTemplate.assign(offset, offset + Encryption::SIZE_STORED7_USUM);
            }
        }
    }

    void Trainer7USUM::parseBoxNames()
    {
        const uint8_t *l = at(blkBoxLayout());
        for (size_t boxIndex = 0; boxIndex < getBoxCount(); ++boxIndex)
        {
            const std::u16string name = readUtf16Field(l + boxIndex * BL_NAME_BYTES, BL_NAME_BYTES / 2, false);
            boxNames[boxIndex] = name.empty() ? ("Box " + std::to_string(boxIndex + 1)) : utf16ToUtf8(name);
        }
    }

    void Trainer7USUM::parseItems()
    {
        const uint8_t *g = at(blkItem());
        const PouchLayout7USUM *pouchLayout = POUCHES7_USUM;
        const int pouchCount = POUCH_COUNT7_USUM;
        items.assign(pouchCount, {});
        itemRaw.assign(pouchCount, {});
        itemSlot.assign(pouchCount, {});
        for (int pIndex = 0; pIndex < pouchCount; ++pIndex)
        {
            // EVERY slot, not up to the first zero id. A Gen 7 pouch is NOT packed: Ultra Sun's
            // Items pouch holds 164 real entries, then 248 empty slots, then 15 more entries with
            // a count of 0 -- items the player used up, parked at the end and still carrying their
            // free-space sort index. Stopping at the first hole loses them and re-packing moves
            // them, either of which rewrites a bag nobody touched.
            for (size_t pouchSlotIndex = 0; pouchSlotIndex < pouchLayout[pIndex].slots; ++pouchSlotIndex)
            {
                const uint8_t *e = g + pouchLayout[pIndex].offset + pouchSlotIndex * 4;
                const uint32_t packedItemWord = readUInt32LittleEndian(e);
                const uint16_t itemId = static_cast<uint16_t>(packedItemWord & ITEM_ID_MASK7_USUM);
                if (itemId == 0)
                    continue;
                items[pIndex].push_back(InventoryItem{
                    itemId,
                    static_cast<uint16_t>((packedItemWord >> ITEM_COUNT_SHIFT7_USUM) & ITEM_ID_MASK7_USUM),
                    (packedItemWord & ITEM_NEW_BIT7_USUM) != 0, false});
                itemRaw[pIndex].push_back(packedItemWord);
                itemSlot[pIndex].push_back(ItemOrigin{static_cast<uint16_t>(pouchSlotIndex), itemId});
            }
        }
    }

    void Trainer7USUM::updatePartyBlock()
    {
        uint8_t *bytes = at(blkParty());
        const size_t recordSize = Encryption::SIZE_PARTY7_USUM;
        const auto blank = Encryption::blankRecord7USUM(recordSize);
        size_t written = 0;
        for (size_t partySlotIndex = 0; partySlotIndex < party.size() && partySlotIndex < MAX_PARTY_SLOTS;
             ++partySlotIndex)
        {
            if (!party[partySlotIndex] || party[partySlotIndex]->speciesID() == 0)
                continue;
            party[partySlotIndex]->refreshChecksum();
            // Staged through a party-length buffer -- see Trainer7USUM::makeEntity.
            std::vector<std::byte> rec(recordSize, std::byte{0});
            const auto srcData = party[partySlotIndex]->getData();
            std::memcpy(rec.data(), srcData.data(), std::min(rec.size(), srcData.size()));
            std::byte *encryptedRecord = Encryption::encryptArray7USUM(rec);
            std::memcpy(bytes + written * recordSize, encryptedRecord, recordSize);
            delete[] encryptedRecord;
            ++written;
        }
        for (size_t partySlotIndex = written; partySlotIndex < MAX_PARTY_SLOTS; ++partySlotIndex)
            std::memcpy(bytes + partySlotIndex * recordSize, blank.data(), blank.size());
        bytes[MAX_PARTY_SLOTS * recordSize] = static_cast<uint8_t>(written);
    }

    void Trainer7USUM::updateBoxBlock()
    {
        uint8_t *b = at(blkBox());
        const size_t recordSize = Encryption::SIZE_STORED7_USUM;
        for (size_t box = 0; box < getBoxCount(); ++box)
        {
            for (size_t slotIndex = 0; slotIndex < BOX_SLOTS; ++slotIndex)
            {
                uint8_t *offset = b + (box * BOX_SLOTS + slotIndex) * recordSize;
                auto &pokemon = boxes[box][slotIndex];
                if (pokemon)
                {
                    // Empty slots go through here too. Re-encrypting the record we read back is
                    // a bijection given its encryption constant, so an untouched slot reproduces
                    // its own bytes -- whatever blank this game happens to use.
                    pokemon->refreshChecksum();
                    // Only the STORED half goes to a box slot; a party-size entity's tail (level,
                    // battle stats) is exactly what a box record does not carry.
                    std::vector<std::byte> rec(recordSize, std::byte{0});
                    const auto srcData = pokemon->getData();
                    std::memcpy(rec.data(), srcData.data(), std::min(rec.size(), srcData.size()));
                    std::byte *encryptedRecord = Encryption::encryptArray7USUM(rec);
                    std::memcpy(offset, encryptedRecord, recordSize);
                    delete[] encryptedRecord;
                }
                else if (!emptyTemplate.empty())
                {
                    std::memcpy(offset, emptyTemplate.data(), recordSize);
                }
                else
                {
                    const auto blank = Encryption::blankRecord7USUM(recordSize);
                    std::memcpy(offset, blank.data(), blank.size());
                }
            }
        }
    }

    void Trainer7USUM::updateTrainerInfoBlock()
    {
        uint8_t *sourceBytes = at(blkStatus());
        const size_t otOfs = ST_OT;
        writeUtf16Field(sourceBytes + otOfs, ST_OT_BYTES / 2, utf8ToUtf16(trainerName), UTF16_TERM_G67, false);
        writeUInt32LittleEndian(sourceBytes + ST_ID32, ID32);
        sourceBytes[ST_GENDER] = trainerGender & 1;
        writeUInt32LittleEndian(at(blkMisc()) + MISC_MONEY,
                                money > getMaxMoney() ? getMaxMoney() : money);
    }

    void Trainer7USUM::updateBoxNameBlock()
    {
        uint8_t *l = at(blkBoxLayout());
        for (size_t boxIndex = 0; boxIndex < getBoxCount(); ++boxIndex)
        {
            if (!isBoxNameDirty(boxIndex))
                continue;
            writeUtf16Field(l + boxIndex * BL_NAME_BYTES, BL_NAME_BYTES / 2,
                            utf8ToUtf16(boxNames[boxIndex]), UTF16_TERM_G67, false);
        }
    }

    void Trainer7USUM::updateCurrentBoxBlock()
    {
        at(blkBoxLayout())[BL_CURRENT] = currentBox;
    }

    void Trainer7USUM::updateItemBlock()
    {
        uint8_t *g = at(blkItem());
        const PouchLayout7USUM *pouchLayout = POUCHES7_USUM;
        const int pouchCount = POUCH_COUNT7_USUM;
        for (int pouchIndex = 0; pouchIndex < pouchCount && pouchIndex < static_cast<int>(items.size()); ++pouchIndex)
        {
            const size_t capacity = pouchLayout[pouchIndex].slots;
            uint8_t *pouch = g + pouchLayout[pouchIndex].offset;
            // Gen 7 packs id, count, the free-space sort index and the new flag into ONE word, so
            // it places the items with the shared rule and writes its own codec over the result.
            const std::vector<ItemPlacement> placement =
                placePouchPositional(capacity, items[pouchIndex],
                                     pouchIndex < static_cast<int>(itemSlot.size()) ? itemSlot[pouchIndex]
                                                                                      : std::vector<ItemOrigin>{});

            for (size_t slotIndex = 0; slotIndex < capacity; ++slotIndex)
            {
                writeUInt32LittleEndian(pouch + slotIndex * 4, 0);
            }
            for (size_t itemIndex = 0; itemIndex < items[pouchIndex].size(); ++itemIndex)
            {
                if (placement[itemIndex].slot == capacity)
                {
                    continue;
                }
                const InventoryItem &inventoryItem = items[pouchIndex][itemIndex];
                // The player's free-space sort index and new flag describe the entry this item was
                // read from, so they ride along with it and an item new to the pouch gets neither.
                uint32_t carry = 0;
                if (pouchIndex < static_cast<int>(itemRaw.size()) &&
                    placement[itemIndex].originIndex < itemRaw[pouchIndex].size())
                {
                    carry = itemRaw[pouchIndex][placement[itemIndex].originIndex] & ITEM_CARRY_MASK7_USUM;
                }
                else if (inventoryItem.isNew)
                {
                    carry = ITEM_NEW_BIT7_USUM;
                }
                writeUInt32LittleEndian(pouch + placement[itemIndex].slot * 4,
                                        (inventoryItem.itemId & ITEM_ID_MASK7_USUM) |
                                            ((inventoryItem.count & ITEM_ID_MASK7_USUM)
                                             << ITEM_COUNT_SHIFT7_USUM) |
                                            carry);
            }
        }
    }

    bool Trainer7USUM::canStoreBoxName(const std::string &name) const
    {
        return utf8ToUtf16(name).size() <= getMaxBoxNameLength();
    }

    std::unique_ptr<::Pokemon::Pokemon> Trainer7USUM::createBlankPokemon() const
    {
        return std::make_unique<::Pokemon::Pokemon7USUM>();
    }

    const std::vector<uint8_t> &Trainer7USUM::serialize()
    {
        updateItemBlock();
        updatePartyBlock();
        updateBoxBlock();
        updateBoxNameBlock();
        updateCurrentBoxBlock();
        updateTrainerInfoBlock();
        writeChecksums();
        sign(); // LAST: the signature covers the checksum table writeChecksums() just wrote
        return saveData;
    }
}
