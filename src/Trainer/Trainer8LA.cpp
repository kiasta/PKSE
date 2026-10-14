#include <algorithm>
#include <cstring>

#include "Trainer/Trainer8LA.h"
#include "Pokemon/DexTable8LA.h"       // getStatisticsIndex8LA -- the species+form -> entry lookup
#include "Pokemon/PersonalInfoTable.h" // getPersonalInfo -> per-game presence
#include "Utils/Logger.h"

using namespace Utils;

namespace Trainer
{

    void Trainer8LA::parseBlock(const Block &block)
    {
        switch (block.key)
        {
        case MY_STATUS8_LA:
            parseMyStatusBlock(block);
            break;
        case PARTY8_LA:
            parsePartyBlock(block);
            break;
        case MONEY8_LA:
            parseMoneyBlock(block);
            break;
        case ITEM_REGULAR8_LA:
        case ITEM_KEY8_LA:
        case ITEM_STORED8_LA:
        case ITEM_RECIPE8_LA:
            parseItemBlock(block);
            break;
        case BOX8_LA:
            parseBoxBlock(block);
            break;
        case BOX_LAYOUT8_LA:
            parseBoxLayoutBlock(block);
            break;
        case CURRENT_BOX8_LA:
            parseCurrentBoxBlock(block);
            break;
        case SAVE_REVISION8_LA:
            parseSaveRevisionBlock(block);
            break;
        default:
            break;
        }
    }

    void Trainer8LA::parseMyStatusBlock(const Block &block)
    {
        /**
         * MyStatus8a Block Structure (Pokemon Legends: Arceus):
         * 0x10: ID32 (4 bytes) - TID16 (u16 @ 0x10) + SID16 (u16 @ 0x12)
         * 0x15: Trainer gender (1 byte)
         * 0x20: OT name (26 bytes / 13 UTF-16LE chars)
         *
         * ID32 format: SID16 << 16 | TID16
         * Display TID: ID32 % 1000000
         * Display SID: ID32 / 1000000
         */
        if (block.data.size() < 0x20 + 26)
        {
            logInfoToFile("Insufficient data in MY_STATUS block");
            return;
        }

        this->ID32 = readUInt32LittleEndian(&block.data[0x10]);
        this->TID16 = readUInt16LittleEndian(&block.data[0x10]);
        this->SID16 = readUInt16LittleEndian(&block.data[0x12]);
        this->TID = this->ID32 % 1000000;
        this->SID = this->ID32 / 1000000;
        size_t nameLength = 0x1A; // 26 bytes = 13 UTF-16LE chars
        auto nameSpan = std::span<const uint8_t>(block.data.data() + 0x20, nameLength);
        this->trainerName = utf16ToUtf8(getString(nameSpan.data(), nameLength));
        // The u32 at 0x3C identifies which of the eight preset characters the player chose at the start
        // of the game, and that choice is also what fixes the gender -- PLA has no independent gender
        // field. Bit 1 of the code is the gender (male codes 0,1,4,5 / female 2,3,6,7). Read from here
        // rather than the plain 0/1 byte at 0x15, which is the value PKHeX calls "Gender".
        if (block.data.size() >= 0x3C + 4)
        {
            const uint32_t code = readUInt32LittleEndian(&block.data[0x3C]);
            this->trainerGender = static_cast<uint8_t>((code >> 1) & 1);
        }
        logInfoToFile("Parsed Trainer Name", this->trainerName.c_str());
    }

    void Trainer8LA::parsePartyBlock(const Block &block)
    {
        /**
         * PARTY Block Structure (Pokemon Legends: Arceus):
         * Pokemon stored back-to-back with no inter-slot gap:
         * - Slot 0: offset 0 (SIZE_PARTY8_LA bytes)
         * - Slot 1: offset SIZE_PARTY8_LA
         * - Slot 2: offset 2 * SIZE_PARTY8_LA
         * - ... up to 6 slots
         *
         * Each slot is exactly SIZE_PARTY8_LA (0x178) bytes of Pokemon data,
         * packed with no inter-slot gap.
         */
        const std::span<const std::byte> blockSpan(reinterpret_cast<const std::byte *>(block.data.data()), block.data.size());

        for (size_t slot = 0; slot < MAX_PARTY_SLOTS; ++slot)
        {
            // Calculate offset to this slot (packed for LA)
            const size_t offset = slot * partySlotStride;
            if (offset + SIZE_PARTY8_LA > block.data.size())
                break;

            std::span<const std::byte> slotSpan = blockSpan.subspan(offset, SIZE_PARTY8_LA);

            bool isEmptySlot = true;
            for (size_t slotSpanIndex = 0; slotSpanIndex < SIZE_PARTY8_LA && slotSpanIndex < slotSpan.size(); ++slotSpanIndex)
            {
                if (slotSpan[slotSpanIndex] != std::byte{0})
                {
                    isEmptySlot = false;
                    break;
                }
            }

            if (!isEmptySlot)
            {
                // Decrypt and create Pokemon8LA object as unique_ptr
                // Pokemon8LA constructor handles decryption automatically
                party.push_back(std::make_unique<Pokemon8LA>(slotSpan));
            }
        }
    }

