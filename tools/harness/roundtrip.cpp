/**
 * PKSE has no test suite; the substitute has been a manual discipline that only runs when someone
 * remembers. This is the automated half of it, and the single most valuable check available:
 *
 *     read a real save -> parse it -> serialize it straight back -> assert the bytes are identical
 *
 * A no-op edit MUST reproduce the file exactly. Any difference means the write path invented,
 * dropped or mangled something, and every byte it gets wrong is a byte the real game will read.
 * Several bugs this project actually shipped would have been caught here in seconds:
 *   - the party stat tail read past its allocation and returned garbage that changed per call
 *   - Gen 9 box/item writes corrupted the save in-game
 *   - "ghost" slots re-encrypted into Bad Eggs
 *
 * Runs on the HOST, not the Switch. The save layer is pure byte manipulation; only a few headers
 * needed <switch.h> for scalar typedefs, which Utils/NXTypes.h now supplies off-console.
 *
 * Build + run:  tools/harness/build.sh          (see that script for save paths)
 */
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "SaveTree.h" // saves are found by their TITLE FOLDER, not by a guessed filename

/// The save root, so a skip can name the folder the reader needs to create.
static std::string g_saveRoot;

#include "Trainer/Trainer3FRLG.h"
#include "Trainer/Trainer3RSE.h"
#include "Save/RtcFooter.h"
#include "Trainer/Trainer7LGPE.h"
#include "Trainer/Trainer8SWSH.h"
#include "Trainer/Trainer8LA.h"
#include "Pokemon/DexTable8LA.h"
#include "Trainer/Trainer9SV.h"
#include "Trainer/Trainer9LZA.h"
#include "Encryption/Encryption.h"
#include "Save/Block.h"
#include "Utils/SCXorShift32.h"
#include "Utils/HelperUtilities.h"
#include "Globals.h"     // SIZE_HASH_IN_BYTES

// Logging stubs. Logger.cpp writes a rotating log through POSIX dirent, which MSVC has no header
// for -- and a harness has no use for the log anyway. Defining the two entry points here keeps
// Logger.cpp out of the host build without the save layer noticing.
namespace Utils {
    void logInfoToFile(const char*)  {}
    void logInfoToFile(std::string)  {}
    void logErrorToFile(const char*) {}
    void logErrorToFile(std::string) {}
    void logInfoToFile(const char*, const char*)  {}
    void logErrorToFile(const char*, const char*) {}
    void logTest(const std::string&)        {}
    void logTestSession(const std::string&) {}
}

namespace {

std::vector<uint8_t> readFile(const std::string& path) {
    std::vector<uint8_t> out;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return out;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n > 0) {
        out.resize(static_cast<size_t>(n));
        if (std::fread(out.data(), 1, out.size(), f) != out.size()) out.clear();
    }
    std::fclose(f);
    return out;
}

int g_pass = 0, g_fail = 0;

/**
 * Compare and report. Prints the first few differing offsets rather than just a count: on a save
 * file "1834 bytes differ" tells you nothing, whereas the offsets say which block is at fault.
 */
int g_known = 0;

/**
 * `known` explains a difference PKSE produces ON PURPOSE. Such a case reports KNOWN and does not
 * fail the run, but still prints its byte count, because the count is the regression signal: if
 * "empty party slots" stops being 4 bytes on this save, something else moved.
 *
 * Categorising these matters. A harness that reports failures it will never fix trains everyone to
 * ignore it, and then it catches nothing.
 */
bool compareBytes(const std::string& label, const std::vector<uint8_t>& before, const std::vector<uint8_t>& after, const char* known = nullptr) {
    if (before.size() != after.size()) {
        std::printf("  FAIL  %-22s size changed: %zu -> %zu\n", label.c_str(), before.size(), after.size());
        // Still compare the common prefix. Whether the part we DID write matches decides what kind
        // of bug this is: a byte-perfect prefix means parsing simply stopped early and the tail is
        // unmodelled, while a mismatch inside the prefix means the parse went off the rails.
        const size_t common = std::min(before.size(), after.size());
        size_t firstDiff = common;
        for (size_t i = 0; i < common; ++i) {
            if (before[i] != after[i]) { firstDiff = i; break; }
        }
        if (firstDiff == common)
            std::printf("        common prefix of %zu bytes is IDENTICAL - the parse stopped early,\n        it did not corrupt what it wrote\n", common);
        else
            std::printf("        prefix diverges at 0x%zX - the parse itself is wrong\n", firstDiff);
        ++g_fail;
        return false;
    }
    size_t diffs = 0, firstShown = 0;
    for (size_t i = 0; i < before.size(); ++i) {
        if (before[i] == after[i]) continue;
        ++diffs;
        // Offsets only for genuine failures; a known divergence just needs its count.
        if (!known && firstShown < 8) {
            std::printf("        offset 0x%06zX: %02X -> %02X\n", i, before[i], after[i]);
            ++firstShown;
        }
    }
    if (diffs == 0) {
        std::printf("  PASS  %-22s %zu bytes identical\n", label.c_str(), before.size());
        ++g_pass;
        return true;
    }
    if (known) {
        std::printf("  KNOWN %-22s %zu byte(s) - %s\n", label.c_str(), diffs, known);
        ++g_known;
        return true;
    }
    std::printf("  FAIL  %-22s %zu byte(s) differ\n", label.c_str(), diffs);
    ++g_fail;
    return false;
}

// Which writers to run. Isolating them matters: "the write path changed 13136 bytes" is not
// actionable, whereas "updateItemBlock changed 13136 bytes" names the function to open.
enum Writer { W_None = 0, W_Party = 1, W_Items = 2, W_Boxes = 4, W_BoxNames = 8, W_All = 15 };

template <typename T>
void runUpdates(T& trainer, int which) {
    if (which & W_Party)    trainer.updatePartyBlock();
    if (which & W_Items)    trainer.updateItemBlock();
    if (which & W_Boxes)    trainer.updateBoxBlock();
    if (which & W_BoxNames) trainer.updateBoxNameBlock();
}

