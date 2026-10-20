#include "Trainer/Trainer1RBY.h"

#include <algorithm>
#include <cstring>

#include "Pokemon/Gen1Tables.h"
#include "Utils/Gen1Text.h"
#include "Utils/StringHelpers.h"
#include "Utils/Logger.h"
#include "Globals.h"

using namespace Utils;

namespace Trainer
{

    namespace
    {
        /// A Gen 1/2 Pokemon list is well-formed when its count fits and the marker right after
        /// the last present slot is the 0xFF cap. PKHeX's IsListValidG12 -- the only handle the
        /// format gives for telling an international save from a Japanese one.
        bool listLooksValid(std::span<const uint8_t> bytes, size_t offset, uint8_t maxCount) noexcept
        {
            if (offset + 1 + maxCount + 1 >= bytes.size())
                return false;
            const uint8_t count = bytes[offset];
            if (count > maxCount)
                return false;
            return bytes[offset + 1 + count] == 0xFF;
        }

        /// 3-byte big-endian BCD -> integer. Gen 1 stores money and coins this way; reading the
        /// bytes as a plain integer gives a number that looks right in hex and is wrong in decimal.
        uint32_t readBCD(const uint8_t *bytes, size_t byteCount) noexcept
        {
            uint32_t value = 0;
            for (size_t index = 0; index < byteCount; ++index)
            {
                value = value * 100 + static_cast<uint32_t>((bytes[index] >> 4) * 10 + (bytes[index] & 0x0F));
            }
            return value;
        }

        void writeBCD(uint8_t *bytes, size_t byteCount, uint32_t value) noexcept
        {
            for (size_t byteIndex = byteCount; byteIndex-- > 0;)
            {
                const uint32_t byteVal = value % 100;
                bytes[byteIndex] = static_cast<uint8_t>(((byteVal / 10) << 4) | (byteVal % 10));
                value /= 100;
            }
        }

        std::string decodeName(const uint8_t *bytes, size_t maxBytes, bool isJapanese)
        {
            std::u16string text;
            for (size_t index = 0; index < maxBytes; ++index)
            {
                const char16_t character = Utils::gen1ToChar(bytes[index], isJapanese);
                if (character == 0)
                    break; // Gen 1 STOPS at a glyphless byte; it does not skip
                text += character;
            }
            return Utils::utf16ToUtf8(text);
        }

        /// Encode a name, terminate it, and leave the bytes past the terminator ALONE -- they are
        /// trash the games never cleared, and rewriting them makes an untouched save read as edited.
        void encodeName(uint8_t *bytes, size_t fieldBytes, const std::string &utf8, bool isJapanese)
        {
            const std::u16string value = Utils::utf8ToUtf16(utf8);
            size_t byteCount = 0;
            for (const char16_t character : value)
            {
                if (byteCount + 1 >= fieldBytes)
                    break;
                const uint8_t packedByte = Utils::charToGen1(character, isJapanese);
                if (packedByte == Utils::GEN1_TERMINATOR)
                    break;
                bytes[byteCount++] = packedByte;
            }
            if (byteCount < fieldBytes)
                bytes[byteCount] = Utils::GEN1_TERMINATOR;
        }
    }

    bool Trainer1RBY::detect(std::span<const uint8_t> data, bool &outJapanese) noexcept
    {
        if (data.size() != RBY_SAVE_SIZE)
            return false;
        // Japanese first, matching PKHeX. The two layouts test different offsets, so this is not
        // an ordering hack -- a save that satisfies the Japanese test is Japanese.
        if (listLooksValid(data, 0x2ED5, 30) && listLooksValid(data, 0x302D, 30))
        {
            outJapanese = true;
            return true;
        }
        if (listLooksValid(data, 0x2F2C, 20) && listLooksValid(data, 0x30C0, 20))
        {
            outJapanese = false;
            return true;
        }
        return false;
    }

    Trainer1RBY::Trainer1RBY(std::vector<uint8_t> data, std::string fileName) : Trainer(std::vector<Block>{}), saveData(std::move(data)), saveFileName(std::move(fileName))
    {
        if (!detect(saveData, saveIsJapanese))
        {
            logErrorToFile("Trainer1RBY: not a Gen 1 save", saveFileName.c_str());
            boxes.resize(getBoxCount());
            return;
        }
        offsets = saveIsJapanese ? RBYOffsets::japanese() : RBYOffsets::international();
        // Yellow has no version byte either. PKHeX infers it: the starter field holds Pikachu's
        // id, or -- before a starter is chosen -- the Pikachu-friendship byte is non-zero.
        const uint8_t starter = saveData[offsets.starter];
        saveIsYellow = starter != 0 ? (starter == 0x54) : (saveData[offsets.pikaFriendship] != 0);
        valid = true;

        parseTrainer();
        parseParty();
        parseBoxes();
        parseItems();

        if (g_debugLogging)
        {
            logInfoToFile("Gen 1 save loaded", (saveFileName + " " + (saveIsJapanese ? "JP" : "INT") + " " + gameTitle() + " OT=" + trainerName + " ID=" + std::to_string(TID16) + " party=" + std::to_string(party.size()) + " boxes=" + std::to_string(getBoxCount()) + "x" + std::to_string(getSlotsPerBox())).c_str());
        }
    }

