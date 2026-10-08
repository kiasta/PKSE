/**
 * A 128 KiB save of two slots, each 14 rotated 0x1000 sectors (see Trainer3FRLG.h). We keep the raw
 * save buffer and address logical blocks (Small/Large/Storage) through the active slot's sector table,
 * so box mons that straddle a sector boundary are handled byte-wise. Entities (PK3) are 80 B stored /
 * 100 B party, encrypted (XOR + PID%24 shuffle). Money + bag item-counts are XOR'd with the security
 * key. On save we re-encrypt the party/boxes/items in place and recompute every sector checksum.
 *
 * Offsets validated byte-for-byte against a real FireRed save. A.
 */
#include <algorithm>
#include <cstring>

#include "Trainer/Trainer3FRLG.h"
#include "Utils/Gen3Text.h" // the Gen 3 character set, shared with Pokemon3FRLG + Convert
#include "Utils/HelperUtilities.h"
#include "Utils/Logger.h"

using namespace Utils;
using namespace Pokemon;
using namespace Encryption;

namespace Trainer
{
    namespace
    {
        /// A slot is usable when all 14 logical sector ids are present, once each, behind the sector
        /// magic. detect() and selectActiveSlot() must ask the same question, or a file passes the
        /// probe and then fails to open.
        bool slotHoldsEverySector(const std::vector<uint8_t> &bytes, size_t slotBase, size_t &sectorZeroOffset)
        {
            bool seen[FRLG_SECTORS] = {false};
            for (size_t sectorIndex = 0; sectorIndex < FRLG_SECTORS; ++sectorIndex)
            {
                const size_t byteOffset = slotBase + sectorIndex * FRLG_SECTOR_SIZE;
                if (readUInt32LittleEndian(&bytes[byteOffset + 0xFF8]) != FRLG_SECTOR_MAGIC)
                    return false;
                const uint16_t sectionId = readUInt16LittleEndian(&bytes[byteOffset + 0xFF4]);
                if (sectionId >= FRLG_SECTORS || seen[sectionId])
                    return false;
                seen[sectionId] = true;
                if (sectionId == 0)
                    sectorZeroOffset = byteOffset;
            }
            return true;
        }

        /// PKHeX SAV3BlockDetection.CompareCounters: an erased counter loses unless the other is one
        /// short of it, which only a counter that has rolled over can be.
        bool firstCounterIsNewer(uint32_t firstCounter, uint32_t secondCounter)
        {
            if (firstCounter == 0xFFFFFFFFu && secondCounter != 0xFFFFFFFEu)
                return false;
            if (secondCounter == 0xFFFFFFFFu && firstCounter != 0xFFFFFFFEu)
                return true;
            return firstCounter >= secondCounter;
        }
    }

    bool Trainer3FRLG::detect(const std::vector<uint8_t> &bytes) noexcept
    {
        if (bytes.size() != FRLG_SAVE_SIZE)
            return false;
        // Either slot will do: a save mid-write has one good slot and one being replaced.
        for (size_t slotBase : {FRLG_SLOT_A, FRLG_SLOT_B})
        {
            size_t smallOffset = 0;
            if (!slotHoldsEverySector(bytes, slotBase, smallOffset))
                continue;
            // It is a Gen 3 save. Now: ours, or Ruby/Sapphire/Emerald's? FR/LG write the fixed
            // value 1 at Small+0xAC and nothing else does, so that one word is the whole test --
            // the same discriminator Trainer3RSE::detect() uses, read the other way round.
            return readUInt32LittleEndian(&bytes[smallOffset + 0xAC]) == 1u;
        }
        return false;
    }

    namespace
    {
        // Decodes through UTF-16 and hands back UTF-8, so the accents and the ♀/♂ a Gen 3 name may
        // legitimately contain survive into a std::string. Going straight to narrow chars is what
        // silently dropped them before -- none of them fit in one.
        //
        // THE SAVE'S OWN LANGUAGE PICKS THE TABLE HERE, not a record's: a trainer name and a box
        // name belong to the cartridge, so they are written in its font and nothing else's. That is
        // the opposite of Pokemon3FRLG, where the RECORD decides, because a traded Pokemon keeps
        // the bytes of the game it came from while the box it sits in does not.
        std::string g3Decode(const uint8_t *bytes, size_t maxLen, uint8_t languageId)
        {
            std::u16string wide;
            for (size_t index = 0; index < maxLen; ++index)
            {
                const uint8_t packedByte = bytes[index];
                if (packedByte == Utils::GEN3_TERMINATOR)
                    break;
                // 0 = no glyph -> skip it
                if (const char16_t character = Utils::gen3ToChar(packedByte, languageId)) wide += character;
            }
            // trim trailing spaces (Gen 3 pads short names with spaces)
            while (!wide.empty() && wide.back() == u' ')
                wide.pop_back();
            return Utils::utf16ToUtf8(wide);
        }

