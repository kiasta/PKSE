#include <cstring>
#include <cstdio>

#include "Globals.h"
#include "UI/SaveSelectScreen.h"
#include "UI/Common.h"
#include "UI/ScreenChrome.h"
#include "UI/ListSearch.h"
#include "UI/Dialogs/KeyboardDialog.h"
#include "UI/SystemIcons.h"
#include "UI/TouchInput.h"
#include "Enums/GameVersion.h"
#include "Utils/Logger.h"
#include "Utils/FileUtilities.h"
#include "Save/GetSaveFileContents.h"
#include "Save/SaveFileName.h"   // the one rule for what looks like a loose save
#include "Save/TitleFolder.h"    // the one rule for what a title is called, on screen and on the card

using namespace Utils;
using namespace Enums;

namespace UI
{
    // Layout (1280x720).
    constexpr int HEADER_Y = 82; // top of the user header card
    constexpr int HEADER_H = 104;
    constexpr int AVATAR = 84;
    constexpr int CHIP = 56; // small per-user switcher avatar
    constexpr int TILE_W = 184;
    constexpr int TILE_H = 200;
    constexpr int ICON = 140;
    constexpr int CARD_GAP = 24;
    constexpr int MAX_COLS = 5;
    constexpr int GRID_Y = 208;
    // Rows of tiles that fit between the grid's top and the nav bar: (720 - 56 - 208) = 456px of band
    // against a 224px row pitch. Anything past this scrolls rather than being drawn off the bottom edge.
    constexpr int VISIBLE_ROWS = 2;

    SaveSelectScreen::SaveSelectScreen()
    {
        loadUsers();
    }

    void SaveSelectScreen::loadUsers()
    {
        users.clear();

        AccountUid userIds[ACC_USER_LIST_SIZE];
        s32 userCount = 0;
        Result resultCode = accountListAllUsers(userIds, ACC_USER_LIST_SIZE, &userCount);
        if (R_FAILED(resultCode))
        {
            logErrorToFile("SaveSelect: failed to list users");
            userCount = 0;
        }

        for (s32 userIndex = 0; userIndex < userCount; userIndex++)
        {
            UserEntry entry;
            entry.accountUid = userIds[userIndex];

            AccountProfile profile;
            AccountProfileBase base;
            if (R_SUCCEEDED(accountGetProfile(&profile, userIds[userIndex])))
            {
                if (R_SUCCEEDED(accountProfileGet(&profile, NULL, &base)))
                {
                    entry.name = std::string(base.nickname);
                }
                else
                {
                    entry.name = "Unknown User";
                }
                accountProfileClose(&profile);
            }
            else
            {
                entry.name = "Unknown User";
            }

            loadTitlesForUser(entry);
            users.push_back(std::move(entry));
        }

        // BACKUP FOLDERS CARRY THE TITLE ID, so anything written under the older naming has to be moved or
        // it simply stops being listed -- on every user's card, not just the one where the collision was
        // found. Run after EVERY user is loaded, because the backup tree is shared between users and
        // whether a legacy folder is ambiguous depends on every title the console has, not just this one's.
        // See Save::migrateLegacyTitleFolders: it refuses the ambiguous ones rather than guessing, and a
        // failure leaves the old folder exactly where it was, which is what makes it safe to attempt on
        // every launch.
        std::vector<Save::InstalledTitle> installedTitles;
        for (const UserEntry &user : users)
        {
            for (const TitleEntry &title : user.titles)
            {
                bool alreadyCounted = false;
                for (const Save::InstalledTitle &counted : installedTitles)
                    alreadyCounted = alreadyCounted || counted.titleId == title.titleId;
                if (!alreadyCounted)
                    installedTitles.push_back({title.titleId, title.gameVersion});
            }
        }
        Save::migrateLegacyTitleFolders(installedTitles);

        if (users.empty())
        {
            UserEntry defaultUser;
            defaultUser.name = "No users found";
            memset(&defaultUser.accountUid, 0, sizeof(AccountUid));
            users.push_back(std::move(defaultUser));
        }
    }

