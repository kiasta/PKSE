/**
 * A stand-in for libnx's <switch.h>, used ONLY by the host UI harness.
 *
 * This exists so the harness needs no edits to the shipped tree at all. The UI headers include
 * <switch.h>; putting this directory first on the include path satisfies that off-console, and the
 * product sources stay exactly as they are. Delete tools/harness/uihost/ and nothing remains.
 *
 * It deliberately declares almost nothing. The scalars and AccountUid come from the project's own
 * include/Utils/NXTypes.h, which already has a host branch for exactly this purpose, so there is
 * only ever ONE definition of those. What is added here is the input path, and only that:
 *
 *   PadState is a plain struct, and libnx's button readers are constexpr reads over it. So a
 *   synthesized PadState is not a mock -- it is the same bytes the console hands the screens, and
 *   update(pad, touch) cannot tell the difference.
 *
 * KNOWN LIMITATION: the button bit values below are libnx's, copied. Nothing here can prove they
 * still match, because the real header only exists on console and this file is never compiled
 * there. They are stable ABI and have not moved in libnx's lifetime, but if a test ever passes on
 * the host and the same press does something else on hardware, check these first.
 *
 * Anything that is a console SERVICE rather than a type -- fs, ns, applet, hid, swkbd -- is
 * deliberately NOT declared. The screens that call those (SaveSelectScreen, SystemIcons, UI.cpp,
 * TestRunnerScreen) are excluded from the harness build instead, so a missing symbol is a build
 * error that names the problem rather than a stub quietly returning a fiction.
 */
#ifndef PKSE_UIHOST_SWITCH_H
#define PKSE_UIHOST_SWITCH_H

#include "Utils/NXTypes.h"   // u8..s64 + AccountUid, from the project's own host branch

/// libnx's stick position, in the same -32767..32767 range. Left at zero by the harness, which
/// drives the D-pad only, so a centred stick contributes no navigation.
typedef struct HidAnalogStickState {
    s32 x = 0;
    s32 y = 0;
} HidAnalogStickState;

/// The fields libnx's accessors read. The rest of the real PadState is controller bookkeeping
/// no screen touches, so it is absent rather than faked.
typedef struct PadState {
    u64 buttons_cur = 0;
    u64 buttons_old = 0;
    HidAnalogStickState sticks[2] = {};
} PadState;

enum HidNpadButton : u64 {
    HidNpadButton_A      = 1UL << 0,
    HidNpadButton_B      = 1UL << 1,
    HidNpadButton_X      = 1UL << 2,
    HidNpadButton_Y      = 1UL << 3,
    HidNpadButton_StickL = 1UL << 4,
    HidNpadButton_StickR = 1UL << 5,
    HidNpadButton_L      = 1UL << 6,
    HidNpadButton_R      = 1UL << 7,
    HidNpadButton_ZL     = 1UL << 8,
    HidNpadButton_ZR     = 1UL << 9,
    HidNpadButton_Plus   = 1UL << 10,
    HidNpadButton_Minus  = 1UL << 11,
    HidNpadButton_Left   = 1UL << 12,
    HidNpadButton_Up     = 1UL << 13,
    HidNpadButton_Right  = 1UL << 14,
    HidNpadButton_Down   = 1UL << 15,
};

// Same semantics as libnx: "down" is the press edge, "held" the level, "up" the release edge.
constexpr u64 padGetButtons(const PadState* p)     { return p->buttons_cur; }
constexpr u64 padGetButtonsDown(const PadState* p) { return p->buttons_cur & ~p->buttons_old; }
constexpr u64 padGetButtonsUp(const PadState* p)   { return ~p->buttons_cur & p->buttons_old; }
constexpr HidAnalogStickState padGetStickPos(const PadState* p, unsigned stickIndex) { return p->sticks[stickIndex]; }

#endif