    const char *Trainer1RBY::gameTitle() const noexcept
    {
        if (saveIsYellow)
            return "Yellow";
        // Japan's third title was Green; JP Blue is a later re-release. Calling the Japanese trio
        // "Red/Blue" would omit the game a Japanese save is most likely to actually be, which is
        // the same failure as naming an id from the wrong table -- plausible, and wrong.
        return saveIsJapanese ? "Red/Green/Blue" : "Red/Blue";
    }

    void Trainer1RBY::parseTrainer()
    {
        const size_t nameBytes = getMaxTrainerNameLength() + 1; // field is one longer than the cap
        trainerName = decodeName(&saveData[RBYOffsets::otNameOffset], nameBytes, saveIsJapanese);
        rivalName = decodeName(&saveData[offsets.rival], nameBytes, saveIsJapanese);

        // TID is BIG ENDIAN, and Gen 1 has no secret id at all -- ID32 is the TID alone, not a
        // 32-bit value with a zero half that later code might try to split.
        TID16 = static_cast<uint16_t>((saveData[offsets.tid16] << 8) | saveData[offsets.tid16 + 1]);
        SID16 = 0;
        ID32 = TID16;
        TID = TID16;
        SID = 0;
        trainerGender = 0; // Gen 1 has no player gender; Red/Blue/Yellow are male-only

        money = readBCD(&saveData[offsets.money], 3);
        coins = static_cast<uint16_t>(readBCD(&saveData[offsets.coin], 2));
        badgeFlags = saveData[offsets.badges];

        playedHours = saveData[offsets.playTime + 0];
        playedMinutes = saveData[offsets.playTime + 2];
        playedSeconds = saveData[offsets.playTime + 3];

        // Dex counts: 19 bytes of bits each, species 1..151 at bit (species-1).
        auto popcount19 = [&](uint16_t base)
        {
            uint16_t count = 0;
            for (uint16_t internalSpeciesId = 1; internalSpeciesId <= ::Pokemon::MAX_SPECIES_GEN1; ++internalSpeciesId)
            {
                const uint16_t bit = static_cast<uint16_t>(internalSpeciesId - 1);
                if ((saveData[base + (bit >> 3)] >> (bit & 7)) & 1)
                    ++count;
            }
            return count;
        };
        dexOwned = popcount19(offsets.dexCaught);
        dexSeen = popcount19(offsets.dexSeen);

        currentBox = currentBoxIndex();
        saveRevision = 0;
        // NOT the game name. saveRevisionString is the save's revision/DLC level (SwSh's Isle of
        // Armor, S/V's Teal Mask) and the title bar appends it in parentheses -- so putting the
        // version here rendered as "Pokemon Red/Blue  (Red/Blue)". Gen 1 has no revision at all.
        saveRevisionString = "Base";
        gameVersionString = saveIsJapanese ? "Gen 1 (JP)" : "Gen 1";
    }