    namespace
    {
        /**
         * Identifies a Pokemon save PKSE knows, from its CONTENT, for a title id it does not.
         *
         * A Switch save is found by title id, so `getGameVersion()` is a hand-kept list -- and a
         * hand-kept list of Nintendo's title ids goes stale the moment a title ships that nobody
         * has reported. That is not hypothetical: FireRed and LeafGreen ship a SEPARATE TITLE PER
         * LANGUAGE (the GBA originals have no in-game language option), so listing only the
         * English pair made a French LeafGreen owner see "0 saves found" -- the save was there,
         * readable, and simply never offered. Every other Pokemon title on Switch is one
         * worldwide id, which is why Gen 3 is the only family this can currently discover.
         *
         * Returns the GROUP, never a specific game: a Gen 3 save carries no byte saying whether it
         * is FireRed or LeafGreen, so a probed save is named for the pair, exactly as an unnamed
         * Red/Blue save is. A title id PKSE recognises still takes the fast path above and is
         * named exactly -- this only runs for ids it does not.
         *
         * Every failure is silent and non-fatal: an id that is not a Pokemon title is the normal
         * case, and a console's other games must cost nothing but a mount that immediately fails
         * or finds nothing.
         */
        Enums::GameVersion probeUnknownSave(AccountUid userUid, u64 titleId)
        {
            if (R_FAILED(fsdevMountSaveData("probe", titleId, userUid)))
                return Enums::GameVersion::Invalid;
            // One implementation of "what is in this directory", shared with the loader -- see
            // Save::detectGroupInDirectory. A mounted save and a backup folder are both just
            // directories, and two probes that had to agree would be one more pair of lists that
            // eventually do not.
            const Enums::GameVersion found = Save::detectGroupInDirectory("probe:");
            fsdevUnmountDevice("probe");
            return found;
        }
    }

    /**
     * List the Pokemon saves this user has, by enumerating SAVE DATA -- not installed titles.
     *
     * The distinction is the whole point. Walking `nsListApplicationRecord` and probing each Pokemon
     * title for a mountable save asks "which games are installed, and do they have saves?" when the
     * only question a save editor cares about is "which saves exist?" -- and the two differ in a case
     * that is not rare at all:
     *
     *   A game played from a CARTRIDGE has no application record when the cart is out. Its save
     *   lives on internal storage and is perfectly editable, but the title vanishes from the picker
     *   the moment the cart is swapped for another game. An archived or partly-uninstalled title
     *   does the same.
     *
     * Enumerating save data finds those, because the OS lists the save whether or not anything is
     * currently installed to play it. It is also cheaper: no mount/unmount probe per title, which is
     * both slow and a devoptab slot churn.
     */
    // Scan one save-data space and append this user's Pokemon saves. Returns false only if the
    // space could not be opened at all; an empty space is a perfectly normal success.
    bool SaveSelectScreen::scanSaveSpace(UserEntry &user, int spaceId, int &scanned, int &forUser)
    {
        FsSaveDataInfoReader reader;
        Result resultCode = fsOpenSaveDataInfoReader(&reader, static_cast<FsSaveDataSpaceId>(spaceId));
        if (R_FAILED(resultCode))
        {
            char messageBuffer[112];
            snprintf(messageBuffer, sizeof(messageBuffer), "SaveSelect: cannot read save space %d (rc=0x%08X)", spaceId, (unsigned)resultCode);
            logInfoToFile(messageBuffer);
            return false;
        }

        FsSaveDataInfo info[24];
        s64 readCount = 0;
        while (R_SUCCEEDED(fsSaveDataInfoReaderRead(&reader, info, 24, &readCount)) && readCount > 0)
        {
            for (s64 infoIndex = 0; infoIndex < readCount; infoIndex++)
            {
                scanned++;
                // Account saves only -- system/temporary/cache entries are not a player's save file.
                if (info[infoIndex].save_data_type != FsSaveDataType_Account)
                    continue;
                if (memcmp(&info[infoIndex].uid, &user.accountUid, sizeof(AccountUid)) != 0)
                    continue;
                forUser++;

                const u64 titleId = info[infoIndex].application_id;
                GameVersion gameVersion = getGameVersion(titleId);
                if (gameVersion == GameVersion::Invalid)
                {
                    // Not an id PKSE knows -- which is the normal case for every non-Pokemon game
                    // on the console, and ALSO the case for a Pokemon title whose id nobody has
                    // reported yet. Ask the bytes before giving up; see probeUnknownSave.
                    gameVersion = probeUnknownSave(user.accountUid, titleId);
                    if (gameVersion == GameVersion::Invalid)
                        continue;
                    char probeBuffer[128];
                    snprintf(probeBuffer, sizeof(probeBuffer), "SaveSelect: %016llX unknown id, identified by content as %s", (unsigned long long)titleId, getGameVersionName(gameVersion).c_str());
                    logInfoToFile(probeBuffer);
                }

                // One tile per game. A title can report more than one save entry (save_data_index),
                // and scanning two spaces can see the same save twice -- listing a game twice would
                // be worse than useless.
                bool alreadyListed = false;
                for (const auto &titleEntry : user.titles)
                {
                    if (titleEntry.titleId == titleId)
                    {
                        alreadyListed = true;
                        break;
                    }
                }
                if (alreadyListed)
                    continue;

                char messageBuffer[128];
                snprintf(messageBuffer, sizeof(messageBuffer), "SaveSelect: %s (%016llX) -> listed", getGameVersionName(gameVersion).c_str(), (unsigned long long)titleId);
                logInfoToFile(messageBuffer);

                TitleEntry titleEntry;
                titleEntry.titleId = titleId;
                // BOTH NAMES COME FROM Save/TitleFolder.h, and they are deliberately different
                // strings: the label has 184 pixels to be readable in and the folder has to be
                // UNIQUE PER TITLE ID. Building either from the GameVersion alone is what let every
                // Gen 3 SKU share one backup directory -- see the note in that header.
                titleEntry.gameVersion = gameVersion;
                titleEntry.label = Save::titleDisplayLabel(titleId, gameVersion);
                titleEntry.name = Save::titleDisplayName(titleId, gameVersion);
                titleEntry.folder = Save::titleFolderName(titleId, gameVersion);
                user.titles.push_back(std::move(titleEntry));
            }
        }
        fsSaveDataInfoReaderClose(&reader);
        return true;
    }

