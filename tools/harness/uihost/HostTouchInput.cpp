/**
 * UI::TouchInput fed from a script instead of from hidGetTouchScreenStates.
 *
 * Replaces src/UI/TouchInput.cpp in the harness. The edge derivation below is copied from the real
 * one deliberately rather than shared: it is four lines, and a test harness that computed press and
 * release edges its own way would stop testing the thing the console runs.
 */
#include "UI/TouchInput.h"
#include "HostInput.h"

namespace {
    bool gDown = false;
    int  gX = 0, gY = 0;
}

namespace UIHost {
    void setTouch(bool down, int x, int y) { gDown = down; gX = x; gY = y; }
}

namespace UI {
    void TouchInput::update() {
        prevDown = curDown;
        if (gDown) {
            curX = gX;
            curY = gY;
            if (!curDown) { begX = curX; begY = curY; }   // press edge records the start position
            curDown = true;
        } else {
            curDown = false;
        }
    }

    bool TouchInput::dragged() const {
        const int dx = curX - begX, dy = curY - begY;
        return (dx * dx + dy * dy) > (20 * 20);
    }
}
