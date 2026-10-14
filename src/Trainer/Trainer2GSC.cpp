#include <algorithm>
#include <cstring>

#include "Trainer/Trainer2GSC.h"
#include "Names/ItemPouches.h"
#include "Utils/Gen2Text.h"
#include "Utils/HelperUtilities.h"
#include "Utils/StringHelpers.h"
#include "Utils/Logger.h"

namespace Trainer
{
    namespace
    {
        // PKHeX SAV2Offsets.cs. Four layouts, one row each; a wrong number here is invisible
        // until a save is written back, which is why they are a table and not scattered literals.
        constexpr Gen2Offsets OFFSETS[4] = {
            // GS international
            {false, false, 0x2009, 0x23DB, 0x2053, 0x2021, 0x0000, 0x2724, 0x2727, 0x288A,
             0x2D6C, 0x284C, 0x2D68, 0x2D69, 0x7E6D, 0x23E6, 0x241F, 0x2449, 0x2464, 0x247E},
            // Crystal international
            {false, true, 0x2009, 0x23DC, 0x2052, 0x2021, 0x3E3D, 0x2700, 0x2703, 0x2865,
             0x2D10, 0x284C, 0x2B82, 0x2D0D, 0x1F0D, 0x23E7, 0x2420, 0x244A, 0x2465, 0x247F},
            // GS Japanese
            {true, false, 0x2009, 0x23BC, 0x2034, 0x2017, 0x0000, 0x2705, 0x2708, 0x283E,
             0x2D10, 0x2842, 0x2C8B, 0x2D0D, 0x7F0D, 0x23C7, 0x2400, 0x242A, 0x2445, 0x245F},
            // Crystal Japanese
            {true, true, 0x2009, 0x23BE, 0x2034, 0x2017, 0x8000, 0x26E2, 0x26E5, 0x281A,
             0x2D10, 0x2842, 0x2AE2, 0x2D0D, 0x7F0D, 0x23C9, 0x2402, 0x242C, 0x2447, 0x2461},
        };

        // Pouch capacities, PKHeX PlayerBag2.cs. Order matches the items[] vector below and the
        // GSC rows in gen_itempouches.py -- the two must agree, so the indices are named.
        constexpr size_t GEN2_POUCH_TMHM = 0, GEN2_POUCH_ITEM = 1, GEN2_POUCH_KEY = 2, GEN2_POUCH_BALL = 3,
                         GEN2_POUCH_PC = 4;
        constexpr size_t CAP_TMHM = 57, CAP_ITEM = 20, CAP_KEY = 26, CAP_BALL = 12, CAP_PC = 50;

        /// The TM/HM pouch is POSITIONALLY indexed: `data[i]` is the count of the item at
        /// MACHINE[i], which is the generated GSC TM/HM pouch list.
        ///
        /// IT IS NOT A RANGE. The ids run 191-194, 196-219, 221-242, then HMs 243-249 -- three
        /// gaps, at 190, 195 and 220 (220 is "TM28 (Unused)"). An arithmetic mapping that assumes
        /// one contiguous run puts every TM from 28 on in the wrong slot, so the table is the
        /// single source of truth for both directions and nothing here recomputes it.
        std::span<const uint16_t> machines() noexcept
        {
            return Names::getPouchItems(Enums::GameVersion::GSC, GEN2_POUCH_TMHM);
        }
        uint16_t machineId(size_t machineIndex) noexcept
        {
            const auto machineList = machines();
            return machineIndex < machineList.size() ? machineList[machineIndex] : 0;
        }
        int machineIndex(uint16_t itemId) noexcept
        {
            const auto machineList = machines();
            for (size_t mIndex = 0; mIndex < machineList.size(); ++mIndex)
                if (machineList[mIndex] == itemId)
                    return static_cast<int>(mIndex);
            return -1;
        }