    void SaveSelectScreen::loadTitlesForUser(UserEntry &user)
    {
        int scanned = 0, forUser = 0;

        // `All` is the pseudo-space the header blesses for this reader, and it is what should
        // normally answer. Fall back to the concrete spaces if it is refused, because "found
        // nothing" is precisely the failure this function exists to stop producing.
        if (!scanSaveSpace(user, FsSaveDataSpaceId_All, scanned, forUser))
        {
            scanSaveSpace(user, FsSaveDataSpaceId_User, scanned, forUser);
            scanSaveSpace(user, FsSaveDataSpaceId_SdUser, scanned, forUser);
        }

        // Counts make a missing title diagnosable from the log alone: how many saves the console
        // reported in total, how many belong to this user, and how many were Pokemon titles.
        char summary[192];
        snprintf(summary, sizeof(summary), "SaveSelect: %d save entries on console, %d for %s, %d Pokemon titles listed", scanned, forUser, user.name.c_str(), (int)user.titles.size());
        logInfoToFile(summary);
    }

    const SaveSelectScreen::UserEntry *SaveSelectScreen::currentUser() const
    {
        if (users.empty())
            return nullptr;
        return &users[userIndex];
    }

    int SaveSelectScreen::titleCount() const
    {
        const UserEntry *u = currentUser();
        return u ? static_cast<int>(u->titles.size()) : 0;
    }

    int SaveSelectScreen::titleColumns() const
    {
        int count = titleCount();
        if (count <= 0)
            return 1;
        return count < MAX_COLS ? count : MAX_COLS;
    }

    // Rows of tiles, at MAX_COLS per row.
    int SaveSelectScreen::titleRows() const
    {
        const int count = titleCount();
        if (count <= 0)
            return 0;
        const int cols = titleColumns();
        return (count + cols - 1) / cols;
    }

