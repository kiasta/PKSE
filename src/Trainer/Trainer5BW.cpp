#include <algorithm>
#include <cstring>
#include <vector>

#include "Trainer/Trainer5BW.h"
#include "Trainer/Inventory5BW.h"
#include "Encryption/Encryption5BW.h"
#include "Utils/CRC16.h"
#include "Utils/Utf16Text.h"
#include "Utils/HelperUtilities.h"
#include "Utils/StringHelpers.h"
#include "Utils/Logger.h"

using namespace Utils;

namespace Trainer
{
    namespace
    {
        // PKHeX SAV5: Box = 0x400 with a 0x1000 stride, Party = 0x18E00 with the count at +4 and
        // records from +8. Both are absolute; they happen to equal the block offsets.
        constexpr size_t BOX_STRIDE = 0x1000;
        constexpr size_t PARTY_COUNT_OFS = 4;
        constexpr size_t PARTY_DATA_OFS = 8;

        // Within the box-name block (block 0).
        constexpr size_t BN_CURRENT_BOX = 0x000;
        constexpr size_t BN_NAME_BASE = 0x004;
        constexpr size_t BN_NAME_STRIDE = 0x28; // stride, NOT the field width
        constexpr size_t BN_NAME_BYTES = 0x14;  // the field width: 10 UTF-16 units

        // Within the trainer block (block 27).
        constexpr size_t TR_NAME = 0x04; // 16 bytes, 8 UTF-16 units
        constexpr size_t TR_ID32 = 0x14;
        constexpr size_t TR_GAME = 0x1F; // PKHeX PlayerData5.Game -- a GameVersion id, not an index
        constexpr size_t TR_GENDER = 0x21;
    }

    bool Trainer5BW::detectGen5(const std::vector<uint8_t> &bytes, size_t mainSize, size_t infoLen) noexcept
    {
        if (bytes.size() != GEN5_FILE_SIZE)
            return false;
        const size_t base = mainSize - 0x100;
        if (base + infoLen + 0x10 > bytes.size())
            return false;
        const uint16_t stored = readUInt16LittleEndian(&bytes[base + infoLen + 0x10 - 2]);
        return crc16ccitt(&bytes[base], infoLen) == stored;
    }

    Trainer5BW::Trainer5BW(std::vector<uint8_t> raw, std::string path) : saveData(std::move(raw)), savePath(std::move(path))
    {
        init();
    }

    void Trainer5BW::init()
    {
        if (saveData.size() != GEN5_FILE_SIZE)
            return;
        boxes.resize(getBoxCount());
        boxNames.resize(getBoxCount());
        parse();
        valid = true;
    }

    void Trainer5BW::writeChecksums() noexcept
    {
        // Every block but the last, then the checksum block itself -- it holds the mirrors, so
        // stamping it earlier would checksum bytes we are about to overwrite.
        for (size_t blockIndex = 0; blockIndex + 1 < blockCount(); ++blockIndex)
        {
            const BlockEntryNDS &b = blk(blockIndex);
            const uint16_t blockChecksum = crc16ccitt(&saveData[b.offset], b.length);
            writeUInt16LittleEndian(&saveData[b.checksumOffset], blockChecksum);
            writeUInt16LittleEndian(&saveData[b.checksumMirror], blockChecksum);
        }
        const BlockEntryNDS &last = blk(blockCount() - 1);
        writeUInt16LittleEndian(&saveData[last.checksumOffset], crc16ccitt(&saveData[last.offset], last.length));
    }

