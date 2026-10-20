/**
 * WHY THIS EXISTS. The browser had a real defect that every other kind of test was structurally
 * incapable of seeing: it filtered on `.sav`, `.srm`, `.dat` and `.sgm` alone, so a Gen 6 or Gen 7
 * 3DS save -- every one of which is called `main`, with no extension at all -- was never offered.
 * The bytes were fine, `openExternalSave` opened them, the self test's own folder scan found them;
 * the picker simply did not list them. Eight of the twenty-seven openable saves in a real
 * per-generation collection were invisible. A logic test cannot catch that, because no logic was
 * wrong. Only driving the dialog and looking at what it produced would have.
 *
 * IT BUILDS ITS OWN FIXTURE rather than listing whatever is on the card. A test that pointed at a
 * real directory could only assert "something appeared", which is the assertion that already
 * passed while the bug shipped. So it writes one file per REAL-WORLD save filename -- the names
 * emulators, cartridge dumpers and 3DS save managers actually produce -- plus the save-adjacent
 * files that must NOT be offered, and then asserts the listing exactly.
 *
 * THE `domain` ROW IS NOT FILLER. `main` has to be matched as a WHOLE NAME; matching it as a
 * suffix would be one character shorter to write and would also swallow `domain`, `remain` and
 * anything else ending in those four letters. That row is what fails if someone simplifies the
 * rule back.
 *
 * NOTHING THE USER OWNS IS TOUCHED. The fixture is created inside the run's own scratch directory
 * and every file it makes is removed again by name -- no recursive delete, so a wrong path cannot
 * take anything with it. The browser is opened on that directory only, never on the SD root.
 *
 * HOST-BUILDABLE ON PURPOSE. FileBrowserDialog reaches the filesystem through dirent and stat and
 * needs no Switch SDK, so this suite runs BOTH in the on-console pass and in the uihost harness --
 * which is what makes it a fast loop rather than something only a console can answer.
 */
#include "Test/TestSuite.h"

#include <sys/stat.h>
#include <unistd.h>    // rmdir
#include <cstdio>
#include <string>
#include <vector>

#include "Save/SaveFileName.h"
#include <cctype>

#include "UI/Dialogs/FileBrowserDialog.h"

namespace Test
{
    namespace
    {
        /// A fixture row: the name to create, and whether the browser must offer it.
        struct FixtureFile
        {
            const char *fileName;
            bool shouldBeListed;
            const char *why;
        };

        /// Every name here is one a real save is actually called. The listed ones come from a
        /// per-generation collection of real dumps; the unlisted ones are the files that sit
        /// beside them in the same folders.
        constexpr FixtureFile FIXTURE[] = {
            // must be offered
            {"main", true, "Gen 6 / Gen 7 3DS -- no extension at all"},
            {"sav.dat", true, "Gen 1 and Gen 2 virtual console"},
            {"00000001.sav", true, "Gen 3 GBA, as mGBA names it"},
            {"Pokemon Black.sav", true, "Gen 4 and Gen 5 DS dumps"},
            {"POKEMON_P.SAV", true, "upper-cased extension"},
            {"game.srm", true, "the other common emulator extension"},
            {"game.sgm", true, "and the third"},
            {"Pokemon Platinum.dsv", true, "DeSmuME and DraStic, a footer after the DS save"},
            {"Pokemon Platinum.duc", true, "an Action Replay DS dump, a header before the DS save"},
            {"Pokemon Pearl.dss", true, "an Action Replay MAX Drive DS dump"},
            {"Pokemon Ruby.fla", true, "a flash dump, an extension PKHeX lists"},
            {"Pokemon Crystal.SaveRAM", true, "BizHawk, in its own casing"},
            {"Pokemon Ruby.sa2", true, "mGBA's second multiplayer save, TGB Dual's second slot"},
            {"Pokemon Gold.srm2", true, "Gearboy's link mode, the second Game Boy"},
            {"Pokemon HeartGold.sav3", true, "a TWiLight Menu++ save slot"},
            // must NOT be offered
            {"Pokemon HeartGold.sav10", false, "TWiLight has nine slots; .sav1 is not a prefix match"},
            {"Pokemon Gold.rtc", false, "the clock a Gambatte or SameBoy save keeps beside it"},
            {"domain", false, "ends in `main`: proves the whole-name match, not a suffix"},
            {"main2", false, "Legends: Arceus writes one beside its save"},
            {"poke_trade", false, "Sword/Shield and Scarlet/Violet sibling"},
            {"backup", false, "the same"},
            {"savedata.bin", false, "Let's Go -- a Switch title, opened by title id"},
            {"readme.txt", false, "an ordinary file that happens to be in the folder"},
        };