    /**
     * Move the scroll window as LITTLE as possible to keep the selected tile on screen.
     *
     * `scrollRow` is deliberately STATE, not something recomputed from `titleIndex` each frame. The
     * derived version centred the selection -- `first = selRow - VISIBLE_ROWS/2` -- which reads as
     * paging: with three rows, selecting the bottom row shows rows 1-2, and moving back up to the
     * middle row snapped the view to rows 0-1 even though the middle row was *already visible*.
     * Every vertical move repainted the whole grid.
     *
     * Scrolling only when the selection would otherwise fall outside the window means the view
     * holds still while the cursor moves inside it, and shifts by exactly one row at the edges.
     * With three rows that is: nothing at all between the top two rows, one row down when you enter
     * the bottom row, and one row back up only when you leave the top row.
     */
    void SaveSelectScreen::scrollSelectionIntoView()
    {
        const int rows = titleRows();
        if (rows <= VISIBLE_ROWS)
        {
            scrollRow = 0;
            return;
        } // everything fits; never offset

        const int selRow = titleIndex / titleColumns();
        if (selRow < scrollRow)
            scrollRow = selRow;
        else if (selRow >= scrollRow + VISIBLE_ROWS)
            scrollRow = selRow - VISIBLE_ROWS + 1;

        // Clamp for the case the row count shrank under us (switching to a user with fewer saves).
        if (scrollRow > rows - VISIBLE_ROWS)
            scrollRow = rows - VISIBLE_ROWS;
        if (scrollRow < 0)
            scrollRow = 0;
    }

    void SaveSelectScreen::setUser(int index)
    {
        if (users.empty())
            return;
        userIndex = (index % (int)users.size() + (int)users.size()) % (int)users.size();
        titleIndex = 0;
        scrollRow = 0; // a different user has a different title count; start at the top
    }

    void SaveSelectScreen::selectCurrentTitle()
    {
        const UserEntry *u = currentUser();
        if (!u || titleIndex < 0 || titleIndex >= static_cast<int>(u->titles.size()))
            return;
        selectedUserUid = u->accountUid;
        selectedTitleId = u->titles[titleIndex].titleId;
        selectedTitleName = u->titles[titleIndex].name;
        selectedTitleFolder = u->titles[titleIndex].folder;
        titleSelected = true;
    }

    void SaveSelectScreen::update(const PadState &pad, const TouchInput &touch)
    {
        // A tap on a nav-bar badge becomes that button's press, so every handler below is
        // reached identically whether the user pressed the button or tapped its on-screen badge.
        const HidAnalogStickState stick = padGetStickPos(&pad, 0);
        u64 buttonsDown = controllerNavigation.apply(
            padGetButtonsDown(&pad), padGetButtons(&pad), stick.x, stick.y,
            HidNpadButton_Up, HidNpadButton_Down, HidNpadButton_Left, HidNpadButton_Right,
            HidNpadButton_L | HidNpadButton_R)
            | navTouchButton(touch);

        // A tap recorded earlier fires once its selection has actually been on screen. See armTap().
        if (pendingTapButton != 0 && --pendingTapFrames <= 0)
        {
            buttonsDown |= pendingTapButton;
            pendingTapButton = 0;
        }

        // The browser owns input entirely while it is open -- otherwise the grid underneath would
        // move under a cursor the user cannot see, and B would exit the screen instead of the
        // dialog. Same rule every other overlay in the app follows.
        if (fileBrowser.active)
        {
            handleFileBrowserInput(buttonsDown, touch);
            return;
        }
        if (buttonsDown & HidNpadButton_Y)
        {
            openFileBrowser();
            return;
        }
        // Touch (tap targets were captured last draw()).
        if (touch.justPressed())
        {
            const int touchX = touch.x(), ty = touch.y();
            bool handled = false;
            for (const auto &r : userRects)
            {
                if (touchX >= r.hitX && touchX < r.hitX + r.hitWidth && ty >= r.hitY && ty < r.hitY + r.hitHeight)
                {
                    setUser(r.entryIndex);
                    handled = true;
                    break;
                }
            }
            if (!handled)
            {
                for (const auto &r : titleRects)
                {
                    if (touchX >= r.hitX && touchX < r.hitX + r.hitWidth && ty >= r.hitY && ty < r.hitY + r.hitHeight)
                    {
                        titleIndex = r.entryIndex;      // draws selected this frame
                        armTap(HidNpadButton_A);        // ...and opens on the next
                        return;
                    }
                }
            }
        }

        // Switch users with the shoulder buttons (only when there's more than one).
        if (users.size() > 1)
        {
            if (buttonsDown & HidNpadButton_L)
                setUser(userIndex - 1);
            if (buttonsDown & HidNpadButton_R)
                setUser(userIndex + 1);
        }

        // Title grid navigation. titleCount() already resolves the current user.
        int count = titleCount();
        if (count > 0)
        {
            int cols = titleColumns();
            if (buttonsDown & HidNpadButton_Left)
                titleIndex = (titleIndex - 1 + count) % count;
            if (buttonsDown & HidNpadButton_Right)
                titleIndex = (titleIndex + 1) % count;
            if (buttonsDown & HidNpadButton_Down)
            {
                // A partial last row still has to be reachable. Straight down when that column
                // exists below; otherwise fall to the final tile rather than refusing to move --
                // with twelve titles the bottom row is just two wide, and Down from the right-hand
                // columns would otherwise do nothing at all.
                if (titleIndex + cols < count)
                    titleIndex += cols;
                else if (titleIndex / cols < (count - 1) / cols)
                    titleIndex = count - 1;
            }
            if ((buttonsDown & HidNpadButton_Up) && titleIndex - cols >= 0)
                titleIndex -= cols;
            if (buttonsDown & HidNpadButton_A)
                selectCurrentTitle();
            // One place, after every way the selection can move -- stick, D-pad or a tap.
            scrollSelectionIntoView();
        }

        if (buttonsDown & HidNpadButton_Plus)
            exitRequested = true;
    }

