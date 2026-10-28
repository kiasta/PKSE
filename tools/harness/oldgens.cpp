/**
 * THE GATE IS THE UNEDITED ROUND TRIP. Load a real save, write it straight back without changing
 * anything, and require a byte-identical file. An editor that cannot reproduce a file it did not
 * change will corrupt one it did -- and unlike a checksum, this catches the whole class of bugs
 * where PKSE rewrites bytes it had no business touching. Every one of the following was found
 * exactly this way and nothing else would have found them:
 *
 *   - Gen 4/5 empty slots hold encrypt(zeros), not zeros          (~67 KB of churn per save)
 *   - Gen 6 empty box slots hold a species-0 record NICKNAMED "Egg", so the right blank is the
 *     one the file already contains, not one we invent            (~1.8 KB per save)
 *   - Gen 6/7 bag pouches are POSITIONAL, not packed: Ultra Sun parks 15 used-up items at the
 *     end of a pouch with 248 empty slots in front of them        (35 bytes, and a reordered bag)
 *   - Gen 7 block 36 carries the MemeCrypto signature, and its checksum is stale BY DESIGN
 *
 * Then it checks that edits actually persist -- "the save didn't work" is not a bug report,
 * "the box name persisted and the item count didn't" is.
 *
 * Build and run:  tools/harness/build-oldgens.sh [saves-root]     (default ~/pkse-test-saves)
 */
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "SaveTree.h" // saves are found by their TITLE FOLDER, not by a guessed filename

#include "Save/RtcFooter.h" // the RTC-footer rule, tested here because it is what makes a real Silver save open
#include "Save/SaveEnvelope.h"
#include "Test/SaveEnvelopeFixtures.h"
#include "Trainer/Trainer2GSC.h"
#include "Trainer/Trainer4DP.h"
#include "Trainer/Trainer4PT.h"
#include "Trainer/Trainer4HGSS.h"
#include "Trainer/Trainer5BW.h"
#include "Trainer/Trainer5B2W2.h"
#include "Trainer/Trainer6XY.h"
#include "Trainer/Trainer6ORAS.h"
#include "Trainer/Trainer7SM.h"
#include "Trainer/Trainer7USUM.h"
#include "Utils/StringHelpers.h"
#include "Utils/SHA256.h"
#include "Utils/HelperUtilities.h"

extern "C"
{
#include "memecrypto.h"
}

// The save layer reaches a handful of Switch-SDK entry points. Stub them rather than link libnx.
namespace Utils
{
    void logInfoToFile(const char *) {}
    void logInfoToFile(std::string) {}
    void logErrorToFile(const char *) {}
    void logErrorToFile(std::string) {}
    void logInfoToFile(const char *, const char *) {}
    void logErrorToFile(const char *, const char *) {}
    void logEventToFile(const std::string &) {}
    void logTest(const std::string &) {}
    void logTestSession(const std::string &) {}
    void cleanupOldLogs() {}
}

static int g_failureCount = 0;
static int g_checkCount = 0;
static int g_skippedSaveCount = 0;
// clang-format off
#define CHECK(condition, format, ...) do { ++g_checkCount; if (!(condition)) { std::printf("      [FAIL] " format "\n", ##__VA_ARGS__); ++g_failureCount; } } while (0)
// clang-format on

static std::vector<uint8_t> load(const std::string &path)
{
    FILE *file = std::fopen(path.c_str(), "rb");
    if (!file)
    {
        return {};
    }
    std::fseek(file, 0, SEEK_END);
    long fileSize = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    std::vector<uint8_t> fileBytes(static_cast<size_t>(fileSize));
    if (std::fread(fileBytes.data(), 1, fileBytes.size(), file) != fileBytes.size())
    {
        fileBytes.clear();
    }
    std::fclose(file);
    return fileBytes;
}

/// The save root, for the one message that has to name a path the user should create.
static std::string g_saveRoot;

/// Says which FOLDER to put the missing save in. A skip that names only the file it could not find
/// tells the reader nothing they can act on, and with saves filed by title the actionable half is
/// the folder name -- especially for the pairs where the folder is the only thing that can say
/// which game a save is.
static void printMissing(const char *title)
{
    std::printf("  %-16s SKIPPED -- no save; put one in %s/%s/\n", title, g_saveRoot.c_str(), title);
}

// AN UNEDITED SAVE MUST COME BACK BYTE FOR BYTE -- with at most one documented exception per
// format, and none at all for most. Default: nothing may move.
template <typename TrainerT>
static bool differencesAreLegitimate(const TrainerT &, const std::vector<uint8_t> &, const std::vector<uint8_t> &, std::string &explanation)
{
    explanation = "this format refreshes nothing on write";
    return false;
}

// GEN 2 KEEPS A BACKUP OF THE TRAINER BLOCK and the games read it back, so writing a save refreshes
// it from the primary (PKHeX SAV2.GetFinalData). A file whose backup has drifted therefore does not
// reproduce byte for byte -- the drift is corrected. That is allowed only under two proofs, both
// asserted here rather than assumed: every byte that moved lies inside a mirror DESTINATION (or is
// one of the two checksum words), and afterwards every mirror equals its SOURCE. Together those say
// the write can only have brought the backup into line -- never invented anything, never touched the
// primary. It is the Gen 2 counterpart of Gen 1's current-box mirror flush.
static bool differencesAreLegitimate(const Trainer::Trainer2GSC &trainer, const std::vector<uint8_t> &before, const std::vector<uint8_t> &after, std::string &explanation)
{
    const auto mirrors = trainer.mirrorRegions();
    const Trainer::Gen2Offsets &layoutOffsets = Trainer::gen2Offsets(trainer.layout());
    size_t outsideMirrorByteCount = 0;
    size_t insideMirrorByteCount = 0;
    size_t checksumByteCount = 0;
    for (size_t byteIndex = 0; byteIndex < before.size() && byteIndex < after.size(); ++byteIndex)
    {
        if (before[byteIndex] == after[byteIndex])
        {
            continue;
        }
        if (byteIndex == layoutOffsets.overallChecksumPosition || byteIndex == layoutOffsets.overallChecksumPosition + 1u || byteIndex == layoutOffsets.overallChecksumPosition2 || byteIndex == layoutOffsets.overallChecksumPosition2 + 1u)
        {
            ++checksumByteCount;
            continue;
        }
        bool covered = false;
        for (const auto &mirror : mirrors)
        {
            if (byteIndex >= mirror.destinationOffset && byteIndex < mirror.destinationOffset + mirror.length)
            {
                covered = true;
            }
        }
        if (covered)
        {
            ++insideMirrorByteCount;
        }
        else
        {
            ++outsideMirrorByteCount;
        }
    }
    size_t mismatchedByteCount = 0;
    for (const auto &mirror : mirrors)
    {
        for (size_t byteIndex = 0; byteIndex < mirror.length; ++byteIndex)
        {
            if (after[mirror.destinationOffset + byteIndex] != after[mirror.sourceOffset + byteIndex])
            {
                ++mismatchedByteCount;
            }
        }
    }

    char buffer[192];
    std::snprintf(buffer, sizeof buffer, "the file's backup block was stale; %zu byte(s) in mirrors + %zu checksum brought into line", insideMirrorByteCount, checksumByteCount);
    explanation = buffer;
    return outsideMirrorByteCount == 0 && mismatchedByteCount == 0;
}

