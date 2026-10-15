#include "UI/Dialogs/SaveConfirmDialog.h"
#include "UI/Dialogs/DialogFrame.h"
#include "UI/Dialogs/EditControls.h" // drawEditChoiceButton -- on-button controller glyphs
#include "UI/TrainerViewScreen.h"
#include "UI/Common.h"
#include "UI/PKSEFramebuffer.h"
#include "Globals.h"

namespace UI
{
    namespace Dialogs
    {
        namespace
        {
            // Leaf folder name of a backup path -- that IS the backup's name in the picker list, the
            // same way it is in the backup selection screen.
            std::string leafOf(const std::string &path)
            {
                const size_t slash = path.find_last_of('/');
                return (slash == std::string::npos) ? path : path.substr(slash + 1);
            }
        }

        void drawSaveConfirmDialog(TrainerViewScreen &screen, PKSEFramebuffer &framebuffer)
        {
            constexpr int dialogWidth = 560;

            // Exiting with unsaved changes is a different question ("throw these away?"), not a
            // destination choice, so it keeps the plain two-button form.
            if (screen.exitingWithUnsavedChanges)
            {
                constexpr int dialogHeight = 248;
                const int dialogX = (framebuffer.getWidth() - dialogWidth) / 2, y = (framebuffer.getHeight() - dialogHeight) / 2;
                int centerY = drawDialogFrame(framebuffer, dialogX, y, dialogWidth, dialogHeight, "Unsaved Changes", Colors::Warning);
                framebuffer.drawText(dialogX + 24, centerY, "You have unsaved changes.", Colors::Text);
                framebuffer.drawText(dialogX + 24, centerY + 28, "Changes will be lost if you continue.", Colors::TextDim);

                // Buttons carry their glyph (id 0 = Cancel/B, id 1 = Discard & Exit/A), no guide line.
                screen.touchButtons.clear();
                const int choiceButtonHeight = TouchTargetMin, cby = y + dialogHeight - choiceButtonHeight - 16;
                const int choiceButtonWidth = (dialogWidth - 48 - 16) / 2;
                drawEditChoiceButton(screen, framebuffer, dialogX + 24, cby, choiceButtonWidth, choiceButtonHeight, "B", "Cancel", 0);
                drawEditChoiceButton(screen, framebuffer, dialogX + dialogWidth - 24 - choiceButtonWidth, cby, choiceButtonWidth, choiceButtonHeight, "A", "Discard & Exit", 1);
                return;
            }

            const int rows = screen.saveDestinationCount();
            constexpr int rowH = 58, rowGap = 8;
            // The notice is a LINE here, not an extra dialog. This dialog is already a confirm/cancel,
            // so folding a warning in tells the user without adding a step.
            const int warnH = (screen.illegalDataWritten ? 26 : 0);
            // 126 rather than 168: the card has no hint strip of its own, which would sit directly above
            // the screen's nav bar and list the same three keys a second time.
            const int dialogHeight = 126 + warnH + rows * (rowH + rowGap);
            const int dialogX = (framebuffer.getWidth() - dialogWidth) / 2, y = (framebuffer.getHeight() - dialogHeight) / 2;

            int centerY = drawDialogFrame(framebuffer, dialogX, y, dialogWidth, dialogHeight, "Save Changes", Colors::Text);
            framebuffer.drawText(dialogX + 24, centerY, screen.hasUnsavedChanges ? "Where should this be written?" : "No changes made. Write anyway?", screen.hasUnsavedChanges ? Colors::Text : Colors::TextDim);
            if (screen.illegalDataWritten)
            {
                framebuffer.drawText(dialogX + 24, centerY + 24, "Contains values the games treat as illegal.", Color(235, 120, 120), TextStyle::Caption);
                centerY += 26;
            }
            // There is deliberately no "this save contains DLC content" warning. Owning a DLC gates the
            // AREAS, not the Pokemon -- the patch ships the data to every copy, so a player without the
            // Expansion Pass can hold a Crown Tundra species traded to them and it works normally.

            const std::string backupName = leafOf(screen.backupDir);
            const char *titles[4] = {"This backup", "New backup...", "Game save", "Save file"};
            const std::string subs[4] = {
                backupName,
                "name it with the keyboard",
                // Same destination, very different act depending on where this session came from.
                screen.loadedFromCart ? "write back to " + screen.titleName
                                      : "REPLACES your live save with this backup",
                // A loose file: name it, and say that the original is copied aside first, because the
                // user is about to overwrite a file PKSE did not create and may not have another of.
                "write back to " + leafOf(screen.backupDir) + " (a copy is kept first)",
            };

            screen.touchButtons.clear();
            const int rowX = dialogX + 20, rw = dialogWidth - 40;
            int rectY = centerY + 34;
            for (int rowIndex = 0; rowIndex < rows; ++rowIndex)
            {
                // A row is not its destination: a title session shows Game save / New backup, so the
                // row index has to be mapped rather than used to index the arrays directly.
                const int destinationId = static_cast<int>(screen.saveDestinationAt(rowIndex));
                const bool selectedIndex = (screen.saveDestinationIndex == rowIndex);
                if (selectedIndex)
                    framebuffer.drawSelectionHighlight(rowX, rectY, rw, rowH);
                else
                    framebuffer.drawFilledRoundedRect(rowX, rectY, rw, rowH, 10, Colors::PanelAlt);

                // Red only when writing to the game would DESTROY something: a backup-sourced session
                // overwriting live progress. Saving a cart session back to its own cart is routine and
                // shouldn't be dressed up as a hazard.
                const bool danger = (destinationId == TrainerViewScreen::DestinationGameSave) && !screen.loadedFromCart;
                framebuffer.drawText(rowX + 18, rectY + 7, titles[destinationId], danger ? Color(235, 120, 120) : Colors::Text, TextStyle::Body);
                framebuffer.drawText(rowX + 18, rectY + 32, subs[destinationId], Colors::TextDim, TextStyle::Caption);

                screen.touchButtons.push_back({rowIndex, rowX, rectY, rw, rowH});
                rectY += rowH + rowGap;
            }

        }

