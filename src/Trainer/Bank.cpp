#include "Trainer/Bank.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <vector>
#include <sys/stat.h>

#include "Pokemon/Pokemon7LGPE.h"
#include "Pokemon/Pokemon8SWSH.h"
#include "Pokemon/Pokemon9LZA.h"
#include "Pokemon/Pokemon9SV.h"
#include "Pokemon/Pokemon8LA.h"
#include "Pokemon/Pokemon8BDSP.h"
#include "Pokemon/Pokemon3FRLG.h"
#include "Pokemon/Pokemon3RSE.h"
#include "Pokemon/Pokemon1RBY.h"
#include "Pokemon/Pokemon2GSC.h"
#include "Pokemon/Pokemon4DP.h"
#include "Pokemon/Pokemon4PT.h"
#include "Pokemon/Pokemon4HGSS.h"
#include "Pokemon/Pokemon5BW.h"
#include "Pokemon/Pokemon5B2W2.h"
#include "Pokemon/Pokemon6XY.h"
#include "Pokemon/Pokemon6ORAS.h"
#include "Pokemon/Pokemon7SM.h"
#include "Pokemon/Pokemon7USUM.h"
#include "Encryption/Encryption7LGPE.h"
#include "Encryption/Encryption8SWSH.h"
#include "Encryption/Encryption9LZA.h"
#include "Encryption/Encryption9SV.h"
#include "Encryption/Encryption4DP.h"
#include "Encryption/Encryption4PT.h"
#include "Encryption/Encryption4HGSS.h"
#include "Encryption/Encryption5BW.h"
#include "Encryption/Encryption5B2W2.h"
#include "Encryption/Encryption6XY.h"
#include "Encryption/Encryption6ORAS.h"
#include "Encryption/Encryption7SM.h"
#include "Encryption/Encryption7USUM.h"
#include "Encryption/Encryption8LA.h"
#include "Encryption/Encryption8BDSP.h"
#include "Encryption/Encryption3FRLG.h"
#include "Encryption/Encryption3RSE.h"
#include "Utils/FileUtilities.h"
#include "Utils/HelperUtilities.h"
#include "Utils/Logger.h"
#include "Globals.h"

using namespace Utils;
using namespace Enums;
using namespace Pokemon;
using namespace Encryption;

namespace Trainer
{
    namespace
    {
        // File: BASE_SAVE_DIRECTORY/bank/bank.dat
        //   Header (16 B): char magic[8]="PKSEBANK"; u32 version; u32 boxCount
        //   Then boxCount*BANK_SLOTS_PER_BOX fixed records, each:
        //     u32 groupTag; u8 payload[maxPayload]   (payload = native ENCRYPTED per-generation bytes)
        // An empty slot has groupTag == BTAG_EMPTY (0). Records are fixed-size (payload sized to
        // the largest party record across all games) so slot N is always at a computable offset.
        constexpr char BANK_MAGIC[8] = {'P', 'K', 'S', 'E', 'B', 'A', 'N', 'K'};
        constexpr uint32_t BANK_VERSION = 1;
        constexpr size_t HEADER_SIZE = 16; // magic[8] + version(4) + boxCount(4)

        // Optional per-box names live in a section appended AFTER the fixed records, introduced by
        // this marker. Old readers stop at the last record and ignore the trailing bytes, so adding
        // it needs no version bump -- a v1 file without the section simply has no custom names, and a
        // v1 file with it is still read correctly by code that predates the section. Per box:
        // u16 LE length + that many UTF-8 bytes.
        constexpr uint8_t NAMES_MARKER[4] = {'N', 'A', 'M', 'S'};

        // The open box, appended AFTER the names section for the same reason and on the same terms:
        // a reader that predates it stops early and ignores the trailing bytes, so no version bump.
        // AFTER rather than before is load-bearing -- an older PKSE looks for NAMS immediately past
        // the record table, so putting this first would make it find CBOX there instead and quietly
        // drop every box name the user had set. u16 LE box index, clamped on read.
        constexpr uint8_t CURRENT_BOX_MARKER[4] = {'C', 'B', 'O', 'X'};

        // Frozen on-disk group tags -- written into every slot record. NEVER renumber these;
        // add-only. (Deliberately independent of the Enums::GameVersion numeric values, which are
        // title-ID-derived and must be free to change without invalidating banked mons.)
        enum : uint32_t
        {
            BTAG_EMPTY = 0,
            BTAG_GG = 1,
            BTAG_SWSH = 2,
            BTAG_BDSP = 3,
            BTAG_PLA = 4,
            BTAG_SV = 5,
            BTAG_ZA = 6,
            BTAG_FRLG = 7, // PK3 -- FireRed/LeafGreen AND Ruby/Sapphire/Emerald (see below)
            // Gen 1 needs TWO tags, not one. A PokeList1's length is the only thing that says
            // whether its name fields are 11 bytes or 6, and the on-disk record has no length
            // field -- so the tag carries it. Guessing from the payload is not an option: 0xFF is
            // a real Gen 1 character ('9'), so "trailing 0xFF means Japanese" misreads a
            // legitimately-named international Pokemon. Two tags, no heuristic.
            BTAG_RBY = 8,    // international, 69-byte record
            BTAG_RBY_JP = 9, // Japanese,      59-byte record
            // Gen 2 has the same locale problem as Gen 1 and for the same reason: a PokeList2's
            // length is the only thing that says whether its name fields are 11 bytes or 6, and
            // the on-disk record carries no length. Two tags, no heuristic.
            BTAG_GSC = 10,    // international, 73-byte record
            BTAG_GSC_JP = 11, // Japanese,      63-byte record
            // One tag per ENTITY format, not per game: the bank stores Pokemon, and DP/Pt/HGSS
            // all store a PK4, BW/B2W2 a PK5, XY/ORAS a PK6, SM/USUM a PK7. Each maps back to
            // the group its entity class reports.
            BTAG_G4 = 12, // PK4, 236-byte party record
            BTAG_G5 = 13, // PK5, 220
            BTAG_G6 = 14, // PK6, 260
            BTAG_G7 = 15, // PK7, 260
        };

