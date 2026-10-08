#include <cstdio>
#include <vector>
#include <locale>
#include <codecvt>

#include <errno.h>
#include <bits/basic_string.h>
#include <sys/stat.h>
#include <dirent.h>

#include "Globals.h"
#include "Save/Block.h"
#include "Save/GetSaveFileContents.h"
#include "Save/SaveEnvelope.h" // the rules live there so they can be tested off-console
#include "Utils/FileUtilities.h"
#include "Utils/Logger.h"
#include "Utils/HelperUtilities.h"
#include "Utils/StringHelpers.h"
#include "Trainer/Trainer.h"
#include "Trainer/Trainer7LGPE.h"
#include "Trainer/Trainer8SWSH.h"
#include "Trainer/Trainer9LZA.h"
#include "Trainer/Trainer9SV.h"
#include "Trainer/Trainer8LA.h"
#include "Trainer/Trainer8BDSP.h"
#include "Trainer/Trainer3RSE.h"
#include "Encryption/Encryption.h"
#include "Enums/GameVersion.h"

using namespace Utils;
using namespace Trainer;
using namespace Encryption;
using namespace Enums;

namespace Save
{

    /// Is `path` a directory that exists? Told apart from a file, because a stray file named like a
    /// backup folder must not be renamed over one.
    static bool isExistingDirectory(const std::string &path)
    {
        struct stat entryStatus{};
        return stat(path.c_str(), &entryStatus) == 0 && S_ISDIR(entryStatus.st_mode);
    }

    int migrateLegacyTitleFolders(const std::vector<InstalledTitle> &titles)
    {
        int movedCount = 0;
        for (const InstalledTitle &title : titles)
        {
            const std::string legacyPath =
                BASE_SAVE_DIRECTORY + "/" + legacyTitleFolderName(title.gameVersion);
            const std::string targetPath =
                BASE_SAVE_DIRECTORY + "/" + titleFolderName(title.titleId, title.gameVersion);
            // cannot happen while the folder carries the id, but say so rather than assume
            if (legacyPath == targetPath) continue;
            // nothing left behind for this title -- the normal case after the first run
            if (!isExistingDirectory(legacyPath)) continue;
            if (isExistingDirectory(targetPath))
            {
                // NEVER MERGE. The new folder already holds backups made since the rename, and
                // pouring an older tree into it would mix two histories that are the same format.
                Utils::logInfoToFile("Legacy backup folder left alone, its new folder already exists",
                                     legacyPath.c_str());
                continue;
            }

            // AMBIGUITY IS REFUSED RATHER THAN GUESSED -- the rule itself lives in TitleFolder.h,
            // pure, so it can be tested without a console.
            if (legacyFolderIsAmbiguous(title, titles))
            {
                Utils::logInfoToFile("Legacy backup folder is ambiguous and was NOT moved -- more than "
                                     "one installed title could have made it",
                                     legacyPath.c_str());
                continue;
            }

            // A rename is the whole job when the devoptab can do one. It is cheap, it cannot half-
            // finish, and it keeps the timestamps.
            if (rename(legacyPath.c_str(), targetPath.c_str()) == 0)
            {
                Utils::logInfoToFile("Moved legacy backup folder to", targetPath.c_str());
                ++movedCount;
                continue;
            }

            // FALL BACK TO COPY-THEN-DELETE, and in that order: the legacy folder is the only copy
            // of the user's backup history, so it is deleted only once a complete copy exists. A
            // failed copy takes its own partial target with it and leaves the original untouched.
            if (!copyDirectory(legacyPath.c_str(), targetPath.c_str()))
            {
                Utils::logErrorToFile("Could not copy legacy backup folder; it is untouched", legacyPath.c_str());
                deleteDirectoryRecursive(targetPath.c_str());
                continue;
            }
            if (deleteDirectoryRecursive(legacyPath.c_str()))
            {
                Utils::logInfoToFile("Copied legacy backup folder to", targetPath.c_str());
                ++movedCount;
            }
            else
            {
                // The copy is good, so nothing is lost -- but the old folder is still there and
                // would be copied again next launch. It cannot be: the target now exists, which the
                // merge guard above refuses. Worth a line so the duplicate is explainable.
                Utils::logErrorToFile("Copied legacy backup folder but could not remove the original",
                                      legacyPath.c_str());
                ++movedCount;
            }
        }
        if (movedCount > 0)
        {
            char summary[96];
            snprintf(summary, sizeof(summary), "Migrated %d backup folder(s) to title-id naming", movedCount);
            Utils::logInfoToFile(summary);
        }
        return movedCount;
    }

    std::unique_ptr<Trainer::Trainer> readTitleSaveAsTrainer(const char *backupDir, u64 titleId)
    {
        TrainerVariant variant = readTrainerInfo(backupDir, titleId);
        // The variant holds a VALUE, and the trainer classes are movable but not copyable, so the
        // chosen alternative is moved onto the heap rather than copied out.
        return std::visit(
            [](auto &trainer) -> std::unique_ptr<Trainer::Trainer>
            { return std::make_unique<std::decay_t<decltype(trainer)>>(std::move(trainer)); },
            variant);
    }

    TrainerVariant readTrainerInfo(const char *backupDir, u64 titleId)
    {

        GameVersion version = getGameVersion(titleId);
        GameVersion group = getGameGroup(version);
        // unknown title id -- ask the bytes
        if (group == GameVersion::Invalid) group = detectGroupInDirectory(backupDir);

        char buffer[512];
        snprintf(buffer, sizeof(buffer), "Detected game: %s (Group: %s)",
                 getGameVersionName(version).c_str(), getGameVersionName(group).c_str());
        Utils::logInfoToFile(buffer);

        switch (group)
        {
        case GameVersion::GG: // Let's Go Pikachu/Eevee
            return readTrainerInfoLetsGo(backupDir);

        case GameVersion::SWSH: // Sword/Shield
            return readTrainerInfoSwSh(backupDir);

        case GameVersion::ZA:
            return readTrainerInfoLZA(backupDir);

        case GameVersion::SV: // Scarlet/Violet — dedicated Gen 9 class (packed, no-gap slots)
            return readTrainerInfoSV(backupDir);

        case GameVersion::PLA: // Legends: Arceus — PA8 (read path)
            return readTrainerInfoLA(backupDir);
        case GameVersion::BDSP: // Brilliant Diamond/Shining Pearl — PB8 (flat read path)
            return readTrainerInfoBDSP(backupDir);

        case GameVersion::FRLG: // FireRed/LeafGreen — PK3 (GBA section-based read path)
            return readTrainerInfoFRLG(backupDir);

        default:
            logErrorToFile("Unsupported game version");
            return Trainer::Trainer7LGPE(std::vector<Block>());
        }
    }

