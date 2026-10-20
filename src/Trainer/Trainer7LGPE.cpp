#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "Trainer/Trainer7LGPE.h"
#include "Pokemon/Pokemon7LGPE.h"
#include "Pokemon/DexTable7LGPE.h"      // the generated dex form-bit + entry tables
#include "Pokemon/PersonalInfoTable.h" // getPersonalInfo -> per-game presence + formCount (Pokedex gate)
#include "Utils/Logger.h"

using namespace Utils;
using namespace Pokemon;

namespace Trainer
{

    void Trainer7LGPE::parseBlock(const Block &block)
    {
        switch (block.key)
        {
        case MY_ITEM7_LGPE:
            parseMyItemBlock(block);
            break;
        case MY_STATUS7_LGPE:
            parseMyStatusBlock(block);
            break;
        case POKE_LIST_HEADER7_LGPE:
            parsePokeListHeaderBlock(block);
            break;
        case POKE_LIST_POKEMON7_LGPE:
            parsePokeListPokemonBlock(block);
            break;
        case MISC7_LGPE:
            parseMiscBlock(block);
            break;
        case PLAY_TIME7_LGPE:
            parsePlayTimeBlock(block);
            break;
        default:
            break;
        }
    }

    void Trainer7LGPE::parseMyStatusBlock(const Block &block)
    {
        /**
         * MY_STATUS Block Structure (from PKHeX MyStatus7):
         * 0x00: ID32 (4 bytes) - Combined TID16 and SID16
         * 0x04: Game version (1 byte)
         * 0x05: Gender (1 byte, 0=Male, 1=Female)
         * 0x38: Trainer Name (26 bytes, UTF-16LE)
         *
         * ID32 format: SID16 << 16 | TID16
         * Display TID: ID32 % 1000000
         * Display SID: ID32 / 1000000
         */
        char logBuffer[256];
        snprintf(logBuffer, sizeof(logBuffer), "parseMyStatusBlock: block size = %zu bytes", block.data.size());
        logInfoToFile(logBuffer);

        if (block.data.size() < 0x38 + 26)
        {
            logInfoToFile("Insufficient data in MY_STATUS block");
            return;
        }

        // The save names its own game (PKHeX MyStatus7b.Game). Accepted only if it is one of this
        // group's two titles -- a byte that says anything else is a misparse, not a discovery, and
        // the group representative is the honest answer then.
        const GameVersion storedVersion = static_cast<GameVersion>(block.data[0x04]);
        if (storedVersion == GameVersion::GP || storedVersion == GameVersion::GE)
            this->gameVersion = storedVersion;

        this->ID32 = readUInt32LittleEndian(&block.data[0x00]);
        this->TID16 = readUInt16LittleEndian(&block.data[0x00]);
        this->SID16 = readUInt16LittleEndian(&block.data[0x02]);
        this->TID = this->ID32 % 1000000;
        this->SID = this->ID32 / 1000000;

        this->trainerName = utf16ToUtf8(getString(&block.data[0x38], 26));
        this->trainerGender = block.data[0x05] & 1; // 0x05: gender (0=M, 1=F)

        snprintf(logBuffer, sizeof(logBuffer), "Parsed trainer: Name='%s', ID32=%u, TID=%u, SID=%u", this->trainerName.c_str(), this->ID32, this->TID, this->SID);
        logInfoToFile(logBuffer);

        // Let's Go doesn't have save revision/DLC, set base values
        this->saveRevision = 0;
        this->saveRevisionString = "Base";
        this->gameVersionString = "";
    }

    void Trainer7LGPE::parsePokeListHeaderBlock(const Block &block)
    {
        /**
         * POKE_LIST_HEADER Block Structure (from PKHeX PokeListHeader.cs):
         *
         * Let's Go uses an INDEX-BASED party system.
         * IMPORTANT: There is NO explicit party count field!
         *
         * Structure (16 bytes total):
         * 0x00-0x01: Party Slot 0 Pointer (u16)
         * 0x02-0x03: Party Slot 1 Pointer (u16)
         * 0x04-0x05: Party Slot 2 Pointer (u16)
         * 0x06-0x07: Party Slot 3 Pointer (u16)
         * 0x08-0x09: Party Slot 4 Pointer (u16)
         * 0x0A-0x0B: Party Slot 5 Pointer (u16)
         * 0x0C-0x0D: Starter Pointer (u16)
         * 0x0E-0x0F: List Count / Next Empty Slot (u16)
         *
         * MAX_SLOTS = 1000 (valid indices: 0-999)
         * SLOT_EMPTY = 1001 marks empty party positions
         *
         * Party count is CALCULATED by counting non-1001 party pointers.
         */
        char logBuffer[256];
        snprintf(logBuffer, sizeof(logBuffer), "parsePokeListHeaderBlock: block size = %zu bytes", block.data.size());
        logInfoToFile(logBuffer);

        if (block.data.size() < 16)
        {
            logInfoToFile("Insufficient data in POKE_LIST_HEADER block");
            return;
        }

        // Debug: Log first 16 bytes
        snprintf(logBuffer, sizeof(logBuffer), "Header bytes 0-15: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X", block.data[0], block.data[1], block.data[2], block.data[3], block.data[4], block.data[5], block.data[6], block.data[7], block.data[8], block.data[9], block.data[10], block.data[11], block.data[12], block.data[13], block.data[14], block.data[15]);
        logInfoToFile(logBuffer);

        constexpr uint16_t SLOT_EMPTY = 1001;
        constexpr uint16_t MAX_SLOTS = 1000;

        // Read party pointers (6 × u16 starting at offset 0)
        // Party count is calculated, not stored!
        uint8_t calculatedPartyCount = 0;
        for (size_t partySlotIndex = 0; partySlotIndex < MAX_PARTY_SLOTS; ++partySlotIndex)
        {
            uint16_t index = readUInt16LittleEndian(&block.data[partySlotIndex * 2]);
            partyIndices[partySlotIndex] = index;

            // Count valid (non-empty) party members
            if (index != SLOT_EMPTY && index < MAX_SLOTS)
            {
                calculatedPartyCount++;
            }

            snprintf(logBuffer, sizeof(logBuffer), "Party slot %zu: index = %u%s", partySlotIndex, index, (index == SLOT_EMPTY) ? " (empty)" : (index >= MAX_SLOTS ? " (INVALID)" : ""));
            logInfoToFile(logBuffer);
        }

        this->partyCount = calculatedPartyCount;
        snprintf(logBuffer, sizeof(logBuffer), "Calculated party count: %u", this->partyCount);
        logInfoToFile(logBuffer);

        starterIndex = readUInt16LittleEndian(&block.data[0x0C]);
        snprintf(logBuffer, sizeof(logBuffer), "Starter index: %u%s", starterIndex, (starterIndex == SLOT_EMPTY) ? " (empty)" : "");
        logInfoToFile(logBuffer);

        uint16_t listCount = readUInt16LittleEndian(&block.data[0x0E]);
        snprintf(logBuffer, sizeof(logBuffer), "List count (next empty slot): %u", listCount);
        logInfoToFile(logBuffer);
    }