    std::unique_ptr<::Pokemon::Pokemon> Trainer1RBY::readListEntry(size_t listOffset, size_t capacity, bool isParty, size_t index) const
    {
        const size_t slotIndex = strLen();
        const size_t bodyLen = isParty ? ::Pokemon::SIZE_1PARTY : ::Pokemon::SIZE_1STORED;
        const size_t start = 1 + capacity + 1;
        const size_t ofsBody = listOffset + start + bodyLen * index;
        const size_t ofsOT = listOffset + start + bodyLen * capacity + slotIndex * index;
        const size_t ofsNick = ofsOT + slotIndex * capacity;
        if (ofsNick + slotIndex > saveData.size())
            return nullptr;

        // A POKELIST ENDS AT ITS COUNT, NOT AT ITS MARKERS -- the games and PKHeX's PokeList1 both
        // read `count` entries and nothing further. writeList() relies on exactly that: it writes
        // the count and ONE 0xFF cap and leaves every marker after the cap as the game left it,
        // which is what keeps an unedited save byte-identical. Reading by marker alone therefore
        // brought back whatever sits past the cap: Pokemon the player released or moved long ago,
        // and in a box bank the game never initialised, leftover memory. A real Blue save wrote
        // back and re-read with ELEVEN Pokemon that were not in its boxes -- a Mew and a box of
        // Pidgey and Rattata. Gen 2's readList has always stopped at the count.
        const size_t listCount = std::min<size_t>(saveData[listOffset], capacity);
        if (index >= listCount)
            return nullptr;

        const uint8_t marker = saveData[listOffset + 1 + index];
        if (marker == 0 || marker == 0xFF)
            return nullptr; // empty slot

        // Build the single-entry list Pokemon1RBY expects. A BOX body is 33 bytes and is copied
        // into a 44-byte party body -- the extra 11 (level + battle stats) are what the game
        // recomputes on withdrawal, which recalculateStats() below does for us.
        std::vector<std::byte> rec(saveIsJapanese ? ::Pokemon::SIZE_1JLIST : ::Pokemon::SIZE_1ULIST, std::byte{0});
        rec[0] = std::byte{1};
        rec[1] = std::byte{marker};
        rec[2] = std::byte{0xFF};
        std::memcpy(rec.data() + 3, &saveData[ofsBody], bodyLen);
        std::memcpy(rec.data() + 3 + ::Pokemon::SIZE_1PARTY, &saveData[ofsOT], slotIndex);
        std::memcpy(rec.data() + 3 + ::Pokemon::SIZE_1PARTY + slotIndex, &saveData[ofsNick], slotIndex);

        auto pokemon = std::make_unique<::Pokemon::Pokemon1RBY>(std::span<const std::byte>(rec.data(), rec.size()));
        if (!pokemon->isValid())
            return nullptr;
        if (!isParty)
            pokemon->recalculateStats(); // box records carry no stats; the game recomputes them
        return pokemon;
    }

    void Trainer1RBY::writeList(size_t listOffset, size_t capacity, bool isParty, const std::vector<::Pokemon::Pokemon *> &mons)
    {
        const size_t slotIndex = strLen();
        const size_t bodyLen = isParty ? ::Pokemon::SIZE_1PARTY : ::Pokemon::SIZE_1STORED;
        const size_t start = 1 + capacity + 1;
        const size_t total = start + (bodyLen + slotIndex + slotIndex) * capacity;
        if (listOffset + total > saveData.size())
            return;

        size_t byteCount = 0;
        for (size_t monIndex = 0; monIndex < capacity && monIndex < mons.size(); ++monIndex)
        {
            const auto *source = mons[monIndex];
            if (!source || source->speciesID() == 0)
                continue;
            const auto *pokemon = static_cast<const ::Pokemon::Pokemon1RBY *>(source);
            const auto raw = pokemon->getData();
            const size_t ofsBody = listOffset + start + bodyLen * byteCount;
            const size_t ofsOT = listOffset + start + bodyLen * capacity + slotIndex * byteCount;
            const size_t ofsNick = ofsOT + slotIndex * capacity;

            saveData[listOffset + 1 + byteCount] = static_cast<uint8_t>(pokemon->speciesInternal());
            std::memcpy(&saveData[ofsBody], raw.data() + 3, bodyLen);
            std::memcpy(&saveData[ofsOT], raw.data() + 3 + ::Pokemon::SIZE_1PARTY, slotIndex);
            std::memcpy(&saveData[ofsNick], raw.data() + 3 + ::Pokemon::SIZE_1PARTY + slotIndex, slotIndex);
            ++byteCount;
        }
        saveData[listOffset] = static_cast<uint8_t>(byteCount);
        // The cap goes immediately after the last present entry and NOTHING FURTHER is touched.
        // The games write exactly one 0xFF there and leave the remaining marker bytes at whatever
        // they were; filling the rest is harmless to the game but rewrites untouched bytes, which
        // is how an unedited save stops round-tripping. `count` is what bounds the read anyway.
        saveData[listOffset + 1 + byteCount] = 0xFF;
    }

    void Trainer1RBY::parseParty()
    {
        party.clear();
        const size_t count = std::min<size_t>(saveData[offsets.party], 6);
        for (size_t index = 0; index < count; ++index)
        {
            if (auto pokemon = readListEntry(offsets.party, 6, true, index))
                party.push_back(std::move(pokemon));
        }
    }