    /// Builds a PK5 from an on-disk record, GROWING a box record to party size. Same reasoning as
    /// Trainer4::makeEntity -- PK5's level and battle stats live in the 0x54 bytes past the stored
    /// record, so a box entity reports level 0 and over-reads if it reaches the party.
    std::unique_ptr<::Pokemon::Pokemon> Trainer5BW::makeEntity(const uint8_t *bytes, size_t byteCount) const
    {
        const std::span<const std::byte> source(reinterpret_cast<const std::byte *>(bytes), byteCount);
        if (byteCount >= Encryption::SIZE_PARTY5_BW)
            return std::make_unique<::Pokemon::Pokemon5BW>(source);

        std::byte *dec = Encryption::decryptArray5BW(source);
        std::vector<std::byte> full(Encryption::SIZE_PARTY5_BW, std::byte{0});
        std::memcpy(full.data(), dec, byteCount);
        delete[] dec;
        std::byte *encryptedRecord = Encryption::encryptArray5BW(full);
        auto pokemon = std::make_unique<::Pokemon::Pokemon5BW>(std::span<const std::byte>(encryptedRecord, Encryption::SIZE_PARTY5_BW));
        delete[] encryptedRecord;
        pokemon->recalculateStats();
        return pokemon;
    }

    void Trainer5BW::parse()
    {
        parseTrainer();
        parseParty();
        parseBoxes();
        parseItems();
        parseBoxNames();
    }

    void Trainer5BW::parseTrainer()
    {
        const uint8_t *t = at(BLOCK_TRAINER_5BW);
        trainerName = utf16ToUtf8(readUtf16Field(t + TR_NAME, 8, true));
        ID32 = readUInt32LittleEndian(t + TR_ID32);
        TID16 = readUInt16LittleEndian(t + TR_ID32);
        SID16 = readUInt16LittleEndian(t + TR_ID32 + 2);
        TID = TID16;
        SID = SID16;
        trainerGender = t[TR_GENDER] & 1;
        money = readUInt32LittleEndian(at(BLK_MISC));
        currentBox = at(BLOCK_BOX_NAMES_5BW)[BN_CURRENT_BOX];
    }

    GameVersion Trainer5BW::getGameVersion() const noexcept
    {
        // Gen 5 is the first generation whose SAVE names its own game: PlayerData5 +0x1F holds a
        // GameVersion id (PKHeX PlayerData5.Game). That is what lets Black and White be named
        // separately rather than one of them standing in for the pair.
        //
        // The byte is accepted only if it belongs to THIS save's group. A value that says "Black"
        // inside a Black 2 layout is a misparse, not a discovery, and the group representative is
        // the honest answer in that case.
        if (saveData.size() != GEN5_FILE_SIZE)
            return static_cast<GameVersion>(Enums::getGroupRepVersion(getGameGroup()));
        const GameVersion stored = static_cast<GameVersion>(at(BLOCK_TRAINER_5BW)[TR_GAME]);
        if (stored == GameVersion::B || stored == GameVersion::W)
            return stored;
        return static_cast<GameVersion>(Enums::getGroupRepVersion(getGameGroup()));
    }

    void Trainer5BW::parseParty()
    {
        const uint8_t *bytes = at(BLOCK_PARTY_5BW);
        const uint8_t count = bytes[PARTY_COUNT_OFS];
        if (count > MAX_PARTY_SLOTS)
            return;
        for (uint8_t index = 0; index < count; ++index)
        {
            const size_t offset = PARTY_DATA_OFS + index * Encryption::SIZE_PARTY5_BW;
            party.push_back(makeEntity(bytes + offset, Encryption::SIZE_PARTY5_BW));
        }
    }

    void Trainer5BW::parseBoxes()
    {
        for (size_t boxIndex = 0; boxIndex < getBoxCount(); ++boxIndex)
        {
            const uint8_t *box = at(BLOCK_BOX_FIRST_5BW + boxIndex);
            for (size_t slotIndex = 0; slotIndex < BOX_SLOTS; ++slotIndex)
            {
                boxes[boxIndex][slotIndex] = makeEntity(box + slotIndex * Encryption::SIZE_STORED5_BW, Encryption::SIZE_STORED5_BW);
            }
        }
    }