        /**
         * Inverse of g3Decode. Returns FALSE if any character has no Gen 3 representation, so the
         * caller can refuse the name outright rather than silently storing a mangled one -- the
         * Switch keyboard will happily produce accents and emoji that this table cannot express.
         *
         * Writes the text plus a single 0xFF terminator and **touches nothing after it**, so `out`
         * must arrive holding the bytes currently in the save.
         *
         * That tail matters more than it looks. A real FireRed save carries a MIX of 0x00 and 0xFF
         * after the terminator -- the game writes a name and leaves whatever was already there.
         * Clearing to 0xFF drifts 33 bytes; clearing to 0x00 drifts 19. Only preserving the tail
         * reproduces the file, and it is the safer rule anyway: don't rewrite bytes you have no reason
         * to touch. Decoding never notices either way, since g3Decode stops at the terminator -- which
         * is precisely why only a byte compare finds it.
         *
         * `in` is UTF-8 and is decoded before mapping. Walking it as raw bytes instead rejects every
         * multi-byte character on the lead byte alone, which quietly refuses names Gen 3 can in fact
         * store -- ♀, ♂ and the accented letters all have real bytes in its table.
         *
         * maxChars counts Gen 3 bytes, i.e. glyphs, not UTF-8 code units, so a name of accents still
         * measures against the field the way the player sees it.
         */
        bool g3Encode(const std::string &in, uint8_t *out, size_t bytes, size_t maxChars, uint8_t languageId)
        {
            size_t byteCount = 0;
            for (const char16_t c : Utils::utf8ToUtf16(in))
            {
                if (byteCount >= maxChars || byteCount >= bytes)
                    break;
                const uint8_t packedByte = Utils::charToGen3(c, languageId);
                // no Gen 3 glyph for this character
                if (packedByte == Utils::GEN3_TERMINATOR) return false;
                out[byteCount++] = packedByte;
            }
            // terminator; everything past it is left alone
            if (byteCount < bytes) out[byteCount] = Utils::GEN3_TERMINATOR;
            return true;
        }

        // Build a 100-byte ENCRYPTED party record from an entity, computing the party-only battle stats
        // (0x50-0x63) so a pokemon promoted from a box (80 B, no stats) serializes correctly. Caller writes 100 B.
        // THE STAT TAIL (0x50-0x63) IS STORAGE, AND A PARTY RECORD ARRIVES CARRYING THE GAME'S OWN.
        // Only a record that has none -- one promoted from a box, 80 bytes -- gets one computed here.
        //
        // Recomputing unconditionally is the obvious implementation and it corrupts an untouched save.
        // Gen 3 refreshes a Pokemon's battle stats only on level-up, evolution or a PC deposit, so a
        // Pokemon that has been EV-trained since its last level-up legitimately stores stats BELOW what
        // the formula gives -- that is the game's state, not a stale cache. A real Ruby save's level 92
        // Starmie stores 246 HP where the formula says 249; writing 249 back applies a PC deposit the
        // player never made, and drifts six bytes of a save PKSE was only asked to read.
        //
        // Everything that edits a stat-affecting field already calls recalculateStats(), which fills
        // this same tail -- so an edited Pokemon is still written with fresh stats, and an unedited one
        // reproduces byte for byte.
        void buildPartyRecord(::Pokemon::Pokemon &pk, uint8_t out[100])
        {
            uint8_t buffer[100];
            std::memset(buffer, 0, sizeof(buffer));
            const size_t byteCount = std::min<size_t>(pk.getDataSize(), 100);
            std::memcpy(buffer, pk.getData().data(), byteCount); // canonical header + G/A/E/M (+ stats if party)
            // NO TAIL means no bytes past the stored 80 OR bytes that are all zero. A real party record
            // always holds a level, a max HP and 0xFF as its mail id, so an all-zero tail is padding from
            // wherever the record was lengthened -- the bank's 100-byte record is one such place. Copying
            // it as found wrote a level 0, 0/0 HP Pokemon into the party.
            const bool hasPartyTail =
                byteCount == 100 && std::any_of(buffer + 80, buffer + 100, [](uint8_t value) { return value != 0; });
            if (!hasPartyTail)
            {
                // Box-sourced: no tail in the source bytes, so derive one. 0x50 (status) stays 0
                // = healthy, and 0x55 is the mail id, whose "none" is 0xFF rather than 0.
                const uint16_t maxHP = pk.statHPMax();
                buffer[0x54] = pk.level();
                buffer[0x55] = 0xFF; // mail id: none
                writeUInt16LittleEndian(buffer + 0x56, maxHP); // arrives at full health
                writeUInt16LittleEndian(buffer + 0x58, maxHP);
                writeUInt16LittleEndian(buffer + 0x5A, pk.statATK());
                writeUInt16LittleEndian(buffer + 0x5C, pk.statDEF());
                writeUInt16LittleEndian(buffer + 0x5E, pk.statSPE());
                writeUInt16LittleEndian(buffer + 0x60, pk.statSPA());
                writeUInt16LittleEndian(buffer + 0x62, pk.statSPD());
            }
            std::byte *encryptedRecord = encryptArray3FRLG(
                std::span<const std::byte>(reinterpret_cast<const std::byte *>(buffer), 100));
            std::memcpy(out, encryptedRecord, 100);
            delete[] encryptedRecord;
        }