    /**
     * Delete a `ModifiedSave` folder left inside a backup by an older build.
     *
     * That folder held edits only the inject path ever read back, so saving to a backup wrote a file
     * the loader never looked at and the edits appeared to vanish. Edits go straight into the backup
     * now, which makes any surviving ModifiedSave both stale and misleading: it holds an older copy
     * of the same save, sitting next to the real one.
     *
     * Called only after a save has SUCCEEDED, so the data being removed has just been superseded by
     * something newer in the directory above it. Best-effort -- a backup that keeps its old folder is
     * untidy, not broken, and must never fail the save.
     */
    static void purgeLegacyModifiedSave(const char *backupDir)
    {
        char legacyDir[1024];
        snprintf(legacyDir, sizeof(legacyDir), "%s/ModifiedSave", backupDir);

        struct stat fileStatus;
        // nothing there, normal case
        if (stat(legacyDir, &fileStatus) != 0 || !S_ISDIR(fileStatus.st_mode)) return;

        if (deleteDirectoryRecursive(legacyDir))
        {
            logInfoToFile("Removed legacy ModifiedSave folder from backup", legacyDir);
        }
        else
        {
            logErrorToFile("Could not remove legacy ModifiedSave folder (harmless)", legacyDir);
        }
    }

    bool saveTrainerInfo(Trainer::Trainer &trainer, const char *backupDir, u64 titleId, AccountUid userUid,
                         bool injectToTitle)
    {
        /**
         * Auto-detects the game version and calls the appropriate saving function.
         * Uses virtual getGameGroup() method to determine concrete type without RTTI.
         */

        // A file the user browsed to has no title id, and `backupDir` is that FILE. The session decides
        // the writer, not the group: FireRed/LeafGreen arrive both as an installed title and as a file.
        if (titleId == 0)
            return saveExternalSave(trainer, backupDir);

        GameVersion version = getGameVersion(titleId);
        GameVersion group = getGameGroup(version);
        // unknown title id -- ask the bytes
        if (group == GameVersion::Invalid) group = detectGroupInDirectory(backupDir);

        char buffer[512];
        snprintf(buffer, sizeof(buffer), "Saving for game: %s (Group: %s)",
                 getGameVersionName(version).c_str(), getGameVersionName(group).c_str());
        Utils::logInfoToFile(buffer);

        GameVersion trainerGroup = trainer.getGameGroup();

        bool passed;
        if (trainerGroup == GameVersion::GG)
        {
            // Let's Go - cast is safe because we checked the type via virtual method
            passed = saveTrainerInfoLetsGo(static_cast<Trainer::Trainer7LGPE &>(trainer), backupDir, titleId, userUid,
                                           injectToTitle);
        }
        else if (trainerGroup == GameVersion::SWSH)
        {
            // Sword/Shield - cast is safe because we checked the type via virtual method
            passed = saveTrainerInfoSwSh(static_cast<Trainer::Trainer8SWSH &>(trainer), backupDir, titleId, userUid,
                                         injectToTitle);
        }
        else if (trainerGroup == GameVersion::ZA)
        {
            passed =
                saveTrainerInfoLZA(static_cast<Trainer9LZA &>(trainer), backupDir, titleId, userUid, injectToTitle);
        }
        else if (trainerGroup == GameVersion::SV)
        {
            passed = saveTrainerInfoSV(static_cast<Trainer9SV &>(trainer), backupDir, titleId, userUid, injectToTitle);
        }
        else if (trainerGroup == GameVersion::PLA)
        {
            passed = saveTrainerInfoLA(static_cast<Trainer8LA &>(trainer), backupDir, titleId, userUid, injectToTitle);
        }
        else if (trainerGroup == GameVersion::BDSP)
        {
            passed =
                saveTrainerInfoBDSP(static_cast<Trainer8BDSP &>(trainer), backupDir, titleId, userUid, injectToTitle);
        }
        else if (trainerGroup == GameVersion::FRLG)
        {
            passed =
                saveTrainerInfoFRLG(static_cast<Trainer3FRLG &>(trainer), backupDir, titleId, userUid, injectToTitle);
        }
        else
        {
            logErrorToFile("Unsupported trainer type");
            return false;
        }

        if (passed)
            purgeLegacyModifiedSave(backupDir);
        return passed;
    }

    Trainer7LGPE readTrainerInfoLetsGo(const char *backupDir)
    {
        /**
         * Reads Pokemon Let's Go Pikachu/Eevee save file.
         *
         * Let's Go save format:
         * - File: savedata.bin (1,048,576 bytes = 1MB exactly)
         * - Active save data: First 0xB8800 bytes (757,760 bytes)
         * - Backup area follows after
         * - No external hash like SWSH
         *
         * The save data is NOT encrypted at file level like SWSH.
         * Individual Pokemon data IS encrypted using Gen 6/7 algorithm.
         */

        char savePath[512];
        snprintf(savePath, sizeof(savePath), "%s/savedata.bin", backupDir);

        char buffer[LOG_BUFFER_SIZE];
        snprintf(buffer, sizeof(buffer), "Reading Let's Go save from: %s", savePath);
        Utils::logInfoToFile(buffer);

        size_t fileSize = 0;
        uint8_t *file = readAllBytes(savePath, &fileSize);

        if (file == nullptr)
        {
            logErrorToFile("Failed to read Let's Go save file");
            return Trainer7LGPE(std::vector<Block>());
        }

        char fileSizeBuffer[512];
        snprintf(fileSizeBuffer, sizeof(fileSizeBuffer), "0x%016llX", static_cast<unsigned long long>(fileSize));
        logInfoToFile("Filesize", fileSizeBuffer);

        // Expected size: 1,048,576 bytes (1MB)
        if (fileSize < SAVE_SIZE7_LGPE)
        {
            logErrorToFile("Let's Go save file is too small");
            delete[] file;
            return Trainer7LGPE(std::vector<Block>());
        }

        std::vector<uint8_t> saveData(file, file + SAVE_SIZE7_LGPE);

        std::vector<Block> blocks = createBlocksFromSaveData7LGPE(saveData);

        snprintf(buffer, sizeof(buffer), "Created %zu blocks from Let's Go save data", blocks.size());
        logInfoToFile(buffer);

        Trainer7LGPE trainer(std::move(blocks));

        delete[] file;
        return trainer;
    }