    /// In Let's Go, party and boxes share ONE list: indices 0-999 are the box slots (40 boxes of
    /// 25), and the party is named by partyIndices in PokeListHeader rather than stored separately.
    void Trainer7LGPE::parsePokeListPokemonBlock(const Block &block)
    {
        char logBuffer[256];
        snprintf(logBuffer, sizeof(logBuffer), "parsePokeListPokemonBlock: block size = %zu bytes", block.data.size());
        logInfoToFile(logBuffer);

        const std::span<const std::byte> blockSpan(reinterpret_cast<const std::byte *>(block.data.data()), block.data.size());

        size_t pokemonCount = 0;
        constexpr uint16_t SLOT_EMPTY = 1001;
        constexpr size_t MAX_STORAGE_SLOTS = BOX_COUNT7_LGPE * SLOTS_PER_BOX7_LGPE; // 1000

        for (size_t boxIndex = 0; boxIndex < BOX_COUNT7_LGPE; ++boxIndex)
        {
            if (boxNames[boxIndex].empty())
            {
                boxNames[boxIndex] = "Box " + std::to_string(boxIndex + 1);
            }

            for (size_t slot = 0; slot < SLOTS_PER_BOX7_LGPE; ++slot)
            {
                const size_t offset = (boxIndex * SLOTS_PER_BOX7_LGPE + slot) * SIZE_PARTY7_LGPE;
                if (offset + SIZE_PARTY7_LGPE > block.data.size())
                {
                    snprintf(logBuffer, sizeof(logBuffer), "Box %zu slot %zu: offset %zu exceeds block size", boxIndex, slot, offset);
                    logInfoToFile(logBuffer);
                    break;
                }

                std::span<const std::byte> slotSpan = blockSpan.subspan(offset, SIZE_PARTY7_LGPE);

                uint32_t encryptionConstant = readUInt32LittleEndian(reinterpret_cast<const uint8_t *>(slotSpan.data()));
                if (encryptionConstant != 0)
                {
                    boxes[boxIndex][slot] = std::make_unique<Pokemon7LGPE>(slotSpan);
                    pokemonCount++;
                }
                else
                {
                    boxes[boxIndex][slot] = nullptr;
                }
            }
        }

        snprintf(logBuffer, sizeof(logBuffer), "Loaded %zu box Pokemon", pokemonCount);
        logInfoToFile(logBuffer);

        // Now populate party from partyIndices
        // Party indices point to storage slots (0-999)
        for (size_t partySlotIndex = 0; partySlotIndex < partyCount && partySlotIndex < MAX_PARTY_SLOTS; ++partySlotIndex)
        {
            uint16_t storageIndex = partyIndices[partySlotIndex];

            if (storageIndex == SLOT_EMPTY || storageIndex >= MAX_STORAGE_SLOTS)
            {
                snprintf(logBuffer, sizeof(logBuffer), "Party slot %zu: skipped (index=%u)", partySlotIndex, storageIndex);
                logInfoToFile(logBuffer);
                continue;
            }

            size_t boxIndex = storageIndex / SLOTS_PER_BOX7_LGPE;
            size_t slotIndex = storageIndex % SLOTS_PER_BOX7_LGPE;

            if (boxIndex < BOX_COUNT7_LGPE && boxes[boxIndex][slotIndex])
            {
                const size_t offset = storageIndex * SIZE_PARTY7_LGPE;
                if (offset + SIZE_PARTY7_LGPE <= block.data.size())
                {
                    std::span<const std::byte> slotSpan = blockSpan.subspan(offset, SIZE_PARTY7_LGPE);
                    party.push_back(std::make_unique<Pokemon7LGPE>(slotSpan));
                    snprintf(logBuffer, sizeof(logBuffer), "Party slot %zu: loaded from storage index %u (box %zu, slot %zu)", partySlotIndex, storageIndex, boxIndex, slotIndex);
                    logInfoToFile(logBuffer);
                }
            }
            else
            {
                snprintf(logBuffer, sizeof(logBuffer), "Party slot %zu: storage index %u has no Pokemon", partySlotIndex, storageIndex);
                logInfoToFile(logBuffer);
            }
        }

        snprintf(logBuffer, sizeof(logBuffer), "Loaded %zu party Pokemon from indices", party.size());
        logInfoToFile(logBuffer);
    }

    void Trainer7LGPE::parseMiscBlock(const Block &block)
    {
        if (block.data.size() < 0x04 + 4)
        {
            return;
        }

        this->money = readUInt32LittleEndian(&block.data[0x04]);
    }

    void Trainer7LGPE::parsePlayTimeBlock(const Block &block)
    {
        if (block.data.size() < 4)
        {
            return;
        }

        uint16_t hours = readUInt16LittleEndian(&block.data[0x00]);
        uint8_t minutes = block.data[0x02];
        uint8_t seconds = block.data[0x03];

        char buffer[64];
        snprintf(buffer, sizeof(buffer), "Play time: %d:%02d:%02d", hours, minutes, seconds);
        logInfoToFile(buffer);
    }