        uint32_t bankTagFor(GameVersion group)
        {
            switch (group)
            {
            case GameVersion::GG:
                return BTAG_GG;
            case GameVersion::SWSH:
                return BTAG_SWSH;
            case GameVersion::BDSP:
                return BTAG_BDSP;
            case GameVersion::PLA:
                return BTAG_PLA;
            case GameVersion::SV:
                return BTAG_SV;
            case GameVersion::ZA:
                return BTAG_ZA;
            case GameVersion::FRLG:
            case GameVersion::RSE:
                // One tag per ENTITY format, the same rule Gens 4-7 follow below: all five GBA
                // games store a PK3, byte for byte, with the same crypt and the same shuffle.
                // The tag is frozen on disk and add-only, so RSE joins the existing one rather
                // than claiming a new number -- a PK3 banked before Ruby/Sapphire/Emerald existed
                // and one banked after are the same 100 bytes and must stay readable as each other.
                return BTAG_FRLG;
            case GameVersion::RBY:
                return BTAG_RBY; // locale-blind; see bankTagForEntity
            case GameVersion::GSC:
                return BTAG_GSC; // ditto
            // Every Gen 4-7 group, because each one has its own entity class now and each of
            // them reports its own group. The TAG is still per entity format -- it is a frozen
            // on-disk value and a banked record has no game of origin to remember.
            case GameVersion::DP:
            case GameVersion::PT:
            case GameVersion::HGSS:
                return BTAG_G4;
            case GameVersion::BW:
            case GameVersion::B2W2:
                return BTAG_G5;
            case GameVersion::XY:
            case GameVersion::ORAS:
                return BTAG_G6;
            case GameVersion::SM:
            case GameVersion::USUM:
                return BTAG_G7;
            default:
                return BTAG_EMPTY;
            }
        }

        /// The tag actually written to disk. Identical to bankTagFor() for every group except
        /// Gen 1, where the entity's own length picks the international or Japanese tag. Uses the
        /// buffer size rather than a cast because RTTI is disabled project-wide.
        uint32_t bankTagForEntity(const ::Pokemon::Pokemon &pk)
        {
            const GameVersion gameVersion = pk.getGameGroup();
            if (gameVersion == GameVersion::RBY)
            {
                return pk.getDataSize() == ::Pokemon::SIZE_1JLIST ? BTAG_RBY_JP : BTAG_RBY;
            }
            if (gameVersion == GameVersion::GSC)
            {
                return pk.getDataSize() == ::Pokemon::SIZE_2JLIST ? BTAG_GSC_JP : BTAG_GSC;
            }
            return bankTagFor(gameVersion);
        }

        // Maps a frozen tag back to its group. Returns false for BTAG_EMPTY / unknown tags.
        bool groupForBankTag(uint32_t bankTag, GameVersion &out)
        {
            switch (bankTag)
            {
            case BTAG_GG:
                out = GameVersion::GG;
                return true;
            case BTAG_SWSH:
                out = GameVersion::SWSH;
                return true;
            case BTAG_BDSP:
                out = GameVersion::BDSP;
                return true;
            case BTAG_PLA:
                out = GameVersion::PLA;
                return true;
            case BTAG_SV:
                out = GameVersion::SV;
                return true;
            case BTAG_ZA:
                out = GameVersion::ZA;
                return true;
            case BTAG_FRLG:
                out = GameVersion::FRLG;
                return true;
            case BTAG_RBY:
            case BTAG_RBY_JP:
                out = GameVersion::RBY;
                return true;
            case BTAG_GSC:
            case BTAG_GSC_JP:
                out = GameVersion::GSC;
                return true;
            // A tag names an entity FORMAT, so mapping back has to pick one of the groups that
            // share it. The superset group is the safe pick: its tables cover the others', so a
            // banked record is never checked against a narrower pool than it came from.
            case BTAG_G4:
                out = GameVersion::HGSS;
                return true;
            case BTAG_G5:
                out = GameVersion::B2W2;
                return true;
            case BTAG_G6:
                out = GameVersion::ORAS;
                return true;
            case BTAG_G7:
                out = GameVersion::USUM;
                return true;
            default:
                return false;
            }
        }

