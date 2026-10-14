#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>

#include <switch.h>

#include "Test/TestRunner.h"
#include "Test/TestSuite.h"
#include "Globals.h"
#include "Utils/Keyboard.h" // resetKeyboardSuppressionCount -- no library applets during a run
#include "Save/GetSaveFileContents.h"
#include "Trainer/Trainer.h"
#include "Trainer/Trainer1RBY.h"
#include "Trainer/Bank.h"
#include "Utils/FileUtilities.h"
#include "Utils/Logger.h"
#include "Save/SaveFileName.h"  // isSaveFileName -- shared with the file browser

namespace Test
{

    namespace
    {
        // Everything the run creates lives under here. One tree, easy to delete, and nowhere near
        // the user's backups or their bank.
        const std::string TEST_ROOT = BASE_SAVE_DIRECTORY + "/tests";
        const std::string WORK_ROOT = BASE_SAVE_DIRECTORY + "/tests/work";

        // Where a loose pre-Switch save is looked for. ONE place, and only its per-title folders.
        // Sweeping sdmc:/PKSE/saves and sdmc:/PKSE too, and accepting bare files at the top of any of
        // them, makes the plan unpredictable: a stray .sav anywhere near PKSE's folder becomes a
        // candidate for whichever title its bytes could belong to. What is in the title folder is the
        // whole answer -- if it is not in tests/saves/<Title>/, it is not in the run, no matter what
        // else is on the card.
        const char *const LOOSE_SAVE_DIRECTORIES[] = {
            "sdmc:/PKSE/tests/saves",
        };

        void ensureDir(const std::string &path)
        {
            if (mkdir(path.c_str(), 0777) != 0 && errno != EEXIST)
                Utils::logErrorToFile("SelfTest: mkdir failed", path.c_str());
        }

        /// Files that WERE in a title folder and still did not become a candidate, with why.
        ///
        /// "No save file found" is the wrong thing to tell someone staring at a folder with a file
        /// in it. Three things put a file here: it is not a save PKSE can read at all (a 512 KB
        /// block of zeros, which is what an unwritten Gen 4 slot looks like), it is a save of a
        /// different format than its folder claims, or it parsed but produced nothing. The row for
        /// that title shows the reason instead of pretending the folder was empty.
        std::vector<std::pair<Enums::GameVersion, std::string>> gRejectedSaves;

        bool dirExists(const std::string &path)
        {
            struct stat entryStatus{};
            return stat(path.c_str(), &entryStatus) == 0 && S_ISDIR(entryStatus.st_mode);
        }

        std::string nowStamp()
        {
            const time_t nowSeconds = time(nullptr);
            char buffer[32];
            strftime(buffer, sizeof(buffer), "%Y-%m-%d_%H-%M-%S", localtime(&nowSeconds));
            return std::string(buffer);
        }

        std::string nowHuman()
        {
            const time_t nowSeconds = time(nullptr);
            char buffer[32];
            strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", localtime(&nowSeconds));
            return std::string(buffer);
        }

        /// A short filesystem-safe tag for a save-format group, used to name its scratch directory.
        const char *groupTag(Enums::GameVersion group)
        {
            switch (group)
            {
            case Enums::GameVersion::RBY:
                return "rby";
            case Enums::GameVersion::GSC:
                return "gsc";
            case Enums::GameVersion::DP:
                return "dp";
            case Enums::GameVersion::PT:
                return "pt";
            case Enums::GameVersion::HGSS:
                return "hgss";
            case Enums::GameVersion::BW:
                return "bw";
            case Enums::GameVersion::B2W2:
                return "b2w2";
            case Enums::GameVersion::XY:
                return "xy";
            case Enums::GameVersion::ORAS:
                return "oras";
            case Enums::GameVersion::SM:
                return "sm";
            case Enums::GameVersion::USUM:
                return "usum";
            case Enums::GameVersion::RSE:
                return "rse";
            case Enums::GameVersion::FRLG:
                return "frlg";
            case Enums::GameVersion::GG:
                return "gg";
            case Enums::GameVersion::SWSH:
                return "swsh";
            case Enums::GameVersion::BDSP:
                return "bdsp";
            case Enums::GameVersion::PLA:
                return "pla";
            case Enums::GameVersion::SV:
                return "sv";
            case Enums::GameVersion::ZA:
                return "za";
            default:
                return "unknown";
            }
        }


        /// Where `title` sits in TESTED_TITLES, or -1 for a title the run does not cover. This is
        /// the run order and the plan-screen row order, so it is also what targets sort by.
        int titleOrder(Enums::GameVersion title) noexcept
        {
            for (int index = 0; index < (int)(sizeof(TESTED_TITLES) / sizeof(TESTED_TITLES[0])); ++index)
                if (TESTED_TITLES[index] == title)
                    return index;
            return -1;
        }

