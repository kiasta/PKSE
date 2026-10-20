#include <cstdio>
#include <algorithm>

#include "Test/TestRunnerScreen.h"
#include "UI/ScreenChrome.h"
#include "UI/TouchInput.h"
#include "Globals.h"
#include "Utils/Logger.h"

namespace UI
{

    namespace
    {
        constexpr int ROW_HEIGHT = 62;
        constexpr int ROW_GAP = 10;
        constexpr int ROW_PITCH = ROW_HEIGHT + ROW_GAP;
        constexpr int LIST_TOP = 130;
        constexpr int FOOTER_HEIGHT = 96; // the two caption lines under the list, plus breathing room

        /// How many rows fit between the top of the list and the footer, which has to stay above the
        /// nav bar.
        ///
        /// DERIVED, NOT CHOSEN. It was a constant 8, and eight rows at this pitch end at y=696 on a
        /// 720-line screen whose nav bar starts at 674: the last row was cut off under the bar, and
        /// the footer beneath it was drawn entirely off screen -- the two lines saying nothing the
        /// user owns is written, on the one screen where that is the thing to know.
        static int visibleRowCapacity(int screenHeight)
        {
            return std::max(1, (screenHeight - NAV_BAR_HEIGHT - FOOTER_HEIGHT - LIST_TOP) / ROW_PITCH);
        }

        // Scroll window: keep the selected row visible (centered when scrolling).
        static int firstVisibleRow(int selectedIndex, int rowCount, int visibleRowCount)
        {
            if (rowCount <= visibleRowCount) return 0;
            int firstRow = selectedIndex - visibleRowCount / 2;
            if (firstRow < 0)
            {
                firstRow = 0;
            }
            if (firstRow > rowCount - visibleRowCount)
            {
                firstRow = rowCount - visibleRowCount;
            }
            return firstRow;
        }

        constexpr int SCREEN_MARGIN = 40;

        /// Work done per frame, in milliseconds. Sized so the screen still redraws at a readable
        /// rate while spending most of its time testing -- and, more importantly, so appletMainLoop
        /// keeps being pumped. A run that blocks for its whole length looks like a hung app to the
        /// system, and the HOME button stops answering.
        constexpr int STEP_BUDGET_MILLISECONDS = 20;

        Color severityColor(Test::Severity severity)
        {
            switch (severity)
            {
            case Test::Severity::Fail:
                return Colors::ShinyStar;
            case Test::Severity::Warn:
                return Colors::Warning;
            case Test::Severity::Skip:
                return Colors::Accent;
            default:
                return Colors::TextDim;
            }
        }
    }

    TestRunnerScreen::TestRunnerScreen()
    {
        discoveredTargets = Test::Runner::discover();
        buildPlan();
    }

    void TestRunnerScreen::buildPlan()
    {
        titleRows.clear();
        const Enums::GameVersion *titles = Test::testedTitles();
        for (int titleIndex = 0; titleIndex < Test::testedTitleCount(); ++titleIndex)
        {
            TitleRow row;
            row.gameVersion = titles[titleIndex];
            for (size_t targetIndex = 0; targetIndex < discoveredTargets.size(); ++targetIndex)
            {
                if (discoveredTargets[targetIndex].version == row.gameVersion)
                {
                    row.candidateTargets.push_back((int)targetIndex);
                }
            }
            // discover() returns candidates in release order, so index 0 is the default pick.
            row.chosenCandidate = 0;
            row.included = !row.candidateTargets.empty();
            titleRows.push_back(std::move(row));
        }

        // There is deliberately no "covered by its sibling" pass. Red/Blue, Gold/Silver, Diamond/Pearl and
        // HeartGold/SoulSilver do share a save layout with no version byte, but a per-title FOLDER settles
        // which one a save is -- so a title without one has no save, full stop, and saying it was covered
        // by its sibling claims a test that never ran under that name. A row with no save reads "no save
        // file found" like every other, and is counted as the coverage gap it is.
    }