    void Trainer1RBY::parseBoxes()
    {
        boxes.clear();
        boxes.resize(getBoxCount());
        const size_t capacity = boxSlotCount();
        const uint8_t current = currentBoxIndex();
        const bool initialized = boxesInitialized();

        for (size_t boxIndex = 0; boxIndex < getBoxCount(); ++boxIndex)
        {
            // The bank copy of the CURRENT box is stale by design -- the game keeps the live copy
            // at `currentBox` and only flushes on a box change. And until the first flush ever
            // happens the whole bank is uninitialised garbage, which is what `boxesInitialized`
            // records; reading it then would invent Pokemon out of unwritten memory.
            const bool useLive = (boxIndex == current) || !initialized;
            if (!initialized && boxIndex != current)
                continue;
            const size_t listOfs = useLive ? offsets.currentBox : boxRawOffset(boxIndex);
            for (size_t slotIndex = 0; slotIndex < capacity && slotIndex < BOX_SLOTS; ++slotIndex)
            {
                boxes[boxIndex][slotIndex] = readListEntry(listOfs, capacity, false, slotIndex);
            }
        }
    }

    void Trainer1RBY::readPouch(size_t offset, size_t capacity, std::vector<InventoryItem> &out) const
    {
        out.clear();
        if (offset + 1 + capacity * 2 + 1 > saveData.size())
            return;
        size_t byteCount = saveData[offset];
        if (byteCount > capacity)
            byteCount = 0; // an uninitialised Yellow bag reads 0xFF here
        for (size_t index = 0; index < byteCount; ++index)
        {
            InventoryItem inventoryItem{};
            inventoryItem.itemId = saveData[offset + 1 + index * 2];
            inventoryItem.count = saveData[offset + 2 + index * 2];
            inventoryItem.isNew = inventoryItem.isFavorite = false;
            out.push_back(inventoryItem);
        }
    }

    void Trainer1RBY::writePouch(size_t offset, size_t capacity, const std::vector<InventoryItem> &in)
    {
        if (offset + 1 + capacity * 2 + 1 > saveData.size())
            return;
        size_t byteCount = 0;
        for (const auto &inventoryItem : in)
        {
            if (byteCount >= capacity)
                break;
            if (inventoryItem.itemId == 0 || inventoryItem.count == 0)
                continue; // count 0 is not "an item you have none of"
            saveData[offset + 1 + byteCount * 2] = static_cast<uint8_t>(inventoryItem.itemId);
            // Clamped to the FIELD ceiling (255), not to the game's legal 99. A real save can
            // hold more -- this one arrived with Max Elixir x158 and PP Up x203 -- and clamping on
            // write-back silently rewrites a quantity the user never touched. Out-of-range is the
            // legality checker's business to report, not the serializer's to quietly "fix".
            saveData[offset + 2 + byteCount * 2] = static_cast<uint8_t>(std::min<uint16_t>(inventoryItem.count, 255));
            ++byteCount;
        }
        saveData[offset] = static_cast<uint8_t>(byteCount);
        saveData[offset + 1 + byteCount * 2] = 0xFF; // the pouch terminator; the game reads until this
    }

    void Trainer1RBY::parseItems()
    {
        items.clear();
        items.resize(POUCH_COUNT1_RBY);
        readPouch(offsets.items, getItemPouchCapacity(static_cast<int>(PouchType1RBY::Items)), items[static_cast<size_t>(PouchType1RBY::Items)]);
        readPouch(offsets.pcItems, getItemPouchCapacity(static_cast<int>(PouchType1RBY::PCItems)), items[static_cast<size_t>(PouchType1RBY::PCItems)]);
    }

    void Trainer1RBY::updatePartyBlock()
    {
        std::vector<::Pokemon::Pokemon *> ptrs;
        for (auto &p : party)
            ptrs.push_back(p.get());
        writeList(offsets.party, 6, true, ptrs);
    }

    void Trainer1RBY::updateBoxBlock()
    {
        const size_t capacity = boxSlotCount();
        const uint8_t current = currentBoxIndex();
        for (size_t boxIndex = 0; boxIndex < getBoxCount(); ++boxIndex)
        {
            std::vector<::Pokemon::Pokemon *> ptrs;
            for (size_t slotIndex = 0; slotIndex < capacity && slotIndex < BOX_SLOTS; ++slotIndex)
                ptrs.push_back(boxes[boxIndex][slotIndex].get());
            writeList(boxRawOffset(boxIndex), capacity, false, ptrs);
            // The current box exists in two places and the game reads the live copy, so writing
            // only the bank would leave the player's open box showing its pre-edit contents.
            if (boxIndex == current)
                writeList(offsets.currentBox, capacity, false, ptrs);
        }
        // Claim the bank is initialised only once something is actually in it. Setting the bit
        // unconditionally marks a brand-new save's empty boxes as real data, which is a lie about
        // a file the user only opened -- and it is exactly the flag the next load trusts.
        bool anyContent = false;
        for (size_t boxIndex = 0; boxIndex < getBoxCount() && !anyContent; ++boxIndex)
            for (size_t slotIndex = 0; slotIndex < capacity && slotIndex < BOX_SLOTS; ++slotIndex)
                if (boxes[boxIndex][slotIndex])
                {
                    anyContent = true;
                    break;
                }
        if (anyContent)
            saveData[offsets.currentBoxIndex] = static_cast<uint8_t>((saveData[offsets.currentBoxIndex] & 0x7F) | 0x80);
    }

