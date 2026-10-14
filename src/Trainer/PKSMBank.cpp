#include "Trainer/PKSMBank.h"

#include <algorithm>
#include <cstring>
#include <utility>

#include "Trainer/Bank.h"
#include "Pokemon/Pokemon1RBY.h"
#include "Pokemon/Pokemon2GSC.h"
#include "Pokemon/Pokemon3FRLG.h"
#include "Pokemon/Pokemon4HGSS.h"
#include "Pokemon/Pokemon5B2W2.h"
#include "Pokemon/Pokemon6ORAS.h"
#include "Pokemon/Pokemon7LGPE.h"
#include "Pokemon/Pokemon7USUM.h"
#include "Pokemon/Pokemon8SWSH.h"
#include "Pokemon/Experience.h"
#include "Encryption/Encryption3FRLG.h"
#include "Encryption/Encryption4HGSS.h"
#include "Encryption/Encryption5B2W2.h"
#include "Encryption/Encryption6ORAS.h"
#include "Encryption/Encryption7LGPE.h"
#include "Encryption/Encryption7USUM.h"
#include "Encryption/Encryption8SWSH.h"
#include "Utils/HelperUtilities.h"
#include "Utils/Logger.h"

using namespace Utils;
using namespace Encryption;

namespace Trainer
{
    namespace
    {
        // v3 (current): 16-byte header, 336-byte entries with a 328-byte payload.
        // v1/v2 (pre-2019 upgrades, only seen in old backups): 264-byte entries, 260-byte payload,
        // and v1's header is 12 bytes with NO box count -- it has to be derived from the file size.
        constexpr uint8_t MAGIC[8] = {'P', 'K', 'S', 'M', 'B', 'A', 'N', 'K'};
        constexpr size_t SLOTS_PER_BOX = 30; // always 30, every version, game-independent
        constexpr size_t V3_HEADER = 16;
        constexpr size_t V3_ENTRY = 0x150;   // 336
        constexpr size_t V3_PAYLOAD = 0x148; // 328 -- sized for a Gen 8 box record
        constexpr size_t V12_ENTRY = 264;
        constexpr size_t V12_PAYLOAD = 260;
        constexpr size_t V1_HEADER = 12;
        constexpr uint32_t EMPTY_TAG = 0xFFFFFFFFu;
        // The headerless pre-2019 bank.bin is nothing but back-to-back 232-byte Gen 6 box records.
        constexpr size_t LEGACY_BIN_RECORD = 232;

        // PKSM stores BOX records, so every length below is the box length of that format. These
        // assert PKSE's own geometry against the lengths transcribed from PKSM (FUTURE_VERSIONS
        // Appendix A.1): the two are independent descriptions of one on-disk fact, and a silent
        // disagreement would read every record of that generation at the wrong stride.
        static_assert(SIZE_STORED3_FRLG == 80, "PKSM stores a PK3 box record as 80 bytes");
        static_assert(SIZE_STORED4_HGSS == 136, "PKSM stores a PK4 box record as 136 bytes");
        static_assert(SIZE_STORED5_B2W2 == 136, "PKSM stores a PK5 box record as 136 bytes");
        static_assert(SIZE_STORED6_ORAS == 232, "PKSM stores a PK6 box record as 232 bytes");
        static_assert(SIZE_STORED7_USUM == 232, "PKSM stores a PK7 box record as 232 bytes");
        static_assert(SIZE_STORED7_LGPE == 232, "PKSM stores a PB7 box record as 232 bytes");
        static_assert(SIZE_STORED8_SWSH == 328, "PKSM stores a PK8 box record as 328 bytes");

        /// True for the two formats PKSM stores as a POKELIST -- header + party body + two name
        /// fields -- rather than as a bare box record. They share a shape that differs from every
        /// other tag here: no encryption, no box->party growth, a locale-dependent length, and
        /// battle stats the game itself computed already present in the payload.
        bool isPokeListFormat(uint32_t bankTag) noexcept
        {
            const PKSMGen generation = static_cast<PKSMGen>(bankTag);
            return generation == PKSMGen::Gen1 || generation == PKSMGen::Gen2;
        }

