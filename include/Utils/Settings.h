#ifndef UTILS_SETTINGS_H
#define UTILS_SETTINGS_H

#include <string>
#include <vector>

namespace Utils {
    // Persisted app settings live in sdmc:/PKSE/settings.cfg (simple key=value text).
    // Currently: theme (dark/light), auto-backup (on/off), and the file browser's folders.

    // Load settings into the runtime globals (UI::g_themeMode via applyTheme,
    // ::g_autoBackupEnabled). Safe when the file is missing — defaults are kept.
    // Call once at startup, before any screen draws.
    void loadSettings();

    // Write the current settings back to settings.cfg. Call after a setting changes.
    void saveSettings();

    /// Points loadSettings/saveSettings at another file; empty restores settings.cfg. For the self
    /// test, so driving the file browser cannot rewrite the user's favorites or last folders.
    void setSettingsFileOverride(const std::string &settingsFilePath);

    /// Which file browser a remembered folder belongs to. The save pickers share one, the import
    /// browser keeps its own: opening a PKSM bank should not start in an emulator's save folder.
    enum class LastFolderKind
    {
        SaveFiles,
        ImportFiles,
    };

    /// The folder this kind of browser last picked a file from; empty when there is none yet.
    const std::string &lastFolder(LastFolderKind kind);
    /// Remembers `directory` and saves the settings when it changed.
    void setLastFolder(LastFolderKind kind, const std::string &directory);

    /// Folders the user marked in the file browser, in the order they were added. Shared by every
    /// browser: a folder is worth jumping to whatever is being opened from it.
    const std::vector<std::string> &favoriteFolders();
    bool isFavoriteFolder(const std::string &directory);
    /// Adds `directory`, or removes it when it is already a favorite, and saves the settings.
    /// Returns true when it is a favorite afterwards.
    bool toggleFavoriteFolder(const std::string &directory);
}

#endif
