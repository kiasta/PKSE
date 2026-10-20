/**
 * Three views on one screen, because the run is one continuous thing:
 *
 *   PLAN     one row per TITLE -- all thirty-five, Red through Legends: Z-A -- showing the save
 *            each will be tested against and, where there is none, what to do about it. Where a
 *            title somehow has more than one save on the console, Left/Right cycles between them.
 *            X drops a title from the run entirely. Four rows can never have a save of their own:
 *            Blue, Silver, Pearl and SoulSilver share a layout with their sibling and no file says
 *            which half it is, so they are shown as covered by that sibling's save.
 *   RUNNING  what is being tested right now, a bar for the current suite, live counters, and a
 *            ticker of the last few findings so a break is visible while it is happening rather
 *            than only in the report afterwards.
 *   RESULTS  the summary, the report's path on the SD card, and the failure groups worst-first.
 *
 * The run is driven from update() a slice at a time, NOT in one blocking call. Blocking for the
 * length of a full pass would stop the app pumping appletMainLoop, and on a Switch that means the
 * HOME button stops answering and the system starts deciding the app has hung.
 */
#ifndef TEST_TEST_RUNNER_SCREEN_H
#define TEST_TEST_RUNNER_SCREEN_H

#include <string>
#include <vector>

#include <switch.h>

#include "UI/UIScreen.h"
#include "UI/PKSEFramebuffer.h"
#include "UI/Common.h"
#include "Test/TestRunner.h"

namespace UI
{

    class TestRunnerScreen : public UIScreen
    {
    public:
        TestRunnerScreen();

        void update(const PadState &pad, const TouchInput &touch) override;
        void draw(PKSEFramebuffer &framebuffer) override;
        /// Leave the self test and go back to the save picker.
        bool shouldExit() const override { return goBack || exitRequested; }
        /// Close the APP. Separate from shouldExit() for the reason TrainerViewScreen keeps them separate:
        /// leaving a screen and quitting are different outcomes, and the caller has to be able to tell which
        /// happened.
        bool hasRequestedExit() const { return exitRequested; }

    private:
        enum class View : uint8_t
        {
            Plan,
            Running,
            Results
        };

        /// One TITLE's row in the plan: every save on the console that could be this title, which
        /// one is selected, and whether it is in the run at all.
        struct TitleRow
        {
            Enums::GameVersion gameVersion = Enums::GameVersion::Invalid;
            std::vector<int> candidateTargets; // indices into `discoveredTargets`
            int chosenCandidate = 0;           // index into `candidateTargets`
            bool included = true;
        };

        void buildPlan();
        void startRun();
        /// The caption strip drawn over the screen the "ui" suite is driving.
        void drawDriveCaption(PKSEFramebuffer &framebuffer);

        std::vector<Test::Target> discoveredTargets;
        std::vector<TitleRow> titleRows;
        int selectedRowIndex = 0;
        bool goBack = false;        // B -- back to the save picker
        bool exitRequested = false; // + -- close the app (same name, same meaning, as TrainerViewScreen)
        /// + pressed mid-run: cancel, let the report land, THEN leave. Runner::cancel() only queues
        /// the write -- the report is produced by the next step() -- so quitting on the spot would
        /// throw away the partial run the user asked to keep.
        bool exitAfterReport = false;

        View view = View::Plan;
        Test::Runner runner;

        /// Scroll offset for the results list (failure groups can easily outrun one screen).
        int resultScrollOffset = 0;
    };
}

#endif