        /// Meaningful bytes of a record, per tag. 0 = a generation PKSE has no format for, which
        /// is counted and reported rather than imported.
        size_t payloadLengthFor(uint32_t bankTag)
        {
            switch (static_cast<PKSMGen>(bankTag))
            {
            case PKSMGen::Gen3:
                return SIZE_STORED3_FRLG;
            case PKSMGen::Gen4:
                return SIZE_STORED4_HGSS;
            case PKSMGen::Gen5:
                return SIZE_STORED5_B2W2;
            case PKSMGen::Gen6:
                return SIZE_STORED6_ORAS;
            case PKSMGen::Gen7:
                return SIZE_STORED7_USUM;
            case PKSMGen::LGPE:
                return SIZE_STORED7_LGPE;
            case PKSMGen::Gen8:
                return SIZE_STORED8_SWSH;
            // Gen 1's record is 69 bytes international and 59 Japanese, Gen 2's 73 and 63. Both
            // report the LONGER one, which is what "does PKSE have a format for this tag" and the
            // bounds check need; the actual length comes from probePokeListLength() below.
            case PKSMGen::Gen1:
                return ::Pokemon::SIZE_1ULIST;
            case PKSMGen::Gen2:
                return ::Pokemon::SIZE_2ULIST;
            default:
                return 0;
            }
        }

        /**
         * Recovers a PokeList record's true length from the payload.
         *
         * Gens 1 and 2 are the formats here whose record length is not fixed, and the length is
         * the ONLY thing that says whether the name fields are 11 bytes or 6 -- get it wrong and
         * the names are read from the wrong offsets, which yields a plausible wrong name rather
         * than an error. PKSM stores no locale marker anywhere: the four bytes past the 328-byte
         * payload are uninitialised heap (the bank this was tested against has the ASCII "tran"
         * sitting in them on 51 records). What PKSM does do is write the record's own length and
         * 0xFF-fill the rest of the payload, so the boundary of that fill is the signal.
         *
         * The test is "is every byte between the Japanese and international lengths 0xFF", and it
         * is not the naive one it looks like. 0xFF is a real Gen 1/2 character ('9'), and the
         * bytes after a name's terminator are trash the games never cleared, so an isolated 0xFF
         * proves nothing. What makes the run decisive is where it sits: for an INTERNATIONAL
         * record those ten bytes are the tail of the nickname field, and a nickname is at most 10
         * characters in an 11-byte field, so its terminator ALWAYS lands at or before the last of
         * them. For all ten to be 0xFF the nickname would have to be empty AND its trash all-0xFF
         * -- a combination the games do not produce. A Japanese record has its 0xFF padding there
         * by construction.
         *
         * Both generations share this because they share the geometry: Gen 1 is 59/69 and Gen 2
         * is 63/73, in each case a 10-byte gap that is the international nickname's tail. Two
         * copies of reasoning this delicate would eventually disagree.
         */
        size_t probePokeListLength(const uint8_t *payload, size_t payloadSize, size_t japaneseLength, size_t internationalLength)
        {
            if (payloadSize < internationalLength)
                return 0;
            for (size_t index = japaneseLength; index < internationalLength; ++index)
            {
                if (payload[index] != 0xFF)
                    return internationalLength;
            }
            return japaneseLength;
        }

        /// Japanese/international length pair for a PokeList tag; {0, 0} for anything else.
        void pokeListLengths(uint32_t bankTag, size_t &japaneseLength, size_t &internationalLength)
        {
            if (static_cast<PKSMGen>(bankTag) == PKSMGen::Gen1)
            {
                japaneseLength = ::Pokemon::SIZE_1JLIST;
                internationalLength = ::Pokemon::SIZE_1ULIST;
                return;
            }
            japaneseLength = ::Pokemon::SIZE_2JLIST;
            internationalLength = ::Pokemon::SIZE_2ULIST;
        }

        inline uint32_t rd32(const uint8_t *bytes) { return readUInt32LittleEndian(bytes); }
        inline uint16_t rd16(const uint8_t *bytes) { return readUInt16LittleEndian(bytes); }