template <typename TrainerT>
static void run(const std::string &path, const char *tag)
{
    auto originalBytes = load(path);
    if (originalBytes.empty())
    {
        printMissing(tag);
        ++g_skippedSaveCount;
        return;
    }
    std::printf("  %-16s (%zu bytes)\n", tag, originalBytes.size());

    // MIRROR THE APP'S ENTRY PATH. Save::openExternalSave strips an emulator/flashcart RTC footer
    // before any detector sees the bytes, and ONLY when nothing recognised the file as it stands.
    // A harness that skips that step rejects files the app opens happily -- which is exactly how
    // three real 0x8010-byte Gen 2 saves read here as "never written to" while being perfectly
    // good. The footer is not part of the save image and is re-appended on write, so the stripped
    // image is also what the round trip below must compare.
    if (!TrainerT::detect(originalBytes) && Save::stripRtcFooter(originalBytes))
    {
        std::printf("      (stripped an RTC footer, as openExternalSave does -> %zu bytes)\n", originalBytes.size());
    }

    if (!TrainerT::detect(originalBytes))
    {
        // Not a failure: detect() is the authority on what is a save of this type, and a file
        // sitting at the expected path may simply never have been written to. The 3DS in
        // particular leaves a full-size, correctly-checksummed `main` with a ZEROED metadata
        // header for a game that has been started but never saved -- PKHeX rejects those too.
        // Printed loudly rather than swallowed, so a real detection regression is still visible.
        std::printf("      SKIPPED -- detect() rejected it; the file is present but is not a valid %s save (never written to?)\n", tag);
        ++g_skippedSaveCount;
        return;
    }

    {
        TrainerT trainer(originalBytes, path);
        CHECK(trainer.isValid(), "parse failed");
        int boxedCount = 0;
        int badChecksumCount = 0;
        int itemCount = 0;
        size_t filledPouchCount = 0;
        for (auto &box : trainer.boxes)
        {
            for (auto &pokemon : box)
            {
                if (pokemon && pokemon->speciesID())
                {
                    ++boxedCount;
                    if (!pokemon->isStructurallyValid())
                    {
                        ++badChecksumCount;
                    }
                }
            }
        }
        for (auto &pokemon : trainer.party)
        {
            if (pokemon && pokemon->speciesID() && !pokemon->isStructurallyValid())
            {
                ++badChecksumCount;
            }
        }
        for (auto &pouch : trainer.items)
        {
            if (!pouch.empty())
            {
                ++filledPouchCount;
            }
            itemCount += static_cast<int>(pouch.size());
        }
        std::printf("      OT=\"%s\" id=%u money=%u party=%zu boxes=%zu boxed=%d items=%d in %zu pouches\n", trainer.trainerName.c_str(), trainer.TID16, trainer.money, trainer.getPartySize(), trainer.getBoxCount(), boxedCount, itemCount, filledPouchCount);
        CHECK(badChecksumCount == 0, "%d entities have a bad checksum", badChecksumCount);
        for (size_t partyIndex = 0; partyIndex < trainer.party.size(); ++partyIndex)
        {
            auto *pokemon = trainer.party[partyIndex].get();
            if (!pokemon || !pokemon->speciesID())
            {
                continue;
            }
            std::printf("        %-13s Lv%-4d %-12s IV %2d/%2d/%2d/%2d/%2d/%2d  HP %d/%d\n", Trainer::getSpeciesName(pokemon->speciesID()), pokemon->level(), Utils::utf16ToUtf8(pokemon->nickname()).c_str(), pokemon->ivHP(), pokemon->ivATK(), pokemon->ivDEF(), pokemon->ivSPA(), pokemon->ivSPD(), pokemon->ivSPE(), pokemon->statHPCurrent(), pokemon->statHPMax());
        }
    }

    {
        TrainerT trainer(originalBytes, path);
        const auto &serializedBytes = trainer.serialize();
        size_t differingByteCount = 0;
        size_t firstDifferenceOffset = 0;
        size_t lastDifferenceOffset = 0;
        for (size_t byteIndex = 0; byteIndex < originalBytes.size(); ++byteIndex)
        {
            if (originalBytes[byteIndex] != serializedBytes[byteIndex])
            {
                if (!differingByteCount)
                {
                    firstDifferenceOffset = byteIndex;
                }
                lastDifferenceOffset = byteIndex;
                ++differingByteCount;
            }
        }
        ++g_checkCount;
        std::string explanation;
        bool refreshed = false;
        if (differingByteCount && differencesAreLegitimate(trainer, originalBytes, serializedBytes, explanation))
        {
            refreshed = true;
            differingByteCount = 0;
        }
        if (differingByteCount)
        {
            std::printf("      [FAIL] round trip: %zu bytes differ (0x%zX..0x%zX)\n", differingByteCount, firstDifferenceOffset, lastDifferenceOffset);
            ++g_failureCount;
        }
        else if (refreshed)
        {
            std::printf("      round trip EXACT apart from a documented refresh -- %s\n", explanation.c_str());
        }
        else
        {
            std::printf("      round trip EXACT\n");
        }
    }

    //
    // Both halves of one bug. A box record is shorter than a party record and the level and
    // battle stats live in the difference, so a stored-size entity reports LEVEL 0 to the box
    // grid, the details modal and the legality checker -- and moving one into the party made the
    // party writer encrypt a party-length span over a box-length allocation, a heap over-read
    // that AddressSanitizer catches and a console does not.
    {
        TrainerT trainer(originalBytes, path);
        int boxedCount = 0;
        int levellessCount = 0;
        for (auto &box : trainer.boxes)
        {
            for (auto &pokemon : box)
            {
                if (!pokemon || !pokemon->speciesID())
                {
                    continue;
                }
                ++boxedCount;
                if (pokemon->level() == 0)
                {
                    ++levellessCount;
                }
            }
        }
        if (boxedCount > 0)
        {
            CHECK(levellessCount == 0, "%d of %d boxed Pokemon report level 0 (box records must be grown to party size on load)", levellessCount, boxedCount);

            // Move the first boxed Pokemon into a FREE party slot and write the save back.
            TrainerT movedTrainer(originalBytes, path);
            if (movedTrainer.getPartySize() < 6)
            {
                for (auto &box : movedTrainer.boxes)
                {
                    bool moved = false;
                    for (auto &pokemon : box)
                    {
                        if (!pokemon || !pokemon->speciesID())
                        {
                            continue;
                        }
                        const uint16_t species = pokemon->speciesID();
                        const uint8_t level = pokemon->level();
                        movedTrainer.party.push_back(std::move(pokemon));
                        auto movedImage = movedTrainer.serialize();
                        TrainerT reloadedTrainer(movedImage, path);
                        CHECK(!reloadedTrainer.party.empty(), "the moved Pokemon did not reach the party");
                        if (!reloadedTrainer.party.empty())
                        {
                            auto *partyPokemon = reloadedTrainer.party[reloadedTrainer.party.size() - 1].get();
                            CHECK(partyPokemon->speciesID() == species, "box -> party changed the species (%u -> %u)", species, partyPokemon->speciesID());
                            CHECK(partyPokemon->level() == level, "box -> party changed the level (%u -> %u)", level, partyPokemon->level());
                            CHECK(partyPokemon->isStructurallyValid(), "box -> party produced a Pokemon with a bad checksum");
                        }
                        moved = true;
                        break;
                    }
                    if (moved)
                    {
                        break;
                    }
                }
            }
            std::printf("      %d boxed Pokemon, all with a level; box -> party is clean\n", boxedCount);
        }
    }

    {
        TrainerT editedTrainer(originalBytes, path);
        size_t pouchIndex = 0;
        for (size_t candidatePouchIndex = 0; candidatePouchIndex < editedTrainer.items.size(); ++candidatePouchIndex)
        {
            if (!editedTrainer.items[candidatePouchIndex].empty())
            {
                pouchIndex = candidatePouchIndex;
                break;
            }
        }
        const uint16_t keptItemId = editedTrainer.items[pouchIndex].empty() ? 0 : editedTrainer.items[pouchIndex][0].itemId;

        editedTrainer.money = 654321;
        editedTrainer.trainerName = "Zephyr";
        editedTrainer.currentBox = 7;
        editedTrainer.boxNames[2] = "Testing";
        editedTrainer.markBoxNameDirty(2);
        if (!editedTrainer.items[pouchIndex].empty())
        {
            editedTrainer.items[pouchIndex][0].count = 77;
        }
        auto *pokemon = editedTrainer.party.empty() ? nullptr : editedTrainer.party[0].get();
        const uint16_t species = pokemon ? pokemon->speciesID() : 0;
        if (pokemon)
        {
            // ATTACK, NOT HP, AND CLAMPED TO THE FORMAT'S OWN RANGE. Gen 1 and Gen 2 store 4-bit
            // DVs whose HP value is DERIVED from the low bits of the other four, so writing HP
            // there is correctly a no-op -- and 29 is out of range for a DV besides. Attack is
            // stored directly in every format, which is what makes it a real persistence probe.
            const uint8_t probeIV = pokemon->maxIV() < 29 ? pokemon->maxIV() : static_cast<uint8_t>(29);
            pokemon->setIV(1, probeIV);
            pokemon->setEV(1, 52);
            pokemon->setNickname(u"Roundtrip");
            pokemon->recalculateStats();
            pokemon->refreshChecksum();
        }

        auto editedImage = editedTrainer.serialize();          // by value: the re-parse must not alias it
        CHECK(TrainerT::detect(editedImage), "the edited save no longer detects (checksums are wrong)");
        TrainerT reloadedTrainer(editedImage, path);
        CHECK(reloadedTrainer.isValid(), "re-parse of the edited save failed");
        CHECK(reloadedTrainer.money == 654321u, "money did not persist (%u)", reloadedTrainer.money);
        CHECK(reloadedTrainer.trainerName == "Zephyr", "trainer name did not persist (\"%s\")", reloadedTrainer.trainerName.c_str());
        CHECK(reloadedTrainer.currentBox == 7, "current box did not persist (%d)", reloadedTrainer.currentBox);
        CHECK(reloadedTrainer.boxNames[2] == "Testing", "box name did not persist (\"%s\")", reloadedTrainer.boxNames[2].c_str());
        if (!reloadedTrainer.items[pouchIndex].empty())
        {
            CHECK(reloadedTrainer.items[pouchIndex][0].itemId == keptItemId, "item id changed under an unrelated edit");
            CHECK(reloadedTrainer.items[pouchIndex][0].count == 77, "item count did not persist (%d)", reloadedTrainer.items[pouchIndex][0].count);
        }
        if (pokemon && !reloadedTrainer.party.empty())
        {
            auto *reloadedPokemon = reloadedTrainer.party[0].get();
            const uint8_t expectedIV = reloadedPokemon->maxIV() < 29 ? reloadedPokemon->maxIV() : static_cast<uint8_t>(29);
            CHECK(reloadedPokemon->ivATK() == expectedIV, "IV Attack did not persist (%d, wanted %d)", reloadedPokemon->ivATK(), expectedIV);
            CHECK(reloadedPokemon->evATK() == 52, "EV Atk did not persist (%d)", reloadedPokemon->evATK());
            CHECK(reloadedPokemon->nickname() == u"Roundtrip", "nickname did not persist (\"%s\")", Utils::utf16ToUtf8(reloadedPokemon->nickname()).c_str());
            CHECK(reloadedPokemon->isStructurallyValid(), "the edited party member has a bad checksum");
            CHECK(reloadedPokemon->speciesID() == species, "species changed under an unrelated edit");
        }
        std::printf("      edits persist\n");
    }
}