    void Trainer1RBY::updateItemBlock()
    {
        if (items.size() > static_cast<size_t>(PouchType1RBY::Items))
            writePouch(offsets.items, getItemPouchCapacity(static_cast<int>(PouchType1RBY::Items)), items[static_cast<size_t>(PouchType1RBY::Items)]);
        if (items.size() > static_cast<size_t>(PouchType1RBY::PCItems))
            writePouch(offsets.pcItems, getItemPouchCapacity(static_cast<int>(PouchType1RBY::PCItems)), items[static_cast<size_t>(PouchType1RBY::PCItems)]);
    }

    void Trainer1RBY::updateTrainerInfoBlock()
    {
        // Rewrite a name only if it actually changed. An unset field is 0x00-filled, and 0x00 is a
        // terminator just as 0x50 is -- so re-encoding an empty name writes a 0x50 where the game
        // left a 0x00, changing bytes nobody edited. The general rule this is an instance of: the
        // serializer's job is to persist edits, not to normalise a file it was merely shown.
        const size_t nameBytes = getMaxTrainerNameLength() + 1;
        if (decodeName(&saveData[RBYOffsets::otNameOffset], nameBytes, saveIsJapanese) != trainerName)
            encodeName(&saveData[RBYOffsets::otNameOffset], nameBytes, trainerName, saveIsJapanese);
        if (decodeName(&saveData[offsets.rival], nameBytes, saveIsJapanese) != rivalName)
            encodeName(&saveData[offsets.rival], nameBytes, rivalName, saveIsJapanese);
        saveData[offsets.tid16] = static_cast<uint8_t>(TID16 >> 8); // big endian
        saveData[offsets.tid16 + 1] = static_cast<uint8_t>(TID16 & 0xFF);
        writeBCD(&saveData[offsets.money], 3, std::min<uint32_t>(money, getMaxMoney()));
        writeBCD(&saveData[offsets.coin], 2, std::min<uint32_t>(coins, 9999));
        saveData[offsets.badges] = badgeFlags;
        saveData[offsets.playTime + 0] = playedHours;
        saveData[offsets.playTime + 2] = playedMinutes;
        saveData[offsets.playTime + 3] = playedSeconds;
    }

    void Trainer1RBY::updateCurrentBoxBlock()
    {
        saveData[offsets.currentBoxIndex] = static_cast<uint8_t>((saveData[offsets.currentBoxIndex] & 0x80) | (currentBox & 0x7F));
    }

    void Trainer1RBY::updatePokedexBlock()
    {
        // Owned implies seen. Gen 1 keeps them as two independent bit arrays, and a species marked
        // caught but not seen is a state the games never produce.
        for (uint16_t internalSpeciesId = 1; internalSpeciesId <= ::Pokemon::MAX_SPECIES_GEN1; ++internalSpeciesId)
        {
            const uint16_t bit = static_cast<uint16_t>(internalSpeciesId - 1);
            if ((saveData[offsets.dexCaught + (bit >> 3)] >> (bit & 7)) & 1)
                saveData[offsets.dexSeen + (bit >> 3)] |= static_cast<uint8_t>(1 << (bit & 7));
        }
    }

    void Trainer1RBY::refreshChecksum()
    {
        // One byte: the complement of the sum from the trainer block to just before the checksum.
        uint8_t sum = 0;
        for (size_t index = RBYOffsets::otNameOffset; index < offsets.checksumOfs; ++index)
            sum = static_cast<uint8_t>(sum + saveData[index]);
        saveData[offsets.checksumOfs] = static_cast<uint8_t>(~sum);
    }

    const std::vector<uint8_t> &Trainer1RBY::serialize()
    {
        updateTrainerInfoBlock();
        updateCurrentBoxBlock();
        updatePartyBlock();
        updateBoxBlock();
        updateItemBlock();
        updatePokedexBlock();
        refreshChecksum(); // last: it covers everything written above
        return saveData;
    }
}
