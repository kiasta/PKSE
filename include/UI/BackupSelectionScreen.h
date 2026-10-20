#ifndef UI_BACKUP_SELECTION_SCREEN_H
#define UI_BACKUP_SELECTION_SCREEN_H

#include <vector>
#include <string>

#include <switch.h>

#include "UI/UIScreen.h"
#include "UI/NavigationRepeat.h"
#include "UI/PKSEFramebuffer.h"

namespace UI
{
    class BackupSelectionScreen : public UIScreen
    {
    public:
        /// `titleName` is what the title bar reads; `titleFolder` is the directory under
        /// sdmc:/PKSE. One is prose and one carries the title id -- see Save/TitleFolder.h.
        BackupSelectionScreen(u64 titleId, const std::string &titleName, const std::string &titleFolder);
        void update(const PadState &pad, const TouchInput &touch) override;
        void draw(PKSEFramebuffer &framebuffer) override;
        bool shouldExit() const override { return goBack; }

        bool hasSelectedBackup() const { return backupSelected; }
        bool shouldCreateNewBackup() const { return createNewBackup; }
        const std::string &getSelectedBackupPath() const { return selectedBackupPath; }

        /**
         * Report that acting on the selection failed, so the screen can say so and take the choice
         * back. Without this the caller's only option is to bail out of the whole flow, which
         * drops the user back at the save picker with no indication that anything went wrong — the
         * failure is indistinguishable from having pressed B.
         */
        void reportFailure(const std::string &message)
        {
            statusMessage = message;
            statusFrames = 480;
            backupSelected = false; // hand the choice back rather than re-firing it every frame
            createNewBackup = false;
        }

    private:
        ControllerNavigation controllerNavigation;

        struct BackupInfo
        {
            std::string timestamp;
            std::string displayName;
        };

        u64 titleId;
        std::string titleName;
        std::string gameDirectory;
        std::vector<BackupInfo> backups;
        /// Indexes `backups` directly. This screen has no search: it picks WHICH SAVE to open,
        /// like the title picker before it, and the two of them are deliberately outside the set
        /// of lists that get a search box (see UI/ListSearch.h). Row 0 is "Create New Backup".
        int selectedIndex;

        /// Top of the list, under the heading divider.
        int backupListTop() const;
        bool backupSelected;
        bool createNewBackup; // True if user wants to load from title directly
        bool goBack;
        std::string selectedBackupPath;
        bool showDeleteConfirmation;
        int deleteConfirmationIndex;
        // Delete-confirm button rects, captured during draw and hit-tested next frame (this screen
        // has no touchButtons framework; same one-frame-late contract as TrainerViewScreen).
        struct DlgBtn
        {
            int hitX = 0, hitY = 0, hitWidth = 0, hitHeight = 0;
        };
        DlgBtn deleteCancelBtn, deleteDeleteBtn;
        /// A tap recorded last frame, fired at the top of the next one. Acting in the tap's own
        /// frame meant the backup or the button never got drawn selected before the action ran --
        /// a delete simply happened, with nothing on screen having said which row it was for.
        /// Same defer, and the same reasoning, as TrainerViewScreen::armTap.
        u64 pendingTapButton = 0;
        /// 0 = Cancel, 1 = Delete while a confirm tap is waiting; -1 when nothing is. Matches the
        /// id convention the storage dialogs use, so the two screens read alike.
        int pendingTapButtonId = -1;
        /// Frames left before the pending press fires.
        int pendingTapFrames = 0;
        /// How long a tapped button is drawn HELD before its action runs, matching
        /// TrainerViewScreen::TAP_PRESS_FRAMES -- a tap must feel the same on every screen.
        static constexpr int TAP_PRESS_FRAMES = 4;
        /// Records a tap to fire once its press has been seen. Returns true so a handler can read
        /// `if (armTap(...)) return;`.
        bool armTap(int buttonId, u64 button);
        std::string statusMessage; // transient failure notice, shown just above the nav bar
        int statusFrames = 0;

        void loadBackups();
        void drawBackupList(PKSEFramebuffer &framebuffer);
        void drawDeleteConfirmation(PKSEFramebuffer &framebuffer);
        std::string formatTimestamp(const std::string &timestamp);
        void deleteBackup(int index);
    };
}

#endif