    void Trainer7LGPE::parseMyItemBlock(const Block &block)
    {
        /**
         * MY_ITEM Block Structure for Let's Go:
         * Items are stored in pouches (categories):
         * - Medicine: Healing items
         * - TMs: Technical Machines
         * - Candy: Stat-boosting candies
         * - PowerUp: Evolution stones, PP items
         * - Catching: Poke Balls, Berries
         * - Battle: Battle items, Mega Stones
         * - KeyItems: Quest items
         *
         * Each item is stored as 4 bytes: (count << 10) | itemId
         */
        char logBuffer[256];
        snprintf(logBuffer, sizeof(logBuffer), "parseMyItemBlock: block size = %zu bytes", block.data.size());
        logInfoToFile(logBuffer);

        items.resize(POUCH_COUNT7_LGPE);

        for (size_t pouchIndex = 0; pouchIndex < POUCH_COUNT7_LGPE; pouchIndex++)
        {
            PouchType7LGPE pouchType = static_cast<PouchType7LGPE>(pouchIndex);
            const PouchInfo7LGPE &info = getPouchInfo7LGPE(pouchType);

            std::vector<InventoryItem> pouch;
            pouch.reserve(info.maxSlots);

            for (int itemSlotIndex = 0; itemSlotIndex < info.maxSlots; itemSlotIndex++)
            {
                size_t offset = info.offset + (itemSlotIndex * 4);
                if (offset + 4 <= block.data.size())
                {
                    uint32_t itemValue = readUInt32LittleEndian(&block.data[offset]);
                    InventoryItem7LGPE item = InventoryItem7LGPE::fromValue(itemValue);

                    // Only add items with valid IDs (non-zero). isNew (bit 30) is preserved as-read
                    // so it round-trips faithfully on save.
                    if (item.itemId != 0)
                    {
                        pouch.push_back(item);
                    }
                }
            }

            items[pouchIndex] = std::move(pouch);
        }

        // Log item counts
        size_t totalItems = 0;
        for (const auto &pouch : items)
        {
            totalItems += pouch.size();
        }
        snprintf(logBuffer, sizeof(logBuffer), "Loaded %zu total items across %zu pouches", totalItems, items.size());
        logInfoToFile(logBuffer);
    }

    namespace
    {
        // Encrypt a Pokemon's decrypted buffer (seed = EncryptionConstant at 0x00) and write
        // it into `destination` at `offset`. Inverse of the read path's decryptArray7LGPE.
        void writeEncryptedPokemon(std::vector<uint8_t> &destination, size_t offset, const ::Pokemon::Pokemon &pokemon)
        {
            const size_t size = pokemon.getDataSize();
            if (offset + size > destination.size())
                return;
            uint32_t encryptionConstant = readUInt32LittleEndian(reinterpret_cast<const uint8_t *>(pokemon.getData().data()));
            std::span<const std::byte> decrypted(pokemon.getData().data(), size);
            std::byte *encryptedRecord = Encryption::encryptArray7LGPE(decrypted, encryptionConstant);
            std::memcpy(&destination[offset], encryptedRecord, size);
            delete[] encryptedRecord;
        }
    }

    void Trainer7LGPE::updatePartyBlock()
    {
        // No-op for LGPE: party, boxes, and the header are all serialized together in
        // updateBoxBlock(), which compacts the gapless storage and remaps the party/starter
        // pointers in one pass. (saveTrainerInfoLetsGo calls updateBoxBlock() first.)
    }

    void Trainer7LGPE::updateBoxBlock()
    {
        /**
         * Serializes ALL LGPE storage on save (updatePartyBlock is a no-op that defers here).
         * LGPE storage is a GAPLESS packed list of 1000 slots, so moves that leave gaps must be
         * compacted before writing:
         *   1. Walk boxes in order, collect occupied Pokemon into a packed list, and record each
         *      old storage index -> new packed index (this removes any gaps left by moves).
         *   2. Write the packed list to storage; fill the tail with the file's own empty slots.
         *   3. Overlay the party copies at their remapped indices (party edits win over the box
         *      copy of the same slot).
         *   4. Write the header: remapped party pointers + starter + the packed count.
         * For in-place edits and swaps (no gaps) this collapses to the identity mapping, so it
         * produces exactly the previous result.
         */
        constexpr size_t TOTAL = BOX_COUNT7_LGPE * SLOTS_PER_BOX7_LGPE; // 1000
        constexpr uint16_t SLOT_EMPTY = 1001;

        // 1. Build packed list + old->new index map.
        std::vector<int> oldToNew(TOTAL, -1);
        std::vector<::Pokemon::Pokemon *> packed;
        packed.reserve(TOTAL);
        for (size_t box = 0; box < BOX_COUNT7_LGPE; ++box)
        {
            for (size_t slot = 0; slot < SLOTS_PER_BOX7_LGPE; ++slot)
            {
                const size_t oldIdx = box * SLOTS_PER_BOX7_LGPE + slot;
                if (boxes[box][slot] && boxes[box][slot]->speciesID() != 0)
                {
                    oldToNew[oldIdx] = static_cast<int>(packed.size());
                    packed.push_back(boxes[box][slot].get());
                }
            }
        }
        const uint16_t packedCount = static_cast<uint16_t>(packed.size());

        auto remap = [&](uint16_t oldIdx) -> uint16_t
        {
            return (oldIdx < TOTAL && oldToNew[oldIdx] >= 0) ? static_cast<uint16_t>(oldToNew[oldIdx]) : SLOT_EMPTY;
        };

        // 2 & 3. Write packed storage, then overlay party copies at their remapped indices.
        for (auto &block : blocks)
        {
            if (block.key != POKE_LIST_POKEMON7_LGPE)
                continue;
            const size_t required = TOTAL * SIZE_PARTY7_LGPE;
            if (block.data.size() < required)
                block.data.resize(required, 0);

            // The tail keeps the file's own empty slots, in order, as PKHeX's CompressStorage does.
            // The game writes an empty slot as the ENCRYPTED blank, 246 non-zero bytes behind a zero
            // encryption constant, so zeroing the tail rewrote a quarter of the file on an unedited
            // save. A slot the user emptied still holds its Pokemon, so it is not reused.
            std::vector<uint8_t> emptySlotBytes;
            for (size_t nIndex = 0; nIndex < TOTAL; ++nIndex)
            {
                const size_t slotOffset = nIndex * SIZE_PARTY7_LGPE;
                if (readUInt32LittleEndian(&block.data[slotOffset]) == 0)
                {
                    emptySlotBytes.insert(emptySlotBytes.end(), block.data.begin() + static_cast<std::ptrdiff_t>(slotOffset), block.data.begin() + static_cast<std::ptrdiff_t>(slotOffset + SIZE_PARTY7_LGPE));
                }
            }
            const size_t emptySlotCount = emptySlotBytes.size() / SIZE_PARTY7_LGPE;

            for (size_t nIndex = 0; nIndex < TOTAL; ++nIndex)
            {
                const size_t offset = nIndex * SIZE_PARTY7_LGPE;
                if (nIndex < packed.size())
                {
                    writeEncryptedPokemon(block.data, offset, *packed[nIndex]);
                    continue;
                }
                const size_t tailIndex = nIndex - packed.size();
                if (tailIndex < emptySlotCount)
                {
                    std::memcpy(&block.data[offset], &emptySlotBytes[tailIndex * SIZE_PARTY7_LGPE], SIZE_PARTY7_LGPE);
                }
                else
                {
                    std::memset(&block.data[offset], 0, SIZE_PARTY7_LGPE);
                }
            }

            for (size_t partySlotIndex = 0; partySlotIndex < party.size() && partySlotIndex < MAX_PARTY_SLOTS; ++partySlotIndex)
            {
                const uint16_t newIdx = remap(partyIndices[partySlotIndex]);
                if (newIdx >= TOTAL)
                    continue;
                if (!party[partySlotIndex] || party[partySlotIndex]->speciesID() == 0)
                    continue;
                writeEncryptedPokemon(block.data, static_cast<size_t>(newIdx) * SIZE_PARTY7_LGPE, *party[partySlotIndex]);
            }
            break;
        }

        // 4. Header: remapped party pointers + starter + packed count.
        for (auto &block : blocks)
        {
            if (block.key != POKE_LIST_HEADER7_LGPE)
                continue;
            if (block.data.size() >= 0x10)
            {
                for (size_t partySlotIndex = 0; partySlotIndex < MAX_PARTY_SLOTS; ++partySlotIndex)
                {
                    writeUInt16LittleEndian(&block.data[partySlotIndex * 2], remap(partyIndices[partySlotIndex]));
                }
                writeUInt16LittleEndian(&block.data[0x0C], remap(starterIndex));
                writeUInt16LittleEndian(&block.data[0x0E], packedCount);
            }
            break;
        }
    }