    bool saveTrainerInfoLetsGo(Trainer7LGPE &trainer, const char *backupDir, u64 titleId, AccountUid userUid,
                               bool injectToTitle)
    {
        /**
         * Saves a Pokemon Let's Go Pikachu/Eevee save.
         *
         * LGPE isn't encrypted at the file level (only individual Pokemon are). We apply the
         * edits back into the trainer's blocks, re-read the ORIGINAL 1MB file (the trainer only
         * kept a few blocks, so we need the untouched blocks + the BEEF checksum footer + the
         * padding intact), patch the edited blocks in place, recompute their CRC-16/ARC block
         * checksums into the footer, and write the whole file back.
         */

        // Apply edits into the trainer's blocks: lay down all storage, then overlay party.
        trainer.updateItemBlock();  // serialize inventory back into the MY_ITEM block
        trainer.updateBoxBlock();   // full unified 1000-slot storage
        trainer.updatePartyBlock(); // overlay party members at their storage indices

        // Re-read the original full save file.
        char srcPath[512];
        snprintf(srcPath, sizeof(srcPath), "%s/savedata.bin", backupDir);
        size_t fileSize = 0;
        uint8_t *file = readAllBytes(srcPath, &fileSize);
        if (file == nullptr)
        {
            logErrorToFile("Failed to re-read Let's Go save for writing", srcPath);
            return false;
        }
        if (fileSize < SAVE_SIZE7_LGPE)
        {
            logErrorToFile("Let's Go save file too small to write");
            delete[] file;
            return false;
        }

        std::vector<uint8_t> raw(file, file + fileSize);
        delete[] file;

        // Patch edited blocks + recompute block checksums into the BEEF footer.
        trainer.updateTrainerInfoBlock(); // money / OT name; mutates blocks before the copy
        trainer.updatePokedexBlock();     // Zukan flags for everything in storage; also before the copy
        writeBlocksToSaveData7LGPE(raw, trainer.getBlocks());

        char savePath[1024];
        snprintf(savePath, sizeof(savePath), "%s/savedata.bin", backupDir);
        FILE *outFile = fopen(savePath, "wb");
        if (!outFile)
        {
            logErrorToFile("Failed to open file for writing", savePath);
            return false;
        }
        size_t written = fwrite(raw.data(), 1, raw.size(), outFile);
        fclose(outFile);
        if (written != raw.size())
        {
            logErrorToFile("Failed to write complete save file", savePath);
            return false;
        }

        logInfoToFile("Successfully wrote modified Let's Go save", savePath);

        if (injectToTitle)
        {
            logInfoToFile("Restoring modified Let's Go save to game save device...");
            if (!restoreBackupToTitle(userUid, titleId, backupDir, "savedata.bin"))
            {
                logErrorToFile("Failed to restore modified Let's Go save to game");
                return false;
            }
        }
        else
        {
            logInfoToFile("Not injecting to the game save - Let's Go save written to the backup only");
        }

        return true;
    }

    Trainer8SWSH readTrainerInfoSwSh(const char *backupDir)
    {
        char mainPath[512];
        snprintf(mainPath, sizeof(mainPath), "%s/main", backupDir);

        char buffer[LOG_BUFFER_SIZE];
        snprintf(buffer, sizeof(buffer), "Reading trainer info from: %s", mainPath);
        logInfoToFile(buffer);

        size_t fileSize = 0;
        uint8_t *file = readAllBytes(mainPath, &fileSize);

        char fileSizeBuffer[512];
        snprintf(fileSizeBuffer, sizeof(fileSizeBuffer), "0x%016llX", static_cast<unsigned long long>(fileSize));
        // Filesize should be 1,603,146 bytes (size)
        logInfoToFile("Filesize", fileSizeBuffer);

        std::vector<Block> blocks = decrypt(file, fileSize);
        Trainer8SWSH trainer(blocks);

        delete[] file;
        return trainer;
    }

    bool saveTrainerInfoSwSh(Trainer8SWSH &trainer, const char *backupDir, u64 titleId, AccountUid userUid,
                             bool injectToTitle)
    {
        trainer.updateItemBlock();

        trainer.updatePartyBlock();

        trainer.updateBoxBlock();
        trainer.updateBoxNameBlock(); // must precede this game's checksum/hash pass
        trainer.updateCurrentBoxBlock();

        trainer.updateTrainerInfoBlock(); // money / OT name; before the hash pass
        trainer.updatePokedexBlock();     // Zukan flags for everything in storage; before the hash pass
        std::vector<uint8_t> encryptedData = encrypt(trainer.getBlocks());

        char savePath[1024];
        snprintf(savePath, sizeof(savePath), "%s/main", backupDir);

        FILE *outFile = fopen(savePath, "wb");
        if (!outFile)
        {
            logErrorToFile("Failed to open file for writing", savePath);
            return false;
        }

        size_t written = fwrite(encryptedData.data(), 1, encryptedData.size(), outFile);
        fclose(outFile);

        if (written != encryptedData.size())
        {
            logErrorToFile("Failed to write complete save file", savePath);
            return false;
        }

        std::string successMsg = std::string("Successfully saved modified save to: ") + savePath;
        logInfoToFile(successMsg.c_str());

        // Only write into the real game save when THIS save was told to inject
        if (injectToTitle)
        {
            logInfoToFile("Restoring modified save to game save device...");
            if (!restoreBackupToTitle(userUid, titleId, backupDir, "main", {"backup", "poke_trade"}))
            {
                logErrorToFile("Failed to restore modified save to game");
                return false;
            }
        }
        else
        {
            logInfoToFile("Not injecting to the game save - written to the backup only");
        }

        return true;
    }