        // Per-group serialization: record size, construction, and encryption.
        size_t recordSizeForImpl(GameVersion group)
        {
            switch (group)
            {
            case GameVersion::GG:
                return 260; // SIZE_PARTY7_LGPE (0x104)
            case GameVersion::SWSH:
                return SIZE_PARTY8_SWSH;
            case GameVersion::ZA:
                return SIZE_PARTY9_LZA;
            case GameVersion::SV:
                return SIZE_PARTY9_SV;
            case GameVersion::PLA:
                return SIZE_PARTY8_LA;
            case GameVersion::BDSP:
                return SIZE_PARTY8_BDSP;
            case GameVersion::FRLG:
            case GameVersion::RSE:
                return SIZE_PARTY3_FRLG; // 100 (< max payload; on-disk stride unchanged)
            case GameVersion::RBY:
                return ::Pokemon::SIZE_1ULIST; // 69 -- the LONGER of the two
            case GameVersion::GSC:
                return ::Pokemon::SIZE_2ULIST; // 73 -- ditto
            case GameVersion::DP:
                return Encryption::SIZE_PARTY4_DP; // 236
            case GameVersion::PT:
                return Encryption::SIZE_PARTY4_PT; // 236
            case GameVersion::HGSS:
                return Encryption::SIZE_PARTY4_HGSS; // 236
            case GameVersion::BW:
                return Encryption::SIZE_PARTY5_BW; // 220
            case GameVersion::B2W2:
                return Encryption::SIZE_PARTY5_B2W2; // 220
            case GameVersion::XY:
                return Encryption::SIZE_PARTY6_XY; // 260
            case GameVersion::ORAS:
                return Encryption::SIZE_PARTY6_ORAS; // 260
            case GameVersion::SM:
                return Encryption::SIZE_PARTY7_SM; // 260
            case GameVersion::USUM:
                return Encryption::SIZE_PARTY7_USUM; // 260
            default:
                return 260;
            }
        }

        /// Record size for an on-disk tag. Only Gen 1 needs this rather than recordSizeFor():
        /// its two tags share a group but not a length, and handing Pokemon1RBY a 69-byte span
        /// for a 59-byte Japanese record would read its names from the wrong offsets and return
        /// plausible garbage rather than failing.
        size_t recordSizeForTagImpl(uint32_t bankTag)
        {
            if (bankTag == BTAG_RBY_JP)
                return ::Pokemon::SIZE_1JLIST;
            if (bankTag == BTAG_GSC_JP)
                return ::Pokemon::SIZE_2JLIST;
            GameVersion gameVersion;
            if (!groupForBankTag(bankTag, gameVersion))
                return 0;
            return recordSizeForImpl(gameVersion);
        }

        // Largest party record across all supported games -> the fixed per-slot payload size.
        // EVERY group must appear here. FRLG was missing; it is smaller than the maximum so the
        // value didn't change, but a future format added the same way and left off this list would
        // have a record longer than the payload it is written into -- a silent overflow.
        size_t maxPayloadSize()
        {
            return std::max({recordSizeForImpl(GameVersion::GG), recordSizeForImpl(GameVersion::SWSH),
                             recordSizeForImpl(GameVersion::BDSP), recordSizeForImpl(GameVersion::PLA),
                             recordSizeForImpl(GameVersion::SV), recordSizeForImpl(GameVersion::ZA),
                             recordSizeForImpl(GameVersion::FRLG), recordSizeForImpl(GameVersion::RSE),
                             recordSizeForImpl(GameVersion::RBY),
                             recordSizeForImpl(GameVersion::GSC), recordSizeForImpl(GameVersion::HGSS),
                             recordSizeForImpl(GameVersion::B2W2), recordSizeForImpl(GameVersion::ORAS),
                             recordSizeForImpl(GameVersion::USUM)});
        }

        /// The part of an on-disk record that is the Pokemon, as opposed to padding.
        ///
        /// A GEN 3 BOX POKEMON IS 80 BYTES AND ITS BANK RECORD 100. serialize() copies the 80 and leaves
        /// the 20-byte party tail zero-padded, and a tail that is ENTIRELY zero was never a tail: a real
        /// party record always carries a level, a max HP, and 0xFF as its mail id. Reloading such a
        /// record at 100 bytes handed back something other than what was deposited -- and Gen 3's party
        /// writer takes a 100-byte record's tail at its word, so a Sapphire box Mudkip withdrawn into a
        /// LeafGreen party after a bank reload was written as level 0 with 0/0 HP. verifyImage() already
        /// called that tail padding; this is the loader agreeing with it.
        ///
        /// Gen 3 only, and deliberately so: PK8 and PK9 index level and stats straight out of the party
        /// tail, so trimming one of THOSE to its stored length would read past the buffer. PKSE keeps
        /// Gen 3 box Pokemon at 80 bytes in memory; nothing else is held at its stored length.
        std::span<const std::byte> depositedSpan(GameVersion group, std::span<const std::byte> record)
        {
            if (!Enums::isGen3Group(group) || record.size() != SIZE_PARTY3_FRLG)
                return record;
            const bool tailIsPadding = std::all_of(record.begin() + SIZE_STORED3_FRLG, record.end(), [](std::byte value) { return value == std::byte{0}; });
            return tailIsPadding ? record.first(SIZE_STORED3_FRLG) : record;
        }