/// Gen 3: the container IS the byte vector, so this is the most direct round trip of the set --
/// and the most intricate container (rotated sectors, per-section checksums, security key).
void roundTripFRLG(const std::string& path) {
    const std::vector<uint8_t> before = readFile(path);
    if (before.empty()) {
        std::printf("  SKIP  %-22s (%s)\n", "FireRed", SaveTree::missing(g_saveRoot, "FireRed").c_str());
        return;
    }

    // Same per-writer isolation as the SCBlock games, re-reading the save each time.
    //
    // NOTHING HERE IS ALLOWED TO DIVERGE, party writer included. A party writer that stamps an
    // all-zero blank over each unused slot and recomputes the battle stats of the occupied ones costs
    // four bytes on this save and seven on a real Ruby. Both are wrong for the same reason -- an empty
    // Gen 3 slot carries 0xFF at +0x55 (mail id "none"), and Gen 3 stores the stats it last computed
    // rather than the stats the formula gives right now. Neither is a normalisation PKSE gets to make
    // on a save it was only asked to read, so a reappearing byte count is a FAILURE.
    const struct { const char* name; int which; const char* known; } steps[] = {
        { "party",     W_Party,    nullptr },
        { "items",     W_Items,    nullptr },
        { "boxes",     W_Boxes,    nullptr },
        { "box names", W_BoxNames, nullptr },
        { "all",       W_All,      nullptr },
    };
    for (const auto& s : steps) {
        Trainer::Trainer3FRLG trainer(before, "FireRed_e.sav");
        runUpdates(trainer, s.which);
        trainer.finalizeChecksums();
        compareBytes(std::string("FireRed/LeafGreen [") + s.name + "]", before, trainer.getSaveData(), s.known);
    }
    // serialize() is what the app's write path calls: every writer above PLUS the Pokedex, the
    // current box and the trainer block. The per-writer steps cannot see those three.
    {
        Trainer::Trainer3FRLG trainer(before, "FireRed_e.sav");
        compareBytes("FireRed/LeafGreen [serialize]", before, trainer.serialize(), nullptr);
    }
}

/// Ruby / Sapphire / Emerald. Same container as FireRed and the same rule: an unedited save must
/// come back byte for byte. Two things differ from roundTripFRLG.
///
/// One is the RTC FOOTER. Every one of these files an emulator produces is 0x20008..0x20030 bytes
/// rather than 0x20000 -- the clock state is appended after the save -- and PKSE trims it exactly as
/// Save::openExternalSave does before handing the bytes to a Trainer. The comparison is against the
/// TRIMMED file, because that is the image the save layer owns; putting the footer back on the way
/// out is saveExternalSave's job and needs the Switch filesystem, so it is not reachable from here.
///
/// The other is that there are THREE titles rather than one pair, and each gets its own folder --
/// Ruby and Sapphire are byte-identical formats with no version byte, so only the folder can say
/// which. Emerald is genuinely detectable (it alone carries a security key) and is checked for it.
void roundTripRSE(const char* title, const std::string& path, const std::string& root) {
    std::vector<uint8_t> before = readFile(path);
    if (before.empty()) { std::printf("  SKIP  %-22s (%s)\n", title, SaveTree::missing(root, title).c_str()); return; }

    const size_t rawSize = before.size();
    if (Save::stripRtcFooter(before))
        std::printf("        %s: trimmed a %zu-byte RTC footer (%zu -> %zu)\n", title, rawSize - before.size(), rawSize, before.size());

    {
        Trainer::Trainer3RSE probe(before, "probe.sav");
        if (!probe.isValid()) {
            std::printf("  FAIL  %-22s did not parse as Ruby/Sapphire/Emerald\n", title);
            ++g_fail;
            return;
        }
        std::printf("        %s: layout %s, OT \"%s\", %zu party, key 0x%08X\n", title, probe.layout() == Trainer::Gen3HoennLayout::Emerald ? "Emerald" : "Ruby/Sapphire", probe.trainerName.c_str(), probe.party.size(), probe.securityKey());
        // The layout is what decides the bag offsets and whether money is keyed, so a save that
        // resolved to the wrong one would still round-trip (nothing was edited) while showing the
        // user a bag full of nonsense. Check it against the folder, which is the one thing that knows.
        const bool wantEmerald = std::string(title) == "Emerald";
        const bool gotEmerald = probe.layout() == Trainer::Gen3HoennLayout::Emerald;
        if (wantEmerald != gotEmerald) {
            std::printf("  FAIL  %-22s resolved to the %s layout\n", title, gotEmerald ? "Emerald" : "Ruby/Sapphire");
            ++g_fail;
        }
    }

    // Per-writer isolation, re-reading the save each time, exactly as FireRed does above --
    // and with no known-divergence allowance, for the same reason.
    const struct { const char* name; int which; } steps[] = {
        { "party",     W_Party    },
        { "items",     W_Items    },
        { "boxes",     W_Boxes    },
        { "box names", W_BoxNames },
        { "all",       W_All      },
    };
    for (const auto& s : steps) {
        Trainer::Trainer3RSE trainer(before, "probe.sav");
        runUpdates(trainer, s.which);
        trainer.finalizeChecksums();
        compareBytes(std::string(title) + " [" + s.name + "]", before, trainer.getSaveData(), nullptr);
    }
    // serialize() is the app's write path; see roundTripFRLG.
    {
        Trainer::Trainer3RSE trainer(before, "probe.sav");
        compareBytes(std::string(title) + " [serialize]", before, trainer.serialize(), nullptr);
    }
}