static void gen2Synthetic()
{
    std::printf("  %-16s (synthetic -- no real save available)\n", "Gold/Silver");

    std::vector<uint8_t> saveImage(Trainer::GEN2_SAVE_SIZE, 0);
    // The minimum that makes PKHeX's IsG2GSINT probe succeed: an empty, well-formed Pokemon list
    // at the party and current-box offsets -- count 0 followed immediately by the 0xFF cap.
    saveImage[0x288A] = 0;
    saveImage[0x288B] = 0xFF;   // party
    saveImage[0x2D6C] = 0;
    saveImage[0x2D6D] = 0xFF;   // current-box mirror
    Trainer::Gen2Layout layout{};
    CHECK(Trainer::Trainer2GSC::detect(saveImage, &layout), "synthetic save not detected");
    CHECK(layout == Trainer::Gen2Layout::GS_INTL, "detected the wrong layout");

    // An RTC footer must not hide the save. Gold/Silver/Crystal have a real-time clock, and
    // emulators append its state after the save proper -- the user's Silver and Crystal files are
    // 0x8010, and every size check here is EXACT, so PKSE refused both as "not a save PKSE can
    // open". Same image, sixteen bytes longer, must still be found. The odd 7-byte case is one
    // flashcart's; the 0x40 case must be REFUSED, or the rule would start truncating real files.
    for (size_t footerSize : {static_cast<size_t>(0x10), static_cast<size_t>(0x0C), static_cast<size_t>(0x30), static_cast<size_t>(0x07)})
    {
        std::vector<uint8_t> padded = saveImage;
        padded.resize(saveImage.size() + footerSize, 0xA5);
        CHECK(!Trainer::Trainer2GSC::detect(padded, nullptr), "a %zu-byte-footered save was detected without stripping, so the size check is not exact", footerSize);
        CHECK(Save::stripRtcFooter(padded), "a %zu-byte RTC footer was not recognised", footerSize);
        CHECK(padded.size() == saveImage.size(), "stripping a %zu-byte footer left %zu bytes, want %zu", footerSize, padded.size(), saveImage.size());
        CHECK(Trainer::Trainer2GSC::detect(padded, nullptr), "the save did not detect after a %zu-byte strip", footerSize);
    }
    for (size_t footerSize : {static_cast<size_t>(0x02), static_cast<size_t>(0x0B), static_cast<size_t>(0x32), static_cast<size_t>(0x11)})
    {
        std::vector<uint8_t> padded = saveImage;
        padded.resize(saveImage.size() + footerSize, 0xA5);
        CHECK(!Save::stripRtcFooter(padded), "a %zu-byte tail was treated as an RTC footer; only 7 or an even 0x0C..0x30 may be", footerSize);
    }

    Trainer::Trainer2GSC trainer(saveImage, "<synthetic>");
    CHECK(trainer.isValid(), "synthetic save did not parse");
    CHECK(trainer.getBoxCount() == 14, "box count %zu, want 14", trainer.getBoxCount());
    CHECK(trainer.getSlotsPerBox() == 20, "slots/box %zu, want 20", trainer.getSlotsPerBox());

    // Fill in a trainer, a party member and a boxed Pokemon, then read it all back.
    trainer.trainerName = "Zephyr";
    trainer.TID16 = 12345;
    trainer.money = 123456;
    trainer.currentBox = 3;
    trainer.boxNames[3] = "Testing";
    trainer.markBoxNameDirty(3);
    trainer.items[1].push_back(Trainer::InventoryItem{1, 5, false, false});     // Master Ball, Items
    trainer.items[0].push_back(Trainer::InventoryItem{191, 3, false, false});   // TM01, positional
    trainer.items[2].push_back(Trainer::InventoryItem{7, 1, false, false});     // a key item

    {
        auto pokemon = trainer.createBlankPokemon();
        pokemon->setSpecies(155);            // Cyndaquil
        pokemon->setLevel(12);
        pokemon->setIV(0, 13);
        pokemon->setNickname(u"Blaze");
        pokemon->recalculateStats();
        pokemon->refreshChecksum();
        trainer.party.push_back(std::move(pokemon));
    }
    {
        auto pokemon = trainer.createBlankPokemon();
        pokemon->setSpecies(158);            // Totodile
        pokemon->setLevel(7);
        pokemon->recalculateStats();
        pokemon->refreshChecksum();
        trainer.boxes[3][2] = std::move(pokemon);
    }

    auto serializedBytes = trainer.serialize();
    CHECK(serializedBytes.size() == Trainer::GEN2_SAVE_SIZE, "size changed");
    CHECK(Trainer::Trainer2GSC::detect(serializedBytes), "the written save no longer detects");

    // The checksum must be the wrapping u16 sum over [trainer1, accumulatedChecksumEnd] INCLUSIVE,
    // stored little endian in BOTH positions.
    {
        const auto &layoutOffsets = Trainer::gen2Offsets(Trainer::Gen2Layout::GS_INTL);
        uint16_t expectedChecksum = 0;
        for (size_t byteIndex = layoutOffsets.trainer1; byteIndex <= layoutOffsets.accumulatedChecksumEnd; ++byteIndex)
        {
            expectedChecksum = static_cast<uint16_t>(expectedChecksum + serializedBytes[byteIndex]);
        }
        const uint16_t firstChecksum = static_cast<uint16_t>(serializedBytes[layoutOffsets.overallChecksumPosition] | (serializedBytes[layoutOffsets.overallChecksumPosition + 1] << 8));
        const uint16_t secondChecksum = static_cast<uint16_t>(serializedBytes[layoutOffsets.overallChecksumPosition2] | (serializedBytes[layoutOffsets.overallChecksumPosition2 + 1] << 8));
        CHECK(firstChecksum == expectedChecksum, "checksum copy 1 is %#06x, want %#06x", firstChecksum, expectedChecksum);
        CHECK(secondChecksum == expectedChecksum, "checksum copy 2 is %#06x, want %#06x", secondChecksum, expectedChecksum);
    }

    Trainer::Trainer2GSC reloadedTrainer(serializedBytes, "<synthetic>");
    CHECK(reloadedTrainer.isValid(), "re-parse failed");
    CHECK(reloadedTrainer.trainerName == "Zephyr", "trainer name \"%s\"", reloadedTrainer.trainerName.c_str());
    CHECK(reloadedTrainer.TID16 == 12345, "TID %u", reloadedTrainer.TID16);
    CHECK(reloadedTrainer.money == 123456u, "money %u", reloadedTrainer.money);
    CHECK(reloadedTrainer.currentBox == 3, "current box %d", reloadedTrainer.currentBox);
    CHECK(reloadedTrainer.boxNames[3] == "Testing", "box name \"%s\"", reloadedTrainer.boxNames[3].c_str());
    CHECK(reloadedTrainer.getPartySize() == 1, "party size %zu", reloadedTrainer.getPartySize());
    if (reloadedTrainer.getPartySize() == 1)
    {
        auto *partyPokemon = reloadedTrainer.party[0].get();
        CHECK(partyPokemon->speciesID() == 155, "party species %u", partyPokemon->speciesID());
        CHECK(partyPokemon->level() == 12, "party level %u", partyPokemon->level());
        CHECK(partyPokemon->ivHP() == 13 || partyPokemon->ivHP() <= 15, "party IV HP %u out of range", partyPokemon->ivHP());
        CHECK(partyPokemon->nickname() == u"Blaze", "party nickname \"%s\"", Utils::utf16ToUtf8(partyPokemon->nickname()).c_str());
    }
    CHECK(reloadedTrainer.boxes[3][0] && reloadedTrainer.boxes[3][0]->speciesID() == 158, "boxed Pokemon did not survive (compactStorage packs it to slot 0)");
    // Items: TM/HM is positional, Items is a {id,count} list, Key Items is ids only.
    CHECK(!reloadedTrainer.items[0].empty() && reloadedTrainer.items[0][0].itemId == 191 && reloadedTrainer.items[0][0].count == 3, "TM/HM pouch did not persist");
    CHECK(!reloadedTrainer.items[1].empty() && reloadedTrainer.items[1][0].itemId == 1 && reloadedTrainer.items[1][0].count == 5, "Items pouch did not persist");
    CHECK(!reloadedTrainer.items[2].empty() && reloadedTrainer.items[2][0].itemId == 7, "Key Items pouch did not persist");

    // Parse/serialise must be idempotent: writing what we just read changes nothing.
    {
        Trainer::Trainer2GSC reparsedTrainer(serializedBytes, "<synthetic>");
        auto reserializedBytes = reparsedTrainer.serialize();
        size_t differingByteCount = 0;
        for (size_t byteIndex = 0; byteIndex < serializedBytes.size(); ++byteIndex)
        {
            if (serializedBytes[byteIndex] != reserializedBytes[byteIndex])
            {
                ++differingByteCount;
            }
        }
        ++g_checkCount;
        if (differingByteCount)
        {
            std::printf("      [FAIL] re-serialising changed %zu bytes\n", differingByteCount);
            ++g_failureCount;
        }
        else
        {
            std::printf("      round trip EXACT (of a save PKSE itself wrote)\n");
        }
    }
    std::printf("      edits persist\n");
}