    Trainer9LZA readTrainerInfoLZA(const char *backupDir)
    {
        char mainPath[512];
        snprintf(mainPath, sizeof(mainPath), "%s/main", backupDir);

        char buffer[LOG_BUFFER_SIZE];
        snprintf(buffer, sizeof(buffer), "Reading trainer info from: %s", mainPath);
        logInfoToFile(buffer);

        size_t fileSize = 0;
        uint8_t *file = readAllBytes(mainPath, &fileSize);

        std::vector<Block> blocks = decrypt(file, fileSize);
        Trainer9LZA trainer(blocks);

        delete[] file;
        return trainer;
    }

    Trainer9SV readTrainerInfoSV(const char *backupDir)
    {
        char mainPath[512];
        snprintf(mainPath, sizeof(mainPath), "%s/main", backupDir);

        char buffer[LOG_BUFFER_SIZE];
        snprintf(buffer, sizeof(buffer), "Reading trainer info from: %s", mainPath);
        logInfoToFile(buffer);

        size_t fileSize = 0;
        uint8_t *file = readAllBytes(mainPath, &fileSize);

        std::vector<Block> blocks = decrypt(file, fileSize);
        Trainer9SV trainer(blocks);

        delete[] file;
        return trainer;
    }

    Trainer8LA readTrainerInfoLA(const char *backupDir)
    {
        char mainPath[512];
        snprintf(mainPath, sizeof(mainPath), "%s/main", backupDir);

        char buffer[LOG_BUFFER_SIZE];
        snprintf(buffer, sizeof(buffer), "Reading trainer info from: %s", mainPath);
        logInfoToFile(buffer);

        size_t fileSize = 0;
        uint8_t *file = readAllBytes(mainPath, &fileSize);

        std::vector<Block> blocks = decrypt(file, fileSize);
        Trainer8LA trainer(blocks);

        delete[] file;
        return trainer;
    }

    Trainer8BDSP readTrainerInfoBDSP(const char *backupDir)
    {
        char mainPath[512];
        snprintf(mainPath, sizeof(mainPath), "%s/SaveData.bin", backupDir);

        char buffer[LOG_BUFFER_SIZE];
        snprintf(buffer, sizeof(buffer), "Reading BDSP trainer info from: %s", mainPath);
        logInfoToFile(buffer);

        size_t fileSize = 0;
        uint8_t *file = readAllBytes(mainPath, &fileSize);

        // BDSP is a FLAT blob (no SwishCrypto) — hand the raw bytes straight to Trainer8BDSP.
        std::vector<uint8_t> data(file, file + fileSize);
        Trainer8BDSP trainer(std::move(data));

        delete[] file;
        return trainer;
    }

    bool saveTrainerInfoBDSP(Trainer8BDSP &trainer, const char *backupDir, u64 titleId, AccountUid userUid,
                             bool injectToTitle)
    {
        // BDSP is a FLAT blob (no SwishCrypto). Re-serialize party + box into the buffer, then
        // recompute the whole-file MD5 — after which getSaveData() IS the final on-disk save. Write
        // both SaveData.bin and its Backup.bin mirror so the game loads the edits either way.
        trainer.updateItemBlock();
        trainer.updatePartyBlock();
        trainer.updateBoxBlock();
        trainer.updateBoxNameBlock(); // must precede this game's checksum/hash pass
        trainer.updateCurrentBoxBlock();
        trainer.updateTrainerInfoBlock(); // money / OT name; before the MD5 hash pass
        trainer.updatePokedexBlock();     // Zukan flags for everything in storage; before the hash pass
        trainer.recomputeHash();
        const std::vector<uint8_t> &saveData = trainer.getSaveData();

        const char *fileNames[] = {"SaveData.bin", "Backup.bin"};
        for (const char *fname : fileNames)
        {
            char savePath[1024];
            snprintf(savePath, sizeof(savePath), "%s/%s", backupDir, fname);
            FILE *outFile = fopen(savePath, "wb");
            if (!outFile)
            {
                logErrorToFile("Failed to open file for writing", savePath);
                return false;
            }
            size_t written = fwrite(saveData.data(), 1, saveData.size(), outFile);
            fclose(outFile);
            if (written != saveData.size())
            {
                logErrorToFile("Failed to write complete save file", savePath);
                return false;
            }
        }
        logInfoToFile("Successfully wrote BDSP backup (SaveData.bin + Backup.bin)");

        if (injectToTitle)
        {
            logInfoToFile("Restoring modified BDSP save to game save device...");
            if (!restoreBackupToTitle(userUid, titleId, backupDir, "SaveData.bin", {"Backup.bin"}))
            {
                logErrorToFile("Failed to restore modified save to game");
                return false;
            }
        }
        else
        {
            logInfoToFile("Not injecting to the game save - written to the backup only");
        }
        return true;
    }