        /// PKHeX SaveUtil.IsListValidG12: a well-formed Pokemon list has a plausible count and a
        /// 0xFF cap immediately after the last species marker.
        bool listValid(const std::vector<uint8_t> &saveBytes, size_t offset, uint8_t maxCount) noexcept
        {
            if (offset + 1 + maxCount + 1 > saveBytes.size())
                return false;
            const uint8_t count = saveBytes[offset];
            return count <= maxCount && saveBytes[offset + 1 + count] == 0xFF;
        }
        bool hasListAt(const std::vector<uint8_t> &saveBytes, size_t firstOffset, size_t secondOffset,
                       uint8_t maxCount) noexcept
        {
            return listValid(saveBytes, firstOffset, maxCount) && listValid(saveBytes, secondOffset, maxCount);
        }

        // Gen 2 is BIG ENDIAN for the trainer id and for money, like Gen 1 -- but money is a
        // plain integer here, not the BCD Gen 1 uses. Reading Gen 2's as BCD gives a number that
        // looks plausible and is wrong.
        uint16_t rd16be(const uint8_t *bytes) noexcept
        {
            return static_cast<uint16_t>((bytes[0] << 8) | bytes[1]);
        }
        uint32_t rd32be(const uint8_t *bytes) noexcept
        {
            return (static_cast<uint32_t>(bytes[0]) << 24) | (static_cast<uint32_t>(bytes[1]) << 16) |
                   (static_cast<uint32_t>(bytes[2]) << 8) | static_cast<uint32_t>(bytes[3]);
        }
        void wr16be(uint8_t *bytes, uint16_t value) noexcept
        {
            bytes[0] = static_cast<uint8_t>(value >> 8);
            bytes[1] = static_cast<uint8_t>(value);
        }
        void wr32be(uint8_t *bytes, uint32_t value) noexcept
        {
            bytes[0] = static_cast<uint8_t>(value >> 24);
            bytes[1] = static_cast<uint8_t>(value >> 16);
            bytes[2] = static_cast<uint8_t>(value >> 8);
            bytes[3] = static_cast<uint8_t>(value);
        }

        /// A Gen 2 text field: one byte per glyph, 0x50 terminates. A glyphless byte terminates
        /// too, as in Gen 1 -- there is nothing sensible to render and continuing invents text.
        std::string readText(const uint8_t *bytes, size_t length, bool isJapanese)
        {
            std::u16string out;
            for (size_t index = 0; index < length; ++index)
            {
                if (bytes[index] == 0x50 || bytes[index] == 0x00)
                    break;
                const char16_t character = Utils::gen2ToChar(bytes[index], isJapanese);
                if (character == 0)
                    break;
                out.push_back(character);
            }
            return Utils::utf16ToUtf8(out);
        }

        /// PKHeX StringConverterOption.Clear50: fill the WHOLE field with 0x50, then write the
        /// characters over it. There is no separate terminator -- the fill is the terminator, and
        /// a name that exactly fills the field correctly has none.
        void writeText(const std::string &text, uint8_t *bytes, size_t length, bool isJapanese)
        {
            std::memset(bytes, 0x50, length);
            const std::u16string utf16Text = Utils::utf8ToUtf16(text);
            for (size_t uIndex = 0; uIndex < length && uIndex < utf16Text.size(); ++uIndex)
            {
                const uint8_t packedByte = Utils::charToGen2(utf16Text[uIndex], isJapanese);
                if (packedByte == 0)
                    break;
                bytes[uIndex] = packedByte;
            }
        }
    }

    const Gen2Offsets &gen2Offsets(Gen2Layout layout) noexcept
    {
        return OFFSETS[static_cast<size_t>(layout)];
    }