/// Gen 3: a save interrupted mid-write leaves its NEWER slot missing a sector, and the game loads the
/// older, complete one. PKSE has to as well (PKHeX SAV3.GetActiveSlot): when detect() accepts the
/// file but the parser takes the broken slot, the file browser lets the save through and it then
/// fails to open. Built from a real save by erasing one sector of the newer slot, and compared with
/// the same save with that whole slot erased, which every slot rule reads the same way.
template <typename TrainerT>
void interruptedSaveUsesOlderSlot(const char* title, const std::string& path) {
    const std::string label = std::string(title) + " [interrupted]";
    std::vector<uint8_t> save = readFile(path);
    if (save.empty()) { std::printf("  SKIP  %-22s (%s)\n", label.c_str(), SaveTree::missing(g_saveRoot, title).c_str()); return; }
    Save::stripRtcFooter(save);

    constexpr size_t slotA = 0x0000, slotB = 0xE000, sectorSize = 0x1000, sectorCount = 14;
    constexpr size_t sectorIdOffset = 0xFF4, sectorCounterOffset = 0xFFC;
    auto sectorId = [&](size_t slotBase, size_t sectorIndex) {
        return Utils::readUInt16LittleEndian(&save[slotBase + sectorIndex * sectorSize + sectorIdOffset]);
    };
    auto slotCounter = [&](size_t slotBase) -> uint32_t {
        for (size_t sectorIndex = 0; sectorIndex < sectorCount; ++sectorIndex)
            if (sectorId(slotBase, sectorIndex) == 0)
                return Utils::readUInt32LittleEndian(&save[slotBase + sectorIndex * sectorSize + sectorCounterOffset]);
        return 0;
    };
    const size_t newerSlot = slotCounter(slotB) > slotCounter(slotA) ? slotB : slotA;
    size_t erasedSector = 0;
    while (erasedSector < sectorCount && sectorId(newerSlot, erasedSector) == 0) ++erasedSector;

    std::vector<uint8_t> interrupted = save;
    const auto erasedStart = interrupted.begin() + static_cast<std::ptrdiff_t>(newerSlot + erasedSector * sectorSize);
    std::fill(erasedStart, erasedStart + sectorSize, 0xFF);
    std::vector<uint8_t> olderSlotOnly = save;
    const auto newerStart = olderSlotOnly.begin() + static_cast<std::ptrdiff_t>(newerSlot);
    std::fill(newerStart, newerStart + sectorCount * sectorSize, 0xFF);

    const TrainerT expected(olderSlotOnly, "probe.sav");
    const TrainerT trainer(interrupted, "probe.sav");
    const bool detected = TrainerT::detect(interrupted);
    const bool sameSave = trainer.isValid() && expected.isValid() && trainer.trainerName == expected.trainerName && trainer.money == expected.money && trainer.party.size() == expected.party.size();
    if (detected && sameSave) {
        std::printf("  PASS  %-22s opens from the older slot (OT \"%s\")\n", label.c_str(), trainer.trainerName.c_str());
        ++g_pass;
        return;
    }
    std::printf("  FAIL  %-22s detect=%d valid=%d, OT \"%s\" where the older slot holds \"%s\"\n", label.c_str(), detected, trainer.isValid(), trainer.trainerName.c_str(), expected.trainerName.c_str());
    ++g_fail;
}

/// SCBlock games: decrypt -> parse -> re-serialize -> encrypt. Exercises the block layer as well
/// as each game's own writers.
template <typename TrainerT>
void roundTripSCBlock(const char* label, const std::string& path) {
    std::vector<uint8_t> before = readFile(path);
    if (before.empty()) {
        std::printf("  SKIP  %-22s (%s)\n", label, SaveTree::missing(g_saveRoot, label).c_str());
        return;
    }

    // Does the FILE validate? PKHeX assumes the hash is good before it parses, and treats the whole
    // payload as blocks. So if the hash checks out, any early stop is our reader's fault, not a
    // damaged or unusual save -- which is the difference between "fix PKSE" and "this file is odd".
    {
        const size_t payloadLen = before.size() - SIZE_HASH_IN_BYTES;
        uint8_t want[SIZE_HASH_IN_BYTES];
        Encryption::computeHash(before.data(), payloadLen, want);
        const bool ok = std::memcmp(want, before.data() + payloadLen, SIZE_HASH_IN_BYTES) == 0;
        std::printf("        file hash: %s\n", ok ? "VALID" : "MISMATCH (file damaged or not this format)");
    }

    // Report parse COVERAGE, not just success. serializeAllBlocks re-emits only what parsed, and
    // the save path writes that as the whole file -- so a parse that stops short means the write
    // truncates the save. Without this the symptom is just "the file got smaller", with no clue why.
    std::vector<uint8_t> payload(before.begin(), before.end() - SIZE_HASH_IN_BYTES);
    Encryption::cryptStaticXorpadBytes(payload, payload.size());
    size_t consumed = 0;
    const std::vector<Save::Block> probe = Save::parseAllBlocks(payload.data(), payload.size(), &consumed);
    std::printf("        %zu blocks, parsed %zu / %zu payload bytes (%.2f%%)\n", probe.size(), consumed, payload.size(), payload.empty() ? 0.0 : 100.0 * static_cast<double>(consumed) / static_cast<double>(payload.size()));

    // If it stopped short, name the block that stopped it. "Parsing failed" is not actionable;
    // "type code 0x0B on key 0x________" points straight at getTypeSize().
    if (consumed + 5 <= payload.size()) {
        const uint32_t key = Utils::readUInt32LittleEndian(payload.data() + consumed);
        Utils::SCXorShift32 xk(key);
        const uint8_t type = payload[consumed + 4] ^ xk.Next();
        std::printf("        STOPPED at 0x%zX on key 0x%08X, type code 0x%02X (%u) -- %zu bytes dropped\n", consumed, key, type, type, payload.size() - consumed);
    }

    std::vector<Save::Block> blocks = Encryption::decrypt(before.data(), before.size());
    if (blocks.empty()) { std::printf("  FAIL  %-22s decrypt produced no blocks\n", label); ++g_fail; return; }

    // Two separate round trips, because they blame completely different code.
    //   BLOCK LAYER: decrypt -> encrypt, touching nothing. Any difference here is parse/serialize.
    //   WRITE PATH : the same, but with every update*Block() in between. A difference that appears
    //                only here belongs to one of those writers re-emitting a block differently.
    // Running only the second (as this harness first did) can't tell the two apart.
    compareBytes(std::string(label) + " [blocks]", before, Encryption::encrypt(blocks));

    // Then each writer alone, re-parsing from scratch each time so they can't contaminate each
    // other. A writer that changes bytes here is rewriting a block it was given nothing to change.
    // updateBoxBlock deliberately normalises EMPTY slots: it stamps one canonical encrypted blank
    // over every unoccupied slot, because a slot holding leftover bytes that decrypt to species 0
    // re-encrypts into a Bad Egg (the ghost-slot fix). The game reads those slots as empty
    // either way, so the rewrite is intended rather than a defect.
    static constexpr const char* emptySlotsNote = "empty box slots normalised to a canonical blank";
    const struct { const char* name; int which; const char* known; } steps[] = {
        { "party",     W_Party,    nullptr    },
        { "items",     W_Items,    nullptr    },
        { "boxes",     W_Boxes,    emptySlotsNote },
        { "box names", W_BoxNames, nullptr    },
        { "all",       W_All,      emptySlotsNote },
    };
    for (const auto& s : steps) {
        std::vector<Save::Block> fresh = Encryption::decrypt(before.data(), before.size());
        TrainerT trainer(std::move(fresh));
        runUpdates(trainer, s.which);
        compareBytes(std::string(label) + " [" + s.name + "]", before, Encryption::encrypt(trainer.getBlocks()), s.known);
    }
}