    bool saveTrainerInfoLA(Trainer8LA &trainer, const char *backupDir, u64 titleId, AccountUid userUid,
                           bool injectToTitle)
    {
        // Re-serialize the edited party + box blocks, then encrypt (hash is computed inside).
        // updateItemBlock is a no-op stub for LA (item write deferred).
        trainer.updateItemBlock();
        trainer.updatePartyBlock();
        trainer.updateBoxBlock();
        trainer.updateBoxNameBlock(); // must precede this game's checksum/hash pass
        trainer.updateCurrentBoxBlock();
        trainer.updateTrainerInfoBlock(); // money / OT name; before the hash pass
        trainer.updatePokedexBlock();     // research + statistics entries; before the hash pass
        std::vector<uint8_t> encryptedData = encrypt(trainer.getBlocks());

        char savePath[1024];
        snprintf(savePath, sizeof(savePath), "%s/main", backupDir);

        FILE *outFile = fopen(savePath, "wb");
        if (!outFile)
        {
            logErrorToFile("Failed to open file for writing", savePath);
            return false;
        }

        size_t written = fwrite(encryptedData.data(), 1, encryptedData.size(), outFile);
        fclose(outFile);

        if (written != encryptedData.size())
        {
            logErrorToFile("Failed to write complete save file", savePath);
            return false;
        }

        std::string successMsg = std::string("Successfully saved modified save to: ") + savePath;
        logInfoToFile(successMsg.c_str());

        if (injectToTitle)
        {
            logInfoToFile("Restoring modified save to game save device...");
            // Legends: Arceus pairs main with main2 -- NOT with Sword/Shield's backup/poke_trade.
            // It has no Surprise Trade, so there is no pending trade result for a poke_trade to
            // hold. Confirmed against real backups; `backup` is carried too because it has been
            // reported beside them, and a save without one just skips it.
            if (!restoreBackupToTitle(userUid, titleId, backupDir, "main", {"main2", "backup"}))
            {
                logErrorToFile("Failed to restore modified save to game");
                return false;
            }
        }
        else
        {
            logInfoToFile("Not injecting to the game save - written to the backup only");
        }

        return true;
    }

    bool saveTrainerInfoLZA(Trainer9LZA &trainer, const char *backupDir, u64 titleId, AccountUid userUid,
                            bool injectToTitle)
    {
        trainer.updateItemBlock();

        trainer.updatePartyBlock();

        trainer.updateBoxBlock();
        trainer.updateBoxNameBlock(); // must precede this game's checksum/hash pass
        trainer.updateCurrentBoxBlock();

        trainer.updateTrainerInfoBlock(); // money / OT name; before the hash pass
        trainer.updatePokedexBlock();     // Zukan flags for everything in storage; before the hash pass
        std::vector<uint8_t> encryptedData = encrypt(trainer.getBlocks());

        char savePath[1024];
        snprintf(savePath, sizeof(savePath), "%s/main", backupDir);

        FILE *outFile = fopen(savePath, "wb");
        if (!outFile)
        {
            logErrorToFile("Failed to open file for writing", savePath);
            return false;
        }

        size_t written = fwrite(encryptedData.data(), 1, encryptedData.size(), outFile);
        fclose(outFile);

        if (written != encryptedData.size())
        {
            logErrorToFile("Failed to write complete save file", savePath);
            return false;
        }

        std::string successMsg = std::string("Successfully saved modified save to: ") + savePath;
        logInfoToFile(successMsg.c_str());

        // Only write into the real game save when THIS save was told to inject
        if (injectToTitle)
        {
            logInfoToFile("Restoring modified save to game save device...");
            // Only `main` has ever turned up in a real Z-A backup. backup/poke_trade are listed
            // regardless because Z-A shares Scarlet/Violet's save format, where both exist and are
            // written conditionally -- if Z-A never produces them they are skipped and cost nothing.
            if (!restoreBackupToTitle(userUid, titleId, backupDir, "main", {"backup", "poke_trade"}))
            {
                logErrorToFile("Failed to restore modified save to game");
                return false;
            }
        }
        else
        {
            logInfoToFile("Not injecting to the game save - written to the backup only");
        }

        return true;
    }

    bool saveTrainerInfoSV(Trainer9SV &trainer, const char *backupDir, u64 titleId, AccountUid userUid,
                           bool injectToTitle)
    {
        // Re-serialize the edited blocks (item, party, box), then encrypt (hash is computed inside).
        trainer.updateItemBlock();
        trainer.updatePartyBlock();
        trainer.updateBoxBlock();
        trainer.updateBoxNameBlock(); // must precede this game's checksum/hash pass
        trainer.updateCurrentBoxBlock();
        trainer.updateTrainerInfoBlock(); // money / OT name; before the hash pass
        trainer.updatePokedexBlock();     // Zukan flags for everything in storage; before the hash pass
        std::vector<uint8_t> encryptedData = encrypt(trainer.getBlocks());

        char savePath[1024];
        snprintf(savePath, sizeof(savePath), "%s/main", backupDir);

        FILE *outFile = fopen(savePath, "wb");
        if (!outFile)
        {
            logErrorToFile("Failed to open file for writing", savePath);
            return false;
        }

        size_t written = fwrite(encryptedData.data(), 1, encryptedData.size(), outFile);
        fclose(outFile);

        if (written != encryptedData.size())
        {
            logErrorToFile("Failed to write complete save file", savePath);
            return false;
        }

        std::string successMsg = std::string("Successfully saved modified save to: ") + savePath;
        logInfoToFile(successMsg.c_str());

        if (injectToTitle)
        {
            logInfoToFile("Restoring modified save to game save device...");
            if (!restoreBackupToTitle(userUid, titleId, backupDir, "main", {"backup", "poke_trade"}))
            {
                logErrorToFile("Failed to restore modified save to game");
                return false;
            }
        }
        else
        {
            logInfoToFile("Not injecting to the game save - written to the backup only");
        }

        return true;
    }

    // Locate the GBA save inside a backup dir: prefer an exactly-128 KiB file, else any *.sav. Handles
    // Checkpoint exports ("FireRed_e.sav") and any mount name without hardcoding a filename.
    static std::string findGen3SaveFile(const char *dir)
    {
        DIR *d = opendir(dir);
        if (!d)
            return "";
        std::string best, anySav;
        struct dirent *ent;
        while ((ent = readdir(d)) != nullptr)
        {
            std::string name = ent->d_name;
            // "ModifiedSave" is a leftover directory from builds before saves went straight
            // into the backup; skip it so an old copy is never picked as the save to edit.
            if (name == "." || name == ".." || name == "ModifiedSave")
                continue;
            char full[1024];
            snprintf(full, sizeof(full), "%s/%s", dir, name.c_str());
            struct stat fileStatus;
            if (stat(full, &fileStatus) != 0 || !S_ISREG(fileStatus.st_mode))
                continue;
            if (static_cast<size_t>(fileStatus.st_size) == Trainer::FRLG_SAVE_SIZE)
            {
                best = name;
                break;
            }
            if (name.size() > 4 && name.compare(name.size() - 4, 4, ".sav") == 0)
                anySav = name;
        }
        closedir(d);
        return !best.empty() ? best : anySav;
    }