    bool Trainer2GSC::detect(const std::vector<uint8_t> &bytes, Gen2Layout *out) noexcept
    {
        auto layoutMatches = [&](Gen2Layout layout)
        { if (out) *out = layout; return true; };
        // PKHeX SaveUtil.IsG2: EITHER size, and every layout is probed whatever the size. The
        // locale does not follow from the length -- a Japanese cartridge's SRAM is 32 KiB like
        // everyone else's, and every Japanese offset sits below 0x8000 -- so tying the layout to
        // the size refused ordinary Japanese saves PKHeX opens. The one Japanese field past 0x8000
        // (Crystal's trainer gender) is bounds-checked where it is read and written.
        if (bytes.size() != GEN2_SAVE_SIZE && bytes.size() != GEN2_SAVE_SIZE_DOUBLED)
            return false;
        // PKHeX's order: international before Japanese. Korean GS (0x2DAE / 0x28CC) is
        // deliberately not probed -- see the scope note in the header.
        if (hasListAt(bytes, 0x2D6C, 0x288A, 20))
            return layoutMatches(Gen2Layout::GS_INTL);
        if (hasListAt(bytes, 0x2D10, 0x2865, 20))
            return layoutMatches(Gen2Layout::C_INTL);
        // Japanese GS and Crystal SHARE the 0x2D10 probe (it is CurrentBox for both) and are
        // separated only by the party offset. Probing one list cannot tell them apart.
        if (hasListAt(bytes, 0x2D10, 0x283E, 30))
            return layoutMatches(Gen2Layout::GS_JP);
        if (hasListAt(bytes, 0x2D10, 0x281A, 30))
            return layoutMatches(Gen2Layout::C_JP);
        return false;
    }

    Trainer2GSC::Trainer2GSC(std::vector<uint8_t> raw, std::string path)
        : saveData(std::move(raw)), savePath(std::move(path)) { init(); }

    void Trainer2GSC::init()
    {
        if (!detect(saveData, &saveLayout))
            return;
        offsets = &gen2Offsets(saveLayout);
        boxes.resize(getBoxCount());
        boxNames.resize(getBoxCount());
        parse();
        valid = true;
    }

    std::string Trainer2GSC::gameTitle() const
    {
        return offsets && offsets->crystal ? "Pokemon Crystal" : "Pokemon Gold/Silver";
    }

    GameVersion Trainer2GSC::getGameVersion() const noexcept
    {
        // Crystal is detectable -- it has its own offsets, which is what offsets->crystal records.
        // Gold and Silver share a layout and carry no version byte, so Gold claims the pair, the
        // same convention getGroupRepVersion() uses for Red/Blue and Diamond/Pearl.
        return offsets && offsets->crystal ? GameVersion::C : GameVersion::GD;
    }

    size_t Trainer2GSC::boxListSize() const noexcept
    {
        return (nameWidth() * 2 + storedSize() + 1) * getSlotsPerBox() + 2;
    }

    size_t Trainer2GSC::boxOffset(size_t box) const noexcept
    {
        // The stride is the list length PLUS 2, and the banks split after box 7 (6 in Japanese).
        const size_t split = offsets->japanese ? 6 : 7;
        const size_t stride = boxListSize() + 2;
        return box < split ? 0x4000 + box * stride
                           : 0x6000 + (box - split) * stride;
    }

    void Trainer2GSC::readList(size_t listOffset, size_t capacity, size_t bodySize,
                               std::vector<std::unique_ptr<::Pokemon::Pokemon>> &out) const
    {
        const size_t slotIndex = nameWidth();
        const size_t start = 1 + capacity + 1;
        const size_t total = start + (bodySize + slotIndex + slotIndex) * capacity;
        if (listOffset + total > saveData.size())
            return;

        const size_t byteCount = std::min<size_t>(saveData[listOffset], capacity);
        for (size_t index = 0; index < byteCount; ++index)
        {
            const uint8_t marker = saveData[listOffset + 1 + index];
            if (marker == 0 || marker == 0xFF)
            {
                out.push_back(nullptr);
                continue;
            }
            const size_t ofsBody = listOffset + start + bodySize * index;
            const size_t ofsOT = listOffset + start + bodySize * capacity + slotIndex * index;
            const size_t ofsNick = ofsOT + slotIndex * capacity;

            // Build the single-entry list Pokemon2GSC expects. A BOX body is 32 bytes copied into
            // a 48-byte party body; the extra 16 are level + battle stats, which the game
            // recomputes on withdrawal and recalculateStats() reproduces.
            std::vector<std::byte> rec(offsets->japanese ? ::Pokemon::SIZE_2JLIST : ::Pokemon::SIZE_2ULIST,
                                       std::byte{0});
            rec[0] = std::byte{1};
            rec[1] = std::byte{marker};
            rec[2] = std::byte{0xFF};
            std::memcpy(rec.data() + 3, &saveData[ofsBody], bodySize);
            std::memcpy(rec.data() + 3 + ::Pokemon::SIZE_2PARTY, &saveData[ofsOT], slotIndex);
            std::memcpy(rec.data() + 3 + ::Pokemon::SIZE_2PARTY + slotIndex, &saveData[ofsNick], slotIndex);

            auto pokemon = std::make_unique<::Pokemon::Pokemon2GSC>(
                std::span<const std::byte>(rec.data(), rec.size()));
            if (!pokemon->isValid())
            {
                out.push_back(nullptr);
                continue;
            }
            if (bodySize == storedSize())
                pokemon->recalculateStats();
            out.push_back(std::move(pokemon));
        }
    }

