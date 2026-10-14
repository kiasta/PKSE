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
 * paths are the one thing here that still needs a console, along with ROMFS and the bank.
 *
 * That gives two things: a way to know the framework works before it reaches hardware, and a fast
 * loop for the parts most likely to change. It does NOT replace the on-console run -- the save
 * WRITE path, edit persistence, the ROMFS sprite sweep and the bank are hardware-only.
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

namespace Utils {
    void logInfoToFile(const char*)  {}
    void logInfoToFile(std::string)  {}
    void logErrorToFile(const char*) {}
    void logErrorToFile(std::string) {}
    void logInfoToFile(const char*, const char*)  {}
    void logErrorToFile(const char*, const char*) {}
    void logEventToFile(const std::string&) {}
    void logTest(const std::string&)        {}
    void logTestSession(const std::string&) {}
    // Real, because PokemonFile::write reads its own file back to verify it before promoting it. The
    // app's version allocates with malloc and so does this one.
    uint8_t* readAllBytes(const char* path, size_t* outSize) {
        if (outSize) *outSize = 0;
        FILE* file = std::fopen(path, "rb");
        if (!file) return nullptr;
        std::fseek(file, 0, SEEK_END);
        const long length = std::ftell(file);
        std::fseek(file, 0, SEEK_SET);
        uint8_t* bytes = static_cast<uint8_t*>(std::malloc(length > 0 ? (size_t)length : 1));
        const size_t got = (bytes && length > 0) ? std::fread(bytes, 1, (size_t)length, file) : 0;
        std::fclose(file);
        if (!bytes || got != (size_t)(length > 0 ? length : 0)) { std::free(bytes); return nullptr; }
        if (outSize) *outSize = got;
        return bytes;
    }
}

namespace {

    /// Every save-format group, so the per-generation tables are swept for all of them rather than
    /// only for the ones this machine happens to have a save for. On hardware the same suite runs
    /// once per generation in the plan; here there is no such limit, so it runs for everything.
    /// The list itself is Test::ALL_SAVE_GROUPS -- shared with the shipped suites, so a new game
    /// cannot be added to one run and forgotten in the other.
    using Test::ALL_SAVE_GROUPS;

    /// The scratch directory a suite may write into, as the runner's work directory is on console.
    std::string g_workDir;

    void runSuite(Test::Report& rep, const char* name, const std::string& target,
                  Enums::GameVersion group, Enums::GameVersion version,
                  int (*plan)(Test::Context&), void (*run)(Test::Context&, int),
                  Trainer::Trainer* trainer = nullptr) {
        Test::Context context;
        context.rep     = &rep;
        context.suite   = name;
        context.target  = target;
        context.group   = group;
        context.version = version;
        context.trainer = trainer;
        context.workDir = g_workDir;
        rep.setScope(target, name);

        const int chunks = plan(context);
        std::printf("  %-10s %s  (%d chunk%s)\n", name, target.c_str(), chunks, chunks == 1 ? "" : "s");
        for (int c = 0; c < chunks; ++c) {
            rep.setScope(target, name);
            run(context, c);
        }
    }