    Enums::GameVersion detectGroupInDirectory(const char *directory)
    {
        // Gen 3 is the only family that can be discovered this way, and that is not an omission:
        // every other Pokemon title on Switch is a single worldwide id, so there is no unreported
        // localisation for the probe to find. FireRed/LeafGreen ship one title per language, which
        // is the whole reason this exists. Adding a family later is one more block here.
        const std::string fileName = findGen3SaveFile(directory);
        if (!fileName.empty())
        {
            char savePath[1024];
            snprintf(savePath, sizeof(savePath), "%s/%s", directory, fileName.c_str());
            size_t byteCount = 0;
            uint8_t *bytes = readAllBytes(savePath, &byteCount);
            if (bytes != nullptr)
            {
                const std::vector<uint8_t> image(bytes, bytes + byteCount);
                delete[] bytes;
                if (Trainer3FRLG::detect(image))
                    return Enums::GameVersion::FRLG;
            }
        }
        return Enums::GameVersion::Invalid;
    }

    Trainer3FRLG readTrainerInfoFRLG(const char *backupDir)
    {
        std::string fileName = findGen3SaveFile(backupDir);
        if (fileName.empty())
        {
            logErrorToFile("No FRLG save (128 KiB / *.sav) found in backup dir", backupDir);
            return Trainer3FRLG(std::vector<uint8_t>(), "");
        }

        char savePath[1024];
        snprintf(savePath, sizeof(savePath), "%s/%s", backupDir, fileName.c_str());
        logInfoToFile("Reading FRLG save from", savePath);

        size_t fileSize = 0;
        uint8_t *file = readAllBytes(savePath, &fileSize);
        if (file == nullptr)
        {
            logErrorToFile("Failed to read FRLG save file", savePath);
            return Trainer3FRLG(std::vector<uint8_t>(), fileName);
        }
        std::vector<uint8_t> data(file, file + fileSize);
        delete[] file;

        char szBuf[64];
        snprintf(szBuf, sizeof(szBuf), "0x%zX", fileSize);
        logInfoToFile("FRLG filesize", szBuf);

        return Trainer3FRLG(std::move(data), fileName);
    }

    bool saveTrainerInfoFRLG(Trainer3FRLG &trainer, const char *backupDir, u64 titleId, AccountUid userUid,
                             bool injectToTitle)
    {
        const std::vector<uint8_t> &raw = trainer.serialize();

        const std::string name = trainer.fileName().empty() ? std::string("save.sav") : trainer.fileName();
        char savePath[1024];
        snprintf(savePath, sizeof(savePath), "%s/%s", backupDir, name.c_str());

        FILE *outFile = fopen(savePath, "wb");
        if (!outFile)
        {
            logErrorToFile("Failed to open FRLG save for writing", savePath);
            return false;
        }
        const size_t written = fwrite(raw.data(), 1, raw.size(), outFile);
        fclose(outFile);
        if (written != raw.size())
        {
            logErrorToFile("Failed to write complete FRLG save file", savePath);
            return false;
        }
        logInfoToFile("Successfully wrote modified FRLG save", savePath);

        if (injectToTitle)
        {
            logInfoToFile("Restoring modified FRLG save to game save device...");
            if (!restoreBackupToTitle(userUid, titleId, backupDir, name))
            {
                logErrorToFile("Failed to restore modified FRLG save to game");
                return false;
            }
        }
        else
        {
            logInfoToFile("Not injecting to the game save - FRLG save written to the backup only");
        }

        return true;
    }

    void backupOriginalFile(const char *savePath)
    {
        size_t length = 0;
        uint8_t *original = Utils::readAllBytes(savePath, &length);
        if (!original)
        {
            logErrorToFile("Could not read the save to back it up", savePath);
            return;
        }

        const char *slash = strrchr(savePath, '/');
        const std::string base = slash ? slash + 1 : savePath;
        const std::string directory = std::string(BASE_SAVE_DIRECTORY) + "/FileBackups";
        mkdir(std::string(BASE_SAVE_DIRECTORY).c_str(), 0777);
        mkdir(directory.c_str(), 0777);

        time_t nowSeconds = time(nullptr);
        struct tm *t = localtime(&nowSeconds);
        char stamp[32] = "unknown";
        if (t)
            strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", t);

        const std::string destination = directory + "/" + stamp + "_" + base;
        FILE *bf = fopen(destination.c_str(), "wb");
        if (bf)
        {
            const size_t dialogWidth = fwrite(original, 1, length, bf);
            fclose(bf);
            if (dialogWidth == length)
                logInfoToFile("Save backed up to", destination.c_str());
            else
                logErrorToFile("The backup was short", destination.c_str());
        }
        else
        {
            logErrorToFile("Could not write the backup", destination.c_str());
        }
        delete[] original;
    }

    namespace
    {
        /// One row per format PKSE can open from a file. The ORDER IS PKHeX'S GetTypeInfo order
        /// and it is load-bearing -- Gen 1 international and Gen 2 international are both 0x8000
        /// bytes, and Gen 4 and Gen 5 are both 0x80000, so the first probe that matches wins and
        /// the wrong order silently opens a save as the wrong game.
        struct LooseFormat
        {
            bool (*detect)(const std::vector<uint8_t> &);
            std::unique_ptr<Trainer::Trainer> (*make)(std::vector<uint8_t>, const std::string &);
            /// The save-format group this row opens. Carried ON THE ROW so that "which groups
            /// arrive as a file" has exactly one answer -- groupOpensFromFile() reads this table
            /// rather than restating it, and a second list that must agree with this one is the
            /// shape that silently goes wrong.
            Enums::GameVersion group;
        };