    void Trainer2GSC::writeList(size_t listOffset, size_t capacity, size_t bodySize,
                                const std::vector<::Pokemon::Pokemon *> &mons)
    {
        const size_t slotIndex = nameWidth();
        const size_t start = 1 + capacity + 1;
        const size_t total = start + (bodySize + slotIndex + slotIndex) * capacity;
        if (listOffset + total > saveData.size())
            return;

        size_t byteCount = 0;
        for (size_t monIndex = 0; monIndex < capacity && monIndex < mons.size(); ++monIndex)
        {
            const auto *source = mons[monIndex];
            if (!source || source->speciesID() == 0)
                continue;
            auto *pokemon = static_cast<::Pokemon::Pokemon2GSC *>(mons[monIndex]);
            pokemon->refreshChecksum(); // re-syncs the list header; PK2 has no checksum of its own
            const auto raw = pokemon->getData();
            const size_t ofsBody = listOffset + start + bodySize * byteCount;
            const size_t ofsOT = listOffset + start + bodySize * capacity + slotIndex * byteCount;
            const size_t ofsNick = ofsOT + slotIndex * capacity;

            // The record's OWN marker byte, not the species: 0xFD there means egg, and Gen 2
            // records that nowhere else. syncListHeader() above keeps it correct.
            saveData[listOffset + 1 + byteCount] = static_cast<uint8_t>(raw[1]);
            std::memcpy(&saveData[ofsBody], raw.data() + 3, bodySize);
            std::memcpy(&saveData[ofsOT], raw.data() + 3 + ::Pokemon::SIZE_2PARTY, slotIndex);
            std::memcpy(&saveData[ofsNick], raw.data() + 3 + ::Pokemon::SIZE_2PARTY + slotIndex, slotIndex);
            ++byteCount;
        }
        saveData[listOffset] = static_cast<uint8_t>(byteCount);
        // Exactly one cap byte, immediately after the last entry -- nothing beyond it is touched.
        // The games write one 0xFF and leave the rest as-is; filling the remainder is harmless to
        // the game but rewrites bytes nobody edited, which is how an unedited save stops
        // round-tripping.
        saveData[listOffset + 1 + byteCount] = 0xFF;
    }

    void Trainer2GSC::parse()
    {
        parseTrainer();
        parseParty();
        parseBoxes();
        parseItems();
        parseBoxNames();
    }

    void Trainer2GSC::parseTrainer()
    {
        const Gen2Offsets &offset = *offsets;
        trainerName = readText(&saveData[offset.trainer1 + 2], getMaxTrainerNameLength(), offset.japanese);
        TID16 = rd16be(&saveData[offset.trainer1]);
        SID16 = 0; // Gen 2 has no secret ID
        ID32 = TID16;
        TID = TID16;
        SID = 0;
        // Three bytes, big endian, read as a u32 >> 8. Byte money+3 belongs to something else.
        money = rd32be(&saveData[offset.money]) >> 8;
        trainerGender =
            (offset.crystal && offset.gender && offset.gender < saveData.size()) ? (saveData[offset.gender] & 1) : 0;
        // The whole byte, as PKHeX reads it. Gen 1's 0x80 "boxes initialised" bit is a Gen 1
        // concept and masking it off here would silently renumber a box.
        currentBox = saveData[offset.currentBoxIndex];
    }

