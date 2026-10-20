/**
 * WHAT IT RUNS AGAINST. The framework tests ONE SAVE PER TITLE, and the plan lists EVERY title --
 * all thirty-eight of them, from Red to Legends: Z-A. Grouping by generation was the earlier design
 * and it hid real differences: Violet and Legends: Z-A are both Gen 9 but are not the same save
 * format, and testing one while calling it "Gen 9" left the other untested and unmentioned.
 *
 * Every title names itself. A Switch save's title id IS its title -- Sword and Shield, Scarlet and
 * Violet, FireRed and LeafGreen each have their own -- and Gens 5, 6 and 7 carry a real game byte
 * in the save, so Black and White land on separate rows. The four exceptions are Red/Blue,
 * Gold/Silver, Diamond/Pearl and HeartGold/SoulSilver: each pair shares a layout with no version
 * byte anywhere in it, so the file itself could be either game and will never say which. THE FOLDER
 * DECIDES: a save under tests/saves/Blue/ is Blue, and one under tests/saves/Red/ is Red. Both
 * titles are listed and each needs its own save; neither stands in for the other, because a run
 * against Red's file is a run against Red and calling it Blue would claim a test that never
 * happened under that name.
 *
 * A title with NO save on the console is reported as a skip, loudly. A pass that quietly covered
 * five titles out of thirty-eight is worse than no pass at all.
 *
 * NOTHING THE USER OWNS IS WRITTEN. Every save is copied into sdmc:/PKSE/tests/work/ first and every
 * write test happens there; the live game save is never injected into (the inject flag is passed
 * false, always), the user's backup folders are never touched, and the bank tests run against a
 * scratch bank file rather than the real one. The only things this run creates outside that scratch
 * tree are the report itself and, if debug logging is on, the usual log lines.
 *
 * WHY A STEP MACHINE. The suites run inside the app's own frame loop, so the runner hands out work in
 * chunks sized to a frame budget instead of blocking for the length of a full pass. A run that
 * blocked for forty seconds would stop pumping appletMainLoop, which on a Switch means the HOME
 * button stops answering and the system decides the app has hung.
 */
#ifndef TEST_TEST_RUNNER_H
#define TEST_TEST_RUNNER_H

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "Test/TestReport.h"
#include "Test/TestSuite.h"
#include "Enums/GameVersion.h"
#include "Utils/NXTypes.h"

namespace Trainer
{
    class Trainer;
    class Trainer1RBY;
}

namespace Test
{

    /// One save the run could use. Either an installed title's save (titleId != 0) or a loose file
    /// the user dropped on the SD card (titleId == 0, sourcePath set) -- which is how every
    /// pre-Switch title arrives, since a Game Boy save has no title id and belongs to no account.
    struct Target
    {
        Enums::GameVersion version = Enums::GameVersion::Invalid; // the concrete title (SW, VL, YW)
        Enums::GameVersion group = Enums::GameVersion::Invalid;   // its save-format group
        std::string label;                                        // "Sword", "Red/Blue  rb.sav"
        uint64_t titleId = 0;
        AccountUid accountUid{};
        std::string sourcePath; // loose-file path, else empty

        /// "Sword" -- how this target is named everywhere in the report.
        std::string display() const;
    };

    /// Every title the framework tests, in release order. This is the plan screen's row order and
    /// the run order.
    const Enums::GameVersion *testedTitles() noexcept;
    int testedTitleCount() noexcept;

    /// Does a save for this title arrive as a FILE the user copies onto the SD card, rather than as
    /// installed save data? It decides which instruction an empty plan row shows, and the two are
    /// not interchangeable: a Diamond gap is fixed by dropping a .sav in a folder, while a Sword
    /// gap needs the game installed and there is nothing to copy.
    bool comesFromLooseFile(Enums::GameVersion title) noexcept;

    class Runner
    {
    public:
        enum class State : uint8_t
        {
            Idle,
            Running,
            Finished
        };

        Runner();
        /// Defined in the .cpp, where `Loaded` is a complete type. The loaded save is held behind a
        /// unique_ptr to an incomplete struct so this header does not have to pull in every Trainer
        /// subclass just to be included by the UI.
        ~Runner();

        /// Everything on this console that could be a target: one entry per installed Pokemon save,
        /// plus any loose pre-Switch save found in the scan directories. Ordered by title, then
        /// by the preference used to pick a default representative.
        static std::vector<Target> discover();

        /// Why a title with a file in its folder still has no candidate, or "" if that is not the
        /// case. Valid after discover(). "No save file found" is the wrong thing to tell someone
        /// looking at a folder with a file in it; the plan row shows this instead.
        static std::string rejectedSaveReason(Enums::GameVersion title);

        /// Directories searched for a loose save file, in order. Exposed so the picker can tell the
        /// user where to put one when a title comes up empty.
        static const char *const *looseScanDirectories() noexcept;
        static int looseScanDirectoryCount() noexcept;

        /// Begins a run over `targets` (at most one per title). `missingTitles` names the titles
        /// that had no save at all, so they can be recorded as skips rather than quietly omitted.
        void start(std::vector<Target> targets, std::vector<Enums::GameVersion> missingTitles);

