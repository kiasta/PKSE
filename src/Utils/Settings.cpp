#include "Utils/Settings.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <sys/stat.h>

#include "Globals.h"
#include "Names/NameLanguage.h"
#include "UI/Common.h"
#include "Utils/Logger.h"

namespace Utils
{
    namespace
    {
        std::string g_settingsFileOverride;
        std::string g_lastSaveFolder;
        std::string g_lastImportFolder;
        std::vector<std::string> g_favoriteFolders;

        std::string &lastFolderFor(LastFolderKind kind)
        {
            return kind == LastFolderKind::ImportFiles ? g_lastImportFolder : g_lastSaveFolder;
        }
    }

    static std::string settingsPath()
    {
        if (!g_settingsFileOverride.empty())
            return g_settingsFileOverride;
        return BASE_SAVE_DIRECTORY + "/settings.cfg";
    }

    void setSettingsFileOverride(const std::string &settingsFilePath)
    {
        g_settingsFileOverride = settingsFilePath;
    }

    const std::string &lastFolder(LastFolderKind kind)
    {
        return lastFolderFor(kind);
    }

    void setLastFolder(LastFolderKind kind, const std::string &directory)
    {
        std::string &rememberedFolder = lastFolderFor(kind);
        if (rememberedFolder == directory)
            return;
        rememberedFolder = directory;
        saveSettings();
    }

    const std::vector<std::string> &favoriteFolders()
    {
        return g_favoriteFolders;
    }

    bool isFavoriteFolder(const std::string &directory)
    {
        for (const std::string &favoriteFolder : g_favoriteFolders)
        {
            if (favoriteFolder == directory)
                return true;
        }
        return false;
    }

    bool toggleFavoriteFolder(const std::string &directory)
    {
        for (auto favoriteFolder = g_favoriteFolders.begin(); favoriteFolder != g_favoriteFolders.end(); ++favoriteFolder)
        {
            if (*favoriteFolder == directory)
            {
                g_favoriteFolders.erase(favoriteFolder);
                saveSettings();
                return false;
            }
        }
        g_favoriteFolders.push_back(directory);
        saveSettings();
        return true;
    }