    void TestRunnerScreen::startRun()
    {
        std::vector<Test::Target> chosenTargets;
        std::vector<Enums::GameVersion> missingTitles;
        for (const TitleRow &row : titleRows)
        {
            if (!row.candidateTargets.empty() && row.included)
            {
                chosenTargets.push_back(discoveredTargets[row.candidateTargets[row.chosenCandidate]]);
                continue;
            }
            missingTitles.push_back(row.gameVersion);
        }
        runner.start(std::move(chosenTargets), std::move(missingTitles));
        view = View::Running;
        resultScrollOffset = 0;
    }

    void TestRunnerScreen::update(const PadState &pad, const TouchInput &touch)
    {
        const u64 buttonsDown = padGetButtonsDown(&pad) | navTouchButton(touch);

        switch (view)
        {
        case View::Plan:
        {
            if (buttonsDown & HidNpadButton_B)
            {
                goBack = true;
                return;
            }
            const int rowCount = (int)titleRows.size();
            if (rowCount > 0)
            {
                if (buttonsDown & HidNpadButton_Down)
                {
                    selectedRowIndex = (selectedRowIndex + 1) % rowCount;
                }
                if (buttonsDown & HidNpadButton_Up)
                {
                    selectedRowIndex = (selectedRowIndex - 1 + rowCount) % rowCount;
                }

                TitleRow &row = titleRows[selectedRowIndex];
                const int candidateCount = (int)row.candidateTargets.size();
                if (candidateCount > 1)
                {
                    if (buttonsDown & HidNpadButton_Right)
                    {
                        row.chosenCandidate = (row.chosenCandidate + 1) % candidateCount;
                    }
                    if (buttonsDown & HidNpadButton_Left)
                    {
                        row.chosenCandidate = (row.chosenCandidate - 1 + candidateCount) % candidateCount;
                    }
                }
                if ((buttonsDown & HidNpadButton_X) && candidateCount > 0)
                {
                    row.included = !row.included;
                }
            }
            if (buttonsDown & HidNpadButton_A)
            {
                startRun();
            }
            if (buttonsDown & HidNpadButton_Plus)
            {
                exitRequested = true;
                return;
            }
            return;
        }

        case View::Running:
        {
            // Cancelling still writes the report; a partial pass is worth more than nothing.
            // + is the same cancel plus a standing instruction to quit once the report has landed:
            // Runner::cancel() only moves the run to its WriteReport phase, and the file is written
            // by the NEXT step(), so leaving here and now would discard the very thing cancelling
            // preserves.
            if (buttonsDown & (HidNpadButton_B | HidNpadButton_Plus))
            {
                runner.cancel();
            }
            if (buttonsDown & HidNpadButton_Plus)
            {
                exitAfterReport = true;
            }
            if (!runner.step(STEP_BUDGET_MILLISECONDS))
            {
                view = View::Results;
                if (exitAfterReport)
                {
                    exitRequested = true;
                }
            }
            return;
        }

        case View::Results:
        {
            if (buttonsDown & HidNpadButton_Plus)
            {
                exitRequested = true;
                return;
            }
            if (buttonsDown & (HidNpadButton_A | HidNpadButton_B))
            {
                view = View::Plan;
                return;
            }
            if (buttonsDown & HidNpadButton_Down)
            {
                resultScrollOffset += 1;
            }
            if (buttonsDown & HidNpadButton_Up)
            {
                resultScrollOffset -= 1;
            }
            if (buttonsDown & HidNpadButton_R)
            {
                resultScrollOffset += 6;
            }
            if (buttonsDown & HidNpadButton_L)
            {
                resultScrollOffset -= 6;
            }
            if (resultScrollOffset < 0)
            {
                resultScrollOffset = 0;
            }
            return;
        }
        }
    }