    void Trainer8LA::parseMoneyBlock(const Block &block)
    {
        /**
         * MONEY Block (Gen 8 Legends: Arceus):
         * For Gen 8 Legends: Arceus, the money value is stored directly as the block value.
         * 0x00: Money (4 bytes)
         */
        if (block.data.size() < 4)
        {
            return;
        }

        this->money = readUInt32LittleEndian(block.data.data());
    }

    void Trainer8LA::parseItemBlock(const Block &block)
    {
        // LA stores each pouch as a packed list of 4-byte entries { itemId u16 @0, count u16 @2 }.
        // Map this block's key to its pouch, then collect every non-empty (itemId != 0) entry.
        int pouch;
        switch (block.key)
        {
        case ITEM_REGULAR8_LA:
            pouch = static_cast<int>(PouchType8LA::Regular);
            break;
        case ITEM_KEY8_LA:
            pouch = static_cast<int>(PouchType8LA::KeyItems);
            break;
        case ITEM_STORED8_LA:
            pouch = static_cast<int>(PouchType8LA::Stored);
            break;
        case ITEM_RECIPE8_LA:
            pouch = static_cast<int>(PouchType8LA::Recipes);
            break;
        default:
            return;
        }

        if (items.size() < POUCH_COUNT8_LA)
            items.resize(POUCH_COUNT8_LA);
        items[pouch].clear();

        const size_t count = block.data.size() / ITEM_ENTRY_SIZE8_LA;
        for (size_t index = 0; index < count; ++index)
        {
            const size_t offset = index * ITEM_ENTRY_SIZE8_LA;
            const uint16_t itemId = readUInt16LittleEndian(&block.data[offset]);
            const uint16_t quantity = readUInt16LittleEndian(&block.data[offset + 2]);
            if (itemId != 0)
            {
                items[pouch].push_back(InventoryItem{itemId, quantity, false, false});
            }
        }
    }

    void Trainer8LA::parseBoxBlock(const Block &block)
    {
        /**
         * BOX Block Structure (Pokemon Legends: Arceus):
         * Pokemon stored back-to-back with no inter-slot gap:
         * - Box 0, Slot 0: offset 0 (SIZE_STORED8_LA bytes)
         * - Box 0, Slot 1: offset SIZE_STORED8_LA
         * - ... Box 0, Slot 29: offset 29 * SIZE_STORED8_LA
         * - Box 1, Slot 0: offset 30 * SIZE_STORED8_LA
         * - ... etc for all 32 boxes
         *
         * Each slot is exactly SIZE_STORED8_LA (0x168) bytes of Pokemon data,
         * packed with no inter-slot gap.
         *
         * Total size: 32 boxes * 30 slots * SIZE_STORED8_LA bytes
         */
        const std::span<const std::byte> blockSpan(reinterpret_cast<const std::byte *>(block.data.data()), block.data.size());

        for (size_t boxIndex = 0; boxIndex < BOX_COUNT8_LA; ++boxIndex)
        {
            for (size_t slot = 0; slot < BOX_SLOTS; ++slot)
            {
                // Calculate offset (packed for LA)
                const size_t offset = (boxIndex * BOX_SLOTS + slot) * boxSlotStride;
                if (offset + SIZE_STORED8_LA > block.data.size())
                {
                    break;
                }

                std::span<const std::byte> slotSpan = blockSpan.subspan(offset, SIZE_STORED8_LA);

                bool isEmptySlot = true;
                for (size_t slotSpanIndex = 0; slotSpanIndex < SIZE_STORED8_LA && slotSpanIndex < slotSpan.size(); ++slotSpanIndex)
                {
                    if (slotSpan[slotSpanIndex] != std::byte{0})
                    {
                        isEmptySlot = false;
                        break;
                    }
                }

                if (!isEmptySlot)
                {
                    boxes[boxIndex][slot] = std::make_unique<Pokemon8LA>(slotSpan);
                }
                else
                {
                    boxes[boxIndex][slot] = nullptr;
                }
            }
        }
    }

