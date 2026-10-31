#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <new>
#include <vector>
#include <string>
#include <algorithm>

#include <switch.h>
#include <sys/dirent.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include "Globals.h"
#include "Utils/Logger.h"

namespace Utils
{
    uint8_t *readAllBytes(const char *path, size_t *outSize)
    {
        // Allocated with new[], so every caller releases it with delete[]. It once came from
        // malloc() while most callers used delete[], which is undefined behaviour that only this
        // toolchain's allocator happened to forgive. std::nothrow keeps the null check meaningful
        // with exceptions disabled. Failures are logged, not printed: perror() reaches nowhere on a
        // console, which left a failed read with no trace even with debug logging on.
        FILE *file = fopen(path, "rb");
        if (!file)
        {
            logInfoToFile("readAllBytes: could not open", path); // often just "not there yet"
            return NULL;
        }

        fseek(file, 0, SEEK_END);
        const long length = ftell(file);
        rewind(file);
        if (length < 0)
        {
            logErrorToFile("readAllBytes: could not size", path);
            fclose(file);
            return NULL;
        }
        const size_t fileSize = static_cast<size_t>(length);

        uint8_t *buffer = new (std::nothrow) uint8_t[fileSize > 0 ? fileSize : 1];
        if (!buffer)
        {
            logErrorToFile("readAllBytes: out of memory for", path);
            fclose(file);
            return NULL;
        }

        size_t bytesRead = fread(buffer, 1, fileSize, file);
        if (bytesRead != fileSize)
        {
            logErrorToFile("readAllBytes: short read of", path);
            delete[] buffer;
            fclose(file);
            return NULL;
        }

        fclose(file);

        if (outSize)
            *outSize = fileSize;
        return buffer;
    }