    // A compact strip over the driven screen: which save, which journey, which step, and the
    // fields that step changed. The diff is shown live rather than only in the report so a watcher
    // can see a keypress do exactly what it should -- or, in red, something it should not have.
    void TestRunnerScreen::drawDriveCaption(PKSEFramebuffer &framebuffer)
    {
        const auto &caption = Test::Runner::driveCaption();
        const Test::Report &report = runner.report();
        const int screenWidth = framebuffer.getWidth();
        const int screenHeight = framebuffer.getHeight();
        const int stripHeight = 76;
        const int stripY = screenHeight - stripHeight;

        framebuffer.drawFilledRect(0, stripY, screenWidth, stripHeight, Colors::CaptionBacking);
        framebuffer.drawFilledRect(0, stripY, screenWidth, 2, caption.changedSomethingUndeclared ? Colors::ShinyStar : Colors::Accent);

        char headingText[220];
        snprintf(headingText, sizeof(headingText), "UI WALKTHROUGH   %s   %s   step %d/%d", caption.target.c_str(), caption.journey.c_str(), caption.step, caption.stepCount);
        framebuffer.drawText(SCREEN_MARGIN, stripY + 10, headingText, Colors::TextDim, TextStyle::Caption);
        framebuffer.drawText(SCREEN_MARGIN, stripY + 30, caption.action, Colors::Text, TextStyle::Body);

        const std::string changedText = "changed: " + (caption.changed.empty() ? std::string("nothing") : caption.changed);
        framebuffer.drawText(SCREEN_MARGIN, stripY + 54, changedText, caption.changedSomethingUndeclared ? Colors::ShinyStar : Colors::TextDim, TextStyle::Caption);

        char tallyText[120];
        snprintf(tallyText, sizeof(tallyText), "%d checks   %d failed   %ds", report.checks(), report.failures(), runner.elapsedSeconds());
        int tallyWidth = 0;
        int tallyHeight = 0;
        framebuffer.measureText(tallyText, tallyWidth, tallyHeight, TextStyle::Caption);
        framebuffer.drawText(screenWidth - SCREEN_MARGIN - tallyWidth, stripY + 54, tallyText, report.failures() > 0 ? Colors::ShinyStar : Colors::TextDim, TextStyle::Caption);

        // Progress along the walkthrough itself.
        const int progressBarWidth = screenWidth - SCREEN_MARGIN * 2;
        framebuffer.drawPill(SCREEN_MARGIN, stripY + stripHeight - 8, progressBarWidth, 4, Colors::Border);
        if (caption.stepCount > 0)
        {
            framebuffer.drawPill(SCREEN_MARGIN, stripY + stripHeight - 8, (progressBarWidth * caption.step) / caption.stepCount, 4, Colors::Accent);
        }
    }