        std::unique_ptr<::Pokemon::Pokemon> makePokemonImpl(GameVersion group, std::span<const std::byte> record)
        {
            switch (group)
            {
            case GameVersion::GG:
                return std::make_unique<Pokemon7LGPE>(record);
            case GameVersion::SWSH:
                return std::make_unique<Pokemon8SWSH>(record);
            case GameVersion::ZA:
                return std::make_unique<Pokemon9LZA>(record);
            case GameVersion::SV:
                return std::make_unique<Pokemon9SV>(record);
            case GameVersion::PLA:
                return std::make_unique<Pokemon8LA>(record);
            case GameVersion::BDSP:
                return std::make_unique<Pokemon8BDSP>(record);
            case GameVersion::FRLG:
                return std::make_unique<Pokemon3FRLG>(record);
            case GameVersion::RSE:
                return std::make_unique<Pokemon3RSE>(record);
            // Pokemon1RBY reads its locale from the span's length, so the caller sizing the
            // span by TAG (recordSizeForTag) is what makes this correct for both Gen 1 forms.
            case GameVersion::RBY:
                return std::make_unique<Pokemon1RBY>(record);
            // Pokemon2GSC reads its locale from the span's length too.
            case GameVersion::GSC:
                return std::make_unique<Pokemon2GSC>(record);
            case GameVersion::DP:
                return std::make_unique<Pokemon4DP>(record);
            case GameVersion::PT:
                return std::make_unique<Pokemon4PT>(record);
            case GameVersion::HGSS:
                return std::make_unique<Pokemon4HGSS>(record);
            case GameVersion::BW:
                return std::make_unique<Pokemon5BW>(record);
            case GameVersion::B2W2:
                return std::make_unique<Pokemon5B2W2>(record);
            case GameVersion::XY:
                return std::make_unique<Pokemon6XY>(record);
            case GameVersion::ORAS:
                return std::make_unique<Pokemon6ORAS>(record);
            case GameVersion::SM:
                return std::make_unique<Pokemon7SM>(record);
            case GameVersion::USUM:
                return std::make_unique<Pokemon7USUM>(record);
            default:
                return nullptr;
            }
        }

        std::byte *encryptFor(GameVersion group, std::span<const std::byte> decryptedRecord, uint32_t encryptionConstant)
        {
            switch (group)
            {
            case GameVersion::GG:
                return encryptArray7LGPE(decryptedRecord, encryptionConstant);
            case GameVersion::SWSH:
                return encryptArray8SWSH(decryptedRecord, encryptionConstant);
            case GameVersion::ZA:
                return encryptArray9LZA(decryptedRecord, encryptionConstant);
            case GameVersion::SV:
                return encryptArray9SV(decryptedRecord, encryptionConstant);
            case GameVersion::PLA:
                return encryptArray8LA(decryptedRecord, encryptionConstant);
            case GameVersion::BDSP:
                return encryptArray8BDSP(decryptedRecord, encryptionConstant);
            case GameVersion::FRLG:
                return encryptArray3FRLG(decryptedRecord); // Gen 3 keys off the PID in-buffer (no ec)
            case GameVersion::RSE:
                return encryptArray3RSE(decryptedRecord); // same crypt; see Encryption3RSE.h
            // Gen 4/5 key on the in-buffer checksum; Gen 6/7 on the in-buffer EC.
            case GameVersion::DP:
                return encryptArray4DP(decryptedRecord);
            case GameVersion::PT:
                return encryptArray4PT(decryptedRecord);
            case GameVersion::HGSS:
                return encryptArray4HGSS(decryptedRecord);
            case GameVersion::BW:
                return encryptArray5BW(decryptedRecord);
            case GameVersion::B2W2:
                return encryptArray5B2W2(decryptedRecord);
            case GameVersion::XY:
                return encryptArray6XY(decryptedRecord);
            case GameVersion::ORAS:
                return encryptArray6ORAS(decryptedRecord);
            case GameVersion::SM:
                return encryptArray7SM(decryptedRecord);
            case GameVersion::USUM:
                return encryptArray7USUM(decryptedRecord);
            case GameVersion::RBY:
            case GameVersion::GSC:
            {
                // Gens 1 and 2 have NO encryption -- the games store the record in the clear.
                // The bank still goes through this path so the "store native bytes" contract
                // holds for every group, so this hands back a plain copy. Returning nullptr
                // instead would make serialize() silently write an empty payload.
                std::byte *out = new std::byte[decryptedRecord.size()];
                std::memcpy(out, decryptedRecord.data(), decryptedRecord.size());
                return out;
            }
            default:
                return nullptr;
            }
        }

        void putU32LE(uint8_t *bytes, uint32_t value)
        {
            bytes[0] = static_cast<uint8_t>(value);
            bytes[1] = static_cast<uint8_t>(value >> 8);
            bytes[2] = static_cast<uint8_t>(value >> 16);
            bytes[3] = static_cast<uint8_t>(value >> 24);
        }
    }

    // A tag names an entity FORMAT, so this is the public form of the mapping the tag tables
    // above encode. See the declaration in Bank.h for why callers must compare against it.
    GameVersion Bank::groupAsBanked(GameVersion group) noexcept
    {
        GameVersion banked;
        if (!groupForBankTag(bankTagFor(group), banked))
            return GameVersion::Invalid;
        return banked;
    }

    Bank::Bank()
    {
        boxes.resize(BANK_BOX_COUNT);
        load();
    }

    // Where the bank file lives. Empty (the normal case) means sdmc:/PKSE/bank; see setRootOverride.
    static std::string g_bankRootOverride;

    void Bank::setRootOverride(const std::string &directory) { g_bankRootOverride = directory; }
    bool Bank::rootOverridden() noexcept { return !g_bankRootOverride.empty(); }

    std::string Bank::bankDir()
    {
        return g_bankRootOverride.empty() ? (BASE_SAVE_DIRECTORY + "/bank") : g_bankRootOverride;
    }

