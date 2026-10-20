#include <algorithm>
#include <cstdio>

#include "UI/BackupSelectionScreen.h"
#include "UI/Common.h"
#include "UI/ScreenChrome.h"
#include "UI/TouchInput.h"
#include "UI/Dialogs/DialogFrame.h"
#include "Utils/HelperUtilities.h"
#include "Utils/FileUtilities.h"
#include "Globals.h"

using namespace Utils;

namespace UI
{
    // UI Layout constants
    constexpr int CARD_X = 40;
    constexpr int CARD_Y = 84;
    constexpr int CARD_W = 1200;
    constexpr int LIST_ROW_H = 62;
    constexpr int LIST_MAX_VISIBLE = 8; // rows that fit in the card before scrolling

    // Scroll window: keep the selected row visible (centered when scrolling).
    static int firstVisibleRow(int selectedIndex, int total)
    {
        if (total <= LIST_MAX_VISIBLE)
            return 0;
        int first = selectedIndex - LIST_MAX_VISIBLE / 2;
        if (first < 0)
            first = 0;
        if (first > total - LIST_MAX_VISIBLE)
            first = total - LIST_MAX_VISIBLE;
        return first;
    }

    BackupSelectionScreen::BackupSelectionScreen(u64 titleId, const std::string &titleName, const std::string &titleFolder) : titleId(titleId), titleName(titleName), selectedIndex(0), backupSelected(false), createNewBackup(false), goBack(false), showDeleteConfirmation(false), deleteConfirmationIndex(-1)
    {
        // THE PATH TAKES THE FOLDER, NOT THE DISPLAY NAME. One string for both is how every Gen 3 language
        // SKU ends up sharing one backup directory -- see Save/TitleFolder.h.
        char gameDirBuf[512];
        snprintf(gameDirBuf, sizeof(gameDirBuf), "%s/%s", BASE_SAVE_DIRECTORY.c_str(), titleFolder.c_str());
        gameDirectory = gameDirBuf;

        loadBackups();
    }

    void BackupSelectionScreen::loadBackups()
    {
        std::vector<std::string> backupDirs = listBackupDirectories(gameDirectory.c_str());

        // Add the "Load from Title" action. With auto-backup ON this creates a new timestamped
        // backup; with it OFF the load reuses a single "Working" copy (see UIManager::handleBackupSelection),
        // so label it honestly rather than always promising a new backup.
        BackupInfo newBackupOption;
        newBackupOption.timestamp = "";
        newBackupOption.displayName = g_autoBackupEnabled ? "Load from Title (Create New Backup)" : "Load from Title (Working Copy)";
        backups.push_back(newBackupOption);

        for (const auto &timestamp : backupDirs)
        {
            BackupInfo info;
            info.timestamp = timestamp;
            info.displayName = formatTimestamp(timestamp);
            backups.push_back(info);
        }
    }

    std::string BackupSelectionScreen::formatTimestamp(const std::string &timestamp)
    {
        // Convert YYYYMMDD_HHMMSS to readable format: YYYY-MM-DD HH:MM:SS
        if (timestamp.length() != 15)
            return timestamp;

        std::string formatted;
        formatted += timestamp.substr(0, 4); // YYYY
        formatted += "-";
        formatted += timestamp.substr(4, 2); // MM
        formatted += "-";
        formatted += timestamp.substr(6, 2); // DD
        formatted += " ";
        formatted += timestamp.substr(9, 2); // HH
        formatted += ":";
        formatted += timestamp.substr(11, 2); // MM
        formatted += ":";
        formatted += timestamp.substr(13, 2); // SS

        return formatted;
    }

    /// Top of the list, under the heading divider. Shared by the draw and the touch hit test --
    /// when those two disagreed, every tap landed one row off.
    int BackupSelectionScreen::backupListTop() const { return CARD_Y + 62; }

