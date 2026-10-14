#ifndef GET_SAVE_FILE_CONTENTS_H
#define GET_SAVE_FILE_CONTENTS_H

#include <string>
#include <variant>
#include <vector>

#include <switch.h>

#include "Enums/GameVersion.h"
#include "Save/SaveEnvelope.h"
#include "Save/TitleFolder.h" // InstalledTitle + the naming rule the migration moves onto
#include "Trainer/Trainer.h"
#include "Trainer/Trainer7LGPE.h"
#include "Trainer/Trainer8SWSH.h"
#include "Trainer/Trainer9LZA.h"
#include "Trainer/Trainer9SV.h"
#include "Trainer/Trainer8LA.h"
#include "Trainer/Trainer8BDSP.h"
#include "Trainer/Trainer3FRLG.h"
#include "Trainer/Trainer1RBY.h"
#include "Trainer/Trainer2GSC.h"
#include "Trainer/Trainer4DP.h"
#include "Trainer/Trainer4PT.h"
#include "Trainer/Trainer4HGSS.h"
#include "Trainer/Trainer5BW.h"
#include "Trainer/Trainer5B2W2.h"
#include "Trainer/Trainer6XY.h"
#include "Trainer/Trainer6ORAS.h"
#include "Trainer/Trainer7SM.h"
#include "Trainer/Trainer7USUM.h"

#include <memory>
#include <vector>

using namespace Enums;
using namespace Trainer;

namespace Save
{
    // Type alias for trainer variants (supports different generation trainers)
    using TrainerVariant =
        std::variant<Trainer7LGPE, Trainer8SWSH, Trainer9LZA, Trainer9SV, Trainer8LA, Trainer8BDSP, Trainer3FRLG>;

    /**
     * Save File Reading/Writing Functions
     *
     * Different Pokemon games use different save file formats:
     * - Let's Go (GG): savedata.bin (1MB, simpler encryption)
     * - Sword/Shield (SWSH): main (1.6MB, block-based encryption)
     *
     * These functions provide game-specific save file handling.
     */

    /**
     * The save-format GROUP of whatever Switch save sits in a directory, judged from CONTENT.
     *
     * The fallback for a title id `Enums::getGameVersion()` does not recognise. That list is
     * hand-kept, and FireRed/LeafGreen ship a SEPARATE TITLE PER LANGUAGE, so an unreported
     * localisation is invisible to it -- which is exactly how a French LeafGreen owner was told
     * "0 saves found" for a save that was present and perfectly readable.
     *
     * Takes a directory rather than a title id so the same implementation answers for both places
     * that need it: a save mounted from the console (`SaveSelectScreen`, deciding what to offer)
     * and a backup directory on the SD card (`readTrainerInfo`/`saveTrainerInfo`, deciding how to
     * parse it). Two probes that must agree would be one more pair of lists that eventually do not.
     *
     * Returns the GROUP, never a specific game: a Gen 3 save holds no byte saying whether it is
     * FireRed or LeafGreen. Returns Invalid for anything unrecognised, which is the normal answer
     * for every non-Pokemon save on the console.
     */
    Enums::GameVersion detectGroupInDirectory(const char *directory);

    /// Moves a backup folder named after the GAME onto the title-id naming. A game name is not an
    /// identity -- Gen 3 ships one application per language, so every SKU of a game shared one
    /// folder -- and leaving the old folders behind orphans every backup already on the card,
    /// silently, because the files stay there and simply stop being listed.
    ///
    /// AMBIGUITY IS REFUSED, NOT GUESSED. A legacy folder moves only when exactly ONE installed
    /// title could have produced its name, so `titles` must list every Pokemon title across ALL
    /// users: the backup tree is shared between them. It never merges (a destination that already
    /// exists is skipped), never deletes what it has not first copied, and leaves the legacy
    /// folder exactly where it was on any failure, which is what makes it safe every launch.
    ///
    /// Returns how many folders moved.
    int migrateLegacyTitleFolders(const std::vector<InstalledTitle> &titles);

    /// When the title id names no game PKSE knows, the CONTENT of the directory decides the
    /// format instead.
    TrainerVariant readTrainerInfo(const char *backupDir, u64 titleId);

    /**
     * The same read, handed back through the BASE pointer instead of the variant.
     *
     * For a caller that only wants to ASK a donor save something -- the trade-partner picker reads
     * one trainer block and throws the rest away -- and has no business knowing which of the seven
     * Switch trainer classes came back. Returns null when the directory holds nothing readable.
     */
    std::unique_ptr<Trainer::Trainer> readTitleSaveAsTrainer(const char *backupDir, u64 titleId);

    bool saveTrainerInfo(Trainer::Trainer &trainer, const char *backupDir, u64 titleId, AccountUid userUid,
                         bool injectToTitle);

    Trainer7LGPE readTrainerInfoLetsGo(const char *backupDir);

    bool saveTrainerInfoLetsGo(Trainer7LGPE &trainer, const char *backupDir, u64 titleId, AccountUid userUid,
                               bool injectToTitle);

    Trainer8SWSH readTrainerInfoSwSh(const char *backupDir);

    bool saveTrainerInfoSwSh(Trainer8SWSH &trainer, const char *backupDir, u64 titleId, AccountUid userUid,
                             bool injectToTitle);

    Trainer9LZA readTrainerInfoLZA(const char *backupDir);

    /**
     * Reads trainer info from a Pokemon Scarlet/Violet save file.
     * S/V uses the Gen 9 SCBlock format but PACKS its box/party slots (no gap), so it has its own
     * dedicated Trainer9SV class (Trainer9LZA is the gapped Legends: Z-A counterpart).
     */
    Trainer9SV readTrainerInfoSV(const char *backupDir);