/// Sword/Shield: an EDITED bag must move nothing the edit did not touch.
///
/// The no-op round trip proves an untouched bag comes back, but it cannot tell a writer that packs
/// each pouch from its first slot from one that writes it back positionally on any pouch without a
/// gap -- and the game leaves gaps: a real Sword save keeps used-up items in the last slots of five
/// pouches. So this changes the count of the LAST entry in every pouch, appends a new item to every
/// pouch with room, and requires the written item block to equal the original with exactly those
/// slots changed: the edited entry where it was, the new one in the pouch's first empty slot.
void itemEditsStayInPlaceSWSH(const char* label, const std::string& path) {
    std::vector<uint8_t> before = readFile(path);
    if (before.empty()) {
        std::printf("  SKIP  %-22s (%s)\n", label, SaveTree::missing(g_saveRoot, label).c_str());
        return;
    }

    Trainer::Trainer8SWSH trainer(Encryption::decrypt(before.data(), before.size()));
    std::vector<uint8_t> expectedItemBlock;
    for (const auto& block : trainer.getBlocks()) {
        if (block.key == Trainer::ITEM8_SWSH) {
            expectedItemBlock = block.data;
        }
    }
    size_t pouchLayoutSize = 0;
    for (size_t pouchIndex = 0; pouchIndex < trainer.items.size(); ++pouchIndex) {
        const Trainer::PouchInfo8SWSH& info = Trainer::getPouchInfo8SWSH(static_cast<Trainer::PouchType8SWSH>(pouchIndex));
        pouchLayoutSize = std::max(pouchLayoutSize, static_cast<size_t>(info.offset + info.maxCount * 4));
    }
    if (trainer.items.empty() || expectedItemBlock.size() < pouchLayoutSize) {
        std::printf("  FAIL  %-22s no complete item block to edit\n", (std::string(label) + " [item edits]").c_str());
        ++g_fail;
        return;
    }

    size_t editedEntryCount = 0;
    size_t parkedEntryCount = 0;
    size_t appendedItemCount = 0;
    for (size_t pouchIndex = 0; pouchIndex < trainer.items.size(); ++pouchIndex) {
        const Trainer::PouchInfo8SWSH& info = Trainer::getPouchInfo8SWSH(static_cast<Trainer::PouchType8SWSH>(pouchIndex));
        std::vector<Trainer::InventoryItem>& pouch = trainer.items[pouchIndex];
        uint8_t* pouchBytes = expectedItemBlock.data() + info.offset;
        const size_t slotCount = static_cast<size_t>(info.maxCount);

        size_t firstEmptySlot = slotCount;
        size_t lastOccupiedSlot = slotCount;
        for (size_t slotIndex = 0; slotIndex < slotCount; ++slotIndex) {
            const uint32_t itemValue = Utils::readUInt32LittleEndian(pouchBytes + slotIndex * 4);
            if (Trainer::InventoryItem8SWSH::fromValue(itemValue).itemId != 0) {
                lastOccupiedSlot = slotIndex;
            } else if (firstEmptySlot == slotCount) {
                firstEmptySlot = slotIndex;
            }
        }

        // The entries are read in slot order, so the pouch's last entry is its last occupied slot.
        if (!pouch.empty() && lastOccupiedSlot < slotCount) {
            Trainer::InventoryItem& lastItem = pouch.back();
            lastItem.count = static_cast<uint16_t>(lastItem.count == 5 ? 6 : 5);
            uint8_t* lastItemBytes = pouchBytes + lastOccupiedSlot * 4;
            Trainer::InventoryItem8SWSH editedEntry = Trainer::InventoryItem8SWSH::fromValue(Utils::readUInt32LittleEndian(lastItemBytes));
            editedEntry.count = lastItem.count;
            Utils::writeUInt32LittleEndian(lastItemBytes, editedEntry.toValue());
            ++editedEntryCount;
            if (firstEmptySlot < lastOccupiedSlot) {
                ++parkedEntryCount;
            }
        }

        if (firstEmptySlot < slotCount) {
            uint16_t newItemId = 1;
            while (std::any_of(pouch.begin(), pouch.end(), [newItemId](const Trainer::InventoryItem& item) { return item.itemId == newItemId; })) {
                ++newItemId;
            }
            pouch.push_back(Trainer::InventoryItem{newItemId, 1, true, false});
            Trainer::InventoryItem8SWSH appendedEntry;
            appendedEntry.itemId = newItemId;
            appendedEntry.count = 1;
            appendedEntry.isNew = true;
            appendedEntry.isFavorite = false;
            Utils::writeUInt32LittleEndian(pouchBytes + firstEmptySlot * 4, appendedEntry.toValue());
            ++appendedItemCount;
        }
    }

    std::printf("        %s: edited %zu pouch tails (%zu parked after an empty slot), appended %zu items\n", label, editedEntryCount, parkedEntryCount, appendedItemCount);
    if (parkedEntryCount == 0) {
        std::printf("        no pouch on this save has a gap, so packing and positional writing agree here\n");
    }

    trainer.updateItemBlock();
    for (const auto& block : trainer.getBlocks()) {
        if (block.key == Trainer::ITEM8_SWSH) {
            compareBytes(std::string(label) + " [item edits]", expectedItemBlock, block.data);
        }
    }
}