        /// Does work for up to `budgetMilliseconds`, then returns. False once the run is over.
        bool step(int budgetMilliseconds);

        /// Stops early and writes what has been found so far. The report is marked PARTIAL rather
        /// than discarded: a run abandoned three suites in because the first one was clearly broken
        /// is still a run whose findings are worth reading.
        void cancel();

        /// Seconds the run took (or has taken so far).
        int elapsedSeconds() const noexcept;

        /// The live framebuffer, handed to suites that DRIVE the UI so the screens they drive can
        /// actually render. Set by TestRunnerScreen from its own draw(), which is the only place a
        /// framebuffer reference exists -- update() is not given one, and the alternative was
        /// threading one through UIManager, which is shipped code this test has no business editing.
        /// Null until the first frame has been drawn, which suites must tolerate.
        static void setFramebuffer(UI::PKSEFramebuffer *framebuffer) noexcept;

        // The "ui" suite drives a real screen. For that to be worth watching -- and it has to be
        // watchable, or a walkthrough of the app is indistinguishable from the progress bar it
        // replaces -- the screen being driven has to be the thing ON SCREEN, not something drawn
        // into a frame the runner then paints over. So the suite PUBLISHES the screen it is driving
        // and TestRunnerScreen draws that instead of its own running view, with a caption strip.

        /// The screen currently being driven, or null when nothing is.
        static void setDrivenScreen(UI::UIScreen *screen) noexcept;
        static UI::UIScreen *drivenScreen() noexcept;

        /// What the drive is doing right now, for the on-screen caption.
        struct DriveCaption
        {
            std::string target;  // "Shield"
            std::string journey; // "boxes"
            std::string action;  // "boxes: move the cursor right"
            std::string changed; // "slot"  -- the diff this action produced
            bool changedSomethingUndeclared = false;
            int step = 0;
            int stepCount = 0;
        };
        static void setDriveCaption(const DriveCaption &caption);
        static const DriveCaption &driveCaption() noexcept;

        /// Asks step() to end the frame after the current chunk instead of starting another.
        ///
        /// Without this the budget loop runs chunks back to back until its milliseconds are gone,
        /// so an entire walkthrough completes inside one or two frames and nothing of it is ever
        /// presented. A paced suite calls this once per chunk to get exactly one chunk per frame.
        static void yieldFrame() noexcept;

        State state() const noexcept { return runState; }
        const Report &report() const noexcept { return runReport; }
        const std::string &reportPath() const noexcept { return reportFilePath; }

        /// "Gen 8 / Sword" and "entity 4/13" -- the two halves of the on-screen progress readout.
        const std::string &phaseTarget() const noexcept { return phaseTargetText; }
        const std::string &phaseDetail() const noexcept { return phaseDetailText; }
        /// 0..1 through the CURRENT target (or the global/cross pass). Deliberately not an overall
        /// fraction: the per-target chunk counts are only known once that target is loaded, and a
        /// bar that recalculates its own scale mid-run reads as broken.
        float phaseProgress() const noexcept;
        int targetIndex() const noexcept { return (int)currentTargetIndex; }
        int targetCount() const noexcept { return (int)plan.size(); }

    private:
        enum class Phase : uint8_t
        {
            Idle,
            GlobalSuites,
            TargetLoad,
            TargetSuites,
            TargetDone,
            CrossSuites,
            WriteReport,
            Done
        };

        bool stepOnce();
        /// Advances through the suites matching `scope`, one chunk per call. True while working.
        /// Builds its own Context per chunk (see makeContext) -- the caller has nothing to pass.
        bool advanceSuites(Scope scope);
        void beginTarget();
        void endTarget();
        void releaseTarget(); // free the loaded save and delete its scratch copy
        /// `inProgress` marks a checkpoint written after a target, which the final write replaces.
        void writeReport(bool inProgress = false);
        Context makeContext(const char *suiteName);

        std::vector<Target> plan;
        std::vector<Enums::GameVersion> missingTitleList;
        Report runReport;
        State runState = State::Idle;
        Phase phase = Phase::Idle;

        size_t currentTargetIndex = 0;
        int currentSuiteIndex = 0; // index into suites()
        int currentChunkIndex = 0;
        int chunkCount = -1; // -1 = the current suite has not been planned yet
        /// The report's check and skip counts when the current suite was planned. A suite that ends
        /// with neither having moved checked nothing, and that is reported rather than left to read
        /// as a clean pass.
        int checksAtSuiteStart = 0;
        int skipsAtSuiteStart = 0;

        // The loaded save. Two holders because Gen 1 is not in the TrainerVariant: a .sav has no
        // title id, so it never goes through Save::readTrainerInfo's title-id dispatch.
        struct Loaded;
        std::unique_ptr<Loaded> loaded;
        Trainer::Trainer *trainer = nullptr;
        std::string workDirectory;
        /// The scratch file a LOOSE target was copied to; empty for an installed title. See
        /// Context::savePath for why this is carried rather than searched for.
        std::string workSavePath;

        std::string reportFilePath;
        std::string phaseTargetText;
        std::string phaseDetailText;
        std::string runStamp;
        long startedAt = 0;
        long finishedAt = 0;
        bool cancelled = false;
    };
}

#endif
