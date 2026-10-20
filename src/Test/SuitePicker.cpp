/**
 * Separate from SuiteUI.cpp for a build reason worth keeping: SaveSelectScreen enumerates save data
 * and application records through fs/ns, so it includes <switch.h> and cannot compile off-console.
 * Keeping it here leaves SuiteUI.cpp host-buildable, which is what lets the editor journeys -- the
 * bulk of the work -- be debugged in seconds instead of by pushing an .nro to a console. This file
 * is the part that can only ever be verified on hardware, so it is deliberately the smallest part.
 *
 * NOTHING IS ACTED ON. The suite builds its OWN SaveSelectScreen, so selecting a title in it sets a
 * flag on that instance and nothing else: no backup is created, no save is opened, the app's own
 * navigation never sees it. That is the same reason the editor journeys are safe -- the screen under
 * test is not the screen the user is standing in.
 *
 * HOW A MOVED CURSOR IS PROVEN without reaching into private state: build two pickers, press A on
 * one immediately and Right-then-A on the other, and compare the title ids they landed on. If Right
 * moved the cursor the ids differ; if it silently did nothing they match. That is a stronger check
 * than reading titleIndex would be, because it goes through selectCurrentTitle() the way a user does.
 */
#include "Test/TestSuite.h"
#include "Test/TestRunner.h"
#include "Test/UIJourney.h"

#include <memory>
#include <string>
#include <vector>

#include "UI/SaveSelectScreen.h"
#include "Utils/Logger.h"

namespace Test
{

    namespace
    {
        namespace Button
        {
            constexpr u64 BUTTON_A = HidNpadButton_A;
            constexpr u64 BUTTON_B = HidNpadButton_B;
            constexpr u64 BUTTON_Y = HidNpadButton_Y;
            constexpr u64 DPAD_RIGHT = HidNpadButton_Right;
            constexpr u64 DPAD_LEFT = HidNpadButton_Left;
        }

        /// Runs a fresh picker through `presses`, then presses A, and reports what it selected.
        /// Returns 0 if nothing was selected.
        uint64_t pickAfter(Context &context, const char *journeyName, const u64 *presses, int pressCount)
        {
            UI::SaveSelectScreen savePicker;
            Journey journey(context, savePicker, nullptr, nullptr, journeyName);
            journey.settle("picker: open", Allow::AllowNothing, 2);
            for (int pressIndex = 0; pressIndex < pressCount; ++pressIndex)
            {
                journey.act("picker: move the cursor", presses[pressIndex], Allow::AllowNothing);
            }
            journey.act("picker: press A on the focused title", Button::BUTTON_A, Allow::AllowNothing);
            return savePicker.hasSelectedTitle() ? savePicker.getSelectedTitleId() : 0ULL;
        }
    }

    int pickerPlan(Context &) { return 1; }

    void pickerRun(Context &context, int)
    {
        const std::vector<Target> discoveredTargets = Runner::discover();
        int installedCount = 0;
        for (const auto &target : discoveredTargets)
        {
            if (target.titleId != 0)
            {
                ++installedCount;
            }
        }

        if (installedCount == 0)
        {
            context.skip("no installed Pokemon save on this console, so the save picker was not driven");
            return;
        }

        if (context.report)
        {
            context.report->addTrace("[picker] driving the save picker (" + std::to_string(installedCount) + " installed saves)");
        }

        // 1. It opens, renders, and offers something.
        {
            UI::SaveSelectScreen savePicker;
            Journey journey(context, savePicker, nullptr, nullptr, "picker-open");
            journey.settle("picker: open and render", Allow::AllowNothing, 3);
            journey.expect(!savePicker.shouldExit(), "the save picker asked to close as soon as it opened");
            journey.expect(!savePicker.hasSelectedTitle(), "the save picker selected a title before any input");
        }

        // 2. A selects a title, and it is one the console actually has.
        const uint64_t firstTitleId = pickAfter(context, "picker-select", nullptr, 0);
        if (!context.check(firstTitleId != 0, "pressing A on the save picker did not select a title", "installed=" + std::to_string(installedCount))) return;

        bool titleFound = false;
        for (const auto &target : discoveredTargets)
        {
            if (target.titleId == firstTitleId)
            {
                titleFound = true;
                break;
            }
        }
        context.check(titleFound, "the save picker selected a title id the runner never found", hexField("titleId", firstTitleId, 16));

        // 3. The cursor actually moves -- only decidable when there is more than one title.
        if (installedCount > 1)
        {
            const u64 rightPresses[] = {Button::DPAD_RIGHT};
            const uint64_t secondTitleId = pickAfter(context, "picker-move", rightPresses, 1);
            context.check(secondTitleId != 0, "pressing A after moving right did not select a title");
            context.check(secondTitleId != firstTitleId, "Right did not move the save picker's cursor", hexField("both", firstTitleId, 16));
        }
        else
        {
            context.note("only one installed save, so save-picker cursor movement was not tested", "installed=1");
        }

        // 4. The file browser -- the Gen 1 entry point -- opens and closes again.
        //
        // B goes up one folder and closes the browser only at sdmc:/, and the browser opens a folder
        // or two below it (sdmc:/PKSE/ or sdmc:/PKSE/saves/), so B is pressed until it closes, up to
        // a bound deeper than any folder the browser starts in.
        {
            constexpr int maximumUpPresses = 8;
            UI::SaveSelectScreen savePicker;
            Journey journey(context, savePicker, nullptr, nullptr, "picker-browser");
            journey.settle("picker: open", Allow::AllowNothing, 2);
            journey.act("picker: Y opens the file browser", Button::BUTTON_Y, Allow::AllowNothing);
            journey.expect(savePicker.isFileBrowserOpen(), "Y on the save picker did not open the file browser");
            int upPressCount = 0;
            while (savePicker.isFileBrowserOpen() && upPressCount < maximumUpPresses)
            {
                journey.act("picker: B goes up a folder, closing the browser at the SD root", Button::BUTTON_B, Allow::AllowNothing);
                ++upPressCount;
            }
            journey.expect(!savePicker.isFileBrowserOpen(), "B never closed the file browser", "presses=" + std::to_string(upPressCount));
            journey.expect(!savePicker.shouldExit(), "closing the file browser exited the save picker");
            journey.expect(!savePicker.hasSelectedFile(), "closing the file browser selected a file anyway");
        }
    }
}