    bool Trainer7LGPE::compactStorage()
    {
        /**
         * The in-memory twin of updateBoxBlock()'s step 1. LGPE storage is one gapless 1000-slot
         * list, so a move that vacates a slot leaves a hole the game cannot represent; the save
         * path already closes it, and this closes it live so the editor shows what will be written
         * instead of a gap that silently disappears on reload.
         *
         * The dangerous half is the remap at the end, NOT the shuffle: party members and the
         * partner reference storage BY INDEX, so re-packing without remapping would leave them
         * pointing at whichever Pokemon slid into the vacated slot.
         */
        constexpr size_t TOTAL = BOX_COUNT7_LGPE * SLOTS_PER_BOX7_LGPE; // 1000
        constexpr uint16_t SLOT_EMPTY = 1001;

        // Cheap pre-scan, no allocation: storage is packed iff no occupied slot follows an empty
        // one. This runs every frame, so the common "nothing to do" case must stay allocation-free.
        bool seenEmpty = false, hasGap = false;
        for (size_t box = 0; box < BOX_COUNT7_LGPE && !hasGap; ++box)
        {
            for (size_t slot = 0; slot < SLOTS_PER_BOX7_LGPE; ++slot)
            {
                const auto &cell = boxes[box][slot];
                // Gate on species, not the pointer: a "ghost" (non-null but species 0) is an empty
                // slot, and treating it as occupied would hold the hole open forever.
                if (!cell || cell->speciesID() == 0)
                    seenEmpty = true;
                else if (seenEmpty)
                {
                    hasGap = true;
                    break;
                }
            }
        }
        if (!hasGap)
            return false;

        std::vector<int> oldToNew(TOTAL, -1);
        std::vector<std::unique_ptr<::Pokemon::Pokemon>> packed;
        packed.reserve(TOTAL);
        for (size_t box = 0; box < BOX_COUNT7_LGPE; ++box)
        {
            for (size_t slot = 0; slot < SLOTS_PER_BOX7_LGPE; ++slot)
            {
                auto &cell = boxes[box][slot];
                if (cell && cell->speciesID() != 0)
                {
                    oldToNew[box * SLOTS_PER_BOX7_LGPE + slot] = static_cast<int>(packed.size());
                    packed.push_back(std::move(cell));
                }
                else
                {
                    cell.reset(); // drop ghosts while we are here
                }
            }
        }

        size_t byteCount = 0;
        for (size_t box = 0; box < BOX_COUNT7_LGPE; ++box)
        {
            for (size_t slot = 0; slot < SLOTS_PER_BOX7_LGPE; ++slot)
            {
                boxes[box][slot] = (byteCount < packed.size()) ? std::move(packed[byteCount++]) : nullptr;
            }
        }

        // Remap everything that points INTO storage. Same mapping updateBoxBlock() will apply.
        auto remap = [&](uint16_t oldIdx) -> uint16_t
        {
            return (oldIdx < TOTAL && oldToNew[oldIdx] >= 0) ? static_cast<uint16_t>(oldToNew[oldIdx]) : SLOT_EMPTY;
        };
        for (size_t partySlotIndex = 0; partySlotIndex < MAX_PARTY_SLOTS; ++partySlotIndex)
            partyIndices[partySlotIndex] = remap(partyIndices[partySlotIndex]);
        starterIndex = remap(starterIndex);
        return true;
    }

    void Trainer7LGPE::mirrorPartyMemberFromBox(size_t boxIndex, size_t slotIndex)
    {
        // If this box slot is a party member, copy its (just-edited) bytes into the party copy so
        // updateBoxBlock()'s party overlay doesn't clobber the edit on save. Both are PK7b (same
        // size), and the box edit already recomputed stats + checksum, so a raw buffer copy suffices.
        const int position = getPartyPosition(boxIndex, slotIndex); // 1-based; 0 if not a party member
        if (position <= 0)
            return;
        const size_t partyIndex = static_cast<size_t>(position - 1);
        if (partyIndex >= party.size() || !party[partyIndex])
            return;
        if (boxIndex >= boxes.size() || slotIndex >= boxes[boxIndex].size())
            return;
        auto &source = boxes[boxIndex][slotIndex];
        if (!source)
            return;
        const size_t byteCount = std::min(party[partyIndex]->getDataSize(), source->getDataSize());
        std::memcpy(party[partyIndex]->getData().data(), source->getData().data(), byteCount);
    }

