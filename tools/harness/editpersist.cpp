/**
 * roundtrip.cpp proves a NO-OP reproduces the file byte for byte. That is the write path's
 * regression gate, but it deliberately cannot answer the question users actually report:
 *
 *     "I changed a met level / added an item, saved, and nothing changed in-game."
 *
 * A writer that drops every edit on the floor passes roundtrip.cpp perfectly — the output
 * matches the input precisely because nothing was written. So this harness asks the other
 * question:
 *
 *     read a real save -> change one field -> serialize -> RE-PARSE the result
 *                      -> assert the new value is there, and the old one is gone
 *
 * Re-parsing is the point. Checking the in-memory object after an edit only proves the
 * setter ran; only a fresh parse of the serialized bytes proves the value survived the
 * block update, the re-encryption and the hash.
 *
 * Two edits are exercised per game, because those are the two the field reports name:
 *   - a party Pokemon's MET LEVEL (entity buffer -> party block)
 *   - an inventory item's COUNT   (inventory model -> item block)
 *
 * Runs on the HOST, not the Switch, like the other harnesses. It never writes to disk:
 * everything happens on an in-memory copy of the save.
 *
 * Build + run:  pwsh tools/harness/build.ps1 -Harness editpersist
 */
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "SaveTree.h" // saves are found by their TITLE FOLDER, not by a guessed filename

/// The save root, so a skip can name the folder the reader needs to create.
static std::string g_saveRoot;

#include "Encryption/Encryption.h"
#include "Globals.h"     // SIZE_HASH_IN_BYTES
#include "Pokemon/Pokemon.h"
#include "Save/Block.h"
#include "Trainer/Trainer8LA.h"
#include "Trainer/Trainer8SWSH.h"
#include "Trainer/Trainer9LZA.h"
#include "Trainer/Trainer9SV.h"

// Same logging stubs as the other harnesses -- Logger.cpp needs POSIX dirent.
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

int g_pass = 0, g_fail = 0, g_skip = 0;

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

void report(const char* label, const char* what, bool ok, const std::string& detail) {
    std::printf("  %s  %-18s %-12s %s\n", ok ? "PASS" : "FAIL", label, what, detail.c_str());
    if (ok) ++g_pass; else ++g_fail;
}

/// First occupied party slot, or nullptr.
::Pokemon::Pokemon* firstPartyMon(Trainer::Trainer& t) {
    for (auto& p : t.party)
        if (p && p->speciesID() != 0) return p.get();
    return nullptr;
}

/// First pouch holding at least one item; returns pouch index or -1.
int firstStockedPouch(Trainer::Trainer& t) {
    for (size_t i = 0; i < t.items.size(); ++i)
        if (!t.items[i].empty()) return static_cast<int>(i);
    return -1;
}

/**
 * One game: edit, serialize, re-parse, verify.
 *
 * The edits are chosen to be unmistakable rather than realistic. Met level moves to a value
 * the Pokemon demonstrably did not have, so "it worked" cannot be confused with "it was
 * already that". Item count does the same.
 */
template <typename TrainerT>
void checkSCBlock(const char* label, const std::string& path) {
    std::vector<uint8_t> raw = readFile(path);
    if (raw.empty()) {
        std::printf("  SKIP  %-18s (%s)\n", label, SaveTree::missing(g_saveRoot, label).c_str());
        ++g_skip;
        return;
    }

    std::vector<Save::Block> blocks = Encryption::decrypt(raw.data(), raw.size());
    TrainerT trainer(std::move(blocks));

    ::Pokemon::Pokemon* pokemon = firstPartyMon(trainer);
    const int pouch = firstStockedPouch(trainer);
    if (!pokemon || pouch < 0) {
        std::printf("  SKIP  %-18s (needs a party Pokemon and a stocked pouch)\n", label);
        ++g_skip;
        return;
    }

    const uint8_t  oldMet   = pokemon->metLevel();
    const uint8_t  newMet   = static_cast<uint8_t>(oldMet == 3 ? 4 : 3);
    const uint16_t itemId   = trainer.items[pouch][0].itemId;
    const uint16_t oldCount = trainer.items[pouch][0].count;
    const uint16_t newCount = static_cast<uint16_t>(oldCount == 7 ? 8 : 7);
    const std::string species = pokemon->species();

    pokemon->setMetLevel(newMet);
    pokemon->refreshChecksum();
    trainer.items[pouch][0].count = newCount;

    trainer.updatePartyBlock();
    trainer.updateItemBlock();

    const std::vector<uint8_t> written = Encryption::encrypt(trainer.getBlocks());
    std::vector<uint8_t> reread = written;
    std::vector<Save::Block> freshBlocks = Encryption::decrypt(reread.data(), reread.size());
    TrainerT fresh(std::move(freshBlocks));

    ::Pokemon::Pokemon* freshMon = firstPartyMon(fresh);
    if (!freshMon) {
        report(label, "met level", false, "party Pokemon vanished after the save/reload cycle");
        return;
    }

    {
        const uint8_t got = freshMon->metLevel();
        char buffer[192];
        std::snprintf(buffer, sizeof(buffer), "%s: %u -> %u, read back %u", species.c_str(), oldMet, newMet, got);
        report(label, "met level", got == newMet, buffer);
    }

    {
        uint16_t got = 0;
        bool found = false;
        for (const auto& it : fresh.items[pouch]) {
            if (it.itemId == itemId) { got = it.count; found = true; break; }
        }
        char buffer[192];
        std::snprintf(buffer, sizeof(buffer), "item id %u: %u -> %u, read back %u%s", itemId, oldCount, newCount, got, found ? "" : " (ITEM MISSING)");
        report(label, "item count", found && got == newCount, buffer);
    }

    // The checksum/hash must still validate, or the console would reject the file outright.
    {
        const size_t payloadLen = written.size() - SIZE_HASH_IN_BYTES;
        uint8_t want[SIZE_HASH_IN_BYTES];
        Encryption::computeHash(written.data(), payloadLen, want);
        const bool ok = std::memcmp(want, written.data() + payloadLen, SIZE_HASH_IN_BYTES) == 0;
        report(label, "save hash", ok, ok ? "valid after the edit" : "MISMATCH - the game would reject this");
    }
}

}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: editpersist <save-root>\n  Expects, relative to <save-root>:\n  One folder per TITLE, any filename inside:\n    Shield/  Violet/  Legends Z-A/  Legends Arceus/\n");
        return 2;
    }
    const std::string root = argv[1];
    g_saveRoot = root;

    std::printf("PKSE edit-persistence harness\nedit -> serialize -> RE-PARSE -> assert the new value survived. roundtrip.cpp\nproves a no-op reproduces the file; this proves an edit is not silently dropped.\n\n");

    // Found by TITLE FOLDER; see SaveTree.h.
    checkSCBlock<Trainer::Trainer9SV>("Violet",         SaveTree::violet(root));
    checkSCBlock<Trainer::Trainer8SWSH>("Shield",       SaveTree::shield(root));
    checkSCBlock<Trainer::Trainer9LZA>("Legends Z-A",   SaveTree::legendsZA(root));
    checkSCBlock<Trainer::Trainer8LA>("Legends Arceus", SaveTree::legendsArceus(root));

    std::printf("\n%d passed, %d FAILED, %d skipped\n", g_pass, g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