    bool copyDirectoryRecursive(const char *srcPath, const char *destPath)
    {
        DIR *dir = opendir(srcPath);
        if (!dir)
        {
            logErrorToFile("Failed to open source directory", srcPath);
            logErrorToFile("opendir error", strerror(errno));
            return false;
        }

        bool overallSuccess = true;
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL)
        {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;

            char srcFilePath[512];
            char destFilePath[512];
            snprintf(srcFilePath, sizeof(srcFilePath), "%s/%s", srcPath, entry->d_name);
            snprintf(destFilePath, sizeof(destFilePath), "%s/%s", destPath, entry->d_name);

            if (entry->d_type == DT_DIR)
            {
                // Recursively create and copy subdirectory
                if (mkdir(destFilePath, 0777) != 0 && errno != EEXIST)
                {
                    logErrorToFile("Failed to create subdirectory", destFilePath);
                    overallSuccess = false;
                }
                else
                {
                    if (!copyDirectoryRecursive(srcFilePath, destFilePath))
                    {
                        overallSuccess = false;
                    }
                }
            }
            else if (entry->d_type == DT_REG)
            {
                size_t size = 0;
                unsigned char *data = readAllBytes(srcFilePath, &size);
                if (!data)
                {
                    logErrorToFile("Failed to read file", srcFilePath);
                    overallSuccess = false;
                    continue;
                }

                FILE *out = fopen(destFilePath, "wb");
                if (!out)
                {
                    logErrorToFile("Failed to open for writing", destFilePath);
                    logErrorToFile("fopen error", strerror(errno));
                    delete[] data;
                    overallSuccess = false;
                    continue;
                }

                const bool fileWritten = fwrite(data, 1, size, out) == size;
                // The final flush happens in fclose: on a full SD card or a full save journal it is
                // the call that fails, so a copy is complete only when both succeed.
                const bool fileClosed = fclose(out) == 0;
                if (!fileWritten || !fileClosed)
                {
                    logErrorToFile("Failed to write complete file", destFilePath);
                    overallSuccess = false;
                }
                else
                {
                    logInfoToFile("Successfully copied file", entry->d_name);
                    logInfoToFile("File size (bytes)", std::to_string(size).c_str());
                }
                delete[] data;
            }
        }
        closedir(dir);
        return overallSuccess;
    }

    bool copyDirectory(const char *srcPath, const char *destPath)
    {
        if (mkdir(destPath, 0777) != 0 && errno != EEXIST)
        {
            logErrorToFile("Failed to create destination directory", destPath);
            logErrorToFile("mkdir error", strerror(errno));
            return false;
        }
        logInfoToFile("Created/copied to destination directory", destPath);
        return copyDirectoryRecursive(srcPath, destPath);
    }

    bool copyFile(const char *srcPath, const char *destPath)
    {
        size_t size = 0;
        unsigned char *data = readAllBytes(srcPath, &size);
        if (!data)
        {
            logErrorToFile("Failed to read file", srcPath);
            return false;
        }

        FILE *out = fopen(destPath, "wb");
        if (!out)
        {
            logErrorToFile("Failed to open for writing", destPath);
            logErrorToFile("fopen error", strerror(errno));
            delete[] data;
            return false;
        }

        const bool fileWritten = fwrite(data, 1, size, out) == size;
        const bool fileClosed = fclose(out) == 0; // the final flush can fail here, not in fwrite
        const bool success = fileWritten && fileClosed;
        if (!success)
        {
            logErrorToFile("Failed to write complete file", destPath);
        }
        else
        {
            logInfoToFile("Successfully copied file to", destPath);
        }

        delete[] data;
        return success;
    }

    bool deleteDirectoryRecursive(const char *path)
    {
        DIR *dir = opendir(path);
        if (!dir)
        {
            logErrorToFile("Failed to open directory for deletion", path);
            return false;
        }

        bool success = true;
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL)
        {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            {
                continue;
            }

            char fullPath[1024];
            snprintf(fullPath, sizeof(fullPath), "%s/%s", path, entry->d_name);

            if (entry->d_type == DT_DIR)
            {
                if (!deleteDirectoryRecursive(fullPath))
                {
                    success = false;
                }
            }
            else
            {
                if (remove(fullPath) != 0)
                {
                    logErrorToFile("Failed to delete file", fullPath);
                    success = false;
                }
            }
        }
        closedir(dir);

        if (rmdir(path) != 0)
        {
            logErrorToFile("Failed to remove directory", path);
            return false;
        }

        return success;
    }

    std::string getTimestamp()
    {
        time_t nowSeconds = time(nullptr);
        struct tm *timeinfo = localtime(&nowSeconds);

        char buffer[32];
        strftime(buffer, sizeof(buffer), "%Y%m%d_%H%M%S", timeinfo);
        return std::string(buffer);
    }

    bool copySaveDataTo(AccountUid userUid, u64 titleId, const char *destDir)
    {
        char buffer[LOG_BUFFER_SIZE];

        Result result = fsdevMountSaveData("save", titleId, userUid);
        if (R_FAILED(result))
        {
            snprintf(buffer, sizeof(buffer), "fsdevMountSaveData failed for titleId 0x%016llX: 0x%x", static_cast<unsigned long long>(titleId), result);
            logErrorToFile(buffer);
            return false;
        }

        const bool copied = copyDirectory("save:/", destDir);
        fsdevUnmountDevice("save");
        return copied;
    }

    std::string backupSaveData(AccountUid userUid, u64 titleId, std::string titleFolder, bool timestamped)
    {
        char titleBuf[32];
        snprintf(titleBuf, sizeof(titleBuf), "0x%016llX", static_cast<unsigned long long>(titleId));
        logInfoToFile("Pokemon titleId: ", titleBuf);
        logInfoToFile("Pokemon backup folder: ", titleFolder.c_str());

        // Create base game directory: PKSE/{titleFolder}/
        char gameDirectory[512];
        snprintf(gameDirectory, sizeof(gameDirectory), "%s/%s", BASE_SAVE_DIRECTORY.c_str(), titleFolder.c_str());

        // History backup -> PKSE/{titleFolder}/{timestamp}/ ; auto-backup off -> PKSE/{titleFolder}/Working/
        std::string folderName = timestamped ? getTimestamp() : std::string("Working");
        char backupDirectory[1024];
        snprintf(backupDirectory, sizeof(backupDirectory), "%s/%s", gameDirectory, folderName.c_str());

        logInfoToFile("Backup directory", backupDirectory);
        logInfoToFile("Backing up save for title", titleFolder.c_str());

        if (mkdir(BASE_SAVE_DIRECTORY.c_str(), 0777) != 0 && errno != EEXIST)
        {
            logErrorToFile("Failed to create base directory", BASE_SAVE_DIRECTORY.c_str());
            logErrorToFile("mkdir error", strerror(errno));
            return "";
        }
        if (mkdir(gameDirectory, 0777) != 0 && errno != EEXIST)
        {
            logErrorToFile("Failed to create game directory", gameDirectory);
            logErrorToFile("mkdir error", strerror(errno));
            return "";
        }
        // The reused Working folder is emptied first. A copy only adds files, so anything a
        // previous session left there -- another user's companion file, one the game no longer
        // writes -- would otherwise survive, and restoring this backup would inject it as part of it.
        struct stat workingStatus{};
        if (!timestamped && stat(backupDirectory, &workingStatus) == 0 && !deleteDirectoryRecursive(backupDirectory))
        {
            logErrorToFile("Failed to clear the previous Working backup", backupDirectory);
            return "";
        }
        if (mkdir(backupDirectory, 0777) != 0 && errno != EEXIST)
        {
            logErrorToFile("Failed to create backup directory", backupDirectory);
            logErrorToFile("mkdir error", strerror(errno));
            return "";
        }

        const bool copySuccess = copySaveDataTo(userUid, titleId, backupDirectory);

        if (copySuccess)
        {
            logInfoToFile("Backup completed successfully!");
            return std::string(backupDirectory);
        }
        logErrorToFile("Backup failed during file copying.");
        return "";
    }

    // Tells "the backup never held this file" apart from "the copy failed". Both reach copyFile as
    // the same failed fopen, but only the second one is an error worth reporting.
    static bool backupHasFile(const char *path)
    {
        struct stat entryStatus{};
        return stat(path, &entryStatus) == 0 && S_ISREG(entryStatus.st_mode);
    }

    /**
     * Copy a backup's save files onto the real game save.
     *
     * The backup directory IS the edited save: PKSE writes edits straight into it, so there is one
     * source path rather than two.
     *
     * Only `primaryFile` has to exist. Which companion files sit beside it is a property of the
     * individual save, not of the game: Sword/Shield's `poke_trade` holds a Surprise Trade result
     * that has not been collected yet, so a save that never sent one simply has no such file. A flat
     * per-game array of required names makes that ordinary case fatal -- the copy loop breaks on the
     * missing file and returns before `fsdevCommitDevice`, so the writes already made to save:/ are
     * dropped on unmount and the user's edit disappears with a "failed to copy" error naming a file
     * they were never supposed to have.
     *
     * So an absent optional file is skipped and not logged as a failure. A file that IS there and
     * will not copy still stops the restore, before the commit, leaving the game save untouched.
     */
    bool restoreBackupToTitle(AccountUid userUid, u64 titleId, const char *backupDir, const std::string &primaryFile, const std::vector<std::string> &optionalFiles)
    {
        char buffer[LOG_BUFFER_SIZE];
        logInfoToFile("Restoring backup to game save", backupDir);

        if (primaryFile.empty())
        {
            logErrorToFile("No primary save file specified for restore");
            return false;
        }

        // Checked before mounting: if the one required file is missing there is nothing this can
        // do, and bailing here means never having opened the game's save data at all.
        char primaryPath[512];
        snprintf(primaryPath, sizeof(primaryPath), "%s/%s", backupDir, primaryFile.c_str());
        if (!backupHasFile(primaryPath))
        {
            snprintf(buffer, sizeof(buffer), "Backup has no %s to restore", primaryFile.c_str());
            logErrorToFile(buffer);
            return false;
        }

        Result result = fsdevMountSaveData("save", titleId, userUid);

        if (R_FAILED(result))
        {
            snprintf(buffer, sizeof(buffer), "fsdevMountSaveData failed for titleId 0x%016lX: 0x%x", titleId, result);
            logErrorToFile(buffer);
            return false;
        }

        logInfoToFile("Successfully mounted save:/ for restore");

        // Copy the named save files only, never subdirectories.
        logInfoToFile("Copying backup save files to save:/", backupDir);

        char srcPath[512];
        char destPath[512];
        bool copyAllSuccess = true;

        snprintf(destPath, sizeof(destPath), "save:/%s", primaryFile.c_str());
        if (!copyFile(primaryPath, destPath))
        {
            snprintf(buffer, sizeof(buffer), "Failed to copy %s", primaryFile.c_str());
            logErrorToFile(buffer);
            copyAllSuccess = false;
        }

        for (size_t fileIndex = 0; copyAllSuccess && fileIndex < optionalFiles.size(); fileIndex++)
        {
            snprintf(srcPath, sizeof(srcPath), "%s/%s", backupDir, optionalFiles[fileIndex].c_str());

            if (!backupHasFile(srcPath))
            {
                // Ordinary: this save never produced the file. Whatever the game currently has
                // under that name stays where it is.
                logInfoToFile("Not in this backup, leaving the game's copy alone", optionalFiles[fileIndex].c_str());
                continue;
            }

            snprintf(destPath, sizeof(destPath), "save:/%s", optionalFiles[fileIndex].c_str());
            if (!copyFile(srcPath, destPath))
            {
                snprintf(buffer, sizeof(buffer), "Failed to copy %s", optionalFiles[fileIndex].c_str());
                logErrorToFile(buffer);
                copyAllSuccess = false;
            }
        }

        if (!copyAllSuccess)
        {
            logErrorToFile("Failed to copy backup files to the game save");
            fsdevUnmountDevice("save");
            return false;
        }

        // No second pass. The copies above already wrote the edited primary file, because the
        // backup directory holds the edits -- there is no separate "modified" copy to overlay.

        // CRITICAL: Commit changes to the save device before unmounting
        // Without this, changes remain in memory buffers and are never written to disk
        logInfoToFile("Committing changes to save device...");

        Result commitResult = fsdevCommitDevice("save");
        if (R_FAILED(commitResult))
        {
            snprintf(buffer, sizeof(buffer), "fsdevCommitDevice failed: 0x%x", commitResult);
            logErrorToFile(buffer);
            fsdevUnmountDevice("save");
            return false;
        }

        logInfoToFile("Successfully committed changes to save device");

        fsdevUnmountDevice("save");

        logInfoToFile("Backup restored to the game save successfully!");
        return true;
    }

    // True only for an auto-history folder shaped exactly like getTimestamp(): YYYYMMDD_HHMMSS.
    // User-named backups and the reusable "Working" copy are, by definition, everything else.
    static bool isTimestampName(const std::string &name)
    {
        if (name.length() != 15 || name[8] != '_')
            return false;
        for (size_t index = 0; index < name.length(); ++index)
        {
            if (index == 8)
                continue;
            if (name[index] < '0' || name[index] > '9')
                return false;
        }
        return true;
    }

    std::vector<std::string> listBackupDirectories(const char *gameDirectory)
    {
        std::vector<std::string> backupDirs;

        // A missing game directory just means "no backups for this title yet" — the ordinary state
        // before the first backup — so don't treat opendir failing as an error worth logging.
        DIR *dir = opendir(gameDirectory);
        if (!dir)
        {
            return backupDirs;
        }

        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL)
        {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            {
                continue;
            }

            // "Working" is the internal reusable staging copy used when auto-backup is off, not a
            // user backup — keep it out of the picker so it never reads as one (C5).
            if (strcmp(entry->d_name, "Working") == 0)
            {
                continue;
            }

            // Every other subdirectory of the game folder IS a user-facing backup: an auto-history
            // timestamp or a user-named backup. The old code kept only the timestamp shape, which
            // hid every custom-named backup the destination picker can create.
            if (entry->d_type == DT_DIR)
            {
                backupDirs.push_back(entry->d_name);
            }
        }
        closedir(dir);

        // User-named backups first (alphabetical), then auto-history newest-first. A named backup is
        // a deliberate choice, so it belongs at the top rather than sorted into the middle of the
        // timestamps by ASCII accident.
        std::sort(backupDirs.begin(), backupDirs.end(), [](const std::string &a, const std::string &b)
                  {
                      const bool whereLabel = isTimestampName(a), bt = isTimestampName(b);
                      if (whereLabel != bt)
                          return !whereLabel; // named before timestamped
                      if (whereLabel)
                          return a > b; // both timestamps: lexicographically greatest (newest) first
                      return a < b;     // both named: alphabetical
                  });

        return backupDirs;
    }

}