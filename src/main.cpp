#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

#include <switch.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <bits/stdc++.h>

#include "Globals.h"
#include "Save/GetSaveFileContents.h"
#include "UI/SpriteManager.h"
#include "UI/SystemIcons.h"
#include "UI/UI.h"
#include "Utils/Logger.h"
#include "Utils/HelperUtilities.h"
#include "Utils/FileUtilities.h"
#include "Utils/Settings.h"

int main()
{
    // Settings FIRST, before the first log line. "Enable Debug Logging" gates every sink, so
    // loading it up here is what makes the toggle cover startup as well -- read any later and the
    // dozen lines below would follow the compiled-in default instead of the user's choice, which
    // means either losing exactly the init diagnostics a bug report needs or writing a file the
    // user switched off. Safe this early: loadSettings only fopen()s sdmc:/PKSE/settings.cfg
    // (libnx mounts sdmc before main), applyTheme just swaps colour globals, and nothing in it
    // logs. It needs no ROMFS and no SDL, neither of which exists yet; the one service it can
    // reach -- settings, for the system-language default -- Names::systemLanguageIndex opens and
    // closes itself.
    Utils::loadSettings();

    logInfoToFile("Initializing PKSE...");

    Utils::cleanupOldLogs();

    Result nsServiceInitializeResult = nsInitialize();
    if (R_FAILED(nsServiceInitializeResult))
    {
        logErrorToFile("Failed to initialize ns service");
        return -1;
    }

    Result accountServiceInitializeResult = accountInitialize(AccountServiceType_Application);
    if (R_FAILED(accountServiceInitializeResult))
    {
        logErrorToFile("Failed to initialize account service");
        nsExit();
        return -1;
    }

    Utils::logInfoToFile("Initializing ROMFS...");
    Result romfsInitResult = romfsInit();
    bool romfsInitialized = false;
    if (R_FAILED(romfsInitResult))
    {
        logErrorToFile("Failed to initialize ROMFS - sprites will not be available");
        // Don't exit, app can still run without sprites
    }
    else
    {
        Utils::logInfoToFile("ROMFS initialized successfully");
        romfsInitialized = true;
    }

    // The console's own shared fonts, for Japanese, Korean and Chinese text.
    //
    // PKSE bundles Nunito plus two Noto Symbols faces, and between them they cover Latin-1 and the
    // gender/star glyphs completely -- French, German, Italian and Spanish names have always
    // rendered. What none of the three carries is a single CJK glyph, so a Japanese Pokemon's
    // nickname decoded correctly and then drew as nothing at all.
    //
    // These are preferred over bundling Noto CJK because they are THE SAME FACES the console and
    // the games themselves render Pokemon names with, so a Japanese nickname looks in PKSE exactly
    // as it does in the game it came from -- which a separately-sourced font would not guarantee.
    // They also cannot drift: there is no fetch step to go stale and no version skew between the
    // glyphs PKSE draws and the ones the system draws. (This is NOT a size decision. PKSE is run
    // under title override for the memory headroom, so the .nro's size is not a constraint; if
    // self-containment were ever wanted, bundling Noto CJK alongside these is a free addition.)
    //
    // Failure is non-fatal and deliberately so: without it every Latin script still renders exactly
    // as before, which is a far better outcome than refusing to start.
    Result sharedFontInitResult = plInitialize(PlServiceType_User);
    bool sharedFontsInitialized = R_SUCCEEDED(sharedFontInitResult);
    if (!sharedFontsInitialized)
        logErrorToFile("plInitialize failed - CJK text will render as blanks");

    // Initialize SDL for the window, GL context and input only -- rendering is NanoVG on GL.
    // SDL_image and SDL_ttf are deliberately NOT initialised: PNGs are decoded by stb_image in
    // SpriteManager and text is drawn by NanoVG's own font atlas, so neither library is used.
    Utils::logInfoToFile("Initializing SDL...");
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        logErrorToFile("SDL_Init(VIDEO) failed");
        logErrorToFile(SDL_GetError());
    }

    Utils::logInfoToFile("Initializing Sprite Manager...");
    UI::SpriteManager::init();

    Utils::logInfoToFile("Testing sprite loading...");
    UI::Sprite *testSprite = UI::SpriteManager::getSprite(25, false); // Pikachu
    if (testSprite && testSprite->data)
    {
        logInfoToFile(("SUCCESS: Test sprite loaded! (" + std::to_string(testSprite->width) + "x" + std::to_string(testSprite->height) + ")").c_str());
    }
    else
    {
        Utils::logInfoToFile("WARNING: Test sprite failed to load - sprites may not be available");
    }

    Utils::logInfoToFile("Starting UI Manager...");

    {
        UI::UIManager uiManager;
        uiManager.run();
    } // UIManager (and its SDL-backed framebuffer) destroyed here, before SDL_Quit

    // Cleanup
    Utils::logInfoToFile("Cleaning up Sprite Manager...");
    UI::SpriteManager::cleanup();
    UI::SystemIcons::cleanup();

    SDL_Quit();

    if (romfsInitialized)
    {
        Utils::logInfoToFile("Cleaning up ROMFS...");
        romfsExit();
    }

    // After SDL_Quit and the NanoVG context it owned: the shared-font buffers live in memory the
    // pl service maps, and fontstash holds pointers into them for as long as the context exists.
    if (sharedFontsInitialized)
        plExit();

    accountExit();
    nsExit();

    return 0;
}