    void Trainer8LA::parseBoxLayoutBlock(const Block &block)
    {
        for (size_t boxIndex = 0; boxIndex < BOX_COUNT8_LA; ++boxIndex)
        {
            size_t offset = boxIndex * BOX_NAME_LENGTH8_LA;
            if (offset + BOX_NAME_LENGTH8_LA <= block.data.size())
            {
                std::u16string boxNameU16 = getString(block.data.data() + offset, BOX_NAME_LENGTH8_LA);
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

    void Trainer8LA::parseSaveRevisionBlock(const Block &)
    { /* LA save-revision detection deferred */
    }

    void Trainer8LA::updatePartyBlock()
    {
        /**
         * Updates the PARTY block with modified Pokemon data (Legends: Arceus / PA8).
         *
         * PARTY slots are PARTY-size (SIZE_PARTY8_LA = 0x178): the full 0x168 stored region plus the
         * 0x10-byte party-stats tail. Each occupied slot gets the full 0x178 encrypted bytes.
         *
         * Empty party slots must be the game's encrypted "blank" (which DECRYPTS to species 0), NOT
         * literal zeros -- exactly like updateBoxBlock(). The game decrypts every slot and validates it,
         * so a zeroed slot decrypts to garbage and renders a BAD EGG. The party is re-serialized on
         * EVERY save, so a memset(0) here corrupts the party even when the edit only touched a box name.
         *
         * The PartyCount byte at 6*0x178 is deliberately left untouched: empty slots load as species-0
         * "ghosts" that inflate party.size(), so the save's own count is authoritative, not
         * party.size().
         */
        for (auto &block : blocks)
        {
            if (block.key == PARTY8_LA)
            {
                size_t requiredSize = MAX_PARTY_SLOTS * partySlotStride;
                if (block.data.size() < requiredSize)
                {
                    block.data.resize(requiredSize, 0);
                }

                // Synthesize a party-size (0x178) encrypted blank that decrypts to species 0:
                // an all-zero PA8 encrypted with EC 0 (== PKHeX's new PA8() BlankPKM).
                std::vector<uint8_t> blankSlot;
                {
                    std::vector<std::byte> zero(SIZE_PARTY8_LA, std::byte{0});
                    std::byte *encryptedRecord = encryptArray8LA(std::span<const std::byte>(zero.data(), SIZE_PARTY8_LA), 0);
                    blankSlot.assign(reinterpret_cast<const uint8_t *>(encryptedRecord), reinterpret_cast<const uint8_t *>(encryptedRecord) + SIZE_PARTY8_LA);
                    blankSlot.resize(partySlotStride, 0); // 0x178 (no gap for LA)
                    delete[] encryptedRecord;
                }

                for (size_t partySlotIndex = 0; partySlotIndex < party.size() && partySlotIndex < MAX_PARTY_SLOTS; ++partySlotIndex)
                {
                    // Calculate offset (packed for LA: stride == SIZE_PARTY8_LA)
                    const size_t offset = partySlotIndex * partySlotStride;

                    if (party[partySlotIndex] && party[partySlotIndex]->speciesID() != 0)
                    {
                        const ::Pokemon::Pokemon *pokemon = party[partySlotIndex].get();
                        uint32_t encryptionConstant = readUInt32LittleEndian(reinterpret_cast<const uint8_t *>(pokemon->getData().data()));

                        std::span<const std::byte> decryptedSpan(pokemon->getData().data(), pokemon->getDataSize());

                        std::byte *encryptedData = encryptArray8LA(decryptedSpan, encryptionConstant);

                        // Write the FULL party-size (0x178) encrypted bytes into the slot
                        std::memcpy(&block.data[offset], encryptedData, pokemon->getDataSize());

                        // Zero out the gap after the Pokemon data (0 bytes for LA's packed layout)
                        std::memset(&block.data[offset + SIZE_PARTY8_LA], 0, slotGapZero);

                        delete[] encryptedData;
                    }
                    else
                    {
                        // Empty slot: write the encrypted blank, NOT zeros (see the header note).
                        std::memcpy(&block.data[offset], blankSlot.data(), partySlotStride);
                    }
                }

                // Remaining trailing slots are empty -> encrypted blank, not zeros.
                for (size_t partySlotIndex = party.size(); partySlotIndex < MAX_PARTY_SLOTS; ++partySlotIndex)
                {
                    const size_t offset = partySlotIndex * partySlotStride;
                    std::memcpy(&block.data[offset], blankSlot.data(), partySlotStride);
                }

                break;
            }
        }
    }

    void Trainer8LA::parseCurrentBoxBlock(const Block &block)
    {
        // "U8 Box Index" -- a scalar block PKHeX reads as a byte (0..31 fits one byte).
        if (block.data.empty())
            return;
        uint8_t box = block.data[0];
        if (box < BOX_COUNT8_LA)
            this->currentBox = box;
    }

    void Trainer8LA::updateCurrentBoxBlock()
    {
        // Inverse of parseCurrentBoxBlock: write the low byte and clear the rest of the scalar.
        for (auto &block : blocks)
        {
            if (block.key != CURRENT_BOX8_LA || block.data.empty())
                continue;
            block.data[0] = static_cast<uint8_t>(currentBox);
            for (size_t moveIndex = 1; moveIndex < block.data.size() && moveIndex < 4; ++moveIndex)
                block.data[moveIndex] = 0;
            break;
        }
    }

    void Trainer8LA::updateBoxNameBlock()
    {
        // Inverse of parseBoxLayoutBlock. See Trainer8SWSH::updateBoxNameBlock for why the block is
        // bounds-checked rather than resized.
        for (auto &block : blocks)
        {
            if (block.key != BOX_LAYOUT8_LA)
                continue;
            for (size_t boxIndex = 0; boxIndex < BOX_COUNT8_LA && boxIndex < boxNames.size(); ++boxIndex)
            {
                // never persist a display default
                if (!isBoxNameDirty(boxIndex)) continue;
                const size_t offset = boxIndex * BOX_NAME_LENGTH8_LA;
                if (offset + BOX_NAME_LENGTH8_LA > block.data.size())
                    break;
                setString(block.data.data() + offset, BOX_NAME_LENGTH8_LA, utf8ToUtf16(boxNames[boxIndex]), BOX_NAME_LENGTH8_LA / 2 - 1);
            }
            break;
        }
    }

    void Trainer8LA::updateBoxBlock()
    {
        /**
         * Updates the BOX block with modified Pokemon data (Legends: Arceus / PA8).
         *
         * CRITICAL DIVERGENCE from the party block: BOX slots are STORED-size
         * (SIZE_STORED8_LA = 0x168) — they have NO party-stats region. For each occupied
         * slot we still encrypt the pokemon's FULL party-size buffer (0x178) via encryptArray8LA
         * (crypto shuffles/XORs the 0x08..0x168 blocks; the 0x10 party tail is irrelevant
         * to a box slot), then copy ONLY the first 0x168 encrypted bytes into the slot.
         *
         * Empty box slots must be the game's encrypted "blank" (which DECRYPTS to species 0),
         * NOT literal zeros: the game decrypts every box slot and checks species, so a zeroed
         * slot decrypts to garbage and renders a BAD EGG. Reuse the game's own blank by copying
         * an existing empty (species-0) slot; if every box is full, synthesize one from an
         * all-zero PA8 encrypted with EC 0 and take its first 0x168 bytes.
         */
        for (auto &block : blocks)
        {
            if (block.key == BOX8_LA)
            {
                size_t requiredSize = BOX_COUNT8_LA * BOX_SLOTS * boxSlotStride;
                if (block.data.size() < requiredSize)
                {
                    block.data.resize(requiredSize, 0);
                }

                // Capture a reference "blank" slot (0x168 bytes) that decrypts to species 0.
                std::vector<uint8_t> blankSlot;
                for (size_t boxIndex = 0; boxIndex < BOX_COUNT8_LA && blankSlot.empty(); ++boxIndex)
                {
                    for (size_t slotIndex = 0; slotIndex < BOX_SLOTS; ++slotIndex)
                    {
                        if (boxes[boxIndex][slotIndex] && boxes[boxIndex][slotIndex]->speciesID() == 0)
                        {
                            const size_t byteOffset = (boxIndex * BOX_SLOTS + slotIndex) * boxSlotStride;
                            if (byteOffset + boxSlotStride <= block.data.size())
                            {
                                blankSlot.assign(block.data.begin() + byteOffset, block.data.begin() + byteOffset + boxSlotStride);
                                break;
                            }
                        }
                    }
                }
                if (blankSlot.empty())
                {
                    // Synthesize: encrypt an all-zero PA8 (party size) with EC 0, then take the
                    // first SIZE_STORED8_LA (0x168) bytes — a stored-size blank that decrypts to species 0.
                    std::vector<std::byte> zero(SIZE_PARTY8_LA, std::byte{0});
                    std::byte *encryptedRecord = encryptArray8LA(std::span<const std::byte>(zero.data(), SIZE_PARTY8_LA), 0);
                    blankSlot.assign(reinterpret_cast<const uint8_t *>(encryptedRecord), reinterpret_cast<const uint8_t *>(encryptedRecord) + SIZE_STORED8_LA);
                    blankSlot.resize(boxSlotStride, 0); // exactly 0x168 for LA's packed layout
                    delete[] encryptedRecord;
                }

                for (size_t boxIndex = 0; boxIndex < BOX_COUNT8_LA; ++boxIndex)
                {
                    for (size_t slot = 0; slot < BOX_SLOTS; ++slot)
                    {
                        // Calculate offset (packed for LA: stride == SIZE_STORED8_LA = 0x168)
                        const size_t offset = (boxIndex * BOX_SLOTS + slot) * boxSlotStride;

                        // Gate on species, not just the pointer (ghost-slot fix). A slot left holding a
                        // non-null but blank/species-0 pokemon (e.g. after a bank move) must read as EMPTY
                        // in-game — re-encrypting a blank writes a bad egg.
                        if (boxes[boxIndex][slot] && boxes[boxIndex][slot]->speciesID() != 0)
                        {
                            const auto &pokemon = boxes[boxIndex][slot];

                            uint32_t encryptionConstant = readUInt32LittleEndian(reinterpret_cast<const uint8_t *>(pokemon->getData().data()));

                            std::span<const std::byte> decryptedSpan(pokemon->getData().data(), pokemon->getDataSize());

                            std::byte *encryptedData = encryptArray8LA(decryptedSpan, encryptionConstant);

                            // Box slots are STORED-size: copy ONLY the first 0x168 encrypted bytes.
                            // (No party-stats region and no inter-slot gap for a box slot.)
                            std::memcpy(&block.data[offset], encryptedData, SIZE_STORED8_LA);

                            delete[] encryptedData;
                        }
                        else
                        {
                            // Empty/cleared slot: write the game's encrypted blank (0x168), NOT zeros.
                            // Zeros decrypt to garbage in-game and show as a BAD EGG in every empty slot.
                            std::memcpy(&block.data[offset], blankSlot.data(), boxSlotStride);
                        }
                    }
                }
                break;
            }
        }
    }

    std::unique_ptr<::Pokemon::Pokemon> Trainer8LA::createBlankPokemon() const
    {
        // Mirror updateBoxBlock()'s encrypted-blank synth: a zeroed *decrypted* party-size PA8
        // buffer (0x178) encrypted with EC/seed 0. LA entities keep a party-size buffer (box slots
        // are stored-size 0x168, party slots 0x178), so construct from the FULL party-size encrypted
        // span (matches the party read). The ctor decrypts it straight back to zeros -> a clean
        // species-0, checksum-valid entity. Raw zeros would decrypt to garbage (BAD EGG).
        std::vector<std::byte> zero(SIZE_PARTY8_LA, std::byte{0});
        std::byte *encryptedRecord = encryptArray8LA(std::span<const std::byte>(zero.data(), SIZE_PARTY8_LA), 0);
        auto clone = std::make_unique<Pokemon8LA>(std::span<const std::byte>(encryptedRecord, SIZE_PARTY8_LA));
        delete[] encryptedRecord;
        return clone;
    }

    //
    // Legends: Arceus is the outlier of the seven. There is no seen/caught bitfield: the dex is a
    // RESEARCH LOG, one 0x1E460 block split in two, and the two halves are indexed differently.
    //
    //   0x00000  global data (0x10)                    0x00010  five 0x10 per-dex local records
    //   0x00070  research entries -- 0x58 each, indexed straight by SPECIES (981 of them)
    //   0x151A8  statistics entries -- 0x18 each, 1480 slots, indexed by a LOOKUP TABLE keyed on
    //            (species | form<<11). Not by species, not by form -- see PLADexTable.
    //
    // Research entry (per species), the fields this writes:
    //   0x00 u32 flags -- bit0 has-ever-been-updated, bit1 has-any-report, bit2 perfect,
    //                     bit3 selected-gender-is-female, bit4 selected-shiny, bit5 selected-alpha
    //   0x0A u16 ObtainedCount   0x50 u8 selected form
    //
    // Statistics entry (per species+form):
    //   0x00 u32 flags (bit0 = has max size records)   0x04 u8 seen-in-wild   0x05 u8 obtained
    //   0x06 u8 caught-in-wild   0x08/0x0C/0x10/0x14 f32 min/max height, min/max weight
    //
    // The three flag BYTES are indexed by a variant shift, not by form: shiny +4, alpha +2, female +1.
    // So one byte covers all eight combinations of the three.
    namespace
    {
        constexpr size_t PLA_RESEARCH_BASE = 0x70;
        constexpr size_t PLA_RESEARCH_SIZE = 0x58;
        constexpr size_t PLA_STATS_BASE = 0x151A8;
        constexpr size_t PLA_STATS_SIZE = 0x18;
        constexpr size_t PLA_BLOCK_SIZE = 0x1E460;
        constexpr uint16_t PLA_MAX_RESEARCH_POINTS = 60000; // PokedexConstants8a

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
        inline float rdF32(const std::vector<uint8_t> &d, size_t offset)
        {
            float floatValue;
            std::memcpy(&floatValue, &d[offset], sizeof floatValue);
            return floatValue;
        }
        inline void wrF32(std::vector<uint8_t> &d, size_t offset, float floatValue)
        {
            std::memcpy(&d[offset], &floatValue, sizeof floatValue);
        }
    }

    void Trainer8LA::updatePokedexBlock()
    {
        std::vector<uint8_t> *dex = nullptr;
        for (auto &block : blocks)
        {
            if (block.key == POKEDEX8_LA)
            {
                dex = &block.data;
                break;
            }
        }
        if (!dex || dex->size() < PLA_BLOCK_SIZE)
        {
            if (dex)
                logErrorToFile("Pokedex: PLA Zukan block is short; skipped", ("size=" + std::to_string(dex->size()) + " expected=" + std::to_string(PLA_BLOCK_SIZE)).c_str());
            return;
        }
        size_t skippedNoEntry = 0;

        auto registerMon = [&](const ::Pokemon::Pokemon *pokemon)
        {
            if (!pokemon || pokemon->isEgg())
                return;
            const uint16_t species = pokemon->speciesID();
            if (species == 0 || species > ::Pokemon::DEX8LA_MAX_SPECIES)
                return;
            const uint8_t form = pokemon->form();
            const ::Pokemon::PersonalInfo &pi = ::Pokemon::getPersonalInfo(species, form);
            // not in this game
            if ((pi.presence & ::Pokemon::PERSONAL_GAME_PLA) == 0) return;

            // Statistics entries are reached through the lookup, and plenty of species+form pairs
            // simply have none -- that is a legitimate "no dex slot", not an error.
            const uint16_t statIdx = ::Pokemon::getStatisticsIndex8LA(species, form);
            if (statIdx == ::Pokemon::DEX8LA_NO_STAT_ENTRY)
            {
                ++skippedNoEntry;
                return;
            }

            const size_t research = PLA_RESEARCH_BASE + static_cast<size_t>(species) * PLA_RESEARCH_SIZE;
            const size_t stats = PLA_STATS_BASE + static_cast<size_t>(statIdx) * PLA_STATS_SIZE;

            const bool alpha = (pokemon->getGameGroup() == Enums::GameVersion::PLA) && static_cast<const Pokemon8LA *>(pokemon)->isAlpha();
            const bool shiny = pokemon->isShiny(pokemon->id32(), pokemon->species());
            const uint8_t gender = pokemon->gender(); // 0 male, 1 female, 2 genderless
            // Variant shift: shiny +4, alpha +2, female +1. Genderless does NOT shift (PKHeX masks
            // the gender with ~2, so only the female bit counts).
            const int shift = (shiny ? 4 : 0) + (alpha ? 2 : 0) + (((gender & ~2) != 0) ? 1 : 0);
            const uint8_t bit = static_cast<uint8_t>(1u << shift);

            // Size records, BEFORE the obtain bit goes in -- the "have we obtained one before?" test
            // reads the very flags we are about to set. Alphas are excluded: they are fixed at maximum
            // size, so letting one in would wreck the species' real min/max. Mask 0x33 is the four
            // non-alpha slots (shift 0,1,4,5). PKHeX SetPokeObtained.
            if (!alpha)
            {
                const float heightValue = (pokemon->getGameGroup() == Enums::GameVersion::PLA) ? static_cast<const Pokemon8LA *>(pokemon)->heightAbsolute() : 0.0f;
                const float weightValue = (pokemon->getGameGroup() == Enums::GameVersion::PLA) ? static_cast<const Pokemon8LA *>(pokemon)->weightAbsolute() : 0.0f;
                if (heightValue > 0.0f && weightValue > 0.0f)
                {
                    const bool hadNonAlpha = ((*dex)[stats + 0x05] & 0x33) != 0;
                    if (hadNonAlpha)
                    {
                        const bool hasMax = (rdU32(*dex, stats + 0x00) & 0x01) != 0;
                        const float baseH = hasMax ? rdF32(*dex, stats + 0x0C) : rdF32(*dex, stats + 0x08);
                        const float baseW = hasMax ? rdF32(*dex, stats + 0x14) : rdF32(*dex, stats + 0x10);
                        wrF32(*dex, stats + 0x0C, heightValue > baseH ? heightValue : baseH);
                        wrF32(*dex, stats + 0x14, weightValue > baseW ? weightValue : baseW);
                        if (!hasMax)
                            wrU32(*dex, stats + 0x00, rdU32(*dex, stats + 0x00) | 0x01);
                        const float minH = rdF32(*dex, stats + 0x08), minW = rdF32(*dex, stats + 0x10);
                        wrF32(*dex, stats + 0x08, heightValue < minH ? heightValue : minH);
                        wrF32(*dex, stats + 0x10, weightValue < minW ? weightValue : minW);
                    }
                    else
                    {
                        wrF32(*dex, stats + 0x08, heightValue); // first of its kind -> seeds the minimum
                        wrF32(*dex, stats + 0x10, weightValue);
                    }
                }
            }

            // "Is this species new?" has to be read before ObtainedCount is touched, for the same reason.
            const bool wasNew = rdU16(*dex, research + 0x0A) == 0;

            (*dex)[stats + 0x04] |= bit; // seen in the wild
            (*dex)[stats + 0x05] |= bit; // obtained
            // 0x06 caught-in-wild is deliberately NOT set: a pokemon that arrived by transfer or was made
            // in the editor was never caught in this game's wild, and that flag feeds catch research.

            // Research entry: bump the obtained counter (the "catch N of these" task) and mark the
            // species touched. has-any-report (bit1) is deliberately left alone -- that means the
            // player has REPORTED to Laventon, which has not happened; the game grants it when they do.
            wrU32(*dex, research + 0x00, rdU32(*dex, research + 0x00) | 0x01); // has-ever-been-updated
            const uint32_t obtained = rdU16(*dex, research + 0x0A) + 1u;
            wrU16(*dex, research + 0x0A, static_cast<uint16_t>(obtained > PLA_MAX_RESEARCH_POINTS ? PLA_MAX_RESEARCH_POINTS : obtained));

            // Selected variant -- what the dex page shows for this species. Only for a species that had
            // none, so an entry the player already had keeps what it was showing.
            if (wasNew)
            {
                (*dex)[research + 0x50] = form;
                uint32_t flags = rdU32(*dex, research + 0x00) & ~0x38u; // clear gender/shiny/alpha
                // The female bit only means anything where the dex keeps the two sexes as separate
                // models. Where it does not, male and female share one record and the bit would claim
                // a variant the page cannot show (PKHeX reads the same PokemonInfoGenders bit 3).
                if (gender == 1 && ::Pokemon::tracksGenderSeparately8LA(species, form))
                    flags |= 0x08;
                if (shiny)
                    flags |= 0x10;
                if (alpha)
                    flags |= 0x20;
                wrU32(*dex, research + 0x00, flags);
            }
        };

        for (const auto &pokemon : party)
            registerMon(pokemon.get());
        for (const auto &box : boxes)
            for (const auto &pokemon : box)
                registerMon(pokemon.get());

        // Not an error -- many species+form pairs genuinely have no statistics slot -- but a sudden
        // jump here would mean the lookup table is wrong, and silence is how that hides.
        if (skippedNoEntry != 0)
        {
            logInfoToFile("Pokedex: species+form with no PLA statistics entry", (std::to_string(skippedNoEntry) + " skipped").c_str());
        }
    }

    void Trainer8LA::updateTrainerInfoBlock()
    {
        // Write money / OT name back to the blocks parse reads them from. encrypt() re-hashes.
        for (auto &block : blocks)
        {
            if (block.key == MY_STATUS8_LA)
            {
                if (block.data.size() >= 0x20 + 0x1A)
                    setString(&block.data[0x20], 0x1A, utf8ToUtf16(trainerName), 12);
            }
            else if (block.key == MONEY8_LA)
            {
                // MONEY8_LA is a u32 scalar block
                if (block.data.size() >= 4) writeUInt32LittleEndian(block.data.data(), money);
            }
        }
    }

    void Trainer8LA::updateItemBlock()
    {
        // Rewrite each pouch block in place from PKSE's parsed list. Safe to rebuild: we parse EVERY
        // non-zero entry (no unknown items to lose, unlike the indexed Gen 9 pouches). Zero the block,
        // then write the entries compacted to the front — trailing zeros are empty slots. The block
        // size is preserved (the game reads the full capacity and ignores itemId==0 entries).
        struct PouchBlock
        {
            size_t key;
            PouchType8LA pouch;
        };
        const PouchBlock pouches[] = {
            {ITEM_REGULAR8_LA, PouchType8LA::Regular},
            {ITEM_KEY8_LA, PouchType8LA::KeyItems},
            {ITEM_STORED8_LA, PouchType8LA::Stored},
            {ITEM_RECIPE8_LA, PouchType8LA::Recipes},
        };
        for (const auto &pb : pouches)
        {
            const size_t index = static_cast<size_t>(pb.pouch);
            if (index >= items.size())
                continue;
            for (auto &block : blocks)
            {
                if (block.key != pb.key)
                    continue;
                std::memset(block.data.data(), 0, block.data.size());
                const size_t capacity = block.data.size() / ITEM_ENTRY_SIZE8_LA;
                size_t slot = 0;
                for (const auto &item : items[index])
                {
                    if (slot >= capacity)
                        break;
                    const size_t offset = slot * ITEM_ENTRY_SIZE8_LA;
                    block.data[offset] = static_cast<uint8_t>(item.itemId & 0xFF);
                    block.data[offset + 1] = static_cast<uint8_t>((item.itemId >> 8) & 0xFF);
                    block.data[offset + 2] = static_cast<uint8_t>(item.count & 0xFF);
                    block.data[offset + 3] = static_cast<uint8_t>((item.count >> 8) & 0xFF);
                    ++slot;
                }
                break;
            }
        }
    }

    // Per-pouch capacity, matching PKHeX PlayerBag8a. The three specialty pouches are fixed; the
    // general Items bag grows with the player's Satchel Upgrades (0-39): min(675, upgrades + 20).
    // updateItemBlock already clamps writes to the block's own size, so this only gates the UI's
    // add-item flow -- it can't overflow anything.
    size_t Trainer8LA::getItemPouchCapacity(int pouch) const
    {
        switch (static_cast<PouchType8LA>(pouch))
        {
        case PouchType8LA::KeyItems:
            return POUCH_CAP_KEY8_LA; // 100
        case PouchType8LA::Stored:
            return POUCH_CAP_STORED8_LA; // 180
        case PouchType8LA::Recipes:
            return POUCH_CAP_RECIPE8_LA; // 70
        case PouchType8LA::Regular:
        {
            uint32_t upgrades = 0;
            for (const auto &b : blocks)
            {
                if (b.key == SATCHEL_UPGRADES8_LA)
                {
                    if (b.data.size() >= 4)
                        upgrades = readUInt32LittleEndian(b.data.data());
                    break;
                }
            }
            const uint32_t capacity = upgrades + 20;
            return capacity < POUCH_CAP_REGULAR_MAX8_LA ? capacity : POUCH_CAP_REGULAR_MAX8_LA;
        }
        }
        return 0;
    }
}