        /// A title name flattened for comparison: lowercase, with everything that is only
        /// punctuation or spacing removed. "HeartGold", "heartgold", "Heart Gold" and "heart-gold"
        /// all come out "heartgold", and "Legends: Z-A" comes out "legendsza". Folder names are
        /// typed by a person, so they must not have to match a colon or a space exactly.
        std::string flattenTitleName(const std::string &name)
        {
            std::string out;
            for (unsigned char character : name)
                if (std::isalnum(character))
                    out += static_cast<char>(std::tolower(character));
            return out;
        }

        /// The title a save DIRECTORY names, or Invalid when the folder is not a title name.
        ///
        /// Four pairs of games share a save layout with no version byte anywhere in the file --
        /// Red/Blue, Gold/Silver, Diamond/Pearl, HeartGold/SoulSilver -- so a save that came out of
        /// one of them can never say which it was. The only thing that knows is the person who
        /// dumped it, and a folder is where they can write it down. `<saves>/SoulSilver/` says
        /// SoulSilver; the bytes cannot.
        ///
        /// Matched against the names PKSE already prints for those titles, so there is no second
        /// table of folder spellings to keep in step -- what the report calls a game is what its
        /// folder is called. That requires every title's flattened name to stay DISTINCT and
        /// non-empty; all 35 currently are, and a new title whose name flattens onto an existing
        /// one would silently resolve its folder to whichever comes first in TESTED_TITLES.
        Enums::GameVersion titleFromFolderName(const std::string &folderName)
        {
            std::string wanted = flattenTitleName(folderName);
            if (wanted.empty())
                return Enums::GameVersion::Invalid;

            auto lookup = [](const std::string &flattened) -> Enums::GameVersion
            {
                for (Enums::GameVersion title : TESTED_TITLES)
                    if (flattenTitleName(Enums::getGameVersionName(title)) == flattened)
                        return title;
                return Enums::GameVersion::Invalid;
            };

            const Enums::GameVersion direct = lookup(wanted);
            if (direct != Enums::GameVersion::Invalid)
                return direct;

            // "Pokemon Yellow" is what people actually type -- it is how the games are titled and
            // how the save files that come out of dumpers are named. No title PKSE knows begins
            // with "Pokemon", so dropping that prefix cannot collide with anything, and the match
            // that follows is still exact ("pokemonsoulsilver" -> "soulsilver", never a substring
            // hit against "Silver"). Making the user rename a folder whose intent is unmistakable
            // is not a rule worth having.
            constexpr const char *titleFolderPrefix = "pokemon";
            if (wanted.rfind(titleFolderPrefix, 0) == 0)
                return lookup(wanted.substr(std::char_traits<char>::length(titleFolderPrefix)));
            return Enums::GameVersion::Invalid;
        }

    }

    std::string Target::display() const
    {
        // The TITLE leads, always: this string is the report's target column and every finding's
        // `target=`, and those have to say which game. `label` is the file the save came out of, which
        // matters only when there is more than one candidate, so it rides in parentheses. Label alone is
        // whatever openExternalSave called the save, which for Gen 1 is the GROUP name -- a Red save
        // reporting its target as "Pokemon Red/Blue" on a row already headed "Red".
        const std::string titleName = Enums::getGameVersionName(version);
        return label.empty() ? titleName : titleName + "  (" + label + ")";
    }


    bool comesFromLooseFile(Enums::GameVersion title) noexcept
    {
        // Everything before the Switch arrives as a FILE the user copies onto the card -- Gens 1
        // and 2 as a Game Boy .sav, Gen 3's Hoenn games as a GBA one, Gens 4 and 5 as a DS one,
        // Gens 6 and 7 as a 3DS `main`. The Switch titles arrive as installed save data and there
        // is nothing to copy.
        switch (Enums::getVersionGeneration(static_cast<uint8_t>(title)))
        {
        case 3:
            // Gen 3 is both, exactly as Gen 7 is below: Ruby, Sapphire and Emerald are GBA files
            // the user copies across, while FireRed and LeafGreen are an installed Switch title.
            return title != Enums::GameVersion::FR && title != Enums::GameVersion::LG;
        case 1:
        case 2:
        case 4:
        case 5:
        case 6:
            return true;
        case 7:
            // Gen 7 is both: Sun/Moon and Ultra Sun/Ultra Moon are 3DS files, while Let's Go is an
            // installed Switch title sharing the generation number.
            return title != Enums::GameVersion::GP && title != Enums::GameVersion::GE;
        default:
            return false;
        }
    }

    // Thin accessors over the shared array in TestSuite.h, kept because the UI and the report
    // already call them. The array itself had to leave this file: SuiteTables checks that every
    // tested title resolves to a save-format group, and the host harness runs that suite WITHOUT
    // linking TestRunner.cpp -- which reaches dirent, the SD card and the Switch filesystem.
    const Enums::GameVersion *testedTitles() noexcept { return TESTED_TITLES; }
    int testedTitleCount() noexcept { return TESTED_TITLE_COUNT; }

    const char *const *Runner::looseScanDirs() noexcept { return LOOSE_SAVE_DIRECTORIES; }
    int Runner::looseScanDirCount() noexcept
    {
        return (int)(sizeof(LOOSE_SAVE_DIRECTORIES) / sizeof(LOOSE_SAVE_DIRECTORIES[0]));
    }

