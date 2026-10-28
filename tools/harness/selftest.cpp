/**
 * The on-console self test can only be run on a console, which makes the framework itself the one
 * piece of PKSE that would otherwise ship untested. Six of its nine suites need neither the Switch
 * SDK nor the SD card, so they compile and run here against exactly the code the .nro will run:
 *
 *   tables / gentables   pure data -- names, the personal table, experience curves, per-generation
 *                        item pouches and learnsets, the Gen 1 tables. No save needed.
 *   entity / setters     every Pokemon in a real save, and a set/get sweep over a blank entity.
 *   legality             the analyzer over every stored Pokemon.
 *   conversion           the cross-generation matrix, from samples the entity sweep collected.
 *
 * The last four need a save, and they get REAL ones: point the harness at a directory laid out the
 * way tools/harness/roundtrip.cpp expects -- one folder per TITLE (FireRed/, Shield/, Violet/,
 * Legends Z-A/, Legends Arceus/), any filename inside; see tools/harness/SaveTree.h
 * and each present save becomes a target. Saves are opened READ-ONLY and never written -- the write
 * paths are the one thing here that still needs a console, along with ROMFS. The exception is the
 * box block of a Switch save, which is re-encoded in memory so the save suite's occupied-slot rule
 * runs against every save here (checkUnmodifiedBoxWrite); the file is still never touched. The bank
 * runs against a scratch bank in the work directory.
 *
 * That gives two things: a way to know the framework works before it reaches hardware, and a fast
 * loop for the parts most likely to change. It does NOT replace the on-console run -- the save
 * WRITE path, edit persistence and the ROMFS sprite sweep are hardware-only.
 *
 *   ./tools/harness/build-selftest.sh [report-path] [save-root]
 *
 * Exits non-zero if any check fails.
 */
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <sys/stat.h>

#include "SaveTree.h" // saves are found by their TITLE FOLDER, not by a guessed filename

#include "Test/TestSuite.h"
#include "Test/SCBlockRewrite.h"
#include "Enums/GameVersion.h"
#include "Encryption/Encryption.h"
#include "Trainer/Trainer3FRLG.h"
#include "Trainer/Trainer3RSE.h"
#include "Save/RtcFooter.h"
#include "Trainer/Trainer1RBY.h"
#include "Trainer/Trainer2GSC.h"
#include "Trainer/Trainer4DP.h"
#include "Trainer/Trainer4PT.h"
#include "Trainer/Trainer4HGSS.h"
#include "Trainer/Trainer5BW.h"
#include "Trainer/Trainer5B2W2.h"
#include "Trainer/Trainer6XY.h"
#include "Trainer/Trainer6ORAS.h"
#include "Trainer/Trainer7SM.h"
#include "Trainer/Trainer7LGPE.h"
#include "Trainer/Trainer7LGPE.h"
#include "Trainer/Trainer7USUM.h"
#include "Trainer/Trainer8LA.h"
#include "Trainer/Trainer8SWSH.h"
#include "Trainer/Trainer9LZA.h"
#include "Trainer/Trainer9SV.h"

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
    // Real, because PokemonFile::write reads its own file back to verify it before promoting it. The
    // app's version allocates with new[] and so does this one, so delete[] matches either way.
    uint8_t *readAllBytes(const char *path, size_t *outSize)
    {
        if (outSize)
        {
            *outSize = 0;
        }
        FILE *file = std::fopen(path, "rb");
        if (!file)
        {
            return nullptr;
        }
        std::fseek(file, 0, SEEK_END);
        const long fileSize = std::ftell(file);
        std::fseek(file, 0, SEEK_SET);
        uint8_t *fileBytes = new uint8_t[fileSize > 0 ? static_cast<size_t>(fileSize) : 1];
        const size_t readByteCount = (fileBytes && fileSize > 0) ? std::fread(fileBytes, 1, static_cast<size_t>(fileSize), file) : 0;
        std::fclose(file);
        if (!fileBytes || readByteCount != static_cast<size_t>(fileSize > 0 ? fileSize : 0))
        {
            delete[] fileBytes;
            return nullptr;
        }
        if (outSize)
        {
            *outSize = readByteCount;
        }
        return fileBytes;
    }
}