        template <typename T>
        std::unique_ptr<Trainer::Trainer> makeLoose(std::vector<uint8_t> bytes, const std::string &p)
        {
            auto looseTrainer = std::make_unique<T>(std::move(bytes), p);
            return looseTrainer->isValid() ? std::unique_ptr<Trainer::Trainer>(std::move(looseTrainer)) : nullptr;
        }

        bool detectRBY(const std::vector<uint8_t> &bytes)
        {
            bool isJapanese = false;
            return Trainer::Trainer1RBY::detect(bytes, isJapanese);
        }
        bool detectGSC(const std::vector<uint8_t> &bytes) { return Trainer::Trainer2GSC::detect(bytes); }

        const LooseFormat LOOSE_FORMATS[] = {
            {detectRBY, makeLoose<Trainer::Trainer1RBY>, GameVersion::RBY},
            {detectGSC, makeLoose<Trainer::Trainer2GSC>, GameVersion::GSC},
            // Gen 3 goes here, between Gen 2 and Gen 4, because that is PKHeX's order. The two Gen 3
            // rows cannot both match: they share a container and only Small+0xAC tells them apart, and
            // each detect() refuses the other's file rather than open it with the wrong offsets.
            {Trainer::Trainer3RSE::detect, makeLoose<Trainer::Trainer3RSE>, GameVersion::RSE},
            {Trainer::Trainer3FRLG::detect, makeLoose<Trainer::Trainer3FRLG>, GameVersion::FRLG},
            {Trainer::Trainer4DP::detect, makeLoose<Trainer::Trainer4DP>, GameVersion::DP},
            {Trainer::Trainer4PT::detect, makeLoose<Trainer::Trainer4PT>, GameVersion::PT},
            {Trainer::Trainer4HGSS::detect, makeLoose<Trainer::Trainer4HGSS>, GameVersion::HGSS},
            {Trainer::Trainer5BW::detect, makeLoose<Trainer::Trainer5BW>, GameVersion::BW},
            {Trainer::Trainer5B2W2::detect, makeLoose<Trainer::Trainer5B2W2>, GameVersion::B2W2},
            {Trainer::Trainer6XY::detect, makeLoose<Trainer::Trainer6XY>, GameVersion::XY},
            {Trainer::Trainer6ORAS::detect, makeLoose<Trainer::Trainer6ORAS>, GameVersion::ORAS},
            {Trainer::Trainer7SM::detect, makeLoose<Trainer::Trainer7SM>, GameVersion::SM},
            {Trainer::Trainer7USUM::detect, makeLoose<Trainer::Trainer7USUM>, GameVersion::USUM},
        };

        const LooseFormat *findLooseFormat(const std::vector<uint8_t> &saveImage)
        {
            for (const LooseFormat &format : LOOSE_FORMATS)
            {
                if (format.detect(saveImage))
                    return &format;
            }
            return nullptr;
        }

        bool isLooseSaveImage(const std::vector<uint8_t> &saveImage) { return findLooseFormat(saveImage) != nullptr; }
    }

    bool groupOpensFromFile(GameVersion group) noexcept
    {
        for (const LooseFormat &format : LOOSE_FORMATS)
        {
            if (format.group == group)
                return true;
        }
        return false;
    }

    bool findExternalSaveEnvelope(const std::vector<uint8_t> &fileBytes, SaveEnvelope &envelope,
                                  std::vector<uint8_t> &saveImage)
    {
        return findSaveEnvelope(fileBytes, isLooseSaveImage, envelope, saveImage);
    }

    std::unique_ptr<Trainer::Trainer> openExternalSave(std::vector<uint8_t> bytes, const std::string &path,
                                                       std::string *label, std::string *refusalText)
    {
        SaveEnvelope envelope;
        std::vector<uint8_t> saveImage;
        if (!findExternalSaveEnvelope(bytes, envelope, saveImage))
        {
            // A container PKSE recognises but cannot read says what to change; anything else gives its
            // size, the one fact a user can check against what their emulator should have written.
            const char *containerReason = describeUnreadableSaveContainer(bytes);
            if (refusalText)
                *refusalText = containerReason
                                   ? std::string(containerReason)
                                   : "is not a save PKSE can open (" + std::to_string(bytes.size()) + " bytes)";
            return nullptr;
        }
        if (envelope.kind != SaveEnvelopeKind::None)
            logInfoToFile((std::string("The loose save is inside ") + saveEnvelopeName(envelope.kind)).c_str(),
                          path.c_str());
        if (!envelope.paddingBytes.empty())
            logInfoToFile(("The loose save is followed by " + std::to_string(envelope.paddingBytes.size()) +
                           " bytes of padding")
                              .c_str(),
                          path.c_str());

        const LooseFormat *format = findLooseFormat(saveImage);
        std::unique_ptr<Trainer::Trainer> looseTrainer = format->make(std::move(saveImage), path);
        if (!looseTrainer)
        {
            logErrorToFile("A loose save was detected but failed to parse", path.c_str());
            if (refusalText)
                *refusalText = "looks like a " + getGameVersionName(format->group) + " save, but it could not be read";
            return nullptr;
        }
        if (label)
            *label = externalSaveLabel(*looseTrainer);
        logInfoToFile("Opened loose save", label ? label->c_str() : path.c_str());
        return looseTrainer;
    }

    std::string externalSaveLabel(const Trainer::Trainer &trainer)
    {
        const GameVersion gameVersion = trainer.getGameGroup();
        // Two formats can say more about themselves than their group name does, so they are asked.
        if (gameVersion == GameVersion::RBY)
        {
            const auto &r = static_cast<const Trainer::Trainer1RBY &>(trainer);
            return "Pokemon " + std::string(r.gameTitle()) + (r.japanese() ? " (JP)" : "");
        }
        if (gameVersion == GameVersion::GSC)
        {
            const auto &r = static_cast<const Trainer::Trainer2GSC &>(trainer);
            return r.gameTitle() + std::string(r.japanese() ? " (JP)" : "");
        }
        if (gameVersion == GameVersion::RSE)
        {
            // Emerald is the one of the three that is detectable (it alone has a security key), so
            // it is named outright. Ruby and Sapphire are byte-identical with nothing in the file
            // to separate them, exactly like Red/Blue, so the pair is named instead of guessing.
            const auto &hoenn = static_cast<const Trainer::Trainer3RSE &>(trainer);
            return hoenn.getGameVersion() == GameVersion::EM ? "Pokemon Emerald" : "Pokemon Ruby/Sapphire";
        }
        return "Pokemon " + std::string(getGameVersionName(gameVersion));
    }