    namespace
    {
        /// Append every Pokemon save in one save-data space. Deliberately a compressed copy of
        /// SaveSelectScreen::scanSaveSpace rather than a call into it: the picker asks "which saves
        /// does THIS user have", and the self test wants every save on the console regardless of
        /// which account owns it, because whichever account has a Gen 8 save is the one that gets to
        /// represent Gen 8.
        void scanSaveSpace(int spaceId, std::vector<Target> &out)
        {
            FsSaveDataInfoReader reader;
            if (R_FAILED(fsOpenSaveDataInfoReader(&reader, static_cast<FsSaveDataSpaceId>(spaceId))))
                return;

            FsSaveDataInfo info[24];
            s64 readCount = 0;
            while (R_SUCCEEDED(fsSaveDataInfoReaderRead(&reader, info, 24, &readCount)) && readCount > 0)
            {
                for (s64 infoIndex = 0; infoIndex < readCount; infoIndex++)
                {
                    if (info[infoIndex].save_data_type != FsSaveDataType_Account)
                        continue;

                    const uint64_t titleId = info[infoIndex].application_id;
                    const Enums::GameVersion gameVersion = Enums::getGameVersion(titleId);
                    if (gameVersion == Enums::GameVersion::Invalid)
                        continue;
                    if (!Enums::isGameSupported(gameVersion))
                        continue;

                    bool alreadyListed = false;
                    for (const auto &discoveredTarget : out)
                        if (discoveredTarget.titleId == titleId)
                        {
                            alreadyListed = true;
                            break;
                        }
                    if (alreadyListed)
                        continue;

                    // The title id IS the title for a Switch save -- Sword and Shield, Scarlet
                    // and Violet, FireRed and LeafGreen are each their own id -- so nothing here
                    // has to guess, and no pair has to be collapsed.
                    Target discoveredTarget;
                    discoveredTarget.version = gameVersion;
                    discoveredTarget.group = Enums::getGameGroup(gameVersion);
                    discoveredTarget.label = Enums::getGameVersionName(gameVersion);
                    discoveredTarget.titleId = titleId;
                    discoveredTarget.accountUid = info[infoIndex].uid;
                    if (titleOrder(discoveredTarget.version) >= 0)
                        out.push_back(std::move(discoveredTarget));
                }
            }
            fsSaveDataInfoReaderClose(&reader);
        }