        /**
         * Builds a PKSE entity from one decrypted PKSM record.
         *
         * Three things happen here, and all three are required:
         *  1. The record is grown from PKSM's BOX length to PKSE's PARTY length. PK8's level and
         *     battle stats live in that tail and its accessors index straight into it, so a
         *     stored-size buffer would read past the allocation.
         *  2. PK8's level byte is seeded from EXP, because recalculateStats() reads level() first
         *     and bails on 0 -- it needs a real level before it can compute against one.
         *  3. The buffer is ENCRYPTED, because every entity constructor decrypts what it is handed
         *     and PKSM stores plaintext. Gen 3 gets its substructure shuffle restored by the same
         *     call, which PKSM also undoes.
         */
        std::unique_ptr<::Pokemon::Pokemon> buildEntity(uint32_t bankTag, const uint8_t *payload, size_t length)
        {
            // Gens 1 and 2 leave before any of this. Neither has encryption to re-apply, neither
            // needs box->party growth (a PokeList already carries the party body), and neither has
            // a checksum to refresh -- so the record is handed over exactly as PKSM stored it,
            // which is also what makes the bank's byte-in == byte-out contract hold for them.
            if (static_cast<PKSMGen>(bankTag) == PKSMGen::Gen1)
            {
                if (!::Pokemon::isGen1ListSize(length))
                    return nullptr;
                return std::make_unique<::Pokemon::Pokemon1RBY>(std::span<const std::byte>(reinterpret_cast<const std::byte *>(payload), length));
            }
            if (static_cast<PKSMGen>(bankTag) == PKSMGen::Gen2)
            {
                if (!::Pokemon::isGen2ListSize(length))
                    return nullptr;
                return std::make_unique<::Pokemon::Pokemon2GSC>(std::span<const std::byte>(reinterpret_cast<const std::byte *>(payload), length));
            }

            std::vector<std::byte> decryptedRecord;
            size_t partySize = 0;

            switch (static_cast<PKSMGen>(bankTag))
            {
            case PKSMGen::Gen3:
                partySize = SIZE_PARTY3_FRLG;
                break; // 100
            case PKSMGen::Gen4:
                partySize = SIZE_PARTY4_HGSS;
                break;
            case PKSMGen::Gen5:
                partySize = SIZE_PARTY5_B2W2;
                break; // 220
            case PKSMGen::Gen6:
                partySize = SIZE_PARTY6_ORAS;
                break;
            case PKSMGen::Gen7:
                partySize = SIZE_PARTY7_USUM;
                break;
            case PKSMGen::LGPE:
                partySize = SIZE_PARTY7_LGPE;
                break;
            case PKSMGen::Gen8:
                partySize = SIZE_PARTY8_SWSH;
                break; // 344
            default:
                return nullptr;
            }
            if (length == 0 || length > partySize)
                return nullptr;

            decryptedRecord.assign(partySize, std::byte{0});
            std::memcpy(decryptedRecord.data(), payload, length);

            // Gen 8 alone needs its level seeded by hand. Every one of these formats keeps level in
            // the party tail the growth above just zeroed, but Pokemon4HGSS/5B2W2/6ORAS/7USUM each
            // fall back to deriving it from EXP inside recalculateStats() and write the byte back,
            // so the zero repairs itself. Pokemon8SWSH::level() reads 0x148 raw with no such
            // fallback, and recalculateStats() bails on a level of 0 -- leaving a Level 0 import.
            if (static_cast<PKSMGen>(bankTag) == PKSMGen::Gen8)
            {
                const uint16_t species = rd16(payload + 0x08);
                const uint32_t expv = rd32(payload + 0x10);
                decryptedRecord[0x148] = static_cast<std::byte>(::Pokemon::getLevelFromExp(expv, ::Pokemon::getGrowthRate(species)));
            }

            const uint32_t encryptionConstant = rd32(payload); // encryption constant / PID, the crypto seed
            std::span<const std::byte> decSpan(decryptedRecord.data(), decryptedRecord.size());
            std::byte *encryptedRecord = nullptr;
            switch (static_cast<PKSMGen>(bankTag))
            {
            case PKSMGen::Gen3:
                encryptedRecord = encryptArray3FRLG(decSpan);
                break; // Gen 3 keys off the in-buffer PID
            // Gens 4 and 5 take no explicit constant: like Gen 3 they seed from the decrypted
            // buffer itself (PID and checksum). PKSM stores the record with the game's own
            // checksum intact and the box->party growth above only appends a zeroed tail, which
            // sits outside the checksummed block region -- so that seed is still the right one.
            case PKSMGen::Gen4:
                encryptedRecord = encryptArray4HGSS(decSpan);
                break;
            case PKSMGen::Gen5:
                encryptedRecord = encryptArray5B2W2(decSpan);
                break;
            case PKSMGen::Gen6:
                encryptedRecord = encryptArray6ORAS(decSpan, encryptionConstant);
                break;
            case PKSMGen::Gen7:
                encryptedRecord = encryptArray7USUM(decSpan, encryptionConstant);
                break;
            case PKSMGen::LGPE:
                encryptedRecord = encryptArray7LGPE(decSpan, encryptionConstant);
                break;
            case PKSMGen::Gen8:
                encryptedRecord = encryptArray8SWSH(decSpan, encryptionConstant);
                break;
            default:
                return nullptr;
            }
            if (!encryptedRecord)
                return nullptr;

            std::span<const std::byte> encSpan(encryptedRecord, partySize);
            std::unique_ptr<::Pokemon::Pokemon> pokemon;
            switch (static_cast<PKSMGen>(bankTag))
            {
            // A PKSM tag names an entity FORMAT, and several PKSE groups share each one, so the
            // group built here is the format's REPRESENTATIVE -- exactly what Bank's own
            // groupForBankTag() answers for the equivalent tag (PK4 -> HGSS, PK5 -> B2W2,
            // PK6 -> ORAS, PK7 -> USUM). Picking the superset group keeps the importer and the
            // bank from disagreeing about what a stored record is.
            case PKSMGen::Gen3:
                pokemon = std::make_unique<::Pokemon::Pokemon3FRLG>(encSpan);
                break;
            case PKSMGen::Gen4:
                pokemon = std::make_unique<::Pokemon::Pokemon4HGSS>(encSpan);
                break;
            case PKSMGen::Gen5:
                pokemon = std::make_unique<::Pokemon::Pokemon5B2W2>(encSpan);
                break;
            case PKSMGen::Gen6:
                pokemon = std::make_unique<::Pokemon::Pokemon6ORAS>(encSpan);
                break;
            case PKSMGen::Gen7:
                pokemon = std::make_unique<::Pokemon::Pokemon7USUM>(encSpan);
                break;
            case PKSMGen::LGPE:
                pokemon = std::make_unique<::Pokemon::Pokemon7LGPE>(encSpan);
                break;
            case PKSMGen::Gen8:
                pokemon = std::make_unique<::Pokemon::Pokemon8SWSH>(encSpan);
                break;
            default:
                break;
            }
            delete[] encryptedRecord;
            return pokemon;
        }

