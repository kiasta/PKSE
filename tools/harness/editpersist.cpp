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
namespace Utils
{
    void logInfoToFile(const char *) {}
    void logInfoToFile(std::string) {}
    void logErrorToFile(const char *) {}
    void logErrorToFile(std::string) {}
    void logInfoToFile(const char *, const char *) {}
    void logErrorToFile(const char *, const char *) {}
    void logTest(const std::string &) {}
    void logTestSession(const std::string &) {}
}

namespace
{

    int g_passCount = 0;
    int g_failCount = 0;
    int g_skipCount = 0;

    std::vector<uint8_t> readFile(const std::string &filePath)
    {
        std::vector<uint8_t> fileBytes;
        FILE *file = std::fopen(filePath.c_str(), "rb");
        if (!file)
        {
            return fileBytes;
        }
        std::fseek(file, 0, SEEK_END);
        const long fileSize = std::ftell(file);
        std::fseek(file, 0, SEEK_SET);
        if (fileSize > 0)
        {
            fileBytes.resize(static_cast<size_t>(fileSize));
            if (std::fread(fileBytes.data(), 1, fileBytes.size(), file) != fileBytes.size())
            {
                fileBytes.clear();
            }
        }
        std::fclose(file);
        return fileBytes;
    }

    void report(const char *gameLabel, const char *checkName, bool passed, const std::string &detail)
    {
        std::printf("  %s  %-18s %-12s %s\n", passed ? "PASS" : "FAIL", gameLabel, checkName, detail.c_str());
        if (passed)
        {
            ++g_passCount;
        }
        else
        {
            ++g_failCount;
        }
    }

    /// First occupied party slot, or nullptr.
    ::Pokemon::Pokemon *firstPartyPokemon(Trainer::Trainer &trainer)
    {
        for (auto &partySlot : trainer.party)
        {
            if (partySlot && partySlot->speciesID() != 0)
            {
                return partySlot.get();
            }
        }
        return nullptr;
    }

    /// First pouch holding at least one item; returns pouch index or -1.
    int firstStockedPouch(Trainer::Trainer &trainer)
    {
        for (size_t pouchIndex = 0; pouchIndex < trainer.items.size(); ++pouchIndex)
        {
            if (!trainer.items[pouchIndex].empty())
            {
                return static_cast<int>(pouchIndex);
            }
        }
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
    void checkSCBlock(const char *gameLabel, const std::string &savePath)
    {
        std::vector<uint8_t> rawSaveBytes = readFile(savePath);
        if (rawSaveBytes.empty())
        {
            std::printf("  SKIP  %-18s (%s)\n", gameLabel, SaveTree::missing(g_saveRoot, gameLabel).c_str());
            ++g_skipCount;
            return;
        }

        std::vector<Save::Block> blocks = Encryption::decrypt(rawSaveBytes.data(), rawSaveBytes.size());
        TrainerT trainer(std::move(blocks));

        ::Pokemon::Pokemon *pokemon = firstPartyPokemon(trainer);
        const int pouchIndex = firstStockedPouch(trainer);
        if (!pokemon || pouchIndex < 0)
        {
            std::printf("  SKIP  %-18s (needs a party Pokemon and a stocked pouch)\n", gameLabel);
            ++g_skipCount;
            return;
        }

        const uint8_t oldMetLevel = pokemon->metLevel();
        const uint8_t newMetLevel = static_cast<uint8_t>(oldMetLevel == 3 ? 4 : 3);
        const uint16_t itemId = trainer.items[pouchIndex][0].itemId;
        const uint16_t oldItemCount = trainer.items[pouchIndex][0].count;
        const uint16_t newItemCount = static_cast<uint16_t>(oldItemCount == 7 ? 8 : 7);
        const std::string speciesName = pokemon->species();

        pokemon->setMetLevel(newMetLevel);
        pokemon->refreshChecksum();
        trainer.items[pouchIndex][0].count = newItemCount;

        trainer.updatePartyBlock();
        trainer.updateItemBlock();

        const std::vector<uint8_t> writtenSaveBytes = Encryption::encrypt(trainer.getBlocks());
        std::vector<uint8_t> rereadSaveBytes = writtenSaveBytes;
        std::vector<Save::Block> freshBlocks = Encryption::decrypt(rereadSaveBytes.data(), rereadSaveBytes.size());
        TrainerT freshTrainer(std::move(freshBlocks));

        ::Pokemon::Pokemon *freshPokemon = firstPartyPokemon(freshTrainer);
        if (!freshPokemon)
        {
            report(gameLabel, "met level", false, "party Pokemon vanished after the save/reload cycle");
            return;
        }

        {
            const uint8_t readBackMetLevel = freshPokemon->metLevel();
            char detailText[192];
            std::snprintf(detailText, sizeof(detailText), "%s: %u -> %u, read back %u", speciesName.c_str(), oldMetLevel, newMetLevel, readBackMetLevel);
            report(gameLabel, "met level", readBackMetLevel == newMetLevel, detailText);
        }

        {
            uint16_t readBackCount = 0;
            bool itemFound = false;
            for (const auto &pouchItem : freshTrainer.items[pouchIndex])
            {
                if (pouchItem.itemId == itemId)
                {
                    readBackCount = pouchItem.count;
                    itemFound = true;
                    break;
                }
            }
            char detailText[192];
            std::snprintf(detailText, sizeof(detailText), "item id %u: %u -> %u, read back %u%s", itemId, oldItemCount, newItemCount, readBackCount, itemFound ? "" : " (ITEM MISSING)");
            report(gameLabel, "item count", itemFound && readBackCount == newItemCount, detailText);
        }

        // The checksum/hash must still validate, or the console would reject the file outright.
        {
            const size_t payloadLength = writtenSaveBytes.size() - SIZE_HASH_IN_BYTES;
            uint8_t expectedHash[SIZE_HASH_IN_BYTES];
            Encryption::computeHash(writtenSaveBytes.data(), payloadLength, expectedHash);
            const bool hashMatches = std::memcmp(expectedHash, writtenSaveBytes.data() + payloadLength, SIZE_HASH_IN_BYTES) == 0;
            report(gameLabel, "save hash", hashMatches, hashMatches ? "valid after the edit" : "MISMATCH - the game would reject this");
        }
    }

}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::printf("usage: editpersist <save-root>\n  Expects, relative to <save-root>:\n  One folder per TITLE, any filename inside:\n    Shield/  Violet/  Legends Z-A/  Legends Arceus/\n");
        return 2;
    }
    const std::string root = argv[1];
    g_saveRoot = root;

    std::printf("PKSE edit-persistence harness\nedit -> serialize -> RE-PARSE -> assert the new value survived. roundtrip.cpp\nproves a no-op reproduces the file; this proves an edit is not silently dropped.\n\n");

    // Found by TITLE FOLDER; see SaveTree.h.
    checkSCBlock<Trainer::Trainer9SV>("Violet", SaveTree::violet(root));
    checkSCBlock<Trainer::Trainer8SWSH>("Shield", SaveTree::shield(root));
    checkSCBlock<Trainer::Trainer9LZA>("Legends Z-A", SaveTree::legendsZA(root));
    checkSCBlock<Trainer::Trainer8LA>("Legends Arceus", SaveTree::legendsArceus(root));

    std::printf("\n%d passed, %d FAILED, %d skipped\n", g_passCount, g_failCount, g_skipCount);
    return g_failCount == 0 ? 0 : 1;
}
