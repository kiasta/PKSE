#ifndef PKSE_UIHOST_UIDRIVER_H
#define PKSE_UIHOST_UIDRIVER_H

#include <string>

#include "Test/TestReport.h"
#include "UI/UIScreen.h"
#include "UI/PKSEFramebuffer.h"
#include "UI/TouchInput.h"
#include "FrameRecorder.h"
#include "HostInput.h"

/**
 * Drives a real UIScreen with synthetic input and asserts on what it drew.
 *
 * The loop here is the same three lines src/UI/UI.cpp runs on console -- update(pad, touch), draw(framebuffer),
 * flush() -- with the pad built in memory instead of read from a controller and the framebuffer
 * recording instead of rasterizing. Everything between those two ends is the shipped code.
 *
 * Findings go through Test::Context, so a UI failure groups, counts and prints exactly like one from
 * the nine existing suites, in the same report the user already reads. There is no second reporting
 * path to keep in step.
 *
 * WHAT THIS CATCHES that the logic tests cannot: a button that stopped doing anything, a screen you
 * can enter but not leave, a dialog that opens and never closes, a label that renders off-panel or
 * scrolled out of its clip, a grid whose cells stop lining up, a pokemon drawn in an empty slot, an edit
 * that reaches the buffer but never the screen -- and any input sequence that crashes.
 */
namespace UIHost {

    class Driver {
    public:
        Driver(Test::Context& context, UI::UIScreen& screen, std::string screenName);

        /// Advance one frame with no input. Needed more than it looks: TrainerViewScreen defers
        /// keyboard-backed edits by one frame so the highlight draws before the applet blocks, so a
        /// test that presses and asserts in the same frame tests the frame before the work happened.
        Driver& frame(int n = 1);
        /// One press: a frame with the button down (the press edge fires) then a frame with it up.
        Driver& press(u64 button, int times = 1);
        /// Hold a button down across `frames` frames -- for anything driven by a repeat timer.
        Driver& hold(u64 button, int frames);
        /// Push the left stick to (stickX, stickY) for `frames` frames, then centre it for one. The
        /// range is libnx's, -32767..32767, with +y up.
        Driver& tilt(int stickX, int stickY, int frames = 1);
        Driver& tap(int x, int y);
        Driver& drag(int x0, int y0, int x1, int y1, int steps = 4);
        /// Queue keyboard input, then press. Reads like cy.type().
        Driver& type(const std::string& text) { queueText(text); return *this; }
        Driver& number(int v)                 { queueNumber(v);  return *this; }
        Driver& cancelPrompt()                { queueCancel();   return *this; }

        /// Names what the test is doing. Appears in every finding raised until the next step(),
        /// because "text not found" is not a bug report and "text not found after opening the box
        /// view from the HOME menu" is.
        Driver& step(std::string what);

        bool see(const std::string& text);
        bool notSee(const std::string& text);
        bool seeCount(const std::string& text, size_t expected);
        /// Visible somewhere within `tol` pixels of (x,y). Use a tolerance: host text advance widths
        /// are averaged rather than measured from Nunito, so text-driven layout lands a few pixels
        /// off what the console draws. See RecordingFramebuffer.cpp.
        bool seeNear(const std::string& text, int x, int y, int tol = 24);
        bool seeOp(const char* op, size_t atLeast = 1);
        bool seeSprites(size_t atLeast);
        /// A general assertion that still gets the frame dump and screenshot on failure.
        bool expect(bool ok, const std::string& what, const std::string& detail = std::string());

        const Frame& frameData() const { return Recorder::get().last(); }
        bool exited() const { return screen.shouldExit(); }
        long frames() const { return Recorder::get().frameCount(); }

        /// Write the last frame to `path` as SVG -- shapes, sprite boxes and real text, openable in a
        /// browser. Called automatically on the first failure of each run, which is how a headless
        /// test still lets you SEE the screen that broke.
        void screenshot(const std::string& path) const;
        /// Where the failure screenshots go; unset means none are written.
        static void setShotDir(std::string dir);
        static int  shotsWritten();

    private:
        void pump(u64 held);
        void invariants();
        void onFailure(const std::string& what);
        std::string where() const;

        Test::Context& context;
        UI::UIScreen&  screen;
        std::string    name, curStep;
        UI::PKSEFramebuffer framebuffer;
        PadState       pad{};
        UI::TouchInput touch;
        int            failuresHere = 0;
    };
}

#endif