        /// Look for a loose pre-Switch save -- Gens 1, 2 and 4-7. Candidates go through the SAME
        /// probe chain the manual file browser uses, so what the self test accepts and what the
        /// picker accepts cannot drift apart.
        ///
        /// Each scan directory is read at its top level AND one level down, because a subdirectory
        /// named after a title is how the user says which game a save came from. That matters for
        /// the four pairs whose saves carry no version byte at all: a Diamond save and a Pearl save
        /// are the same bytes, so without a folder the run has to pick one and mark the other as
        /// covered by it. `<saves>/Pearl/` settles it. One level only -- deeper nesting is a save
        /// organised by something other than its title, and guessing at what would put the run back
        /// to inferring the answer.
        void scanLooseSaves(std::vector<Target> &out)
        {
            /// One candidate file: read it, construct it, and add it if it is a save PKSE handles.
            /// `folderTitle` is the title its directory claims, or Invalid at the top level.
            auto considerFile = [&out](const std::string &directory, const std::string &name,
                                       Enums::GameVersion folderTitle)
            {
                // The SAME rule the file browser offers files by -- a 3DS `main` has no extension
                // at all and is matched by name. Shared rather than restated: these two had already
                // drifted once, and the picker was the copy missing `main`.
                if (!Save::isSaveFileName(name))
                    return;

                const std::string path = directory + "/" + name;
                size_t length = 0;
                uint8_t *raw = Utils::readAllBytes(path.c_str(), &length);
                if (!raw)
                    return;
                std::vector<uint8_t> bytes(raw, raw + length);
                delete[] raw;

                // Construct it, not merely detect it: a file that passes detect() but produces
                // an empty editor is exactly the case the picker warns about, and a test run
                // should not begin by failing to open its own subject.
                std::string label;
                auto probe = Save::openExternalSave(std::move(bytes), path, &label);
                if (!probe)
                {
                    if (folderTitle != Enums::GameVersion::Invalid)
                        gRejectedSaves.emplace_back(folderTitle, name + " is not a save PKSE can read" +
                                                                     (length == 0 ? " (empty file)" : ""));
                    return;
                }

                // The trainer names its own title. Gens 5, 6 and 7 read a real game byte out of
                // the save, so Black and White land on separate rows. The four pairs that carry no
                // such byte report the FIRST of the pair, which the folder below then overrides --
                // that folder is the only thing that can ever say a save is Blue rather than Red.
                Target target;
                target.group = probe->getGameGroup();
                target.version = probe->getGameVersion();
                // The FILE, not the game. The plan row already prints the title in its own column,
                // so repeating it here read as "Red: Pokemon Red/Blue" -- the group name, which is
                // the one thing the row is trying not to say. What the user cannot see from the row
                // is WHICH file this is, and that is what has to distinguish two candidates.
                target.label = name;
                target.sourcePath = path;

                // THE FOLDER IS AUTHORITATIVE. A file under <Title>/ is that title's save or it is nothing --
                // it can never end up as a candidate for some other title. Keeping the DETECTED version where the
                // folder's game and the file's format disagree sounds more careful and is worse: the user put the
                // file in that folder, and the result is a save filed under Silver turning up as a second
                // candidate on the Red row, which is unexplainable from the outside. Refuse it and say why.
                if (folderTitle == Enums::GameVersion::Invalid)
                    return; // only files inside a title folder are candidates at all
                if (Enums::getGameGroup(folderTitle) != target.group)
                {
                    Utils::logInfoToFile(
                        "SelfTest: ignoring a save filed under the wrong title",
                        (path + " is not a " + Enums::getGameVersionName(folderTitle) + " save").c_str());
                    gRejectedSaves.emplace_back(folderTitle, name + " is a " + Enums::getGameVersionName(target.group) +
                                                                 " save, not " +
                                                                 Enums::getGameVersionName(folderTitle));
                    return;
                }
                target.version = folderTitle;

                if (titleOrder(target.version) >= 0)
                    out.push_back(std::move(target));
            };

            for (int looseScanDirIndex = 0; looseScanDirIndex < Runner::looseScanDirCount(); ++looseScanDirIndex)
            {
                const std::string scanDir = LOOSE_SAVE_DIRECTORIES[looseScanDirIndex];
                DIR *dir = opendir(scanDir.c_str());
                if (!dir)
                    continue;

                struct dirent *entry;
                while ((entry = readdir(dir)) != nullptr)
                {
                    const std::string name(entry->d_name);
                    if (name == "." || name == "..")
                        continue;

                    // d_type is NOT filled in by every devoptab -- the SD card's is one that leaves
                    // it DT_UNKNOWN -- so a title folder tested with `d_type == DT_DIR` reads as a
                    // file, fails the extension check inside considerFile(), and is silently never
                    // descended into. Every per-title folder on the card was invisible.
                    // FileBrowserDialog already carries this fallback with the same comment; this
                    // did not, which is why the folder feature worked on the host harness (its own
                    // SaveTree walks with stat) and not on hardware.
                    const std::string fullPath = scanDir + "/" + name;
                    const bool entryIsDirectory = (entry->d_type == DT_DIR) ||
                                                  (entry->d_type == DT_UNKNOWN && dirExists(fullPath));
                    if (entryIsDirectory)
                    {
                        // Only a directory NAMED after a title is descended into. Everything else
                        // under the scan roots -- work/, the report files, whatever else the card
                        // holds -- is left alone, so this stays a scan of saves the user filed
                        // rather than a walk of the SD card.
                        const Enums::GameVersion folderTitle = titleFromFolderName(name);
                        // not a title PKSE tests -- someone else's folder, left alone
                        if (folderTitle == Enums::GameVersion::Invalid) continue;
                        DIR *inner = opendir(fullPath.c_str());
                        if (!inner)
                            continue;
                        struct dirent *innerEntry;
                        while ((innerEntry = readdir(inner)) != nullptr)
                        {
                            const std::string innerName(innerEntry->d_name);
                            if (innerName == "." || innerName == "..")
                                continue;
                            if (innerEntry->d_type == DT_DIR ||
                                // one level deep only
                                (innerEntry->d_type == DT_UNKNOWN && dirExists(fullPath + "/" + innerName))) continue;
                            considerFile(fullPath, innerName, folderTitle);
                        }
                        closedir(inner);
                    }
                    // A bare file at the top of the scan root is deliberately NOT a candidate. It
                    // cannot say which title it is -- that is the whole reason the folders exist --
                    // and accepting it put saves in the plan that the user had not filed anywhere.
                }
                closedir(dir);
            }
        }
    }

    /// Create one folder per title whose save is a LOOSE FILE, under the documented scan root.
    ///
    /// Folder names the user has to GUESS are names they guess wrong -- "Pokemon Yellow" is the
    /// obvious try, and a name that matches nothing looks exactly like a missing save. Putting the
    /// folders on the card makes the answer the thing the user is already looking at: open
    /// sdmc:/PKSE/tests/saves and every name is there, spelled the way the runner reads it. Drop a
    /// save in, any filename.
    ///
    /// Only loose-file titles get one. A Switch title's save comes from installed save data by title
    /// id, so a `Sword/` folder would invite a file that is never read.
    ///
    /// mkdir on an existing directory is EEXIST, which ensureDir ignores, so this is safe to run on
    /// every open and never disturbs a folder the user has already filled.
    std::string Runner::rejectedSaveReason(Enums::GameVersion title)
    {
        for (const auto &entry : gRejectedSaves)
            if (entry.first == title)
                return entry.second;
        return {};
    }