    bool saveTrainerInfoLZA(Trainer9LZA &trainer, const char *backupDir, u64 titleId, AccountUid userUid,
                            bool injectToTitle);

    /**
     * Saves trainer info to a Pokemon Scarlet/Violet save file (dedicated Trainer9SV, packed slots).
     */
    bool saveTrainerInfoSV(Trainer9SV &trainer, const char *backupDir, u64 titleId, AccountUid userUid,
                           bool injectToTitle);

    /** Reads trainer info from a Pokemon Legends: Arceus save file (PA8; box slots are stored-size). */
    Trainer8LA readTrainerInfoLA(const char *backupDir);
    /** Saves a Legends: Arceus save (party + box serialize; LA item write is deferred). */
    bool saveTrainerInfoLA(Trainer8LA &trainer, const char *backupDir, u64 titleId, AccountUid userUid,
                           bool injectToTitle);

    /** Reads a BDSP (SAV8BDSP) FLAT save (SaveData.bin) into a Trainer8BDSP — no SwishCrypto decrypt. */
    Trainer8BDSP readTrainerInfoBDSP(const char *backupDir);
    /** Saves a BDSP save: flat party/box serialize + whole-file MD5 rehash (SaveData.bin + Backup.bin). */
    bool saveTrainerInfoBDSP(Trainer8BDSP &trainer, const char *backupDir, u64 titleId, AccountUid userUid,
                             bool injectToTitle);

    /** Reads a GBA FireRed/LeafGreen (SAV3FRLG) 128 KiB save into a Trainer3FRLG (scans the dir for the
     *  128 KiB / *.sav file so Checkpoint exports like "FireRed_e.sav" are found automatically). */
    Trainer3FRLG readTrainerInfoFRLG(const char *backupDir);
    /** Saves an FRLG save: re-encrypt party/boxes/items in place + recompute all 14 sector checksums. */
    bool saveTrainerInfoFRLG(Trainer3FRLG &trainer, const char *backupDir, u64 titleId, AccountUid userUid,
                             bool injectToTitle);

    /**
     * Writes a Gen 1 save back to the file it was opened from.
     *
     * The odd one out, because a Gen 1 save is not an installed title's save: `savePath` is the
     * FILE, not a backup directory, and there is no title to inject into afterwards. A timestamped
     * copy of the ORIGINAL bytes is written under sdmc:/PKSE/Gen1Backups first -- the user pointed
     * PKSE at a file that is very likely their only copy, so it is backed up before it is touched,
     * not merely alongside.
     */
    bool saveTrainerInfoRBY(Trainer::Trainer1RBY &trainer, const char *savePath);

    /// A display name for the title bar. Most groups just name themselves; Gen 1 and Gen 2 know
    /// more about their own file than the group does (locale, and for Gen 2 the actual version).
    std::string externalSaveLabel(const Trainer::Trainer &trainer);

    /// Copies a file's CURRENT bytes to sdmc:/PKSE/FileBackups before it is overwritten.
    void backupOriginalFile(const char *savePath);

    /// Identifies `bytes` and constructs the matching trainer, or returns nullptr if the file is
    /// not a save PKSE opens.
    ///
    /// PROBE ORDER IS PKHeX'S, and it matters: Gen 1 international and Gen 2 international are
    /// both 0x8000 bytes, and Gen 4 and Gen 5 are both 0x80000, so several formats can only be
    /// told apart by which offsets hold well-formed data. Gen 1 is tried before Gen 2 and Gen 4
    /// before Gen 5, exactly as SaveUtil.GetTypeInfo does.
    ///
    /// A file that is not a save as it stands is tried again without each envelope an emulator,
    /// dumper or online service puts around one (Save/SaveEnvelope.h).
    ///
    /// `label` receives a display name for the title bar. `refusalText` receives why the file was
    /// refused, worded to follow its name: the file browser shows it, so the user is never left to
    /// guess why nothing opened.
    std::unique_ptr<Trainer::Trainer> openExternalSave(std::vector<uint8_t> bytes, const std::string &path,
                                                       std::string *label, std::string *refusalText = nullptr);

    /// Splits a loose save FILE into the envelope around it and the save inside, by the one rule
    /// openExternalSave and saveExternalSave both follow. False when no loose format is in there.
    bool findExternalSaveEnvelope(const std::vector<uint8_t> &fileBytes, SaveEnvelope &envelope,
                                  std::vector<uint8_t> &saveImage);

    /**
     * True when a save of this GROUP arrives as a loose FILE rather than as installed save data.
     *
     * Derived from the LOOSE_FORMATS table itself, so it cannot drift from what openExternalSave
     * will actually accept. The trade-partner picker is the caller: a record whose format comes
     * from a file gets the file browser, and everything else -- Sword/Shield, BD/SP, S/V, Z-A,
     * Let's Go -- gets the console's own save list, because those titles have no file to browse to.
     * FireRed/LeafGreen open both ways; no Gen 3 record reaches the picker, since before Gen 6 a
     * trade writes nothing to a Pokemon.
     */
    bool groupOpensFromFile(Enums::GameVersion group) noexcept;

    /// Writes a loose save back to the file it came from, backing up the ORIGINAL bytes first, and
    /// puts back any envelope the file had. Same contract as saveTrainerInfoRBY, which it now
    /// subsumes for every generation.
    ///
    /// `backupOriginal` is false ONLY for a caller writing to a throwaway copy it made itself and
    /// must leave nothing behind -- the same reason such a caller passes `injectToTitle`
    /// false. For a real user the backup is the whole point: they pointed PKSE at a file it did
    /// not create and may have no other copy of, so the default is true and should stay that way.
    bool saveExternalSave(Trainer::Trainer &trainer, const char *savePath, bool backupOriginal = true);
}

#endif