    void Trainer2GSC::parseParty()
    {
        readList(offsets->party, 6, partySize(), party);
        // A party list has no holes; drop any null the reader produced rather than leaving gaps.
        party.erase(std::remove(party.begin(), party.end(), nullptr), party.end());
    }

    void Trainer2GSC::parseBoxes()
    {
        const size_t capacity = getSlotsPerBox();
        for (size_t boxIndex = 0; boxIndex < getBoxCount(); ++boxIndex)
        {
            std::vector<std::unique_ptr<::Pokemon::Pokemon>> scratch;
            // The BANK, never the mirror. PKHeX: "Don't treat the CurrentBox segment as valid;
            // Stadium ignores it and will de-synchronize it."
            readList(boxOffset(boxIndex), capacity, storedSize(), scratch);
            for (size_t slotIndex = 0; slotIndex < scratch.size() && slotIndex < BOX_SLOTS; ++slotIndex)
                boxes[boxIndex][slotIndex] = std::move(scratch[slotIndex]);
        }
    }

    void Trainer2GSC::parseBoxNames()
    {
        const Gen2Offsets &offset = *offsets;
        for (size_t boxIndex = 0; boxIndex < getBoxCount(); ++boxIndex)
        {
            const size_t nameOffset = offset.boxNames + boxIndex * boxNameWidth();
            if (nameOffset + boxNameWidth() > saveData.size())
                break;
            std::string name = readText(&saveData[nameOffset], boxNameWidth(), offset.japanese);
            boxNames[boxIndex] = name.empty() ? ("Box " + std::to_string(boxIndex + 1)) : name;
        }
    }

    void Trainer2GSC::parseItems()
    {
        const Gen2Offsets &offset = *offsets;
        items.assign(5, {});

        // (a) TM/HM: positionally indexed, one count byte per legal machine.
        for (size_t index = 0; index < CAP_TMHM; ++index)
        {
            const uint8_t storedCount = saveData[offset.pouchTMHM + index];
            if (storedCount == 0)
                continue;
            items[GEN2_POUCH_TMHM].push_back(InventoryItem{machineId(index), storedCount, false, false});
        }
        // (b) count-prefixed {id, count} pairs.
        auto pairPouch = [&](size_t pouchOffset, size_t capacity, size_t slot)
        {
            const size_t storedCount = std::min<size_t>(saveData[pouchOffset], capacity);
            for (size_t index = 0; index < storedCount; ++index)
                items[slot].push_back(InventoryItem{saveData[pouchOffset + 1 + index * 2],
                                                    saveData[pouchOffset + 2 + index * 2], false, false});
        };
        pairPouch(offset.pouchItem, CAP_ITEM, GEN2_POUCH_ITEM);
        pairPouch(offset.pouchBall, CAP_BALL, GEN2_POUCH_BALL);
        pairPouch(offset.pouchPC, CAP_PC, GEN2_POUCH_PC);
        // (c) Key Items: count-prefixed ids only; the quantity is implicitly 1.
        {
            const size_t storedCount = std::min<size_t>(saveData[offset.pouchKey], CAP_KEY);
            for (size_t index = 0; index < storedCount; ++index)
                items[GEN2_POUCH_KEY].push_back(InventoryItem{saveData[offset.pouchKey + 1 + index], 1, false, false});
        }
    }

    void Trainer2GSC::updatePartyBlock()
    {
        std::vector<::Pokemon::Pokemon *> ptrs;
        for (auto &p : party)
            ptrs.push_back(p.get());
        writeList(offsets->party, 6, partySize(), ptrs);
    }

