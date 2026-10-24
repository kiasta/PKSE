#include <string>
#include <vector>

#include "UI/Dialogs/ItemEditDialog.h"
#include "UI/Dialogs/DialogFrame.h"
#include "UI/Dialogs/EditControls.h" // shared step buttons + A/B choice buttons
#include "UI/TrainerViewScreen.h"
#include "UI/Common.h"
#include "UI/PKSEFramebuffer.h"
#include "Trainer/Trainer.h"
#include "Names/ItemNames.h" // getItemNameG3 -- Gen 3 uses a separate item id space
#include "Enums/GameVersion.h"
#include "Utils/HelperUtilities.h"

using namespace Trainer;
using namespace Utils;

namespace UI
{
    namespace Dialogs
    {
        void drawItemEditDialog(TrainerViewScreen &screen, PKSEFramebuffer &framebuffer)
        {
            constexpr int dialogWidth = 560, h = 360;
            const int dialogX = (framebuffer.getWidth() - dialogWidth) / 2;
            const int dialogY = (framebuffer.getHeight() - h) / 2;

            int centerY = drawDialogFrame(framebuffer, dialogX, dialogY, dialogWidth, h, "Edit Item", Colors::Text);

            // Resolve the item name via the visible list (selectedItemIndex indexes visible items).
            // Gen 3 ids are a SEPARATE id space from the modern one -- resolving a FireRed bag item
            // through the modern table named Potion (g3 13) "Dusk Ball" (modern 13). The list panel
            // already branched; this dialog did not, so the two disagreed on screen.
            // The item id space is per-generation; the group decides which table names it.
            const Enums::GameVersion itemGroup = screen.trainer.getGameGroup();
            std::string itemName = "Unknown Item";
            if (screen.selectedCategory >= 0 && screen.selectedCategory < static_cast<int>(screen.trainer.items.size()))
            {
                const auto &pouch = screen.trainer.items[screen.selectedCategory];
                std::vector<int> visible = screen.visibleItemIndices();
                if (screen.selectedItemIndex >= 0 && screen.selectedItemIndex < static_cast<int>(visible.size()))
                {
                    const uint16_t selectedItemId = pouch[visible[screen.selectedItemIndex]].itemId;
                    itemName = Names::getItemNameFor(itemGroup, selectedItemId);
                }
            }

            screen.touchButtons.clear();

            // ITEM label + the pouch's max stack (so the cap is visible instead of the value just refusing
            // to rise -- e.g. a key item stuck at "1 / max 1").
            framebuffer.drawText(dialogX + 24, centerY, "ITEM", Colors::TextDim, TextStyle::Caption);
            {
                const std::string capacity = "max " + std::to_string(screen.currentItemMaxCount());
                int captionWidth, ch;
                framebuffer.measureText(capacity, captionWidth, ch, TextStyle::Caption);
                framebuffer.drawText(dialogX + dialogWidth - 24 - captionWidth, centerY, capacity, Colors::TextDim, TextStyle::Caption);
            }

            // Item drop-down: shows the current item; X (or a tap) opens the picker to CHANGE its type
            // (Potion -> Super Potion). It is a real touch target (id 40) plus the X face button.
            const int dropdownX = dialogX + 24, ddY = centerY + 20, ddW = dialogWidth - 48, ddH = 42;
            framebuffer.drawFilledRoundedRect(dropdownX, ddY, ddW, ddH, 8, Colors::PanelAlt);
            framebuffer.drawRoundedRect(dropdownX, ddY, ddW, ddH, 8, Colors::Border, 1);
            framebuffer.drawText(dropdownX + 16, ddY + (ddH - framebuffer.lineHeight(TextStyle::Body)) / 2, itemName, Colors::Text);
            framebuffer.drawSymbol(dropdownX + ddW - 28, ddY + ddH / 2 - 8, "\xE2\x96\xBC", Colors::TextDim, TextStyle::Caption); // v
            {
                const int glyphWidth = buttonGlyphWidth(framebuffer, "X");
                buttonGlyph(framebuffer, dropdownX + ddW - 40 - glyphWidth, ddY + ddH / 2, "X", false);
            }
            screen.touchButtons.push_back({40, dropdownX, ddY, ddW, ddH});

            // Amount, large and centered in the accent color.
            std::string amount = std::to_string(screen.itemEditDialogValue);
            int amountWidth, ah;
            framebuffer.measureText(amount, amountWidth, ah, TextStyle::Title);
            framebuffer.drawText(dialogX + (dialogWidth - amountWidth) / 2, centerY + 76, amount, Colors::Accent, TextStyle::Title);

            // Step buttons (ZL -100 | L -10 | < -1 | > +1 | R +10 | ZR +100), then Cancel / Remove / Confirm
            // labelled with B / Y / A.
            const int choiceButtonHeight = TouchTargetMin, cby = dialogY + h - choiceButtonHeight - 14;
            drawEditStepRow(screen, framebuffer, dialogX, dialogWidth, cby - 58 - 14, 58);

            const int choiceButtonWidth = 160, gap = 16;
            drawEditChoiceButton(screen, framebuffer, dialogX + 24, cby, choiceButtonWidth, choiceButtonHeight, "B", "Cancel", 0);
            drawEditChoiceButton(screen, framebuffer, dialogX + 24 + choiceButtonWidth + gap, cby, choiceButtonWidth, choiceButtonHeight, "Y", "Remove", 2);
            drawEditChoiceButton(screen, framebuffer, dialogX + dialogWidth - 24 - choiceButtonWidth, cby, choiceButtonWidth, choiceButtonHeight, "A", "Confirm", 1);
        }