    std::string Bank::filePath() const
    {
        return bankDir() + "/bank.dat";
    }

    std::string Bank::boxDisplayName(size_t box) const
    {
        if (box < BANK_BOX_COUNT && !boxNames[box].empty())
            return boxNames[box];
        return "Bank " + std::to_string(box + 1); // default, 1-indexed
    }

    std::vector<uint8_t> Bank::serialize() const
    {
        const size_t payload = maxPayloadSize();
        const size_t recSize = 4 + payload;
        const size_t total = BANK_BOX_COUNT * BANK_SLOTS_PER_BOX;
        std::vector<uint8_t> buffer(HEADER_SIZE + total * recSize, 0);

        // Header.
        std::memcpy(&buffer[0], BANK_MAGIC, 8);
        putU32LE(&buffer[8], BANK_VERSION);
        putU32LE(&buffer[12], static_cast<uint32_t>(BANK_BOX_COUNT));

        for (size_t box = 0; box < BANK_BOX_COUNT; ++box)
        {
            for (size_t slot = 0; slot < BANK_SLOTS_PER_BOX; ++slot)
            {
                const auto &pokemon = boxes[box][slot];
                if (!pokemon || pokemon->speciesID() == 0)
                    continue;

                const GameVersion gameVersion = pokemon->getGameGroup();
                const uint32_t bankTag = bankTagForEntity(*pokemon);
                // unknown group -> can't tag it, skip
                if (bankTag == BTAG_EMPTY) continue;

                const size_t recOff = HEADER_SIZE + (box * BANK_SLOTS_PER_BOX + slot) * recSize;
                putU32LE(&buffer[recOff], bankTag);

                // Mirror the pokemon's native bytes: encrypt with its own group + EC, copy up to its
                // record size (box mons are shorter than party; the tail stays zero-padded).
                const uint32_t encryptionConstant = readUInt32LittleEndian(reinterpret_cast<const uint8_t *>(pokemon->getData().data()));
                std::span<const std::byte> dec(pokemon->getData().data(), pokemon->getDataSize());
                std::byte *encryptedRecord = encryptFor(gameVersion, dec, encryptionConstant);
                if (encryptedRecord)
                {
                    const size_t byteCount = std::min({pokemon->getDataSize(), recordSizeForTagImpl(bankTag), payload});
                    std::memcpy(&buffer[recOff + 4], encryptedRecord, byteCount);
                    delete[] encryptedRecord;
                }
            }
        }

        // Names section (see NAMES_MARKER). Appended, length-prefixed per box, so it stays
        // backward compatible with readers that only know the fixed record table.
        buffer.insert(buffer.end(), NAMES_MARKER, NAMES_MARKER + 4);
        for (size_t box = 0; box < BANK_BOX_COUNT; ++box)
        {
            std::string name = boxNames[box];
            // length field is u16
            if (name.size() > 0xFFFF) name.resize(0xFFFF);
            const uint16_t nameLength = static_cast<uint16_t>(name.size());
            buffer.push_back(static_cast<uint8_t>(nameLength & 0xFF));
            buffer.push_back(static_cast<uint8_t>((nameLength >> 8) & 0xFF));
            buffer.insert(buffer.end(), name.begin(), name.end());
        }

        // Open box (see CURRENT_BOX_MARKER). Being in here is what makes a box move an unsaved
        // change: hasChanged() is serialize() != savedImage, so this is the only place a cursor
        // position can live and still reach the Save/Discard prompt.
        buffer.insert(buffer.end(), CURRENT_BOX_MARKER, CURRENT_BOX_MARKER + 4);
        buffer.push_back(static_cast<uint8_t>(currentBox & 0xFF));
        buffer.push_back(static_cast<uint8_t>((currentBox >> 8) & 0xFF));
        return buffer;
    }