    bool saveExternalSave(Trainer::Trainer &trainer, const char *savePath, bool backupOriginal)
    {
        if (!savePath || !*savePath)
        {
            logErrorToFile("Loose save: no path to write to");
            return false;
        }

        // Back up the ORIGINAL bytes before writing anything. This file is not a copy PKSE made --
        // the user pointed at their own save, quite possibly the only copy of it -- so the backup
        // has to be of what is on disk now, taken before the write. A caller writing to a scratch
        // copy of its own opts out: a backup per write would leave megabytes behind.
        if (backupOriginal)
            backupOriginalFile(savePath);

        // serialize() applies every block and refreshes the checksums last. It is not virtual on
        // the base -- each generation returns its own image -- so this dispatches on the group,
        // the same way everything else in PKSE does. Adding a format means adding a row here.
        const std::vector<uint8_t> *raw = nullptr;
        switch (trainer.getGameGroup())
        {
        case GameVersion::RBY:
            raw = &static_cast<Trainer::Trainer1RBY &>(trainer).serialize();
            break;
        case GameVersion::GSC:
            raw = &static_cast<Trainer::Trainer2GSC &>(trainer).serialize();
            break;
        case GameVersion::RSE:
            raw = &static_cast<Trainer::Trainer3RSE &>(trainer).serialize();
            break;
        case GameVersion::FRLG:
            raw = &static_cast<Trainer::Trainer3FRLG &>(trainer).serialize();
            break;
        case GameVersion::DP:
            raw = &static_cast<Trainer::Trainer4DP &>(trainer).serialize();
            break;
        case GameVersion::PT:
            raw = &static_cast<Trainer::Trainer4PT &>(trainer).serialize();
            break;
        case GameVersion::HGSS:
            raw = &static_cast<Trainer::Trainer4HGSS &>(trainer).serialize();
            break;
        case GameVersion::BW:
            raw = &static_cast<Trainer::Trainer5BW &>(trainer).serialize();
            break;
        case GameVersion::B2W2:
            raw = &static_cast<Trainer::Trainer5B2W2 &>(trainer).serialize();
            break;
        case GameVersion::XY:
            raw = &static_cast<Trainer::Trainer6XY &>(trainer).serialize();
            break;
        case GameVersion::ORAS:
            raw = &static_cast<Trainer::Trainer6ORAS &>(trainer).serialize();
            break;
        case GameVersion::SM:
            raw = &static_cast<Trainer::Trainer7SM &>(trainer).serialize();
            break;
        case GameVersion::USUM:
            raw = &static_cast<Trainer::Trainer7USUM &>(trainer).serialize();
            break;
        default:
            logErrorToFile("saveExternalSave called for a group that is not a loose save");
            return false;
        }

        // THE ENVELOPE GOES BACK ON. openExternalSave takes off whatever an emulator, a dumper or an
        // online service wrapped around the save, and the file has to come back in that shape or the
        // program that wrote it cannot read it: an RTC footer is Ruby's berry growth and tides, and VBA
        // Next expects its 136 KiB .srm. It is found again in the file on disk, by the same rule that
        // opened it, rather than carried on the trainer: it belongs to the FILE and no save format has
        // a field for it. It is kept only around a save the size of the one being written, so a path
        // that turns out to hold something else is left alone rather than glued onto.
        std::vector<uint8_t> fileBytes = *raw;
        {
            size_t existingSize = 0;
            if (uint8_t *existing = readAllBytes(savePath, &existingSize))
            {
                const std::vector<uint8_t> existingBytes(existing, existing + existingSize);
                delete[] existing;
                SaveEnvelope envelope;
                std::vector<uint8_t> existingImage;
                if (findExternalSaveEnvelope(existingBytes, envelope, existingImage) && !isBareSave(envelope) &&
                    existingImage.size() == raw->size())
                {
                    fileBytes = joinSaveEnvelope(envelope, *raw);
                    std::string keptMessage = std::string("Keeping ") + saveEnvelopeName(envelope.kind);
                    if (!envelope.paddingBytes.empty())
                        keptMessage += " and " + std::to_string(envelope.paddingBytes.size()) + " bytes of padding";
                    keptMessage += " on write-back";
                    logInfoToFile(keptMessage.c_str(), savePath);
                }
            }
        }

        FILE *f = fopen(savePath, "wb");
        if (!f)
        {
            logErrorToFile("Failed to open the save for writing", savePath);
            return false;
        }
        const size_t written = fwrite(fileBytes.data(), 1, fileBytes.size(), f);
        fclose(f);
        if (written != fileBytes.size())
        {
            logErrorToFile("Failed to write the complete save", savePath);
            return false;
        }
        logInfoToFile("Loose save written", savePath);
        return true;
    }

    bool saveTrainerInfoRBY(Trainer::Trainer1RBY &trainer, const char *savePath)
    {
        if (!savePath || !*savePath)
        {
            logErrorToFile("Gen 1 save: no path to write to");
            return false;
        }

        backupOriginalFile(savePath);

        // serialize() applies every block and refreshes the one-byte checksum last.
        const std::vector<uint8_t> &raw = trainer.serialize();
        FILE *f = fopen(savePath, "wb");
        if (!f)
        {
            logErrorToFile("Failed to open Gen 1 save for writing", savePath);
            return false;
        }
        const size_t written = fwrite(raw.data(), 1, raw.size(), f);
        fclose(f);
        if (written != raw.size())
        {
            logErrorToFile("Failed to write the complete Gen 1 save", savePath);
            return false;
        }
        logInfoToFile("Successfully wrote Gen 1 save", savePath);
        Utils::logEventToFile("SAVE gen=1 file=\"" + std::string(savePath) + "\" bytes=" +
                              std::to_string(written) + " result=OK");
        return true;
    }
}