/// Legends: Arceus: the Pokedex writer must count a Pokemon toward its species' research once, when it
/// arrives, exactly as PKHeX's OnPokeGet does -- one catch, one alpha for an alpha, one for each size
/// task the species has and the Pokemon's size meets -- and never again. Re-registering storage on every
/// save once added one per stored Pokemon per save, and an unedited round trip cannot see it because
/// every other byte that writer touches is a flag it may legitimately set.
void catchResearchCountsOnceLA(const char* label, const std::string& path) {
    std::vector<uint8_t> before = readFile(path);
    if (before.empty()) {
        std::printf("  SKIP  %-22s (%s)\n", label, SaveTree::missing(g_saveRoot, label).c_str());
        return;
    }
    using Field = Trainer::ResearchField8LA;
    using SizeTask = Pokemon::SizeResearchTask8LA;
    const Field countFields[] = { Field::CatchCount, Field::AlphaCount, Field::LargeCount, Field::SmallCount, Field::HeavyCount, Field::LightCount };
    auto pokedexBytes = [](const Trainer::Trainer8LA& trainer) {
        for (const auto& block : trainer.getBlocks()) {
            if (block.key == Trainer::POKEDEX8_LA) {
                return block.data;
            }
        }
        return std::vector<uint8_t>();
    };
    // Every research field of every species, plus the next update counter: what a write must not move.
    auto changedFieldCount = [&](const std::vector<uint8_t>& beforeBytes, const std::vector<uint8_t>& afterBytes) {
        size_t fieldCount = Trainer::Trainer8LA::nextResearchUpdateCounter(beforeBytes) != Trainer::Trainer8LA::nextResearchUpdateCounter(afterBytes);
        for (uint16_t speciesId = 1; speciesId < Pokemon::DEX8LA_MAX_SPECIES; ++speciesId) {
            for (Field field : countFields) {
                fieldCount += Trainer::Trainer8LA::researchField(beforeBytes, speciesId, field) != Trainer::Trainer8LA::researchField(afterBytes, speciesId, field);
            }
            fieldCount += Trainer::Trainer8LA::researchField(beforeBytes, speciesId, Field::UpdateCounter) != Trainer::Trainer8LA::researchField(afterBytes, speciesId, Field::UpdateCounter);
        }
        return fieldCount;
    };
    auto writeFloat = [](Pokemon::Pokemon& pokemon, size_t offset, float floatValue) {
        std::memcpy(pokemon.getData().data() + offset, &floatValue, sizeof floatValue);
    };
    int failureCount = 0;
    auto report = [&](bool passed, const std::string& what) {
        std::printf("        %s %s\n", passed ? "ok  " : "FAIL", what.c_str());
        failureCount += passed ? 0 : 1;
    };
    const std::string testLabel = std::string(label) + " [catch research]";

    Trainer::Trainer8LA trainer(Encryption::decrypt(before.data(), before.size()));
    const std::vector<uint8_t> loadedPokedex = pokedexBytes(trainer);
    trainer.updatePokedexBlock();
    trainer.updatePokedexBlock();
    report(changedFieldCount(loadedPokedex, pokedexBytes(trainer)) == 0, "two unedited writes change no research field");

    // The source: a non-alpha whose species has a size task, so the arrivals below can exercise one.
    const Pokemon::Pokemon8LA* sourcePokemon = nullptr;
    SizeTask sourceTask = SizeTask::CatchLarge;
    uint8_t sourceThreshold = 0;
    std::vector<std::unique_ptr<::Pokemon::Pokemon>*> emptySlots;
    for (auto& box : trainer.boxes) {
        for (auto& slotPokemon : box) {
            if (!slotPokemon || slotPokemon->speciesID() == 0) {
                emptySlots.push_back(&slotPokemon);
                continue;
            }
            const auto* candidate = static_cast<const Pokemon::Pokemon8LA*>(slotPokemon.get());
            for (SizeTask task : { SizeTask::CatchLarge, SizeTask::CatchSmall, SizeTask::CatchHeavy, SizeTask::CatchLight }) {
                if (!sourcePokemon && !candidate->isAlpha() && Pokemon::getSizeResearchThreshold8LA(candidate->speciesID(), task, sourceThreshold)) {
                    sourcePokemon = candidate;
                    sourceTask = task;
                }
            }
        }
    }
    if (!sourcePokemon || emptySlots.size() < 4) {
        std::printf("  FAIL  %-22s no non-alpha Pokemon with a size task, or fewer than four empty box slots\n", testLabel.c_str());
        ++g_fail;
        return;
    }
    const uint16_t speciesId = sourcePokemon->speciesID();
    const bool taskIsWeight = sourceTask == SizeTask::CatchHeavy || sourceTask == SizeTask::CatchLight;
    const bool taskIsUpperBound = sourceTask == SizeTask::CatchSmall || sourceTask == SizeTask::CatchLight;
    const float thresholdValue = taskIsWeight ? static_cast<float>(sourceThreshold / 10.0) : static_cast<float>(sourceThreshold);
    const size_t sizeOffset = taskIsWeight ? 0xB0 : 0xAC;
    const size_t taskFieldIndex = 2 + static_cast<size_t>(sourceTask);
    std::printf("        species %u, size task %zu, threshold %u\n", speciesId, static_cast<size_t>(sourceTask), sourceThreshold);

    // Each arrival on its own, against the dex as it stands, then a second write that must change nothing.
    struct Arrival { const char* name; float sizeValue; bool alpha; std::vector<int> expectedIncrements; };
    const float meetingSize = taskIsUpperBound ? thresholdValue * 0.5f : thresholdValue * 2.0f;
    const float missingSize = taskIsUpperBound ? thresholdValue * 2.0f : thresholdValue * 0.5f;
    std::vector<Arrival> arrivals = {
        { "meets the size task", meetingSize, false, { 1, 0, 0, 0, 0, 0 } },
        { "misses the size task", missingSize, false, { 1, 0, 0, 0, 0, 0 } },
        { "an alpha that meets it", meetingSize, true, { 1, 1, 0, 0, 0, 0 } },
        { "no recorded size", 0.0f, false, { 1, 0, 0, 0, 0, 0 } },
    };
    arrivals[0].expectedIncrements[taskFieldIndex] = 1;
    arrivals[2].expectedIncrements[taskFieldIndex] = 1;
    for (size_t arrivalIndex = 0; arrivalIndex < arrivals.size(); ++arrivalIndex) {
        const Arrival& arrival = arrivals[arrivalIndex];
        auto arrivingPokemon = sourcePokemon->clone();
        writeFloat(*arrivingPokemon, sizeOffset, arrival.sizeValue);
        if (arrival.alpha) {
            arrivingPokemon->getData()[0x16] |= std::byte{0x20};
        }
        arrivingPokemon->refreshChecksum();
        const std::vector<uint8_t> beforeArrival = pokedexBytes(trainer);
        *emptySlots[arrivalIndex] = std::move(arrivingPokemon);
        trainer.updatePokedexBlock();
        const std::vector<uint8_t> afterArrival = pokedexBytes(trainer);
        bool countsMatch = true;
        std::string countText;
        for (size_t fieldIndex = 0; fieldIndex < std::size(countFields); ++fieldIndex) {
            const int increment = Trainer::Trainer8LA::researchField(afterArrival, speciesId, countFields[fieldIndex]) - Trainer::Trainer8LA::researchField(beforeArrival, speciesId, countFields[fieldIndex]);
            countsMatch = countsMatch && increment == arrival.expectedIncrements[fieldIndex];
            countText += (fieldIndex ? "/" : "") + std::to_string(increment);
        }
        size_t otherSpeciesChanges = 0;
        for (uint16_t otherSpeciesId = 1; otherSpeciesId < Pokemon::DEX8LA_MAX_SPECIES; ++otherSpeciesId) {
            if (otherSpeciesId == speciesId) {
                continue;
            }
            for (Field field : countFields) {
                otherSpeciesChanges += Trainer::Trainer8LA::researchField(beforeArrival, otherSpeciesId, field) != Trainer::Trainer8LA::researchField(afterArrival, otherSpeciesId, field);
            }
        }
        report(countsMatch && otherSpeciesChanges == 0, std::string("arrival that ") + arrival.name + ": catch/alpha/large/small/heavy/light +" + countText + (otherSpeciesChanges ? ", other species changed" : ""));
        trainer.updatePokedexBlock();
        report(changedFieldCount(afterArrival, pokedexBytes(trainer)) == 0, std::string("  ...and the next write counts it no further"));
    }

    // The absolutes PKSE gives a converted or created Pokemon must be the ones the game computes: every
    // Pokemon this save holds was sized by the game itself.
    size_t sizedPokemonCount = 0;
    size_t mismatchedPokemonCount = 0;
    Trainer::Trainer8LA freshTrainer(Encryption::decrypt(before.data(), before.size()));
    std::vector<const ::Pokemon::Pokemon*> storedPokemonList;
    for (const auto& partyPokemon : freshTrainer.party) {
        storedPokemonList.push_back(partyPokemon.get());
    }
    for (const auto& box : freshTrainer.boxes) {
        for (const auto& slotPokemon : box) {
            storedPokemonList.push_back(slotPokemon.get());
        }
    }
    {
        for (const ::Pokemon::Pokemon* slotPokemon : storedPokemonList) {
            if (!slotPokemon || slotPokemon->speciesID() == 0) {
                continue;
            }
            const auto& storedPokemon = static_cast<const Pokemon::Pokemon8LA&>(*slotPokemon);
            auto resizedPokemon = storedPokemon.clone();
            auto& resized = static_cast<Pokemon::Pokemon8LA&>(*resizedPokemon);
            resized.resetSizeAbsolutes();
            const float storedHeight = storedPokemon.heightAbsolute();
            const float storedWeight = storedPokemon.weightAbsolute();
            const float resizedHeight = resized.heightAbsolute();
            const float resizedWeight = resized.weightAbsolute();
            ++sizedPokemonCount;
            if (std::memcmp(&storedHeight, &resizedHeight, sizeof(float)) != 0 || std::memcmp(&storedWeight, &resizedWeight, sizeof(float)) != 0) {
                if (mismatchedPokemonCount == 0) {
                    std::printf("        first size mismatch: species %u stored %.9g/%.9g computed %.9g/%.9g\n", storedPokemon.speciesID(), storedHeight, storedWeight, resizedHeight, resizedWeight);
                }
                ++mismatchedPokemonCount;
            }
        }
    }
    report(sizedPokemonCount != 0 && mismatchedPokemonCount == 0, "resetSizeAbsolutes reproduces the game's sizes bit for bit (" + std::to_string(sizedPokemonCount - mismatchedPokemonCount) + " of " + std::to_string(sizedPokemonCount) + ")");

    if (failureCount == 0) {
        std::printf("  PASS  %-22s each arrival counts once, as PKHeX OnPokeGet does; storage is never re-counted\n", testLabel.c_str());
        ++g_pass;
    } else {
        std::printf("  FAIL  %-22s %d research check(s) failed\n", testLabel.c_str(), failureCount);
        ++g_fail;
    }
}