        constexpr const char *FIXTURE_SUBDIRECTORY = "subfolder";
        /// The upper-cased 3DS save, from a dumper that capitalises names -- kept in the SUBFOLDER, not
        /// beside `main`. The console's SD card is FAT/exFAT, which folds case: creating `MAIN` next to
        /// `main` reopens the same file, so the folder held one entry where the fixture counted two,
        /// and the run reported a real save the browser "does not offer" plus a "show all files" that
        /// "still hid something". A case-sensitive host filesystem made both files, so only the console
        /// ever failed. Here the name is checked on a real devoptab without colliding with anything.
        constexpr const char *FIXTURE_SUBDIRECTORY_FILE = "MAIN";

        bool writeEmptyFile(const std::string &path)
        {
            FILE *fileHandle = fopen(path.c_str(), "wb");
            if (!fileHandle)
            {
                return false;
            }
            // One byte, so a zero-length file cannot be mistaken for a failed create.
            fputc(0, fileHandle);
            fclose(fileHandle);
            return true;
        }

        /// True when `entries` holds a row with this exact name.
        bool isListed(const UI::Dialogs::FileBrowserState &browser, const std::string &fileName)
        {
            for (const auto &entry : browser.entries)
            {
                if (entry.name == fileName)
                {
                    return true;
                }
            }
            return false;
        }

        void removeFixture(const std::string &fixtureDirectory)
        {
            for (const FixtureFile &fixture : FIXTURE)
            {
                remove((fixtureDirectory + "/" + fixture.fileName).c_str());
            }
            remove((fixtureDirectory + "/" + FIXTURE_SUBDIRECTORY + "/" + FIXTURE_SUBDIRECTORY_FILE).c_str());
            rmdir((fixtureDirectory + "/" + FIXTURE_SUBDIRECTORY).c_str());
            rmdir(fixtureDirectory.c_str());
        }
    }

    int fileBrowserPlan(Context &) { return 1; }