    void SaveSelectScreen::draw(PKSEFramebuffer &framebuffer)
    {
        titleRects.clear();
        userRects.clear();

        framebuffer.clear(Colors::Background);
        drawTitleBar(framebuffer, "Pokémon Save Editor   v" + VERSION_STRING);

        const UserEntry *u = currentUser();

        framebuffer.drawCard(24, HEADER_Y, framebuffer.getWidth() - 48, HEADER_H);

        const int avatarX = 44, avY = HEADER_Y + (HEADER_H - AVATAR) / 2;
        if (u)
        {
            const IconImage &av = SystemIcons::userIcon(u->accountUid);
            if (av.valid())
                framebuffer.drawImageScaled(avatarX, avY, av.width, av.height, AVATAR, AVATAR, av.data, 4);
            else
                framebuffer.drawFilledRoundedRect(avatarX, avY, AVATAR, AVATAR, 10, Colors::Selected);
            framebuffer.drawRoundedRect(avatarX, avY, AVATAR, AVATAR, 10, Colors::Accent, 2);
        }

        const int nameX = avatarX + AVATAR + 20;
        if (u)
        {
            int nameLineHeight = framebuffer.lineHeight(TextStyle::Title);
            framebuffer.drawText(nameX, avY + 6, u->name, Colors::Text, TextStyle::Title);
            int saveCount = (int)u->titles.size();
            std::string subtitleText =
                std::to_string(saveCount) + (saveCount == 1 ? " Pokémon save" : " Pokémon saves");
            framebuffer.drawText(nameX, avY + 6 + nameLineHeight + 4, subtitleText, Colors::TextDim, TextStyle::Caption);
        }

        // Per-user switcher chips (only when more than one account).
        if (users.size() > 1)
        {
            int userCountOnScreen = (int)users.size();
            int totalW = userCountOnScreen * CHIP + (userCountOnScreen - 1) * 12;
            int startX = framebuffer.getWidth() - 40 - totalW;
            int chipY = HEADER_Y + (HEADER_H - CHIP) / 2;
            for (int index = 0; index < userCountOnScreen; index++)
            {
                int centerX = startX + index * (CHIP + 12);
                const IconImage &av = SystemIcons::userIcon(users[index].accountUid);
                if (av.valid())
                    framebuffer.drawImageScaled(centerX, chipY, av.width, av.height, CHIP, CHIP, av.data, 4);
                else
                    framebuffer.drawFilledRoundedRect(centerX, chipY, CHIP, CHIP, 8, Colors::Selected);
                if (index == userIndex)
                    framebuffer.drawRoundedRect(centerX, chipY, CHIP, CHIP, 8, Colors::Primary, 3);
                else
                    framebuffer.drawRoundedRect(centerX, chipY, CHIP, CHIP, 8, Colors::Border, 1);
                userRects.push_back({centerX, chipY, CHIP, CHIP, index});
            }
        }

        int count = titleCount();
        if (count == 0)
        {
            const char *message = "No Pokémon saves found for this user";
            int messageWidth, mh;
            framebuffer.measureText(message, messageWidth, mh, TextStyle::Body);
            framebuffer.drawText((framebuffer.getWidth() - messageWidth) / 2, GRID_Y + 120, message, Colors::TextDim, TextStyle::Body);
        }
        else
        {
            const int cols = titleColumns();
            const int rows = titleRows();
            const int first = scrollRow;
            const int gridW = cols * TILE_W + (cols - 1) * CARD_GAP;
            int startX = (framebuffer.getWidth() - gridW) / 2;
            if (startX < 40)
                startX = 40;

            // Only the rows inside the window are drawn, and only those get tap targets -- an
            // off-screen tile must not be tappable through whatever is covering it.
            const int firstIdx = first * cols;
            const int lastIdx = (first + VISIBLE_ROWS) * cols; // exclusive

            for (int index = firstIdx; index < count && index < lastIdx; index++)
            {
                int columnIndex = index % cols, row = (index / cols) - first;
                int tileX = startX + columnIndex * (TILE_W + CARD_GAP);
                int tileY = GRID_Y + row * (TILE_H + CARD_GAP);
                bool selectedIndex = (index == titleIndex);

                framebuffer.drawSoftShadow(tileX, tileY, TILE_W, TILE_H, 16);
                framebuffer.drawFilledRoundedRect(tileX, tileY, TILE_W, TILE_H, 16, selectedIndex ? Colors::PanelAlt : Colors::Panel);
                if (selectedIndex)
                    framebuffer.drawRoundedRect(tileX, tileY, TILE_W, TILE_H, 16, Colors::Primary, 3);
                else
                    framebuffer.drawRoundedRect(tileX, tileY, TILE_W, TILE_H, 16, Colors::Border, 1);

                int iconX = tileX + (TILE_W - ICON) / 2;
                int iconY = tileY + 16;
                const IconImage &ic = SystemIcons::titleIcon(u->titles[index].titleId);
                if (ic.valid())
                    framebuffer.drawImageScaled(iconX, iconY, ic.width, ic.height, ICON, ICON, ic.data, 4);
                else
                    framebuffer.drawFilledRoundedRect(iconX, iconY, ICON, ICON, 10, Colors::PanelAlt);

                // Short label centered under the icon.
                const std::string &label = u->titles[index].label;
                int labelWidth, lh;
                framebuffer.measureText(label, labelWidth, lh, TextStyle::Caption);
                framebuffer.drawText(tileX + (TILE_W - labelWidth) / 2, iconY + ICON + 8, label,
                            selectedIndex ? Colors::Text : Colors::TextDim, TextStyle::Caption);

                titleRects.push_back({tileX, tileY, TILE_W, TILE_H, index});
            }

            // Scrollbar to the right of the grid, drawn only when the rows overflow -- same thumb
            // the backup list and the details editor use.
            constexpr int ROW_PITCH = TILE_H + CARD_GAP;
            drawScrollbar(framebuffer, startX + gridW + 12, GRID_Y, VISIBLE_ROWS * ROW_PITCH, rows * ROW_PITCH,
                          first * ROW_PITCH);
        }

        // ONE nav bar, naming whatever owns input. A second bar drawn over this, plus a third strip inside
        // the browser card, is two visible rows of controller badges saying different things about the same
        // dialog. The bar is chosen here and drawn once, the same way TrainerViewScreen's instruction chain
        // does it.
        if (const Dialogs::KeyboardState *keyboard = Dialogs::activeKeyboard())
            drawNavBar(framebuffer, keyboard->navHint());
        else if (fileBrowser.active)
            drawNavBar(framebuffer, "Up/Down: Move  |  L/R: Page  |  A: Open  |  Y: Search  |  X: All Files  |  B: Up / Close");
        else if (users.size() > 1)
            drawNavBar(framebuffer, "A: Select  |  L/R: Switch User  |  Y: Open Save File  |  +: Exit");
        else
            drawNavBar(framebuffer, "A: Select  |  Y: Open Save File  |  +: Exit");

        // Drawn last so it sits above the grid.
        if (fileBrowser.active)
            Dialogs::drawFileBrowser(fileBrowser, framebuffer, browserTaps);
    }