        // Build an 80-byte ENCRYPTED stored record (box format; the party-only battle stats are dropped).
        void buildBoxRecord(::Pokemon::Pokemon &pk, uint8_t out[80])
        {
            uint8_t buffer[80];
            std::memset(buffer, 0, sizeof(buffer));
            const size_t byteCount = std::min<size_t>(pk.getDataSize(), 80);
            std::memcpy(buffer, pk.getData().data(), byteCount);
            std::byte *encryptedRecord = encryptArray3FRLG(
                std::span<const std::byte>(reinterpret_cast<const std::byte *>(buffer), 80));
            std::memcpy(out, encryptedRecord, 80);
            delete[] encryptedRecord;
        }
    }

    Trainer3FRLG::Trainer3FRLG(std::vector<uint8_t> data, std::string fileName)
        : Trainer(std::vector<Block>{}), saveData(std::move(data)), saveFileName(std::move(fileName))
    {
        // Always leave the containers in a consistent shape, even for a bad save.
        boxes.clear();
        boxes.resize(FRLG_BOX_COUNT);
        items.clear();
        items.resize(POUCH_COUNT3_FRLG);
        boxNames.clear();
        for (size_t fRLG_BOXIndex = 0; fRLG_BOXIndex < FRLG_BOX_COUNT; ++fRLG_BOXIndex)
            boxNames.push_back("Box " + std::to_string(fRLG_BOXIndex + 1));

        if (saveData.size() < FRLG_SAVE_SIZE)
        {
            logErrorToFile("FRLG save too small (need 0x20000 bytes)");
            return;
        }
        selectActiveSlot();
        if (!valid)
        {
            logErrorToFile("FRLG save missing one or more sector ids (0..13) in the active slot");
            return;
        }
        parseTrainer();
        parseParty();
        parseBoxes();
        parseBoxNames();
        parseItems();
    }

    void Trainer3FRLG::selectActiveSlot()
    {
        // PKHeX SAV3.GetActiveSlot. A slot missing a sector is never the save, however new its counter:
        // an interrupted save leaves the newer slot exactly like that, and the game loads the other one.
        size_t slotASectorZero = 0;
        size_t slotBSectorZero = 0;
        const bool slotAComplete = slotHoldsEverySector(saveData, FRLG_SLOT_A, slotASectorZero);
        const bool slotBComplete = slotHoldsEverySector(saveData, FRLG_SLOT_B, slotBSectorZero);
        const uint32_t slotACounter = slotAComplete ? readUInt32LittleEndian(&saveData[slotASectorZero + 0xFFC]) : 0;
        const uint32_t slotBCounter = slotBComplete ? readUInt32LittleEndian(&saveData[slotBSectorZero + 0xFFC]) : 0;
        bool aWins;
        if (slotAComplete && slotBComplete)
            aWins = firstCounterIsNewer(slotACounter, slotBCounter);
        else
            aWins = slotAComplete || !slotBComplete;
        slotBaseOffset = aWins ? FRLG_SLOT_A : FRLG_SLOT_B;

        // Resolve the rotated sector table: sectorOffset[id] = absolute offset of the sector carrying id.
        bool seen[FRLG_SECTORS] = {false};
        int found = 0;
        for (size_t sIndex = 0; sIndex < FRLG_SECTORS; ++sIndex)
        {
            const size_t byteOffset = slotBaseOffset + sIndex * FRLG_SECTOR_SIZE;
            const uint16_t sectionId = readUInt16LittleEndian(&saveData[byteOffset + 0xFF4]);
            if (sectionId < FRLG_SECTORS && !seen[sectionId])
            {
                sectorOffset[sectionId] = byteOffset;
                seen[sectionId] = true;
                ++found;
            }
        }
        valid = (found == static_cast<int>(FRLG_SECTORS));

        char buffer[128];
        snprintf(buffer, sizeof(buffer), "FRLG active slot @0x%05zX (counter A=%u%s B=%u%s)", slotBaseOffset,
                 slotACounter, slotAComplete ? "" : " incomplete", slotBCounter, slotBComplete ? "" : " incomplete");
        logInfoToFile(buffer);
    }