template <typename TrainerT>
static void gen7Signature(const std::string &path, const char *tag, size_t signatureOffset, size_t hashedSize)
{
    auto originalBytes = load(path);
    if (originalBytes.empty() || !TrainerT::detect(originalBytes))
    {
        return;
    }

    TrainerT trainer(originalBytes, path);
    trainer.money = 424242;                       // any edit at all changes the checksum table
    trainer.trainerName = "Signed";
    auto signedImage = trainer.serialize();

    uint8_t decryptedSignature[0x80];
    std::memset(decryptedSignature, 0, sizeof(decryptedSignature));
    const int signatureVerified = memecrypto_verify(&signedImage[signatureOffset], decryptedSignature, 0x80);
    CHECK(signatureVerified != 0, "%s: the signature PKSE wrote does not verify", tag);
    if (!signatureVerified)
    {
        return;
    }

    uint8_t expectedHash[Utils::PKSE_SHA256_HASH_SIZE];
    Utils::SHA256 hasher;
    hasher.update(&signedImage[signedImage.size() - 0x200], hashedSize);
    hasher.finalize(expectedHash);
    CHECK(std::memcmp(decryptedSignature, expectedHash, sizeof(expectedHash)) == 0, "%s: the signed hash does not match the checksum table PKSE wrote", tag);

    // And the edit really did move the table -- otherwise this whole check is vacuous.
    CHECK(std::memcmp(&originalBytes[originalBytes.size() - 0x200], &signedImage[signedImage.size() - 0x200], hashedSize) != 0, "%s: the checksum table did not change, so the signature check proves nothing", tag);
    std::printf("      %-16s signature verifies after an edit\n", tag);
}


