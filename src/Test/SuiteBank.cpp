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
#include <vector>
#include <cstdio>

#include "Test/TestSuite.h"
#include "Conversion/Convert.h"
#include "Enums/LanguageID.h"
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

        bool sameBytes(const Pokemon::Pokemon &depositedPokemon, const Pokemon::Pokemon &reloadedPokemon, size_t &firstDifferenceOffset)
        {
            const auto depositedBytes = depositedPokemon.getData();
            const auto reloadedBytes = reloadedPokemon.getData();
            if (depositedBytes.size() != reloadedBytes.size())
            {
                firstDifferenceOffset = 0;
                return false;
            }
            for (size_t byteOffset = 0; byteOffset < depositedBytes.size(); ++byteOffset)
            {
                if (depositedBytes[byteOffset] != reloadedBytes[byteOffset])
                {
                    firstDifferenceOffset = byteOffset;
                    return false;
                }
            }
            return true;
        }

        /// Read the user's own bank, without touching it. load() only reads; the one thing it can
        /// write is a rename of an unparseable file out of the way, which is the behaviour we want
        /// to know about anyway.
        void checkRealBank(Context &context)
        {
            Trainer::Bank realBank;
            size_t occupiedSlotCount = 0;
            for (size_t boxIndex = 0; boxIndex < realBank.boxCount(); ++boxIndex)
            {
                for (size_t slotIndex = 0; slotIndex < realBank.slotsPerBox(); ++slotIndex)
                {
                    if (!isEmptySlot(realBank.boxes[boxIndex][slotIndex].get()))
                    {
                        ++occupiedSlotCount;
                    }
                }
            }

            context.check(realBank.lastLoadRejects() == 0, "the user's bank file has records that would not decode to a valid Pokemon", "rejects=" + std::to_string(realBank.lastLoadRejects()) + " loaded=" + std::to_string(occupiedSlotCount));
            context.check(!realBank.hasChanged(), "a freshly loaded bank already reports unsaved changes (load and serialize disagree, so leaving the storage view would prompt to save)", "loaded=" + std::to_string(occupiedSlotCount));

            // Every record that DID load must still be structurally valid: load() gates on
            // isStructurallyValid, so anything here that fails it means the gate is not doing its job.
            size_t invalidPokemonCount = 0;
            for (size_t boxIndex = 0; boxIndex < realBank.boxCount(); ++boxIndex)
            {
                for (size_t slotIndex = 0; slotIndex < realBank.slotsPerBox(); ++slotIndex)
                {
                    if (!isEmptySlot(realBank.boxes[boxIndex][slotIndex].get()) && !realBank.boxes[boxIndex][slotIndex]->isStructurallyValid())
                    {
                        ++invalidPokemonCount;
                    }
                }
            }
            context.check(invalidPokemonCount == 0, "the user's bank holds Pokemon that fail their structural validity check", "count=" + std::to_string(invalidPokemonCount));

            context.note("user's bank", "occupied=" + std::to_string(occupiedSlotCount) + " rejects=" + std::to_string(realBank.lastLoadRejects()));
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
        void checkBankFileProtection(Context &context, const std::string &bankDirectory)
        {
            const std::string bankPath = bankDirectory + "/bank.dat";
            const std::string temporaryPath = bankPath + ".tmp";
            const std::string asidePath = bankPath + ".unreadable";
            if (!pathExists(bankPath))
            {
                context.skip("the scratch bank was not written, so bank-file protection was not tested");
                return;
            }

            // A damaged RECORD in an otherwise good file. load() drops it, and the next save would
            // write the bank without it, so the file as it was read must be kept beside it.
            {
                const std::string damagedPath = bankPath + ".damaged";
                std::remove(damagedPath.c_str());
                std::vector<uint8_t> originalFile;
                if (FILE *bankFile = std::fopen(bankPath.c_str(), "rb"))
                {
                    int character = 0;
                    while ((character = std::fgetc(bankFile)) != EOF)
                        originalFile.push_back(static_cast<uint8_t>(character));
                    std::fclose(bankFile);
                }
                constexpr size_t firstRecordOffset = 16; // past the bank header: the first slot's tag
                if (context.check(originalFile.size() > firstRecordOffset + 4, "could not read the scratch bank to damage a record in it"))
                {
                    std::vector<uint8_t> damagedFile = originalFile;
                    damagedFile[firstRecordOffset + 3] = 0x7F; // a tag no format writes
                    auto writeBank = [&](const std::vector<uint8_t> &bankBytes)
                    {
                        if (FILE *bankFile = std::fopen(bankPath.c_str(), "wb"))
                        {
                            std::fwrite(bankBytes.data(), 1, bankBytes.size(), bankFile);
                            std::fclose(bankFile);
                        }
                    };
                    writeBank(damagedFile);
                    Trainer::Bank damagedRecordBank;
                    context.check(damagedRecordBank.lastLoadRejects() == 1, "a bank with one damaged record did not reject exactly that record", "rejects=" + std::to_string(damagedRecordBank.lastLoadRejects()));
                    context.check(!damagedRecordBank.lastLoadProblem().empty(), "a bank that dropped a damaged record opened with no message");
                    std::vector<uint8_t> keptFile;
                    if (FILE *keptBank = std::fopen(damagedPath.c_str(), "rb"))
                    {
                        int character = 0;
                        while ((character = std::fgetc(keptBank)) != EOF)
                            keptFile.push_back(static_cast<uint8_t>(character));
                        std::fclose(keptBank);
                    }
                    context.check(keptFile == damagedFile, "a bank that dropped a damaged record did not keep the file it read as bank.dat.damaged", "kept=" + std::to_string(keptFile.size()) + " bytes");
                    writeBank(originalFile);
                }
                std::remove(damagedPath.c_str());
            }

            // An interrupted save.
            std::remove(temporaryPath.c_str());
            if (context.check(std::rename(bankPath.c_str(), temporaryPath.c_str()) == 0, "could not stage an interrupted bank save", "dir=\"" + bankDirectory + "\""))
            {
                Trainer::Bank recoveredBank;
                context.check(pathExists(bankPath) && !pathExists(temporaryPath), "a bank save interrupted before its rename was not recovered from bank.dat.tmp");
                context.check(recoveredBank.boxNames[0] == "SelfTest", "the bank recovered from an interrupted save is not the bank that was saved", "box1=\"" + recoveredBank.boxNames[0] + "\"");
                context.check(recoveredBank.lastLoadProblem().empty(), "recovering an interrupted bank save was reported as a problem", "message=\"" + recoveredBank.lastLoadProblem() + "\"");
            }

            // A file that is not a bank.
            std::remove(asidePath.c_str());
            std::remove(bankPath.c_str());
            if (FILE *garbageFile = std::fopen(bankPath.c_str(), "wb"))
            {
                std::fputs("not a PKSE bank", garbageFile);
                std::fclose(garbageFile);
                Trainer::Bank damagedBank;
                context.check(!damagedBank.lastLoadProblem().empty(), "a bank file that is not a bank opened with no message");
                context.check(pathExists(asidePath), "a bank file that is not a bank was not kept as bank.dat.unreadable");
                damagedBank.boxNames[0] = "AfterDamage";
                context.check(damagedBank.save(), "the bank could not be saved after its damaged file was set aside");
                context.check(pathExists(asidePath), "saving the bank removed the damaged file that had been set aside");
            }
            std::remove(asidePath.c_str());

            // A file that exists but will not read, staged two ways: a directory in its place (no
            // read of it succeeds on a console), and a file with no read permission (which a
            // console ignores but a desktop host honours).
            auto checkUnreadable = [&](const char *stagingMethod)
            {
                Trainer::Bank unreadableBank;
                context.check(!unreadableBank.lastLoadProblem().empty(), "a bank file that exists but cannot be read opened as an empty bank with no message", std::string("staged=") + stagingMethod);
                unreadableBank.boxNames[0] = "Overwrite";
                const bool bankSaved = unreadableBank.save();
                // Either it was moved aside and the save went to a new file, or it could not be
                // moved and the save refused. Writing over it in place is the one wrong answer.
                context.check(pathExists(asidePath) || !bankSaved, "a bank file that could not be read was neither set aside nor protected from the next save", std::string("staged=") + stagingMethod);
            };
            std::remove(bankPath.c_str());
            if (context.check(mkdir(bankPath.c_str(), 0777) == 0, "could not stage an unreadable bank file", "dir=\"" + bankDirectory + "\""))
            {
                checkUnreadable("directory");
                rmdir(asidePath.c_str());
                rmdir(bankPath.c_str());
            }
            std::remove(bankPath.c_str());
            if (FILE *lockedFile = std::fopen(bankPath.c_str(), "wb"))
            {
                std::fputs("PKSEBANK", lockedFile);
                std::fclose(lockedFile);
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
            const std::string workRoot = context.workDirectory.empty() ? BASE_SAVE_DIRECTORY + "/tests/work" : context.workDirectory;
            const std::string bankDirectory = workRoot + "/bank";
            mkdir((BASE_SAVE_DIRECTORY + "/tests").c_str(), 0777);
            mkdir(workRoot.c_str(), 0777);
            mkdir(bankDirectory.c_str(), 0777);
            std::remove((bankDirectory + "/bank.dat").c_str()); // start from an empty bank every run

            Trainer::Bank::setRootOverride(bankDirectory);

            {
                Trainer::Bank scratchBank;
                context.check(scratchBank.lastLoadRejects() == 0, "a brand-new scratch bank reported rejected records", "dir=\"" + bankDirectory + "\"");

                // Deposit one sample per slot, spread across two boxes so the record table's stride
                // is exercised rather than just its first row.
                size_t placedCount = 0;
                for (const auto &sample : samples())
                {
                    if (!sample.pokemon)
                    {
                        continue;
                    }
                    if (auto copiedPokemon = sample.pokemon->clone())
                    {
                        const size_t boxIndex = placedCount / 4;
                        const size_t slotIndex = placedCount % 4;
                        if (boxIndex >= scratchBank.boxCount())
                        {
                            break;
                        }
                        scratchBank.boxes[boxIndex][slotIndex] = std::move(copiedPokemon);
                        ++placedCount;
                    }
                }
                context.check(placedCount > 0, "no sample could be cloned into the scratch bank");
                scratchBank.boxNames[0] = "SelfTest";

                context.check(scratchBank.hasChanged(), "the bank does not report a change after Pokemon were deposited into it");

                const bool bankSaved = scratchBank.save();
                context.check(bankSaved, "writing the scratch bank failed", "dir=\"" + bankDirectory + "\"");
                // The bank verifies its own image on the way out. A non-zero count here is the
                // byte-in == byte-out contract failing, reported rather than enforced by design.
                context.check(scratchBank.lastVerifyFailures() == 0, "the bank's own save-time verification found slots that did not survive an encrypt/decrypt round trip", "failures=" + std::to_string(scratchBank.lastVerifyFailures()) + " placed=" + std::to_string(placedCount));
                context.check(!scratchBank.hasChanged(), "the bank still reports unsaved changes immediately after a successful save");

                Trainer::Bank reloadedBank;
                context.check(reloadedBank.lastLoadRejects() == 0, "reloading the bank PKSE just wrote rejected records", "rejects=" + std::to_string(reloadedBank.lastLoadRejects()));
                context.check(reloadedBank.boxNames[0] == "SelfTest", "a bank box name did not survive save/reload", "read=\"" + reloadedBank.boxNames[0] + "\"");

                // DISCARD. Storage's Save/Discard prompt discards by reloading the bank, so an unsaved
                // release and rename followed by load() must give back exactly what was saved, and the
                // bank must stop reporting a change.
                if (reloadedBank.boxes[0][0])
                {
                    const uint16_t savedSpeciesId = reloadedBank.boxes[0][0]->speciesID();
                    reloadedBank.boxes[0][0].reset();
                    reloadedBank.boxNames[0] = "Discard";
                    context.check(reloadedBank.hasChanged(), "the bank does not report a change after a release and a rename");
                    reloadedBank.load();
                    context.check(!reloadedBank.hasChanged(), "the bank still reports a change after discarding by reload");
                    context.check(reloadedBank.boxNames[0] == "SelfTest", "discarding did not restore a renamed bank box", "read=\"" + reloadedBank.boxNames[0] + "\"");
                    context.check(reloadedBank.boxes[0][0] && reloadedBank.boxes[0][0]->speciesID() == savedSpeciesId, "discarding did not bring back a released bank Pokemon", "want=" + std::to_string(savedSpeciesId));
                }

                size_t sampleIndex = 0;
                for (const auto &sample : samples())
                {
                    if (!sample.pokemon)
                    {
                        continue;
                    }
                    const size_t boxIndex = sampleIndex / 4;
                    const size_t slotIndex = sampleIndex % 4;
                    ++sampleIndex;
                    if (boxIndex >= reloadedBank.boxCount())
                    {
                        break;
                    }

                    const std::string whereLabel = sample.origin + "  " + pokemonLabel(*sample.pokemon) + "  " + slot((int)boxIndex, (int)slotIndex);
                    const auto &reloadedPokemon = reloadedBank.boxes[boxIndex][slotIndex];
                    if (!context.check(reloadedPokemon != nullptr, "a deposited Pokemon is missing after save/reload", whereLabel))
                    {
                        continue;
                    }
                    // Against the group the bank STORES it as, not the group it was deposited
                    // from. A bank tag names an entity format and several groups share one, so a
                    // Ruby PK3 legitimately reads back as FireRed/LeafGreen. Asserting the
                    // deposited group instead reported thirteen real Pokemon as corrupted.
                    const Enums::GameVersion expectedGroup = formatGroupFor(sample.group);
                    context.check(reloadedPokemon->getGameGroup() == expectedGroup, "a deposited Pokemon came back tagged as a different game", whereLabel + " deposited=" + Enums::getGameVersionName(sample.group) + " expected=" + Enums::getGameVersionName(expectedGroup) + " got=" + Enums::getGameVersionName(reloadedPokemon->getGameGroup()));
                    size_t firstDifferenceOffset = 0;
                    context.check(sameBytes(*sample.pokemon, *reloadedPokemon, firstDifferenceOffset), "a deposited Pokemon's bytes changed across save/reload (the bank's byte-in == byte-out contract)", whereLabel + " " + hexField("offset", firstDifferenceOffset, 4));

                    // WITHDRAWING IT BACK INTO THE GAME IT CAME FROM. It reloads as its format's
                    // representative group, so a Ruby, Diamond, Black, X or Sun Pokemon is withdrawn
                    // across groups -- and must come back as its own game's entity, bytes unchanged,
                    // with no handler stamped. Both the withdraw path and the storage pane's convert
                    // ask canConvert() first and then convert().
                    if (reloadedPokemon->getGameGroup() != sample.group)
                    {
                        Conversion::Result gateResult = Conversion::Result::Ok;
                        const bool allowed = Conversion::canConvert(*reloadedPokemon, sample.group, gateResult);
                        context.check(allowed && gateResult == Conversion::Result::Ok, "a banked Pokemon cannot be withdrawn into the game it was deposited from", whereLabel + " deposited=" + Enums::getGameVersionName(sample.group) + " result=" + Conversion::resultMessage(gateResult));
                        Conversion::Result convertResult = Conversion::Result::Ok;
                        Conversion::ReceivingTrainer receivingTrainer{u"Other", 1, static_cast<uint8_t>(Enums::LanguageID::French)};
                        auto withdrawnPokemon = Conversion::convert(*reloadedPokemon, sample.group, convertResult, 0, &receivingTrainer);
                        if (context.check(withdrawnPokemon != nullptr && convertResult == Conversion::Result::Ok, "a banked Pokemon was refused on withdrawal into the game it was deposited from", whereLabel + " result=" + Conversion::resultMessage(convertResult)))
                        {
                            context.check(withdrawnPokemon->getGameGroup() == sample.group, "a banked Pokemon was withdrawn as the wrong game's entity", whereLabel + " got=" + Enums::getGameVersionName(withdrawnPokemon->getGameGroup()));
                            context.check(sameBytes(*sample.pokemon, *withdrawnPokemon, firstDifferenceOffset), "a banked Pokemon's bytes changed when withdrawn into the game it was deposited from", whereLabel + " " + hexField("offset", firstDifferenceOffset, 4));
                        }
                    }
                }
            }

            checkBankFileProtection(context, bankDirectory);

            // SAVE-TIME VERIFICATION APPLIES LOAD'S TEST. A record whose checksum no longer holds
            // round-trips byte for byte, and is then dropped by the next load; verification must
            // count it now, while the slot is still in memory.
            for (const auto &sample : samples())
            {
                if (!sample.pokemon || !sample.pokemon->isStructurallyValid() || sample.pokemon->checksum() == 0)
                    continue;
                auto tamperedPokemon = sample.pokemon->clone();
                if (!tamperedPokemon)
                    continue;
                tamperedPokemon->setTID16(static_cast<uint16_t>(tamperedPokemon->tid16() ^ 0x0101)); // inside the checksum, which is not refreshed
                if (tamperedPokemon->isStructurallyValid())
                    continue; // a format whose structural test is not its checksum
                Trainer::Bank verifyBank;
                for (auto &box : verifyBank.boxes)
                    for (auto &slot : box)
                        slot.reset();
                verifyBank.boxes[0][0] = std::move(tamperedPokemon);
                verifyBank.save();
                context.check(verifyBank.lastVerifyFailures() == 1, "the bank's save-time verification passed a record the next load drops", sample.origin + " failures=" + std::to_string(verifyBank.lastVerifyFailures()));
                std::remove((bankDirectory + "/bank.dat.damaged").c_str());
                break;
            }

            Trainer::Bank::setRootOverride("");
            context.check(!Trainer::Bank::rootOverridden(), "the bank path override was not cleared after the test");
            std::remove((bankDirectory + "/bank.dat").c_str());
        }
    }

    int bankPlan(Context &)
    {
        return ChunkCount;
    }

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