        // Confirm before deleting the selected item from the Items list. Mirrors the storage
        // release confirm: red frame, B = Cancel (id 0), A = Remove (id 1). The delete itself runs in
        // TrainerViewScreen's itemRemoveConfirmActive handler.
        void drawItemRemoveConfirm(TrainerViewScreen &screen, PKSEFramebuffer &framebuffer)
        {
            // The item id space is per-generation; the group decides which table names it.
            const Enums::GameVersion itemGroup = screen.trainer.getGameGroup();
            std::string itemName = "this item";
            if (screen.selectedCategory >= 0 && screen.selectedCategory < static_cast<int>(screen.trainer.items.size()))
            {
                const auto &pouch = screen.trainer.items[screen.selectedCategory];
                std::vector<int> visible = screen.visibleItemIndices();
                if (screen.selectedItemIndex >= 0 && screen.selectedItemIndex < static_cast<int>(visible.size()))
                {
                    const uint16_t selectedItemId = pouch[visible[screen.selectedItemIndex]].itemId;
                    itemName = Names::getItemNameFor(itemGroup, selectedItemId);
                }
            }

            constexpr int dialogWidth = 540, h = 226;
            const int dialogX = (framebuffer.getWidth() - dialogWidth) / 2;
            const int dialogY = (framebuffer.getHeight() - h) / 2;
            int centerY = drawDialogFrame(framebuffer, dialogX, dialogY, dialogWidth, h, "Remove Item", Colors::Red);
            framebuffer.drawText(dialogX + 28, centerY, "Remove " + itemName + "?", Colors::Text);
            framebuffer.drawText(dialogX + 28, centerY + 34, "It will be taken out of this pocket.", Colors::TextDim, TextStyle::Caption);

            screen.touchButtons.clear();
            const int boxWidth = 190, bh = TouchTargetMin, by = dialogY + h - bh - 18;
            const int remX = dialogX + dialogWidth - boxWidth - 20;   // right = Remove (id 1 -> A)
            const int cancelX = remX - boxWidth - 16; // left  = Cancel (id 0 -> B)
            drawEditChoiceButton(screen, framebuffer, cancelX, by, boxWidth, bh, "B", "Cancel", 0, Colors::PanelAlt);
            drawEditChoiceButton(screen, framebuffer, remX, by, boxWidth, bh, "A", "Remove", 1, Colors::Red, Colors::White);
        }