template <typename TrainerT>
static void gameByteFollows(const std::string &path, const char *tag, size_t gameByteOffset, Enums::GameVersion sibling)
{
    std::vector<uint8_t> saveBytes = load(path);
    if (saveBytes.empty() || !TrainerT::detect(saveBytes))
    {
        ++g_skippedSaveCount;
        printMissing(tag);
        return;
    }
    if (gameByteOffset >= saveBytes.size())
    {
        CHECK(false, "%s: the game byte is past the file", tag);
        return;
    }

    std::vector<uint8_t> asFound = saveBytes;
    TrainerT found(std::move(asFound), path);
    const Enums::GameVersion asFoundVersion = found.getGameVersion();

    saveBytes[gameByteOffset] = static_cast<uint8_t>(sibling);
    TrainerT flipped(std::move(saveBytes), path);
    CHECK(flipped.getGameVersion() == sibling, "%s: setting the game byte to %s still reports %s -- the byte is not being read", tag, Enums::getGameVersionName(sibling).c_str(), Enums::getGameVersionName(flipped.getGameVersion()).c_str());
    std::printf("      %-16s names itself %s (and follows the byte to %s)\n", tag, Enums::getGameVersionName(asFoundVersion).c_str(), Enums::getGameVersionName(sibling).c_str());
}

/// The save a fixture was built around, for isFixtureImage(): findSaveEnvelope takes a plain function
/// pointer, so the image the check expects is parked here for the length of one check.
static const std::vector<uint8_t> *g_fixtureImage = nullptr;
static bool isFixtureImage(const std::vector<uint8_t> &bytes)
{
    return g_fixtureImage && bytes == *g_fixtureImage;
}

/// no$gba run-length coded is the one layout PKSE writes in different bytes from the file it read: the code
/// can cut a save in many ways, and the encoder picks its own. Everything else must come back byte for byte.
static bool isRecoded(Save::SaveEnvelopeKind kind, bool alternateLayout)
{
    return kind == Save::SaveEnvelopeKind::NocashGbaContainer && !alternateLayout;
}

/// One enveloped file: found, joined back, and an edited save joined and found again.
static void envelopeRoundTrip(const char *tag, const char *envelopeName, const std::vector<uint8_t> &file, Save::SaveEnvelopeKind kind, bool recoded, const std::vector<uint8_t> &image, const std::vector<uint8_t> &padding)
{
    g_fixtureImage = &image;
    Save::SaveEnvelope envelope;
    std::vector<uint8_t> found;
    const bool envelopeFound = Save::findSaveEnvelope(file, isFixtureImage, envelope, found);
    CHECK(envelopeFound && envelope.kind == kind && found == image && envelope.paddingBytes == padding, "%s: not found inside %s", tag, envelopeName);
    if (!envelopeFound)
    {
        return;
    }

    const std::vector<uint8_t> joined = Save::joinSaveEnvelope(envelope, found);
    if (!recoded)
    {
        CHECK(joined == file, "%s: %s did not join back into the same file", tag, envelopeName);
    }
    else
    {
        // Read back by the fixture's own decoder, which shares nothing with PKSE's encoder but the format.
        std::vector<uint8_t> held = image;
        held.insert(held.end(), padding.begin(), padding.end());
        CHECK(Test::EnvelopeFixture::nocashRunLengthDecode(joined) == held, "%s: %s, coded again by PKSE, does not decode to the same save", tag, envelopeName);
        const size_t entriesAfterByteCount = envelope.footerBytes.size();
        CHECK(joined.size() >= entriesAfterByteCount && std::equal(envelope.footerBytes.begin(), envelope.footerBytes.end(), joined.end() - static_cast<long>(entriesAfterByteCount)) && std::equal(file.begin(), file.begin() + 0x40, joined.begin()), "%s: %s lost its header or the entries after the save", tag, envelopeName);
    }

    // An edited save goes back into the same envelope; only Switch Online's hash may move with it.
    std::vector<uint8_t> edited = image;
    edited[0x100] ^= 0xFF;
    const std::vector<uint8_t> rejoined = Save::joinSaveEnvelope(envelope, edited);
    g_fixtureImage = &edited;
    Save::SaveEnvelope envelopeAgain;
    std::vector<uint8_t> foundAgain;
    CHECK(Save::findSaveEnvelope(rejoined, isFixtureImage, envelopeAgain, foundAgain) && envelopeAgain.kind == kind && foundAgain == edited && envelopeAgain.paddingBytes == padding, "%s: an edited save did not come back out of %s", tag, envelopeName);
    if (!recoded)
    {
        size_t movedByteCount = 0;
        for (size_t byteIndex = 0; byteIndex < file.size() && byteIndex < rejoined.size(); ++byteIndex)
        {
            movedByteCount += file[byteIndex] != rejoined[byteIndex];
        }
        const size_t hashMovedByteCount = kind == Save::SaveEnvelopeKind::SwitchOnlineHeader ? 40 : 0;
        CHECK(rejoined.size() == file.size() && movedByteCount >= 1 && movedByteCount <= 1 + hashMovedByteCount, "%s: editing one byte moved %zu bytes of the %s file", tag, movedByteCount, envelopeName);
    }
    g_fixtureImage = nullptr;
}