    static void ensureTitleSaveFolders()
    {
        ensureDir(BASE_SAVE_DIRECTORY);
        ensureDir(TEST_ROOT);
        ensureDir(LOOSE_SAVE_DIRECTORIES[0]);
        for (Enums::GameVersion title : TESTED_TITLES)
            if (comesFromLooseFile(title))
                ensureDir(std::string(LOOSE_SAVE_DIRECTORIES[0]) + "/" + Enums::getGameVersionName(title));
    }

    std::vector<Target> Runner::discover()
    {
        std::vector<Target> out;
        // Before scanning, not after: the folders are what the user fills in, so they have to exist
        // the first time the self test is opened rather than the second.
        ensureTitleSaveFolders();
        gRejectedSaves.clear();
        scanLooseSaves(out);

        // FsSaveDataSpaceId_All is the pseudo-space that normally answers for everything; the two
        // concrete spaces are the fallback, exactly as the picker does it.
        const size_t before = out.size();
        scanSaveSpace(FsSaveDataSpaceId_All, out);
        if (out.size() == before)
        {
            scanSaveSpace(FsSaveDataSpaceId_User, out);
            scanSaveSpace(FsSaveDataSpaceId_SdUser, out);
        }

        // Release order, so the candidates for a title sit together and index 0 of a title's run
        // is the default pick. A stable insertion sort on purpose: where a title somehow has two
        // saves (an installed copy and a loose file of the same game), the one discovered first
        // stays first rather than being reordered arbitrarily between runs.
        for (size_t outIndex = 1; outIndex < out.size(); ++outIndex)
        {
            Target key = std::move(out[outIndex]);
            const int keyOrder = titleOrder(key.version);
            size_t insertIndex = outIndex;
            while (insertIndex > 0 && titleOrder(out[insertIndex - 1].version) > keyOrder)
            {
                out[insertIndex] = std::move(out[insertIndex - 1]);
                --insertIndex;
            }
            out[insertIndex] = std::move(key);
        }

        char message[96];
        snprintf(message, sizeof(message), "SelfTest: discovered %d candidate save(s)", (int)out.size());
        Utils::logInfoToFile(message);
        return out;
    }

    struct Runner::Loaded
    {
        // Two holders, because the loose generations are not members of TrainerVariant: a file
        // save has no title id, so it never reaches Save::readTrainerInfo's title-id dispatch and
        // is constructed through the probe chain instead. Owned by base pointer because the
        // concrete class is not known until that chain runs.
        std::optional<Save::TrainerVariant> variant;
        std::unique_ptr<Trainer::Trainer> loose;
    };

    // Both defined here rather than in the header: `Loaded` is only complete in this file, and
    // destroying a unique_ptr to an incomplete type is a compile error at the point of use.
    Runner::Runner() = default;
    Runner::~Runner()
    {
        // The user gets their keyboard back no matter how the run ended. Clearing it only on the
        // WriteReport phase would leave swkbd dead for the rest of the session if a run were ever
        // abandoned before that -- a setting the user cannot see and cannot turn off.
        g_suppressKeyboard = false;
    }

    void Runner::start(std::vector<Target> targets, std::vector<Enums::GameVersion> missingTitles)
    {
        plan = std::move(targets);
        missingTitleList = std::move(missingTitles);
        runReport.reset();
        clearSamples();
        // Nothing may still be published as the driven screen once the trainer behind it is gone.
        // A suite that builds a screen owns tearing it down, but the runner is what frees the
        // trainer, so it gets the last word: a stale pointer here is a use-after-free in the very
        // next draw(), and that is a crash on the console only -- the host harness never draws.
        setDrivenScreen(nullptr);
        loaded.reset();
        trainer = nullptr;
        targetIdx = 0;
        suiteIdx = 0;
        chunkIdx = 0;
        chunkCount = -1;
        runStamp = nowStamp();
        outPath = TEST_ROOT + "/report_" + runStamp + ".log";
        uiTarget = "Starting";
        uiDetail.clear();

        ensureDir(BASE_SAVE_DIRECTORY);
        ensureDir(TEST_ROOT);
        ensureDir(WORK_ROOT);

        // Record the coverage gaps FIRST, so they sit at the top of the warnings rather than being
        // discovered at the bottom of a clean-looking report.
        runReport.setScope("-", "plan");
        std::string searched;
        for (int looseScanDirIndex = 0; looseScanDirIndex < looseScanDirCount(); ++looseScanDirIndex)
        {
            if (looseScanDirIndex)
                searched += ", ";
            searched += LOOSE_SAVE_DIRECTORIES[looseScanDirIndex];
        }
        for (Enums::GameVersion missing : missingTitleList)
        {
            Context context;
            context.rep = &runReport;
            const std::string title = Enums::getGameVersionName(missing);
            // The two gaps are not interchangeable, so they do not get one message. A loose-file
            // title is fixed by copying a save onto the card, and saying where is the whole point;
            // an installed title needs the game itself, and there is nothing the user can copy.
            if (comesFromLooseFile(missing))
                context.skip("title not tested: no save found",
                         "title=\"" + title + "\" searched=\"" + searched + "\"");
            else
                context.skip("title not tested: no save found",
                         "title=\"" + title + "\" reason=\"the game is not installed on this console\"");
        }
        for (const auto &t : plan)
        {
            Context context;
            context.rep = &runReport;
            context.note("title tested", t.display());
        }

        runState = State::Running;
        phase = Phase::GlobalSuites;
        cancelled = false;
        startedAt = (long)time(nullptr);
        finishedAt = startedAt;
        // No library applets for the length of the run. swkbd BLOCKS until a human types something,
        // so one prompt reached by a driven screen stops the pass dead and waits for hands on the
        // console. Sits beside the other two things a run makes unconditionally safe: nothing is
        // written to the user's saves, and the bank is redirected to a scratch directory.
        g_suppressKeyboard = true;
        Utils::resetKeyboardSuppressionCount();
        Utils::logInfoToFile("SelfTest: run started");
    }