    void Bank::load()
    {
        // Reset every slot first so this is a true reload -- also the "discard changes" path.
        for (auto &box : boxes)
            for (auto &slot : box)
                slot.reset();
        // names revert on reload/discard too
        for (auto &n : boxNames) n.clear();
        currentBox = 0; // and so does the open box -- this is the discard path as well as the load

        const std::string path = filePath();
        loadProblem.clear();
        fileUntouchable = false;

        // A save interrupted between removing the old file and renaming the new one into place
        // (see save()) leaves only the temporary file, and it is complete: the old file is removed
        // only after the new one is fully written. Put it back where it belongs.
        const std::string temporaryPath = path + ".tmp";
        struct stat fileStatus;
        if (stat(path.c_str(), &fileStatus) != 0 && stat(temporaryPath.c_str(), &fileStatus) == 0)
        {
            if (std::rename(temporaryPath.c_str(), path.c_str()) == 0)
                logErrorToFile("Bank: recovered bank.dat from an interrupted save", temporaryPath.c_str());
            else
                logErrorToFile("Bank: could not recover bank.dat from an interrupted save", temporaryPath.c_str());
        }
        const bool fileExists = stat(path.c_str(), &fileStatus) == 0;

        size_t fileSize = 0;
        uint8_t *file = fileExists ? readAllBytes(path.c_str(), &fileSize) : nullptr;

        // A file we cannot use must be moved aside, NOT left in place to be silently overwritten
        // by the next save. Whatever it is -- a newer format, a bad card, a half-finished write --
        // it is the user's only copy of those Pokemon, and starting empty over the top of it would
        // destroy them. Renaming lets us proceed with an empty bank while the original survives;
        // if even that fails, save() refuses to write over it.
        auto abandonFile = [&](const char *why)
        {
            const std::string aside = path + ".unreadable";
            std::remove(aside.c_str()); // keep only the most recent casualty
            if (std::rename(path.c_str(), aside.c_str()) == 0)
            {
                logErrorToFile("Bank: unreadable file preserved as bank.dat.unreadable", why);
                loadProblem = "The bank file could not be read. It was kept as bank.dat.unreadable and the bank opened empty.";
            }
            else
            {
                logErrorToFile("Bank: unreadable file could NOT be preserved", why);
                fileUntouchable = true;
                loadProblem = "The bank file could not be read, so the bank opened empty and will not be saved over it.";
            }
            delete[] file;
            savedImage = serialize();
        };

        if (!fileExists)
        {
            // No unified bank yet: one-time import of any legacy per-group bank files, then snapshot.
            // Skipped for a redirected (self-test) bank -- the legacy files are the USER's, and
            // dragging them into a scratch file would be work nobody asked for.
            if (!rootOverridden())
                migrateLegacyBanks();
            savedImage = serialize();
            logInfoToFile("Bank: no unified bank file; migrated legacy banks if present", path.c_str());
            return;
        }
        if (!file)
        {
            // There IS a bank file and it will not read: an SD card error, a short read, no memory.
            // Treating that as "no bank yet" re-ran the legacy migration and let the next save
            // overwrite every Pokemon in it.
            abandonFile("the file exists but could not be read");
            return;
        }

        const size_t payload = maxPayloadSize();
        const size_t recSize = 4 + payload;

        if (fileSize < HEADER_SIZE || std::memcmp(file, BANK_MAGIC, 8) != 0)
        {
            abandonFile("bad magic or truncated header");
            return;
        }
        const uint32_t version = readUInt32LittleEndian(file + 8);
        if (version != BANK_VERSION)
        {
            // Refuse rather than misread. Parsing a future layout as v1 would manufacture garbage
            // Pokemon out of correctly-written bytes -- worse than reading nothing.
            abandonFile("unsupported bank version");
            return;
        }

        // How many boxes the FILE was written with. This is what sizes its record table, and therefore
        // where its names section begins -- using our own BANK_BOX_COUNT instead would look past the end
        // of any smaller, older bank and silently drop every custom box name the first time the count
        // was raised. Records are position-indexed, so a shorter table simply fills the low boxes.
        uint32_t fileBoxes = readUInt32LittleEndian(file + 12);
        if (fileBoxes == 0 || fileBoxes > 4096)
        {
            // Header damaged or absurd; fall back to the record count the file can actually hold rather
            // than trusting it to compute an offset.
            fileBoxes = static_cast<uint32_t>(BANK_BOX_COUNT);
        }
        if (fileBoxes > BANK_BOX_COUNT)
        {
            // The bank was written by a build with MORE boxes. Everything past ours cannot be loaded,
            // and saving would drop it, so say so loudly rather than quietly truncating someone's bank.
            logErrorToFile("Bank: file has more boxes than this build supports; extra boxes will be lost if saved", std::to_string(fileBoxes).c_str());
        }
        const size_t fileTotal = static_cast<size_t>(fileBoxes) * BANK_SLOTS_PER_BOX;
        const size_t total = BANK_BOX_COUNT * BANK_SLOTS_PER_BOX;
        // Whole records only: a file truncated mid-record contributes nothing past the last
        // complete one, which is what keeps the span below inside the buffer.
        const size_t avail = (fileSize > HEADER_SIZE) ? (fileSize - HEADER_SIZE) / recSize : 0;
        const size_t count = std::min({total, fileTotal, avail});
        loadRejects = 0;
        for (size_t nIndex = 0; nIndex < count; ++nIndex)
        {
            const size_t recOff = HEADER_SIZE + nIndex * recSize;
            const uint32_t bankTag = readUInt32LittleEndian(file + recOff);
            GameVersion gameVersion;
            // genuinely empty slot
            if (bankTag == BTAG_EMPTY) continue;
            if (!groupForBankTag(bankTag, gameVersion))
            {
                ++loadRejects;
                continue;
            } // unknown tag = damage

            std::span<const std::byte> rec(reinterpret_cast<const std::byte *>(file + recOff + 4), recordSizeForTagImpl(bankTag));
            auto pokemon = makePokemonImpl(gameVersion, depositedSpan(gameVersion, rec));

            // Bytes off the SD card are NOT trusted. A slot that decodes to nonsense must be
            // dropped, not stored: a non-null slot holding garbage is a "ghost", and it
            // re-encrypts into a Bad Egg the moment it is withdrawn into a real save. The
            // checksum is the decisive test -- every format carries one.
            // isStructurallyValid() is the checksum test for every format that has one, and the
            // nearest available equivalent for Gen 1, which has none.
            if (!pokemon || pokemon->speciesID() == 0 || !pokemon->isStructurallyValid())
            {
                ++loadRejects;
                continue;
            }
            boxes[nIndex / BANK_SLOTS_PER_BOX][nIndex % BANK_SLOTS_PER_BOX] = std::move(pokemon);
        }

        // Optional names section, appended after the full record table (see NAMES_MARKER). Absent in
        // files written before names existed -- those just keep the default "Bank N" labels.
        // Positioned by the FILE's record table, not ours -- see fileBoxes above.
        const size_t namesOff = HEADER_SIZE + fileTotal * recSize;
        // Where the section after the names begins. Starts at the names' own offset so a file
        // written before names existed still finds whatever follows the record table.
        size_t sectionOffset = namesOff;
        if (fileSize >= namesOff + 4 && std::memcmp(file + namesOff, NAMES_MARKER, 4) == 0)
        {
            size_t writeOffset = namesOff + 4;
            const size_t nameCount = std::min<size_t>(fileBoxes, BANK_BOX_COUNT);
            for (size_t box = 0; box < nameCount; ++box)
            {
                // truncated: stop cleanly
                if (writeOffset + 2 > fileSize) break;
                const uint16_t length = static_cast<uint16_t>(file[writeOffset] | (file[writeOffset + 1] << 8));
                writeOffset += 2;
                // truncated string: stop
                if (writeOffset + length > fileSize) break;
                const size_t take = std::min<size_t>(length, MAX_BOX_NAME_LEN * 4); // UTF-8 guard
                boxNames[box].assign(reinterpret_cast<const char *>(file + writeOffset), take);
                writeOffset += length;
            }
            sectionOffset = writeOffset;
        }

        // Optional open-box section (see CURRENT_BOX_MARKER). Absent in files written before it
        // existed, and absent from a truncated names section too -- both keep box 0, which is where
        // the bank opened before any of this.
        if (fileSize >= sectionOffset + 6 && std::memcmp(file + sectionOffset, CURRENT_BOX_MARKER, 4) == 0)
        {
            const uint16_t storedBox = static_cast<uint16_t>(file[sectionOffset + 4] | (file[sectionOffset + 5] << 8));
            // BANK_BOX_COUNT is a ratchet, so a file written by a build with more boxes than this
            // one can name a box that no longer exists.
            currentBox = (storedBox < BANK_BOX_COUNT) ? storedBox : 0;
        }

        delete[] file;
        savedImage = serialize(); // baseline snapshot of the just-loaded state
        if (loadRejects > 0)
            logErrorToFile("Bank: dropped damaged slot records on load", std::to_string(loadRejects).c_str());
        logInfoToFile("Bank: loaded unified bank", path.c_str());
    }