    void loadSettings()
    {
        // The language default is the CONSOLE's, not English: a French player should get French
        // names without configuring anything, which is the whole point of reading it here. An
        // explicit `language=` line below then overrides it, so a choice once made is kept even if
        // it happens to equal the system language. A console set to a language the Pokemon games
        // have never shipped in (Dutch, Portuguese, Russian) falls back to English, as they do.
        Names::setDisplayLanguage(Names::systemLanguageIndex());

        // No config yet -> keep the compiled-in defaults, but still fall through to the language
        // pin below. Returning early here skipped it, which is the FIRST run on every console --
        // exactly the case the pin exists for.
        // Cleared first so a second load (the self test re-reads its own file) replaces the folders
        // rather than appending to them.
        g_lastSaveFolder.clear();
        g_lastImportFolder.clear();
        g_favoriteFolders.clear();

        FILE *f = fopen(settingsPath().c_str(), "r");
        // Sized for a folder path, not just the short toggles: fgets splits a longer line, and the
        // tail of a split path would be read as a line of its own.
        char line[1024];
        while (f && fgets(line, sizeof(line), f))
        {
            // Trim the trailing newline / carriage return.
            char *nl = strpbrk(line, "\r\n");
            if (nl)
                *nl = '\0';

            char *eq = strchr(line, '=');
            if (!eq)
                continue;
            *eq = '\0';
            const char *key = line;
            const char *value = eq + 1;

            if (strcmp(key, "theme") == 0)
            {
                UI::applyTheme(strcmp(value, "light") == 0 ? UI::ThemeMode::Light : UI::ThemeMode::Dark);
            }
            else if (strcmp(key, "autoBackup") == 0)
            {
                g_autoBackupEnabled = (strcmp(value, "0") != 0);
            }
            else if (strcmp(key, "allowIllegal") == 0)
            {
                g_allowIllegalEdits = (strcmp(value, "0") != 0);
            }
            else if (strcmp(key, "autoLegalize") == 0)
            {
                g_autoLegalizeTransfers = (strcmp(value, "0") != 0);
            }
            else if (strcmp(key, "moveWarn") == 0 || strcmp(key, "lgpeMoveWarn") == 0)
            {
                // "lgpeMoveWarn" is the old key for the same toggle, still read so an existing
                // settings.cfg doesn't silently revert the user's choice to the default. Only the
                // new key is written back, so it ages out on the first save.
                g_moveWarn = (strcmp(value, "0") != 0);
            }
            else if (strcmp(key, "injectToGame") == 0)
            {
                g_injectToGameSave = (strcmp(value, "0") != 0);
            }
            else if (strcmp(key, "debugLogging") == 0)
            {
                g_debugLogging = (strcmp(value, "0") != 0);
            }
            else if (strcmp(key, "lastSaveFolder") == 0)
            {
                g_lastSaveFolder = value;
            }
            else if (strcmp(key, "lastImportFolder") == 0)
            {
                g_lastImportFolder = value;
            }
            else if (strcmp(key, "favoriteFolder") == 0)
            {
                if (*value != '\0' && !isFavoriteFolder(value))
                    g_favoriteFolders.push_back(value);
            }
            else if (strcmp(key, "language") == 0)
            {
                // Stored as the PKHeX resource suffix ("fr", "zh-Hant") rather than an index, so
                // the file stays readable and a future reordering of the table cannot silently
                // repoint an existing config at a different language.
                for (size_t languageIndex = 0; languageIndex < Names::LANGUAGE_COUNT; ++languageIndex)
                {
                    if (strcmp(value, Names::nameLanguageSuffix(languageIndex)) == 0)
                    {
                        Names::setDisplayLanguage(languageIndex);
                        break;
                    }
                }
            }
        }
        if (f)
            fclose(f);

        // Pin English while the language is not user-selectable. Everything above still runs --
        // the console is still asked, an existing `language=` line is still honoured -- so
        // re-enabling the feature is deleting this block and nothing else. See NameLanguage.h for
        // why a manual override was the wrong control and what replaces it.
        if (!Names::DISPLAY_LANGUAGE_SELECTABLE)
            Names::setDisplayLanguage(Names::LANGUAGE_INDEX_ENGLISH);
    }

    void saveSettings()
    {
        // Make sure the base dir exists (it normally does once a backup has been taken).
        mkdir(BASE_SAVE_DIRECTORY.c_str(), 0777);

        FILE *f = fopen(settingsPath().c_str(), "w");
        if (!f)
        {
            logErrorToFile("Failed to write settings file", settingsPath().c_str());
            return;
        }
        fprintf(f, "theme=%s\n", (UI::g_themeMode == UI::ThemeMode::Light) ? "light" : "dark");
        fprintf(f, "autoBackup=%d\n", g_autoBackupEnabled ? 1 : 0);
        fprintf(f, "allowIllegal=%d\n", g_allowIllegalEdits ? 1 : 0);
        fprintf(f, "autoLegalize=%d\n", g_autoLegalizeTransfers ? 1 : 0);
        fprintf(f, "moveWarn=%d\n", g_moveWarn ? 1 : 0);
        fprintf(f, "injectToGame=%d\n", g_injectToGameSave ? 1 : 0);
        fprintf(f, "debugLogging=%d\n", g_debugLogging ? 1 : 0);
        fprintf(f, "language=%s\n", Names::nameLanguageSuffix(Names::displayLanguageIndex()));
        if (!g_lastSaveFolder.empty())
            fprintf(f, "lastSaveFolder=%s\n", g_lastSaveFolder.c_str());
        if (!g_lastImportFolder.empty())
            fprintf(f, "lastImportFolder=%s\n", g_lastImportFolder.c_str());
        for (const std::string &favoriteFolder : g_favoriteFolders)
            fprintf(f, "favoriteFolder=%s\n", favoriteFolder.c_str());
        fclose(f);
    }
}