    void fileBrowserRun(Context &context, int)
    {
        if (context.workDirectory.empty())
        {
            context.skip("no scratch directory, so the file browser was not driven");
            return;
        }

        // NO TWO FIXTURE NAMES MAY DIFFER ONLY BY CASE. The console's SD card folds case, so such a
        // pair is ONE file there and the listing checks below fail for a reason that has nothing to do
        // with the browser -- while a case-sensitive host passes. Checked here so it fails everywhere.
        const auto caseFolded = [](std::string fileName)
        {
            for (char &character : fileName)
            {
                character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            }
            return fileName;
        };
        const int fixtureRowCount = static_cast<int>(sizeof(FIXTURE) / sizeof(FIXTURE[0]));
        for (int firstRow = 0; firstRow < fixtureRowCount; ++firstRow)
        {
            for (int secondRow = firstRow + 1; secondRow < fixtureRowCount; ++secondRow)
            {
                context.check(caseFolded(FIXTURE[firstRow].fileName) != caseFolded(FIXTURE[secondRow].fileName), "two file-browser fixture names differ only by case, which the SD card stores as one file", std::string("names=\"") + FIXTURE[firstRow].fileName + "\",\"" + FIXTURE[secondRow].fileName + "\"");
            }
        }

        const std::string fixtureDirectory = context.workDirectory + "/browsertest";
        removeFixture(fixtureDirectory); // a previous run that was interrupted must not change this one
        mkdir(fixtureDirectory.c_str(), 0777);
        mkdir((fixtureDirectory + "/" + FIXTURE_SUBDIRECTORY).c_str(), 0777);

        int createdCount = 0;
        for (const FixtureFile &fixture : FIXTURE)
        {
            if (writeEmptyFile(fixtureDirectory + "/" + fixture.fileName))
            {
                ++createdCount;
            }
        }
        const std::string subdirectoryPath = fixtureDirectory + "/" + FIXTURE_SUBDIRECTORY;
        const bool subdirectoryFileMade = writeEmptyFile(subdirectoryPath + "/" + FIXTURE_SUBDIRECTORY_FILE);

        const int fixtureCount = static_cast<int>(sizeof(FIXTURE) / sizeof(FIXTURE[0]));
        if (!context.check(createdCount == fixtureCount && subdirectoryFileMade, "could not build the file-browser fixture", "dir=\"" + fixtureDirectory + "\" created=" + std::to_string(createdCount) + " of " + std::to_string(fixtureCount)))
        {
            removeFixture(fixtureDirectory);
            return;
        }

        UI::Dialogs::FileBrowserState browser;

        // This is the check the shipped bug would have failed: `main` is a save and was not listed.
        browser.open("Open a Save File", "Save files", {fixtureDirectory}, Save::saveFileExtensions(), Save::saveFileExactNames());
        context.check(browser.directory == fixtureDirectory, "the browser did not land in the fixture directory", "wanted=\"" + fixtureDirectory + "\" got=\"" + browser.directory + "\"");

        for (const FixtureFile &fixture : FIXTURE)
        {
            const bool fixtureListed = isListed(browser, fixture.fileName);
            if (fixture.shouldBeListed)
            {
                context.check(fixtureListed, "the file browser does not offer a real save file", std::string("name=\"") + fixture.fileName + "\" why=\"" + fixture.why + "\"");
            }
            else
            {
                context.check(!fixtureListed, "the file browser offers a file that is not a save", std::string("name=\"") + fixture.fileName + "\" why=\"" + fixture.why + "\"");
            }
        }

        // TWO MATCHERS READ THE SAME TWO LISTS. The browser filters `entries` with its own loop --
        // it is a generic dialog and also lists `.bnk` files -- while Save::isSaveFileName is what
        // the self test's title-folder scan asks. Sharing the LISTS stops them disagreeing about
        // WHICH names count; nothing but this stops them disagreeing about HOW they are matched,
        // which is exactly how the picker and the self test drifted apart in the first place.
        for (const FixtureFile &fixture : FIXTURE)
        {
            context.check(Save::isSaveFileName(fixture.fileName) == fixture.shouldBeListed, "Save::isSaveFileName disagrees with what the file browser offers", std::string("name=\"") + fixture.fileName + "\" browser=" + (fixture.shouldBeListed ? "offers" : "hides") + " rule=" + (Save::isSaveFileName(fixture.fileName) ? "save" : "not a save"));
        }
        context.check(Save::isSaveFileName(FIXTURE_SUBDIRECTORY_FILE), "Save::isSaveFileName does not accept an upper-cased 3DS save name", std::string("name=\"") + FIXTURE_SUBDIRECTORY_FILE + "\"");

        // A filter that hid folders would make every save in a subdirectory unreachable.
        context.check(isListed(browser, FIXTURE_SUBDIRECTORY), "the file browser hid a directory because it did not match the file filter", std::string("name=\"") + FIXTURE_SUBDIRECTORY + "\"");
        context.check(isListed(browser, ".."), "the file browser offered no way back out of a subdirectory");

        // The escape hatch that made the bug survivable, so it has to keep working.
        browser.showAllFiles = true;
        browser.refresh();
        int hiddenStillMissing = 0;
        for (const FixtureFile &fixture : FIXTURE)
        {
            if (!isListed(browser, fixture.fileName))
            {
                ++hiddenStillMissing;
            }
        }
        context.check(hiddenStillMissing == 0, "\"show all files\" still hid something", "missing=" + std::to_string(hiddenStillMissing));
        browser.showAllFiles = false;
        browser.refresh();

        const int subdirectoryRow = [&]
        {
            for (int rowIndex = 0; rowIndex < static_cast<int>(browser.entries.size()); ++rowIndex)
            {
                if (browser.entries[rowIndex].name == FIXTURE_SUBDIRECTORY)
                {
                    return rowIndex;
                }
            }
            return -1;
        }();
        if (context.check(subdirectoryRow >= 0, "the fixture subdirectory vanished from the listing"))
        {
            browser.selectedIndex = subdirectoryRow;
            const bool enteredReportedChosen = browser.activate();
            context.check(!enteredReportedChosen, "entering a directory reported a file had been chosen");
            context.check(browser.directory != fixtureDirectory, "activating a directory did not enter it", "still=\"" + browser.directory + "\"");
            context.check(isListed(browser, FIXTURE_SUBDIRECTORY_FILE), "the save inside the subdirectory was not listed", std::string("name=\"") + FIXTURE_SUBDIRECTORY_FILE + "\" why=\"an upper-cased 3DS save name\"");

            context.check(browser.goUp(), "the browser could not go back up out of a subdirectory");
            context.check(browser.directory == fixtureDirectory, "going up did not return to the fixture directory", "got=\"" + browser.directory + "\"");
            context.check(browser.entries[browser.selectedIndex].name == FIXTURE_SUBDIRECTORY, "going up did not put the cursor back on the folder just left", "row=\"" + browser.entries[browser.selectedIndex].name + "\"");
        }

        for (int rowIndex = 0; rowIndex < static_cast<int>(browser.entries.size()); ++rowIndex)
        {
            if (browser.entries[rowIndex].name == std::string("main"))
            {
                browser.selectedIndex = rowIndex;
                break;
            }
        }
        context.check(browser.chosenPath.empty(), "the browser had already chosen a path before A was pressed");
        const bool choseFile = browser.activate();
        context.check(choseFile, "activating a save file did not choose it");
        context.check(browser.chosenPath == fixtureDirectory + "/main", "the browser reported the wrong path for the chosen file", "got=\"" + browser.chosenPath + "\"");

        browser.move(-1000);
        context.check(browser.selectedIndex == 0, "moving up past the top left the cursor out of range", "index=" + std::to_string(browser.selectedIndex));
        browser.move(1000);
        context.check(browser.selectedIndex == static_cast<int>(browser.entries.size()) - 1, "moving down past the end left the cursor out of range", "index=" + std::to_string(browser.selectedIndex));

        browser.close();
        context.check(!browser.active, "closing the browser left it active");
        context.check(browser.chosenPath.empty(), "closing the browser left a chosen path behind");

        removeFixture(fixtureDirectory);
    }
}