    void Bank::migrateLegacyBanks()
    {
        // Legacy layout (pre-unified): one file per group, "<tag>_bank.dat", uniform
        // recordSizeForImpl(group) stride, no header, a slot occupied iff its EC (first u32) != 0.
        // Import every non-empty pokemon into the unified bank, first-fit; overflow is dropped + logged.
        struct Legacy
        {
            GameVersion group;
            const char *tag;
        };
        static const Legacy legacy[] = {
            {GameVersion::GG, "gg"},
            {GameVersion::SWSH, "swsh"},
            {GameVersion::ZA, "za"},
            {GameVersion::SV, "sv"},
            {GameVersion::PLA, "pla"},
            {GameVersion::BDSP, "bdsp"},
        };

        size_t dstBox = 0, dstSlot = 0;
        auto placeNext = [&](std::unique_ptr<::Pokemon::Pokemon> pokemon) -> bool
        {
            while (dstBox < BANK_BOX_COUNT)
            {
                if (dstSlot >= BANK_SLOTS_PER_BOX)
                {
                    dstSlot = 0;
                    ++dstBox;
                    continue;
                }
                if (!boxes[dstBox][dstSlot])
                {
                    boxes[dstBox][dstSlot] = std::move(pokemon);
                    ++dstSlot;
                    return true;
                }
                ++dstSlot;
            }
            return false; // bank full
        };

        int imported = 0, dropped = 0;
        for (const auto &legacyBank : legacy)
        {
            const std::string bankPath = BASE_SAVE_DIRECTORY + "/bank/" + legacyBank.tag + "_bank.dat";
            size_t size = 0;
            uint8_t *f = readAllBytes(bankPath.c_str(), &size);
            if (!f)
                continue;

            const size_t recSize = recordSizeForImpl(legacyBank.group);
            const size_t byteCount = recSize ? (size / recSize) : 0;
            for (size_t index = 0; index < byteCount; ++index)
            {
                const size_t offset = index * recSize;
                // legacy empty (EC == 0)
                if (readUInt32LittleEndian(f + offset) == 0) continue;
                std::span<const std::byte> rec(reinterpret_cast<const std::byte *>(f + offset), recSize);
                auto pokemon = makePokemonImpl(legacyBank.group, depositedSpan(legacyBank.group, rec));
                if (pokemon && pokemon->speciesID() != 0)
                {
                    if (placeNext(std::move(pokemon)))
                        ++imported;
                    else
                        ++dropped;
                }
            }
            delete[] f;
        }

        if (imported > 0 || dropped > 0)
        {
            const std::string message = std::to_string(imported) + " imported, " + std::to_string(dropped) + " dropped";
            logInfoToFile("Bank: migrated legacy per-group banks", message.c_str());
        }
    }