        void drawAddAllItemsDialog(TrainerViewScreen &screen, PKSEFramebuffer &framebuffer)
        {
            constexpr int dialogWidth = 600, dialogHeight = 440;
            const int dialogX = (framebuffer.getWidth() - dialogWidth) / 2;
            const int dialogY = (framebuffer.getHeight() - dialogHeight) / 2;
            const int contentTop = drawDialogFrame(framebuffer, dialogX, dialogY, dialogWidth, dialogHeight, "Add All Items", Colors::Text);

            screen.touchButtons.clear();

            framebuffer.drawText(dialogX + 24, contentTop, "AMOUNT TO ADD TO EACH ITEM", Colors::TextDim, TextStyle::Caption);
            const std::string amountText = std::to_string(screen.addAllItemsAmount);
            int amountWidth, amountHeight;
            framebuffer.measureText(amountText, amountWidth, amountHeight, TextStyle::Title);
            framebuffer.drawText(dialogX + (dialogWidth - amountWidth) / 2, contentTop + 24, amountText, Colors::Accent, TextStyle::Title);
            const std::string limitText = "Each item stops at the most the game allows.";
            int limitWidth, limitHeight;
            framebuffer.measureText(limitText, limitWidth, limitHeight, TextStyle::Caption);
            framebuffer.drawText(dialogX + (dialogWidth - limitWidth) / 2, contentTop + 24 + amountHeight + 4, limitText, Colors::TextDim, TextStyle::Caption);

            // The Include Key Items switch: a real touch target (id 41) as well as the X face button.
            const int switchX = dialogX + 24, switchY = contentTop + 110, switchWidth = dialogWidth - 48, switchHeight = 42;
            framebuffer.drawFilledRoundedRect(switchX, switchY, switchWidth, switchHeight, 8, Colors::PanelAlt);
            framebuffer.drawRoundedRect(switchX, switchY, switchWidth, switchHeight, 8, Colors::Border, 1);
            framebuffer.drawText(switchX + 16, switchY + (switchHeight - framebuffer.lineHeight(TextStyle::Body)) / 2, "Include Key Items", Colors::Text);
            {
                const char *switchStateText = screen.addAllItemsIncludesKeyItems ? "On" : "Off";
                int stateWidth, stateHeight;
                framebuffer.measureText(switchStateText, stateWidth, stateHeight, TextStyle::Body);
                const int glyphWidth = buttonGlyphWidth(framebuffer, "X");
                const int stateX = switchX + switchWidth - 16 - stateWidth;
                framebuffer.drawText(stateX, switchY + (switchHeight - stateHeight) / 2, switchStateText, screen.addAllItemsIncludesKeyItems ? Colors::Accent : Colors::TextDim);
                buttonGlyph(framebuffer, stateX - 12 - glyphWidth, switchY + switchHeight / 2, "X", false);
            }
            screen.touchButtons.push_back({41, switchX, switchY, switchWidth, switchHeight});
            if (screen.addAllItemsIncludesKeyItems)
            {
                framebuffer.drawText(switchX, switchY + switchHeight + 8, "Key items given early can skip or break story events.", Colors::Warning, TextStyle::Caption);
            }
            else
            {
                framebuffer.drawText(switchX, switchY + switchHeight + 8, "Key items are left out.", Colors::TextDim, TextStyle::Caption);
            }

            const int choiceButtonHeight = TouchTargetMin, choiceButtonTop = dialogY + dialogHeight - choiceButtonHeight - 14;
            drawEditStepRow(screen, framebuffer, dialogX, dialogWidth, choiceButtonTop - 58 - 14, 58);

            const int choiceButtonWidth = 180;
            drawEditChoiceButton(screen, framebuffer, dialogX + 24, choiceButtonTop, choiceButtonWidth, choiceButtonHeight, "B", "Cancel", 0);
            drawEditChoiceButton(screen, framebuffer, dialogX + dialogWidth - 24 - choiceButtonWidth, choiceButtonTop, choiceButtonWidth, choiceButtonHeight, "A", "Add", 1);
        }
    }
}