        void drawSaveInjectConfirm(TrainerViewScreen &screen, PKSEFramebuffer &framebuffer)
        {
            constexpr int dialogWidth = 620, h = 268;
            const int dialogX = (framebuffer.getWidth() - dialogWidth) / 2, y = (framebuffer.getHeight() - h) / 2;

            int centerY = drawDialogFrame(framebuffer, dialogX, y, dialogWidth, h, "Write to the game save?", Color(235, 120, 120));
            framebuffer.drawText(dialogX + 24, centerY, "This replaces " + screen.titleName + "'s save data", Colors::Text);
            framebuffer.drawText(dialogX + 24, centerY + 26, "with backup \"" + leafOf(screen.backupDir) + "\" plus your edits.", Colors::Text);
            // Say plainly what is at risk. The user may have loaded a backup from weeks ago, in which
            // case this rolls their game back -- and the dialog is the only place that can warn them.
            framebuffer.drawText(dialogX + 24, centerY + 60, "Any progress made since that backup will be lost.", Color(235, 120, 120));
            framebuffer.drawText(dialogX + 24, centerY + 86, "The backup itself is written either way.", Colors::TextDim, TextStyle::Caption);

            screen.touchButtons.clear();
            const int boxWidth = (dialogWidth - 60) / 2, bh = 52, by = y + h - 48 - bh - 8;
            // Glyph ON each button (B: Cancel, A: Write to game); the destructive action stays red.
            drawEditChoiceButton(screen, framebuffer, dialogX + 20, by, boxWidth, bh, "B", "Cancel", 0);
            drawEditChoiceButton(screen, framebuffer, dialogX + 40 + boxWidth, by, boxWidth, bh, "A", "Write to game", 1, Color(160, 60, 60), Colors::White);
        }
    }
}