    std::vector<uint8_t> readFile(const std::string& path) {
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) return {};
        std::fseek(f, 0, SEEK_END);
        const long n = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        std::vector<uint8_t> out(n > 0 ? (size_t)n : 0);
        if (!out.empty() && std::fread(out.data(), 1, out.size(), f) != out.size()) out.clear();
        std::fclose(f);
        return out;
    }

    /// Run every suite that needs a loaded save, against one. The save is never written back --
    /// the write path is the one part of the "save" suite that genuinely needs a console, so it
    /// is not run here at all rather than half-run.
    void runTargetSuites(Test::Report& rep, const std::string& target, Enums::GameVersion group,
                         Trainer::Trainer& trainer) {
        // The trainer names its own title -- the same answer the on-console runner uses for a
        // loose save -- so the harness and the console bucket samples identically.
        const Enums::GameVersion version = trainer.getGameVersion();
        runSuite(rep, "entity",   target, group, version, Test::entityPlan,   Test::entityRun,   &trainer);
        runSuite(rep, "setters",  target, group, version, Test::settersPlan,  Test::settersRun,  &trainer);
        runSuite(rep, "legality", target, group, version, Test::legalityPlan, Test::legalityRun, &trainer);
        runSuite(rep, "pokemonfile", target, group, version, Test::pokemonFilePlan, Test::pokemonFileRun, &trainer);
    }

    /// A loose Gen 2 / 4 / 5 / 6 / 7 save, opened the way Save::openExternalSave opens one: an
    /// emulator RTC footer is trimmed only when the bytes as they stand do not detect. Without these
    /// the per-save suites never saw nine of the nineteen formats on the host.
    template <typename TrainerT>
    bool openLoose(Test::Report& rep, const char* title, Enums::GameVersion group, const std::string& path) {
        std::vector<uint8_t> bytes = readFile(path);
        if (bytes.empty()) return false;
        if (!TrainerT::detect(bytes)) Save::stripRtcFooter(bytes);
        if (!TrainerT::detect(bytes)) {
            std::printf("  !! %s: present but not detected as a save (%s)\n", title, path.c_str());
            return false;
        }
        TrainerT trainer(bytes, path);
        if (!trainer.isValid()) {
            std::printf("  !! %s: detected but did not parse (%s)\n", title, path.c_str());
            return false;
        }
        runTargetSuites(rep, title, group, trainer);
        return true;
    }

    /// An SCBlock game (SwSh / BDSP-less / LA / SV / Z-A): decrypt the file into blocks and hand
    /// them to that game's Trainer, exactly as Save::readTrainerInfo does on console.
    template <typename TrainerT>
    bool openSCBlock(Test::Report& rep, const char* label, Enums::GameVersion group,
                     const std::string& path) {
        std::vector<uint8_t> bytes = readFile(path);
        if (bytes.empty()) return false;
        // decrypt() takes a non-const pointer (it decrypts the XOR pad in place on a copy it makes
        // internally); the buffer here is ours and about to be discarded either way.
        std::vector<Save::Block> blocks = Encryption::decrypt(bytes.data(), bytes.size());
        if (blocks.empty()) {
            std::printf("  !! %s: decrypt produced no blocks (%s)\n", label, path.c_str());
            return false;
        }
        TrainerT trainer(std::move(blocks));
        // Name the target by the title the SAVE says it is, not by the group: this file is one of
        // Sword or Shield, not both, and the report should say which. `label` is only the fallback
        // for a format that cannot tell (and the printed name if the save failed to parse).
        const Enums::GameVersion version = trainer.getGameVersion();
        runTargetSuites(rep, version == Enums::GameVersion::Invalid
                                 ? std::string(label)
                                 : Enums::getGameVersionName(version),
                        group, trainer);
        return true;
    }
}