    void Trainer5BW::parseBoxNames()
    {
        const uint8_t *count = at(BLOCK_BOX_NAMES_5BW);
        for (size_t boxIndex = 0; boxIndex < getBoxCount(); ++boxIndex)
        {
            const std::u16string name = readUtf16Field(count + BN_NAME_BASE + boxIndex * BN_NAME_STRIDE, BN_NAME_BYTES / 2, true);
            boxNames[boxIndex] = name.empty() ? ("Box " + std::to_string(boxIndex + 1)) : utf16ToUtf8(name);
        }
    }

    void Trainer5BW::parseItems()
    {
        const uint8_t *g = at(BLOCK_INVENTORY_5BW);
        const PouchLayout5BW *pouchLayout = POUCHES5_BW;
        items.assign(POUCH_COUNT5_BW, {});
        itemSlot.assign(POUCH_COUNT5_BW, {});
        for (int pouchIndex = 0; pouchIndex < POUCH_COUNT5_BW; ++pouchIndex)
        {
            // EVERY slot -- Gen 5 bags are positional, not packed. See writePouchPositional().
            for (size_t pouchSlotIndex = 0; pouchSlotIndex < pouchLayout[pouchIndex].slots; ++pouchSlotIndex)
            {
                const size_t entryOffset = pouchLayout[pouchIndex].offset + pouchSlotIndex * 4;
                const uint16_t itemId = readUInt16LittleEndian(g + entryOffset);
                if (itemId == 0)
                    continue;
                items[pouchIndex].push_back(InventoryItem{itemId, readUInt16LittleEndian(g + entryOffset + 2), false, false});
                itemSlot[pouchIndex].push_back(ItemOrigin{static_cast<uint16_t>(pouchSlotIndex), itemId});
            }
        }
    }

    void Trainer5BW::updatePartyBlock()
    {
        uint8_t *bytes = at(BLOCK_PARTY_5BW);
        const auto blank = Encryption::blankRecord5BW(Encryption::SIZE_PARTY5_BW);
        size_t written = 0;
        for (size_t partySlotIndex = 0; partySlotIndex < party.size() && partySlotIndex < MAX_PARTY_SLOTS; ++partySlotIndex)
        {
            if (!party[partySlotIndex] || party[partySlotIndex]->speciesID() == 0)
                continue;
            // Unchanged since load: keep the bytes (a Bad Egg keeps its bad checksum, as in PKHeX).
            if (slotHoldsLoadedRecord(party[partySlotIndex].get(), bytes + PARTY_DATA_OFS + written * Encryption::SIZE_PARTY5_BW, Encryption::SIZE_PARTY5_BW, Encryption::decryptArray5BW))
            {
                ++written;
                continue;
            }
            party[partySlotIndex]->refreshChecksum(); // the checksum IS the crypt key -- before, never after
            // Staged through a party-length buffer -- see Trainer4::updatePartyBlock.
            std::vector<std::byte> rec(Encryption::SIZE_PARTY5_BW, std::byte{0});
            const auto srcData = party[partySlotIndex]->getData();
            std::memcpy(rec.data(), srcData.data(), std::min(rec.size(), srcData.size()));
            std::byte *encryptedRecord = Encryption::encryptArray5BW(rec);
            std::memcpy(bytes + PARTY_DATA_OFS + written * Encryption::SIZE_PARTY5_BW, encryptedRecord, Encryption::SIZE_PARTY5_BW);
            delete[] encryptedRecord;
            ++written;
        }
        for (size_t partySlotIndex = written; partySlotIndex < MAX_PARTY_SLOTS; ++partySlotIndex)
            std::memcpy(bytes + PARTY_DATA_OFS + partySlotIndex * Encryption::SIZE_PARTY5_BW, blank.data(), blank.size());
        bytes[PARTY_COUNT_OFS] = static_cast<uint8_t>(written);
    }

