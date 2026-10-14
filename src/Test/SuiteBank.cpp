/**
 * The bank's contract is one sentence: byte-in == byte-out. A deposited Pokemon is stored as-is, in
 * its own game's native (encrypted) bytes, and comes back out identical -- the bank never converts
 * or mutates anything. That is what makes it safe to park a pokemon from one game in it while another is
 * open, and it is not a property that can be eyeballed: the failure looks like a Pokemon that is
 * still there and still has the right name.
 *
 * So this deposits a real Pokemon from every generation the run found, writes the bank, reads it
 * back into a SECOND bank, and compares the bytes slot by slot. That covers the tag routing (each
 * slot carries its own game-group tag), the per-generation encrypt/decrypt pair, the record
 * table's fixed-stride layout, and load()'s rejection path all at once.
 *
 * IT RUNS AGAINST A SCRATCH BANK. Bank::setRootOverride points the file at the run's own working
 * directory for the duration; the user's real bank is never written. It IS read, once, without the
 * override -- opening it read-only and reporting how many records it dropped is a genuinely useful
 * thing to know and costs nothing, since load() writes no file.
 */
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>

#include "Test/TestSuite.h"
#include "Trainer/Bank.h"
#include "Globals.h"

namespace Test
{

    namespace
    {
        /// The group a Pokemon is banked AS: a bank tag names an entity FORMAT, and several groups
        /// share one (PKHeX's PK3, PK4, PK5, PK6 and PK7 each serve a whole generation), stored under
        /// the latest group of that generation. Stated here rather than read from
        /// Bank::groupAsBanked, which is the mapping under test.
        Enums::GameVersion formatGroupFor(Enums::GameVersion group)
        {
            using Enums::GameVersion;
            switch (group)
            {
            case GameVersion::RSE:
                return GameVersion::FRLG;
            case GameVersion::DP:
            case GameVersion::PT:
                return GameVersion::HGSS;
            case GameVersion::BW:
                return GameVersion::B2W2;
            case GameVersion::XY:
                return GameVersion::ORAS;
            case GameVersion::SM:
                return GameVersion::USUM;
            default:
                return group;
            }
        }

        enum Chunk
        {
            RealBankChunk = 0,
            ScratchBankChunk,
            ChunkCount
        };

        bool sameBytes(const Pokemon::Pokemon &a, const Pokemon::Pokemon &b, size_t &firstDiff)
        {
            const auto firstData = a.getData(), db = b.getData();
            if (firstData.size() != db.size())
            {
                firstDiff = 0;
                return false;
            }
            for (size_t daIndex = 0; daIndex < firstData.size(); ++daIndex)
                if (firstData[daIndex] != db[daIndex])
                {
                    firstDiff = daIndex;
                    return false;
                }
            return true;
        }

        /// Read the user's own bank, without touching it. load() only reads; the one thing it can
        /// write is a rename of an unparseable file out of the way, which is the behaviour we want
        /// to know about anyway.
        void checkRealBank(Context &context)
        {
            Trainer::Bank real;
            size_t occupied = 0;
            for (size_t boxIndex = 0; boxIndex < real.boxCount(); ++boxIndex)
                for (size_t slotIndex = 0; slotIndex < real.slotsPerBox(); ++slotIndex)
                    if (!isEmptySlot(real.boxes[boxIndex][slotIndex].get()))
                        ++occupied;

            context.check(real.lastLoadRejects() == 0, "the user's bank file has records that would not decode to a valid Pokemon", "rejects=" + std::to_string(real.lastLoadRejects()) + " loaded=" + std::to_string(occupied));
            context.check(!real.hasChanged(), "a freshly loaded bank already reports unsaved changes (load and serialize disagree, so leaving the storage view would prompt to save)", "loaded=" + std::to_string(occupied));

            // Every record that DID load must still be structurally valid: load() gates on
            // isStructurallyValid, so anything here that fails it means the gate is not doing its job.
            size_t rejectedCount = 0;
            for (size_t boxIndex = 0; boxIndex < real.boxCount(); ++boxIndex)
                for (size_t slotIndex = 0; slotIndex < real.slotsPerBox(); ++slotIndex)
                    if (!isEmptySlot(real.boxes[boxIndex][slotIndex].get()) && !real.boxes[boxIndex][slotIndex]->isStructurallyValid())
                        ++rejectedCount;
            context.check(rejectedCount == 0, "the user's bank holds Pokemon that fail their structural validity check", "count=" + std::to_string(rejectedCount));

            context.note("user's bank", "occupied=" + std::to_string(occupied) + " rejects=" + std::to_string(real.lastLoadRejects()));
        }

        bool pathExists(const std::string &path)
        {
            struct stat status;
            return stat(path.c_str(), &status) == 0;
        }

