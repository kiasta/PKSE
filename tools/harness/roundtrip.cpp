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
#include "Trainer/Trainer8BDSP.h"
#include "Pokemon/DexTable8SWSH.h"
#include "Pokemon/SpeciesConverter9.h"
#include "Trainer/Inventory9LZA.h"
#include "Trainer/Inventory8BDSP.h"
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

    int g_passCount = 0;
    int g_failCount = 0;

    /**
     * Compare and report. Prints the first few differing offsets rather than just a count: on a save
     * file "1834 bytes differ" tells you nothing, whereas the offsets say which block is at fault.
     */
    int g_knownCount = 0;

    /**
     * `knownDivergence` explains a difference PKSE produces ON PURPOSE. Such a case reports KNOWN and does not
     * fail the run, but still prints its byte count, because the count is the regression signal: if
     * "empty party slots" stops being 4 bytes on this save, something else moved.
     *
     * Categorising these matters. A harness that reports failures it will never fix trains everyone to
     * ignore it, and then it catches nothing.
     */
    bool compareBytes(const std::string &label, const std::vector<uint8_t> &beforeBytes, const std::vector<uint8_t> &afterBytes, const char *knownDivergence = nullptr)
    {
        if (beforeBytes.size() != afterBytes.size())
        {
            std::printf("  FAIL  %-22s size changed: %zu -> %zu\n", label.c_str(), beforeBytes.size(), afterBytes.size());
            // Still compare the common prefix. Whether the part we DID write matches decides what kind
            // of bug this is: a byte-perfect prefix means parsing simply stopped early and the tail is
            // unmodelled, while a mismatch inside the prefix means the parse went off the rails.
            const size_t commonLength = std::min(beforeBytes.size(), afterBytes.size());
            size_t firstDifferenceOffset = commonLength;
            for (size_t byteOffset = 0; byteOffset < commonLength; ++byteOffset)
            {
                if (beforeBytes[byteOffset] != afterBytes[byteOffset])
                {
                    firstDifferenceOffset = byteOffset;
                    break;
                }
            }
            if (firstDifferenceOffset == commonLength)
            {
                std::printf("        common prefix of %zu bytes is IDENTICAL - the parse stopped early,\n        it did not corrupt what it wrote\n", commonLength);
            }
            else
            {
                std::printf("        prefix diverges at 0x%zX - the parse itself is wrong\n", firstDifferenceOffset);
            }
            ++g_failCount;
            return false;
        }
        size_t differenceCount = 0;
        size_t shownOffsetCount = 0;
        for (size_t byteOffset = 0; byteOffset < beforeBytes.size(); ++byteOffset)
        {
            if (beforeBytes[byteOffset] == afterBytes[byteOffset])
            {
                continue;
            }
            ++differenceCount;
            // Offsets only for genuine failures; a known divergence just needs its count.
            if (!knownDivergence && shownOffsetCount < 8)
            {
                std::printf("        offset 0x%06zX: %02X -> %02X\n", byteOffset, beforeBytes[byteOffset], afterBytes[byteOffset]);
                ++shownOffsetCount;
            }
        }
        if (differenceCount == 0)
        {
            std::printf("  PASS  %-22s %zu bytes identical\n", label.c_str(), beforeBytes.size());
            ++g_passCount;
            return true;
        }
        if (knownDivergence)
        {
            std::printf("  KNOWN %-22s %zu byte(s) - %s\n", label.c_str(), differenceCount, knownDivergence);
            ++g_knownCount;
            return true;
        }
        std::printf("  FAIL  %-22s %zu byte(s) differ\n", label.c_str(), differenceCount);
        ++g_failCount;
        return false;
    }

    // Which writers to run. Isolating them matters: "the write path changed 13136 bytes" is not
    // actionable, whereas "updateItemBlock changed 13136 bytes" names the function to open.
    enum Writer
    {
        WriteNone = 0,
        WriteParty = 1,
        WriteItems = 2,
        WriteBoxes = 4,
        WriteBoxNames = 8,
        WriteAll = 15
    };

    template <typename TrainerT>
    void runUpdates(TrainerT &trainer, int writers)
    {
        if (writers & WriteParty)
        {
            trainer.updatePartyBlock();
        }
        if (writers & WriteItems)
        {
            trainer.updateItemBlock();
        }
        if (writers & WriteBoxes)
        {
            trainer.updateBoxBlock();
        }
        if (writers & WriteBoxNames)
        {
            trainer.updateBoxNameBlock();
        }
    }

    /// Gen 3: the container IS the byte vector, so this is the most direct round trip of the set --
    /// and the most intricate container (rotated sectors, per-section checksums, security key).
    void roundTripFRLG(const std::string &savePath)
    {
        const std::vector<uint8_t> originalSaveBytes = readFile(savePath);
        if (originalSaveBytes.empty())
        {
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
        const struct
        {
            const char *label;
            int writers;
            const char *knownDivergence;
        } steps[] = {
            {"party", WriteParty, nullptr},
            {"items", WriteItems, nullptr},
            {"boxes", WriteBoxes, nullptr},
            {"box names", WriteBoxNames, nullptr},
            {"all", WriteAll, nullptr},
        };
        for (const auto &step : steps)
        {
            Trainer::Trainer3FRLG trainer(originalSaveBytes, "FireRed_e.sav");
            runUpdates(trainer, step.writers);
            trainer.finalizeChecksums();
            compareBytes(std::string("FireRed/LeafGreen [") + step.label + "]", originalSaveBytes, trainer.getSaveData(), step.knownDivergence);
        }
        // serialize() is what the app's write path calls: every writer above PLUS the Pokedex, the
        // current box and the trainer block. The per-writer steps cannot see those three.
        {
            Trainer::Trainer3FRLG trainer(originalSaveBytes, "FireRed_e.sav");
            compareBytes("FireRed/LeafGreen [serialize]", originalSaveBytes, trainer.serialize(), nullptr);
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
    void roundTripRSE(const char *title, const std::string &savePath, const std::string &root)
    {
        std::vector<uint8_t> originalSaveBytes = readFile(savePath);
        if (originalSaveBytes.empty())
        {
            std::printf("  SKIP  %-22s (%s)\n", title, SaveTree::missing(root, title).c_str());
            return;
        }

        const size_t rawFileSize = originalSaveBytes.size();
        if (Save::stripRtcFooter(originalSaveBytes))
        {
            std::printf("        %s: trimmed a %zu-byte RTC footer (%zu -> %zu)\n", title, rawFileSize - originalSaveBytes.size(), rawFileSize, originalSaveBytes.size());
        }

        {
            Trainer::Trainer3RSE probeTrainer(originalSaveBytes, "probe.sav");
            if (!probeTrainer.isValid())
            {
                std::printf("  FAIL  %-22s did not parse as Ruby/Sapphire/Emerald\n", title);
                ++g_failCount;
                return;
            }
            std::printf("        %s: layout %s, OT \"%s\", %zu party, key 0x%08X\n", title, probeTrainer.layout() == Trainer::Gen3HoennLayout::Emerald ? "Emerald" : "Ruby/Sapphire", probeTrainer.trainerName.c_str(), probeTrainer.party.size(), probeTrainer.securityKey());
            // The layout is what decides the bag offsets and whether money is keyed, so a save that
            // resolved to the wrong one would still round-trip (nothing was edited) while showing the
            // user a bag full of nonsense. Check it against the folder, which is the one thing that knows.
            const bool folderSaysEmerald = std::string(title) == "Emerald";
            const bool layoutIsEmerald = probeTrainer.layout() == Trainer::Gen3HoennLayout::Emerald;
            if (folderSaysEmerald != layoutIsEmerald)
            {
                std::printf("  FAIL  %-22s resolved to the %s layout\n", title, layoutIsEmerald ? "Emerald" : "Ruby/Sapphire");
                ++g_failCount;
            }
        }

        // Per-writer isolation, re-reading the save each time, exactly as FireRed does above --
        // and with no known-divergence allowance, for the same reason.
        const struct
        {
            const char *label;
            int writers;
        } steps[] = {
            {"party", WriteParty},
            {"items", WriteItems},
            {"boxes", WriteBoxes},
            {"box names", WriteBoxNames},
            {"all", WriteAll},
        };
        for (const auto &step : steps)
        {
            Trainer::Trainer3RSE trainer(originalSaveBytes, "probe.sav");
            runUpdates(trainer, step.writers);
            trainer.finalizeChecksums();
            compareBytes(std::string(title) + " [" + step.label + "]", originalSaveBytes, trainer.getSaveData(), nullptr);
        }
        // serialize() is the app's write path; see roundTripFRLG.
        {
            Trainer::Trainer3RSE trainer(originalSaveBytes, "probe.sav");
            compareBytes(std::string(title) + " [serialize]", originalSaveBytes, trainer.serialize(), nullptr);
        }
    }

    /// Gen 3: a save interrupted mid-write leaves its NEWER slot missing a sector, and the game loads the
    /// older, complete one. PKSE has to as well (PKHeX SAV3.GetActiveSlot): when detect() accepts the
    /// file but the parser takes the broken slot, the file browser lets the save through and it then
    /// fails to open. Built from a real save by erasing one sector of the newer slot, and compared with
    /// the same save with that whole slot erased, which every slot rule reads the same way.
    template <typename TrainerT>
    void interruptedSaveUsesOlderSlot(const char *title, const std::string &savePath)
    {
        const std::string label = std::string(title) + " [interrupted]";
        std::vector<uint8_t> saveBytes = readFile(savePath);
        if (saveBytes.empty())
        {
            std::printf("  SKIP  %-22s (%s)\n", label.c_str(), SaveTree::missing(g_saveRoot, title).c_str());
            return;
        }
        Save::stripRtcFooter(saveBytes);

        constexpr size_t slotAOffset = 0x0000;
        constexpr size_t slotBOffset = 0xE000;
        constexpr size_t sectorSize = 0x1000;
        constexpr size_t sectorCount = 14;
        constexpr size_t sectorIdOffset = 0xFF4;
        constexpr size_t sectorCounterOffset = 0xFFC;
        auto sectorId = [&](size_t slotBase, size_t sectorIndex) { return Utils::readUInt16LittleEndian(&saveBytes[slotBase + sectorIndex * sectorSize + sectorIdOffset]); };
        auto slotCounter = [&](size_t slotBase) -> uint32_t
        {
            for (size_t sectorIndex = 0; sectorIndex < sectorCount; ++sectorIndex)
            {
                if (sectorId(slotBase, sectorIndex) == 0)
                {
                    return Utils::readUInt32LittleEndian(&saveBytes[slotBase + sectorIndex * sectorSize + sectorCounterOffset]);
                }
            }
            return 0;
        };
        const size_t newerSlotOffset = slotCounter(slotBOffset) > slotCounter(slotAOffset) ? slotBOffset : slotAOffset;
        size_t erasedSector = 0;
        while (erasedSector < sectorCount && sectorId(newerSlotOffset, erasedSector) == 0)
        {
            ++erasedSector;
        }

        std::vector<uint8_t> interruptedSave = saveBytes;
        const auto erasedStart = interruptedSave.begin() + static_cast<std::ptrdiff_t>(newerSlotOffset + erasedSector * sectorSize);
        std::fill(erasedStart, erasedStart + sectorSize, 0xFF);
        std::vector<uint8_t> olderSlotOnly = saveBytes;
        const auto newerStart = olderSlotOnly.begin() + static_cast<std::ptrdiff_t>(newerSlotOffset);
        std::fill(newerStart, newerStart + sectorCount * sectorSize, 0xFF);

        const TrainerT expectedTrainer(olderSlotOnly, "probe.sav");
        const TrainerT trainer(interruptedSave, "probe.sav");
        const bool detected = TrainerT::detect(interruptedSave);
        const bool sameSave = trainer.isValid() && expectedTrainer.isValid() && trainer.trainerName == expectedTrainer.trainerName && trainer.money == expectedTrainer.money && trainer.party.size() == expectedTrainer.party.size();
        if (detected && sameSave)
        {
            std::printf("  PASS  %-22s opens from the older slot (OT \"%s\")\n", label.c_str(), trainer.trainerName.c_str());
            ++g_passCount;
            return;
        }
        std::printf("  FAIL  %-22s detect=%d valid=%d, OT \"%s\" where the older slot holds \"%s\"\n", label.c_str(), detected, trainer.isValid(), trainer.trainerName.c_str(), expectedTrainer.trainerName.c_str());
        ++g_failCount;
    }

    /// SCBlock games: decrypt -> parse -> re-serialize -> encrypt. Exercises the block layer as well
    /// as each game's own writers.
    template <typename TrainerT>
    void roundTripSCBlock(const char *label, const std::string &savePath)
    {
        std::vector<uint8_t> originalSaveBytes = readFile(savePath);
        if (originalSaveBytes.empty())
        {
            std::printf("  SKIP  %-22s (%s)\n", label, SaveTree::missing(g_saveRoot, label).c_str());
            return;
        }

        // Does the FILE validate? PKHeX assumes the hash is good before it parses, and treats the whole
        // payload as blocks. So if the hash checks out, any early stop is our reader's fault, not a
        // damaged or unusual save -- which is the difference between "fix PKSE" and "this file is odd".
        {
            const size_t payloadLength = originalSaveBytes.size() - SIZE_HASH_IN_BYTES;
            uint8_t expectedHash[SIZE_HASH_IN_BYTES];
            Encryption::computeHash(originalSaveBytes.data(), payloadLength, expectedHash);
            const bool hashMatches = std::memcmp(expectedHash, originalSaveBytes.data() + payloadLength, SIZE_HASH_IN_BYTES) == 0;
            std::printf("        file hash: %s\n", hashMatches ? "VALID" : "MISMATCH (file damaged or not this format)");
        }

        // Report parse COVERAGE, not just success. serializeAllBlocks re-emits only what parsed, and
        // the save path writes that as the whole file -- so a parse that stops short means the write
        // truncates the save. Without this the symptom is just "the file got smaller", with no clue why.
        std::vector<uint8_t> payload(originalSaveBytes.begin(), originalSaveBytes.end() - SIZE_HASH_IN_BYTES);
        Encryption::cryptStaticXorpadBytes(payload, payload.size());
        size_t consumedByteCount = 0;
        const std::vector<Save::Block> parsedBlocks = Save::parseAllBlocks(payload.data(), payload.size(), &consumedByteCount);
        std::printf("        %zu blocks, parsed %zu / %zu payload bytes (%.2f%%)\n", parsedBlocks.size(), consumedByteCount, payload.size(), payload.empty() ? 0.0 : 100.0 * static_cast<double>(consumedByteCount) / static_cast<double>(payload.size()));

        // If it stopped short, name the block that stopped it. "Parsing failed" is not actionable;
        // "type code 0x0B on key 0x________" points straight at getTypeSize().
        if (consumedByteCount + 5 <= payload.size())
        {
            const uint32_t stoppedBlockKey = Utils::readUInt32LittleEndian(payload.data() + consumedByteCount);
            Utils::SCXorShift32 keyStream(stoppedBlockKey);
            const uint8_t typeCode = payload[consumedByteCount + 4] ^ keyStream.Next();
            std::printf("        STOPPED at 0x%zX on key 0x%08X, type code 0x%02X (%u) -- %zu bytes dropped\n", consumedByteCount, stoppedBlockKey, typeCode, typeCode, payload.size() - consumedByteCount);
        }

        std::vector<Save::Block> blocks = Encryption::decrypt(originalSaveBytes.data(), originalSaveBytes.size());
        if (blocks.empty())
        {
            std::printf("  FAIL  %-22s decrypt produced no blocks\n", label);
            ++g_failCount;
            return;
        }

        // Two separate round trips, because they blame completely different code.
        //   BLOCK LAYER: decrypt -> encrypt, touching nothing. Any difference here is parse/serialize.
        //   WRITE PATH : the same, but with every update*Block() in between. A difference that appears
        //                only here belongs to one of those writers re-emitting a block differently.
        // Running only the second (as this harness first did) can't tell the two apart.
        compareBytes(std::string(label) + " [blocks]", originalSaveBytes, Encryption::encrypt(blocks));

        // Then each writer alone, re-parsing from scratch each time so they can't contaminate each
        // other. A writer that changes bytes here is rewriting a block it was given nothing to change.
        // updateBoxBlock writes only the slots that changed: an unchanged record and an empty slot
        // that already holds a game blank are left alone, so no step here may diverge.
        const struct
        {
            const char *label;
            int writers;
            const char *knownDivergence;
        } steps[] = {
            {"party", WriteParty, nullptr},
            {"items", WriteItems, nullptr},
            {"boxes", WriteBoxes, nullptr},
            {"box names", WriteBoxNames, nullptr},
            {"all", WriteAll, nullptr},
        };
        for (const auto &step : steps)
        {
            std::vector<Save::Block> freshBlocks = Encryption::decrypt(originalSaveBytes.data(), originalSaveBytes.size());
            TrainerT trainer(std::move(freshBlocks));
            runUpdates(trainer, step.writers);
            compareBytes(std::string(label) + " [" + step.label + "]", originalSaveBytes, Encryption::encrypt(trainer.getBlocks()), step.knownDivergence);
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
    void itemEditsStayInPlaceSWSH(const char *label, const std::string &savePath)
    {
        std::vector<uint8_t> originalSaveBytes = readFile(savePath);
        if (originalSaveBytes.empty())
        {
            std::printf("  SKIP  %-22s (%s)\n", label, SaveTree::missing(g_saveRoot, label).c_str());
            return;
        }

        Trainer::Trainer8SWSH trainer(Encryption::decrypt(originalSaveBytes.data(), originalSaveBytes.size()));
        std::vector<uint8_t> expectedItemBlock;
        for (const auto &block : trainer.getBlocks())
        {
            if (block.key == Trainer::ITEM8_SWSH)
            {
                expectedItemBlock = block.data;
            }
        }
        size_t pouchLayoutSize = 0;
        for (size_t pouchIndex = 0; pouchIndex < trainer.items.size(); ++pouchIndex)
        {
            const Trainer::PouchInfo8SWSH &pouchInfo = Trainer::getPouchInfo8SWSH(static_cast<Trainer::PouchType8SWSH>(pouchIndex));
            pouchLayoutSize = std::max(pouchLayoutSize, static_cast<size_t>(pouchInfo.offset + pouchInfo.maxCount * 4));
        }
        if (trainer.items.empty() || expectedItemBlock.size() < pouchLayoutSize)
        {
            std::printf("  FAIL  %-22s no complete item block to edit\n", (std::string(label) + " [item edits]").c_str());
            ++g_failCount;
            return;
        }

        size_t editedEntryCount = 0;
        size_t parkedEntryCount = 0;
        size_t appendedItemCount = 0;
        for (size_t pouchIndex = 0; pouchIndex < trainer.items.size(); ++pouchIndex)
        {
            const Trainer::PouchInfo8SWSH &pouchInfo = Trainer::getPouchInfo8SWSH(static_cast<Trainer::PouchType8SWSH>(pouchIndex));
            std::vector<Trainer::InventoryItem> &pouch = trainer.items[pouchIndex];
            uint8_t *pouchBytes = expectedItemBlock.data() + pouchInfo.offset;
            const size_t slotCount = static_cast<size_t>(pouchInfo.maxCount);

            size_t firstEmptySlot = slotCount;
            size_t lastOccupiedSlot = slotCount;
            for (size_t slotIndex = 0; slotIndex < slotCount; ++slotIndex)
            {
                const uint32_t itemValue = Utils::readUInt32LittleEndian(pouchBytes + slotIndex * 4);
                if (Trainer::InventoryItem8SWSH::fromValue(itemValue).itemId != 0)
                {
                    lastOccupiedSlot = slotIndex;
                }
                else if (firstEmptySlot == slotCount)
                {
                    firstEmptySlot = slotIndex;
                }
            }

            // The entries are read in slot order, so the pouch's last entry is its last occupied slot.
            if (!pouch.empty() && lastOccupiedSlot < slotCount)
            {
                Trainer::InventoryItem &lastItem = pouch.back();
                lastItem.count = static_cast<uint16_t>(lastItem.count == 5 ? 6 : 5);
                uint8_t *lastItemBytes = pouchBytes + lastOccupiedSlot * 4;
                Trainer::InventoryItem8SWSH editedEntry = Trainer::InventoryItem8SWSH::fromValue(Utils::readUInt32LittleEndian(lastItemBytes));
                editedEntry.count = lastItem.count;
                Utils::writeUInt32LittleEndian(lastItemBytes, editedEntry.toValue());
                ++editedEntryCount;
                if (firstEmptySlot < lastOccupiedSlot)
                {
                    ++parkedEntryCount;
                }
            }

            if (firstEmptySlot < slotCount)
            {
                uint16_t newItemId = 1;
                while (std::any_of(pouch.begin(), pouch.end(), [newItemId](const Trainer::InventoryItem &item) { return item.itemId == newItemId; }))
                {
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
        if (parkedEntryCount == 0)
        {
            std::printf("        no pouch on this save has a gap, so packing and positional writing agree here\n");
        }

        trainer.updateItemBlock();
        for (const auto &block : trainer.getBlocks())
        {
            if (block.key == Trainer::ITEM8_SWSH)
            {
                compareBytes(std::string(label) + " [item edits]", expectedItemBlock, block.data);
            }
        }
    }

    /// Legends: Arceus: the Pokedex writer must count a Pokemon toward its species' research once, when it
    /// arrives, exactly as PKHeX's OnPokeGet does -- one catch, one alpha for an alpha, one for each size
    /// task the species has and the Pokemon's size meets -- and never again. Re-registering storage on every
    /// save once added one per stored Pokemon per save, and an unedited round trip cannot see it because
    /// every other byte that writer touches is a flag it may legitimately set.
    void catchResearchCountsOnceLA(const char *label, const std::string &savePath)
    {
        std::vector<uint8_t> originalSaveBytes = readFile(savePath);
        if (originalSaveBytes.empty())
        {
            std::printf("  SKIP  %-22s (%s)\n", label, SaveTree::missing(g_saveRoot, label).c_str());
            return;
        }
        using Field = Trainer::ResearchField8LA;
        using SizeTask = Pokemon::SizeResearchTask8LA;
        const Field countFields[] = {Field::CatchCount, Field::AlphaCount, Field::LargeCount, Field::SmallCount, Field::HeavyCount, Field::LightCount};
        auto pokedexBytes = [](const Trainer::Trainer8LA &trainer)
        {
            for (const auto &block : trainer.getBlocks())
            {
                if (block.key == Trainer::POKEDEX8_LA)
                {
                    return block.data;
                }
            }
            return std::vector<uint8_t>();
        };
        // Every research field of every species, plus the next update counter: what a write must not move.
        auto changedFieldCount = [&](const std::vector<uint8_t> &beforeBytes, const std::vector<uint8_t> &afterBytes)
        {
            size_t fieldCount = Trainer::Trainer8LA::nextResearchUpdateCounter(beforeBytes) != Trainer::Trainer8LA::nextResearchUpdateCounter(afterBytes);
            for (uint16_t speciesId = 1; speciesId < Pokemon::DEX8LA_MAX_SPECIES; ++speciesId)
            {
                for (Field field : countFields)
                {
                    fieldCount += Trainer::Trainer8LA::researchField(beforeBytes, speciesId, field) != Trainer::Trainer8LA::researchField(afterBytes, speciesId, field);
                }
                fieldCount += Trainer::Trainer8LA::researchField(beforeBytes, speciesId, Field::UpdateCounter) != Trainer::Trainer8LA::researchField(afterBytes, speciesId, Field::UpdateCounter);
            }
            return fieldCount;
        };
        auto writeFloat = [](Pokemon::Pokemon &pokemon, size_t offset, float floatValue) { std::memcpy(pokemon.getData().data() + offset, &floatValue, sizeof floatValue); };
        int failureCount = 0;
        auto report = [&](bool passed, const std::string &checkDescription)
        {
            std::printf("        %s %s\n", passed ? "ok  " : "FAIL", checkDescription.c_str());
            failureCount += passed ? 0 : 1;
        };
        const std::string testLabel = std::string(label) + " [catch research]";

        Trainer::Trainer8LA trainer(Encryption::decrypt(originalSaveBytes.data(), originalSaveBytes.size()));
        const std::vector<uint8_t> loadedPokedex = pokedexBytes(trainer);
        trainer.updatePokedexBlock();
        trainer.updatePokedexBlock();
        report(changedFieldCount(loadedPokedex, pokedexBytes(trainer)) == 0, "two unedited writes change no research field");

        // The source: a non-alpha whose species has a size task, so the arrivals below can exercise one.
        const Pokemon::Pokemon8LA *sourcePokemon = nullptr;
        SizeTask sourceTask = SizeTask::CatchLarge;
        uint8_t sourceThreshold = 0;
        std::vector<std::unique_ptr<::Pokemon::Pokemon> *> emptySlots;
        for (auto &box : trainer.boxes)
        {
            for (auto &slotPokemon : box)
            {
                if (!slotPokemon || slotPokemon->speciesID() == 0)
                {
                    emptySlots.push_back(&slotPokemon);
                    continue;
                }
                const auto *candidate = static_cast<const Pokemon::Pokemon8LA *>(slotPokemon.get());
                for (SizeTask task : {SizeTask::CatchLarge, SizeTask::CatchSmall, SizeTask::CatchHeavy, SizeTask::CatchLight})
                {
                    if (!sourcePokemon && !candidate->isAlpha() && Pokemon::getSizeResearchThreshold8LA(candidate->speciesID(), task, sourceThreshold))
                    {
                        sourcePokemon = candidate;
                        sourceTask = task;
                    }
                }
            }
        }
        if (!sourcePokemon || emptySlots.size() < 4)
        {
            std::printf("  FAIL  %-22s no non-alpha Pokemon with a size task, or fewer than four empty box slots\n", testLabel.c_str());
            ++g_failCount;
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
        struct Arrival
        {
            const char *description;
            float sizeValue;
            bool isAlpha;
            std::vector<int> expectedIncrements;
        };
        const float meetingSize = taskIsUpperBound ? thresholdValue * 0.5f : thresholdValue * 2.0f;
        const float missingSize = taskIsUpperBound ? thresholdValue * 2.0f : thresholdValue * 0.5f;
        std::vector<Arrival> arrivals = {
            {"meets the size task", meetingSize, false, {1, 0, 0, 0, 0, 0}},
            {"misses the size task", missingSize, false, {1, 0, 0, 0, 0, 0}},
            {"an alpha that meets it", meetingSize, true, {1, 1, 0, 0, 0, 0}},
            {"no recorded size", 0.0f, false, {1, 0, 0, 0, 0, 0}},
        };
        arrivals[0].expectedIncrements[taskFieldIndex] = 1;
        arrivals[2].expectedIncrements[taskFieldIndex] = 1;
        for (size_t arrivalIndex = 0; arrivalIndex < arrivals.size(); ++arrivalIndex)
        {
            const Arrival &arrival = arrivals[arrivalIndex];
            auto arrivingPokemon = sourcePokemon->clone();
            writeFloat(*arrivingPokemon, sizeOffset, arrival.sizeValue);
            if (arrival.isAlpha)
            {
                arrivingPokemon->getData()[0x16] |= std::byte{0x20};
            }
            arrivingPokemon->refreshChecksum();
            const std::vector<uint8_t> beforeArrival = pokedexBytes(trainer);
            *emptySlots[arrivalIndex] = std::move(arrivingPokemon);
            trainer.updatePokedexBlock();
            const std::vector<uint8_t> afterArrival = pokedexBytes(trainer);
            bool countsMatch = true;
            std::string countText;
            for (size_t fieldIndex = 0; fieldIndex < std::size(countFields); ++fieldIndex)
            {
                const int fieldIncrement = Trainer::Trainer8LA::researchField(afterArrival, speciesId, countFields[fieldIndex]) - Trainer::Trainer8LA::researchField(beforeArrival, speciesId, countFields[fieldIndex]);
                countsMatch = countsMatch && fieldIncrement == arrival.expectedIncrements[fieldIndex];
                countText += (fieldIndex ? "/" : "") + std::to_string(fieldIncrement);
            }
            size_t otherSpeciesChangeCount = 0;
            for (uint16_t otherSpeciesId = 1; otherSpeciesId < Pokemon::DEX8LA_MAX_SPECIES; ++otherSpeciesId)
            {
                if (otherSpeciesId == speciesId)
                {
                    continue;
                }
                for (Field field : countFields)
                {
                    otherSpeciesChangeCount += Trainer::Trainer8LA::researchField(beforeArrival, otherSpeciesId, field) != Trainer::Trainer8LA::researchField(afterArrival, otherSpeciesId, field);
                }
            }
            report(countsMatch && otherSpeciesChangeCount == 0, std::string("arrival that ") + arrival.description + ": catch/alpha/large/small/heavy/light +" + countText + (otherSpeciesChangeCount ? ", other species changed" : ""));
            trainer.updatePokedexBlock();
            report(changedFieldCount(afterArrival, pokedexBytes(trainer)) == 0, std::string("  ...and the next write counts it no further"));
        }

        // The absolutes PKSE gives a converted or created Pokemon must be the ones the game computes: every
        // Pokemon this save holds was sized by the game itself.
        size_t sizedPokemonCount = 0;
        size_t mismatchedPokemonCount = 0;
        Trainer::Trainer8LA freshTrainer(Encryption::decrypt(originalSaveBytes.data(), originalSaveBytes.size()));
        std::vector<const ::Pokemon::Pokemon *> storedPokemonList;
        for (const auto &partyPokemon : freshTrainer.party)
        {
            storedPokemonList.push_back(partyPokemon.get());
        }
        for (const auto &box : freshTrainer.boxes)
        {
            for (const auto &slotPokemon : box)
            {
                storedPokemonList.push_back(slotPokemon.get());
            }
        }
        {
            for (const ::Pokemon::Pokemon *slotPokemon : storedPokemonList)
            {
                if (!slotPokemon || slotPokemon->speciesID() == 0)
                {
                    continue;
                }
                const auto &storedPokemon = static_cast<const Pokemon::Pokemon8LA &>(*slotPokemon);
                auto resizedClone = storedPokemon.clone();
                auto &resizedPokemon = static_cast<Pokemon::Pokemon8LA &>(*resizedClone);
                resizedPokemon.resetSizeAbsolutes();
                const float storedHeight = storedPokemon.heightAbsolute();
                const float storedWeight = storedPokemon.weightAbsolute();
                const float resizedHeight = resizedPokemon.heightAbsolute();
                const float resizedWeight = resizedPokemon.weightAbsolute();
                ++sizedPokemonCount;
                if (std::memcmp(&storedHeight, &resizedHeight, sizeof(float)) != 0 || std::memcmp(&storedWeight, &resizedWeight, sizeof(float)) != 0)
                {
                    if (mismatchedPokemonCount == 0)
                    {
                        std::printf("        first size mismatch: species %u stored %.9g/%.9g computed %.9g/%.9g\n", storedPokemon.speciesID(), storedHeight, storedWeight, resizedHeight, resizedWeight);
                    }
                    ++mismatchedPokemonCount;
                }
            }
        }
        report(sizedPokemonCount != 0 && mismatchedPokemonCount == 0, "resetSizeAbsolutes reproduces the game's sizes bit for bit (" + std::to_string(sizedPokemonCount - mismatchedPokemonCount) + " of " + std::to_string(sizedPokemonCount) + ")");

        if (failureCount == 0)
        {
            std::printf("  PASS  %-22s each arrival counts once, as PKHeX OnPokeGet does; storage is never re-counted\n", testLabel.c_str());
            ++g_passCount;
        }
        else
        {
            std::printf("  FAIL  %-22s %d research check(s) failed\n", testLabel.c_str(), failureCount);
            ++g_failCount;
        }
    }

    /// Sword/Shield: REMOVING an item must empty its slot and move nothing else.
    ///
    /// Items are matched to the slot they were read from by item id rather than by list position,
    /// which is what this proves: an erase shifts every later entry down one place in the list, so a
    /// position-matched writer would send all of them to new slots -- on this save it moved a used-up
    /// Max Potion from slot 59 to slot 16 without being asked to.
    void itemRemovalEmptiesOneSlotSWSH(const char *label, const std::string &savePath)
    {
        std::vector<uint8_t> originalSaveBytes = readFile(savePath);
        if (originalSaveBytes.empty())
        {
            std::printf("  SKIP  %-22s (%s)\n", label, SaveTree::missing(g_saveRoot, label).c_str());
            return;
        }

        Trainer::Trainer8SWSH trainer(Encryption::decrypt(originalSaveBytes.data(), originalSaveBytes.size()));
        std::vector<uint8_t> expectedItemBlock;
        for (const auto &block : trainer.getBlocks())
        {
            if (block.key == Trainer::ITEM8_SWSH)
            {
                expectedItemBlock = block.data;
            }
        }
        if (trainer.items.empty() || expectedItemBlock.empty())
        {
            std::printf("  FAIL  %-22s no item block to edit\n", (std::string(label) + " [item removal]").c_str());
            ++g_failCount;
            return;
        }

        size_t removedItemCount = 0;
        for (size_t pouchIndex = 0; pouchIndex < trainer.items.size(); ++pouchIndex)
        {
            const Trainer::PouchInfo8SWSH &pouchInfo = Trainer::getPouchInfo8SWSH(static_cast<Trainer::PouchType8SWSH>(pouchIndex));
            std::vector<Trainer::InventoryItem> &pouch = trainer.items[pouchIndex];
            if (pouch.empty())
            {
                continue;
            }
            uint8_t *pouchBytes = expectedItemBlock.data() + pouchInfo.offset;
            for (size_t slotIndex = 0; slotIndex < static_cast<size_t>(pouchInfo.maxCount); ++slotIndex)
            {
                if (Trainer::InventoryItem8SWSH::fromValue(Utils::readUInt32LittleEndian(pouchBytes + slotIndex * 4)).itemId == 0)
                {
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
        for (const auto &block : trainer.getBlocks())
        {
            if (block.key == Trainer::ITEM8_SWSH)
            {
                compareBytes(std::string(label) + " [item removal]", expectedItemBlock, block.data);
            }
        }
    }

}

/// The Let's Go Pokedex's size records, as Trainer7LGPE.cpp lays them out: four groups (minimum and
/// maximum height, minimum and maximum weight) of 186 six-byte entries at 0xF78 in the Zukan block.
size_t sizeRecordOffsetLGPE(int sizeGroup, int entryIndex)
{
    return 0xF78 + 6 * static_cast<size_t>(entryIndex + sizeGroup * 186);
}

/// The partner Pikachu (form 8) and partner Eevee (form 1). PKHeX's Zukan7b.SetDex registers
/// neither, because the dex has no entry for either form.
bool isPartnerLGPE(const Pokemon::Pokemon &pokemon)
{
    return (pokemon.speciesID() == 25 && pokemon.form() == 8) || (pokemon.speciesID() == 133 && pokemon.form() == 1);
}

std::vector<uint8_t> zukanBlockLGPE(const Trainer::Trainer7LGPE &trainer)
{
    for (const auto &block : trainer.getBlocks())
    {
        if (block.key == Trainer::ZUKAN7_LGPE)
        {
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
void roundTripLGPE(const char *label, const std::string &savePath)
{
    const std::vector<uint8_t> originalSaveBytes = readFile(savePath);
    if (originalSaveBytes.size() < Trainer::SAVE_SIZE7_LGPE)
    {
        std::printf("  SKIP  %-22s (%s)\n", label, SaveTree::missing(g_saveRoot, label).c_str());
        return;
    }
    const std::vector<uint8_t> saveAreaBytes(originalSaveBytes.begin(), originalSaveBytes.begin() + Trainer::SAVE_SIZE7_LGPE);
    auto report = [](bool passed, const std::string &checkDescription)
    {
        std::printf("  %s  %s\n", passed ? "PASS" : "FAIL", checkDescription.c_str());
        ++(passed ? g_passCount : g_failCount);
    };
    static constexpr const char *pokedexNote = "the Pokedex registered stored Pokemon its flags did not yet show";
    auto writeAll = [](Trainer::Trainer7LGPE &trainer)
    {
        trainer.updateItemBlock();
        trainer.updateBoxBlock();
        trainer.updatePartyBlock();
        trainer.updateTrainerInfoBlock();
        trainer.updatePokedexBlock();
    };
    const struct
    {
        const char *label;
        void (*write)(Trainer::Trainer7LGPE &);
        const char *knownDivergence;
    } steps[] = {
        {"items", [](Trainer::Trainer7LGPE &trainer) { trainer.updateItemBlock(); }, nullptr},
        {"boxes", [](Trainer::Trainer7LGPE &trainer) { trainer.updateBoxBlock(); }, nullptr},
        {"party",
            [](Trainer::Trainer7LGPE &trainer)
            {
                trainer.updateBoxBlock();
                trainer.updatePartyBlock();
            },
            nullptr},
        {"trainer", [](Trainer::Trainer7LGPE &trainer) { trainer.updateTrainerInfoBlock(); }, nullptr},
        {"pokedex", [](Trainer::Trainer7LGPE &trainer) { trainer.updatePokedexBlock(); }, pokedexNote},
    };
    for (const auto &step : steps)
    {
        Trainer::Trainer7LGPE trainer(Trainer::createBlocksFromSaveData7LGPE(saveAreaBytes));
        step.write(trainer);
        std::vector<uint8_t> writtenSaveBytes = originalSaveBytes;
        Trainer::writeBlocksToSaveData7LGPE(writtenSaveBytes, trainer.getBlocks());
        compareBytes(std::string(label) + " [" + step.label + "]", originalSaveBytes, writtenSaveBytes, step.knownDivergence);
    }

    Trainer::Trainer7LGPE originalTrainer(Trainer::createBlocksFromSaveData7LGPE(saveAreaBytes));
    Trainer::Trainer7LGPE firstTrainer(Trainer::createBlocksFromSaveData7LGPE(saveAreaBytes));
    writeAll(firstTrainer);
    std::vector<uint8_t> firstWrite = originalSaveBytes;
    Trainer::writeBlocksToSaveData7LGPE(firstWrite, firstTrainer.getBlocks());
    compareBytes(std::string(label) + " [all]", originalSaveBytes, firstWrite, pokedexNote);

    // Only the Pokedex block may differ. Compared block by block after re-parsing what was written,
    // so a writer that strayed into another block is named rather than hidden in the byte count.
    const std::vector<uint8_t> firstAreaBytes(firstWrite.begin(), firstWrite.begin() + Trainer::SAVE_SIZE7_LGPE);
    Trainer::Trainer7LGPE reloadedTrainer(Trainer::createBlocksFromSaveData7LGPE(firstAreaBytes));
    std::string changedBlockKeys;
    for (const auto &originalBlock : originalTrainer.getBlocks())
    {
        if (originalBlock.key == Trainer::ZUKAN7_LGPE)
        {
            continue;
        }
        for (const auto &reloadedBlock : reloadedTrainer.getBlocks())
        {
            if (reloadedBlock.key == originalBlock.key && reloadedBlock.data != originalBlock.data)
            {
                changedBlockKeys += " " + std::to_string(originalBlock.key);
            }
        }
    }
    report(changedBlockKeys.empty(), std::string(label) + " [all] an unedited write changes no block but the Pokedex" + (changedBlockKeys.empty() ? "" : " (changed:" + changedBlockKeys + ")"));

    // Whatever the first write registered, the second must find nothing left to do.
    writeAll(reloadedTrainer);
    std::vector<uint8_t> secondWrite = firstWrite;
    Trainer::writeBlocksToSaveData7LGPE(secondWrite, reloadedTrainer.getBlocks());
    compareBytes(std::string(label) + " [second write]", firstWrite, secondWrite);

    // A partner Pikachu or Eevee must leave its species' size records alone. Checked only for a
    // species whose every stored Pokemon is the partner, so no other Pokemon could have moved them.
    for (const uint16_t partnerSpeciesId : {static_cast<uint16_t>(25), static_cast<uint16_t>(133)})
    {
        size_t partnerPokemonCount = 0;
        size_t otherPokemonCount = 0;
        auto countPokemon = [&](const Pokemon::Pokemon *pokemon)
        {
            if (!pokemon || pokemon->speciesID() != partnerSpeciesId || pokemon->isEgg())
            {
                return;
            }
            ++(isPartnerLGPE(*pokemon) ? partnerPokemonCount : otherPokemonCount);
        };
        for (const auto &pokemon : originalTrainer.party)
        {
            countPokemon(pokemon.get());
        }
        for (const auto &box : originalTrainer.boxes)
        {
            for (const auto &pokemon : box)
            {
                countPokemon(pokemon.get());
            }
        }
        if (partnerPokemonCount == 0 || otherPokemonCount != 0)
        {
            continue;
        }
        const std::vector<uint8_t> zukanBefore = zukanBlockLGPE(originalTrainer);
        const std::vector<uint8_t> zukanAfter = zukanBlockLGPE(firstTrainer);
        size_t changedRecordCount = 0;
        for (int sizeGroup = 0; sizeGroup < 4; ++sizeGroup)
        {
            const size_t recordOffset = sizeRecordOffsetLGPE(sizeGroup, partnerSpeciesId - 1);
            if (recordOffset + 6 <= zukanBefore.size() && recordOffset + 6 <= zukanAfter.size() && !std::equal(zukanBefore.begin() + recordOffset, zukanBefore.begin() + recordOffset + 6, zukanAfter.begin() + recordOffset))
            {
                ++changedRecordCount;
            }
        }
        report(changedRecordCount == 0, std::string(label) + " [pokedex] the partner " + (partnerSpeciesId == 25 ? "Pikachu" : "Eevee") + " writes no size record (" + std::to_string(changedRecordCount) + " changed)");
    }
}

/// A Boxes-view Move into an empty slot (swapBoxSlots, then a save) must change exactly the two
/// slots involved. The source is chosen to precede every empty slot, because that is the order in
/// which a writer that harvests "the first empty slot's bytes" as its blank picks up the moved
/// Pokemon's record -- and then stamps a clone of it into every empty slot of every box.
template <typename TrainerT>
void moveIntoEmptySlotTouchesTwoSlots(const char *label, const std::string &savePath, size_t boxBlockKey, size_t slotStride)
{
    const std::string checkLabel = std::string(label) + " [box move]";
    std::vector<uint8_t> originalSaveBytes = readFile(savePath);
    if (originalSaveBytes.empty())
    {
        std::printf("  SKIP  %-22s (%s)\n", checkLabel.c_str(), SaveTree::missing(g_saveRoot, label).c_str());
        return;
    }
    TrainerT trainer(Encryption::decrypt(originalSaveBytes.data(), originalSaveBytes.size()));
    const size_t slotCount = trainer.boxes.size() * Trainer::BOX_SLOTS;
    auto slotIsEmpty = [&](size_t flatIndex)
    {
        const auto &pokemon = trainer.boxes[flatIndex / Trainer::BOX_SLOTS][flatIndex % Trainer::BOX_SLOTS];
        return !pokemon || pokemon->speciesID() == 0;
    };
    size_t firstEmptyIndex = slotCount;
    size_t lastEmptyIndex = slotCount;
    for (size_t flatIndex = 0; flatIndex < slotCount; ++flatIndex)
    {
        if (!slotIsEmpty(flatIndex))
        {
            continue;
        }
        if (firstEmptyIndex == slotCount)
        {
            firstEmptyIndex = flatIndex;
        }
        lastEmptyIndex = flatIndex;
    }
    // Ahead of every empty slot when the save has such a slot; otherwise the first occupied one.
    size_t sourceIndex = slotCount;
    for (size_t flatIndex = 0; flatIndex < slotCount; ++flatIndex)
    {
        if (!slotIsEmpty(flatIndex))
        {
            sourceIndex = flatIndex;
            break;
        }
    }
    if (sourceIndex > firstEmptyIndex)
    {
        std::printf("        %s: no occupied slot precedes the first empty one; moving slot %zu\n", label, sourceIndex);
    }
    if (sourceIndex == slotCount || lastEmptyIndex == slotCount || lastEmptyIndex <= sourceIndex)
    {
        std::printf("  FAIL  %-22s the save has no occupied slot ahead of an empty one to move\n", checkLabel.c_str());
        ++g_failCount;
        return;
    }
    auto boxBlockBytes = [&](const TrainerT &owner)
    {
        for (const auto &block : owner.getBlocks())
        {
            if (block.key == boxBlockKey)
            {
                return block.data;
            }
        }
        return std::vector<uint8_t>();
    };
    const std::vector<uint8_t> blockBefore = boxBlockBytes(trainer);
    const auto &moved = trainer.boxes[sourceIndex / Trainer::BOX_SLOTS][sourceIndex % Trainer::BOX_SLOTS];
    const std::vector<std::byte> movedBytes(moved->getData().begin(), moved->getData().end());

    trainer.swapBoxSlots(sourceIndex / Trainer::BOX_SLOTS, sourceIndex % Trainer::BOX_SLOTS, lastEmptyIndex / Trainer::BOX_SLOTS, lastEmptyIndex % Trainer::BOX_SLOTS);
    trainer.updateBoxBlock();
    const std::vector<uint8_t> blockAfter = boxBlockBytes(trainer);

    size_t strayByteCount = 0;
    size_t straySlotCount = 0;
    for (size_t flatIndex = 0; flatIndex < slotCount; ++flatIndex)
    {
        if (flatIndex == sourceIndex || flatIndex == lastEmptyIndex)
        {
            continue;
        }
        const size_t slotOffset = flatIndex * slotStride;
        size_t slotDiffCount = 0;
        for (size_t byteIndex = 0; byteIndex < slotStride && slotOffset + byteIndex < blockBefore.size(); ++byteIndex)
        {
            slotDiffCount += blockBefore[slotOffset + byteIndex] != blockAfter[slotOffset + byteIndex];
        }
        strayByteCount += slotDiffCount;
        straySlotCount += slotDiffCount != 0;
    }

    std::vector<uint8_t> written = Encryption::encrypt(trainer.getBlocks());
    TrainerT reloaded(Encryption::decrypt(written.data(), written.size()));
    const auto &sourceAfter = reloaded.boxes[sourceIndex / Trainer::BOX_SLOTS][sourceIndex % Trainer::BOX_SLOTS];
    const auto &destinationAfter = reloaded.boxes[lastEmptyIndex / Trainer::BOX_SLOTS][lastEmptyIndex % Trainer::BOX_SLOTS];
    const bool sourceNowEmpty = !sourceAfter || sourceAfter->speciesID() == 0;
    const bool destinationHoldsMoved = destinationAfter && destinationAfter->getDataSize() >= movedBytes.size() && std::memcmp(destinationAfter->getData().data(), movedBytes.data(), movedBytes.size()) == 0;

    if (strayByteCount == 0 && sourceNowEmpty && destinationHoldsMoved)
    {
        std::printf("  PASS  %-22s slot %zu -> %zu, no other slot changed\n", checkLabel.c_str(), sourceIndex, lastEmptyIndex);
        ++g_passCount;
        return;
    }
    std::printf("  FAIL  %-22s slot %zu -> %zu: %zu other slot(s) changed (%zu bytes), source empty=%d, destination holds it=%d\n", checkLabel.c_str(), sourceIndex, lastEmptyIndex, straySlotCount, strayByteCount, sourceNowEmpty, destinationHoldsMoved);
    ++g_failCount;
}

/// Legends: Z-A: (1) a held item never keeps IsNewNotify (flags bit 2, "never held": PKHeX
/// InventoryPouch9a.EnsureFlagsConsistent clears it whenever the count is non-zero), so a first
/// acquisition does not leave the game's first-pickup flyout armed; the bit is planted on the first
/// held item before parsing. (2) Latin American Spanish (language 11) is dex language bit 9 (PKHeX
/// PokeDexEntry9a.GetDexLangFlag); it used to be dropped. A clone of the first boxed Pokemon,
/// relabelled language 11, is dropped into an empty slot.
void itemAndDexLanguageFlagsLZA(const std::string &savePath)
{
    const char *checkLabel = "Legends Z-A [new flags]";
    std::vector<uint8_t> originalSaveBytes = readFile(savePath);
    if (originalSaveBytes.empty())
    {
        std::printf("  SKIP  %-22s (%s)\n", checkLabel, SaveTree::missing(g_saveRoot, "Legends Z-A").c_str());
        return;
    }
    std::vector<Save::Block> blocks = Encryption::decrypt(originalSaveBytes.data(), originalSaveBytes.size());
    size_t plantedItemOffset = 0;
    for (auto &block : blocks)
    {
        if (block.key != Trainer::ITEM9_LZA)
        {
            continue;
        }
        for (size_t itemOffset = Trainer::ITEM_SIZE9_LZA; itemOffset + Trainer::ITEM_SIZE9_LZA <= block.data.size() && plantedItemOffset == 0; itemOffset += Trainer::ITEM_SIZE9_LZA)
        {
            if (Utils::readUInt32LittleEndian(&block.data[itemOffset + 4]) == 0)
            {
                continue;
            }
            block.data[itemOffset + 8] |= 0x04;
            plantedItemOffset = itemOffset;
        }
    }
    Trainer::Trainer9LZA trainer(std::move(blocks));
    const Pokemon::Pokemon *sourcePokemon = nullptr;
    std::unique_ptr<Pokemon::Pokemon> *emptySlot = nullptr;
    for (auto &box : trainer.boxes)
    {
        for (auto &slotPokemon : box)
        {
            if (!slotPokemon || slotPokemon->speciesID() == 0)
            {
                if (!emptySlot)
                {
                    emptySlot = &slotPokemon;
                }
            }
            else if (!sourcePokemon)
            {
                sourcePokemon = slotPokemon.get();
            }
        }
    }
    if (plantedItemOffset == 0 || !sourcePokemon || !emptySlot)
    {
        std::printf("  SKIP  %-22s no held item, boxed Pokemon or empty slot\n", checkLabel);
        return;
    }
    const uint16_t species = sourcePokemon->speciesID();
    auto relabelled = sourcePokemon->clone();
    relabelled->setLanguage(11);
    *emptySlot = std::move(relabelled);
    trainer.updateItemBlock();
    trainer.updatePokedexBlock();
    bool notifyCleared = false;
    bool spanishLatinRecorded = false;
    const size_t dexOffset = static_cast<size_t>(Pokemon::gen9NationalToInternal(species)) * 0x84;
    for (const auto &block : trainer.getBlocks())
    {
        if (block.key == Trainer::ITEM9_LZA)
        {
            notifyCleared = (block.data[plantedItemOffset + 8] & 0x04) == 0;
        }
        if (block.key == Trainer::POKEDEX9_LZA && dexOffset + 0x0A <= block.data.size())
        {
            spanishLatinRecorded = (Utils::readUInt16LittleEndian(&block.data[dexOffset + 0x08]) & (1u << 9)) != 0;
        }
    }
    if (notifyCleared && spanishLatinRecorded)
    {
        std::printf("  PASS  %-22s IsNewNotify cleared on a held item; language 11 is dex bit 9\n", checkLabel);
        ++g_passCount;
        return;
    }
    std::printf("  FAIL  %-22s IsNewNotify cleared=%d, language 11 recorded=%d\n", checkLabel, notifyCleared, spanishLatinRecorded);
    ++g_failCount;
}

/// Legends: Z-A slot footers. PKHeX sets the first gap byte of every box slot it writes to 1 on a
/// Mega Dimension (revision != 0) save, and leaves the rest of the gap as the game wrote it. The only
/// Z-A test save is a 1.0 save, so the revision is raised in the decrypted blocks before parsing.
void slotFooterFollowsRevisionLZA(const std::string &savePath)
{
    const char *checkLabel = "Legends Z-A [slot footer]";
    std::vector<uint8_t> originalSaveBytes = readFile(savePath);
    if (originalSaveBytes.empty())
    {
        std::printf("  SKIP  %-22s (%s)\n", checkLabel, SaveTree::missing(g_saveRoot, "Legends Z-A").c_str());
        return;
    }
    std::vector<Save::Block> blocks = Encryption::decrypt(originalSaveBytes.data(), originalSaveBytes.size());
    for (auto &block : blocks)
    {
        if (block.key == Trainer::SAVE_REVISION9_LZA && !block.data.empty())
        {
            block.data[0] = 1;
        }
    }
    Trainer::Trainer9LZA trainer(std::move(blocks));
    size_t sourceIndex = 0;
    size_t destinationIndex = 0;
    const size_t slotCount = trainer.boxes.size() * Trainer::BOX_SLOTS;
    for (size_t flatIndex = 0; flatIndex < slotCount; ++flatIndex)
    {
        const auto &pokemon = trainer.boxes[flatIndex / Trainer::BOX_SLOTS][flatIndex % Trainer::BOX_SLOTS];
        const bool slotEmpty = !pokemon || pokemon->speciesID() == 0;
        if (!slotEmpty && sourceIndex == 0 && destinationIndex == 0)
        {
            sourceIndex = flatIndex + 1;
        }
        if (slotEmpty)
        {
            destinationIndex = flatIndex + 1;
        }
    }
    if (sourceIndex == 0 || destinationIndex == 0)
    {
        std::printf("  FAIL  %-22s no occupied and empty slot to move between\n", checkLabel);
        ++g_failCount;
        return;
    }
    --sourceIndex;
    --destinationIndex;
    std::vector<uint8_t> blockBefore;
    for (const auto &block : trainer.getBlocks())
    {
        if (block.key == Trainer::BOX9_LZA)
        {
            blockBefore = block.data;
        }
    }
    trainer.swapBoxSlots(sourceIndex / Trainer::BOX_SLOTS, sourceIndex % Trainer::BOX_SLOTS, destinationIndex / Trainer::BOX_SLOTS, destinationIndex % Trainer::BOX_SLOTS);
    trainer.updateBoxBlock();
    std::vector<uint8_t> blockAfter;
    for (const auto &block : trainer.getBlocks())
    {
        if (block.key == Trainer::BOX9_LZA)
        {
            blockAfter = block.data;
        }
    }
    size_t wrongByteCount = 0;
    for (const size_t flatIndex : { sourceIndex, destinationIndex })
    {
        const size_t slotOffset = flatIndex * Encryption::BOX_SLOT_SIZE9_LZA;
        wrongByteCount += blockAfter[slotOffset + Encryption::PRESENT_FLAG_OFFSET9_LZA] != 1;
        for (size_t gapIndex = Encryption::PRESENT_FLAG_OFFSET9_LZA + 1; gapIndex < Encryption::BOX_SLOT_SIZE9_LZA; ++gapIndex)
        {
            wrongByteCount += blockAfter[slotOffset + gapIndex] != blockBefore[slotOffset + gapIndex];
        }
    }
    if (wrongByteCount == 0)
    {
        std::printf("  PASS  %-22s present flag set, rest of the gap kept, in both written slots\n", checkLabel);
        ++g_passCount;
        return;
    }
    std::printf("  FAIL  %-22s %zu footer byte(s) wrong in the two written slots\n", checkLabel, wrongByteCount);
    ++g_failCount;
}

/// A SwishCrypto save is loaded only when it is whole: longer than its hash, hash valid, and parsed to
/// the last byte (PKHeX checks the size and SwishCrypto.GetIsHashValid before parsing anything). A
/// missing `main` used to wrap the length and abort; a truncated one loaded a prefix that Save then
/// re-emitted, short, under a fresh valid hash.
void refusesDamagedSwishSave(const char *label, const std::string &savePath)
{
    const std::string checkLabel = std::string(label) + " [damaged]";
    std::vector<uint8_t> whole = readFile(savePath);
    if (whole.empty())
    {
        std::printf("  SKIP  %-22s (%s)\n", checkLabel.c_str(), SaveTree::missing(g_saveRoot, label).c_str());
        return;
    }
    size_t acceptedCount = 0;
    std::vector<uint8_t> hashOnly(whole.end() - SIZE_HASH_IN_BYTES, whole.end());
    std::vector<uint8_t> truncated(whole.begin(), whole.begin() + whole.size() / 2);
    std::vector<uint8_t> flipped = whole;
    flipped[flipped.size() / 3] ^= 0x01;
    acceptedCount += !Encryption::decrypt(nullptr, 0).empty();
    acceptedCount += !Encryption::decrypt(hashOnly.data(), hashOnly.size()).empty();
    acceptedCount += !Encryption::decrypt(truncated.data(), truncated.size()).empty();
    acceptedCount += !Encryption::decrypt(flipped.data(), flipped.size()).empty();
    const bool emptyTrainerInvalid = !Trainer::Trainer8SWSH(std::vector<Save::Block>()).isValid();
    const bool wholeAccepted = !Encryption::decrypt(whole.data(), whole.size()).empty();
    if (acceptedCount == 0 && emptyTrainerInvalid && wholeAccepted)
    {
        std::printf("  PASS  %-22s empty, hash-only, truncated and bit-flipped files refused; whole file loads\n", checkLabel.c_str());
        ++g_passCount;
        return;
    }
    std::printf("  FAIL  %-22s %zu damaged file(s) accepted, empty trainer invalid=%d, whole file loads=%d\n", checkLabel.c_str(), acceptedCount, emptyTrainerInvalid, wholeAccepted);
    ++g_failCount;
}

/// Battle teams (PKHeX TeamIndexes8 / BoxLayout8b): six teams of six (box << 8) | slot pointers, and
/// PKHeX treats every member as overwrite-protected. PKSE read none of them. A pointer to box 3
/// slot 6 is planted in the first team place; the save's own team members are counted for the log.
template <typename TrainerT>
void battleTeamSlotsLocked(const char *label, const std::string &savePath, size_t teamIndexesKey)
{
    const std::string checkLabel = std::string(label) + " [team locks]";
    std::vector<uint8_t> originalSaveBytes = readFile(savePath);
    if (originalSaveBytes.empty())
    {
        std::printf("  SKIP  %-22s (%s)\n", checkLabel.c_str(), SaveTree::missing(g_saveRoot, label).c_str());
        return;
    }
    std::vector<Save::Block> blocks = Encryption::decrypt(originalSaveBytes.data(), originalSaveBytes.size());
    bool planted = false;
    for (auto &block : blocks)
    {
        if (block.key != teamIndexesKey || block.data.size() < 2)
        {
            continue;
        }
        block.data[0] = 5;
        block.data[1] = 2;
        planted = true;
    }
    TrainerT trainer(std::move(blocks));
    const bool locked = planted && trainer.isBoxSlotLocked(2, 5) && !trainer.isBoxSlotLocked(2, 6) && !trainer.isBoxSlotLocked(5, 2);
    std::printf("  %s  %-22s %s\n", locked ? "PASS" : "FAIL", checkLabel.c_str(), locked ? "a battle-team slot is locked, its neighbours are not" : (planted ? "the planted team slot is not reported locked" : "the save has no team-index block"));
    ++(locked ? g_passCount : g_failCount);
}

void battleTeamSlotsLockedBDSP(const std::string &savePath)
{
    const char *checkLabel = "Shining Pearl [team locks]";
    std::vector<uint8_t> image = readFile(savePath);
    if (image.size() < Trainer::BDSP_TEAM_POSITION_OFFSET + 2)
    {
        std::printf("  SKIP  %-22s (%s)\n", checkLabel, SaveTree::missing(g_saveRoot, "Shining Pearl").c_str());
        return;
    }
    image[Trainer::BDSP_TEAM_POSITION_OFFSET] = 5;
    image[Trainer::BDSP_TEAM_POSITION_OFFSET + 1] = 2;
    Trainer::Trainer8BDSP trainer(std::move(image));
    const bool locked = trainer.isBoxSlotLocked(2, 5) && !trainer.isBoxSlotLocked(2, 6) && !trainer.isBoxSlotLocked(5, 2);
    std::printf("  %s  %-22s %s\n", locked ? "PASS" : "FAIL", checkLabel, locked ? "a battle-team slot is locked, its neighbours are not" : "the planted team slot is not reported locked");
    ++(locked ? g_passCount : g_failCount);
}

/// Brilliant Diamond/Shining Pearl bag: an item is in the bag exactly when its sort order (u16 at
/// record +0xC) is non-zero, and "new" is seen == 0 at +0x4 (PKHeX InventoryItem8b). PKSE used to
/// read presence from the count and wrote seen = 1 for a new item -- so an added item had no sort
/// order (the game never lists it), and a removed one stayed in the bag.
void bagFollowsSortOrderBDSP(const std::string &savePath)
{
    const char *checkLabel = "Shining Pearl [bag]";
    std::vector<uint8_t> image = readFile(savePath);
    if (image.empty())
    {
        std::printf("  SKIP  %-22s (%s)\n", checkLabel, SaveTree::missing(g_saveRoot, "Shining Pearl").c_str());
        return;
    }
    Trainer::Trainer8BDSP trainer(image);
    if (!trainer.isValid() || trainer.items.empty() || trainer.items[0].size() < 2)
    {
        std::printf("  FAIL  %-22s the save did not load, or its Items pouch holds fewer than two items\n", checkLabel);
        ++g_failCount;
        return;
    }
    auto recordOf = [](const std::vector<uint8_t> &saveBytes, uint16_t itemId) { return &saveBytes[Trainer::BDSP_ITEM_BASE + static_cast<size_t>(itemId) * Trainer::ITEM_ENTRY_SIZE8BDSP]; };
    trainer.updateItemBlock();
    const bool uneditedKept = trainer.getSaveData() == image;

    const auto &itemIds = Trainer::getValidItemIds8BDSP(static_cast<Trainer::PouchType8BDSP>(0));
    uint16_t addedItemId = 0;
    uint16_t highestSortOrder = 0;
    for (uint16_t itemId : itemIds)
    {
        const uint16_t sortOrder = Utils::readUInt16LittleEndian(recordOf(image, itemId) + 0xC);
        highestSortOrder = std::max(highestSortOrder, sortOrder);
        if (sortOrder == 0 && addedItemId == 0)
        {
            addedItemId = itemId;
        }
    }
    const uint16_t removedItemId = trainer.items[0].front().itemId;
    trainer.items[0].erase(trainer.items[0].begin());
    trainer.items[0].push_back(Trainer::InventoryItem{ addedItemId, 3, true, false });
    trainer.updateItemBlock();
    const uint8_t *added = recordOf(trainer.getSaveData(), addedItemId);
    const bool addedListed = addedItemId != 0 && Utils::readUInt32LittleEndian(added) == 3 && Utils::readUInt32LittleEndian(added + 4) == 0 && Utils::readUInt16LittleEndian(added + 0xC) == highestSortOrder + 1;
    const uint8_t *removed = recordOf(trainer.getSaveData(), removedItemId);
    const bool removedCleared = std::all_of(removed, removed + Trainer::ITEM_ENTRY_SIZE8BDSP, [](uint8_t byteValue) { return byteValue == 0; });
    Trainer::Trainer8BDSP reloaded(trainer.getSaveData());
    bool reloadedAdded = false;
    bool reloadedRemoved = false;
    for (const auto &item : reloaded.items[0])
    {
        reloadedAdded |= item.itemId == addedItemId;
        reloadedRemoved |= item.itemId == removedItemId;
    }
    if (uneditedKept && addedListed && removedCleared && reloadedAdded && !reloadedRemoved)
    {
        std::printf("  PASS  %-22s unedited bag unchanged; an added item is listed and new, a removed one gone\n", checkLabel);
        ++g_passCount;
        return;
    }
    std::printf("  FAIL  %-22s unedited kept=%d, added listed=%d, removed cleared=%d, reload has added=%d, reload has removed=%d\n", checkLabel, uneditedKept, addedListed, removedCleared, reloadedAdded, reloadedRemoved);
    ++g_failCount;
}

/// Brilliant Diamond/Shining Pearl: only PKHeX's four save sizes, each with its own revision number
/// in the first u32, load. No BDSP save is in the test tree, so the files are synthesized.
void bdspRefusesUnknownSaves()
{
    const char *checkLabel = "BDSP [save sizes]";
    size_t wrongCount = 0;
    wrongCount += Trainer::Trainer8BDSP(std::vector<uint8_t>()).isValid();
    wrongCount += Trainer::Trainer8BDSP(std::vector<uint8_t>(0x148A8, 0)).isValid();
    for (const Trainer::SaveRevision8BDSP &revision : Trainer::SAVE_REVISIONS8_BDSP)
    {
        std::vector<uint8_t> image(revision.fileSize, 0);
        Utils::writeUInt32LittleEndian(image.data(), revision.revisionId + 1);
        wrongCount += Trainer::Trainer8BDSP(image).isValid();
        Utils::writeUInt32LittleEndian(image.data(), revision.revisionId);
        wrongCount += !Trainer::Trainer8BDSP(std::move(image)).isValid();
    }
    if (wrongCount == 0)
    {
        std::printf("  PASS  %-22s empty, short and mis-numbered files refused; all four revisions load\n", checkLabel);
        ++g_passCount;
        return;
    }
    std::printf("  FAIL  %-22s %zu file(s) judged wrongly\n", checkLabel, wrongCount);
    ++g_failCount;
}

/// An FR/LG save that never loaded (installed but never saved, or a short file) has no sector table,
/// so its writers would all write through offset 0. It must report invalid -- the save path refuses
/// it on that -- and serializing it must leave the bytes alone. Same for Ruby/Sapphire/Emerald.
void unreadableGen3SaveWritesNothing()
{
    const char *checkLabel = "Gen 3 [never saved]";
    const std::vector<uint8_t> blankImage(0x20000, 0);
    Trainer::Trainer3FRLG fireRed(blankImage, "");
    Trainer::Trainer3RSE ruby(blankImage, "");
    const bool bothInvalid = !fireRed.isValid() && !ruby.isValid();
    const bool fireRedUntouched = fireRed.serialize() == blankImage;
    const bool rubyUntouched = ruby.serialize() == blankImage;
    if (bothInvalid && fireRedUntouched && rubyUntouched)
    {
        std::printf("  PASS  %-22s a blank 128 KiB image is invalid and serializes to itself\n", checkLabel);
        ++g_passCount;
        return;
    }
    std::printf("  FAIL  %-22s invalid=%d, FR/LG untouched=%d, R/S/E untouched=%d\n", checkLabel, bothInvalid, fireRedUntouched, rubyUntouched);
    ++g_failCount;
}

/// Sword/Shield: registering a Pokemon must write what PKHeX's Zukan8.SetDex writes. The display
/// gender is ONE 2-bit value at bits 29-30 (gender & 1), not a bit each (a male used to show as
/// female); Alcremie's seen bit is form * 7 + its FormArgument (the sweet); Eternamax (Eternatus
/// form 1) sets seen bit 63; and a registered species gets a battled count of at least 1. The
/// Pokemon are copies of the save's first party member, dropped into empty box slots.
void pokedexRegistersLikePKHeXSWSH(const char *label, const std::string &savePath)
{
    const std::string checkLabel = std::string(label) + " [dex register]";
    std::vector<uint8_t> originalSaveBytes = readFile(savePath);
    if (originalSaveBytes.empty())
    {
        std::printf("  SKIP  %-22s (%s)\n", checkLabel.c_str(), SaveTree::missing(g_saveRoot, label).c_str());
        return;
    }
    auto entryOffset = [](uint16_t species) { return static_cast<size_t>(Pokemon::getDexEntry8SWSH(species).index - 1) * Pokemon::DEX8SWSH_ENTRY_SIZE; };
    // Clear Eternatus's seen bit 63 in all four regions first, so the Eternamax check can only pass
    // if the writer sets it.
    auto decryptedBlocks = Encryption::decrypt(originalSaveBytes.data(), originalSaveBytes.size());
    for (auto &block : decryptedBlocks)
    {
        if (block.key != Trainer::SAVE_REVISION8_SWSH || block.data.size() < entryOffset(890) + 4 * 8)
        {
            continue;
        }
        for (size_t region = 0; region < 4; ++region)
        {
            block.data[entryOffset(890) + region * 8 + 7] &= 0x7F;
        }
    }
    Trainer::Trainer8SWSH trainer(std::move(decryptedBlocks));
    auto galarBytes = [&]() -> const std::vector<uint8_t>*
    {
        for (const auto &block : trainer.getBlocks())
        {
            if (block.key == Trainer::SAVE_REVISION8_SWSH)
            {
                return &block.data;
            }
        }
        return nullptr;
    };
    if (trainer.party.empty() || !galarBytes() || galarBytes()->empty())
    {
        std::printf("  SKIP  %-22s no party member or no Galar Pokedex\n", checkLabel.c_str());
        return;
    }
    uint16_t unownedSpecies = 0;
    for (uint16_t species = 1; species <= 898 && unownedSpecies == 0; ++species)
    {
        if (species == 869 || species == 890 || Pokemon::getDexEntry8SWSH(species).dex != Pokemon::Dex8SWSH::Galar)
        {
            continue;
        }
        const std::vector<uint8_t> &galar = *galarBytes();
        if (Utils::readUInt32LittleEndian(&galar[entryOffset(species) + 0x20]) == 0 && Utils::readUInt32LittleEndian(&galar[entryOffset(species) + 0x24]) == 0)
        {
            unownedSpecies = species;
        }
    }
    struct Placed { uint16_t species; uint8_t form; uint8_t gender; uint8_t formArgument; };
    const Placed placements[] = {{unownedSpecies, 0, 0, 0}, {869, 2, 1, 3}, {890, 1, 2, 0}};
    std::vector<int> regions;
    size_t placedCount = 0;
    for (size_t boxIndex = 0; boxIndex < trainer.boxes.size() && placedCount < 3; ++boxIndex)
    {
        for (size_t slotIndex = 0; slotIndex < trainer.getSlotsPerBox() && placedCount < 3; ++slotIndex)
        {
            auto &slot = trainer.boxes[boxIndex][slotIndex];
            if (slot && slot->speciesID() != 0)
            {
                continue;
            }
            const Placed &placed = placements[placedCount];
            auto pokemon = trainer.party[0]->clone();
            pokemon->setSpecies(placed.species);
            pokemon->setForm(placed.form);
            pokemon->setGender(placed.gender);
            pokemon->setEgg(false);
            pokemon->getData()[0xE4] = static_cast<std::byte>(placed.formArgument);
            regions.push_back((pokemon->gender() & 1) | (pokemon->isShiny(pokemon->id32(), pokemon->species()) ? 2 : 0));
            slot = std::move(pokemon);
            ++placedCount;
        }
    }
    if (unownedSpecies == 0 || placedCount < 3)
    {
        std::printf("  SKIP  %-22s no unowned Galar species or not three empty box slots\n", checkLabel.c_str());
        return;
    }
    trainer.updatePokedexBlock();
    const std::vector<uint8_t> &galar = *galarBytes();
    const uint32_t caughtFlags = Utils::readUInt32LittleEndian(&galar[entryOffset(unownedSpecies) + 0x20]);
    const bool maleDisplayed = ((caughtFlags >> 29) & 3) == 0;
    const bool battledOnce = Utils::readUInt32LittleEndian(&galar[entryOffset(unownedSpecies) + 0x24]) == 1;
    auto seenBit = [&](uint16_t species, int region, unsigned bitIndex)
    {
        const uint64_t seen = Utils::readUInt64LittleEndian(&galar[entryOffset(species) + static_cast<size_t>(region) * 8]);
        return ((seen >> bitIndex) & 1) != 0;
    };
    const bool alcremieSweetSeen = seenBit(869, regions[1], 2 * 7 + 3);
    const bool eternamaxSeen = seenBit(890, regions[2], 63) && seenBit(890, regions[2], 0);
    if (maleDisplayed && battledOnce && alcremieSweetSeen && eternamaxSeen)
    {
        std::printf("  PASS  %-22s display gender, battled count, Alcremie and Eternamax as PKHeX\n", checkLabel.c_str());
        ++g_passCount;
        return;
    }
    std::printf("  FAIL  %-22s male displayed=%d, battled once=%d, Alcremie sweet seen=%d, Eternamax seen=%d\n", checkLabel.c_str(), maleDisplayed, battledOnce, alcremieSweetSeen, eternamaxSeen);
    ++g_failCount;
}

/// Scarlet/Violet: the Pokedex writer must not re-pick the displayed variant (form, gender, shiny)
/// of a species the save already has. "Nothing displayed" and "form 0, male, not shiny" are the
/// same zero bytes, so a writer that tested the display bytes overwrote the player's choice with
/// whichever specimen came last. Kitakami entries are 0x20 bytes, obtained-form bits at +0x00 and
/// the three regional display slots at +0x14/+0x18/+0x1C.
void pokedexKeepsDisplayedVariantSV(const char *label, const std::string &savePath)
{
    const std::string checkLabel = std::string(label) + " [dex display]";
    std::vector<uint8_t> originalSaveBytes = readFile(savePath);
    if (originalSaveBytes.empty())
    {
        std::printf("  SKIP  %-22s (%s)\n", checkLabel.c_str(), SaveTree::missing(g_saveRoot, label).c_str());
        return;
    }
    Trainer::Trainer9SV trainer(Encryption::decrypt(originalSaveBytes.data(), originalSaveBytes.size()));
    auto kitakamiBytes = [&]()
    {
        for (const auto &block : trainer.getBlocks())
        {
            if (block.key == Trainer::ZUKAN9_SV_KITAKAMI)
            {
                return block.data;
            }
        }
        return std::vector<uint8_t>();
    };
    const std::vector<uint8_t> dexBefore = kitakamiBytes();
    if (dexBefore.empty())
    {
        std::printf("  SKIP  %-22s the save has no Kitakami Pokedex block\n", checkLabel.c_str());
        return;
    }
    trainer.updatePokedexBlock();
    const std::vector<uint8_t> dexAfter = kitakamiBytes();
    constexpr size_t entrySize = 0x20;
    size_t changedEntryCount = 0;
    size_t obtainedEntryCount = 0;
    for (size_t entryOffset = 0; entryOffset + entrySize <= dexBefore.size(); entryOffset += entrySize)
    {
        if (Utils::readUInt32LittleEndian(&dexBefore[entryOffset]) == 0)
        {
            continue;
        }
        ++obtainedEntryCount;
        changedEntryCount += !std::equal(dexBefore.begin() + entryOffset + 0x14, dexBefore.begin() + entryOffset + entrySize, dexAfter.begin() + entryOffset + 0x14);
    }
    if (changedEntryCount == 0)
    {
        std::printf("  PASS  %-22s %zu obtained species keep their displayed variant\n", checkLabel.c_str(), obtainedEntryCount);
        ++g_passCount;
        return;
    }
    std::printf("  FAIL  %-22s %zu of %zu obtained species had their displayed variant re-picked\n", checkLabel.c_str(), changedEntryCount, obtainedEntryCount);
    ++g_failCount;
}

/// Japanese Gen 3: trainer names are five characters (PKHeX SAV3.OT), because the zero pair the
/// Japanese games leave in the name field's bytes 6-7 is what marks the save Japanese. A six-glyph
/// rename used to overwrite byte 6 and turn the save international on the next load. There is no
/// Japanese test save, so the FireRed save is made one by zeroing those bytes in its Small sectors
/// (sector id 0, found by the 0xFF4 footer of each 0x1000-byte sector in both save slots).
void japaneseTrainerNameStaysFiveFRLG(const std::string &savePath)
{
    const char *checkLabel = "FireRed [JP name]";
    std::vector<uint8_t> image = readFile(savePath);
    if (image.size() < 0x1C000)
    {
        std::printf("  SKIP  %-22s (%s)\n", checkLabel, SaveTree::missing(g_saveRoot, "FireRed").c_str());
        return;
    }
    for (size_t sectorOffset = 0; sectorOffset + 0x1000 <= 0x1C000; sectorOffset += 0x1000)
    {
        if (Utils::readUInt16LittleEndian(&image[sectorOffset + 0xFF4]) != 0)
        {
            continue;
        }
        image[sectorOffset + 6] = 0;
        image[sectorOffset + 7] = 0;
    }
    Trainer::Trainer3FRLG japaneseSave(image, "");
    const bool readsJapanese = japaneseSave.isValid() && japaneseSave.language() == static_cast<uint8_t>(Enums::LanguageID::Japanese);
    const bool capsAtFive = japaneseSave.getMaxTrainerNameLength() == 5;
    japaneseSave.trainerName = "\xE3\x81\x82\xE3\x81\x84\xE3\x81\x86\xE3\x81\x88\xE3\x81\x8A\xE3\x81\x8B"; // six hiragana
    const std::vector<uint8_t> written = japaneseSave.serialize();
    Trainer::Trainer3FRLG reloaded(written, "");
    const bool staysJapanese = reloaded.isValid() && reloaded.language() == static_cast<uint8_t>(Enums::LanguageID::Japanese);
    if (readsJapanese && capsAtFive && staysJapanese)
    {
        std::printf("  PASS  %-22s five-character names; a six-glyph rename leaves the save Japanese\n", checkLabel);
        ++g_passCount;
        return;
    }
    std::printf("  FAIL  %-22s reads Japanese=%d, capped at five=%d, still Japanese after a rename=%d\n", checkLabel, readsJapanese, capsAtFive, staysJapanese);
    ++g_failCount;
}

/// Legends: Arceus: a newly registered species shows the gender PKHeX's SetSelectedGenderForm
/// picks -- for an entry whose genders are separate records, simply the Pokemon's own. The writer
/// used to gate the female flag on PokemonInfoGenders bit 3, which marks the SHARED records, so a
/// female of a separate-record species showed as male. A clone of the first boxed Pokemon becomes
/// a female of the first such species the save has never caught.
void selectedGenderFollowsPKHeXLA(const char *label, const std::string &savePath)
{
    const std::string checkLabel = std::string(label) + " [dex gender]";
    std::vector<uint8_t> originalSaveBytes = readFile(savePath);
    if (originalSaveBytes.empty())
    {
        std::printf("  SKIP  %-22s (%s)\n", checkLabel.c_str(), SaveTree::missing(g_saveRoot, label).c_str());
        return;
    }
    using Field = Trainer::ResearchField8LA;
    Trainer::Trainer8LA trainer(Encryption::decrypt(originalSaveBytes.data(), originalSaveBytes.size()));
    auto pokedexBytes = [&]()
    {
        for (const auto &block : trainer.getBlocks())
        {
            if (block.key == Trainer::POKEDEX8_LA)
            {
                return block.data;
            }
        }
        return std::vector<uint8_t>();
    };
    const std::vector<uint8_t> loadedPokedex = pokedexBytes();
    uint16_t targetSpecies = 0;
    for (uint16_t speciesId = 1; speciesId < Pokemon::DEX8LA_MAX_SPECIES && targetSpecies == 0; ++speciesId)
    {
        if (Pokemon::getStatisticsIndex8LA(speciesId, 0) == Pokemon::DEX8LA_NO_STAT_ENTRY || Pokemon::sharesGenderRecord8LA(speciesId, 0))
        {
            continue;
        }
        if (Trainer::Trainer8LA::researchField(loadedPokedex, speciesId, Field::CatchCount) == 0 && (Trainer::Trainer8LA::researchField(loadedPokedex, speciesId, Field::Flags) & 0x38) == 0)
        {
            targetSpecies = speciesId;
        }
    }
    const Pokemon::Pokemon *sourcePokemon = nullptr;
    std::unique_ptr<Pokemon::Pokemon> *emptySlot = nullptr;
    for (auto &box : trainer.boxes)
    {
        for (auto &slotPokemon : box)
        {
            if (!slotPokemon || slotPokemon->speciesID() == 0)
            {
                if (!emptySlot)
                {
                    emptySlot = &slotPokemon;
                }
            }
            else if (!sourcePokemon)
            {
                sourcePokemon = slotPokemon.get();
            }
        }
    }
    if (targetSpecies == 0 || !sourcePokemon || !emptySlot)
    {
        std::printf("  SKIP  %-22s no uncaught separate-gender species, boxed Pokemon or empty slot\n", checkLabel.c_str());
        return;
    }
    auto female = sourcePokemon->clone();
    female->setSpecies(targetSpecies);
    female->setForm(0);
    female->setGender(1);
    female->setEgg(false);
    *emptySlot = std::move(female);
    trainer.updatePokedexBlock();
    const uint16_t flags = Trainer::Trainer8LA::researchField(pokedexBytes(), targetSpecies, Field::Flags);
    if ((flags & 0x08) != 0)
    {
        std::printf("  PASS  %-22s a new female of species %u is displayed female\n", checkLabel.c_str(), targetSpecies);
        ++g_passCount;
        return;
    }
    std::printf("  FAIL  %-22s a new female of species %u is displayed male (flags 0x%04X)\n", checkLabel.c_str(), targetSpecies, flags);
    ++g_failCount;
}

/// Let's Go: a size record taken is written to the species' whole evolution line (PKHeX
/// Zukan7b.SetSizeData walks GetEvolutionsAndPreEvolutions). PKSE wrote only the species' own entry.
/// A clone of the first boxed Pokemon becomes an Ivysaur smaller than any recorded (height scalar
/// 0); Bulbasaur's and Venusaur's minimum-height records must then match Ivysaur's.
void sizeRecordCoversEvolutionLineLGPE(const char *label, const std::string &savePath)
{
    const std::string checkLabel = std::string(label) + " [size lineage]";
    const std::vector<uint8_t> originalSaveBytes = readFile(savePath);
    if (originalSaveBytes.size() < Trainer::SAVE_SIZE7_LGPE)
    {
        std::printf("  SKIP  %-22s (%s)\n", checkLabel.c_str(), SaveTree::missing(g_saveRoot, label).c_str());
        return;
    }
    const std::vector<uint8_t> area(originalSaveBytes.begin(), originalSaveBytes.begin() + Trainer::SAVE_SIZE7_LGPE);
    Trainer::Trainer7LGPE trainer(Trainer::createBlocksFromSaveData7LGPE(area));
    const Pokemon::Pokemon *sourcePokemon = nullptr;
    std::unique_ptr<Pokemon::Pokemon> *emptySlot = nullptr;
    for (auto &box : trainer.boxes)
    {
        for (auto &slotPokemon : box)
        {
            if (!slotPokemon || slotPokemon->speciesID() == 0)
            {
                if (!emptySlot)
                {
                    emptySlot = &slotPokemon;
                }
            }
            else if (!sourcePokemon && !isPartnerLGPE(*slotPokemon))
            {
                sourcePokemon = slotPokemon.get();
            }
        }
    }
    if (!sourcePokemon || !emptySlot)
    {
        std::printf("  SKIP  %-22s no boxed Pokemon or no empty slot\n", checkLabel.c_str());
        return;
    }
    auto ivysaur = sourcePokemon->clone();
    ivysaur->setSpecies(2);
    ivysaur->setForm(0);
    ivysaur->setEgg(false);
    auto writeFloat = [&](size_t offset, float floatValue) { std::memcpy(ivysaur->getData().data() + offset, &floatValue, sizeof floatValue); };
    ivysaur->getData()[0x3A] = std::byte{0};   // height scalar
    ivysaur->getData()[0x3B] = std::byte{200}; // weight scalar
    writeFloat(0x2C, 0.25f);                   // height, far below Ivysaur's base
    writeFloat(0xE4, 1.0f);                    // weight, far below Ivysaur's base
    *emptySlot = std::move(ivysaur);
    trainer.updatePokedexBlock();
    const std::vector<uint8_t> zukan = zukanBlockLGPE(trainer);
    auto minimumHeightRecord = [&](int entryIndex)
    {
        const size_t recordOffset = sizeRecordOffsetLGPE(0, entryIndex);
        return std::vector<uint8_t>(zukan.begin() + recordOffset, zukan.begin() + recordOffset + 4);
    };
    const std::vector<uint8_t> ivysaurRecord = minimumHeightRecord(1);
    const bool ivysaurTaken = ivysaurRecord[0] == 0 && ivysaurRecord[1] == 0 && ivysaurRecord[2] == 200;
    const bool lineageTaken = minimumHeightRecord(0) == ivysaurRecord && minimumHeightRecord(2) == ivysaurRecord;
    if (ivysaurTaken && lineageTaken)
    {
        std::printf("  PASS  %-22s Bulbasaur and Venusaur take Ivysaur's new minimum height\n", checkLabel.c_str());
        ++g_passCount;
        return;
    }
    std::printf("  FAIL  %-22s Ivysaur record taken=%d, evolution line updated=%d\n", checkLabel.c_str(), ivysaurTaken, lineageTaken);
    ++g_failCount;
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::printf("usage: roundtrip <save-root>\n  Expects, relative to <save-root>:\n  One folder per TITLE, any filename inside:\n    FireRed/            Let's Go Pikachu/\n    Sword/  Shield/  Violet/  Legends Z-A/  Legends Arceus/\n");
        return 2;
    }
    const std::string root = argv[1];
    g_saveRoot = root;

    std::printf("PKSE save round-trip harness\nread -> parse -> serialize back -> compare. A no-op edit must reproduce the file exactly.\n\n");

    // Found by TITLE FOLDER -- <root>/Shield/, <root>/Violet/ -- with any filename inside.
    // See SaveTree.h for why the folder is the authority and not the file name.
    roundTripFRLG(SaveTree::fireRed(root));
    roundTripRSE("Ruby", SaveTree::ruby(root), root);
    roundTripRSE("Sapphire", SaveTree::sapphire(root), root);
    roundTripRSE("Emerald", SaveTree::emerald(root), root);
    interruptedSaveUsesOlderSlot<Trainer::Trainer3FRLG>("FireRed", SaveTree::fireRed(root));
    interruptedSaveUsesOlderSlot<Trainer::Trainer3RSE>("Ruby", SaveTree::ruby(root));
    interruptedSaveUsesOlderSlot<Trainer::Trainer3RSE>("Emerald", SaveTree::emerald(root));
    const std::string swordPath = SaveTree::sword(root);
    const std::string shieldPath = SaveTree::shield(root);
    roundTripSCBlock<Trainer::Trainer8SWSH>("Sword", swordPath);
    itemEditsStayInPlaceSWSH("Sword", swordPath);
    itemRemovalEmptiesOneSlotSWSH("Sword", swordPath);
    roundTripSCBlock<Trainer::Trainer8SWSH>("Shield", shieldPath);
    itemEditsStayInPlaceSWSH("Shield", shieldPath);
    itemRemovalEmptiesOneSlotSWSH("Shield", shieldPath);
    roundTripSCBlock<Trainer::Trainer9LZA>("Legends Z-A", SaveTree::legendsZA(root));
    roundTripSCBlock<Trainer::Trainer9SV>("Violet", SaveTree::violet(root));
    roundTripSCBlock<Trainer::Trainer8LA>("Legends Arceus", SaveTree::legendsArceus(root));
    moveIntoEmptySlotTouchesTwoSlots<Trainer::Trainer8SWSH>("Sword", swordPath, Trainer::BOX8_SWSH, Encryption::SIZE_PARTY8_SWSH);
    moveIntoEmptySlotTouchesTwoSlots<Trainer::Trainer8SWSH>("Shield", shieldPath, Trainer::BOX8_SWSH, Encryption::SIZE_PARTY8_SWSH);
    moveIntoEmptySlotTouchesTwoSlots<Trainer::Trainer8LA>("Legends Arceus", SaveTree::legendsArceus(root), Trainer::BOX8_LA, Encryption::SIZE_STORED8_LA);
    moveIntoEmptySlotTouchesTwoSlots<Trainer::Trainer9SV>("Violet", SaveTree::violet(root), Trainer::BOX9_SV, Encryption::SIZE_PARTY9_SV);
    moveIntoEmptySlotTouchesTwoSlots<Trainer::Trainer9LZA>("Legends Z-A", SaveTree::legendsZA(root), Trainer::BOX9_LZA, Encryption::BOX_SLOT_SIZE9_LZA);
    slotFooterFollowsRevisionLZA(SaveTree::legendsZA(root));
    itemAndDexLanguageFlagsLZA(SaveTree::legendsZA(root));
    refusesDamagedSwishSave("Shield", shieldPath);
    bdspRefusesUnknownSaves();
    bagFollowsSortOrderBDSP(SaveTree::forTitle(root, "Shining Pearl"));
    battleTeamSlotsLockedBDSP(SaveTree::forTitle(root, "Shining Pearl"));
    battleTeamSlotsLocked<Trainer::Trainer8SWSH>("Sword", swordPath, Trainer::TEAM_INDEXES8_SWSH);
    battleTeamSlotsLocked<Trainer::Trainer9SV>("Violet", SaveTree::violet(root), Trainer::TEAM_INDEXES9_SV);
    battleTeamSlotsLocked<Trainer::Trainer9LZA>("Legends Z-A", SaveTree::legendsZA(root), Trainer::TEAM_INDEXES9_LZA);
    unreadableGen3SaveWritesNothing();
    japaneseTrainerNameStaysFiveFRLG(SaveTree::fireRed(root));
    pokedexKeepsDisplayedVariantSV("Violet", SaveTree::violet(root));
    pokedexRegistersLikePKHeXSWSH("Sword", swordPath);
    selectedGenderFollowsPKHeXLA("Legends Arceus", SaveTree::legendsArceus(root));
    catchResearchCountsOnceLA("Legends Arceus", SaveTree::legendsArceus(root));
    roundTripLGPE("Let's Go Pikachu", SaveTree::letsGoPikachu(root));
    roundTripLGPE("Let's Go Eevee", SaveTree::letsGoEevee(root));
    sizeRecordCoversEvolutionLineLGPE("Let's Go Pikachu", SaveTree::letsGoPikachu(root));

    std::printf("\n%d passed, %d known divergence(s), %d FAILED\n", g_passCount, g_knownCount, g_failCount);
    if (g_knownCount)
    {
        std::printf("Known divergences are intended behaviour. Watch their byte counts: a change\nthere means something moved that nobody decided to move.\n");
    }
    return g_failCount == 0 ? 0 : 1;
}