    void Trainer3FRLG::readBlock(int blockBaseId, size_t logical, uint8_t *destination, size_t length) const
    {
        for (size_t index = 0; index < length; ++index)
        {
            const size_t logicalOffset = logical + index;
            const int sectorId = blockBaseId + static_cast<int>(logicalOffset / FRLG_SECTOR_DATA);
            const size_t within = logicalOffset % FRLG_SECTOR_DATA;
            if (sectorId < 0 || sectorId >= static_cast<int>(FRLG_SECTORS))
            {
                destination[index] = 0;
                continue;
            }
            destination[index] = saveData[sectorOffset[sectorId] + within];
        }
    }

    void Trainer3FRLG::writeBlock(int blockBaseId, size_t logical, const uint8_t *source, size_t length)
    {
        for (size_t index = 0; index < length; ++index)
        {
            const size_t logicalOffset = logical + index;
            const int sectorId = blockBaseId + static_cast<int>(logicalOffset / FRLG_SECTOR_DATA);
            const size_t within = logicalOffset % FRLG_SECTOR_DATA;
            if (sectorId < 0 || sectorId >= static_cast<int>(FRLG_SECTORS))
                continue;
            saveData[sectorOffset[sectorId] + within] = source[index];
        }
    }

    std::unique_ptr<::Pokemon::Pokemon> Trainer3FRLG::readMon(int blockBaseId, size_t logical, size_t size) const
    {
        uint8_t scratch[100];
        if (size > sizeof(scratch))
            size = sizeof(scratch);
        readBlock(blockBaseId, logical, scratch, size);
        auto pokemon = std::make_unique<Pokemon3FRLG>(
            std::span<const std::byte>(reinterpret_cast<const std::byte *>(scratch), size));
        // Which charset this save is written in. An EGG needs it: its own language byte always says
        // Japanese, so nothing in the record can say how to read the OT name beside it.
        pokemon->setSaveLanguage(languageId);
        return pokemon;
    }

    /**
     * Decide which of the two Gen 3 character tables this save is written in.
     *
     * NOTHING IN A GEN 3 SAVE NAMES ITS LANGUAGE, so this is measured from the SHAPE of the trainer
     * name rather than read. The field is 8 bytes at Small+0x00 and the games cap the name at 7
     * characters internationally but 5 in Japanese, so a Japanese save never writes past byte 5 and
     * leaves the last two at zero, where an international one has a character, its 0xFF terminator
     * or leftover there. PKHeX's SAV3 and PKSM's Sav3 both settle it with exactly this test, and
     * both stop here too: the five international languages are indistinguishable from each other
     * (they share one table, and only the quote pair at 0xB1/0xB2 differs between them), so English
     * stands for all of them.
     *
     * Ref: PKHeX SAV3 `Japanese = ReadInt16LittleEndian(Small[0x6..]) == 0`.
     */
    void Trainer3FRLG::detectLanguage(size_t smallBlockOffset)
    {
        const bool japanese = readUInt16LittleEndian(&saveData[smallBlockOffset + 0x06]) == 0;
        languageId = static_cast<uint8_t>(japanese ? Enums::LanguageID::Japanese
                                                     : Enums::LanguageID::English);
    }

    void Trainer3FRLG::parseTrainer()
    {
        const size_t smallBlockOffset = sectorOffset[SMALL_ID];
        detectLanguage(smallBlockOffset);
        this->trainerName = g3Decode(&saveData[smallBlockOffset + 0x00], 7, languageId);
        this->TID16 = readUInt16LittleEndian(&saveData[smallBlockOffset + 0x0A]);
        this->SID16 = readUInt16LittleEndian(&saveData[smallBlockOffset + 0x0C]);
        this->trainerGender = saveData[smallBlockOffset + 0x08] & 1; // 0x08: player gender (0=M, 1=F)
        this->ID32 = readUInt32LittleEndian(&saveData[smallBlockOffset + 0x0A]);
        this->TID = this->TID16; // Gen 3's visible trainer ID is the raw 16-bit TID
        this->SID = this->SID16;
        this->saveSecurityKey = readUInt32LittleEndian(&saveData[smallBlockOffset + 0xF20]);

        uint8_t moneyBuf[4];
        readBlock(LARGE_ID, 0x290, moneyBuf, 4);
        this->money = readUInt32LittleEndian(moneyBuf) ^ saveSecurityKey;

        logInfoToFile("Parsed FRLG Trainer Name", this->trainerName.c_str());
    }

