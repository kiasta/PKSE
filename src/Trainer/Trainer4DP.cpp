#include <algorithm>
#include <cstring>
#include <vector>

#include "Trainer/Trainer4DP.h"
#include "Trainer/Inventory4DP.h"
#include "Utils/CRC16.h"
#include "Utils/Gen4Text.h"
#include "Utils/HelperUtilities.h"
#include "Utils/Logger.h"

using namespace Utils;

namespace Trainer
{
    namespace
    {
        constexpr int PARTITION_FIRST = 0, PARTITION_SECOND = 1, PARTITION_SAME = 2;

        /// PKHeX SAV4BlockDetection.CompareCounters. The 0xFFFFFFFF handling is load-bearing:
        /// an untouched partition reads LARGER than any real counter, so a plain `>` picks it.
        int compareCounters(uint32_t firstCounter, uint32_t secondCounter) noexcept
        {
            if (firstCounter == 0xFFFFFFFFu && secondCounter != 0xFFFFFFFEu)
                return PARTITION_SECOND;
            if (secondCounter == 0xFFFFFFFFu && firstCounter != 0xFFFFFFFEu)
                return PARTITION_FIRST;
            if (firstCounter > secondCounter)
                return PARTITION_FIRST;
            if (firstCounter < secondCounter)
                return PARTITION_SECOND;
            return PARTITION_SAME;
        }

        int compareFooters(const uint8_t *bytes, size_t firstFooterOffset, size_t secondFooterOffset) noexcept
        {
            const int major = compareCounters(readUInt32LittleEndian(bytes + firstFooterOffset), readUInt32LittleEndian(bytes + secondFooterOffset));
            if (major != PARTITION_SAME)
                return major;
            const int minor = compareCounters(readUInt32LittleEndian(bytes + firstFooterOffset + 4), readUInt32LittleEndian(bytes + secondFooterOffset + 4));
            return minor == PARTITION_SECOND ? PARTITION_SECOND : PARTITION_FIRST; // Same -> First
        }
    }

    bool Trainer4DP::detectGeneral(const std::vector<uint8_t> &bytes, size_t gsize) noexcept
    {
        if (bytes.size() != GEN4_SAVE_SIZE)
            return false;
        const size_t end = GEN4_PARTITION + gsize;
        if (end > bytes.size())
            return false;
        // The size field equalling the block's own length is what tells DP from Pt from HGSS --
        // their General blocks are different sizes, so at most one probe matches.
        if (readUInt32LittleEndian(&bytes[end - 0x0C]) != static_cast<uint32_t>(gsize))
            return false;
        const uint32_t sdkVersionWord = readUInt32LittleEndian(&bytes[end - 0x08]);
        return sdkVersionWord == GEN4_MAGIC_INTL || sdkVersionWord == GEN4_MAGIC_KOR;
    }

    Trainer4DP::Trainer4DP(std::vector<uint8_t> raw, std::string path) : saveData(std::move(raw)), savePath(std::move(path))
    {
        init();
    }

    void Trainer4DP::init()
    {
        if (saveData.size() != GEN4_SAVE_SIZE)
            return;
        selectBlocks();
        boxes.resize(getBoxCount());
        boxNames.resize(getBoxCount());
        parse();
        valid = true;
    }

    void Trainer4DP::selectBlocks() noexcept
    {
        // The COUNTERS are always 0x14 from a block's end, in every Gen 4 game -- including HGSS,
        // whose CRC footerSize() is 0x10. Two constants, two questions; do not unify them.
        const size_t gFoot = generalSize() - 0x14;
        const size_t sFoot = storageStart() + storageSize() - 0x14;

        const int generalNewer = compareFooters(saveData.data(), gFoot, gFoot + GEN4_PARTITION);
        const int storageNewer = compareFooters(saveData.data(), sFoot, sFoot + GEN4_PARTITION);

        generalBlockOffset = (generalNewer == PARTITION_FIRST ? 0 : GEN4_PARTITION);
        generalBackupOffset = (generalNewer == PARTITION_FIRST ? GEN4_PARTITION : 0);
        storageBlockOffset = (storageNewer == PARTITION_FIRST ? 0 : GEN4_PARTITION) + storageStart();
        storageBackupOffset = (storageNewer == PARTITION_FIRST ? GEN4_PARTITION : 0) + storageStart();
    }

    void Trainer4DP::writeChecksums() noexcept
    {
        auto stamp = [&](size_t offset, size_t length)
        {
            const uint16_t storedCrc = crc16ccitt(&saveData[offset], length - footerSize());
            writeUInt16LittleEndian(&saveData[offset + length - 2], storedCrc);
        };
        auto initialised = [&](size_t offset, size_t length)
        {
            return readUInt32LittleEndian(&saveData[offset + length - 8]) != 0xFFFFFFFFu;
        };
        stamp(generalBlockOffset, generalSize());
        stamp(storageBlockOffset, storageSize());
        // Only refresh a backup that has ever been written. Stamping a CRC into uninitialised 0xFF
        // space makes a garbage partition look valid to the game.
        if (initialised(generalBackupOffset, generalSize()))
            stamp(generalBackupOffset, generalSize());
        if (initialised(storageBackupOffset, storageSize()))
            stamp(storageBackupOffset, storageSize());
    }