namespace
{

    /// Every save-format group, so the per-generation tables are swept for all of them rather than
    /// only for the ones this machine happens to have a save for. On hardware the same suite runs
    /// once per generation in the plan; here there is no such limit, so it runs for everything.
    /// The list itself is Test::ALL_SAVE_GROUPS -- shared with the shipped suites, so a new game
    /// cannot be added to one run and forgotten in the other.
    using Test::ALL_SAVE_GROUPS;

    /// The scratch directory a suite may write into, as the runner's work directory is on console.
    std::string g_workDirectory;

    void runSuite(Test::Report &report, const char *suiteName, const std::string &target, Enums::GameVersion group, Enums::GameVersion version, int (*plan)(Test::Context &), void (*run)(Test::Context &, int), Trainer::Trainer *trainer = nullptr)
    {
        Test::Context context;
        context.report = &report;
        context.suite = suiteName;
        context.target = target;
        context.group = group;
        context.version = version;
        context.trainer = trainer;
        context.workDirectory = g_workDirectory;
        report.setScope(target, suiteName);

        const int chunkCount = plan(context);
        std::printf("  %-10s %s  (%d chunk%s)\n", suiteName, target.c_str(), chunkCount, chunkCount == 1 ? "" : "s");
        for (int chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex)
        {
            report.setScope(target, suiteName);
            run(context, chunkIndex);
        }
    }

    std::vector<uint8_t> readFile(const std::string &filePath)
    {
        FILE *file = std::fopen(filePath.c_str(), "rb");
        if (!file)
        {
            return {};
        }
        std::fseek(file, 0, SEEK_END);
        const long fileSize = std::ftell(file);
        std::fseek(file, 0, SEEK_SET);
        std::vector<uint8_t> fileBytes(fileSize > 0 ? static_cast<size_t>(fileSize) : 0);
        if (!fileBytes.empty() && std::fread(fileBytes.data(), 1, fileBytes.size(), file) != fileBytes.size())
        {
            fileBytes.clear();
        }
        std::fclose(file);
        return fileBytes;
    }

    /// Run every suite that needs a loaded save, against one. The save is never written back --
    /// the write path is the one part of the "save" suite that genuinely needs a console, so it
    /// is not run here at all rather than half-run.
    void runTargetSuites(Test::Report &report, const std::string &target, Enums::GameVersion group, Trainer::Trainer &trainer)
    {
        // The trainer names its own title -- the same answer the on-console runner uses for a
        // loose save -- so the harness and the console bucket samples identically.
        const Enums::GameVersion version = trainer.getGameVersion();
        runSuite(report, "entity", target, group, version, Test::entityPlan, Test::entityRun, &trainer);
        runSuite(report, "setters", target, group, version, Test::settersPlan, Test::settersRun, &trainer);
        runSuite(report, "legality", target, group, version, Test::legalityPlan, Test::legalityRun, &trainer);
        runSuite(report, "pokemonfile", target, group, version, Test::pokemonFilePlan, Test::pokemonFileRun, &trainer);
    }

    /// A loose Gen 2 / 4 / 5 / 6 / 7 save, opened the way Save::openExternalSave opens one: an
    /// emulator RTC footer is trimmed only when the bytes as they stand do not detect. Without these
    /// the per-save suites never saw nine of the nineteen formats on the host.
    template <typename TrainerT>
    bool openLoose(Test::Report &report, const char *title, Enums::GameVersion group, const std::string &savePath)
    {
        std::vector<uint8_t> saveBytes = readFile(savePath);
        if (saveBytes.empty())
        {
            return false;
        }
        if (!TrainerT::detect(saveBytes))
        {
            Save::stripRtcFooter(saveBytes);
        }
        if (!TrainerT::detect(saveBytes))
        {
            std::printf("  !! %s: present but not detected as a save (%s)\n", title, savePath.c_str());
            return false;
        }
        TrainerT trainer(saveBytes, savePath);
        if (!trainer.isValid())
        {
            std::printf("  !! %s: detected but did not parse (%s)\n", title, savePath.c_str());
            return false;
        }
        runTargetSuites(report, title, group, trainer);
        return true;
    }