/// Sword/Shield: REMOVING an item must empty its slot and move nothing else.
///
/// Items are matched to the slot they were read from by item id rather than by list position,
/// which is what this proves: an erase shifts every later entry down one place in the list, so a
/// position-matched writer would send all of them to new slots -- on this save it moved a used-up
/// Max Potion from slot 59 to slot 16 without being asked to.
void itemRemovalEmptiesOneSlotSWSH(const char* label, const std::string& path) {
    std::vector<uint8_t> before = readFile(path);
    if (before.empty()) {
        std::printf("  SKIP  %-22s (%s)\n", label, SaveTree::missing(g_saveRoot, label).c_str());
        return;
    }

    Trainer::Trainer8SWSH trainer(Encryption::decrypt(before.data(), before.size()));
    std::vector<uint8_t> expectedItemBlock;
    for (const auto& block : trainer.getBlocks()) {
        if (block.key == Trainer::ITEM8_SWSH) {
            expectedItemBlock = block.data;
        }
    }
    if (trainer.items.empty() || expectedItemBlock.empty()) {
        std::printf("  FAIL  %-22s no item block to edit\n", (std::string(label) + " [item removal]").c_str());
        ++g_fail;
        return;
    }

    size_t removedItemCount = 0;
    for (size_t pouchIndex = 0; pouchIndex < trainer.items.size(); ++pouchIndex) {
        const Trainer::PouchInfo8SWSH& info = Trainer::getPouchInfo8SWSH(static_cast<Trainer::PouchType8SWSH>(pouchIndex));
        std::vector<Trainer::InventoryItem>& pouch = trainer.items[pouchIndex];
        if (pouch.empty()) {
            continue;
        }
        uint8_t* pouchBytes = expectedItemBlock.data() + info.offset;
        for (size_t slotIndex = 0; slotIndex < static_cast<size_t>(info.maxCount); ++slotIndex) {
            if (Trainer::InventoryItem8SWSH::fromValue(Utils::readUInt32LittleEndian(pouchBytes + slotIndex * 4)).itemId == 0) {
                continue;
            }
            Utils::writeUInt32LittleEndian(pouchBytes + slotIndex * 4, 0);  // the first entry, now gone
            break;
        }
        pouch.erase(pouch.begin());   // what the editor does on a slot-based game
        ++removedItemCount;
    }

    std::printf("        %s: removed the first item of %zu pouches\n", label, removedItemCount);
    trainer.updateItemBlock();
    for (const auto& block : trainer.getBlocks()) {
        if (block.key == Trainer::ITEM8_SWSH) {
            compareBytes(std::string(label) + " [item removal]", expectedItemBlock, block.data);
        }
    }
}

}