    namespace
    {
        UI::PKSEFramebuffer *gFramebuffer = nullptr;
        UI::UIScreen *gDrivenScreen = nullptr;
        Runner::DriveCaption gDriveCaption;
        bool gYieldRequested = false;
    }

    void Runner::setFramebuffer(UI::PKSEFramebuffer *framebuffer) noexcept { gFramebuffer = framebuffer; }

    void Runner::setDrivenScreen(UI::UIScreen *screen) noexcept { gDrivenScreen = screen; }
    UI::UIScreen *Runner::drivenScreen() noexcept { return gDrivenScreen; }
    void Runner::setDriveCaption(const DriveCaption &caption) { gDriveCaption = caption; }
    const Runner::DriveCaption &Runner::driveCaption() noexcept { return gDriveCaption; }
    void Runner::yieldFrame() noexcept { gYieldRequested = true; }

    Context Runner::makeContext(const char *suiteName)
    {
        Context context;
        context.rep = &runReport;
        context.framebuffer = gFramebuffer;
        context.suite = suiteName;
        context.trainer = trainer;
        // A GLOBAL suite has no target and so no per-save scratch copy, but it may still need
        // somewhere writable -- the browser suite builds a fixture directory. Hand it the run's
        // own work root, which start() has already created, rather than leaving it empty and
        // having the suite invent a path of its own outside the tree the runner cleans up.
        context.workDir = workDir.empty() ? WORK_ROOT : workDir;
        context.savePath = workSavePath;
        if (targetIdx < plan.size() && (phase == Phase::TargetSuites || phase == Phase::TargetLoad))
        {
            context.target = plan[targetIdx].display();
            context.version = plan[targetIdx].version;
            context.group = plan[targetIdx].group;
            context.titleId = plan[targetIdx].titleId;
            context.accountUid = plan[targetIdx].accountUid;
        }
        else if (phase == Phase::CrossSuites)
        {
            context.target = "cross-title";
        }
        else
        {
            context.target = "-";
        }
        runReport.setScope(context.target, suiteName);
        return context;
    }

    bool Runner::advanceSuites(Scope scope)
    {
        while (suiteIdx < suiteCount())
        {
            const Suite &s = suites()[suiteIdx];
            if (s.scope != scope)
            {
                ++suiteIdx;
                chunkIdx = 0;
                chunkCount = -1;
                continue;
            }

            Context context = makeContext(s.name);
            if (chunkCount < 0)
            {
                chunkCount = s.plan(context);
                chunkIdx = 0;
                checksAtSuiteStart = runReport.checks();
                skipsAtSuiteStart = runReport.skips();
                if (chunkCount <= 0)
                {
                    // Every suite the runner reaches has what it needs (a per-target suite only runs
                    // once its save loaded), so a suite that plans nothing is a gap, not a pass.
                    context.skip("suite planned nothing to run");
                    ++suiteIdx;
                    chunkCount = -1;
                    continue;
                }
            }

            uiDetail = std::string(s.name) + "  " + std::to_string(chunkIdx + 1) + "/" + std::to_string(chunkCount);
            s.run(context, chunkIdx);
            ++chunkIdx;
            if (chunkIdx >= chunkCount)
            {
                if (runReport.checks() == checksAtSuiteStart && runReport.skips() == skipsAtSuiteStart)
                    context.skip("suite ran but recorded no checks and no skips",
                                 "chunks=" + std::to_string(chunkCount));
                ++suiteIdx;
                chunkIdx = 0;
                chunkCount = -1;
            }
            return true; // one chunk of work done
        }
        return false; // no suites of this scope left
    }