int main(int argc, char** argv) {
    const std::string outPath  = (argc > 1) ? argv[1] : std::string("selftest-report.log");
    const std::string saveRoot = (argc > 2) ? argv[2]
                               : (std::getenv("HOME") ? std::string(std::getenv("HOME")) + "/pkse-test-saves"
                                                      : std::string());

    // build-selftest.sh points this into build-harness/, which is ignored; run by hand, it goes next
    // to the report.
    const char* workEnv = std::getenv("PKSE_SELFTEST_WORK");
    g_workDir = workEnv ? std::string(workEnv)
                        : (outPath.find('/') == std::string::npos ? std::string(".")
                                                                  : outPath.substr(0, outPath.rfind('/'))) +
                              "/selftest-work";
    mkdir(g_workDir.c_str(), 0755);

    Test::Report rep;
    rep.reset();
    Test::clearSamples();

    std::printf("PKSE self-test framework, host run\n\n");

    runSuite(rep, "tables", "-", Enums::GameVersion::Any, Enums::GameVersion::Invalid,
             Test::tablesPlan, Test::tablesRun);

    for (Enums::GameVersion group : ALL_SAVE_GROUPS)
        runSuite(rep, "gentables", Enums::getGameVersionName(group), group,
                 static_cast<Enums::GameVersion>(Enums::getGroupRepVersion(group)),
                 Test::genTablesPlan, Test::genTablesRun);

    int opened = 0;
    if (!saveRoot.empty()) {
        std::printf("\n  saves: %s\n", saveRoot.c_str());
        {
            const std::vector<uint8_t> frlg = readFile(SaveTree::fireRed(saveRoot));
            if (!frlg.empty()) {
                // The Gen 3 discriminator, BOTH WAYS. Trainer3FRLG::detect() is what identifies a
                // FireRed/LeafGreen save whose Switch title id PKSE does not recognise -- and
                // FR/LG ship a separate title per language, so an unreported localisation reaches
                // the picker through this and nothing else. The two containers are the same 128
                // KiB of fourteen rotated sectors and are told apart by ONE word at Small+0xAC, so
                // a detector that merely said "this is Gen 3" would open a Hoenn save with Kanto's
                // offsets and show a plausible, wrong trainer. Asserting only the positive would
                // not catch that; the negatives below are the half that matters.
                rep.setScope("FireRed", "gen3detect");
                auto expect = [&rep](bool held, const char* what) {
                    if (held) rep.pass();
                    else      rep.record(Test::Severity::Fail, what, "");
                };
                expect(Trainer::Trainer3FRLG::detect(frlg),
                       "a real FireRed save is identified by content");
                std::vector<uint8_t> hoennProbe = readFile(SaveTree::ruby(saveRoot));
                if (hoennProbe.empty())
                    hoennProbe = readFile(SaveTree::emerald(saveRoot));
                if (!hoennProbe.empty()) {
                    Save::stripRtcFooter(hoennProbe);
                    expect(!Trainer::Trainer3FRLG::detect(hoennProbe),
                           "a Ruby/Emerald save is NOT mistaken for FireRed/LeafGreen");
                    expect(!Trainer::Trainer3RSE::detect(frlg),
                           "a FireRed save is NOT mistaken for Ruby/Sapphire/Emerald");
                }
                Trainer::Trainer3FRLG trainer(frlg, "FireRed_e.sav");
                runTargetSuites(rep, "FireRed", Enums::GameVersion::FRLG, trainer);
                ++opened;
            }
        }
        // Ruby, Sapphire and Emerald, each from its own folder. Ruby and Sapphire are byte-identical
        // with no version byte -- exactly the Red/Blue problem below -- so the folder is the only
        // thing that can say which, and Emerald is run separately because it is a different bag
        // layout (30-slot pouches, keyed counts) rather than a different game with the same one.
        // Every one of these files carries an emulator RTC footer, so they are trimmed the way
        // Save::openExternalSave trims them before a Trainer ever sees the bytes.
        for (const char* title : { "Ruby", "Sapphire", "Emerald" }) {
            const std::string path = (std::string(title) == "Ruby")     ? SaveTree::ruby(saveRoot)
                                   : (std::string(title) == "Sapphire") ? SaveTree::sapphire(saveRoot)
                                                                        : SaveTree::emerald(saveRoot);
            std::vector<uint8_t> hoenn = readFile(path);
            if (hoenn.empty())
                continue;
            Save::stripRtcFooter(hoenn);
            if (!Trainer::Trainer3RSE::detect(hoenn))
                continue;
            Trainer::Trainer3RSE trainer(std::move(hoenn), path);
            runTargetSuites(rep, title, Enums::GameVersion::RSE, trainer);
            ++opened;
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
        for (const char* title : { "Red", "Blue" }) {
            const std::string path = (std::string(title) == "Red") ? SaveTree::red(saveRoot)
                                                                   : SaveTree::blue(saveRoot);
            std::vector<uint8_t> rby = readFile(path);
            bool japanese = false;
            if (!rby.empty() && Trainer::Trainer1RBY::detect(rby, japanese)) {
                Trainer::Trainer1RBY trainer(std::move(rby), path);
                runTargetSuites(rep, title, Enums::GameVersion::RBY, trainer);
                ++opened;
            }
        }
        {
            std::vector<uint8_t> gg = readFile(SaveTree::letsGoPikachu(saveRoot));
            if (gg.size() >= Trainer::SAVE_SIZE7_LGPE) {
                std::vector<uint8_t> area(gg.begin(), gg.begin() + Trainer::SAVE_SIZE7_LGPE);
                Trainer::Trainer7LGPE trainer(Trainer::createBlocksFromSaveData7LGPE(area));
                runTargetSuites(rep, "Let's Go Pikachu", Enums::GameVersion::GG, trainer);
                ++opened;
            }
        }
        {
            // Ultra Sun is here for one reason the other pre-Switch saves are not: PK7 is a
            // conversion ENDPOINT now (Gen 1/2 land in it, and every Gen 8/9 game converts both
            // ways with it), so the matrix needs a real 3DS Pokemon to push through the remap
            // rather than only the synthetic ones it builds itself.
            const std::string usumPath = SaveTree::ultraSun(saveRoot);
            std::vector<uint8_t> usum = readFile(usumPath);
            if (!usum.empty() && Trainer::Trainer7USUM::detect(usum)) {
                Trainer::Trainer7USUM trainer(std::move(usum), usumPath);
                if (trainer.isValid()) {
                    runTargetSuites(rep, "Ultra Sun", Enums::GameVersion::USUM, trainer);
                    ++opened;
                }
            }
        }
        struct LooseTitle { const char* title; Enums::GameVersion group; };
        for (const LooseTitle& loose : { LooseTitle{"Gold", Enums::GameVersion::GSC},
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
                                         LooseTitle{"Moon", Enums::GameVersion::SM} }) {
            const std::string path = SaveTree::forTitle(saveRoot, loose.title);
            if (path.empty()) continue;
            bool ok = false;
            switch (loose.group) {
                case Enums::GameVersion::GSC:  ok = openLoose<Trainer::Trainer2GSC>(rep, loose.title, loose.group, path); break;
                case Enums::GameVersion::DP:   ok = openLoose<Trainer::Trainer4DP>(rep, loose.title, loose.group, path); break;
                case Enums::GameVersion::PT:   ok = openLoose<Trainer::Trainer4PT>(rep, loose.title, loose.group, path); break;
                case Enums::GameVersion::HGSS: ok = openLoose<Trainer::Trainer4HGSS>(rep, loose.title, loose.group, path); break;
                case Enums::GameVersion::BW:   ok = openLoose<Trainer::Trainer5BW>(rep, loose.title, loose.group, path); break;
                case Enums::GameVersion::B2W2: ok = openLoose<Trainer::Trainer5B2W2>(rep, loose.title, loose.group, path); break;
                case Enums::GameVersion::XY:   ok = openLoose<Trainer::Trainer6XY>(rep, loose.title, loose.group, path); break;
                case Enums::GameVersion::ORAS: ok = openLoose<Trainer::Trainer6ORAS>(rep, loose.title, loose.group, path); break;
                case Enums::GameVersion::SM:   ok = openLoose<Trainer::Trainer7SM>(rep, loose.title, loose.group, path); break;
                default: break;
            }
            if (ok) ++opened;
        }
        opened += openSCBlock<Trainer::Trainer8SWSH>(rep, "Sword/Shield", Enums::GameVersion::SWSH,
                                                     SaveTree::shield(saveRoot)) ? 1 : 0;
        opened += openSCBlock<Trainer::Trainer8LA>(rep, "Legends Arceus", Enums::GameVersion::PLA,
                                                   SaveTree::legendsArceus(saveRoot)) ? 1 : 0;
        opened += openSCBlock<Trainer::Trainer9SV>(rep, "Scarlet/Violet", Enums::GameVersion::SV,
                                                   SaveTree::violet(saveRoot)) ? 1 : 0;
        opened += openSCBlock<Trainer::Trainer9LZA>(rep, "Legends Z-A", Enums::GameVersion::ZA,
                                                    SaveTree::legendsZA(saveRoot)) ? 1 : 0;
    }
    if (opened == 0) {
        rep.setScope("-", "plan");
        Test::Context context;
        context.rep = &rep;
        context.skip("no saves were opened, so the entity / setters / legality / conversion suites did "
                 "not run", "saveRoot=\"" + saveRoot + "\"");
    } else {
        runSuite(rep, "conversion", "cross-title", Enums::GameVersion::Any,
                 Enums::GameVersion::Invalid, Test::conversionPlan, Test::conversionRun);
        // AND AGAIN WITH AUTO-LEGALIZE ON. The console runs the suite under whatever the user has set,
        // and that setting changes what a Gen 3 conversion does -- so a host that only ever ran the
        // default passed while a console with the toggle on reported 24 failures. Both, every run.
        const bool autoLegalizeBefore = g_autoLegalizeTransfers;
        g_autoLegalizeTransfers = true;
        runSuite(rep, "conversion", "cross-title (auto-legalize)", Enums::GameVersion::Any,
                 Enums::GameVersion::Invalid, Test::conversionPlan, Test::conversionRun);
        g_autoLegalizeTransfers = autoLegalizeBefore;
    }

    std::printf("\n");
    for (const auto& t : rep.tallies())
        std::printf("  %-34s %-10s %7d checks  %4d fail  %4d warn  %4d skip\n",
                    t.target.c_str(), t.suite.c_str(), t.checks, t.fails, t.warns, t.skips);

    std::printf("\nfindings:\n");
    for (const auto& g : rep.groups()) {
        std::printf("  [%s] %s: %s  x%d\n", Test::severityName(g.severity), g.suite.c_str(),
                    g.what.c_str(), g.count);
        for (size_t i = 0; i < g.samples.size() && i < 4; ++i)
            std::printf("        %s\n", g.samples[i].c_str());
    }
    if (rep.groups().empty()) std::printf("  (none)\n");

    rep.write(outPath, "PKSE SELF TEST (host, data tables only)\n");
    std::printf("\nchecks=%d failures=%d warnings=%d skipped=%d notes=%d  -> %s\n",
                rep.checks(), rep.failures(), rep.warnings(), rep.skips(), rep.notes(), outPath.c_str());
    return rep.failures() == 0 ? 0 : 1;
}