        /// The bank file is the user's only copy of everything in it, so a bank PKSE cannot use is
        /// never written over. Run against the scratch bank the deposit check just saved (box 1 is
        /// named "SelfTest"), in the three shapes that once lost a bank or could:
        ///
        ///   - a save interrupted after the new file was written but before it replaced the old one
        ///     leaves only bank.dat.tmp, which the next load must put back;
        ///   - a file that is not a bank is moved aside to bank.dat.unreadable, with a message;
        ///   - a file that exists but cannot be read at all (a directory stands in for an SD card
        ///     error here: it exists, and no read of it succeeds) is not taken for "no bank yet",
        ///     which ran the first-run path and let the next save overwrite it.
        void checkBankFileProtection(Context &context, const std::string &directory)
        {
            const std::string bankPath = directory + "/bank.dat";
            const std::string temporaryPath = bankPath + ".tmp";
            const std::string asidePath = bankPath + ".unreadable";
            if (!pathExists(bankPath))
            {
                context.skip("the scratch bank was not written, so bank-file protection was not tested");
                return;
            }

            // An interrupted save.
            std::remove(temporaryPath.c_str());
            if (context.check(std::rename(bankPath.c_str(), temporaryPath.c_str()) == 0, "could not stage an interrupted bank save", "dir=\"" + directory + "\""))
            {
                Trainer::Bank recovered;
                context.check(pathExists(bankPath) && !pathExists(temporaryPath), "a bank save interrupted before its rename was not recovered from bank.dat.tmp");
                context.check(recovered.boxNames[0] == "SelfTest", "the bank recovered from an interrupted save is not the bank that was saved", "box1=\"" + recovered.boxNames[0] + "\"");
                context.check(recovered.lastLoadProblem().empty(), "recovering an interrupted bank save was reported as a problem", "message=\"" + recovered.lastLoadProblem() + "\"");
            }

            // A file that is not a bank.
            std::remove(asidePath.c_str());
            std::remove(bankPath.c_str());
            if (FILE *garbage = std::fopen(bankPath.c_str(), "wb"))
            {
                std::fputs("not a PKSE bank", garbage);
                std::fclose(garbage);
                Trainer::Bank damaged;
                context.check(!damaged.lastLoadProblem().empty(), "a bank file that is not a bank opened with no message");
                context.check(pathExists(asidePath), "a bank file that is not a bank was not kept as bank.dat.unreadable");
                damaged.boxNames[0] = "AfterDamage";
                context.check(damaged.save(), "the bank could not be saved after its damaged file was set aside");
                context.check(pathExists(asidePath), "saving the bank removed the damaged file that had been set aside");
            }
            std::remove(asidePath.c_str());

            // A file that exists but will not read, staged two ways: a directory in its place (no
            // read of it succeeds on a console), and a file with no read permission (which a
            // console ignores but a desktop host honours).
            auto checkUnreadable = [&](const char *how)
            {
                Trainer::Bank unreadable;
                context.check(!unreadable.lastLoadProblem().empty(), "a bank file that exists but cannot be read opened as an empty bank with no message", std::string("staged=") + how);
                unreadable.boxNames[0] = "Overwrite";
                const bool saved = unreadable.save();
                // Either it was moved aside and the save went to a new file, or it could not be
                // moved and the save refused. Writing over it in place is the one wrong answer.
                context.check(pathExists(asidePath) || !saved, "a bank file that could not be read was neither set aside nor protected from the next save", std::string("staged=") + how);
            };
            std::remove(bankPath.c_str());
            if (context.check(mkdir(bankPath.c_str(), 0777) == 0, "could not stage an unreadable bank file", "dir=\"" + directory + "\""))
            {
                checkUnreadable("directory");
                rmdir(asidePath.c_str());
                rmdir(bankPath.c_str());
            }
            std::remove(bankPath.c_str());
            if (FILE *locked = std::fopen(bankPath.c_str(), "wb"))
            {
                std::fputs("PKSEBANK", locked);
                std::fclose(locked);
                chmod(bankPath.c_str(), 0);
                checkUnreadable("no-read-permission");
                chmod(asidePath.c_str(), 0666);
                chmod(bankPath.c_str(), 0666);
                std::remove(asidePath.c_str());
            }
            std::remove(bankPath.c_str());
            std::remove(temporaryPath.c_str());
        }