    void Runner::beginTarget()
    {
        const Target &t = plan[targetIdx];
        uiTarget = t.display();
        uiDetail = "loading";

        workDir = WORK_ROOT + "/" + groupTag(t.group);
        workSavePath.clear(); // set below for a loose target; an installed title has none
        // A stale copy left by an interrupted run would be tested instead of the real save. Only
        // attempted when it is actually there -- deleteDirectoryRecursive logs an error for a
        // missing directory, and "no leftovers" is the normal case, not a problem.
        if (dirExists(workDir))
            Utils::deleteDirectoryRecursive(workDir.c_str());
        ensureDir(WORK_ROOT);
        ensureDir(workDir);

        runReport.setScope(t.display(), "load");
        Context context;
        context.rep = &runReport;
        context.suite = "load";
        context.target = t.display();

        // Nothing may still be published as the driven screen once the trainer behind it is gone.
        // A suite that builds a screen owns tearing it down, but the runner is what frees the
        // trainer, so it gets the last word: a stale pointer here is a use-after-free in the very
        // next draw(), and that is a crash on the console only -- the host harness never draws.
        setDrivenScreen(nullptr);
        loaded = std::make_unique<Loaded>();
        trainer = nullptr;

        if (t.titleId != 0)
        {
            if (!context.check(Utils::copySaveDataTo(t.accountUid, t.titleId, workDir.c_str()),
                           "could not copy the live save into the test directory",
                           "titleId=" + std::to_string(t.titleId) + " dest=\"" + workDir + "\""))
            {
                phase = Phase::TargetDone;
                return;
            }
            loaded->variant.emplace(Save::readTrainerInfo(workDir.c_str(), t.titleId));
            trainer = std::visit([](auto &tr) -> Trainer::Trainer *
                                 { return &tr; }, *loaded->variant);
        }
        else
        {
            // A loose file: copy it into the scratch tree by name so every write test targets the
            // copy, never the file the user pointed us at.
            const size_t slashPos = t.sourcePath.find_last_of('/');
            const std::string fileName = (slashPos == std::string::npos) ? t.sourcePath
                                                                         : t.sourcePath.substr(slashPos + 1);
            // Prefixed with the title, because the source names are NOT unique once saves are filed
            // by game: every 3DS save is called `main`, and a Diamond and a Pearl dump usually carry
            // the same name too. Copying both to work/main would have the second silently land on
            // the first's bytes -- and since each target loads right after copying, that is the kind
            // of collision that only shows up as one game's save producing another game's findings.
            // flattened, because a title name is not a filename: "Black 2" has a space and
            // "Legends: Z-A" a colon, which FAT does not accept.
            const std::string destination =
                workDir + "/" + flattenTitleName(Enums::getGameVersionName(t.version)) + "_" + fileName;
            workSavePath = destination; // the write tests target exactly this file, never a guess
            if (!context.check(Utils::copyFile(t.sourcePath.c_str(), destination.c_str()),
                               "could not copy the loose save into the test directory",
                               "src=\"" + t.sourcePath + "\" dest=\"" + destination + "\""))
            {
                phase = Phase::TargetDone;
                return;
            }
            size_t length = 0;
            uint8_t *raw = Utils::readAllBytes(destination.c_str(), &length);
            if (!context.check(raw != nullptr, "copied save is unreadable", "path=\"" + destination + "\""))
            {
                phase = Phase::TargetDone;
                return;
            }
            std::vector<uint8_t> bytes(raw, raw + length);
            delete[] raw;
            loaded->loose = Save::openExternalSave(std::move(bytes), destination, nullptr);
            if (!context.check(loaded->loose != nullptr, "copied save no longer opens", "path=\"" + destination + "\""))
            {
                phase = Phase::TargetDone;
                return;
            }
            trainer = loaded->loose.get();
        }

        const bool passed = context.check(trainer != nullptr, "save did not produce a trainer");
        if (passed)
        {
            context.check(trainer->getGameGroup() == t.group,
                          "loaded trainer reports a different save-format group than the title says",
                          "expected=" + Enums::getGameVersionName(t.group) +
                              " actual=" + Enums::getGameVersionName(trainer->getGameGroup()));
        }
        if (!passed)
        {
            phase = Phase::TargetDone;
            return;
        }

        suiteIdx = 0;
        chunkIdx = 0;
        chunkCount = -1;
        phase = Phase::TargetSuites;
    }

    void Runner::releaseTarget()
    {
        // Nothing may still be published as the driven screen once the trainer behind it is gone.
        // A suite that builds a screen owns tearing it down, but the runner is what frees the
        // trainer, so it gets the last word: a stale pointer here is a use-after-free in the very
        // next draw(), and that is a crash on the console only -- the host harness never draws.
        setDrivenScreen(nullptr);
        loaded.reset();
        trainer = nullptr;
        // The scratch copy of a Switch save is up to ~2 MB and there is no reason to leave five of
        // them on the card. The report is the artefact worth keeping, not the working copies.
        if (!workDir.empty() && dirExists(workDir))
            Utils::deleteDirectoryRecursive(workDir.c_str());
        workDir.clear();
        workSavePath.clear();
    }