    size_t Bank::verifyImage(const std::vector<uint8_t> &image) const
    {
        const size_t payload = maxPayloadSize();
        const size_t recSize = 4 + payload;
        size_t failures = 0;

        for (size_t box = 0; box < BANK_BOX_COUNT; ++box)
        {
            for (size_t slot = 0; slot < BANK_SLOTS_PER_BOX; ++slot)
            {
                const size_t recOff = HEADER_SIZE + (box * BANK_SLOTS_PER_BOX + slot) * recSize;
                if (recOff + recSize > image.size())
                {
                    ++failures;
                    continue;
                }

                const auto &source = boxes[box][slot];
                const uint32_t bankTag = readUInt32LittleEndian(image.data() + recOff);
                const bool occupied = source && source->speciesID() != 0 && bankTagForEntity(*source) != BTAG_EMPTY;
                if (!occupied)
                {
                    // wrote a record for a slot we consider empty
                    if (bankTag != BTAG_EMPTY) ++failures;
                    continue;
                }

                // THE TAG ON DISK MUST BE THE TAG THIS ENTITY WRITES, not a group that matches
                // its own. Comparing groupForBankTag(tag) against source->getGameGroup() looks
                // equivalent and is not: a tag names an entity FORMAT, several groups share one,
                // and reading it back deliberately yields the format's representative. So every
                // Ruby PK3, Platinum PK4, X/Y PK6 and Sun PK7 failed here -- before the round trip
                // this function exists to perform ever ran.
                if (bankTag != bankTagForEntity(*source))
                {
                    ++failures;
                    continue;
                }
                GameVersion gameVersion;
                if (!groupForBankTag(bankTag, gameVersion))
                {
                    ++failures;
                    continue;
                }

                // Decoded exactly as load() decodes it, padding and all.
                auto roundTripped = makePokemonImpl(gameVersion, depositedSpan(gameVersion, std::span<const std::byte>(reinterpret_cast<const std::byte *>(image.data() + recOff + 4), recordSizeForTagImpl(bankTag))));
                if (!roundTripped)
                {
                    ++failures;
                    continue;
                }

                // Compare exactly the span serialize() wrote -- a box pokemon is shorter than the party
                // record it sits in, and the zero tail beyond it is padding, not data. Comparing
                // past that would fail on padding rather than on any real difference.
                const size_t byteCount = std::min({source->getDataSize(), roundTripped->getDataSize(), recordSizeForTagImpl(bankTag)});
                if (std::memcmp(source->getData().data(), roundTripped->getData().data(), byteCount) != 0)
                {
                    ++failures;
                    const std::string where = "box " + std::to_string(box + 1) + " slot " + std::to_string(slot + 1) + " (" + std::string(source->species()) + ")";
                    logErrorToFile("Bank: slot failed encrypt/decrypt round trip", where.c_str());
                }
            }
        }
        return failures;
    }

    bool Bank::save() const
    {
        const std::string bankDirectory = bankDir();
        mkdir(bankDirectory.c_str(), 0777); // ignore EEXIST

        std::vector<uint8_t> buffer = serialize();

        // The bank's contract is byte-in == byte-out. Verify the image reproduces every live Pokemon
        // before it goes to disk.
        //
        // Deliberately does NOT abort the save. A failed bank save blocks leaving the storage view, so a
        // false positive here would trap the user in the UI; and if the mismatch is real, refusing to
        // write leaves them with a stale file rather than a fresh one. Record it, log which slot, and let
        // the caller surface it.
        verifyFailures = verifyImage(buffer);

        const std::string path = filePath();
        if (fileUntouchable)
        {
            logErrorToFile("Bank: refusing to write over a bank file that could not be read or set aside", path.c_str());
            return false;
        }

        // Written to a temporary file first, and only moved into place once it is complete. Writing
        // straight into bank.dat truncated it first, so a save cut short (power, an SD error) left a
        // damaged bank. The old file is removed before the rename because a FAT rename does not
        // replace an existing file; load() recovers the temporary file if that is where it stopped.
        const std::string temporaryPath = path + ".tmp";
        FILE *f = fopen(temporaryPath.c_str(), "wb");
        if (!f)
        {
            logErrorToFile("Bank: failed to open bank file for writing", temporaryPath.c_str());
            return false;
        }
        const size_t written = fwrite(buffer.data(), 1, buffer.size(), f);
        const int flushResult = fflush(f);
        const int closeResult = fclose(f);
        if (written != buffer.size() || flushResult != 0 || closeResult != 0)
        {
            logErrorToFile("Bank: failed to write the whole bank file", temporaryPath.c_str());
            std::remove(temporaryPath.c_str());
            return false;
        }
        std::remove(path.c_str());
        if (std::rename(temporaryPath.c_str(), path.c_str()) != 0)
        {
            logErrorToFile("Bank: failed to move the new bank file into place", temporaryPath.c_str());
            return false;
        }

        savedImage = std::move(buffer); // in sync with disk again
        return true;
    }

    bool Bank::hasChanged() const
    {
        return serialize() != savedImage;
    }

    //
    // The bodies stay in the anonymous namespace above because every caller in this file is a free
    // helper rather than a Bank member. These four forward to them so anything else that needs an
    // entity of an arbitrary save format reads the same mapping instead of keeping a second copy
    // of it in step.
    std::unique_ptr<::Pokemon::Pokemon> Bank::makePokemon(GameVersion group, std::span<const std::byte> record)
    {
        return makePokemonImpl(group, record);
    }

    std::byte *Bank::encryptRecord(GameVersion group, std::span<const std::byte> decryptedRecord, uint32_t encryptionConstant)
    {
        return encryptFor(group, decryptedRecord, encryptionConstant);
    }

    size_t Bank::recordSizeFor(GameVersion group) { return recordSizeForImpl(group); }

    size_t Bank::recordSizeForTag(uint32_t bankTag) { return recordSizeForTagImpl(bankTag); }
}