        void checkScratchBank(Context &context)
        {
            if (samples().empty())
            {
                context.skip("bank deposit was not tested: no Pokemon were collected from any save");
                return;
            }

            // The run's work directory (sdmc:/PKSE/tests/work on a console, the harness's scratch
            // folder on the host).
            const std::string workRoot = context.workDir.empty() ? BASE_SAVE_DIRECTORY + "/tests/work" : context.workDir;
            const std::string directory = workRoot + "/bank";
            mkdir((BASE_SAVE_DIRECTORY + "/tests").c_str(), 0777);
            mkdir(workRoot.c_str(), 0777);
            mkdir(directory.c_str(), 0777);
            std::remove((directory + "/bank.dat").c_str()); // start from an empty bank every run

            Trainer::Bank::setRootOverride(directory);

            {
                Trainer::Bank scratch;
                context.check(scratch.lastLoadRejects() == 0, "a brand-new scratch bank reported rejected records", "dir=\"" + directory + "\"");

                // Deposit one sample per slot, spread across two boxes so the record table's stride
                // is exercised rather than just its first row.
                size_t placed = 0;
                for (const auto &s : samples())
                {
                    if (!s.pokemon)
                        continue;
                    if (auto copiedPokemon = s.pokemon->clone())
                    {
                        const size_t box = placed / 4;
                        const size_t slotI = placed % 4;
                        if (box >= scratch.boxCount())
                            break;
                        scratch.boxes[box][slotI] = std::move(copiedPokemon);
                        ++placed;
                    }
                }
                context.check(placed > 0, "no sample could be cloned into the scratch bank");
                scratch.boxNames[0] = "SelfTest";

                context.check(scratch.hasChanged(), "the bank does not report a change after Pokemon were deposited into it");

                const bool saved = scratch.save();
                context.check(saved, "writing the scratch bank failed", "dir=\"" + directory + "\"");
                // The bank verifies its own image on the way out. A non-zero count here is the
                // byte-in == byte-out contract failing, reported rather than enforced by design.
                context.check(scratch.lastVerifyFailures() == 0, "the bank's own save-time verification found slots that did not survive an encrypt/decrypt round trip", "failures=" + std::to_string(scratch.lastVerifyFailures()) + " placed=" + std::to_string(placed));
                context.check(!scratch.hasChanged(), "the bank still reports unsaved changes immediately after a successful save");

                Trainer::Bank reloaded;
                context.check(reloaded.lastLoadRejects() == 0, "reloading the bank PKSE just wrote rejected records", "rejects=" + std::to_string(reloaded.lastLoadRejects()));
                context.check(reloaded.boxNames[0] == "SelfTest", "a bank box name did not survive save/reload", "read=\"" + reloaded.boxNames[0] + "\"");

                // DISCARD. Storage's Save/Discard prompt discards by reloading the bank, so an unsaved
                // release and rename followed by load() must give back exactly what was saved, and the
                // bank must stop reporting a change.
                if (reloaded.boxes[0][0])
                {
                    const uint16_t savedSpecies = reloaded.boxes[0][0]->speciesID();
                    reloaded.boxes[0][0].reset();
                    reloaded.boxNames[0] = "Discard";
                    context.check(reloaded.hasChanged(), "the bank does not report a change after a release and a rename");
                    reloaded.load();
                    context.check(!reloaded.hasChanged(), "the bank still reports a change after discarding by reload");
                    context.check(reloaded.boxNames[0] == "SelfTest", "discarding did not restore a renamed bank box", "read=\"" + reloaded.boxNames[0] + "\"");
                    context.check(reloaded.boxes[0][0] && reloaded.boxes[0][0]->speciesID() == savedSpecies, "discarding did not bring back a released bank Pokemon", "want=" + std::to_string(savedSpecies));
                }

                size_t index = 0;
                for (const auto &s : samples())
                {
                    if (!s.pokemon)
                        continue;
                    const size_t box = index / 4, slotI = index % 4;
                    ++index;
                    if (box >= reloaded.boxCount())
                        break;

                    const std::string whereLabel = s.origin + "  " + monLabel(*s.pokemon) + "  " + slot((int)box, (int)slotI);
                    const auto &got = reloaded.boxes[box][slotI];
                    if (!context.check(got != nullptr, "a deposited Pokemon is missing after save/reload", whereLabel))
                        continue;
                    // Against the group the bank STORES it as, not the group it was deposited
                    // from. A bank tag names an entity format and several groups share one, so a
                    // Ruby PK3 legitimately reads back as FireRed/LeafGreen. Asserting the
                    // deposited group instead reported thirteen real Pokemon as corrupted.
                    const Enums::GameVersion wantGroup = formatGroupFor(s.group);
                    context.check(got->getGameGroup() == wantGroup, "a deposited Pokemon came back tagged as a different game", whereLabel + " deposited=" + Enums::getGameVersionName(s.group) + " expected=" + Enums::getGameVersionName(wantGroup) + " got=" + Enums::getGameVersionName(got->getGameGroup()));
                    size_t diff = 0;
                    context.check(sameBytes(*s.pokemon, *got, diff), "a deposited Pokemon's bytes changed across save/reload (the bank's byte-in == byte-out contract)", whereLabel + " " + hex("offset", diff, 4));
                }
            }

            checkBankFileProtection(context, directory);

            Trainer::Bank::setRootOverride("");
            context.check(!Trainer::Bank::rootOverridden(), "the bank path override was not cleared after the test");
            std::remove((directory + "/bank.dat").c_str());
        }
    }

    int bankPlan(Context &) { return ChunkCount; }

    void bankRun(Context &context, int chunk)
    {
        switch (chunk)
        {
        case RealBankChunk:
            checkRealBank(context);
            break;
        case ScratchBankChunk:
            checkScratchBank(context);
            break;
        default:
            break;
        }
    }
}