    /// The box half of the save suite's unedited write, which is the one write this harness makes:
    /// re-encode every box slot into the trainer's own blocks, and require each OCCUPIED slot back
    /// byte for byte. Nothing reaches the file. The save suite runs the full check on the console,
    /// through the same Test::countBoxSlotChanges, but only against the save on that console; this
    /// runs it against every save here, and a save whose empty slots still hold the game's own blank
    /// is what tells a wrong slot stride from a right one.
    void checkUnmodifiedBoxWrite(Test::Report &report, const std::string &target, Enums::GameVersion group, Trainer::Trainer &trainer, const std::vector<Save::Block> &originalBlocks)
    {
        Test::SCBlockLayout layout;
        if (!Test::scBlockLayout(group, layout))
        {
            return;
        }
        Test::Context context;
        context.report = &report;
        context.suite = "boxwrite";
        context.target = target;
        context.group = group;
        context.trainer = &trainer;
        report.setScope(target, "boxwrite");
        std::printf("  %-10s %s\n", "boxwrite", target.c_str());

        trainer.updateBoxBlock();
        const Save::Block *originalBoxBlock = nullptr;
        for (const Save::Block &block : originalBlocks)
        {
            if (block.key == layout.boxBlockKey)
            {
                originalBoxBlock = &block;
            }
        }
        const Save::Block *writtenBoxBlock = nullptr;
        for (const Save::Block &block : trainer.getBlocks())
        {
            if (block.key == layout.boxBlockKey)
            {
                writtenBoxBlock = &block;
            }
        }
        if (!context.check(originalBoxBlock != nullptr && writtenBoxBlock != nullptr, "the save has no box block to write", Test::hexField("key", layout.boxBlockKey, 8)))
        {
            return;
        }

        Test::BoxSlotChanges boxSlotChanges;
        if (!context.check(Test::countBoxSlotChanges(trainer, originalBoxBlock->data, writtenBoxBlock->data, layout.boxSlotStride, boxSlotChanges), "the box block cannot be split into this format's slots", "size=" + std::to_string(originalBoxBlock->data.size()) + " stride=" + std::to_string(layout.boxSlotStride)))
        {
            return;
        }
        context.check(boxSlotChanges.occupiedSlotByteCount == 0, "an unedited box write changed an OCCUPIED box slot", Test::numberField("bytes", static_cast<long long>(boxSlotChanges.occupiedSlotByteCount)) + " " + Test::slot(static_cast<int>(boxSlotChanges.firstOccupiedSlotIndex / trainer.getSlotsPerBox()), static_cast<int>(boxSlotChanges.firstOccupiedSlotIndex % trainer.getSlotsPerBox())));
        if (boxSlotChanges.emptySlotByteCount)
        {
            context.note("an unedited box write normalised empty box slots to the canonical blank (documented)", Test::numberField("bytes", static_cast<long long>(boxSlotChanges.emptySlotByteCount)));
        }
    }

    /// An SCBlock game (SwSh / BDSP-less / LA / SV / Z-A): decrypt the file into blocks and hand
    /// them to that game's Trainer, exactly as Save::readTrainerInfo does on console.
    template <typename TrainerT>
    bool openSCBlock(Test::Report &report, const char *label, Enums::GameVersion group, const std::string &savePath)
    {
        std::vector<uint8_t> saveBytes = readFile(savePath);
        if (saveBytes.empty())
        {
            return false;
        }
        // decrypt() takes a non-const pointer (it decrypts the XOR pad in place on a copy it makes
        // internally); the buffer here is ours and about to be discarded either way.
        std::vector<Save::Block> blocks = Encryption::decrypt(saveBytes.data(), saveBytes.size());
        if (blocks.empty())
        {
            std::printf("  !! %s: decrypt produced no blocks (%s)\n", label, savePath.c_str());
            return false;
        }
        const std::vector<Save::Block> originalBlocks = blocks;
        TrainerT trainer(std::move(blocks));
        // Name the target by the title the SAVE says it is, not by the group: this file is one of
        // Sword or Shield, not both, and the report should say which. `label` is only the fallback
        // for a format that cannot tell (and the printed name if the save failed to parse).
        const Enums::GameVersion version = trainer.getGameVersion();
        const std::string target = version == Enums::GameVersion::Invalid ? std::string(label) : Enums::getGameVersionName(version);
        runTargetSuites(report, target, group, trainer);
        // Last, because it rewrites the trainer's box block, which every suite above reads through.
        checkUnmodifiedBoxWrite(report, target, group, trainer, originalBlocks);
        return true;
    }
}