    void TestRunnerScreen::draw(PKSEFramebuffer &framebuffer)
    {
        // Hand the live framebuffer to the runner so the "ui" suite can drive real screens and let
        // them RENDER. draw() is the only place one is in scope, and it always runs before the next
        // update(), so the pointer is current by the time a suite uses it.
        Test::Runner::setFramebuffer(&framebuffer);

        const int screenWidth = framebuffer.getWidth();

        // THE UI WALKTHROUGH. When the "ui" suite is driving a screen, that screen is what the
        // display shows -- not a progress bar in front of it. This is the whole point of the suite:
        // an automated walkthrough of the app that nobody can see is indistinguishable from the
        // spinner it replaced.
        //
        // The driven screen paints the full frame itself, so there is no clear() here; a caption
        // strip goes on top saying which step is running and what it changed.
        if (view == View::Running)
        {
            if (UI::UIScreen *drivenScreen = Test::Runner::drivenScreen())
            {
                drivenScreen->draw(framebuffer);
                framebuffer.clearClip(); // the driven screen may have left a scissor set mid-pane
                drawDriveCaption(framebuffer);
                return;
            }
        }

        framebuffer.clear(Colors::Background);
        drawTitleBar(framebuffer, "Self Test");
        if (view == View::Plan)
        {
            framebuffer.drawText(SCREEN_MARGIN, 86, "One save per title. Every title, not one per generation.", Colors::TextDim, TextStyle::Caption);
            const int rowCount = (int)titleRows.size();
            const int visibleRowCount = visibleRowCapacity(framebuffer.getHeight());
            const int firstRow = firstVisibleRow(selectedRowIndex, rowCount, visibleRowCount);
            const int lastRow = std::min(rowCount, firstRow + visibleRowCount);

            for (int rowIndex = firstRow; rowIndex < lastRow; ++rowIndex)
            {
                const TitleRow &row = titleRows[rowIndex];
                const int rowHeight = ROW_HEIGHT;
                const int rowY = LIST_TOP + (rowIndex - firstRow) * ROW_PITCH;
                // The dot and both columns of text share ONE centre line. The text is placed by its
                // capitals rather than by hand-picked offsets, which had it 4-5px above the dot.
                const int rowCenterY = rowY + rowHeight / 2;
                const bool isSelected = (rowIndex == selectedRowIndex);

                framebuffer.drawSoftShadow(SCREEN_MARGIN, rowY, screenWidth - SCREEN_MARGIN * 2, rowHeight, 14);
                framebuffer.drawFilledRoundedRect(SCREEN_MARGIN, rowY, screenWidth - SCREEN_MARGIN * 2, rowHeight, 14, isSelected ? Colors::PanelAlt : Colors::Panel);
                framebuffer.drawRoundedRect(SCREEN_MARGIN, rowY, screenWidth - SCREEN_MARGIN * 2, rowHeight, 14, isSelected ? Colors::Primary : Colors::Border, isSelected ? 3 : 1);

                const bool hasSave = !row.candidateTargets.empty();
                const bool willRun = hasSave && row.included;

                framebuffer.drawPill(SCREEN_MARGIN + 18, rowCenterY - 11, 22, 22, willRun ? Colors::Accent : Colors::Border);
                framebuffer.drawText(SCREEN_MARGIN + 56, framebuffer.textYCenteredOn(rowCenterY, TextStyle::Heading), Enums::getGameVersionName(row.gameVersion), willRun ? Colors::Text : Colors::TextDim, TextStyle::Heading);

                std::string statusText;
                Color statusColor = Colors::TextDim;
                if (hasSave)
                {
                    const Test::Target &chosenTarget = discoveredTargets[row.candidateTargets[row.chosenCandidate]];
                    statusText = chosenTarget.label;
                    // No "where did this come from" marker. There is only one place a loose save
                    // can come from now (tests/saves/<Title>/) and an installed title has no folder
                    // at all, so the note was either redundant or -- on FireRed, whose save comes
                    // from a title id -- flatly wrong.
                    if (row.candidateTargets.size() > 1)
                    {
                        statusText += "   (" + std::to_string(row.chosenCandidate + 1) + " of " + std::to_string(row.candidateTargets.size()) + " - Left/Right)";
                    }
                    if (row.included)
                    {
                        statusColor = Colors::Text;
                    }
                    else
                    {
                        statusText += "   [skipped]";
                    }
                }
                else
                {
                    // A file that was THERE and still did not open is a different problem from an
                    // empty folder, and telling the user "no save file found" while they are
                    // looking straight at one is how an afternoon gets lost.
                    const std::string rejectedReason = Test::Runner::rejectedSaveReason(row.gameVersion);
                    if (!rejectedReason.empty())
                    {
                        statusText = rejectedReason;
                        statusColor = Colors::Warning;
                    }
                    else
                    {
                        statusText = Test::comesFromLooseFile(row.gameVersion) ? "no save file found - put one in sdmc:/PKSE/tests/saves/" + Enums::getGameVersionName(row.gameVersion) + "/" : "no save for this title on this console";
                    }
                }
                framebuffer.drawText(SCREEN_MARGIN + 320, framebuffer.textYCenteredOn(rowCenterY, TextStyle::Body), statusText, statusColor, TextStyle::Body);
            }

            // BESIDE THE ROWS, NOT ON THEM. The rows run edge to edge of the margin, so a track inside
            // that margin ran straight through every row's rounded corner and the selection ring. The
            // save picker puts its bar the same 12px right of its tiles. The track spans exactly the
            // rows' extent -- without the gap after the last one -- so the thumb bottoms out level
            // with the last row rather than below it.
            drawScrollbar(framebuffer, screenWidth - SCREEN_MARGIN + 12, LIST_TOP, visibleRowCount * ROW_PITCH - ROW_GAP, rowCount * ROW_PITCH - ROW_GAP, firstRow * ROW_PITCH);

            const int shownRowCount = std::min(rowCount, visibleRowCount);
            const int footerY = LIST_TOP + shownRowCount * ROW_PITCH + 14;
            framebuffer.drawText(SCREEN_MARGIN, footerY, "Nothing is written: saves are copied to sdmc:/PKSE/tests/work first,", Colors::TextDim, TextStyle::Caption);
            framebuffer.drawText(SCREEN_MARGIN, footerY + 22, "the live game save is never injected into, and the bank tests use a scratch file.", Colors::TextDim, TextStyle::Caption);

            drawNavBar(framebuffer, "A: Run  |  Left/Right: Change Save  |  X: Skip Title  |  B: Back  |  +: Exit App");
            return;
        }
        if (view == View::Running)
        {
            const Test::Report &report = runner.report();

            framebuffer.drawCard(SCREEN_MARGIN, 92, screenWidth - SCREEN_MARGIN * 2, 116);
            framebuffer.drawText(SCREEN_MARGIN + 20, 104, runner.phaseTarget(), Colors::Text, TextStyle::Heading);
            framebuffer.drawText(SCREEN_MARGIN + 20, 138, runner.phaseDetail(), Colors::TextDim, TextStyle::Body);

            // Progress through the CURRENT suite. Deliberately not an overall percentage: the
            // per-target chunk counts are only known once that target is loaded, so a global bar
            // would have to rescale itself mid-run, which reads as a bar going backwards.
            const int progressBarX = SCREEN_MARGIN + 20;
            const int progressBarY = 172;
            const int progressBarWidth = screenWidth - SCREEN_MARGIN * 2 - 40;
            const int progressBarHeight = 12;
            framebuffer.drawPill(progressBarX, progressBarY, progressBarWidth, progressBarHeight, Colors::Border);
            const int filledWidth = (int)(progressBarWidth * runner.phaseProgress());
            if (filledWidth > 0)
            {
                framebuffer.drawPill(progressBarX, progressBarY, filledWidth, progressBarHeight, Colors::Accent);
            }

            char countsText[160];
            snprintf(countsText, sizeof(countsText), "target %d/%d      %d checks      %d failed      %d warnings      %ds", runner.targetIndex() + 1, runner.targetCount() > 0 ? runner.targetCount() : 1, report.checks(), report.failures(), report.warnings(), runner.elapsedSeconds());
            framebuffer.drawText(SCREEN_MARGIN + 20, 214, countsText, report.failures() > 0 ? Colors::ShinyStar : Colors::TextDim, TextStyle::Caption);

            // Live ticker of the most recent problems.
            framebuffer.drawText(SCREEN_MARGIN, 250, "Recent findings", Colors::TextDim, TextStyle::Caption);
            int rowY = 276;
            const auto &recentLines = report.recent();
            for (const auto &recentLine : recentLines)
            {
                const Color lineColor = (recentLine.rfind("FAIL", 0) == 0) ? Colors::ShinyStar : Colors::Warning;
                // Findings carry their full detail string, which is written to be read in the report
                // rather than on a 1280px line. Clipped here so a long one cannot run off the edge
                // and take the rest of the ticker's alignment with it.
                framebuffer.drawText(SCREEN_MARGIN, rowY, recentLine.size() > 118 ? recentLine.substr(0, 115) + "..." : recentLine, lineColor, TextStyle::Caption);
                rowY += 24;
            }
            if (recentLines.empty())
            {
                framebuffer.drawText(SCREEN_MARGIN, rowY, "Nothing wrong so far.", Colors::TextDim, TextStyle::Caption);
            }

            drawNavBar(framebuffer, "B: Stop and write the report so far  |  +: Stop and exit");
            return;
        }

        const Test::Report &report = runner.report();
        const bool hasNoFailures = report.failures() == 0;

        framebuffer.drawCard(SCREEN_MARGIN, 92, screenWidth - SCREEN_MARGIN * 2, 92);
        framebuffer.drawText(SCREEN_MARGIN + 20, 104, hasNoFailures ? "No failures" : (std::to_string(report.failures()) + " failures"), hasNoFailures ? Colors::Accent : Colors::ShinyStar, TextStyle::Title);
        char subtitleText[192];
        snprintf(subtitleText, sizeof(subtitleText), "%d checks   %d warnings   %d skipped   %d notes   %ds", report.checks(), report.warnings(), report.skips(), report.notes(), runner.elapsedSeconds());
        framebuffer.drawText(SCREEN_MARGIN + 20, 146, subtitleText, Colors::TextDim, TextStyle::Caption);

        framebuffer.drawText(SCREEN_MARGIN, 196, "Report: " + runner.reportPath(), Colors::TextDim, TextStyle::Caption);

        // Failure groups first, then warnings, then skips -- the same order the report file uses. A
        // skip is listed because "No failures" next to a run that checked nothing for a title is
        // exactly the reading this screen must not allow.
        int rowY = 228;
        const int listBottom = framebuffer.getHeight() - NAV_BAR_HEIGHT - 12;
        int scrolledPastCount = 0;
        int shownGroupCount = 0;
        const Test::Severity severityOrder[3] = {Test::Severity::Fail, Test::Severity::Warn, Test::Severity::Skip};
        bool pageFull = false;
        for (Test::Severity severity : severityOrder)
        {
            if (pageFull)
            {
                break;
            }
            for (const auto &group : report.groups())
            {
                if (group.severity != severity)
                {
                    continue;
                }
                if (scrolledPastCount < resultScrollOffset)
                {
                    ++scrolledPastCount;
                    continue;
                }
                // Out of page. Break BOTH loops: continuing to the next severity would draw
                // warnings or skips on top of the failures that did not fit.
                if (rowY > listBottom - 20)
                {
                    pageFull = true;
                    break;
                }
                std::string headingText = std::string(Test::severityName(group.severity)) + "  " + group.suite + ": " + group.what + (group.occurrenceCount > 1 ? ("   x" + std::to_string(group.occurrenceCount)) : "");
                if (headingText.size() > 128)
                {
                    headingText = headingText.substr(0, 125) + "...";
                }
                framebuffer.drawText(SCREEN_MARGIN, rowY, headingText, severityColor(group.severity), TextStyle::Caption);
                rowY += 22;
                if (!group.samples.empty() && rowY <= listBottom - 20)
                {
                    const std::string &firstSample = group.samples[0];
                    framebuffer.drawText(SCREEN_MARGIN + 24, rowY, firstSample.size() > 124 ? firstSample.substr(0, 121) + "..." : firstSample, Colors::TextDim, TextStyle::Caption);
                    rowY += 22;
                }
                ++shownGroupCount;
            }
        }
        if (shownGroupCount == 0 && resultScrollOffset == 0)
        {
            framebuffer.drawText(SCREEN_MARGIN, rowY, "Nothing to report: every check passed.", Colors::TextDim, TextStyle::Body);
        }

        drawNavBar(framebuffer, "Up/Down: Scroll  |  L/R: Page  |  A: Done  |  +: Exit App");
    }
}
