/**
 * UI::TouchInput fed from a script instead of from hidGetTouchScreenStates.
 *
 * Replaces src/UI/TouchInput.cpp in the harness. The edge derivation below is copied from the real
 * one deliberately rather than shared: it is four lines, and a test harness that computed press and
 * release edges its own way would stop testing the thing the console runs.
 */
#include "UI/TouchInput.h"
#include "HostInput.h"

namespace
{
    bool g_touchDown = false;
    int g_touchX = 0;
    int g_touchY = 0;
}

namespace UIHost
{
    void setTouch(bool touchDown, int touchX, int touchY)
    {
        g_touchDown = touchDown;
        g_touchX = touchX;
        g_touchY = touchY;
    }
}

namespace UI
{
    void TouchInput::update()
    {
        prevDown = curDown;
        if (g_touchDown)
        {
            curX = g_touchX;
            curY = g_touchY;
            if (!curDown)
            {
                begX = curX;
                begY = curY;
            }   // press edge records the start position
            curDown = true;
        }
        else
        {
            curDown = false;
        }
    }

    bool TouchInput::dragged() const
    {
        const int deltaX = curX - begX;
        const int deltaY = curY - begY;
        return (deltaX * deltaX + deltaY * deltaY) > (20 * 20);
    }
}