    void Trainer7LGPE::mirrorPartyMemberFromParty(size_t partyIndex)
    {
        // Reverse direction: push a party-copy edit back into its box/storage slot (the display copy)
        // so the two representations stay byte-identical.
        constexpr uint16_t SLOT_EMPTY = 1001;
        if (partyIndex >= party.size() || !party[partyIndex])
            return;
        const uint16_t index = partyIndices[partyIndex];
        if (index == SLOT_EMPTY)
            return;
        const size_t boxIndex = index / SLOTS_PER_BOX7_LGPE;
        const size_t slotIndex = index % SLOTS_PER_BOX7_LGPE;
        if (boxIndex >= boxes.size() || slotIndex >= boxes[boxIndex].size())
            return;
        auto &destination = boxes[boxIndex][slotIndex];
        if (!destination)
            return;
        const size_t byteCount = std::min(destination->getDataSize(), party[partyIndex]->getDataSize());
        std::memcpy(destination->getData().data(), party[partyIndex]->getData().data(), byteCount);
    }

    std::unique_ptr<::Pokemon::Pokemon> Trainer7LGPE::createBlankPokemon() const
    {
        // A zeroed *decrypted* PB7 buffer encrypted with EC/seed 0, then fed to the ctor (which
        // decrypts it straight back to zeros) -> a clean species-0, checksum-valid entity. LGPE's
        // own empty box slots are written as raw zeros (the read path gates on a non-zero EC), but a
        // *live* blank entity still needs a valid decrypted PB7 buffer, so use the encrypt->decrypt
        // round-trip here (same principle as the SwishCrypto gens' encrypted-blank fallback).
        std::vector<std::byte> zero(SIZE_PARTY7_LGPE, std::byte{0});
        std::byte *encryptedRecord = Encryption::encryptArray7LGPE(std::span<const std::byte>(zero.data(), SIZE_PARTY7_LGPE), 0);
        auto clone = std::make_unique<Pokemon7LGPE>(std::span<const std::byte>(encryptedRecord, SIZE_PARTY7_LGPE));
        delete[] encryptedRecord;
        return clone;
    }

    //
    // Far richer than Gen 3's two bit arrays. Layout, relative to the block start (PKHeX Zukan7 /
    // Zukan7b; the language-flag offset 0x550 is the ctor argument in SaveBlockAccessor7b):
    //
    //   0x000  magic u32 (0x2F120F17) + flags u32 + misc 0x80
    //   0x088  CAUGHT      0x68 bytes  -- bit (species - 1)
    //   0x0F0  SEEN        4 regions of 0x8C, indexed by shift = (gender & 1) | (shiny << 1)
    //   0x320  DISPLAYED   4 more regions of 0x8C, i.e. SEEN + (shift + 4) * 0x8C
    //   0x550  LANGUAGE    bit (dexBit * 9 + langIndex)
    //
    // The DISPLAYED flag is what makes an entry actually render; seen alone leaves a blank slot. The
    // games set it for the FIRST variant registered and leave it there, so it is only written when no
    // displayed flag exists for that species/form in any of the four regions.
    namespace
    {
        constexpr size_t ZUKAN_OFS_CAUGHT = 0x088;
        constexpr size_t ZUKAN_OFS_SEEN = 0x0F0;
        constexpr size_t ZUKAN_BIT_REGION = 0x08C; // bytes per seen/displayed region
        constexpr size_t ZUKAN_OFS_LANG = 0x550;
        constexpr int ZUKAN_LANG_COUNT = 9;
        constexpr uint16_t LGPE_MAX_SPECIES = 809; // Melmetal -- the base for alternate-form bits

        // The dex form-bit index and the (species, form) entry list are GENERATED, in
        // DexTable7LGPE. They were hand-transcribed here from PKHeX -- correctly, and with the
        // provenance written down -- but PKHeX-derived data that no generator can refresh is data
        // that eventually goes wrong with no way to say so, which is exactly the position the
        // pokemondb base-stat tables were in. getDexFormBitIndex7LGPE / getDexEntryIndex7LGPE
        // reproduce what was here byte for byte.
        //
        // PKHeX additionally bails when the GG form count exceeds the save's personal-table
        // FormCount. That guard was not reproduced because PKSE carried only the Gen 9-derived
        // personal table to compare against; it now has PersonalInfo7LGPE, generated from
        // personal_gg, so the guard is reproducible -- getDexFormCount7LGPE is what it needs. It
        // is still not applied, because the counts agree for all 32 species and turning it on is
        // a behaviour change rather than a move.

        // Let's Go remembers the smallest and largest of each species you have seen. Four groups of
        // 186 six-byte entries at 0xF78; the table ends exactly on the block's last byte (0x20E8),
        // which is a useful check that the geometry is right.
        //   entry[0..1] u16 height = 2 x height scalar, [2] weight scalar, [3] 0, [4..5] untouched
        // An untouched entry reads as UNSET (height 0xFE / weight 0x7F) and the dex shows no record.
        //
        // The height is a u16 holding twice the scalar, not PKHeX's Zukan7b reading (byte 0 the
        // scalar, byte 1 a "flagged" marker). Every record in a real save is even, a third of them
        // have byte 1 set, and each one that matches a stored Pokemon holds exactly twice its
        // scalar. Read PKHeX's way, a save halved every height record it rewrote.
        constexpr size_t ZUKAN_SIZE_START = 0xF78;
        constexpr size_t ZUKAN_SIZE_ENTRY = 6;
        constexpr size_t ZUKAN_SIZE_COUNT = 186;
        enum : int
        {
            SIZE_MIN_HEIGHT = 0,
            SIZE_MAX_HEIGHT = 1,
            SIZE_MIN_WEIGHT = 2,
            SIZE_MAX_WEIGHT = 3
        };

