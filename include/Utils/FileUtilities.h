#ifndef UTILS_FILE_UTILITIES_H
#define UTILS_FILE_UTILITIES_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "Utils/NXTypes.h"   // AccountUid/u64; <switch.h> on console. The IMPLEMENTATIONS
                             // here are Switch-only -- this just lets the header parse off-console
                             // so the save layer, which includes it transitively, can be compiled.

namespace Utils {
    /// The whole file, in a buffer the caller releases with delete[]; null (logged) when it cannot be read.
    uint8_t* readAllBytes(const char* path, size_t* outSize);
    bool copyDirectory(const char* srcPath, const char* destPath);
    bool copyFile(const char* srcPath, const char* destPath);
    bool deleteDirectoryRecursive(const char* path);
    // Copies a title's live save data into `destDir` (which must already exist), mounting and
    // unmounting the save device around the copy. This is the half of backupSaveData that actually
    // moves bytes, split out so a caller can take the same copy into a scratch directory of its
    // own -- a working copy that landed in the user's backup list instead would make the backup
    // screen unusable inside a week.
    bool copySaveDataTo(AccountUid userUid, u64 titleId, const char* destDir);

    // Copies the current game save into PKSE/{titleName}/. When `timestamped` is true a new
    // timestamped history folder is created; when false a single reusable "Working" folder is
    // overwritten (auto-backup disabled — no pile-up). Returns the created folder path, or "" on failure.
    /// `titleFolder` is the DIRECTORY name (Save::titleFolderName), not the prose title -- it
    /// carries the title id, because two titles sharing a backup folder is data loss.
    std::string backupSaveData(AccountUid userUid, u64 titleId, std::string titleFolder, bool timestamped = true);
    // Copy a backup's save files onto the real game save. The backup directory IS the edited
    // save -- PKSE writes edits straight into it -- so there is no separate "modified" copy.
    // `primaryFile` is the one file a backup must hold; the game's `optionalFiles` beside it are
    // copied when present and passed over in silence when not, because whether they exist at all
    // depends on what the player has done in-game.
    bool restoreBackupToTitle(AccountUid userUid, u64 titleId, const char* backupDir,
                              const std::string& primaryFile,
                              const std::vector<std::string>& optionalFiles = {});
    std::string getTimestamp();
    std::vector<std::string> listBackupDirectories(const char* gameDirectory);
}

#endif
