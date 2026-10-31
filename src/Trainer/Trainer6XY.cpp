#include <algorithm>
#include <cstring>
#include <vector>

#include "Trainer/Trainer6XY.h"
#include "Trainer/Inventory6XY.h"
#include "Encryption/Encryption6XY.h"
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
    uint16_t Trainer6XY::blockCrc(const uint8_t *bytes, size_t byteCount) const noexcept
    {
        return crc16ccitt(bytes, byteCount);
    }

    namespace
    {
        constexpr uint32_t BEEF_MAGIC = 0x42454546u; // read as a u32 LE, per PKHeX
        constexpr size_t BEEF_MAGIC_OFS = 0x1F0;     // from the END of the file

        // MyStatus: identical in both generations except where the OT name sits.
        constexpr size_t ST_ID32 = 0x00;
        constexpr size_t ST_GAME = 0x04; // PKHeX MyStatus6/MyStatus7.Game -- a GameVersion id
        constexpr size_t ST_GENDER = 0x05;
        constexpr size_t ST_OT = 0x48;
        constexpr size_t ST_OT_BYTES = 0x1A; // 13 UTF-16 units

        // Misc: money is at 0x08 in Gen 6 (Misc6XY / Misc6AO) and 0x04 in Gen 7 (Misc7).
        constexpr size_t MISC_MONEY = 0x08;

        // BoxLayout. Names are 0x22 bytes each from offset 0 in both; everything after them moves.
        constexpr size_t BL_NAME_BYTES = 0x22;
        constexpr size_t BL_CURRENT = 0x43F; // 31*0x22 backgrounds, flags, unlocked, current
    }

    bool Trainer6XY::detectGen67(const std::vector<uint8_t> &bytes, size_t size) noexcept
    {
        return bytes.size() == size && readUInt32LittleEndian(&bytes[size - BEEF_MAGIC_OFS]) == BEEF_MAGIC;
    }

        
    Trainer6XY::Trainer6XY(std::vector<uint8_t> raw, std::string path) : saveData(std::move(raw)), savePath(std::move(path))
    {
        init();
    }

    void Trainer6XY::init()
    {
        if (saveData.size() != fileSize())
            return;
        boxes.resize(getBoxCount());
        boxNames.resize(getBoxCount());
        parse();
        valid = true;
    }

    size_t Trainer6XY::entitySize(bool partySlot) const noexcept
    {
        return partySlot ? Encryption::SIZE_PARTY6_XY : Encryption::SIZE_STORED6_XY;
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
    std::unique_ptr<::Pokemon::Pokemon> Trainer6XY::makeEntity(const uint8_t *bytes, size_t byteCount) const
    {
        const std::span<const std::byte> source(reinterpret_cast<const std::byte *>(bytes), byteCount);
        auto build = [&](std::span<const std::byte> recordBytes) -> std::unique_ptr<::Pokemon::Pokemon>
        {
            return std::make_unique<::Pokemon::Pokemon6XY>(recordBytes);
        };
        if (byteCount >= Encryption::SIZE_PARTY6_XY)
            return build(source);

        std::byte *decryptedRecord = Encryption::decryptArray6XY(source);
        std::vector<std::byte> full(Encryption::SIZE_PARTY6_XY, std::byte{0});
        std::memcpy(full.data(), decryptedRecord, byteCount);
        delete[] decryptedRecord;
        std::byte *encryptedRecord = Encryption::encryptArray6XY(full);
        auto pokemon = build(std::span<const std::byte>(encryptedRecord, Encryption::SIZE_PARTY6_XY));
        delete[] encryptedRecord;
        // Fills level + the six battle stats. A species-0 slot has no personal data and is left
        // alone, which is what keeps an empty slot byte-exact.
        pokemon->recalculateStats();
        return pokemon;
    }

    void Trainer6XY::writeChecksums() noexcept
    {
        for (size_t blockIndex = 0; blockIndex < blockCount(); ++blockIndex)
        {
            const BlockEntry3DS &b = blk(blockIndex);
            writeUInt16LittleEndian(&saveData[blockChecksumOffset3DS(fileSize(), blockIndex)], blockCrc(&saveData[b.offset], b.length));
        }
    }



    void Trainer6XY::parse()
    {
        parseTrainer();
        parseParty();
        parseBoxes();
        parseItems();
        parseBoxNames();
    }

    void Trainer6XY::parseTrainer()
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
        money = readUInt32LittleEndian(at(blkMisc()) + MISC_MONEY);
        currentBox = at(blkBoxLayout())[BL_CURRENT];
    }

    GameVersion Trainer6XY::getGameVersion() const noexcept
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
        if (stored == GameVersion::X || stored == GameVersion::Y)
            return stored;
        return static_cast<GameVersion>(Enums::getGroupRepVersion(getGameGroup()));
    }

    void Trainer6XY::parseParty()
    {
        const uint8_t *bytes = at(blkParty());
        // The count is AFTER the six records, not before them.
        const uint8_t storedCount = bytes[MAX_PARTY_SLOTS * Encryption::SIZE_PARTY6_XY];
        if (storedCount > MAX_PARTY_SLOTS)
            return;
        for (uint8_t index = 0; index < storedCount; ++index)
            party.push_back(makeEntity(bytes + index * Encryption::SIZE_PARTY6_XY, Encryption::SIZE_PARTY6_XY));
    }

    void Trainer6XY::parseBoxes()
    {
        const uint8_t *bytes = at(blkBox());
        for (size_t box = 0; box < getBoxCount(); ++box)
        {
            for (size_t slotIndex = 0; slotIndex < BOX_SLOTS; ++slotIndex)
            {
                const uint8_t *offset = bytes + (box * BOX_SLOTS + slotIndex) * Encryption::SIZE_STORED6_XY;
                boxes[box][slotIndex] = makeEntity(offset, Encryption::SIZE_STORED6_XY);
                // Keep the first empty slot verbatim as the template for clearing a slot later.
                // A Gen 6 game's blank is NOT encrypt(zeros): it is a species-0 record whose
                // nickname field reads "Egg", with its own valid checksum. Inventing a blank
                // rewrites all 930 slots of an untouched save; copying the one the file already
                // contains is both exact and, by definition, what this game writes.
                if (emptyTemplate.empty() && boxes[box][slotIndex] && boxes[box][slotIndex]->speciesID() == 0)
                    emptyTemplate.assign(offset, offset + Encryption::SIZE_STORED6_XY);
            }
        }
    }

    void Trainer6XY::parseBoxNames()
    {
        const uint8_t *l = at(blkBoxLayout());
        for (size_t boxIndex = 0; boxIndex < getBoxCount(); ++boxIndex)
        {
            const std::u16string name = readUtf16Field(l + boxIndex * BL_NAME_BYTES, BL_NAME_BYTES / 2, false);
            boxNames[boxIndex] = name.empty() ? ("Box " + std::to_string(boxIndex + 1)) : utf16ToUtf8(name);
        }
    }

    void Trainer6XY::parseItems()
    {
        const uint8_t *g = at(blkItem());
        const PouchLayout6XY *pouchLayout = POUCHES6_XY;
        const int pouchCount = POUCH_COUNT6_XY;
        items.assign(pouchCount, {});
        itemSlot.assign(pouchCount, {});
        for (int pIndex = 0; pIndex < pouchCount; ++pIndex)
        {
            // EVERY slot, not up to the first zero id. A Gen 6 pouch is NOT packed, and neither is
            // the Gen 7 one that shares its codec: Ultra Sun's Items pouch holds 164 real entries,
            // then 248 empty slots, then 15 more entries with a count of 0 -- items the player used
            // up, parked at the end. Stopping at the first hole loses them and re-packing moves
            // them, either of which rewrites a bag nobody touched.
            for (size_t pouchSlotIndex = 0; pouchSlotIndex < pouchLayout[pIndex].slots; ++pouchSlotIndex)
            {
                const uint8_t *e = g + pouchLayout[pIndex].offset + pouchSlotIndex * 4;
                const uint16_t itemId = readUInt16LittleEndian(e);
                if (itemId == 0)
                    continue;
                items[pIndex].push_back(InventoryItem{itemId, readUInt16LittleEndian(e + 2), false, false});
                itemSlot[pIndex].push_back(ItemOrigin{static_cast<uint16_t>(pouchSlotIndex), itemId});
            }
        }
    }

    void Trainer6XY::updatePartyBlock()
    {
        uint8_t *bytes = at(blkParty());
        const size_t recordSize = Encryption::SIZE_PARTY6_XY;
        const auto blank = Encryption::blankRecord6XY(recordSize);
        size_t written = 0;
        for (size_t partySlotIndex = 0; partySlotIndex < party.size() && partySlotIndex < MAX_PARTY_SLOTS; ++partySlotIndex)
        {
            if (!party[partySlotIndex] || party[partySlotIndex]->speciesID() == 0)
                continue;
            // Unchanged since load: keep the bytes (a Bad Egg keeps its bad checksum, as in PKHeX).
            if (slotHoldsLoadedRecord(party[partySlotIndex].get(), bytes + written * recordSize, recordSize, Encryption::decryptArray6XY))
            {
                ++written;
                continue;
            }
            party[partySlotIndex]->refreshChecksum();
            // Staged through a party-length buffer -- see Trainer6XY::makeEntity.
            std::vector<std::byte> rec(recordSize, std::byte{0});
            const auto srcData = party[partySlotIndex]->getData();
            std::memcpy(rec.data(), srcData.data(), std::min(rec.size(), srcData.size()));
            std::byte *encryptedRecord = Encryption::encryptArray6XY(rec);
            std::memcpy(bytes + written * recordSize, encryptedRecord, recordSize);
            delete[] encryptedRecord;
            ++written;
        }
        for (size_t partySlotIndex = written; partySlotIndex < MAX_PARTY_SLOTS; ++partySlotIndex)
            std::memcpy(bytes + partySlotIndex * recordSize, blank.data(), blank.size());
        bytes[MAX_PARTY_SLOTS * recordSize] = static_cast<uint8_t>(written);
    }

    void Trainer6XY::updateBoxBlock()
    {
        uint8_t *b = at(blkBox());
        const size_t recordSize = Encryption::SIZE_STORED6_XY;
        for (size_t box = 0; box < getBoxCount(); ++box)
        {
            for (size_t slotIndex = 0; slotIndex < BOX_SLOTS; ++slotIndex)
            {
                uint8_t *offset = b + (box * BOX_SLOTS + slotIndex) * recordSize;
                auto &pokemon = boxes[box][slotIndex];
                // Unchanged since load (an empty slot or a Bad Egg included): keep the file's bytes.
                if (slotHoldsLoadedRecord(pokemon.get(), offset, recordSize, Encryption::decryptArray6XY))
                    continue;
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
                    std::byte *encryptedRecord = Encryption::encryptArray6XY(rec);
                    std::memcpy(offset, encryptedRecord, recordSize);
                    delete[] encryptedRecord;
                }
                else if (!emptyTemplate.empty())
                {
                    std::memcpy(offset, emptyTemplate.data(), recordSize);
                }
                else
                {
                    const auto blank = Encryption::blankRecord6XY(recordSize);
                    std::memcpy(offset, blank.data(), blank.size());
                }
            }
        }
    }

    void Trainer6XY::updateTrainerInfoBlock()
    {
        uint8_t *sourceBytes = at(blkStatus());
        const size_t otOfs = ST_OT;
        writeUtf16Field(sourceBytes + otOfs, ST_OT_BYTES / 2, utf8ToUtf16(trainerName), UTF16_TERM_G67, false);
        writeUInt32LittleEndian(sourceBytes + ST_ID32, ID32);
        sourceBytes[ST_GENDER] = trainerGender & 1;
        writeUInt32LittleEndian(at(blkMisc()) + MISC_MONEY, money > getMaxMoney() ? getMaxMoney() : money);
    }

    void Trainer6XY::updateBoxNameBlock()
    {
        uint8_t *l = at(blkBoxLayout());
        for (size_t boxIndex = 0; boxIndex < getBoxCount(); ++boxIndex)
        {
            if (!isBoxNameDirty(boxIndex))
                continue;
            writeUtf16Field(l + boxIndex * BL_NAME_BYTES, BL_NAME_BYTES / 2, utf8ToUtf16(boxNames[boxIndex]), UTF16_TERM_G67, false);
        }
    }

    void Trainer6XY::updateCurrentBoxBlock()
    {
        at(blkBoxLayout())[BL_CURRENT] = currentBox;
    }

    void Trainer6XY::updateItemBlock()
    {
        uint8_t *g = at(blkItem());
        const PouchLayout6XY *pouchLayout = POUCHES6_XY;
        const int pouchCount = POUCH_COUNT6_XY;
        for (int pouchIndex = 0; pouchIndex < pouchCount && pouchIndex < static_cast<int>(items.size()); ++pouchIndex)
        {
            // Gen 6 slots are Gen 4/5's codec exactly -- `u16 id, u16 count` -- so the shared
            // positional writer does the whole job: an item that was in the pouch when it was read
            // goes back to its own slot, everything else takes the lowest free one.
            writePouchPositional(g + pouchLayout[pouchIndex].offset, pouchLayout[pouchIndex].slots, items[pouchIndex], pouchIndex < static_cast<int>(itemSlot.size()) ? itemSlot[pouchIndex] : std::vector<ItemOrigin>{});
        }
    }

    bool Trainer6XY::canStoreBoxName(const std::string &name) const
    {
        return utf8ToUtf16(name).size() <= getMaxBoxNameLength();
    }

    std::unique_ptr<::Pokemon::Pokemon> Trainer6XY::createBlankPokemon() const
    {
        return std::make_unique<::Pokemon::Pokemon6XY>();
    }

    const std::vector<uint8_t> &Trainer6XY::serialize()
    {
        updateItemBlock();
        updatePartyBlock();
        updateBoxBlock();
        updateBoxNameBlock();
        updateCurrentBoxBlock();
        updateTrainerInfoBlock();
        writeChecksums();
        return saveData;
    }
}