        size_t ggSizeOffset(int group, int index)
        {
            return ZUKAN_SIZE_START + ZUKAN_SIZE_ENTRY * (static_cast<size_t>(index) + static_cast<size_t>(group) * ZUKAN_SIZE_COUNT);
        }
        bool ggSizeUnset(const std::vector<uint8_t> &d, size_t offset)
        {
            // UNSET == 0x007F00FE little-endian: FE 00 7F 00.
            return offset + 4 <= d.size() && d[offset] == 0xFE && d[offset + 1] == 0x00 && d[offset + 2] == 0x7F && d[offset + 3] == 0x00;
        }
        uint8_t ggSizeHeightScalar(const std::vector<uint8_t> &dexBytes, size_t entryOffset)
        {
            return static_cast<uint8_t>(readUInt16LittleEndian(&dexBytes[entryOffset]) / 2);
        }
        void ggSizeWrite(std::vector<uint8_t> &d, size_t offset, uint8_t height, uint8_t weight)
        {
            if (offset + 4 > d.size())
                return;
            writeUInt16LittleEndian(&d[offset], static_cast<uint16_t>(height * 2));
            d[offset + 2] = weight;
            d[offset + 3] = 0;
        }
        // Same ratios the creator and the bank converter use for PB7 absolute size (PKHeX PB7).
        float ggHeightRatio(uint8_t sourceBytes)
        {
            return (static_cast<float>(sourceBytes) / 255.0f) * 0.79999995f + 0.6f;
        }
        float ggWeightRatio(uint8_t sourceBytes)
        {
            return (static_cast<float>(sourceBytes) / 255.0f) * 0.40000004f + 0.8f;
        }
        float ggWeightAbsoluteFrom(const ::Pokemon::PersonalInfo &pi, uint8_t heightScalar, uint8_t weightScalar)
        {
            return ggHeightRatio(heightScalar) * (ggWeightRatio(weightScalar) * static_cast<float>(pi.weight));
        }

        // Language id -> dex language slot. Slot 6 (langID 6) is unused, so 7+ shift down by two.
        int ggLangSlot(uint8_t language)
        {
            if (language == 0 || language == 6 || language > 10)
                return -1;
            return (language >= 7) ? language - 2 : language - 1;
        }

        inline bool getBit(const std::vector<uint8_t> &d, size_t offset, int bit)
        {
            const size_t byteOfs = offset + static_cast<size_t>(bit >> 3);
            return byteOfs < d.size() && (d[byteOfs] & (1u << (bit & 7))) != 0;
        }
        inline void setBit(std::vector<uint8_t> &d, size_t offset, int bit)
        {
            const size_t byteOfs = offset + static_cast<size_t>(bit >> 3);
            if (byteOfs < d.size())
                d[byteOfs] |= static_cast<uint8_t>(1u << (bit & 7));
        }
    }