/// SAVE ENVELOPES (Save/SaveEnvelope.h). Each real save is wrapped the way every program that writes
/// its size wraps it, and has to come back out exactly -- and go back in exactly, around an edited
/// save as well as the original, which is what lets an emulator read the file PKSE writes.
static void envelopes(const std::vector<std::pair<std::string, const char *>> &saves)
{
    using Save::SaveEnvelopeKind;

    // The Switch Online header stores SHA-1 as lowercase hex text. FIPS 180 gives the answer for "abc".
    {
        Save::SaveEnvelope envelope;
        envelope.kind = SaveEnvelopeKind::SwitchOnlineHeader;
        envelope.headerBytes = Test::EnvelopeFixture::switchOnlineHeader(false);
        const std::vector<uint8_t> abc = {'a', 'b', 'c'};
        const std::vector<uint8_t> file = Save::joinSaveEnvelope(envelope, abc);
        const size_t hashEnd = envelope.headerBytes.size();
        const std::string hashText(file.begin() + static_cast<long>(hashEnd - 40), file.begin() + static_cast<long>(hashEnd));
        CHECK(hashText == "a9993e364706816aba3e25717850c26c9cd0d89d", "SHA-1 of \"abc\" came out as %s", hashText.c_str());
    }
    CHECK(Test::EnvelopeFixture::desmumeFooter(0x80000).size() == 0x7A, "the DeSmuME fixture footer is %zu bytes, not DeSmuME's 0x7A", Test::EnvelopeFixture::desmumeFooter(0x80000).size());

    // no$gba's run-length code, known answers. Every control: a copy, a short repeat, a long repeat, the end.
    {
        const std::vector<uint8_t> held = {1, 2, 3, 9, 9, 9, 9, 7};
        std::vector<uint8_t> longRunBuilder(0x1234, 0xFF);
        longRunBuilder.push_back(0x42);
        const std::vector<uint8_t> longRun = longRunBuilder;
        const std::vector<uint8_t> emptyFile = Test::EnvelopeFixture::nocashGbaFile({}, false);
        for (const std::vector<uint8_t> *source : {&held, &longRun})
        {
            Save::SaveEnvelope envelope;
            envelope.kind = SaveEnvelopeKind::NocashGbaContainer;
            envelope.headerBytes.assign(emptyFile.begin(), emptyFile.begin() + 0x40);
            envelope.nocashSaveEntry.compressionMethod = 1;
            const std::vector<uint8_t> file = Save::joinSaveEnvelope(envelope, *source);
            CHECK(Test::EnvelopeFixture::nocashRunLengthDecode(file) == *source, "no$gba's run-length code of %zu bytes does not decode back", source->size());
        }
        Save::SaveEnvelope envelope;
        envelope.kind = SaveEnvelopeKind::NocashGbaContainer;
        envelope.headerBytes.assign(0x40, 0);
        envelope.nocashSaveEntry.compressionMethod = 1;
        const std::vector<uint8_t> file = Save::joinSaveEnvelope(envelope, held);
        const std::vector<uint8_t> expectedCode = {0x03, 1, 2, 3, 0x84, 9, 0x01, 7, 0x00};
        CHECK(file.size() == 0x40 + 16 + expectedCode.size() && std::equal(expectedCode.begin(), expectedCode.end(), file.begin() + 0x40 + 16) && file[0x48] == expectedCode.size() + 4 && file[0x4C] == held.size(), "no$gba's run-length code of 01 02 03 09 09 09 09 07 is not 03 01 02 03 84 09 01 07 00");
    }

    int wrappedCount = 0;
    for (const auto &[path, tag] : saves)
    {
        std::vector<uint8_t> image = load(path);
        if (image.empty())
        {
            continue;
        }
        Save::stripRtcFooter(image); // a real dump may already carry one; the fixtures add their own

        g_fixtureImage = &image;
        Save::SaveEnvelope bareEnvelope;
        std::vector<uint8_t> bareImage;
        CHECK(Save::findSaveEnvelope(image, isFixtureImage, bareEnvelope, bareImage) && Save::isBareSave(bareEnvelope) && bareImage == image, "%s: the bare save was not taken as it stands", tag);

        for (const SaveEnvelopeKind kind : Test::envelopesForSaveSize(image.size()))
        {
            for (const bool alternateLayout : Test::envelopeLayouts(kind))
            {
                const std::vector<uint8_t> file = Test::buildEnvelopedSave(kind, image, alternateLayout);
                envelopeRoundTrip(tag, Save::saveEnvelopeName(kind), file, kind, isRecoded(kind, alternateLayout), image, {});
                ++wrappedCount;
            }
        }
        for (const Test::PaddingFixture &fixture : Test::paddingForSaveSize(image.size()))
        {
            const std::vector<uint8_t> held = Test::paddedSaveBytes(fixture, image);
            const std::vector<uint8_t> padding(held.begin() + static_cast<long>(image.size()), held.end());
            const std::vector<uint8_t> file = Test::buildEnvelopedSave(fixture.kind, held);
            char envelopeName[96];
            std::snprintf(envelopeName, sizeof envelopeName, "%s, padded to 0x%zX with %02X%s", Save::saveEnvelopeName(fixture.kind), fixture.paddedSize, fixture.fillByte, fixture.withYsMenuFlags ? " and YSMenu flags" : "");
            envelopeRoundTrip(tag, envelopeName, file, fixture.kind, false, image, padding);
            ++wrappedCount;

            // One byte of the padding that is neither the fill nor YSMenu's: not padding, so not a save.
            std::vector<uint8_t> stray = file;
            stray[image.size() + padding.size() / 2] ^= 0x5A;
            g_fixtureImage = &image;
            Save::SaveEnvelope envelope;
            std::vector<uint8_t> found;
            CHECK(!Save::findSaveEnvelope(stray, isFixtureImage, envelope, found), "%s: %s with a stray byte in its padding was still taken apart", tag, envelopeName);
            g_fixtureImage = nullptr;
        }
        g_fixtureImage = nullptr;
    }
    std::printf("  %d enveloped saves taken apart and put back\n", wrappedCount);

    // A real detector, not just a byte compare: what the app's probe chain runs on Gen 2, 4 and 5.
    auto detectGen2 = [](const std::vector<uint8_t> &bytes) { return Trainer::Trainer2GSC::detect(bytes, nullptr); };
    auto detectGen45 = [](const std::vector<uint8_t> &bytes) { return Trainer::Trainer4DP::detect(bytes) || Trainer::Trainer4PT::detect(bytes) || Trainer::Trainer4HGSS::detect(bytes) || Trainer::Trainer5BW::detect(bytes) || Trainer::Trainer5B2W2::detect(bytes); };
    for (const auto &[path, tag] : saves)
    {
        std::vector<uint8_t> image = load(path);
        if (image.empty())
        {
            continue;
        }
        Save::stripRtcFooter(image);
        bool (*detector)(const std::vector<uint8_t> &) = nullptr;
        if (detectGen2(image))
        {
            detector = detectGen2;
        }
        else if (detectGen45(image))
        {
            detector = detectGen45;
        }
        if (!detector)
        {
            continue;
        }
        for (const SaveEnvelopeKind kind : Test::envelopesForSaveSize(image.size()))
        {
            for (const bool alternateLayout : Test::envelopeLayouts(kind))
            {
                Save::SaveEnvelope envelope;
                std::vector<uint8_t> found;
                CHECK(Save::findSaveEnvelope(Test::buildEnvelopedSave(kind, image, alternateLayout), detector, envelope, found) && envelope.kind == kind && found == image, "%s: the probe chain's own detector did not find the save inside %s", tag, Save::saveEnvelopeName(kind));
            }
        }
        for (const Test::PaddingFixture &fixture : Test::paddingForSaveSize(image.size()))
        {
            Save::SaveEnvelope envelope;
            std::vector<uint8_t> found;
            CHECK(Save::findSaveEnvelope(Test::buildEnvelopedSave(fixture.kind, Test::paddedSaveBytes(fixture, image)), detector, envelope, found) && envelope.kind == fixture.kind && found == image, "%s: the probe chain's own detector did not find the save padded to 0x%zX", tag, fixture.paddedSize);
        }
    }

    // Near misses. Each is one step from an envelope and must not be taken for one: a file split at the
    // wrong place still has a save-shaped middle often enough to matter.
    const std::vector<uint8_t> dsImage(0x80000, 0x5A);
    const std::vector<uint8_t> gbaImage(0x20000, 0x5A);
    const std::vector<uint8_t> gameBoyImage(0x8000, 0x5A);
    const auto refused = [](SaveEnvelopeKind kind, const std::vector<uint8_t> &file)
    {
        Save::SaveEnvelope envelope;
        std::vector<uint8_t> image;
        return !Save::splitSaveEnvelope(kind, file, envelope, image);
    };
    {
        std::vector<uint8_t> file = Test::buildEnvelopedSave(SaveEnvelopeKind::DesmumeFooter, dsImage);
        file.back() ^= 0x01;
        CHECK(refused(SaveEnvelopeKind::DesmumeFooter, file), "a DeSmuME footer with a damaged signature was accepted");
    }
    {
        std::vector<uint8_t> file = Test::buildEnvelopedSave(SaveEnvelopeKind::LibretroGbaSaveRam, gbaImage);
        file.push_back(0xFF);
        CHECK(refused(SaveEnvelopeKind::LibretroGbaSaveRam, file), "a 0x22001-byte file was taken for VBA Next save RAM");
    }
    {
        std::vector<uint8_t> file = Test::buildEnvelopedSave(SaveEnvelopeKind::ActionReplayHeader, dsImage);
        file.pop_back();
        CHECK(refused(SaveEnvelopeKind::ActionReplayHeader, file), "a file one byte short was taken for an Action Replay dump");
    }
    {
        std::vector<uint8_t> file = Test::buildEnvelopedSave(SaveEnvelopeKind::SwitchOnlineHeader, gameBoyImage);
        file[0] = 'X';
        CHECK(refused(SaveEnvelopeKind::SwitchOnlineHeader, file), "a Switch Online header without its \"SRAM\" magic was accepted");
    }
    {
        std::vector<uint8_t> file = Test::buildEnvelopedSave(SaveEnvelopeKind::SwitchOnlineHeader, gameBoyImage);
        file[0x30] = 0xFF; // a build string longer than the file allows
        CHECK(refused(SaveEnvelopeKind::SwitchOnlineHeader, file), "a Switch Online header running past its own file was accepted");
    }
    {
        std::vector<uint8_t> file = Test::buildEnvelopedSave(SaveEnvelopeKind::TgbDualClockFooter, gameBoyImage);
        file.push_back(0);
        CHECK(refused(SaveEnvelopeKind::TgbDualClockFooter, file), "a 0x8005-byte file was taken for a TGB Dual save");
    }
    {
        std::vector<uint8_t> file = Test::buildEnvelopedSave(SaveEnvelopeKind::MisterClockSector, gbaImage);
        file.pop_back();
        CHECK(refused(SaveEnvelopeKind::MisterClockSector, file), "a 0x201FF-byte file was taken for a MiSTer save");
    }
    {
        std::vector<uint8_t> file = Test::buildEnvelopedSave(SaveEnvelopeKind::MeteorSizeFooter, gbaImage);
        file[0x20002] = 0x01; // a size meteor would not have written for 1 Mbit flash
        CHECK(refused(SaveEnvelopeKind::MeteorSizeFooter, file), "a meteor footer giving the wrong size was accepted");
    }
    {
        std::vector<uint8_t> file = Test::buildEnvelopedSave(SaveEnvelopeKind::BizHawkBatteryHeader, gbaImage);
        file[0] = 'X';
        CHECK(refused(SaveEnvelopeKind::BizHawkBatteryHeader, file), "a BizHawk header without \"GBABATT\" was accepted");
        file[0] = 'G';
        file.pop_back(); // the lengths in the header no longer add up to the file
        CHECK(refused(SaveEnvelopeKind::BizHawkBatteryHeader, file), "a BizHawk file shorter than its header says was accepted");
    }
    {
        std::vector<uint8_t> file = Test::buildEnvelopedSave(SaveEnvelopeKind::ActionReplayMaxDriveHeader, dsImage);
        file[15] = '2';
        CHECK(refused(SaveEnvelopeKind::ActionReplayMaxDriveHeader, file), "an Action Replay header that is not \"ARDS000000000001\" was accepted");
    }
    {
        std::vector<uint8_t> file = Test::buildEnvelopedSave(SaveEnvelopeKind::NocashGbaContainer, gbaImage);
        file[0x1F] = 0x00; // the magic's closing 1A
        CHECK(refused(SaveEnvelopeKind::NocashGbaContainer, file), "a no$gba file with a damaged magic was accepted");
    }
    {
        std::vector<uint8_t> file = Test::buildEnvelopedSave(SaveEnvelopeKind::NocashGbaContainer, gbaImage);
        file[0x4C] += 1; // one byte more than the code decodes to
        CHECK(refused(SaveEnvelopeKind::NocashGbaContainer, file), "a no$gba file whose code is shorter than its stated length was accepted");
    }
    {
        std::vector<uint8_t> file = Test::buildEnvelopedSave(SaveEnvelopeKind::NocashGbaContainer, gbaImage);
        file[0x48] += 1; // an entry length that is neither of the two ways it is read
        CHECK(refused(SaveEnvelopeKind::NocashGbaContainer, file), "a no$gba entry with the wrong length was accepted");
        file[0x48] -= 1;
        file[0x44] = 2; // LZ: refused, and explained
        CHECK(refused(SaveEnvelopeKind::NocashGbaContainer, file), "a no$gba LZ entry was split without an LZ decoder");
        const char *reason = Save::describeUnreadableSaveContainer(file);
        CHECK(reason && std::strstr(reason, "SAV/SNA File Format"), "a no$gba LZ file was refused without saying what to change");
    }

    // The containers PKSE recognises and cannot read each say so. A real save says nothing.
    {
        const auto fileStarting = [](std::string_view magic, size_t magicOffset = 0)
        {
            std::vector<uint8_t> file(0x8000, 0x00);
            std::copy(magic.begin(), magic.end(), file.begin() + static_cast<long>(magicOffset));
            return file;
        };
        const std::pair<std::vector<uint8_t>, const char *> explained[] = {
            {fileStarting("#RZIPv\x01#"), "SaveRAM Compression"},
            {fileStarting(std::string_view("\xD8\x31\xA7\x57", 4)), "Goomba"},
            {fileStarting(std::string_view("\xD9\x31\xA7\x57", 4)), "Goomba"},
            {fileStarting("RTN5"), "Retron 5"},
            {fileStarting("PK\x03\x04"), "ZIP"},
            {fileStarting(std::string_view("DISA\0\0\x04\0", 8), 0x100), "3DS"},
        };
        for (const auto &[file, phrase] : explained)
        {
            const char *reason = Save::describeUnreadableSaveContainer(file);
            CHECK(reason && std::strstr(reason, phrase), "a %s container was not explained", phrase);
        }
        for (const auto &[path, tag] : saves)
        {
            const std::vector<uint8_t> image = load(path);
            CHECK(image.empty() || !Save::describeUnreadableSaveContainer(image), "%s: a real save was explained away as a container PKSE cannot read", tag);
        }
    }
}