int main(int argc, char **argv)
{
    const std::string reportFilePath = (argc > 1) ? argv[1] : std::string("selftest-report.log");
    const std::string saveRoot = (argc > 2) ? argv[2] : (std::getenv("HOME") ? std::string(std::getenv("HOME")) + "/pkse-test-saves" : std::string());

    // build-selftest.sh points this into build-harness/, which is ignored; run by hand, it goes next
    // to the report.
    const char *workDirectoryOverride = std::getenv("PKSE_SELFTEST_WORK");
    g_workDirectory = workDirectoryOverride ? std::string(workDirectoryOverride) : (reportFilePath.find('/') == std::string::npos ? std::string(".") : reportFilePath.substr(0, reportFilePath.rfind('/'))) + "/selftest-work";
    mkdir(g_workDirectory.c_str(), 0755);

    Test::Report report;
    report.reset();
    Test::clearSamples();

    std::printf("PKSE self-test framework, host run\n\n");

    runSuite(report, "tables", "-", Enums::GameVersion::Any, Enums::GameVersion::Invalid, Test::tablesPlan, Test::tablesRun);

    for (Enums::GameVersion group : ALL_SAVE_GROUPS)
    {
        runSuite(report, "gentables", Enums::getGameVersionName(group), group, static_cast<Enums::GameVersion>(Enums::getGroupRepVersion(group)), Test::generationTablesPlan, Test::generationTablesRun);
    }

    int openedSaveCount = 0;
    if (!saveRoot.empty())
    {
        std::printf("\n  saves: %s\n", saveRoot.c_str());
        {
            const std::vector<uint8_t> fireRedBytes = readFile(SaveTree::fireRed(saveRoot));
            if (!fireRedBytes.empty())
            {
                // The Gen 3 discriminator, BOTH WAYS. Trainer3FRLG::detect() is what identifies a
                // FireRed/LeafGreen save whose Switch title id PKSE does not recognise -- and
                // FR/LG ship a separate title per language, so an unreported localisation reaches
                // the picker through this and nothing else. The two containers are the same 128
                // KiB of fourteen rotated sectors and are told apart by ONE word at Small+0xAC, so
                // a detector that merely said "this is Gen 3" would open a Hoenn save with Kanto's
                // offsets and show a plausible, wrong trainer. Asserting only the positive would
                // not catch that; the negatives below are the half that matters.
                report.setScope("FireRed", "gen3detect");
                auto expect = [&report](bool held, const char *failureDescription)
                {
                    if (held)
                    {
                        report.pass();
                    }
                    else
                    {
                        report.record(Test::Severity::Fail, failureDescription, "");
                    }
                };
                expect(Trainer::Trainer3FRLG::detect(fireRedBytes), "a real FireRed save is identified by content");
                std::vector<uint8_t> hoennProbe = readFile(SaveTree::ruby(saveRoot));
                if (hoennProbe.empty())
                {
                    hoennProbe = readFile(SaveTree::emerald(saveRoot));
                }
                if (!hoennProbe.empty())
                {
                    Save::stripRtcFooter(hoennProbe);
                    expect(!Trainer::Trainer3FRLG::detect(hoennProbe), "a Ruby/Emerald save is NOT mistaken for FireRed/LeafGreen");
                    expect(!Trainer::Trainer3RSE::detect(fireRedBytes), "a FireRed save is NOT mistaken for Ruby/Sapphire/Emerald");
                }
                Trainer::Trainer3FRLG trainer(fireRedBytes, "FireRed_e.sav");
                runTargetSuites(report, "FireRed", Enums::GameVersion::FRLG, trainer);
                ++openedSaveCount;
            }
        }
        // Ruby, Sapphire and Emerald, each from its own folder. Ruby and Sapphire are byte-identical
        // with no version byte -- exactly the Red/Blue problem below -- so the folder is the only
        // thing that can say which, and Emerald is run separately because it is a different bag
        // layout (30-slot pouches, keyed counts) rather than a different game with the same one.
        // Every one of these files carries an emulator RTC footer, so they are trimmed the way
        // Save::openExternalSave trims them before a Trainer ever sees the bytes.
        for (const char *title : {"Ruby", "Sapphire", "Emerald"})
        {
            const std::string savePath = (std::string(title) == "Ruby") ? SaveTree::ruby(saveRoot) : (std::string(title) == "Sapphire") ? SaveTree::sapphire(saveRoot) : SaveTree::emerald(saveRoot);
            std::vector<uint8_t> hoennBytes = readFile(savePath);
            if (hoennBytes.empty())
            {
                continue;
            }
            Save::stripRtcFooter(hoennBytes);
            if (!Trainer::Trainer3RSE::detect(hoennBytes))
            {
                continue;
            }
            Trainer::Trainer3RSE trainer(std::move(hoennBytes), savePath);
            runTargetSuites(report, title, Enums::GameVersion::RSE, trainer);
            ++openedSaveCount;
        }
        // Gen 1 and Let's Go were both missing here, and both cost a real failure on hardware that
        // the host run could not have caught: Gen 1 because the setters suite assumed every format
        // has a 16-bit secret id, Let's Go because it is the one format with neither a flat GBA
        // layout nor SCBlocks. Neither is an SCBlock game, so neither fitted the helper below --
        // which is exactly how they came to be skipped.
        // Red and Blue are run SEPARATELY when both folders hold a save, because a Gen 1 save has
        // no version byte anywhere in its 32 KiB -- the folder is the only thing that can say which
        // game it came from, and running one file as "Red/Blue" reports a pair when the user told
        // us a game.
        for (const char *title : {"Red", "Blue"})
        {
            const std::string savePath = (std::string(title) == "Red") ? SaveTree::red(saveRoot) : SaveTree::blue(saveRoot);
            std::vector<uint8_t> redBlueBytes = readFile(savePath);
            bool isJapanese = false;
            if (!redBlueBytes.empty() && Trainer::Trainer1RBY::detect(redBlueBytes, isJapanese))
            {
                Trainer::Trainer1RBY trainer(std::move(redBlueBytes), savePath);
                runTargetSuites(report, title, Enums::GameVersion::RBY, trainer);
                ++openedSaveCount;
            }
        }
        {
            std::vector<uint8_t> letsGoBytes = readFile(SaveTree::letsGoPikachu(saveRoot));
            if (letsGoBytes.size() >= Trainer::SAVE_SIZE7_LGPE)
            {
                std::vector<uint8_t> saveAreaBytes(letsGoBytes.begin(), letsGoBytes.begin() + Trainer::SAVE_SIZE7_LGPE);
                Trainer::Trainer7LGPE trainer(Trainer::createBlocksFromSaveData7LGPE(saveAreaBytes));
                runTargetSuites(report, "Let's Go Pikachu", Enums::GameVersion::GG, trainer);
                ++openedSaveCount;
            }
        }
        {
            // Eevee is its own save and its own Pokedex, and an unedited write of it changed bytes on
            // the console that the Pikachu save never did.
            std::vector<uint8_t> letsGoBytes = readFile(SaveTree::letsGoEevee(saveRoot));
            if (letsGoBytes.size() >= Trainer::SAVE_SIZE7_LGPE)
            {
                std::vector<uint8_t> saveAreaBytes(letsGoBytes.begin(), letsGoBytes.begin() + Trainer::SAVE_SIZE7_LGPE);
                Trainer::Trainer7LGPE trainer(Trainer::createBlocksFromSaveData7LGPE(saveAreaBytes));
                runTargetSuites(report, "Let's Go Eevee", Enums::GameVersion::GG, trainer);
                ++openedSaveCount;
            }
        }
        {
            // Ultra Sun is here for one reason the other pre-Switch saves are not: PK7 is a
            // conversion ENDPOINT now (Gen 1/2 land in it, and every Gen 8/9 game converts both
            // ways with it), so the matrix needs a real 3DS Pokemon to push through the remap
            // rather than only the synthetic ones it builds itself.
            const std::string ultraSunPath = SaveTree::ultraSun(saveRoot);
            std::vector<uint8_t> ultraSunBytes = readFile(ultraSunPath);
            if (!ultraSunBytes.empty() && Trainer::Trainer7USUM::detect(ultraSunBytes))
            {
                Trainer::Trainer7USUM trainer(std::move(ultraSunBytes), ultraSunPath);
                if (trainer.isValid())
                {
                    runTargetSuites(report, "Ultra Sun", Enums::GameVersion::USUM, trainer);
                    ++openedSaveCount;
                }
            }
        }
        struct LooseTitle
        {
            const char *title;
            Enums::GameVersion group;
        };
        // clang-format off
        for (const LooseTitle &loose : {LooseTitle{"Gold", Enums::GameVersion::GSC},
                                        LooseTitle{"Silver", Enums::GameVersion::GSC},
                                        LooseTitle{"Crystal", Enums::GameVersion::GSC},
                                        LooseTitle{"Diamond", Enums::GameVersion::DP},
                                        LooseTitle{"Pearl", Enums::GameVersion::DP},
                                        LooseTitle{"Platinum", Enums::GameVersion::PT},
                                        LooseTitle{"HeartGold", Enums::GameVersion::HGSS},
                                        LooseTitle{"SoulSilver", Enums::GameVersion::HGSS},
                                        LooseTitle{"Black", Enums::GameVersion::BW},
                                        LooseTitle{"White", Enums::GameVersion::BW},
                                        LooseTitle{"Black 2", Enums::GameVersion::B2W2},
                                        LooseTitle{"White 2", Enums::GameVersion::B2W2},
                                        LooseTitle{"X", Enums::GameVersion::XY},
                                        LooseTitle{"Y", Enums::GameVersion::XY},
                                        LooseTitle{"Omega Ruby", Enums::GameVersion::ORAS},
                                        LooseTitle{"Alpha Sapphire", Enums::GameVersion::ORAS},
                                        LooseTitle{"Sun", Enums::GameVersion::SM},
                                        LooseTitle{"Moon", Enums::GameVersion::SM}})
        // clang-format on
        {
            const std::string savePath = SaveTree::forTitle(saveRoot, loose.title);
            if (savePath.empty())
            {
                continue;
            }
            bool looseOpened = false;
            switch (loose.group)
            {
            case Enums::GameVersion::GSC:
                looseOpened = openLoose<Trainer::Trainer2GSC>(report, loose.title, loose.group, savePath);
                break;
            case Enums::GameVersion::DP:
                looseOpened = openLoose<Trainer::Trainer4DP>(report, loose.title, loose.group, savePath);
                break;
            case Enums::GameVersion::PT:
                looseOpened = openLoose<Trainer::Trainer4PT>(report, loose.title, loose.group, savePath);
                break;
            case Enums::GameVersion::HGSS:
                looseOpened = openLoose<Trainer::Trainer4HGSS>(report, loose.title, loose.group, savePath);
                break;
            case Enums::GameVersion::BW:
                looseOpened = openLoose<Trainer::Trainer5BW>(report, loose.title, loose.group, savePath);
                break;
            case Enums::GameVersion::B2W2:
                looseOpened = openLoose<Trainer::Trainer5B2W2>(report, loose.title, loose.group, savePath);
                break;
            case Enums::GameVersion::XY:
                looseOpened = openLoose<Trainer::Trainer6XY>(report, loose.title, loose.group, savePath);
                break;
            case Enums::GameVersion::ORAS:
                looseOpened = openLoose<Trainer::Trainer6ORAS>(report, loose.title, loose.group, savePath);
                break;
            case Enums::GameVersion::SM:
                looseOpened = openLoose<Trainer::Trainer7SM>(report, loose.title, loose.group, savePath);
                break;
            default:
                break;
            }
            if (looseOpened)
            {
                ++openedSaveCount;
            }
        }
        openedSaveCount += openSCBlock<Trainer::Trainer8SWSH>(report, "Sword/Shield", Enums::GameVersion::SWSH, SaveTree::shield(saveRoot)) ? 1 : 0;
        openedSaveCount += openSCBlock<Trainer::Trainer8LA>(report, "Legends Arceus", Enums::GameVersion::PLA, SaveTree::legendsArceus(saveRoot)) ? 1 : 0;
        openedSaveCount += openSCBlock<Trainer::Trainer9SV>(report, "Scarlet/Violet", Enums::GameVersion::SV, SaveTree::violet(saveRoot)) ? 1 : 0;
        openedSaveCount += openSCBlock<Trainer::Trainer9LZA>(report, "Legends Z-A", Enums::GameVersion::ZA, SaveTree::legendsZA(saveRoot)) ? 1 : 0;
    }
    if (openedSaveCount == 0)
    {
        report.setScope("-", "plan");
        Test::Context context;
        context.report = &report;
        context.skip("no saves were opened, so the entity / setters / legality / conversion suites did not run", "saveRoot=\"" + saveRoot + "\"");
    }
    else
    {
        runSuite(report, "conversion", "cross-title", Enums::GameVersion::Any, Enums::GameVersion::Invalid, Test::conversionPlan, Test::conversionRun);
        // AND AGAIN WITH AUTO-LEGALIZE ON. The console runs the suite under whatever the user has set,
        // and that setting changes what a Gen 3 conversion does -- so a host that only ever ran the
        // default passed while a console with the toggle on reported 24 failures. Both, every run.
        const bool autoLegalizeBefore = g_autoLegalizeTransfers;
        g_autoLegalizeTransfers = true;
        runSuite(report, "conversion", "cross-title (auto-legalize)", Enums::GameVersion::Any, Enums::GameVersion::Invalid, Test::conversionPlan, Test::conversionRun);
        g_autoLegalizeTransfers = autoLegalizeBefore;
        // The bank, against a scratch bank in the work directory, from the samples the entity
        // sweep collected. Its other chunk reads the user's real bank, which is not on this machine
        // and so reads as empty.
        runSuite(report, "bank", "bank", Enums::GameVersion::Any, Enums::GameVersion::Invalid, Test::bankPlan, Test::bankRun);
    }

    std::printf("\n");
    for (const auto &tally : report.tallies())
    {
        std::printf("  %-34s %-10s %7d checks  %4d fail  %4d warn  %4d skip\n", tally.target.c_str(), tally.suite.c_str(), tally.checks, tally.fails, tally.warns, tally.skips);
    }

    std::printf("\nfindings:\n");
    for (const auto &findingGroup : report.groups())
    {
        std::printf("  [%s] %s: %s  x%d\n", Test::severityName(findingGroup.severity), findingGroup.suite.c_str(), findingGroup.what.c_str(), findingGroup.occurrenceCount);
        for (size_t i = 0; i < findingGroup.samples.size() && i < 4; ++i)
        {
            std::printf("        %s\n", findingGroup.samples[i].c_str());
        }
    }
    if (report.groups().empty())
    {
        std::printf("  (none)\n");
    }

    report.write(reportFilePath, "PKSE SELF TEST (host, data tables only)\n");
    std::printf("\nchecks=%d failures=%d warnings=%d skipped=%d notes=%d  -> %s\n", report.checks(), report.failures(), report.warnings(), report.skips(), report.notes(), reportFilePath.c_str());
    return report.failures() == 0 ? 0 : 1;
}
