#ifndef UI_SAVE_SELECT_SCREEN_H
#define UI_SAVE_SELECT_SCREEN_H

#include <memory>
#include <vector>
#include <string>

#include <switch.h>

#include "Enums/GameVersion.h" // a tile remembers the version its folder and label were built from
#include "Trainer/Trainer.h"
#include "UI/UIScreen.h"
#include "UI/NavigationRepeat.h"
#include "UI/PKSEFramebuffer.h"
#include "UI/Common.h"
#include "UI/Dialogs/FileBrowserDialog.h"

namespace UI
{
    // JKSV-style combined user + title picker. Shows the selected user's avatar and name at the top and a
    // grid of that user's supported Pokemon game icons below. Switch users with the L/R shoulders (or by
    // tapping a user chip) when more than one account exists. Only titles PKSE supports, and only ones the
    // user actually has a save for, appear.
    class SaveSelectScreen : public UIScreen
    {
    public:
        SaveSelectScreen();
        void update(const PadState &pad, const TouchInput &touch) override;
        void draw(PKSEFramebuffer &framebuffer) override;
        bool shouldExit() const override { return exitRequested; }

        bool hasSelectedTitle() const { return titleSelected; }

        // opening a loose save file (Gen 1 and, in time, every pre-Switch generation)
        // A Game Boy .sav has no title id and belongs to no Switch user, so it cannot appear in
        // the grid above -- the grid is built by enumerating SAVE DATA for an account. Y opens a
        // file browser instead, and the picked path leaves by its own accessor so the caller can
        // tell "user chose an installed title" from "user chose a file on the SD card".
        bool hasSelectedFile() const { return fileSelected; }
        const std::string &getSelectedFilePath() const { return selectedFilePath; }
        /// The file is OPENED before the browser closes -- that is the check -- and handed over
        /// as it was opened, so nothing can open it differently afterwards.
        std::unique_ptr<Trainer::Trainer> takeSelectedFileTrainer() { return std::move(selectedFileTrainer); }
        const std::string &getSelectedFileLabel() const { return selectedFileLabel; }

        AccountUid getSelectedUser() const { return selectedUserUid; }
        u64 getSelectedTitleId() const { return selectedTitleId; }
        const std::string &getSelectedTitleName() const { return selectedTitleName; }
        struct TitleEntry
        {
            u64 titleId;
            std::string name;   // prose: "Pokemon Shield", for title bars and dialog text
            std::string label;  // short display name under the icon (e.g. "Shield")
            std::string folder; // backup directory name, carrying the title id -- NOT the prose
            /// The version the folder and label were BUILT from. Not re-derivable: an id PKSE does
            /// not recognise is resolved by CONTENT, and getGameVersion() answers Invalid for it.
            Enums::GameVersion gameVersion = Enums::GameVersion::Invalid;
        };
        struct UserEntry
        {
            AccountUid accountUid;
            std::string name;
            std::vector<TitleEntry> titles;
        };

        /// The backup DIRECTORY, which is not the display name -- see Save/TitleFolder.h.
        const std::string &getSelectedTitleFolder() const { return selectedTitleFolder; }

        /// Every user and title this screen enumerated, for a caller that needs the OTHER saves on
        /// the console rather than the chosen one -- the editor's trade-partner picker, which has to
        /// offer the games this Pokemon could have been traded to. Exposed rather than enumerated
        /// twice: walking save data needs fs/ns and this screen has already done it.
        const std::vector<UserEntry> &allUsers() const { return users; }

    private:
        ControllerNavigation controllerNavigation;

        struct HitRect
        {
            int hitX, hitY, hitWidth, hitHeight, entryIndex;
        };

        std::vector<UserEntry> users;

        int userIndex = 0;
        /// Indexes the current user's `titles` directly. The grid has no search to filter it:
        /// it holds one tile per Pokemon save this console has, all of them on screen at once,
        /// so there is nothing for a query to narrow. The file browser Y raises does have one.
        int titleIndex = 0;

        bool titleSelected = false;
        bool exitRequested = false;
        AccountUid selectedUserUid{};
        u64 selectedTitleId = 0;
        std::string selectedTitleName;
        std::string selectedTitleFolder;

        // Tap targets captured during draw(), hit-tested on the next update().
        std::vector<HitRect> titleRects;
        std::vector<HitRect> userRects;
        /// A tap recorded last frame, fired at the top of the next one. The tap records the button it stands
        /// for and returns; the next frame draws the selection and the frame after acts on it, so the tile
        /// the user picked is on screen before the save opens. Same defer, and the same reasoning, as
        /// TrainerViewScreen::armTap.
        u64 pendingTapButton = 0;
        /// Frames left before the pending press fires.
        int pendingTapFrames = 0;
        /// How long a tapped tile is drawn SELECTED before it opens, matching
        /// TrainerViewScreen::TAP_PRESS_FRAMES -- a tap must feel the same on every screen.
        static constexpr int TAP_PRESS_FRAMES = 4;
        /// Records a tap to fire once its selection has been seen. Returns true so a handler can
        /// read `if (armTap(...)) return;`.
        bool armTap(u64 button);

        Dialogs::FileBrowserState fileBrowser;
        std::vector<TouchButton> browserTaps;
        bool fileSelected = false;
        std::string selectedFilePath;
        std::unique_ptr<Trainer::Trainer> selectedFileTrainer;
        std::string selectedFileLabel;
        void openFileBrowser();
        void handleFileBrowserInput(u64 buttonsDown, const TouchInput &touch);

        void loadUsers();
        // Titles come from enumerating SAVE DATA, not installed applications: a game played from a
        // cartridge that is currently out, or one that has been uninstalled, keeps its save on
        // internal storage and must still be editable.
        void loadTitlesForUser(UserEntry &user);
        static bool scanSaveSpace(UserEntry &user, int spaceId, int &scanned, int &forUser);
        void setUser(int index);
        void selectCurrentTitle();

        const UserEntry *currentUser() const;
        int titleCount() const;   // titles the current user has (0 when there is no user)
        int titleColumns() const; // grid columns for the current user's title count (<= 5)
        int titleRows() const;    // rows those tiles occupy

        // Top row of the scroll window. Persistent STATE, not derived from the selection: it moves
        // only when the selected tile would otherwise fall outside the window, so the grid holds
        // still while the cursor moves within it instead of re-centring (which reads as paging).
        int scrollRow = 0;
        void scrollSelectionIntoView();
    };
}

#endif