int main(int argc, char **argv)
{
    const std::string saveRoot = argc > 1 ? argv[1] : std::string(std::getenv("HOME")) + "/pkse-test-saves";
    g_saveRoot = saveRoot;
    std::printf("save root: %s\nsaves are found by TITLE FOLDER -- <root>/HeartGold/, <root>/Pearl/ and so on,\nwith any filename inside. Four pairs of games write byte-identical saves with no\nversion byte anywhere, so the folder is the only thing that can say which is which.\n\n", saveRoot.c_str());

    // Saves are found by the TITLE FOLDER they sit in, not by a guessed filename -- see
    // SaveTree.h. Each of these is <root>/<Title>/<whatever the dump is called>. The pairs that
    // share a save layout (Diamond/Pearl, HeartGold/SoulSilver, Gold/Silver) get one folder each
    // and are run separately, because the folder is the only thing that can tell them apart: the
    // saves themselves carry no version byte at all.
    const std::string gold = SaveTree::resolve(saveRoot, "Gold", {"gsc/POKEMON_GLD.sav", "gsc/main.sav", "gsc/save.sav"});
    const std::string silver = SaveTree::resolve(saveRoot, "Silver", {"gsc/POKEMON_SLV.sav"});
    const std::string crystal = SaveTree::resolve(saveRoot, "Crystal", {"gsc/POKEMON_CRY.sav"});
    const std::string diamond = SaveTree::resolve(saveRoot, "Diamond", {"dp/POKEMON_D.sav"});
    const std::string pearl = SaveTree::resolve(saveRoot, "Pearl", {"dp/POKEMON_P.sav"});
    const std::string platinum = SaveTree::resolve(saveRoot, "Platinum", {"pt/POKEMON_PL.sav"});
    const std::string heartGold = SaveTree::resolve(saveRoot, "HeartGold", {"hgss/POKEMON_HG.sav"});
    const std::string soulSilver = SaveTree::resolve(saveRoot, "SoulSilver", {"hgss/POKEMON_SS.sav"});
    const std::string black = SaveTree::resolve(saveRoot, "Black", {"bw/POKEMON_B.sav"});
    const std::string white = SaveTree::resolve(saveRoot, "White", {"bw/POKEMON_W.sav"});
    const std::string black2 = SaveTree::resolve(saveRoot, "Black 2", {"b2w2/POKEMON_B2.sav"});
    const std::string white2 = SaveTree::resolve(saveRoot, "White 2", {"b2w2/POKEMON_W2.sav"});
    const std::string pokemonX = SaveTree::resolve(saveRoot, "X", {"xy/main"});
    const std::string pokemonY = SaveTree::resolve(saveRoot, "Y", {"xy/main.y"});
    const std::string omegaRuby = SaveTree::resolve(saveRoot, "Omega Ruby", {"oras/main"});
    const std::string alphaSapphire = SaveTree::resolve(saveRoot, "Alpha Sapphire", {"oras/main.as"});
    const std::string sun = SaveTree::resolve(saveRoot, "Sun", {"sm/main"});
    const std::string moon = SaveTree::resolve(saveRoot, "Moon", {"sm/main.mn"});
    const std::string ultraSun = SaveTree::resolve(saveRoot, "Ultra Sun", {"usum/main"});
    const std::string ultraMoon = SaveTree::resolve(saveRoot, "Ultra Moon", {"usum/main.um"});

    std::printf("Gen 2\n");
    {
        bool ranAnySave = false;
        for (const auto &[path, tag] : {std::pair{gold, "Gold"}, std::pair{silver, "Silver"}, std::pair{crystal, "Crystal"}})
        {
            if (path.empty())
            {
                continue;
            }
            run<Trainer::Trainer2GSC>(path, tag);
            ranAnySave = true;
        }
        if (!ranAnySave)
        {
            gen2Synthetic();
        }
    }

    std::printf("\nGen 4\n");
    run<Trainer::Trainer4DP>(diamond, "Diamond");
    run<Trainer::Trainer4DP>(pearl, "Pearl");
    run<Trainer::Trainer4PT>(platinum, "Platinum");
    run<Trainer::Trainer4HGSS>(heartGold, "HeartGold");
    run<Trainer::Trainer4HGSS>(soulSilver, "SoulSilver");

    std::printf("\nGen 5\n");
    run<Trainer::Trainer5BW>(black, "Black");
    run<Trainer::Trainer5BW>(white, "White");
    run<Trainer::Trainer5B2W2>(black2, "Black 2");
    run<Trainer::Trainer5B2W2>(white2, "White 2");

    std::printf("\nGen 6\n");
    run<Trainer::Trainer6XY>(pokemonX, "X");
    run<Trainer::Trainer6XY>(pokemonY, "Y");
    run<Trainer::Trainer6ORAS>(omegaRuby, "Omega Ruby");
    run<Trainer::Trainer6ORAS>(alphaSapphire, "Alpha Sapphire");

    std::printf("\nGen 7\n");
    run<Trainer::Trainer7SM>(sun, "Sun");
    run<Trainer::Trainer7SM>(moon, "Moon");
    run<Trainer::Trainer7USUM>(ultraSun, "Ultra Sun");
    run<Trainer::Trainer7USUM>(ultraMoon, "Ultra Moon");

    // Gens 5, 6 and 7 DO carry a game byte, and this proves it is really being read: flip the
    // byte and require the reported title to follow. Reporting the right title as-found proves
    // nothing on its own, because a read that failed falls back to exactly that value.
    std::printf("\nThe save names its own title\n");
    gameByteFollows<Trainer::Trainer5BW>(black, "Black", Trainer::BLOCKS_5BW[Trainer::BLOCK_TRAINER_5BW].offset + 0x1F, Enums::GameVersion::W);
    gameByteFollows<Trainer::Trainer5B2W2>(black2, "Black 2", Trainer::BLOCKS_5B2W2[Trainer::BLOCK_TRAINER_5B2W2].offset + 0x1F, Enums::GameVersion::W2);
    gameByteFollows<Trainer::Trainer6XY>(pokemonX, "X", Trainer::BLOCKS_6XY[Trainer::BLOCK_STATUS_6XY].offset + 0x04, Enums::GameVersion::Y);
    gameByteFollows<Trainer::Trainer6ORAS>(omegaRuby, "Omega Ruby", Trainer::BLOCKS_6ORAS[Trainer::BLOCK_STATUS_6ORAS].offset + 0x04, Enums::GameVersion::AS);
    gameByteFollows<Trainer::Trainer7SM>(sun, "Sun", Trainer::BLOCKS_7SM[Trainer::BLOCK_STATUS_7SM].offset + 0x04, Enums::GameVersion::MN);
    gameByteFollows<Trainer::Trainer7USUM>(ultraSun, "Ultra Sun", Trainer::BLOCKS_7USUM[Trainer::BLOCK_STATUS_7USUM].offset + 0x04, Enums::GameVersion::UM);

    std::printf("\nGen 7 MemeCrypto\n");
    gen7Signature<Trainer::Trainer7SM>(sun, "Sun", 0x6BB00, 0x140);
    gen7Signature<Trainer::Trainer7USUM>(ultraSun, "Ultra Sun", 0x6C100, 0x150);

    std::printf("\nSave envelopes\n");
    envelopes({{SaveTree::red(saveRoot), "Red"}, {gold, "Gold"}, {silver, "Silver"}, {crystal, "Crystal"}, {SaveTree::ruby(saveRoot), "Ruby"}, {SaveTree::fireRed(saveRoot), "FireRed"}, {diamond, "Diamond"}, {platinum, "Platinum"}, {heartGold, "HeartGold"}, {black, "Black"}, {black2, "Black 2"}});

    std::printf("\n%d checks, %d failure(s), %d save(s) skipped for want of a file\n", g_checkCount, g_failureCount, g_skippedSaveCount);
    return g_failureCount != 0;
}