    /**
     * Builds a PK4 from an on-disk record, GROWING a box record to party size.
     *
     * A box record is 0x88 bytes and a party record 0xEC; the extra 0x64 hold the level and the
     * five battle stats, which the game recomputes when a Pokemon leaves the PC. PK4's level() and
     * statXXX() index straight into that tail, so a stored-size entity reports LEVEL 0 AND ZERO
     * STATS to everything that asks -- the box grid, the details modal, the legality checker's
     * level tests -- and moving one into the party made the party writer encrypt a party-length
     * span over a box-length allocation, which is a heap over-read of exactly those 0x64 bytes.
     *
     * Growing on load is what Gen 1 and the PKSM import already do, for the same reason. The
     * stored bytes are untouched by it (recalculateStats() writes only the tail, and the checksum
     * covers 0x08..0x88), so writing the record back is still byte-exact.
     */
    std::unique_ptr<::Pokemon::Pokemon> Trainer4DP::makeEntity(const uint8_t *bytes, size_t byteCount) const
    {
        const std::span<const std::byte> source(reinterpret_cast<const std::byte *>(bytes), byteCount);
        if (byteCount >= Encryption::SIZE_PARTY4_DP)
            return std::make_unique<::Pokemon::Pokemon4DP>(source);

        std::byte *dec = Encryption::decryptArray4DP(source);
        std::vector<std::byte> full(Encryption::SIZE_PARTY4_DP, std::byte{0});
        std::memcpy(full.data(), dec, byteCount);
        delete[] dec;
        std::byte *encryptedRecord = Encryption::encryptArray4DP(full);
        auto pokemon = std::make_unique<::Pokemon::Pokemon4DP>(std::span<const std::byte>(encryptedRecord, Encryption::SIZE_PARTY4_DP));
        delete[] encryptedRecord;
        // Fills level + the five battle stats from EXP, IVs and EVs. A species-0 slot has no
        // personal data and is left alone, which is what keeps an empty slot byte-exact.
        pokemon->recalculateStats();
        return pokemon;
    }

    void Trainer4DP::parse()
    {
        parseTrainer();
        parseParty();
        parseBoxes();
        parseItems();
        parseBoxNames();
    }

    void Trainer4DP::parseTrainer()
    {
        const uint8_t *g = generalBlock();
        const size_t trainerOffset = ofsTrainer();
        std::u16string trainerNameUtf16;
        for (int index = 0; index < 8; ++index)
        {
            const uint16_t raw = readUInt16LittleEndian(g + trainerOffset + index * 2);
            if (raw == GEN4_TERMINATOR || raw == 0)
                break;
            const uint16_t decodedChar = gen4ToChar(raw);
            if (decodedChar == 0)
                break;
            trainerNameUtf16.push_back(static_cast<char16_t>(decodedChar));
        }
        trainerName = utf16ToUtf8(trainerNameUtf16);
        ID32 = readUInt32LittleEndian(g + trainerOffset + 0x10);
        TID16 = readUInt16LittleEndian(g + trainerOffset + 0x10);
        SID16 = readUInt16LittleEndian(g + trainerOffset + 0x12);
        TID = TID16;
        SID = SID16;
        money = readUInt32LittleEndian(g + trainerOffset + 0x14);
        trainerGender = g[trainerOffset + 0x18] & 1;
        currentBox = storageBlock()[ofsCurrentBox()];
    }

    void Trainer4DP::parseParty()
    {
        const uint8_t *g = generalBlock();
        const uint8_t storedCount = g[ofsParty() - 4]; // the count sits immediately BEFORE the records
        if (storedCount > MAX_PARTY_SLOTS)
            return;
        for (uint8_t index = 0; index < storedCount; ++index)
        {
            const size_t offset = ofsParty() + index * Encryption::SIZE_PARTY4_DP;
            if (generalBlockOffset + offset + Encryption::SIZE_PARTY4_DP > saveData.size())
                break;
            party.push_back(makeEntity(g + offset, Encryption::SIZE_PARTY4_DP));
        }
    }