/// The Let's Go Pokedex's size records, as Trainer7LGPE.cpp lays them out: four groups (minimum and
/// maximum height, minimum and maximum weight) of 186 six-byte entries at 0xF78 in the Zukan block.
size_t sizeRecordOffsetLGPE(int sizeGroup, int entryIndex) {
    return 0xF78 + 6 * static_cast<size_t>(entryIndex + sizeGroup * 186);
}

/// The partner Pikachu (form 8) and partner Eevee (form 1). PKHeX's Zukan7b.SetDex registers
/// neither, because the dex has no entry for either form.
bool isPartnerLGPE(const Pokemon::Pokemon& pokemon) {
    return (pokemon.speciesID() == 25 && pokemon.form() == 8) || (pokemon.speciesID() == 133 && pokemon.form() == 1);
}

std::vector<uint8_t> zukanBlockLGPE(const Trainer::Trainer7LGPE& trainer) {
    for (const auto& block : trainer.getBlocks()) {
        if (block.key == Trainer::ZUKAN7_LGPE) {
            return block.data;
        }
    }
    return {};
}

/// Let's Go Pikachu/Eevee: the one format with its own block builder, and until now no harness wrote
/// one back at all, so an unedited write that changed bytes was only ever seen on the console. Each
/// writer runs alone first, re-parsing from scratch, so a difference names the writer that made it;
/// then "all" runs them in the order saveTrainerInfoLetsGo does, which is what a real save writes.
///
/// The Pokedex writer registers everything in storage on every write, so it may change a save whose
/// dex does not yet show a Pokemon it holds -- a Pokemon made shiny by an editor, say, whose shiny
/// sighting was never recorded. That is a KNOWN divergence, held to three rules instead: it touches
/// nothing but the Pokedex block, a second write changes nothing, and a partner Pikachu or Eevee
/// never writes its species' size records.
void roundTripLGPE(const char* label, const std::string& path) {
    const std::vector<uint8_t> before = readFile(path);
    if (before.size() < Trainer::SAVE_SIZE7_LGPE) {
        std::printf("  SKIP  %-22s (%s)\n", label, SaveTree::missing(g_saveRoot, label).c_str());
        return;
    }
    const std::vector<uint8_t> area(before.begin(), before.begin() + Trainer::SAVE_SIZE7_LGPE);
    auto report = [](bool passed, const std::string& what) {
        std::printf("  %s  %s\n", passed ? "PASS" : "FAIL", what.c_str());
        ++(passed ? g_pass : g_fail);
    };
    static constexpr const char* pokedexNote = "the Pokedex registered stored Pokemon its flags did not yet show";
    auto writeAll = [](Trainer::Trainer7LGPE& trainer) {
        trainer.updateItemBlock();
        trainer.updateBoxBlock();
        trainer.updatePartyBlock();
        trainer.updateTrainerInfoBlock();
        trainer.updatePokedexBlock();
    };
    const struct { const char* name; void (*write)(Trainer::Trainer7LGPE&); const char* known; } steps[] = {
        { "items",   [](Trainer::Trainer7LGPE& trainer) { trainer.updateItemBlock(); },                            nullptr },
        { "boxes",   [](Trainer::Trainer7LGPE& trainer) { trainer.updateBoxBlock(); },                             nullptr },
        { "party",   [](Trainer::Trainer7LGPE& trainer) { trainer.updateBoxBlock(); trainer.updatePartyBlock(); }, nullptr },
        { "trainer", [](Trainer::Trainer7LGPE& trainer) { trainer.updateTrainerInfoBlock(); },                     nullptr },
        { "pokedex", [](Trainer::Trainer7LGPE& trainer) { trainer.updatePokedexBlock(); },                         pokedexNote },
    };
    for (const auto& step : steps) {
        Trainer::Trainer7LGPE trainer(Trainer::createBlocksFromSaveData7LGPE(area));
        step.write(trainer);
        std::vector<uint8_t> after = before;
        Trainer::writeBlocksToSaveData7LGPE(after, trainer.getBlocks());
        compareBytes(std::string(label) + " [" + step.name + "]", before, after, step.known);
    }

    Trainer::Trainer7LGPE originalTrainer(Trainer::createBlocksFromSaveData7LGPE(area));
    Trainer::Trainer7LGPE firstTrainer(Trainer::createBlocksFromSaveData7LGPE(area));
    writeAll(firstTrainer);
    std::vector<uint8_t> firstWrite = before;
    Trainer::writeBlocksToSaveData7LGPE(firstWrite, firstTrainer.getBlocks());
    compareBytes(std::string(label) + " [all]", before, firstWrite, pokedexNote);

    // Only the Pokedex block may differ. Compared block by block after re-parsing what was written,
    // so a writer that strayed into another block is named rather than hidden in the byte count.
    const std::vector<uint8_t> firstArea(firstWrite.begin(), firstWrite.begin() + Trainer::SAVE_SIZE7_LGPE);
    Trainer::Trainer7LGPE reloadedTrainer(Trainer::createBlocksFromSaveData7LGPE(firstArea));
    std::string changedBlocks;
    for (const auto& originalBlock : originalTrainer.getBlocks()) {
        if (originalBlock.key == Trainer::ZUKAN7_LGPE) {
            continue;
        }
        for (const auto& reloadedBlock : reloadedTrainer.getBlocks()) {
            if (reloadedBlock.key == originalBlock.key && reloadedBlock.data != originalBlock.data) {
                changedBlocks += " " + std::to_string(originalBlock.key);
            }
        }
    }
    report(changedBlocks.empty(), std::string(label) + " [all] an unedited write changes no block but the Pokedex" + (changedBlocks.empty() ? "" : " (changed:" + changedBlocks + ")"));

    // Whatever the first write registered, the second must find nothing left to do.
    writeAll(reloadedTrainer);
    std::vector<uint8_t> secondWrite = firstWrite;
    Trainer::writeBlocksToSaveData7LGPE(secondWrite, reloadedTrainer.getBlocks());
    compareBytes(std::string(label) + " [second write]", firstWrite, secondWrite);

    // A partner Pikachu or Eevee must leave its species' size records alone. Checked only for a
    // species whose every stored Pokemon is the partner, so no other Pokemon could have moved them.
    for (const uint16_t partnerSpecies : { static_cast<uint16_t>(25), static_cast<uint16_t>(133) }) {
        size_t partnerCount = 0;
        size_t otherCount = 0;
        auto countPokemon = [&](const Pokemon::Pokemon* pokemon) {
            if (!pokemon || pokemon->speciesID() != partnerSpecies || pokemon->isEgg()) {
                return;
            }
            ++(isPartnerLGPE(*pokemon) ? partnerCount : otherCount);
        };
        for (const auto& pokemon : originalTrainer.party) {
            countPokemon(pokemon.get());
        }
        for (const auto& box : originalTrainer.boxes) {
            for (const auto& pokemon : box) {
                countPokemon(pokemon.get());
            }
        }
        if (partnerCount == 0 || otherCount != 0) {
            continue;
        }
        const std::vector<uint8_t> zukanBefore = zukanBlockLGPE(originalTrainer);
        const std::vector<uint8_t> zukanAfter = zukanBlockLGPE(firstTrainer);
        size_t changedRecordCount = 0;
        for (int sizeGroup = 0; sizeGroup < 4; ++sizeGroup) {
            const size_t recordOffset = sizeRecordOffsetLGPE(sizeGroup, partnerSpecies - 1);
            if (recordOffset + 6 <= zukanBefore.size() && recordOffset + 6 <= zukanAfter.size() && !std::equal(zukanBefore.begin() + recordOffset, zukanBefore.begin() + recordOffset + 6, zukanAfter.begin() + recordOffset)) {
                ++changedRecordCount;
            }
        }
        report(changedRecordCount == 0, std::string(label) + " [pokedex] the partner " + (partnerSpecies == 25 ? "Pikachu" : "Eevee") + " writes no size record (" + std::to_string(changedRecordCount) + " changed)");
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: roundtrip <save-root>\n  Expects, relative to <save-root>:\n  One folder per TITLE, any filename inside:\n    FireRed/            Let's Go Pikachu/\n    Sword/  Shield/  Violet/  Legends Z-A/  Legends Arceus/\n");
        return 2;
    }
    const std::string root = argv[1];
    g_saveRoot = root;

    std::printf("PKSE save round-trip harness\nread -> parse -> serialize back -> compare. A no-op edit must reproduce the file exactly.\n\n");

    // Found by TITLE FOLDER -- <root>/Shield/, <root>/Violet/ -- with any filename inside.
    // See SaveTree.h for why the folder is the authority and not the file name.
    roundTripFRLG(SaveTree::fireRed(root));
    roundTripRSE("Ruby",     SaveTree::ruby(root),     root);
    roundTripRSE("Sapphire", SaveTree::sapphire(root), root);
    roundTripRSE("Emerald",  SaveTree::emerald(root),  root);
    interruptedSaveUsesOlderSlot<Trainer::Trainer3FRLG>("FireRed", SaveTree::fireRed(root));
    interruptedSaveUsesOlderSlot<Trainer::Trainer3RSE>("Ruby",      SaveTree::ruby(root));
    interruptedSaveUsesOlderSlot<Trainer::Trainer3RSE>("Emerald",   SaveTree::emerald(root));
    const std::string swordPath = SaveTree::sword(root);
    const std::string shieldPath = SaveTree::shield(root);
    roundTripSCBlock<Trainer::Trainer8SWSH>("Sword",          swordPath);
    itemEditsStayInPlaceSWSH("Sword",                         swordPath);
    itemRemovalEmptiesOneSlotSWSH("Sword",                    swordPath);
    roundTripSCBlock<Trainer::Trainer8SWSH>("Shield",         shieldPath);
    itemEditsStayInPlaceSWSH("Shield",                        shieldPath);
    itemRemovalEmptiesOneSlotSWSH("Shield",                   shieldPath);
    roundTripSCBlock<Trainer::Trainer9LZA>("Legends Z-A",     SaveTree::legendsZA(root));
    roundTripSCBlock<Trainer::Trainer9SV>("Violet",           SaveTree::violet(root));
    roundTripSCBlock<Trainer::Trainer8LA>("Legends Arceus",   SaveTree::legendsArceus(root));
    catchResearchCountsOnceLA("Legends Arceus",                SaveTree::legendsArceus(root));
    roundTripLGPE("Let's Go Pikachu",                         SaveTree::letsGoPikachu(root));
    roundTripLGPE("Let's Go Eevee",                           SaveTree::letsGoEevee(root));

    std::printf("\n%d passed, %d known divergence(s), %d FAILED\n", g_pass, g_known, g_fail);
    if (g_known)
        std::printf("Known divergences are intended behaviour. Watch their byte counts: a change\nthere means something moved that nobody decided to move.\n");
    return g_fail == 0 ? 0 : 1;
}