    void Trainer2GSC::updateBoxBlock()
    {
        const size_t capacity = getSlotsPerBox();
        for (size_t boxIndex = 0; boxIndex < getBoxCount(); ++boxIndex)
        {
            std::vector<::Pokemon::Pokemon *> ptrs;
            for (size_t slotIndex = 0; slotIndex < capacity && slotIndex < BOX_SLOTS; ++slotIndex)
                ptrs.push_back(boxes[boxIndex][slotIndex].get());
            writeList(boxOffset(boxIndex), capacity, storedSize(), ptrs);
        }
        // Re-synchronise the mirror from the bank AFTER writing it -- Gen 2's mirror is the stale
        // copy, so it is an output here, never an input. (Gen 1 is the other way round.)
        //
        // Only when the stored index names a real box. PKHeX copies the mirror inside the box loop
        // (`if (written && i == CurrentBox)`), so an out-of-range index simply copies nothing;
        // substituting box 0 would overwrite the mirror with a box the player never had open.
        if (currentBox >= 0 && static_cast<size_t>(currentBox) < getBoxCount())
        {
            const size_t source = boxOffset(static_cast<size_t>(currentBox));
            const size_t length = boxListSize();
            if (offsets->currentBox + length <= saveData.size() && source + length <= saveData.size())
                std::memmove(&saveData[offsets->currentBox], &saveData[source], length);
        }
    }

    void Trainer2GSC::updateTrainerInfoBlock()
    {
        const Gen2Offsets &offset = *offsets;
        writeText(trainerName, &saveData[offset.trainer1 + 2], getMaxTrainerNameLength(), offset.japanese);
        wr16be(&saveData[offset.trainer1], TID16);
        // Preserve money+3: the field is three bytes and the fourth is somebody else's.
        const uint32_t clamped = money > getMaxMoney() ? getMaxMoney() : money;
        wr32be(&saveData[offset.money], (clamped << 8) | saveData[offset.money + 3]);
        if (offset.crystal && offset.gender && offset.gender < saveData.size())
            saveData[offset.gender] = trainerGender & 1;
    }

    void Trainer2GSC::updateBoxNameBlock()
    {
        const Gen2Offsets &offset = *offsets;
        for (size_t boxIndex = 0; boxIndex < getBoxCount(); ++boxIndex)
        {
            // never write back the "Box N" display defaults
            if (!isBoxNameDirty(boxIndex)) continue;
            const size_t nameOffset = offset.boxNames + boxIndex * boxNameWidth();
            if (nameOffset + boxNameWidth() > saveData.size())
                break;
            writeText(boxNames[boxIndex], &saveData[nameOffset], boxNameWidth(), offset.japanese);
        }
    }

    void Trainer2GSC::updateCurrentBoxBlock()
    {
        saveData[offsets->currentBoxIndex] = static_cast<uint8_t>(currentBox);
    }

    void Trainer2GSC::updateItemBlock()
    {
        const Gen2Offsets &offset = *offsets;
        if (items.size() < 5)
            return;

        // (a) TM/HM: write counts into fixed positions and zero the rest. An id that is not a
        //     machine is dropped rather than written somewhere wrong -- PKHeX does the same.
        std::memset(&saveData[offset.pouchTMHM], 0, CAP_TMHM);
        for (const InventoryItem &it : items[GEN2_POUCH_TMHM])
        {
            const int index = machineIndex(it.itemId);
            if (index < 0)
                continue;
            saveData[offset.pouchTMHM + index] = static_cast<uint8_t>(std::min<uint16_t>(it.count, 255));
        }
        // (b) count-prefixed pairs.
        auto pairPouch = [&](size_t pouchOffset, size_t capacity, size_t slot)
        {
            size_t byteCount = 0;
            for (const InventoryItem &it : items[slot])
            {
                if (byteCount >= capacity)
                    break;
                if (it.itemId == 0 || it.count == 0)
                    continue;
                saveData[pouchOffset + 1 + byteCount * 2] = static_cast<uint8_t>(it.itemId);
                // Clamped to the FIELD ceiling, not the game's legal 99: a real save can carry
                // more, and clamping on write silently rewrites a quantity nobody touched.
                saveData[pouchOffset + 2 + byteCount * 2] = static_cast<uint8_t>(std::min<uint16_t>(it.count, 255));
                ++byteCount;
            }
            saveData[pouchOffset] = static_cast<uint8_t>(byteCount);
            saveData[pouchOffset + 1 + byteCount * 2] = 0xFF;
        };
        pairPouch(offset.pouchItem, CAP_ITEM, GEN2_POUCH_ITEM);
        pairPouch(offset.pouchBall, CAP_BALL, GEN2_POUCH_BALL);
        pairPouch(offset.pouchPC, CAP_PC, GEN2_POUCH_PC);
        // (c) Key Items: ids only.
        {
            size_t byteCount = 0;
            for (const InventoryItem &it : items[GEN2_POUCH_KEY])
            {
                if (byteCount >= CAP_KEY)
                    break;
                if (it.itemId == 0)
                    continue;
                saveData[offset.pouchKey + 1 + byteCount] = static_cast<uint8_t>(it.itemId);
                ++byteCount;
            }
            saveData[offset.pouchKey] = static_cast<uint8_t>(byteCount);
            saveData[offset.pouchKey + 1 + byteCount] = 0xFF;
        }
    }