    void Trainer7LGPE::updatePokedexBlock()
    {
        std::vector<uint8_t> *dex = nullptr;
        for (auto &block : blocks)
        {
            if (block.key == ZUKAN7_LGPE)
            {
                dex = &block.data;
                break;
            }
        }
        // block missing or too small: leave it alone
        if (!dex || dex->size() < ZUKAN_OFS_LANG) return;

        auto registerMon = [&](const ::Pokemon::Pokemon *pokemon)
        {
            if (!pokemon || pokemon->isEgg())
                return;
            const uint16_t species = pokemon->speciesID();
            if (species == 0 || species > LGPE_MAX_SPECIES)
                return;

            const uint8_t form = pokemon->form();
            // Gate on the dex's own entry table, not on the personal table's per-game presence bit.
            // Presence answers "can this game hold it", which is a different question: it is true for
            // forms the DEX has no entry for, and writing a form bit for one of those would set a flag
            // the game never reads.
            //
            // The partner Pikachu (form 8) and partner Eevee (form 1) have no entry, so they are not
            // registered at all, as PKHeX's Zukan7b.SetDex does. Folding them onto form 0 first wrote
            // the partner's size into records the game keeps differently: a real save holds the
            // partner Eevee as Eevee's maximum height and weight, and an unedited write added it as
            // the minimum weight too.
            const int entryIndex = getDexEntryIndex7LGPE(species, form);
            if (entryIndex < 0)
                return;

            const int baseBit = species - 1;
            const uint8_t gender = pokemon->gender();
            const bool shiny = pokemon->isShiny(pokemon->id32(), pokemon->species());
            // Genderless counts as male here, matching the games (gender 2 -> bit 0).
            const int shift = ((gender == 1) ? 1 : 0) | (shiny ? 2 : 0);

            // CAUGHT is per species -- one bit for Rattata whichever form you own.
            setBit(*dex, ZUKAN_OFS_CAUGHT, baseBit);

            // Alternate forms live at their own bit, past the species range.
            int formBit = baseBit;
            if (form > 0)
            {
                const int index = getDexFormBitIndex7LGPE(species);
                if (index >= 0)
                    formBit = LGPE_MAX_SPECIES + index + (form - 1);
            }

            // SEEN is per FORM, not per species -- it is indexed by the FORM bit, and that is what
            // puts a form in the dex's selector. Confirmed against a real save: trading a Kantonian
            // Rattata for an Alolan one set seen bit 815 (Rattata form 1) and touched nothing else in
            // the seen regions. Writing it at the species bit instead (which is what PKHeX's
            // SetDexFlags does) records the sighting against the base form, so the alternate form
            // never appears however many of them you own.
            //
            // Identical for form 0, where formBit == baseBit.
            setBit(*dex, ZUKAN_OFS_SEEN + static_cast<size_t>(shift) * ZUKAN_BIT_REGION, formBit);

            // DISPLAYED is a single marker per dex entry: "this is the variant to show". The game
            // MOVES it to whatever you most recently obtained -- the same trade cleared it from the
            // base Rattata and set it on the Alolan one.
            //
            // It is deliberately NOT moved here. PKSE registers in bulk over storage, so "most
            // recent" would mean "last in box order" -- an arbitrary choice that would silently
            // change which form the player's dex shows every time they save. It is only set when the
            // entry has none at all, which is the case that matters: a species whose first sighting
            // is an alternate form (a fresh Alolan Sandslash) still gets an entry to display.
            bool anyDisplayed = false;
            for (int moveIndex = 0; moveIndex < 4 && !anyDisplayed; ++moveIndex)
            {
                const size_t offset = ZUKAN_OFS_SEEN + static_cast<size_t>(moveIndex + 4) * ZUKAN_BIT_REGION;
                anyDisplayed = getBit(*dex, offset, baseBit) || getBit(*dex, offset, formBit);
            }
            if (!anyDisplayed)
                setBit(*dex, ZUKAN_OFS_SEEN + static_cast<size_t>(shift + 4) * ZUKAN_BIT_REGION, formBit);

            const int lang = ggLangSlot(pokemon->language());
            if (lang >= 0)
                setBit(*dex, ZUKAN_OFS_LANG, baseBit * ZUKAN_LANG_COUNT + lang);

            // Compared against the species' BASE size: a Pokemon smaller than base can only ever be a
            // minimum, larger can only be a maximum, and exactly base is neither. A record is taken
            // when it beats the stored one, or when nothing is stored yet.
            //
            // The size fields are PB7-only, so this needs the derived type. RTTI is off, so the cast is
            // guarded by getGameGroup() -- the project's standard cross-generation dispatch. A slot in an
            // LGPE trainer should always hold a PB7 (the bank converts on withdrawal), but a static_cast
            // is unchecked and this is the one place that would silently read a foreign buffer.
            if (pokemon->getGameGroup() != Enums::GameVersion::GG)
                return;
            const auto *lg = static_cast<const Pokemon7LGPE *>(pokemon);
            const float hAbs = lg->heightAbsolute();
            const float wAbs = lg->weightAbsolute();
            // A Pokemon with no absolute size written (0.0) is not a record of anything -- older PKSE
            // builds left these blank, and treating that as "smallest ever" would stamp a bogus 0 into
            // the dex that the player could never beat.
            if (!(hAbs > 0.0f) || !(wAbs > 0.0f))
                return;

            const ::Pokemon::PersonalInfo &pi = ::Pokemon::getPersonalInfo(species, form);
            const uint8_t heightScalarValue = lg->heightScalar();
            const uint8_t weightScalarValue = lg->weightScalar();

            if (hAbs < static_cast<float>(pi.height))
            {
                const size_t offset = ggSizeOffset(SIZE_MIN_HEIGHT, entryIndex);
                if (offset + ZUKAN_SIZE_ENTRY <= dex->size() && (ggSizeUnset(*dex, offset) || heightScalarValue < ggSizeHeightScalar(*dex, offset)))
                    ggSizeWrite(*dex, offset, heightScalarValue, weightScalarValue);
            }
            else if (hAbs > static_cast<float>(pi.height))
            {
                const size_t offset = ggSizeOffset(SIZE_MAX_HEIGHT, entryIndex);
                if (offset + ZUKAN_SIZE_ENTRY <= dex->size() && (ggSizeUnset(*dex, offset) || heightScalarValue > ggSizeHeightScalar(*dex, offset)))
                    ggSizeWrite(*dex, offset, heightScalarValue, weightScalarValue);
            }

            // Weight records compare ABSOLUTE weight, not the scalar: absolute weight depends on the
            // height scalar too, so a bigger weight scalar is not necessarily a heavier Pokemon.
            if (wAbs < static_cast<float>(pi.weight))
            {
                const size_t offset = ggSizeOffset(SIZE_MIN_WEIGHT, entryIndex);
                if (offset + ZUKAN_SIZE_ENTRY <= dex->size() && (ggSizeUnset(*dex, offset) || wAbs < ggWeightAbsoluteFrom(pi, ggSizeHeightScalar(*dex, offset), (*dex)[offset + 2])))
                    ggSizeWrite(*dex, offset, heightScalarValue, weightScalarValue);
            }
            else if (wAbs > static_cast<float>(pi.weight))
            {
                const size_t offset = ggSizeOffset(SIZE_MAX_WEIGHT, entryIndex);
                if (offset + ZUKAN_SIZE_ENTRY <= dex->size() && (ggSizeUnset(*dex, offset) || wAbs > ggWeightAbsoluteFrom(pi, ggSizeHeightScalar(*dex, offset), (*dex)[offset + 2])))
                    ggSizeWrite(*dex, offset, heightScalarValue, weightScalarValue);
            }
        };

        for (const auto &pokemon : party)
            registerMon(pokemon.get());
        for (const auto &box : boxes)
            for (const auto &pokemon : box)
                registerMon(pokemon.get());
    }

    void Trainer7LGPE::updateTrainerInfoBlock()
    {
        // Write money / OT name back to the same blocks they are parsed from. Block CRC-16/ARC
        // checksums are recomputed later by writeBlocksToSaveData7LGPE().
        for (auto &block : blocks)
        {
            if (block.key == MY_STATUS7_LGPE)
            {
                // OT name, 12 chars
                if (block.data.size() >= 0x38 + 26) setString(&block.data[0x38], 26, utf8ToUtf16(trainerName), 12);
            }
            else if (block.key == MISC7_LGPE)
            {
                if (block.data.size() >= 0x04 + 4)
                    writeUInt32LittleEndian(&block.data[0x04], money);
            }
        }
    }

    void Trainer7LGPE::updateItemBlock()
    {
        /**
         * Serializes the inventory back into the MY_ITEM block (the inverse of parseMyItemBlock).
         * Each pouch's items are written compacted from its offset (4 bytes each via the
         * InventoryItem7b layout), then the remaining slots up to maxSlots are zeroed. Because the
         * fromValue/toValue round-trip is byte-exact for valid items, a save that didn't touch items
         * reproduces the original block bytes. The block is re-hashed/re-encrypted by the caller.
         */
        for (auto &block : blocks)
        {
            if (block.key != MY_ITEM7_LGPE)
                continue;

            for (size_t pouchIndex = 0; pouchIndex < POUCH_COUNT7_LGPE; ++pouchIndex)
            {
                const PouchInfo7LGPE &info = getPouchInfo7LGPE(static_cast<PouchType7LGPE>(pouchIndex));
                const size_t itemCount = (pouchIndex < items.size()) ? items[pouchIndex].size() : 0;

                for (int itemSlotIndex = 0; itemSlotIndex < info.maxSlots; ++itemSlotIndex)
                {
                    const size_t offset = static_cast<size_t>(info.offset) + static_cast<size_t>(itemSlotIndex) * 4;
                    if (offset + 4 > block.data.size())
                        break;

                    uint32_t value = 0; // empty slot
                    if (static_cast<size_t>(itemSlotIndex) < itemCount)
                    {
                        const InventoryItem &source = items[pouchIndex][itemSlotIndex];
                        InventoryItem7LGPE inventoryItem;
                        inventoryItem.itemId = source.itemId;
                        inventoryItem.count = source.count;
                        inventoryItem.isNew = source.isNew;
                        inventoryItem.isFavorite = source.isFavorite;
                        value = inventoryItem.toValue();
                    }
                    writeUInt32LittleEndian(&block.data[offset], value);
                }
            }

            logInfoToFile("Trainer7LGPE::updateItemBlock: item block serialized");
            break;
        }
    }