    void Trainer3FRLG::parseParty()
    {
        party.clear();
        uint8_t count = 0;
        readBlock(LARGE_ID, 0x034, &count, 1);
        if (count > MAX_PARTY_SLOTS)
            count = MAX_PARTY_SLOTS;
        for (uint8_t index = 0; index < count; ++index)
        {
            auto pokemon = readMon(LARGE_ID, 0x038 + static_cast<size_t>(index) * 100, 100);
            if (pokemon->speciesID() != 0)
                party.push_back(std::move(pokemon));
        }
    }

    void Trainer3FRLG::parseBoxes()
    {
        boxes.clear();
        boxes.resize(FRLG_BOX_COUNT);
        for (size_t fRLG_BOXIndex = 0; fRLG_BOXIndex < FRLG_BOX_COUNT; ++fRLG_BOXIndex)
        {
            for (size_t slotIndex = 0; slotIndex < FRLG_BOX_SLOTS; ++slotIndex)
            {
                const size_t index = fRLG_BOXIndex * FRLG_BOX_SLOTS + slotIndex;
                const size_t logical = 0x0004 + index * 80; // Storage: [0]=current-box byte, mons at +4
                auto pokemon = readMon(STORAGE_ID, logical, 80);
                if (pokemon->speciesID() != 0)
                    boxes[fRLG_BOXIndex][slotIndex] = std::move(pokemon);
            }
        }
    }

    bool Trainer3FRLG::canStoreBoxName(const std::string &name) const
    {
        // Gen 3's table is wide (247 bytes: letters, digits, accents, ♀/♂, common punctuation) but
        // it is not Unicode, so whatever the Switch keyboard adds past it must be refused up front
        // rather than dropped on write.
        uint8_t scratch[FRLG_BOX_NAME_BYTES];
        return g3Encode(name, scratch, FRLG_BOX_NAME_BYTES, FRLG_BOX_NAME_CHARS, languageId);
    }

    void Trainer3FRLG::updateBoxNameBlock()
    {
        // Inverse of parseBoxNames: 14 names of 9 bytes at Storage+0x8344, Gen 3 text, 0xFF
        // terminated. Must run BEFORE finalizeChecksums() -- the names sit inside the checksummed
        // sector data, so writing them afterwards would leave every storage sector's checksum stale
        // and the game would reject the save.
        for (size_t boxNameIndex = 0; boxNameIndex < FRLG_BOX_COUNT && boxNameIndex < boxNames.size(); ++boxNameIndex)
        {
            // never persist a display default
            if (!isBoxNameDirty(boxNameIndex)) continue;
            uint8_t nameBuf[FRLG_BOX_NAME_BYTES];
            // Seed with what's already in the save: g3Encode only writes the text and terminator,
            // deliberately leaving the bytes past it untouched (see g3Encode).
            readBlock(STORAGE_ID, FRLG_BOX_NAME_OFFSET + boxNameIndex * FRLG_BOX_NAME_BYTES,
                      nameBuf, FRLG_BOX_NAME_BYTES);
            // A name the Gen 3 table can't express is skipped rather than written mangled. The UI
            // validates before getting here, so this is a backstop, not the primary check.
            if (!g3Encode(boxNames[boxNameIndex], nameBuf, FRLG_BOX_NAME_BYTES, FRLG_BOX_NAME_CHARS, languageId))
                continue;
            writeBlock(STORAGE_ID, FRLG_BOX_NAME_OFFSET + boxNameIndex * FRLG_BOX_NAME_BYTES,
                       nameBuf, FRLG_BOX_NAME_BYTES);
        }
    }

