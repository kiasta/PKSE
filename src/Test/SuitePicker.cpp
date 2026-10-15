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
        namespace Btn
        {
            constexpr u64 BUTTON_A = HidNpadButton_A, BUTTON_B = HidNpadButton_B, BUTTON_Y = HidNpadButton_Y;
            constexpr u64 Right = HidNpadButton_Right, Left = HidNpadButton_Left;
        }

        /// Runs a fresh picker through `presses`, then presses A, and reports what it selected.
        /// Returns 0 if nothing was selected.
        uint64_t pickAfter(Context &context, const char *label, const u64 *presses, int count)
        {
            UI::SaveSelectScreen savePicker;
            Journey j(context, savePicker, nullptr, nullptr, label);
            j.settle("picker: open", Allow::AllowNothing, 2);
            for (int index = 0; index < count; ++index)
                j.act("picker: move the cursor", presses[index], Allow::AllowNothing);
            j.act("picker: press A on the focused title", Btn::BUTTON_A, Allow::AllowNothing);
            return savePicker.hasSelectedTitle() ? savePicker.getSelectedTitleId() : 0ULL;
        }
    }

    int pickerPlan(Context &) { return 1; }

    void pickerRun(Context &context, int)
    {
        const std::vector<Target> found = Runner::discover();
        int installed = 0;
        for (const auto &t : found)
            if (t.titleId != 0)
                ++installed;

        if (installed == 0)
        {
            context.skip("no installed Pokemon save on this console, so the save picker was not driven");
            return;
        }

        if (context.rep)
            context.rep->addTrace("[picker] driving the save picker (" + std::to_string(installed) + " installed saves)");

        // 1. It opens, renders, and offers something.
        {
            UI::SaveSelectScreen savePicker;
            Journey j(context, savePicker, nullptr, nullptr, "picker-open");
            j.settle("picker: open and render", Allow::AllowNothing, 3);
            j.expect(!savePicker.shouldExit(), "the save picker asked to close as soon as it opened");
            j.expect(!savePicker.hasSelectedTitle(), "the save picker selected a title before any input");
        }

        // 2. A selects a title, and it is one the console actually has.
        const uint64_t first = pickAfter(context, "picker-select", nullptr, 0);
        if (!context.check(first != 0, "pressing A on the save picker did not select a title", "installed=" + std::to_string(installed)))
            return;

        bool known = false;
        for (const auto &t : found)
            if (t.titleId == first)
            {
                known = true;
                break;
            }
        context.check(known, "the save picker selected a title id the runner never found", hex("titleId", first, 16));

        // 3. The cursor actually moves -- only decidable when there is more than one title.
        if (installed > 1)
        {
            const u64 right[] = {Btn::Right};
            const uint64_t second = pickAfter(context, "picker-move", right, 1);
            context.check(second != 0, "pressing A after moving right did not select a title");
            context.check(second != first, "Right did not move the save picker's cursor", hex("both", first, 16));
        }
        else
        {
            context.note("only one installed save, so save-picker cursor movement was not tested", "installed=1");
        }

        // 4. The file browser -- the Gen 1 entry point -- opens and closes again.
        {
            UI::SaveSelectScreen savePicker;
            Journey j(context, savePicker, nullptr, nullptr, "picker-browser");
            j.settle("picker: open", Allow::AllowNothing, 2);
            j.act("picker: Y opens the file browser", Btn::BUTTON_Y, Allow::AllowNothing);
            j.expect(savePicker.isFileBrowserOpen(), "Y on the save picker did not open the file browser");
            j.act("picker: B closes the file browser", Btn::BUTTON_B, Allow::AllowNothing);
            j.expect(!savePicker.isFileBrowserOpen(), "B did not close the file browser");
            j.expect(!savePicker.shouldExit(), "closing the file browser exited the save picker");
            j.expect(!savePicker.hasSelectedFile(), "closing the file browser selected a file anyway");
        }
    }
}