    void Trainer2GSC::writeChecksums() noexcept
    {
        const Gen2Offsets &offset = *offsets;
        uint16_t sum = 0;
        for (size_t rawIndex = offset.trainer1; rawIndex <= offset.accumulatedChecksumEnd && rawIndex < saveData.size();
             ++rawIndex)
            sum = static_cast<uint16_t>(sum + saveData[rawIndex]);
        // LITTLE endian, in a format whose every entity field is big endian. Both copies.
        Utils::writeUInt16LittleEndian(&saveData[offset.overallChecksumPosition], sum);
        Utils::writeUInt16LittleEndian(&saveData[offset.overallChecksumPosition2], sum);
    }

    std::vector<Gen2Mirror> Trainer2GSC::mirrorRegions() const
    {
        // PKHeX SAV2.GetFinalData. Japanese saves keep one copy, Crystal one, and Gold/Silver five
        // pieces because their backup is not contiguous with the primary.
        const Gen2Offsets &offset = *offsets;
        if (offset.japanese)
            return {{offset.trainer1, offset.crystal ? 0xADAu : 0xC83u, 0x7209}};
        if (offset.crystal)
            return {{0x2009, 0xB7A, 0x1209}};
        return {{0x2009, 0x222F - 0x2009, 0x15C7},
                {0x222F, 0x23D9 - 0x222F, 0x3D69},
                {0x23D9, 0x2856 - 0x23D9, 0x0C6B},
                {0x2856, 0x288A - 0x2856, 0x7E39},
                {0x288A, 0x2D69 - 0x288A, 0x10E8}};
    }

    void Trainer2GSC::writeMirrors() noexcept
    {
        // The games read these back; they are not optional. Run AFTER writeChecksums() so the
        // mirrored ranges carry the fresh checksum bytes where they overlap -- PKHeX orders it the
        // same way. The ranges come from mirrorRegions() rather than being spelled here as well,
        // so the writer and anything checking it can never disagree about where they are.
        for (const Gen2Mirror &mirror : mirrorRegions())
        {
            if (mirror.sourceOffset + mirror.length <= saveData.size() &&
                mirror.destinationOffset + mirror.length <= saveData.size())
            {
                std::memmove(&saveData[mirror.destinationOffset], &saveData[mirror.sourceOffset], mirror.length);
            }
        }
    }

    bool Trainer2GSC::canStoreBoxName(const std::string &name) const
    {
        return Utils::gen2CanEncode(Utils::utf8ToUtf16(name), offsets && offsets->japanese);
    }

    std::unique_ptr<::Pokemon::Pokemon> Trainer2GSC::createBlankPokemon() const
    {
        return std::make_unique<::Pokemon::Pokemon2GSC>(offsets && offsets->japanese);
    }

    const std::vector<uint8_t> &Trainer2GSC::serialize()
    {
        updateItemBlock();
        updatePartyBlock();
        updateBoxBlock();
        updateBoxNameBlock();
        updateCurrentBoxBlock();
        updateTrainerInfoBlock();
        writeChecksums();
        writeMirrors(); // after the checksums, so the mirrors carry them
        return saveData;
    }
}