    //
    // Gen 3 keeps two flag arrays indexed by (national dex number - 1): CAUGHT once, and SEEN in
    // **three** separate places -- one in the Small block and two mirrors in the Large block. The game
    // cross-checks them, so writing only the first leaves a dex that disagrees with itself. Offsets are
    // PKHeX's (SAV3.SetSeen/SetCaught + SaveBlock3LargeFRLG.SeenOffset2/3).
    //
    // 386 species need 49 bytes of flags. Read/modify/write the whole array once per location rather
    // than a byte per Pokemon, and only ever OR bits in -- never clear one (see updatePokedexBlock).
    void Trainer3FRLG::updatePokedexBlock()
    {
        constexpr size_t DEX_SMALL = 0x18;                  // Pokedex struct, Small block
        constexpr size_t OFS_CAUGHT = DEX_SMALL + 0x10;     // 0x28
        constexpr size_t OFS_SEEN_SMALL = DEX_SMALL + 0x44; // 0x5C
        constexpr size_t OFS_PID_UNOWN = DEX_SMALL + 0x04;  // 0x1C -- decides the letter the dex shows
        constexpr size_t OFS_PID_SPINDA = DEX_SMALL + 0x08; // 0x20 -- likewise, the spot pattern
        constexpr size_t OFS_SEEN_LARGE2 = 0x5F8;
        constexpr size_t OFS_SEEN_LARGE3 = 0x3A18;
        constexpr size_t FLAG_BYTES = 49; // ceil(386 / 8)
        constexpr uint16_t MAX_SPECIES3 = 386;

        uint8_t caught[FLAG_BYTES], seen[FLAG_BYTES], mirror2[FLAG_BYTES], mirror3[FLAG_BYTES];
        readBlock(SMALL_ID, OFS_CAUGHT, caught, FLAG_BYTES);
        readBlock(SMALL_ID, OFS_SEEN_SMALL, seen, FLAG_BYTES);
        readBlock(LARGE_ID, OFS_SEEN_LARGE2, mirror2, FLAG_BYTES);
        readBlock(LARGE_ID, OFS_SEEN_LARGE3, mirror3, FLAG_BYTES);
        // Start from the UNION of all three copies. They are supposed to agree, but if they have drifted
        // (a half-written save, an older tool) then rebuilding the mirrors from the Small array alone
        // would delete whatever the mirrors knew and the Small one didn't. Merging can only add.
        for (size_t index = 0; index < FLAG_BYTES; ++index)
            seen[index] |= static_cast<uint8_t>(mirror2[index] | mirror3[index]);

        // Unown's letter and Spinda's spots are drawn in the dex from a stored PID, not from the entity.
        // The games record the FIRST one seen and never revise it, so only write when the species is not
        // already flagged -- otherwise every save would repaint the dex from whatever is in box order.
        auto firstSeen = [&](uint16_t species)
        {
            const int bit = species - 1;
            return (seen[bit >> 3] & (1u << (bit & 7))) == 0;
        };

        auto registerMon = [&](const ::Pokemon::Pokemon *pokemon)
        {
            if (!pokemon)
                return;
            const uint16_t species = pokemon->speciesID();
            if (species == 0 || species > MAX_SPECIES3)
                return;
            // an egg is not seen or owned until it hatches
            if (pokemon->isEgg()) return;
            if (species == 201 || species == 327)
            { // Unown / Spinda
                if (firstSeen(species))
                {
                    uint8_t pidLE[4];
                    const uint32_t pidValue = pokemon->pid();
                    pidLE[0] = static_cast<uint8_t>(pidValue);
                    pidLE[1] = static_cast<uint8_t>(pidValue >> 8);
                    pidLE[2] = static_cast<uint8_t>(pidValue >> 16);
                    pidLE[3] = static_cast<uint8_t>(pidValue >> 24);
                    writeBlock(SMALL_ID, species == 201 ? OFS_PID_UNOWN : OFS_PID_SPINDA, pidLE, 4);
                }
            }
            const int bit = species - 1;
            caught[bit >> 3] |= static_cast<uint8_t>(1u << (bit & 7));
            seen[bit >> 3] |= static_cast<uint8_t>(1u << (bit & 7));
        };

        for (const auto &pokemon : party)
            registerMon(pokemon.get());
        for (const auto &box : boxes)
            for (const auto &pokemon : box)
                registerMon(pokemon.get());

        writeBlock(SMALL_ID, OFS_CAUGHT, caught, FLAG_BYTES);
        writeBlock(SMALL_ID, OFS_SEEN_SMALL, seen, FLAG_BYTES);
        // The two Large-block mirrors carry the SEEN array only; there is no second caught array.
        writeBlock(LARGE_ID, OFS_SEEN_LARGE2, seen, FLAG_BYTES);
        writeBlock(LARGE_ID, OFS_SEEN_LARGE3, seen, FLAG_BYTES);
    }

    void Trainer3FRLG::updateCurrentBoxBlock()
    {
        // Current box is one byte at Storage logical offset 0 (PKHeX SAV3.CurrentBox). Like the box
        // names it lives in the checksummed sector data, so this must run BEFORE finalizeChecksums().
        uint8_t currentBoxByte = static_cast<uint8_t>(currentBox);
        writeBlock(STORAGE_ID, 0, &currentBoxByte, 1);
    }

