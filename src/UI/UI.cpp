
#include "Globals.h"
#include "Save/GetSaveFileContents.h"
#include "UI/UI.h"
#include "UI/SaveSelectScreen.h"
#include "UI/BackupSelectionScreen.h"
#include "UI/TrainerViewScreen.h"
#include "UI/Dialogs/KeyboardDialog.h"
#include "Utils/HelperUtilities.h"
#include "Utils/Logger.h"
#include "Utils/FileUtilities.h"
#include "Trainer/Trainer.h"
#include "Trainer/Trainer1RBY.h"

using namespace Utils;
using namespace Trainer;

namespace UI
{
    UIManager::UIManager() : running(true)
    {
        padConfigureInput(1, HidNpadStyleSet_NpadStandard);
        padInitializeDefault(&pad);
        hidInitializeTouchScreen(); // enable the touchscreen alongside the gamepad
        Utils::setKeyboardPresenter([this](const Utils::KeyboardRequest &request) { return runKeyboard(request); });
    }

    UIManager::~UIManager()
    {
        Utils::setKeyboardPresenter(nullptr);
    }

    void UIManager::drawKeyboardBackdrop()
    {
        if (keyboardBackdrop)
            keyboardBackdrop->draw(framebuffer);
        else
            framebuffer.clear(Colors::Background);
    }

    // The keyboard is modal, so it runs a loop of its own inside the update() that asked for it --
    // but it keeps DRAWING that screen underneath, which is the point of it: a search filters its list
    // as the query is typed. `pad` and `touch` are the same objects the screen loops read, so the edge
    // state carries straight across in both directions -- the press that raised the prompt is not
    // typed into it, and the press that answers it does not reach the screen.
    Utils::KeyboardResult UIManager::runKeyboard(const Utils::KeyboardRequest &request)
    {
        Dialogs::KeyboardState keyboard;
        keyboard.open(request, framebuffer.getWidth(), framebuffer.getHeight());
        Dialogs::setActiveKeyboard(&keyboard);
        while (!keyboard.finished && appletMainLoop())
        {
            padUpdate(&pad);
            touch.update();
            keyboard.update(pad, touch);
            if (keyboard.finished)
                break;
            drawKeyboardBackdrop();
            Dialogs::drawKeyboard(keyboard, framebuffer);
            framebuffer.flush();
        }
        Dialogs::setActiveKeyboard(nullptr);

        // Hold the screen until whatever answered the prompt is let go. The screen reads the same pad
        // the moment this returns, and a + still held from OK would otherwise reach it as a press --
        // which closes the app. Stick directions are not waited on: a drifting stick never lets go.
        constexpr u64 answeringButtons = HidNpadButton_A | HidNpadButton_B | HidNpadButton_X | HidNpadButton_Y |
                                         HidNpadButton_L | HidNpadButton_R | HidNpadButton_ZL | HidNpadButton_ZR |
                                         HidNpadButton_Plus | HidNpadButton_Minus | HidNpadButton_Up |
                                         HidNpadButton_Down | HidNpadButton_Left | HidNpadButton_Right;
        constexpr int releaseFrameLimit = 60;
        for (int releaseFrame = 0; releaseFrame < releaseFrameLimit && appletMainLoop(); ++releaseFrame)
        {
            drawKeyboardBackdrop();
            framebuffer.flush();
            padUpdate(&pad);
            touch.update();
            const bool buttonStillMoving = ((padGetButtons(&pad) | padGetButtonsUp(&pad)) & answeringButtons) != 0;
            if (!buttonStillMoving && !touch.isDown() && !touch.justReleased())
                break;
        }
        return keyboard.result();
    }

    void UIManager::run()
    {
        while (appletMainLoop() && running)
        {
            handleSaveSelection();
        }
    }

    // Combined JKSV-style user + title picker: pick a user's avatar and one of their supported
    // Pokemon game icons in a single screen, then go straight to backup selection.
    void UIManager::handleSaveSelection()
    {
        SaveSelectScreen selectScreen;
        framebuffer.startFade();

        while (appletMainLoop() && running && !selectScreen.shouldExit())
        {
            padUpdate(&pad);
            touch.update();
            keyboardBackdrop = &selectScreen;
            selectScreen.update(pad, touch);
            // Captured every frame while the picker is up: it re-enumerates on a user switch, and
            // this is the only place the whole console's save list exists.
            consoleSaves.clear();
            for (const auto &user : selectScreen.allUsers())
            {
                for (const auto &title : user.titles)
                {
                    TrainerViewScreen::ConsoleSaveEntry entry;
                    entry.titleId = title.titleId;
                    entry.accountUid = user.accountUid;
                    entry.label = title.label;
                    entry.userName = user.name;
                    entry.folder = title.folder;
                    entry.gameVersion = title.gameVersion;
                    consoleSaves.push_back(std::move(entry));
                }
            }
            selectScreen.draw(framebuffer);
            framebuffer.drawFadeOverlay();
            framebuffer.flush();

            if (selectScreen.hasSelectedTitle())
            {
                handleBackupSelection(selectScreen.getSelectedUser(),
                                      selectScreen.getSelectedTitleId(),
                                      selectScreen.getSelectedTitleName(),
                                      selectScreen.getSelectedTitleFolder());
                // Back from backup/trainer -> return so run() rebuilds the picker (re-lists saves).
                return;
            }
            if (selectScreen.hasSelectedFile())
            {
                // A loose save skips backup selection entirely: there is no backup tree for a file
                // the user pointed at, and no "live save vs older backup" choice to make.
                handleExternalSave(selectScreen.takeSelectedFileTrainer(), selectScreen.getSelectedFileLabel(),
                                   selectScreen.getSelectedFilePath());
                return;
            }
        }

        running = false; // + pressed -> exit the app
    }