    std::vector<Save::Block> createBlocksFromSaveData7LGPE(const std::vector<uint8_t> &saveData)
    {
        /**
         * Let's Go uses fixed-offset blocks, not the SCBlocks of Gen 8+. Offsets verbatim from PKHeX
         * BelugaBlockIndex.cs:
         *   index 0  (MyItem)          @ 0x00000, length 0x00D90
         *   index 2  (MyStatus)        @ 0x01000, length 0x00168
         *   index 4  (Zukan)           @ 0x02A00, length 0x020E8
         *   index 5  (Misc)            @ 0x04C00, length 0x00930
         *   index 8  (PokeListHeader)  @ 0x05A00, length 0x00012  <- party header
         *   index 9  (PokeListPokemon) @ 0x05C00, length 0x3F7A0
         *   index 10 (PlayTime)        @ 0x45400, length 0x00008
         */

        std::vector<Save::Block> blocks;

        if (saveData.size() != SAVE_SIZE7_LGPE)
        {
            logErrorToFile("createBlocksFromSaveData7LGPE: Invalid save file size. Expected " + std::to_string(SAVE_SIZE7_LGPE) + " bytes, got " + std::to_string(saveData.size()) + " bytes.");
            return blocks;
        }

        // Define block info: {key, offset, size}
        struct BlockDef
        {
            size_t key;
            size_t offset;
            size_t size;
        };

        // Fixed-position blocks, offsets and lengths taken verbatim from PKHeX BelugaBlockIndex. The party
        // header (PokeListHeader) is index 8 @ 0x05A00, length 0x12.
        const BlockDef blockDefs[] = {
            {MY_ITEM7_LGPE, 0x00000, 0x00D90},           // index 0  MyItem
            {MY_STATUS7_LGPE, 0x01000, 0x00168},         // index 2  MyStatus
            {ZUKAN7_LGPE, 0x02A00, 0x020E8},             // index 4  Zukan (pokedex)
            {MISC7_LGPE, 0x04C00, 0x00930},              // index 5  Misc (money)
            {POKE_LIST_HEADER7_LGPE, 0x05A00, 0x00012},  // index 8  party header
            {POKE_LIST_POKEMON7_LGPE, 0x05C00, 0x3F7A0}, // index 9  storage (1000 x 260 bytes)
            {PLAY_TIME7_LGPE, 0x45400, 0x00008},         // index 10 PlayTime
        };

        for (const auto &def : blockDefs)
        {
            if (def.offset + def.size <= saveData.size())
            {
                Save::Block block;
                block.key = static_cast<uint32_t>(def.key);
                block.type = SCTypeCode::Array;
                block.data.assign(saveData.begin() + def.offset, saveData.begin() + def.offset + def.size);
                blocks.push_back(std::move(block));
            }
        }

        logInfoToFile("createBlocksFromSaveData7LGPE: Created " + std::to_string(blocks.size()) + " blocks from save data");

        return blocks;
    }

    namespace
    {
        // CRC-16/ARC (reflected poly 0xA001, init 0x0000, no final XOR) — the LGPE block
        // checksum (PKHeX Checksums.CRC16NoInvert / BlockInfo7b). Computed over [p, p+n).
        uint16_t crc16Arc(const uint8_t *bytes, size_t byteCount)
        {
            uint16_t checksum = 0x0000;
            for (size_t index = 0; index < byteCount; ++index)
            {
                checksum ^= bytes[index];
                for (int bitIndex = 0; bitIndex < 8; ++bitIndex)
                {
                    checksum = (checksum & 1) ? static_cast<uint16_t>((checksum >> 1) ^ 0xA001) : static_cast<uint16_t>(checksum >> 1);
                }
            }
            return checksum;
        }

        // Active-area byte offset for a Beluga block key (matches createBlocksFromSaveData7LGPE).
        size_t offsetForBlockKey(uint32_t key)
        {
            switch (key)
            {
            case MY_ITEM7_LGPE:
                return 0x00000;
            case MY_STATUS7_LGPE:
                return 0x01000;
            case ZUKAN7_LGPE:
                return 0x02A00;
            case MISC7_LGPE:
                return 0x04C00;
            case POKE_LIST_HEADER7_LGPE:
                return 0x05A00;
            case POKE_LIST_POKEMON7_LGPE:
                return 0x05C00;
            case PLAY_TIME7_LGPE:
                return 0x45400;
            default:
                return SIZE_MAX;
            }
        }
    }

    void writeBlocksToSaveData7LGPE(std::vector<uint8_t> &raw, const std::vector<Save::Block> &blocks)
    {
        // In the "BEEF" footer, block id N's 2-byte CRC lives at 0xB861A + N*8.
        // (footer base 0xB8600 + 0x14 header + id*8 + 6 for the checksum field.)
        constexpr size_t CHECKSUM_BASE = 0xB861A;

        for (const auto &block : blocks)
        {
            const size_t offset = offsetForBlockKey(block.key);
            if (offset == SIZE_MAX)
                continue;

            const size_t length = block.data.size();
            if (offset + length > raw.size())
                continue;

            // Patch the block bytes in place.
            std::memcpy(raw.data() + offset, block.data.data(), length);

            // Recompute this block's checksum into the footer. (Blocks we don't touch keep
            // their original bytes and valid checksums, so we only need to redo the ones here.)
            const uint16_t storedChecksum = crc16Arc(raw.data() + offset, length);
            const size_t chkOff = CHECKSUM_BASE + static_cast<size_t>(block.key) * 8;
            if (chkOff + 2 <= raw.size())
            {
                writeUInt16LittleEndian(raw.data() + chkOff, storedChecksum);
            }
        }
    }
}