    void Trainer3FRLG::parseBoxNames()
    {
        boxNames.clear();
        for (size_t fRLG_BOXIndex = 0; fRLG_BOXIndex < FRLG_BOX_COUNT; ++fRLG_BOXIndex)
        {
            uint8_t nameBuf[9];
            readBlock(STORAGE_ID, 0x8344 + fRLG_BOXIndex * 9, nameBuf, 9);
            std::string name = g3Decode(nameBuf, 8, languageId);
            if (name.empty())
                name = "Box " + std::to_string(fRLG_BOXIndex + 1);
            boxNames.push_back(name);
        }
        // Current box: a single byte at Storage logical offset 0, clamped defensively.
        uint8_t currentBoxByte = 0;
        readBlock(STORAGE_ID, 0, &currentBoxByte, 1);
        if (currentBoxByte < FRLG_BOX_COUNT)
            currentBox = currentBoxByte;
    }

    void Trainer3FRLG::parseItems()
    {
        items.clear();
        items.resize(POUCH_COUNT3_FRLG);
        const uint16_t key16 = static_cast<uint16_t>(saveSecurityKey & 0xFFFF);
        for (size_t pouchIndex = 0; pouchIndex < POUCH_COUNT3_FRLG; ++pouchIndex)
        {
            const PouchInfo3FRLG &pi = getPouchInfo3FRLG(static_cast<PouchType3FRLG>(pouchIndex));
            for (int itemSlotIndex = 0; itemSlotIndex < pi.maxSlots; ++itemSlotIndex)
            {
                uint8_t entry[4];
                readBlock(LARGE_ID, pi.offset + itemSlotIndex * 4, entry, 4);
                const uint16_t itemId = readUInt16LittleEndian(entry);
                uint16_t count = readUInt16LittleEndian(entry + 2);
                if (pi.keyed)
                    count = static_cast<uint16_t>(count ^ key16);
                if (itemId != 0 && count != 0)
                    items[pouchIndex].push_back(InventoryItem{itemId, count, false, false});
            }
        }
    }

    void Trainer3FRLG::updatePartyBlock()
    {
        for (size_t partySlotIndex = 0; partySlotIndex < MAX_PARTY_SLOTS; ++partySlotIndex)
        {
            const size_t logical = 0x038 + partySlotIndex * 100;
            if (partySlotIndex < party.size() && party[partySlotIndex] && party[partySlotIndex]->speciesID() != 0)
            {
                uint8_t recordBytes[100];
                buildPartyRecord(*party[partySlotIndex], recordBytes);
                writeBlock(LARGE_ID, logical, recordBytes, 100);
            }
            else
            {
                // An empty Gen 3 party slot is NOT all zeros: the games leave 0xFF at 0x55, the
                // mail-id sentinel for "no mail". Zeroing it writes mail #0 into a slot that has
                // none. Measured, not assumed -- every unused slot in real Ruby, Emerald and
                // FireRed saves is zeros with exactly that one byte set.
                uint8_t blank[100] = {0}; // species 0; seed-0 crypt keeps the record clean
                blank[0x55] = 0xFF;       // mail id: none
                writeBlock(LARGE_ID, logical, blank, 100);
            }
        }
        const uint8_t count = static_cast<uint8_t>(std::min<size_t>(party.size(), MAX_PARTY_SLOTS));
        writeBlock(LARGE_ID, 0x034, &count, 1);
    }

    void Trainer3FRLG::updateBoxBlock()
    {
        for (size_t fRLG_BOXIndex = 0; fRLG_BOXIndex < FRLG_BOX_COUNT; ++fRLG_BOXIndex)
        {
            for (size_t slotIndex = 0; slotIndex < FRLG_BOX_SLOTS; ++slotIndex)
            {
                const size_t index = fRLG_BOXIndex * FRLG_BOX_SLOTS + slotIndex;
                const size_t logical = 0x0004 + index * 80;
                if (boxes[fRLG_BOXIndex][slotIndex] && boxes[fRLG_BOXIndex][slotIndex]->speciesID() != 0)
                {
                    uint8_t recordBytes[80];
                    buildBoxRecord(*boxes[fRLG_BOXIndex][slotIndex], recordBytes);
                    writeBlock(STORAGE_ID, logical, recordBytes, 80);
                }
                else
                {
                    uint8_t blank[80] = {0}; // Gen 3 empty box slot = all zero
                    writeBlock(STORAGE_ID, logical, blank, 80);
                }
            }
        }
    }