    void Runner::endTarget()
    {
        releaseTarget();
        ++targetIdx;
        // A checkpoint, so a crash in a later target still leaves everything up to here on the
        // card. The final write replaces it.
        writeReport(true);
        suiteIdx = 0;
        chunkIdx = 0;
        chunkCount = -1;
        phase = (targetIdx < plan.size()) ? Phase::TargetLoad : Phase::CrossSuites;
    }

    void Runner::writeReport(bool inProgress)
    {
        std::string header = "PKSE SELF TEST   " + nowHuman() + "   v" + VERSION_STRING + "\n";
        header += "targets:\n";
        if (plan.empty())
            header += "    (none)\n";
        for (const auto &t : plan)
        {
            header += "    " + t.display();
            if (t.titleId != 0)
            {
                char idbuf[32];
                snprintf(idbuf, sizeof(idbuf), "  titleId=0x%016llX", (unsigned long long)t.titleId);
                header += idbuf;
            }
            else
            {
                header += "  file=\"" + t.sourcePath + "\"";

            }
            header += "\n";
        }
        if (!missingTitleList.empty())
        {
            header += "not tested (no save on this console):";
            for (Enums::GameVersion missing : missingTitleList)
                header += " " + Enums::getGameVersionName(missing);
            header += "\n";
        }
        header += "duration: " + std::to_string(elapsedSeconds()) + "s\n";
        if (cancelled)
            header += "*** PARTIAL: the run was cancelled before it finished ***\n";
        if (inProgress)
            header += "*** IN PROGRESS: written after " + std::to_string(targetIdx) + "/" +
                      std::to_string(plan.size()) + " targets. If the run stopped here, the console "
                      "crashed or was closed during the next one ***\n";

        runReport.write(outPath, header);
        if (!inProgress)
            Utils::logInfoToFile("SelfTest: report written", outPath.c_str());
    }

    bool Runner::stepOnce()
    {
        switch (phase)
        {
        case Phase::GlobalSuites:
        {
            uiTarget = "data tables and ROMFS";
            if (advanceSuites(Scope::Global))
                return true;
            suiteIdx = 0;
            chunkIdx = 0;
            chunkCount = -1;
            phase = plan.empty() ? Phase::CrossSuites : Phase::TargetLoad;
            return true;
        }
        case Phase::TargetLoad:
            beginTarget();
            return true;

        case Phase::TargetSuites:
        {
            if (advanceSuites(Scope::PerTarget))
                return true;
            phase = Phase::TargetDone;
            return true;
        }
        case Phase::TargetDone:
            endTarget();
            return true;

        case Phase::CrossSuites:
        {
            uiTarget = "cross-title";
            if (advanceSuites(Scope::Cross))
                return true;
            phase = Phase::WriteReport;
            return true;
        }
        case Phase::WriteReport:
            uiTarget = "writing report";
            uiDetail.clear();
            finishedAt = (long)time(nullptr);
            writeReport();
            clearSamples(); // the pool holds a Pokemon per sample; nothing needs it now
            phase = Phase::Done;
            runState = State::Finished;
            g_suppressKeyboard = false; // the user gets their keyboard back
            return false;

        case Phase::Done:
        case Phase::Idle:
        default:
            return false;
        }
    }

    bool Runner::step(int budgetMs)
    {
        if (runState != State::Running)
            return false;

        // A chunk runs to completion once started -- the budget decides whether to start ANOTHER
        // one, not whether to interrupt one. Chunks are sized so that is a fair trade; the whole
        // point of the chunking is that no single one is long enough to notice.
        const u64 startTick = armGetSystemTick();
        const u64 budgetTicks = armNsToTicks((u64)budgetMs * 1000000ull);
        do
        {
            gYieldRequested = false;
            if (!stepOnce())
                return false;
            // A paced suite (the UI drive) gets exactly one chunk per frame, whatever the budget
            // says: its chunks are cheap, and running them back to back is precisely what made an
            // entire walkthrough finish invisibly inside a single frame.
            if (gYieldRequested)
                break;
        } while (armGetSystemTick() - startTick < budgetTicks);

        return true;
    }

    void Runner::cancel()
    {
        if (runState != State::Running)
            return;
        cancelled = true;
        runReport.setScope("-", "plan");
        Context context;
        context.rep = &runReport;
        context.skip("the run was cancelled before it finished, so this report is PARTIAL",
                     "completedTargets=" + std::to_string(targetIdx) + "/" + std::to_string(plan.size()));
        releaseTarget();
        clearSamples();
        phase = Phase::WriteReport;
    }

    int Runner::elapsedSeconds() const noexcept
    {
        const long end = (runState == State::Finished) ? finishedAt : (long)time(nullptr);
        const long seconds = end - startedAt;
        return seconds > 0 ? (int)seconds : 0;
    }

    float Runner::phaseProgress() const noexcept
    {
        if (chunkCount <= 0)
            return 0.0f;
        float floatValue = (float)chunkIdx / (float)chunkCount;
        if (floatValue < 0.0f)
            floatValue = 0.0f;
        if (floatValue > 1.0f)
            floatValue = 1.0f;
        return floatValue;
    }
}