    void BackupSelectionScreen::update(const PadState &pad, const TouchInput &touch)
    {
        const HidAnalogStickState stick = padGetStickPos(&pad, 0);
        u64 buttonsDown = controllerNavigation.apply(padGetButtonsDown(&pad), padGetButtons(&pad), stick.x, stick.y, HidNpadButton_Up, HidNpadButton_Down, HidNpadButton_Left, HidNpadButton_Right) | navTouchButton(touch); // nav-bar badges are tappable

        // A tap recorded earlier fires once its press has actually been on screen. See armTap().
        if (pendingTapButton != 0 && --pendingTapFrames <= 0)
        {
            buttonsDown |= pendingTapButton;
            pendingTapButton = 0;
            pendingTapButtonId = -1;
        }
        // expire the failure notice
        if (statusFrames > 0) --statusFrames;

        if (showDeleteConfirmation)
        {
            // Tappable buttons (captured last frame): A = Delete, B = Cancel.
            if (touch.justPressed())
            {
                auto isPressed = [&](const DlgBtn &b)
                {
                    return touch.x() >= b.hitX && touch.x() < b.hitX + b.hitWidth && touch.y() >= b.hitY && touch.y() < b.hitY + b.hitHeight;
                };
                if (isPressed(deleteDeleteBtn) && armTap(1, HidNpadButton_A))
                    return;
                if (isPressed(deleteCancelBtn) && armTap(0, HidNpadButton_B))
                    return;
            }
            if (buttonsDown & HidNpadButton_A)
            {
                deleteBackup(deleteConfirmationIndex);
                showDeleteConfirmation = false;
                deleteConfirmationIndex = -1;
                return;
            }
            if (buttonsDown & HidNpadButton_B)
            {
                // Cancel deletion
                showDeleteConfirmation = false;
                deleteConfirmationIndex = -1;
                return;
            }
            return; // Ignore other inputs while confirmation is shown
        }

        // Touch: tap a backup tile to select + open it (account for the scroll window).
        if (touch.justPressed())
        {
            const int startY = backupListTop(), tileX = CARD_X + 14, tileW = CARD_W - 28;
            if (touch.x() >= tileX && touch.x() < tileX + tileW && touch.y() >= startY)
            {
                int visIdx = (touch.y() - startY) / LIST_ROW_H;
                int index = firstVisibleRow(selectedIndex, (int)backups.size()) + visIdx;
                if (visIdx >= 0 && visIdx < LIST_MAX_VISIBLE && index < (int)backups.size())
                {
                    selectedIndex = index;              // draws selected this frame
                    // ...and opens on the next
                    if (armTap(-1, HidNpadButton_A)) return;
                }
            }
        }

        if (buttonsDown & HidNpadButton_B)
        {
            goBack = true;
            return;
        }

        if (buttonsDown & HidNpadButton_X)
        {
            // X button pressed - show delete confirmation for existing backups only
            if (selectedIndex > 0 && selectedIndex < (int)backups.size())
            {
                showDeleteConfirmation = true;
                deleteConfirmationIndex = selectedIndex;
            }
        }

        if (buttonsDown & HidNpadButton_Up)
        {
            if (selectedIndex > 0)
            {
                selectedIndex--;
            }
        }

        if (buttonsDown & HidNpadButton_Down)
        {
            if (selectedIndex < (int)backups.size() - 1)
            {
                selectedIndex++;
            }
        }

        if (buttonsDown & HidNpadButton_A)
        {
            const int row = selectedIndex;
            if (row < 0 || row >= (int)backups.size())
                return;
            if (row == 0)
            {
                // First option: Load from Title (Create New Backup)
                createNewBackup = true;
                backupSelected = true;
            }
            else
            {
                // Existing backup selected
                createNewBackup = false;
                char backupPath[512];
                snprintf(backupPath, sizeof(backupPath), "%s/%s", gameDirectory.c_str(), backups[row].timestamp.c_str());
                selectedBackupPath = backupPath;
                backupSelected = true;
            }
        }
    }

    void BackupSelectionScreen::draw(PKSEFramebuffer &framebuffer)
    {
        framebuffer.clear(Colors::Background);
        drawTitleBar(framebuffer, titleName);

        // Leave a real gap under the card so the nav bar's shadow falls on the background, not on
        // the card edge. (Was a bare "- 46", which sat flush against the taller bar.)
        const int cardH = framebuffer.getHeight() - CARD_Y - NAV_BAR_HEIGHT - 12;
        framebuffer.drawCard(CARD_X, CARD_Y, CARD_W, cardH);
        framebuffer.drawText(CARD_X + 18, CARD_Y + 14, "Save Backups", Colors::Text, TextStyle::Heading);
        framebuffer.drawHDivider(CARD_X + 18, CARD_Y + 50, CARD_W - 36);

        drawBackupList(framebuffer);

        if (selectedIndex > 0)
        {
            drawNavBar(framebuffer, "A: Select  |  X: Delete  |  B: Back");
        }
        else
        {
            drawNavBar(framebuffer, "A: Select  |  B: Back");
        }

        // Transient failure notice, centred just above the nav bar (same treatment as the editor's).
        if (statusFrames > 0 && !statusMessage.empty())
        {
            int textWidth, th;
            framebuffer.measureText(statusMessage, textWidth, th);
            const int padX = 18, bw = textWidth + padX * 2, bh = th + 14;
            const int boxX = (framebuffer.getWidth() - bw) / 2, by = framebuffer.getHeight() - NAV_BAR_HEIGHT - bh - 12;
            framebuffer.drawSoftShadow(boxX, by, bw, bh, 8);
            framebuffer.drawFilledRoundedRect(boxX, by, bw, bh, 8, Colors::Panel);
            framebuffer.drawRoundedRect(boxX, by, bw, bh, 8, Colors::Orange, 2);
            framebuffer.drawText(boxX + padX, by + 7, statusMessage, Colors::Text);
        }

        if (showDeleteConfirmation)
        {
            drawDeleteConfirmation(framebuffer);
        }
    }

