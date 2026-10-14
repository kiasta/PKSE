#ifndef UI_DIALOGS_EDIT_CONTROLS_H
#define UI_DIALOGS_EDIT_CONTROLS_H

#include "UI/PKSEFramebuffer.h"
#include "UI/Common.h"
#include "UI/ScreenChrome.h"      // buttonGlyph — the same controller badges the bottom guides use
#include "UI/TrainerViewScreen.h" // TrainerViewScreen::touchButtons

namespace UI
{
    namespace Dialogs
    {
        // Shared controls for the value editors (Edit Stat, Edit Amount). Kept identical between the two
        // so they feel the same: a row of controller-badged +/- step buttons, plus A/B-labelled
        // Cancel/Confirm. Touch ids: 34=-100(ZL) 30=-10(L) 31=-1(<) 32=+1(>) 33=+10(R) 35=+100(ZR),
        // 0=Cancel(B) 1=Confirm(A) -- the dialogs' input handlers map these back to the same buttons.
        struct EditStep
        {
            const char *glyph;
            const char *delta;
            int buttonId;
        };

        inline const EditStep EDIT_STEPS[6] = {
            {"ZL", "-100", 34},
            {"L", "-10", 30},
            {"Left", "-1", 31},
            {"Right", "+1", 32},
            {"R", "+10", 33},
            {"ZR", "+100", 35},
        };

        inline void drawEditStepButton(TrainerViewScreen &screen, PKSEFramebuffer &framebuffer, int boxX, int boxY, int boxWidth, int boxHeight, const EditStep &step)
        {
            framebuffer.drawFilledRoundedRect(boxX, boxY, boxWidth, boxHeight, 8, Colors::PanelAlt);
            framebuffer.drawRoundedRect(boxX, boxY, boxWidth, boxHeight, 8, Colors::Border, 1);
            const int glyphWidth = buttonGlyphWidth(framebuffer, step.glyph);
            buttonGlyph(framebuffer, boxX + (boxWidth - glyphWidth) / 2, boxY + 18, step.glyph, false); // badge above
            int textWidth, th;
            framebuffer.measureText(step.delta, textWidth, th, TextStyle::Caption);
            framebuffer.drawText(boxX + (boxWidth - textWidth) / 2, boxY + boxHeight - th - 6, step.delta, Colors::Text, TextStyle::Caption); // delta below
            screen.touchButtons.push_back({step.buttonId, boxX, boxY, boxWidth, boxHeight});
        }

        // Lay the six step buttons across [x, x+w], centred, at rowY (height rowH).
        inline void drawEditStepRow(TrainerViewScreen &screen, PKSEFramebuffer &framebuffer, int editStepRowX, int editStepRowWidth, int rowY, int rowH)
        {
            constexpr int count = 6, gap = 8;
            const int boxWidth = (editStepRowWidth - 32 - (count - 1) * gap) / count;
            int boxX = editStepRowX + (editStepRowWidth - (count * boxWidth + (count - 1) * gap)) / 2;
            for (const EditStep &step : EDIT_STEPS)
            {
                drawEditStepButton(screen, framebuffer, boxX, rowY, boxWidth, rowH, step);
                boxX += boxWidth + gap;
            }
        }

        // A Cancel/Confirm button labelled with its face button. Deliberately NOT accent-filled: colouring
        // "Confirm" as if selected implied a cursor that isn't there -- both are always available.
        //
        // This is THE way a dialog button is drawn. Drawing one and registering its hit box were two
        // statements that had to agree on five numbers, repeated at a dozen sites, and a dialog whose
        // rect drifted from its button was invisible until someone tapped the wrong place. Doing both
        // here also means every dialog button gets the HELD look for free while a tap on it is
        // waiting to fire -- see TrainerViewScreen::armTap for why that frame exists.
        inline void drawEditChoiceButton(TrainerViewScreen &screen, PKSEFramebuffer &framebuffer, int boxX, int boxY, int boxWidth, int boxHeight, const char *glyph, const char *label, int itemId, Color fill = Colors::PanelAlt, Color textColor = Colors::Text)
        {
            drawGlyphButton(framebuffer, boxX, boxY, boxWidth, boxHeight, glyph, label, fill, textColor, screen.pendingTapButtonId == itemId);
            screen.touchButtons.push_back({itemId, boxX, boxY, boxWidth, boxHeight});
        }
    }
}

#endif