    void UIManager::handleBackupSelection(AccountUid userUid, u64 titleId, const std::string &titleName, const std::string &titleFolder)
    {
        BackupSelectionScreen backupScreen(titleId, titleName, titleFolder);
        framebuffer.startFade();

        while (appletMainLoop() && running && !backupScreen.shouldExit())
        {
            padUpdate(&pad);
            touch.update();
            keyboardBackdrop = &backupScreen;
            backupScreen.update(pad, touch);
            backupScreen.draw(framebuffer);
            framebuffer.drawFadeOverlay();
            framebuffer.flush();

            if (backupScreen.hasSelectedBackup())
            {
                if (backupScreen.shouldCreateNewBackup())
                {
                    logInfoToFile("Creating new backup for", titleName.c_str());

                    // Auto-backup ON -> new timestamped history folder (kept indefinitely; the user
                    // decides when to delete backups, so we never prune). OFF -> reuse a single
                    // "Working" copy so backups don't pile up.
                    std::string backupPath = backupSaveData(userUid, titleId, titleFolder, g_autoBackupEnabled);
                    if (backupPath.empty())
                    {
                        logErrorToFile("Failed to back up save data");
                        // Tell the user and stay put. Returning here (the old behaviour) dropped them
                        // back at the save picker with no message, which is exactly what pressing B
                        // does -- so a failed backup was indistinguishable from a cancel.
                        backupScreen.reportFailure("Couldn't create the backup. Check SD card space and try again.");
                        continue;
                    }
                    handleTrainerView(userUid, titleId, titleName, backupPath, true); // read from the live save
                }
                else
                {
                    logInfoToFile("Loading existing backup", backupScreen.getSelectedBackupPath().c_str());
                    handleTrainerView(userUid, titleId, titleName, backupScreen.getSelectedBackupPath(), false);
                }
                return;
            }
        }
    }

    void UIManager::handleTrainerView(AccountUid userUid, u64 titleId, const std::string &titleName, const std::string &backupDir, bool loadedFromCart)
    {
        logInfoToFile("Loading save from", backupDir.c_str());

        // Read trainer data from the specified backup directory
        // Auto-detects game version and uses appropriate reading function
        TrainerVariant trainerVariant = readTrainerInfo(backupDir.c_str(), titleId);

        // Use std::visit to extract reference and create TrainerViewScreen
        std::visit([&](auto &trainer)
        {
            TrainerViewScreen trainerScreen(trainer, titleName, backupDir, titleId, userUid, loadedFromCart);
            // The trade-partner picker offers the OTHER saves on this console -- captured while
            // the picker screen had them enumerated, because walking save data is fs/ns work the
            // editor has no business repeating.
            trainerScreen.setConsoleSaves(consoleSaves);
            framebuffer.startFade();

            while (appletMainLoop() && !trainerScreen.shouldExit() && !trainerScreen.hasRequestedExit()) {
                padUpdate(&pad);
                touch.update();
                keyboardBackdrop = &trainerScreen;
                trainerScreen.update(pad, touch);
                trainerScreen.draw(framebuffer);
                framebuffer.drawFadeOverlay();
                framebuffer.flush();
            }

            // If user pressed + to exit app, stop running
            if (trainerScreen.hasRequestedExit()) {
                running = false;
            }
        }, trainerVariant);
    }

    // A save the user pointed at on the SD card, rather than one belonging to an installed title.
    // Everything the normal path derives from the title id -- which game, where the backups live,
    // which account owns it -- is either absent or comes from the file itself here.
    void UIManager::handleExternalSave(std::unique_ptr<Trainer::Trainer> trainer, const std::string &label,
                                       const std::string &path)
    {
        // The trainer is owned through the base pointer because the concrete type is not known
        // until the probe chain runs -- thirteen formats across seven generations arrive this way.
        if (!trainer)
        {
            logErrorToFile("No opened save came with the picked file", path.c_str());
            return;
        }
        logInfoToFile("Editing external save", path.c_str());

        // There is no title id, so 0 is passed and the save path travels as the "backup dir" so
        // the write-back knows which file it came from.
        TrainerViewScreen trainerScreen(*trainer, label, path, 0, AccountUid{}, false);
        framebuffer.startFade();

        while (appletMainLoop() && !trainerScreen.shouldExit() && !trainerScreen.hasRequestedExit())
        {
            padUpdate(&pad);
            touch.update();
            keyboardBackdrop = &trainerScreen;
            trainerScreen.update(pad, touch);
            trainerScreen.draw(framebuffer);
            framebuffer.drawFadeOverlay();
            framebuffer.flush();
        }
        if (trainerScreen.hasRequestedExit())
            running = false;
    }
}