    void Trainer4DP::parseBoxes()
    {
        const uint8_t *sourceBytes = storageBlock();
        for (size_t boxIndex = 0; boxIndex < getBoxCount(); ++boxIndex)
        {
            for (size_t slotIndex = 0; slotIndex < BOX_SLOTS; ++slotIndex)
            {
                const size_t offset = ofsBox(boxIndex) + slotIndex * Encryption::SIZE_STORED4_DP;
                if (storageBlockOffset + offset + Encryption::SIZE_STORED4_DP > saveData.size())
                    continue;
                // A slot whose decrypted species is 0 is empty. Keep the entity anyway, the way
                // the SwSh reader does -- "empty" is species 0, not a null pointer.
                boxes[boxIndex][slotIndex] = makeEntity(sourceBytes + offset, Encryption::SIZE_STORED4_DP);
            }
        }
    }

    void Trainer4DP::parseBoxNames()
    {
        const uint8_t *sourceBytes = storageBlock();
        for (size_t boxIndex = 0; boxIndex < getBoxCount(); ++boxIndex)
        {
            const size_t offset = ofsBoxNames() + boxIndex * GEN4_BOX_NAME_LEN;
            std::u16string boxNameUtf16;
            for (size_t charIndex = 0; charIndex < GEN4_BOX_NAME_LEN / 2; ++charIndex)
            {
                const uint16_t raw = readUInt16LittleEndian(sourceBytes + offset + charIndex * 2);
                if (raw == GEN4_TERMINATOR || raw == 0)
                    break;
                const uint16_t decodedChar = gen4ToChar(raw);
                if (decodedChar == 0)
                    break;
                boxNameUtf16.push_back(static_cast<char16_t>(decodedChar));
            }
            // An empty stored name gets a display default, like every other game. Only names the
            // user actually changes are written back (boxNameDirty), so this never persists.
            boxNames[boxIndex] = boxNameUtf16.empty() ? ("Box " + std::to_string(boxIndex + 1)) : utf16ToUtf8(boxNameUtf16);
        }
    }