    void SaveSelectScreen::openFileBrowser()
    {
        // Start where a Game Boy save plausibly is. Emulators on the Switch keep saves beside
        // their ROMs, and PKSE's own backup tree is the other likely home; the browser falls
        // through to the first of these that exists.
        fileBrowser.open("Open a Save File", "Save files",
            {"sdmc:/PKSE/saves/", "sdmc:/PKSE/", "sdmc:/roms/", "sdmc:/retroarch/saves/", "sdmc:/emulators/", "sdmc:/"},
            Save::saveFileExtensions(), Save::saveFileExactNames());
        logEventToFile("OPENSAVE action=BROWSE dir=\"" + fileBrowser.directory + "\"");
    }

    bool SaveSelectScreen::armTap(u64 button)
    {
        pendingTapButton = button;
        pendingTapFrames = TAP_PRESS_FRAMES;
        return true;
    }

    void SaveSelectScreen::handleFileBrowserInput(u64 buttonsDown, const TouchInput &touch)
    {
        if (buttonsDown & HidNpadButton_Up)
            fileBrowser.move(-1);
        if (buttonsDown & HidNpadButton_Down)
            fileBrowser.move(1);
        if (buttonsDown & HidNpadButton_L)
            fileBrowser.move(-Dialogs::FileBrowserState::VISIBLE_ROWS);
        if (buttonsDown & HidNpadButton_R)
            fileBrowser.move(Dialogs::FileBrowserState::VISIBLE_ROWS);
        if (buttonsDown & HidNpadButton_Y)
        {
            // Search. refresh() re-lists the folder through the query, so an empty
            // query -- the shared "clear" gesture -- simply lists it all again. Not while typing:
            // refresh() re-reads the folder off the card, which a big folder cannot do per key.
            if (fileBrowser.search.promptForQuery("Files", ListSearch::FilterTiming::OnAccept))
            {
                fileBrowser.returnTo.clear();
                fileBrowser.refresh();
            }
            return;
        }
        if (buttonsDown & HidNpadButton_X)
        {
            fileBrowser.showAllFiles = !fileBrowser.showAllFiles;
            fileBrowser.refresh();
        }
        if (buttonsDown & HidNpadButton_B)
        {
            if (!fileBrowser.goUp())
                fileBrowser.close();
            return;
        }

        bool activate = (buttonsDown & HidNpadButton_A) != 0;
        if (touch.justPressed())
        {
            for (const auto &r : browserTaps)
            {
                if (touch.x() >= r.hitX && touch.x() < r.hitX + r.hitWidth && touch.y() >= r.hitY &&
                    touch.y() < r.hitY + r.hitHeight)
                {
                    fileBrowser.selectedIndex = r.buttonId; // draws selected this frame
                    fileBrowser.move(0);
                    armTap(HidNpadButton_A);                // ...and opens on the next
                    return;
                }
            }
        }
        if (!activate || !fileBrowser.activate())
            return;

        const std::string picked = fileBrowser.chosenPath;
        fileBrowser.chosenPath.clear();
        const size_t slashPosition = picked.find_last_of('/');
        const std::string pickedName = (slashPosition == std::string::npos) ? picked : picked.substr(slashPosition + 1);

        // THE OPEN IS THE CHECK, made BEFORE leaving the picker. A file that passed a lighter test and
        // then failed to open dropped the user back on the title grid with nothing said, so every
        // refusal is reported here, in the browser, with the file still on screen and named.
        size_t length = 0;
        uint8_t *bytes = Utils::readAllBytes(picked.c_str(), &length);
        if (!bytes)
        {
            fileBrowser.status = "Couldn't read \"" + pickedName + "\".";
            logEventToFile("OPENSAVE action=PICK file=\"" + picked + "\" result=UNREADABLE");
            return;
        }
        std::vector<uint8_t> fileBytes(bytes, bytes + length);
        delete[] bytes;
        std::string refusalText;
        std::unique_ptr<Trainer::Trainer> openedTrainer =
            Save::openExternalSave(std::move(fileBytes), picked, &selectedFileLabel, &refusalText);
        if (!openedTrainer)
        {
            fileBrowser.status = "\"" + pickedName + "\" " + refusalText + ".";
            logEventToFile("OPENSAVE action=PICK file=\"" + picked + "\" size=" + std::to_string(length) +
                           " result=UNRECOGNISED reason=\"" + refusalText + "\"");
            return;
        }

        selectedFileTrainer = std::move(openedTrainer);
        selectedFilePath = picked;
        fileSelected = true;
        fileBrowser.close();
        logEventToFile("OPENSAVE action=PICK file=\"" + picked + "\" size=" + std::to_string(length) + " result=OK");
    }
}