    void BackupSelectionScreen::drawBackupList(PKSEFramebuffer &framebuffer)
    {
        const int startY = backupListTop();
        const int total = (int)backups.size();
        const int first = firstVisibleRow(selectedIndex, total);
        const int last = std::min(total, first + LIST_MAX_VISIBLE);
        for (int row = first; row < last; row++)
        {
            int itemY = startY + (row - first) * LIST_ROW_H;
            bool selected = (row == selectedIndex);
            // Row 0 is the "create new backup" action — accent it to stand out.
            drawHomeTile(framebuffer, CARD_X + 14, itemY, CARD_W - 28, LIST_ROW_H - 10, backups[row].displayName, selected, (row == 0));
        }
        // Scrollbar on the card's right edge when the list overflows -- same thumb as the details editor.
        drawScrollbar(framebuffer, CARD_X + CARD_W - 10, startY, LIST_MAX_VISIBLE * LIST_ROW_H, total * LIST_ROW_H, first * LIST_ROW_H);
    }

    void BackupSelectionScreen::drawDeleteConfirmation(PKSEFramebuffer &framebuffer)
    {
        constexpr int dialogWidth = 560, h = 248;
        const int dialogX = (framebuffer.getWidth() - dialogWidth) / 2;
        const int dialogY = (framebuffer.getHeight() - h) / 2;

        int centerY = Dialogs::drawDialogFrame(framebuffer, dialogX, dialogY, dialogWidth, h, "Delete Backup?", Colors::Warning);
        framebuffer.drawText(dialogX + 24, centerY, "This action cannot be undone.", Colors::TextDim);
        if (deleteConfirmationIndex >= 0 && deleteConfirmationIndex < (int)backups.size())
        {
            framebuffer.drawText(dialogX + 24, centerY + 28, backups[deleteConfirmationIndex].displayName, Colors::Text);
        }

        // Buttons carry their glyph (B: Cancel, A: Delete); Delete stays red. Rects are stashed so
        // update() can hit-test taps next frame.
        const int buttonHeight = TouchTargetMin, by = dialogY + h - buttonHeight - 16, bw = (dialogWidth - 48 - 16) / 2;
        const int cancelX = dialogX + 24, delX = dialogX + dialogWidth - 24 - bw;
        drawGlyphButton(framebuffer, cancelX, by, bw, buttonHeight, "B", "Cancel", Colors::PanelAlt, Colors::Text, pendingTapButtonId == 0);
        drawGlyphButton(framebuffer, delX, by, bw, buttonHeight, "A", "Delete", Colors::Red, Colors::White, pendingTapButtonId == 1);
        deleteCancelBtn = {cancelX, by, bw, buttonHeight};
        deleteDeleteBtn = {delX, by, bw, buttonHeight};
    }

    bool BackupSelectionScreen::armTap(int buttonId, u64 button)
    {
        pendingTapButtonId = buttonId;
        pendingTapButton = button;
        pendingTapFrames = TAP_PRESS_FRAMES;
        return true;
    }

    void BackupSelectionScreen::deleteBackup(int index)
    {
        if (index <= 0 || index >= (int)backups.size())
        {
            return; // Can't delete the "Load from Title" option or invalid index
        }

        char backupPath[512];
        snprintf(backupPath, sizeof(backupPath), "%s/%s", gameDirectory.c_str(), backups[index].timestamp.c_str());

        if (deleteDirectoryRecursive(backupPath))
        {
            backups.erase(backups.begin() + index);

            if (selectedIndex >= (int)backups.size())
            {
                selectedIndex = (int)backups.size() - 1;
            }
        }
    }
}