    void Trainer3FRLG::updateTrainerInfoBlock()
    {
        // Raw sector-mapped save: OT name (g3-encoded, 7 chars @ 0x00) lives in the Small sector; money
        // in the Large block at 0x290, XOR-keyed with the security key. finalizeChecksums() runs after.
        // g3Encode writes 7 chars + a 0xFF terminator into 8 bytes, stopping before the gender byte at
        // 0x08; the UI has already rejected any name the Gen-3 glyph table can't store.
        const size_t smallBlockOffset = sectorOffset[SMALL_ID];
        if (smallBlockOffset + 0x09 <= saveData.size())
            g3Encode(this->trainerName, &saveData[smallBlockOffset + 0x00], 8, 7, languageId);
        uint8_t moneyBuf[4];
        writeUInt32LittleEndian(moneyBuf, this->money ^ saveSecurityKey);
        writeBlock(LARGE_ID, 0x290, moneyBuf, 4);
    }

    void Trainer3FRLG::updateItemBlock()
    {
        const uint16_t key16 = static_cast<uint16_t>(saveSecurityKey & 0xFFFF);
        for (size_t pouchIndex = 0; pouchIndex < items.size() && pouchIndex < POUCH_COUNT3_FRLG; ++pouchIndex)
        {
            const PouchInfo3FRLG &pi = getPouchInfo3FRLG(static_cast<PouchType3FRLG>(pouchIndex));
            int slot = 0;
            for (const auto &it : items[pouchIndex])
            {
                if (slot >= pi.maxSlots)
                    break;
                uint8_t entry[4];
                writeUInt16LittleEndian(entry, it.itemId);
                const uint16_t stored = pi.keyed ? static_cast<uint16_t>(it.count ^ key16) : it.count;
                writeUInt16LittleEndian(entry + 2, stored);
                writeBlock(LARGE_ID, pi.offset + slot * 4, entry, 4);
                ++slot;
            }
            // Zero-fill the rest: empty keyed slots store the key (so the decoded count is 0), matching the game.
            for (; slot < pi.maxSlots; ++slot)
            {
                uint8_t entry[4];
                writeUInt16LittleEndian(entry, 0);
                writeUInt16LittleEndian(entry + 2, pi.keyed ? key16 : 0);
                writeBlock(LARGE_ID, pi.offset + slot * 4, entry, 4);
            }
        }
        // Money is written by updateTrainerInfoBlock(), which the save flow calls alongside this;
        // keeping it there avoids a double-write of the same XOR-keyed value.
    }

    std::unique_ptr<::Pokemon::Pokemon> Trainer3FRLG::createBlankPokemon() const
    {
        // A zeroed PK3 is a valid empty entity in Gen 3: PID 0 => seed 0 (identity XOR) + shuffle 0
        // (identity), so decrypt leaves zeros, species 0, checksum 0 (valid). Party-sized so the creator
        // can place it into either party or a box.
        std::vector<std::byte> zero(100, std::byte{0});
        auto pokemon = std::make_unique<Pokemon3FRLG>(std::span<const std::byte>(zero.data(), zero.size()));
        pokemon->setSaveLanguage(languageId); // the save's language picks the Gen 3 character table
        return pokemon;
    }

    const std::vector<uint8_t> &Trainer3FRLG::serialize()
    {
        updateItemBlock();
        updateBoxBlock();
        updateBoxNameBlock();
        updateCurrentBoxBlock();
        updatePartyBlock();
        updatePokedexBlock();
        updateTrainerInfoBlock();
        finalizeChecksums(); // last: it checksums whatever the writers above left behind
        return saveData;
    }

    void Trainer3FRLG::finalizeChecksums()
    {
        // PKHeX FRLG per-sector checksum lengths (id 0 / 4 / 13 are short; the rest full 0xF80).
        static const size_t CHUNK[FRLG_SECTORS] = {
            0xF2C, 0xF80, 0xF80, 0xF80, 0xF08, 0xF80, 0xF80,
            0xF80, 0xF80, 0xF80, 0xF80, 0xF80, 0xF80, 0x7D0};
        for (int sectorId = 0; sectorId < static_cast<int>(FRLG_SECTORS); ++sectorId)
        {
            const size_t byteOffset = sectorOffset[sectorId];
            uint32_t sum = 0;
            for (size_t offset = 0; offset + 4 <= CHUNK[sectorId]; offset += 4)
                sum += readUInt32LittleEndian(&saveData[byteOffset + offset]);
            const uint16_t sectorChecksum = static_cast<uint16_t>((sum + (sum >> 16)) & 0xFFFF);
            writeUInt16LittleEndian(&saveData[byteOffset + 0xFF6], sectorChecksum);
        }
    }
}
