#ifndef UI_DIALOGS_DIALOG_FRAME_H
#define UI_DIALOGS_DIALOG_FRAME_H

#include <string>

#include "UI/PKSEFramebuffer.h"
#include "UI/Common.h"
#include "UI/ScreenChrome.h"

namespace UI
{
    namespace Dialogs
    {
        // Radius shared by every dialog surface. Matches PickerDialog, which was already rounded, so
        // all modal cards now read as one family instead of some square and some not.
        constexpr int DIALOG_CORNER_RADIUS = 16;

        // Draw a centered modal dialog frame: a dim scrim over the screen, a lifted card, a heading
        // title, and an accent underline. Returns the Y at which content should start.
        inline int drawDialogFrame(PKSEFramebuffer &framebuffer, int dialogX, int dialogY, int dialogWidth, int dialogHeight, const std::string &title, Color titleColor)
        {
            // scrim
            framebuffer.drawFilledRect(0, 0, framebuffer.getWidth(), framebuffer.getHeight(), Color(0, 0, 0, 150));
            framebuffer.drawSoftShadow(dialogX, dialogY, dialogWidth, dialogHeight, DIALOG_CORNER_RADIUS);
            framebuffer.drawFilledRoundedRect(dialogX, dialogY, dialogWidth, dialogHeight, DIALOG_CORNER_RADIUS, Colors::Panel);
            framebuffer.drawRoundedRect(dialogX, dialogY, dialogWidth, dialogHeight, DIALOG_CORNER_RADIUS, Colors::Border, 1);
            framebuffer.drawText(dialogX + 24, dialogY + 16, title, titleColor, TextStyle::Heading);
            // Inset so the rule stops short of the rounded corners instead of running into the curve.
            framebuffer.drawFilledRect(dialogX + DIALOG_CORNER_RADIUS, dialogY + 56, dialogWidth - DIALOG_CORNER_RADIUS * 2, 2, Colors::Accent);
            return dialogY + 78;
        }

        // THERE IS NO drawDialogFooter(). A dialog does not list its own controls: the screen's nav
        // bar is the one place they live, and a strip inside the card lands directly above it -- two
        // rows of controller badges for one dialog. The file browser and the save-destination dialog
        // both had one, and neither agreed with the nav bar behind it (the browser card omitted
        // Y: Search while the nav bar omitted L/R: Page). Name the keys in the owning screen's
        // instruction chain instead; see TrainerViewScreen::draw and SaveSelectScreen::draw.
    }
}

#endif