    void Trainer5BW::updateBoxBlock()
    {
        const auto blank = Encryption::blankRecord5BW(Encryption::SIZE_STORED5_BW);
        for (size_t boxIndex = 0; boxIndex < getBoxCount(); ++boxIndex)
        {
            // Write only the 30 records. The 16 bytes of slack that follow are outside the block's
            // CRC, so touching them fails no checksum and shows up only as a round-trip diff.
            uint8_t *box = at(BLOCK_BOX_FIRST_5BW + boxIndex);
            for (size_t slotIndex = 0; slotIndex < BOX_SLOTS; ++slotIndex)
            {
                uint8_t *offset = box + slotIndex * Encryption::SIZE_STORED5_BW;
                auto &pokemon = boxes[boxIndex][slotIndex];
                // Unchanged since load (an empty slot or a Bad Egg included): keep the file's bytes.
                if (slotHoldsLoadedRecord(pokemon.get(), offset, Encryption::SIZE_STORED5_BW, Encryption::decryptArray5BW))
                    continue;
                if (pokemon && pokemon->speciesID() != 0)
                {
                    pokemon->refreshChecksum();
                    // Only the STORED half goes to a box slot.
                    std::vector<std::byte> rec(Encryption::SIZE_STORED5_BW, std::byte{0});
                    const auto srcData = pokemon->getData();
                    std::memcpy(rec.data(), srcData.data(), std::min(rec.size(), srcData.size()));
                    std::byte *encryptedRecord = Encryption::encryptArray5BW(rec);
                    std::memcpy(offset, encryptedRecord, Encryption::SIZE_STORED5_BW);
                    delete[] encryptedRecord;
                }
                else
                {
                    std::memcpy(offset, blank.data(), blank.size());
                }
            }
        }
    }

    void Trainer5BW::updateTrainerInfoBlock()
    {
        uint8_t *t = at(BLOCK_TRAINER_5BW);
        writeUtf16Field(t + TR_NAME, 8, utf8ToUtf16(trainerName), UTF16_TERM_G5, true);
        writeUInt32LittleEndian(t + TR_ID32, ID32);
        t[TR_GENDER] = trainerGender & 1;
        writeUInt32LittleEndian(at(BLK_MISC), money > getMaxMoney() ? getMaxMoney() : money);
    }

    void Trainer5BW::updateBoxNameBlock()
    {
        uint8_t *count = at(BLOCK_BOX_NAMES_5BW);
        for (size_t boxIndex = 0; boxIndex < getBoxCount(); ++boxIndex)
        {
            // never write back the "Box N" display defaults
            if (!isBoxNameDirty(boxIndex)) continue;
            writeUtf16Field(count + BN_NAME_BASE + boxIndex * BN_NAME_STRIDE, BN_NAME_BYTES / 2, utf8ToUtf16(boxNames[boxIndex]), UTF16_TERM_G5, true);
        }
    }

    void Trainer5BW::updateCurrentBoxBlock() { at(BLOCK_BOX_NAMES_5BW)[BN_CURRENT_BOX] = currentBox; }

    void Trainer5BW::updateItemBlock()
    {
        uint8_t *g = at(BLOCK_INVENTORY_5BW);
        const PouchLayout5BW *pouchLayout = POUCHES5_BW;
        for (int pouchIndex = 0; pouchIndex < POUCH_COUNT5_BW && pouchIndex < static_cast<int>(items.size()); ++pouchIndex)
            writePouchPositional(g + pouchLayout[pouchIndex].offset, pouchLayout[pouchIndex].slots, items[pouchIndex], pouchIndex < static_cast<int>(itemSlot.size()) ? itemSlot[pouchIndex] : std::vector<ItemOrigin>{});
    }

    bool Trainer5BW::canStoreBoxName(const std::string &name) const
    {
        return utf8ToUtf16(name).size() <= getMaxBoxNameLength();
    }

    std::unique_ptr<::Pokemon::Pokemon> Trainer5BW::createBlankPokemon() const
    {
        return std::make_unique<::Pokemon::Pokemon5BW>();
    }

    const std::vector<uint8_t> &Trainer5BW::serialize()
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