        /// UTF-8 length cap that never splits a multi-byte sequence.
        void truncateUtf8(std::string &text, size_t maxBytes)
        {
            if (text.size() <= maxBytes)
                return;
            size_t cutPoint = maxBytes;
            // back off continuation bytes
            while (cutPoint > 0 && (static_cast<unsigned char>(text[cutPoint]) & 0xC0) == 0x80) --cutPoint;
            text.resize(cutPoint);
        }

        void appendUtf8(std::string &out, uint32_t codePoint)
        {
            // An unpaired surrogate would encode to invalid UTF-8, which the NanoVG text layer then
            // has to guess at. Substitute U+FFFD so a malformed name degrades to a visible glyph
            // rather than to undefined rendering.
            if ((codePoint >= 0xD800 && codePoint <= 0xDFFF) || codePoint > 0x10FFFF)
                codePoint = 0xFFFD;
            if (codePoint < 0x80)
            {
                out += static_cast<char>(codePoint);
            }
            else if (codePoint < 0x800)
            {
                out += static_cast<char>(0xC0 | (codePoint >> 6));
                out += static_cast<char>(0x80 | (codePoint & 0x3F));
            }
            else if (codePoint < 0x10000)
            {
                out += static_cast<char>(0xE0 | (codePoint >> 12));
                out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (codePoint & 0x3F));
            }
            else
            {
                out += static_cast<char>(0xF0 | (codePoint >> 18));
                out += static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F));
                out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (codePoint & 0x3F));
            }
        }
    }

    const char *pksmGenName(uint32_t bankTag)
    {
        switch (static_cast<PKSMGen>(bankTag))
        {
        case PKSMGen::Gen1:
            return "Gen 1";
        case PKSMGen::Gen2:
            return "Gen 2";
        case PKSMGen::Gen3:
            return "Gen 3";
        case PKSMGen::Gen4:
            return "Gen 4";
        case PKSMGen::Gen5:
            return "Gen 5";
        case PKSMGen::Gen6:
            return "Gen 6";
        case PKSMGen::Gen7:
            return "Gen 7";
        case PKSMGen::LGPE:
            return "Let's Go";
        case PKSMGen::Gen8:
            return "Gen 8";
        default:
            return "Unknown";
        }
    }

    bool pksmGenImportable(uint32_t bankTag)
    {
        return payloadLengthFor(bankTag) != 0;
    }

    void PKSMBankImport::clear()
    {
        staged.clear();
        srcBoxNames.clear();
        importReport = PKSMImportReport{};
    }

    bool PKSMBankImport::scan(std::span<const uint8_t> file)
    {
        clear();

        const size_t size = file.size();
        const uint8_t *data = file.data();

        if (size < V1_HEADER || std::memcmp(data, MAGIC, 8) != 0)
        {
            // Identify by CONTENT, not extension. The original pre-2019 bank had no header at all --
            // just back-to-back Gen 6 box records -- so name it rather than calling the file garbage.
            if (size >= LEGACY_BIN_RECORD && size % LEGACY_BIN_RECORD == 0 && (size / LEGACY_BIN_RECORD) % SLOTS_PER_BOX == 0)
            {
                importReport.error = "This is a pre-2019 PKSM bank.bin. It holds Gen 6 records only, which PKSE has no format for.";
            }
            else
            {
                importReport.error = "Not a PKSM bank file (the PKSMBANK marker is missing).";
            }
            return false;
        }

        const uint32_t version = rd32(data + 8);
        size_t headerSize = 0, entrySize = 0, payloadSize = 0, boxes = 0;
        switch (version)
        {
        case 3:
            headerSize = V3_HEADER;
            entrySize = V3_ENTRY;
            payloadSize = V3_PAYLOAD;
            boxes = (size >= 16) ? rd32(data + 12) : 0;
            break;
        case 2:
            headerSize = V3_HEADER;
            entrySize = V12_ENTRY;
            payloadSize = V12_PAYLOAD;
            boxes = (size >= 16) ? rd32(data + 12) : 0;
            break;
        case 1:
            // v1 predates the box-count field entirely; the entry table IS the whole file.
            headerSize = V1_HEADER;
            entrySize = V12_ENTRY;
            payloadSize = V12_PAYLOAD;
            boxes = (size - V1_HEADER) / (V12_ENTRY * SLOTS_PER_BOX);
            break;
        default:
            importReport.error = "Unsupported PKSM bank version " + std::to_string(version) + " -- it was written by a newer PKSM than this build knows about.";
            return false;
        }
        importReport.containerVersion = version;
        importReport.headerBoxes = boxes;

        // Trust the header, but never past what the file can actually hold: a truncated or damaged
        // header would otherwise walk the entry loop off the end of the buffer. Whole entries only.
        const size_t availEntries = (size > headerSize) ? (size - headerSize) / entrySize : 0;
        const size_t availBoxes = availEntries / SLOTS_PER_BOX;
        if (boxes == 0 || boxes > availBoxes)
            boxes = availBoxes;
        importReport.sourceBoxes = boxes;

        if (boxes == 0)
        {
            importReport.error = "The bank file is empty or truncated -- it holds no complete boxes.";
            return false;
        }

        for (size_t index = 0; index < boxes * SLOTS_PER_BOX; ++index)
        {
            const uint8_t *entry = data + headerSize + index * entrySize;
            const uint32_t bankTag = rd32(entry);
            // 0xFF-filled = unused slot
            if (bankTag == EMPTY_TAG) continue;

            // Counted before the tag is validated, so that importable + unsupported + damaged always
            // adds up to occupied. A slot with a nonsense tag still holds SOMETHING, and reporting it
            // as neither stored nor skipped is how a record goes missing without anyone noticing.
            ++importReport.occupied;
            if (bankTag >= static_cast<uint32_t>(PKSM_GEN_COUNT))
            { // not a tag PKSM ever writes
                ++importReport.damaged;
                continue;
            }
            ++importReport.perGen[bankTag];

            size_t length = payloadLengthFor(bankTag);
            if (length == 0)
            {
                ++importReport.unsupported;
                continue;
            } // a generation PKSE cannot house
            if (length > payloadSize)
            {
                ++importReport.damaged;
                continue;
            } // record longer than this container's payload

            const uint8_t *payload = entry + 4;
            if (isPokeListFormat(bankTag))
            {
                size_t japaneseLength = 0;
                size_t internationalLength = 0;
                pokeListLengths(bankTag, japaneseLength, internationalLength);
                // Gen 1: 69 or 59. Gen 2: 73 or 63. See the probe's note.
                length = probePokeListLength(payload, payloadSize, japaneseLength, internationalLength);
                if (length == 0)
                {
                    ++importReport.damaged;
                    continue;
                }
            }
            auto pokemon = buildEntity(bankTag, payload, length);

            // Bytes off somebody else's SD card are not trusted. isStructurallyValid() is the
            // decisive test: for every checksummed format it IS the checksum, and each one's
            // checksummed region sits entirely inside the bytes PKSM stored, so it catches a
            // corrupt record AND a misparsed one (a wrong stride lands on the wrong bytes and will
            // not checksum). Gens 1 and 2 carry no checksum at all, which is exactly why that
            // virtual exists -- they answer with their own structural test instead.
            if (!pokemon || pokemon->speciesID() == 0 || !pokemon->isStructurallyValid())
            {
                ++importReport.damaged;
                continue;
            }

            // The battle-stat tail was zeroed by the box -> party growth above, so fill it. Without
            // this a Gen 8 import shows Level 0 and a Let's Go one shows Level/CP 0.
            //
            // Gens 1 and 2 are skipped, deliberately. Nothing was grown -- a PokeList is already
            // the party form and arrives with the five battle stats the game itself computed -- so
            // there is no gap to fill, and recomputing would OVERWRITE what was stored. That
            // matters beyond tidiness: silently rewriting a tampered record's stats to the values
            // they should have had repairs the evidence out from under the legality checker, which
            // is the one thing that should be reporting it. Neither format has a checksum to
            // refresh either.
            if (!isPokeListFormat(bankTag))
            {
                pokemon->recalculateStats();
                pokemon->refreshChecksum(); // recalc can touch current HP, which IS inside the checksum
            }

            if (static_cast<PKSMGen>(bankTag) == PKSMGen::Gen3)
            {
                // PK3 is one format across all five GBA games, so a PKSM bank holds Ruby/Sapphire/
                // Emerald mons beside the FireRed/LeafGreen ones -- and PKSM-generated ones are
                // often stamped with an origin that is no Gen 3 game at all (the real bank this was
                // tested against has 250 records split between version 7 and version 9). None of
                // that is a reason to refuse them: the bank tag names the storage FORMAT, not the
                // origin, PK3 is byte-identical across the five games, and trading one into FireRed
                // is exactly what the real hardware does. Just say how many, because PKSE has no
                // encounter tables for those origins and the legality checker will report as much.
                const uint8_t originGameId = pokemon->originGame();
                if (originGameId != 4 && originGameId != 5)
                    ++importReport.gen3NonFRLG;
            }

            Staged stagedRecord;
            stagedRecord.srcBox = static_cast<uint16_t>(index / SLOTS_PER_BOX);
            stagedRecord.srcSlot = static_cast<uint8_t>(index % SLOTS_PER_BOX);
            stagedRecord.pokemon = std::move(pokemon);
            if (staged.empty() || staged.back().srcBox != stagedRecord.srcBox)
                ++importReport.sourceBoxesUsed;
            staged.push_back(std::move(stagedRecord));
            ++importReport.importable;
        }

        logInfoToFile("PKSM bank scanned", ("v" + std::to_string(version) + " boxes=" + std::to_string(importReport.sourceBoxes) + " occupied=" + std::to_string(importReport.occupied) + " importable=" + std::to_string(importReport.importable) + " unsupported=" + std::to_string(importReport.unsupported) + " damaged=" + std::to_string(importReport.damaged)).c_str());
        return true;
    }

    size_t PKSMBankImport::commit(Bank &bank)
    {
        importReport.placed = importReport.overflow = importReport.boxesUsed = importReport.namesCarried = importReport.scattered = 0;
        if (staged.empty())
            return 0;

        const size_t boxCount = Bank::BANK_BOX_COUNT;
        const size_t slotCount = Bank::BANK_SLOTS_PER_BOX;

        // A destination box is available only if it is COMPLETELY empty -- a whole source box lands
        // in it at the original slot positions, so a single occupant would be overwritten.
        auto boxIsEmpty = [&](size_t boxIndex)
        {
            for (size_t slotIndex = 0; slotIndex < slotCount; ++slotIndex)
                if (bank.boxes[boxIndex][slotIndex])
                    return false;
            return true;
        };

        size_t nextBox = 0;             // scan cursor for whole-box placement
        size_t fitBox = 0, fitSlot = 0; // scan cursor for the first-fit fallback

        // Staged entries are in ascending (box, slot) order, so one pass groups them by source box.
        size_t readIndex = 0;
        while (readIndex < staged.size())
        {
            const uint16_t srcBox = staged[readIndex].srcBox;
            size_t scanIndex = readIndex;
            while (scanIndex < staged.size() && staged[scanIndex].srcBox == srcBox)
                ++scanIndex;

            while (nextBox < boxCount && !boxIsEmpty(nextBox))
                ++nextBox;

            if (nextBox < boxCount)
            {
                // Whole-box placement: same slot positions, and the source box's name comes too.
                for (size_t thirdIndex = readIndex; thirdIndex < scanIndex; ++thirdIndex)
                    bank.boxes[nextBox][staged[thirdIndex].srcSlot] = std::move(staged[thirdIndex].pokemon);
                importReport.placed += (scanIndex - readIndex);
                ++importReport.boxesUsed;

                if (srcBox < srcBoxNames.size() && !srcBoxNames[srcBox].empty())
                {
                    std::string boxName = srcBoxNames[srcBox];
                    truncateUtf8(boxName, Bank::MAX_BOX_NAME_LEN * 4);
                    if (!boxName.empty())
                    {
                        bank.boxNames[nextBox] = boxName;
                        ++importReport.namesCarried;
                    }
                }
                ++nextBox;
            }
            else
            {
                // Out of empty boxes. Scatter the rest into whatever slots are free rather than
                // stopping: a half-import cannot be resumed (a re-run has no memory of what it
                // already took, so it would duplicate), and dropping them silently is worse still.
                for (size_t thirdIndex = readIndex; thirdIndex < scanIndex; ++thirdIndex)
                {
                    while (fitBox < boxCount && fitSlot >= slotCount)
                    {
                        fitSlot = 0;
                        ++fitBox;
                    }
                    while (fitBox < boxCount && bank.boxes[fitBox][fitSlot])
                    {
                        if (++fitSlot >= slotCount)
                        {
                            fitSlot = 0;
                            ++fitBox;
                        }
                    }
                    if (fitBox >= boxCount)
                    {
                        ++importReport.overflow;
                        continue;
                    }
                    bank.boxes[fitBox][fitSlot] = std::move(staged[thirdIndex].pokemon);
                    ++fitSlot;
                    ++importReport.placed;
                    ++importReport.scattered;
                }
            }
            readIndex = scanIndex;
        }

        staged.clear(); // consumed -- a second commit() must not place anything twice
        logInfoToFile("PKSM bank import committed", ("placed=" + std::to_string(importReport.placed) + " boxes=" + std::to_string(importReport.boxesUsed) + " scattered=" + std::to_string(importReport.scattered) + " overflow=" + std::to_string(importReport.overflow) + " names=" + std::to_string(importReport.namesCarried)).c_str());
        return importReport.placed;
    }

    std::vector<std::string> parsePKSMBoxNames(std::span<const uint8_t> json)
    {
        std::vector<std::string> out;

        size_t byteCount = json.size();
        // PKSM writes length+1 bytes: strip the trailing NUL
        while (byteCount > 0 && json[byteCount - 1] == 0) --byteCount;
        if (byteCount == 0)
            return out;

        const char *jsonBytes = reinterpret_cast<const char *>(json.data());
        size_t readIndex = 0;
        auto skipWs = [&]()
        {
            while (readIndex < byteCount && (jsonBytes[readIndex] == ' ' || jsonBytes[readIndex] == '\t' || jsonBytes[readIndex] == '\r' || jsonBytes[readIndex] == '\n'))
                ++readIndex;
        };

        skipWs();
        // not the array we expect -> no names, not an error
        if (readIndex >= byteCount || jsonBytes[readIndex] != '[') return out;
        ++readIndex;

        while (readIndex < byteCount)
        {
            skipWs();
            if (readIndex >= byteCount)
                break;
            if (jsonBytes[readIndex] == ']')
                break;
            if (jsonBytes[readIndex] == ',')
            {
                ++readIndex;
                continue;
            }
            // anything else means this isn't an array of strings
            if (jsonBytes[readIndex] != '"') break;
            ++readIndex;

            std::string text;
            bool closed = false;
            while (readIndex < byteCount)
            {
                const char character = jsonBytes[readIndex++];
                if (character == '"')
                {
                    closed = true;
                    break;
                }
                if (character != '\\')
                {
                    text += character;
                    continue;
                }
                if (readIndex >= byteCount)
                    break;
                const char escapeChar = jsonBytes[readIndex++];
                switch (escapeChar)
                {
                case 'n':
                    text += '\n';
                    break;
                case 't':
                    text += '\t';
                    break;
                case 'r':
                    text += '\r';
                    break;
                case 'b':
                    text += '\b';
                    break;
                case 'f':
                    text += '\f';
                    break;
                case 'u':
                {
                    // \uXXXX, with the surrogate pair that a non-BMP name needs.
                    auto hex4 = [&](uint32_t &value)
                    {
                        if (readIndex + 4 > byteCount)
                            return false;
                        value = 0;
                        for (int moveIndex = 0; moveIndex < 4; ++moveIndex)
                        {
                            const char hexDigit = jsonBytes[readIndex + moveIndex];
                            value <<= 4;
                            if (hexDigit >= '0' && hexDigit <= '9')
                                value |= static_cast<uint32_t>(hexDigit - '0');
                            else if (hexDigit >= 'a' && hexDigit <= 'f')
                                value |= static_cast<uint32_t>(hexDigit - 'a' + 10);
                            else if (hexDigit >= 'A' && hexDigit <= 'F')
                                value |= static_cast<uint32_t>(hexDigit - 'A' + 10);
                            else
                                return false;
                        }
                        readIndex += 4;
                        return true;
                    };
                    uint32_t codePoint = 0;
                    if (!hex4(codePoint))
                        break;
                    if (codePoint >= 0xD800 && codePoint <= 0xDBFF && readIndex + 6 <= byteCount && jsonBytes[readIndex] == '\\' && jsonBytes[readIndex + 1] == 'u')
                    {
                        readIndex += 2;
                        uint32_t lowSurrogate = 0;
                        if (hex4(lowSurrogate) && lowSurrogate >= 0xDC00 && lowSurrogate <= 0xDFFF)
                            codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (lowSurrogate - 0xDC00);
                    }
                    appendUtf8(text, codePoint);
                    break;
                }
                default:
                    text += escapeChar;
                    break; // covers \" \\ \/
                }
            }
            // truncated file: keep what we have
            if (!closed) break;
            out.push_back(std::move(text));
        }
        return out;
    }
}