    void Trainer4DP::parseItems()
    {
        const uint8_t *g = generalBlock() + ofsBagBase();
        const PouchLayout4DP *pouchLayout = POUCHES4_DP;
        items.assign(POUCH_COUNT4_DP, {});
        itemSlot.assign(POUCH_COUNT4_DP, {});
        for (int pouchIndex = 0; pouchIndex < POUCH_COUNT4_DP; ++pouchIndex)
        {
            // EVERY slot, not up to the first zero id. PKHeX's InventoryPouch4 reads the whole
            // pouch, and Gen 6/7 -- which share this codec -- demonstrably park entries after a
            // run of empty slots. Stopping at the first hole loses everything past it and
            // re-packing moves the rest, neither of which any checksum would catch.
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

    void Trainer4DP::updatePartyBlock()
    {
        uint8_t *g = generalBlock();
        size_t written = 0;
        for (size_t partySlotIndex = 0; partySlotIndex < party.size() && partySlotIndex < MAX_PARTY_SLOTS; ++partySlotIndex)
        {
            if (!party[partySlotIndex] || party[partySlotIndex]->speciesID() == 0)
                continue;
            // Unchanged since load: keep the bytes (a Bad Egg keeps its bad checksum, as in PKHeX).
            if (slotHoldsLoadedRecord(party[partySlotIndex].get(), g + ofsParty() + written * Encryption::SIZE_PARTY4_DP, Encryption::SIZE_PARTY4_DP, Encryption::decryptArray4DP))
            {
                ++written;
                continue;
            }
            party[partySlotIndex]->refreshChecksum(); // BEFORE encrypting -- the checksum is the crypt key
            // Staged through a party-length buffer rather than encrypted in place. Every entity
            // that reaches the party should already be party size (makeEntity grows box records
            // on load), but an entity arriving from elsewhere need not be, and encrypting a
            // party-length span over a shorter allocation reads off the end of the heap.
            std::vector<std::byte> rec(Encryption::SIZE_PARTY4_DP, std::byte{0});
            const auto srcData = party[partySlotIndex]->getData();
            std::memcpy(rec.data(), srcData.data(), std::min(rec.size(), srcData.size()));
            std::byte *encryptedRecord = Encryption::encryptArray4DP(rec);
            std::memcpy(g + ofsParty() + written * Encryption::SIZE_PARTY4_DP, encryptedRecord, Encryption::SIZE_PARTY4_DP);
            delete[] encryptedRecord;
            ++written;
        }
        const auto blank = Encryption::blankRecord4DP(Encryption::SIZE_PARTY4_DP);
        for (size_t partySlotIndex = written; partySlotIndex < MAX_PARTY_SLOTS; ++partySlotIndex)
            std::memcpy(g + ofsParty() + partySlotIndex * Encryption::SIZE_PARTY4_DP, blank.data(), blank.size());
        g[ofsParty() - 4] = static_cast<uint8_t>(written);
    }

    void Trainer4DP::updateBoxBlock()
    {
        uint8_t *sourceBytes = storageBlock();
        const auto blank = Encryption::blankRecord4DP(Encryption::SIZE_STORED4_DP);
        for (size_t boxIndex = 0; boxIndex < getBoxCount(); ++boxIndex)
        {
            for (size_t slotIndex = 0; slotIndex < BOX_SLOTS; ++slotIndex)
            {
                const size_t offset = ofsBox(boxIndex) + slotIndex * Encryption::SIZE_STORED4_DP;
                auto &pokemon = boxes[boxIndex][slotIndex];
                // Unchanged since load (an empty slot or a Bad Egg included): keep the file's bytes.
                if (slotHoldsLoadedRecord(pokemon.get(), sourceBytes + offset, Encryption::SIZE_STORED4_DP, Encryption::decryptArray4DP))
                    continue;
                if (pokemon && pokemon->speciesID() != 0)
                {
                    pokemon->refreshChecksum();
                    // Only the STORED half goes to a box slot; a party-size entity's tail (level,
                    // battle stats) is exactly what a box record does not carry.
                    std::vector<std::byte> rec(Encryption::SIZE_STORED4_DP, std::byte{0});
                    const auto srcData = pokemon->getData();
                    std::memcpy(rec.data(), srcData.data(), std::min(rec.size(), srcData.size()));
                    std::byte *encryptedRecord = Encryption::encryptArray4DP(rec);
                    std::memcpy(sourceBytes + offset, encryptedRecord, Encryption::SIZE_STORED4_DP);
                    delete[] encryptedRecord;
                }
                else
                {
                    // Not zeros -- see blankRecord4DP(). A Gen 4 save stores encrypt(blank) in an
                    // empty slot, and stamping zeros rewrites every one of the 540.
                    std::memcpy(sourceBytes + offset, blank.data(), blank.size());
                }
            }
        }
    }

    void Trainer4DP::updateTrainerInfoBlock()
    {
        uint8_t *g = generalBlock();
        const size_t trainerOffset = ofsTrainer();
        // The field is 16 bytes (8 units) and the game caps the name at 7 characters.
        writeGen4Field(g + trainerOffset, 8, utf8ToUtf16(trainerName), getMaxTrainerNameLength());
        writeUInt32LittleEndian(g + trainerOffset + 0x10, ID32);
        writeUInt32LittleEndian(g + trainerOffset + 0x14, money > getMaxMoney() ? getMaxMoney() : money);
        g[trainerOffset + 0x18] = trainerGender & 1;
    }

    void Trainer4DP::updateBoxNameBlock()
    {
        uint8_t *sourceBytes = storageBlock();
        for (size_t boxIndex = 0; boxIndex < getBoxCount(); ++boxIndex)
        {
            // Only names the user actually changed. boxNames also holds the "Box N" display
            // defaults parseBoxNames() invented, and writing those back invents names the player
            // never set -- 215 bytes of them on an untouched Z-A save, which is how this rule
            // came to exist.
            if (!isBoxNameDirty(boxIndex))
                continue;
            // 40 bytes (20 units) per box, of which the game uses at most 8 characters.
            writeGen4Field(sourceBytes + ofsBoxNames() + boxIndex * GEN4_BOX_NAME_LEN, GEN4_BOX_NAME_LEN / 2, utf8ToUtf16(boxNames[boxIndex]), getMaxBoxNameLength());
        }
    }

    void Trainer4DP::updateCurrentBoxBlock() { storageBlock()[ofsCurrentBox()] = currentBox; }

    void Trainer4DP::updateItemBlock()
    {
        uint8_t *g = generalBlock() + ofsBagBase();
        const PouchLayout4DP *pouchLayout = POUCHES4_DP;
        for (int pouchIndex = 0; pouchIndex < POUCH_COUNT4_DP && pouchIndex < static_cast<int>(items.size()); ++pouchIndex)
            writePouchPositional(g + pouchLayout[pouchIndex].offset, pouchLayout[pouchIndex].slots, items[pouchIndex], pouchIndex < static_cast<int>(itemSlot.size()) ? itemSlot[pouchIndex] : std::vector<ItemOrigin>{});
    }

    bool Trainer4DP::canStoreBoxName(const std::string &name) const
    {
        return gen4CanEncode(utf8ToUtf16(name));
    }

    std::unique_ptr<::Pokemon::Pokemon> Trainer4DP::createBlankPokemon() const
    {
        // Party size, not stored size: PK4's level and battle stats live in that tail and its
        // accessors index straight into it.
        return std::make_unique<::Pokemon::Pokemon4DP>();
    }

    const std::vector<uint8_t> &Trainer4DP::serialize()
    {
        updateItemBlock();
        updatePartyBlock();
        updateBoxBlock();
        updateBoxNameBlock();
        updateCurrentBoxBlock();
        updateTrainerInfoBlock();
        writeChecksums(); // LAST -- every write above lands inside a checksummed region
        return saveData;
    }
}
