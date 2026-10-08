#ifndef UI_UI_H
#define UI_UI_H

#include <switch.h>

#include "UI/Common.h"
#include "UI/PKSEFramebuffer.h"
#include "UI/UIScreen.h"
#include "UI/TouchInput.h"
#include "UI/SaveSelectScreen.h"
#include "UI/BackupSelectionScreen.h"
#include "UI/TrainerViewScreen.h"
#include "Utils/Keyboard.h"

namespace Trainer
{
    class Trainer;
}

namespace Pokemon
{
    struct Pokemon8SWSH;
}

namespace UI
{

    class UIManager
    {
    public:
        UIManager();
        ~UIManager();

        void run();

    private:
        PKSEFramebuffer framebuffer;
        PadState pad;
        TouchInput touch;
        bool running;
        /// The screen whose update() is running, drawn under the on-screen keyboard. A prompt is only
        /// ever raised from inside a screen's update(), so every loop sets this just before calling one.
        UIScreen *keyboardBackdrop = nullptr;
        /// Every save this console holds, captured while the picker had them enumerated. The
        /// editor's trade-partner picker needs the OTHER saves, and walking save data is fs/ns work
        /// the picker screen has already done -- so it is carried here rather than repeated.
        std::vector<TrainerViewScreen::ConsoleSaveEntry> consoleSaves;

        /// The registered Utils::KeyboardPresenter: runs PKSE's keyboard over keyboardBackdrop until
        /// the prompt is answered, pumping frames exactly as the screen loops do.
        Utils::KeyboardResult runKeyboard(const Utils::KeyboardRequest &request);
        void drawKeyboardBackdrop();

        void handleSaveSelection();
        /// `titleName` is prose and `titleFolder` is a directory; they are NOT interchangeable.
        /// See Save/TitleFolder.h -- the folder carries the title id and the prose must not.
        void handleBackupSelection(AccountUid userUid, u64 titleId, const std::string &titleName, const std::string &titleFolder);
        // loadedFromCart: true when this session was read from the LIVE game save (the "load from
        // title" path), false when an older backup was opened. Writing back to the game is the
        // normal, expected thing to do in the first case and a rollback in the second, so the two
        // are gated very differently at save time.
        void handleTrainerView(AccountUid userUid, u64 titleId, const std::string &titleName, const std::string &backupDir, bool loadedFromCart);
        /// Edits a loose save file picked from the SD card rather than an installed title's save.
        /// There is no title id, no account and no backup directory to work from -- the path IS
        /// the save -- so this is a parallel entry point rather than a variation of the one above.
        /// The picker has already opened it; see SaveSelectScreen::takeSelectedFileTrainer.
        void handleExternalSave(std::unique_ptr<Trainer::Trainer> trainer, const std::string &label,
                                const std::string &path);
    };
}

#endif
