# 05 — The save pipeline

## The whole flow, end to end

```
  pick a save                      Save::readTrainerInfo(backupDir, titleId)
       │                                       │
       ▼                                       ▼
  backupSaveData()  ──copies──>  sdmc:/PKSE/<Title>/<timestamp>/main
       │                                       │
       │                          readAllBytes → decrypt → parseAllBlocks
       │                                       │
       │                                       ▼
       │                          Trainer8SWSH ctor: for each Block → parseBlock()
       │                            ├─ MY_STATUS   → ID32, OT name, gender
       │                            ├─ PARTY       → 6 × Pokemon8SWSH
       │                            ├─ BOX         → 32×30 × Pokemon8SWSH
       │                            ├─ ITEM        → items[pouch][slot]
       │                            ├─ BOX_LAYOUT  → boxNames
       │                            └─ …
       ▼
  ══════════════  the UI edits Trainer / Pokemon objects in memory  ══════════════
       │
       ▼
  Save::saveTrainerInfo(trainer, destDir, titleId, uid, injectToTitle)
       │
       ├─ trainer.updateItemBlock()          ┐
       ├─ trainer.updatePartyBlock()         │  serialise back INTO the Block list /
       ├─ trainer.updateBoxBlock()           │  the raw buffer. ALL of these must run
       ├─ trainer.updateBoxNameBlock()       │  BEFORE the checksum/hash pass, because
       ├─ trainer.updateCurrentBoxBlock()    │  every one of them writes inside the
       ├─ trainer.updateTrainerInfoBlock()   │  checksummed region.
       ├─ trainer.updatePokedexBlock()       ┘
       │
       ├─ encrypt(blocks)  /  recomputeHash()  /  finalizeChecksums()
       ├─ fwrite → <destDir>/main
       └─ if (injectToTitle) restoreBackupToTitle(uid, titleId, destDir, "main", {...})
```

## Reading

`Save::readTrainerInfo(backupDir, titleId)` in `src/Save/GetSaveFileContents.cpp` is the front door.
It maps title ID → game → group and dispatches. An id PKSE does not know is not a refusal: it falls
back to the bytes in the directory (see [Title ids, folders and the save
picker](#rules-title-ids-backup-folders-and-the-save-picker)):

```cpp
GameVersion version = getGameVersion(titleId);
GameVersion group   = getGameGroup(version);
if (group == GameVersion::Invalid) group = detectGroupInDirectory(backupDir);   // ask the bytes
switch (group) {
    case GameVersion::GG:   return readTrainerInfoLetsGo(backupDir);
    case GameVersion::SWSH: return readTrainerInfoSwSh(backupDir);
    case GameVersion::ZA:   return readTrainerInfoLZA(backupDir);
    case GameVersion::SV:   return readTrainerInfoSV(backupDir);
    case GameVersion::PLA:  return readTrainerInfoLA(backupDir);
    case GameVersion::BDSP: return readTrainerInfoBDSP(backupDir);
    case GameVersion::FRLG: return readTrainerInfoFRLG(backupDir);
    default:
        logErrorToFile("Unsupported game version");
        return Trainer::Trainer7LGPE(std::vector<Block>());   // empty fallback
}
```

A per-game read function is short — locate the file, read it, hand the bytes to the right decoder,
construct the trainer:

```cpp
Trainer8SWSH readTrainerInfoSwSh(const char* backupDir) {
    char mainPath[512];
    snprintf(mainPath, sizeof(mainPath), "%s/main", backupDir);

    size_t fileSize = 0;
    uint8_t* file = readAllBytes(mainPath, &fileSize);

    std::vector<Block> blocks = decrypt(file, fileSize);   // Encryption::decrypt — SwishCrypto
    Trainer8SWSH trainer(blocks);

    delete[] file;
    return trainer;
}
```

The `Trainer` constructor does all the parsing:

```cpp
explicit Trainer8SWSH(std::vector<Save::Block> blocks) : Trainer(std::move(blocks))
{
    party.reserve(MAX_PARTY_SLOTS);
    boxes.resize(BOX_COUNT8_SWSH);
    boxNames.resize(BOX_COUNT8_SWSH);
    for (const auto& block : this->blocks) parseBlock(block);
    detectSaveRevision();          // which DLC blocks exist -> "Base" / "IoA" / "CT"
}
```

`parseBlock` is a `switch` on the block key, one case per block PKSE cares about, `default: break`
for everything else:

```cpp
void Trainer8SWSH::parseBlock(const Block& block) {
    switch (block.key) {
        case MY_STATUS8_SWSH:    parseMyStatusBlock(block);   break;
        case PARTY8_SWSH:        parsePartyBlock(block);      break;
        case MONEY8_SWSH:        parseMoneyBlock(block);      break;
        case TRAINER_CARD8_SWSH: parseTrainerCardBlock(block);break;
        case ITEM8_SWSH:         parseItemBlock(block);       break;
        case BOX8_SWSH:          parseBoxBlock(block);        break;
        case BOX_LAYOUT8_SWSH:   parseBoxLayoutBlock(block);  break;
        case CURRENT_BOX8_SWSH:  parseCurrentBoxBlock(block); break;
        default: break;                                       // unknown block — leave it alone
    }
}
```

Unrecognised blocks are **preserved untouched** in `blocks` and re-serialised verbatim on save. That
is what lets PKSE write a save containing thousands of fields it has never heard of.

## The Block model (SwishCrypto games)

Sword/Shield, Legends: Arceus, Scarlet/Violet and Legends: Z-A store their save as a list of
**SCBlocks**: a 32-bit key, a type code, and bytes.

```cpp
// include/Save/Block.h
struct Block {
    uint32_t   key;
    SCTypeCode type;
    SCTypeCode sub_type = SCTypeCode::None;   // Array element type only
    std::vector<uint8_t> data;                // empty for Bool types
};

std::vector<Block> parseAllBlocks(const uint8_t* data, size_t data_size, size_t* outConsumed = nullptr);
std::vector<uint8_t> serializeAllBlocks(const std::vector<Block>& blocks);
```

`SCTypeCode` (`include/Enums/SCTypeCode.h`) tags the scalar/array type: `Bool1/2/3`, `Object`,
`Array`, `Byte`, `UInt16/32/64`, `SByte`, `Int16/32/64`, `Single`, `Double`.

> **Parsing stops at the first block that fails to decode, and everything after it is discarded.**
> This matters far more than it looks: `serializeAllBlocks()` re-emits only what was parsed, and the
> save path writes that as the *entire file* — so a parse that stops early **silently truncates the
> user's save on the next write**. `outConsumed` reports how many bytes were covered; compare it
> against `data_size` to find out whether the parse reached the end.

Block keys are named constants at the top of each `Trainer*.h`:

```cpp
constexpr size_t MY_STATUS8_SWSH    = 0xf25c070e;   // Trainer Details
constexpr size_t PARTY8_SWSH        = 0x2985fe5d;   // Party Data
constexpr size_t MONEY8_SWSH        = 0x1b882b09;
constexpr size_t ITEM8_SWSH         = 0x1177c2c4;
constexpr size_t BOX8_SWSH          = 0x0d66012c;
constexpr size_t BOX_LAYOUT8_SWSH   = 0x19722c89;   // Box Names
constexpr size_t CURRENT_BOX8_SWSH  = 0x017C3CBB;   // "U32 Box Index"
// DLC detection — these blocks only exist if the DLC has been played
constexpr size_t SAVE_REVISION8_R1_SWSH = 0x3F936BA9;   // Isle of Armor
constexpr size_t SAVE_REVISION8_R2_SWSH = 0x3C9366F0;   // Crown Tundra
```

Keys come from PKHeX. To find a new one, look it up in PKHeX's `SaveBlockAccessor*` classes.

### DLC revision is not a content gate

`saveRevision` / `saveRevisionString` record which DLC blocks exist. **Never use it to decide what a
save may contain.** Owning a DLC is not a precondition for holding its content: the games gate the
DLC *areas*, not the Pokémon, and a player without the Expansion Pass can be traded an Isle of Armor
species and use it normally, because the patch ships the data to everyone. PKHeX agrees — nothing
under `PKHeX.Core/Legality` references `SaveRevision`. Its one real constraint is the Pokédex, where
PKHeX declines to *register* a DLC-group species in a DLC-less save.

## Writing

Every per-game save function has the same skeleton. The order is not cosmetic:

```cpp
bool saveTrainerInfoSwSh(Trainer8SWSH& trainer, const char* backupDir,
                         u64 titleId, AccountUid userUid, bool injectToTitle) {
    trainer.updateItemBlock();
    trainer.updatePartyBlock();
    trainer.updateBoxBlock();
    trainer.updateBoxNameBlock();       // must precede this game's checksum/hash pass
    trainer.updateCurrentBoxBlock();
    trainer.updateTrainerInfoBlock();   // money / OT name; before the hash pass
    trainer.updatePokedexBlock();       // Zukan flags for everything in storage; before the hash pass

    std::vector<uint8_t> encryptedData = encrypt(trainer.getBlocks());   // hash computed inside

    char savePath[1024];
    snprintf(savePath, sizeof(savePath), "%s/main", backupDir);
    FILE* outFile = fopen(savePath, "wb");
    if (!outFile) { logErrorToFile("Failed to open file for writing", savePath); return false; }
    size_t written = fwrite(encryptedData.data(), 1, encryptedData.size(), outFile);
    fclose(outFile);
    if (written != encryptedData.size()) { logErrorToFile("...", savePath); return false; }

    if (injectToTitle) {
        if (!restoreBackupToTitle(userUid, titleId, backupDir, "main", {"backup", "poke_trade"}))
            return false;
    }
    return true;
}
```

**Every `update*Block()` call must come before the checksum/hash step**, because all of them write
inside the checksummed region. If you add a new `update*` virtual, add its call to *all seven* save
functions, in that block. A missing call is a silent no-persist — the edit shows in the UI, survives
until reload, and vanishes.

### What differs per game

| Game | File(s) | Container | Integrity |
|---|---|---|---|
| SWSH | `main` | SCBlocks | `encrypt()` — SwishCrypto, SHA-256 tail |
| LA | `main` | SCBlocks | same |
| S/V | `main` | SCBlocks | same |
| Z-A | `main` | SCBlocks | same |
| BDSP | `SaveData.bin` **+ `Backup.bin`** | **flat blob** | whole-file **MD5** (`recomputeHash()`) |
| LGPE | `savedata.bin` | own block layout | internal checksums |
| FRLG | `*.sav` (scanned for) | 14 GBA sectors | per-sector checksums (`finalizeChecksums()`) |
| RBY | the file itself | 32 KiB flat | **one byte**, `~(sum of 0x2598..checksumOfs-1)` |

BDSP writes **both** `SaveData.bin` and its `Backup.bin` mirror so the game loads the edits either
way:

```cpp
trainer.recomputeHash();
const std::vector<uint8_t>& saveData = trainer.getSaveData();
const char* fileNames[] = { "SaveData.bin", "Backup.bin" };
for (const char* fname : fileNames) { /* fwrite both */ }
if (injectToTitle)
    restoreBackupToTitle(userUid, titleId, backupDir, "SaveData.bin", {"Backup.bin"});
```

FireRed/LeafGreen scans the directory for the 128 KiB `*.sav` on read (so a Checkpoint export like
`FireRed_e.sav` is found automatically) and writes back under `trainer.fileName()`. A GBA FR/LG file
opened from the browser is a loose save instead, written by `saveExternalSave`; both writers call
`Trainer3FRLG::serialize()`.

### The encrypted blank

`updateBoxBlock()` for the SwishCrypto games has a subtlety that is easy to get wrong:

```cpp
// Empty box slots in the real save are an ENCRYPTED blank that DECRYPTS to species 0 -- NOT
// literal zeros. The game decrypts every box slot and checks species; a zeroed slot decrypts
// to garbage and renders a BAD EGG. Reuse the game's own blank: copy the raw bytes of an
// existing empty (species-0) slot; synthesize one if every box is full.
std::vector<uint8_t> blankSlot;
for (size_t bi = 0; bi < BOX_COUNT8_SWSH && blankSlot.empty(); ++bi)
    for (size_t s = 0; s < BOX_SLOTS; ++s)
        if (boxes[bi][s] && boxes[bi][s]->speciesID() == 0) { /* harvest those bytes */ }

if (blankSlot.empty()) {
    std::vector<std::byte> zero(SIZE_PARTY8_SWSH, std::byte{0});
    std::byte* enc = encryptArray8SWSH({zero.data(), SIZE_PARTY8_SWSH}, 0);
    const uint8_t* encBytes = reinterpret_cast<const uint8_t*>(enc);
    blankSlot.assign(encBytes, encBytes + SIZE_PARTY8_SWSH);   // assign, NOT resize — see ch. 02
    delete[] enc;
}
```

and the write gates on **species**, not on the pointer:

```cpp
if (boxes[boxIndex][slot] && boxes[boxIndex][slot]->speciesID() != 0) {
    // encrypt with the mon's own EC as seed, memcpy into the block
} else {
    std::memcpy(&block.data[offset], blankSlot.data(), SIZE_PARTY8_SWSH);
}
```

## Gen 1 is the odd one out

`saveTrainerInfoRBY(trainer, savePath)` takes a **file path**, not a backup directory, and ignores
`titleId` / `userUid` / `injectToTitle` entirely. There is no installed title to inject into.

It backs up the **original bytes before writing**, through the same `Save::backupOriginalFile` every
loose save uses, and a backup failure is logged but the write continues:

```cpp
// The user pointed at their own save, quite possibly the only copy of it -- so the backup has to
// be of what is on disk NOW, taken before the write.
backupOriginalFile(savePath);   // -> sdmc:/PKSE/FileBackups/<YYYYMMDD_HHMMSS>_<basename>

const std::vector<uint8_t>& raw = trainer.serialize();   // applies blocks + the 1-byte checksum last
FILE* f = fopen(savePath, "wb");
```

Gen 1 was the first loose format; every other one is written by `saveExternalSave`, covered under
[Loose save files](#rules-loose-save-files-gens-1-2-3-gba-4-5-6-7-3ds).

## Backups, destinations and injection

### The backup *is* the working file

Edits used to land in `<backup>/ModifiedSave/`, which only the inject path ever read back out — so
saving to a backup wrote a file the loader never looked at, and the edits silently vanished on
reload. That directory is gone. `Save::purgeLegacyModifiedSave()` deletes a surviving one after the
first successful save into that backup.

> **Do not reintroduce a staging copy.** The backup *is* the working file, which is what makes
> "save to this backup" mean anything.

### `backupSaveData` / `restoreBackupToTitle`

```cpp
// Utils/FileUtilities.h
bool copySaveDataTo(AccountUid userUid, u64 titleId, const char* destDir);
std::string backupSaveData(AccountUid userUid, u64 titleId, std::string titleName,
                           bool timestamped = true);
bool restoreBackupToTitle(AccountUid userUid, u64 titleId, const char* backupDir,
                          const std::string& primaryFile,
                          const std::vector<std::string>& optionalFiles = {});
```

`copySaveDataTo` is the half that moves bytes (mount save device → copy → unmount). It is split out
because some callers need the same copy into a scratch directory rather than a backup — reading a
trade partner's live save, for instance, goes to `sdmc:/PKSE/tradepartner` and is deleted again
afterwards. Putting a transient working copy into the user's backup list every time would make the
backup screen unusable inside a week.

`restoreBackupToTitle`'s `primaryFile` is the one file a backup must hold; `optionalFiles` beside it
are copied when present and passed over in silence when not, because whether they exist depends on
what the player has done in-game.

Backups are **never auto-pruned**. Deleting them is the user's decision.

### The save-destination picker

`TrainerViewScreen` offers up to three destinations, and *which* ones depends on where the session
came from — not on a blanket setting.

```cpp
enum SaveDest { DestThisBackup = 0, DestNewBackup = 1, DestGameSave = 2, DestExternalFile = 3 };

int saveDestCount() const {
    return externalFileSession ? 1
         : (loadedFromCart ? 2 : (g_injectToGameSave ? 3 : 2));
}
SaveDest saveDestAt(int row) const {
    if (externalFileSession) return DestExternalFile;
    if (loadedFromCart)      return row == 0 ? DestGameSave : DestNewBackup;
    return static_cast<SaveDest>(row);
}
```

| Session | Rows offered | Why |
|---|---|---|
| Loaded from the **live save** (`loadedFromCart`) | Game save (default), New backup | Writing your own save back is the ordinary thing an editor does. The data you'd overwrite is the data you just read. **"This backup" is deliberately not offered** — that backup is an automatic snapshot, not a file the user chose, and picking it silently sends edits somewhere the game never reads. |
| Loaded from an **older backup** | This backup, New backup, [Game save only if `g_injectToGameSave`] | This is the case that rolls a game backwards, so it needs the lock **and** an extra confirmation. |
| **External file** | Write it back where it came from | No backup tree, no live save, nothing to inject into. |

Note `saveDestIndex` is a cursor into the **visible rows**, not a `SaveDest` — the two stopped being
interchangeable once a title session dropped a row from the middle of the enum. Always map with
`saveDestAt()`.

`illegalDataWritten` is set once a value outside the games' own limits has actually been committed
this session (EV > 252, AV > 200, EV total > 510 — only reachable with "Allow illegal values" on).
The save dialog then carries a warning line. It is entirely separate from the destination logic: it
is about *what* is being written, not *where*.

### `performSave` — the single write path

Every destination funnels through one function so success/failure handling cannot drift:

```cpp
void TrainerViewScreen::performSave(const std::string& destDir, bool injectToTitle) {
    const bool ok = Save::saveTrainerInfo(trainer, destDir.c_str(), titleId, userUid, injectToTitle);
    saveConfirmActive = false;

    // The single most important trace line: what was written, where, and whether it worked.
    const std::string saveInfo = std::string("dest=") + (injectToTitle ? "GAME" : "BACKUP")
        + " folder=\"" + leafName(destDir) + "\""
        + " inject="   + (injectToTitle ? "1" : "0")
        + " illegaldata=" + (illegalDataWritten ? "1" : "0")
        + " result="   + (ok ? "OK" : "FAILED");
    Utils::logTest("SAVE     " + saveInfo);
    Utils::logEventToFile("SAVE " + saveInfo + " party=" + std::to_string(trainer.getPartySize()));

    if (ok) {
        hasUnsavedChanges = false;
        /* close the editors that were open */
        postStatus(injectToTitle ? "Written to the game save."
                                 : ("Written to backup \"" + leafName(destDir) + "\"."), 200);
        backupDir = destDir;   // a new backup becomes the session's working copy
    } else {
        // A failed save used to be INDISTINGUISHABLE from a successful one.
        postStatus("SAVE FAILED - your changes are still unsaved. Nothing was written.", 480);
        hasUnsavedChanges = true;
    }
}
```

Two details worth copying if you ever add a write path: **name the destination back to the user**
(with three of them, "saved" alone is ambiguous), and **make failure loud** — a silent failure that
also clears `hasUnsavedChanges` is worse than a crash.

`createNamedBackupDir` seeds a new folder with a **copy of the currently open one**, and that copy
matters: several games' save paths re-read the destination's existing save file to recover the
blocks they do not touch (Let's Go most obviously), so writing into an empty directory would produce
a truncated save. It also filters the typed name to `[alnum] -_` and suffixes `-2`, `-3` rather than
overwriting — a `/` or `..` would escape the PKSE directory entirely.

## Adding a new editable field to a save

The complete checklist, for a field that already exists in the save format:

1. **Find the offset.** PKHeX is the source of truth. Locate the block/offset in its `SAV*` class.
2. **Parse it** in the game's `parseXBlock()` into a member on `Trainer` (base if universal, derived
   if not).
3. **Serialise it** — either in an existing `update*Block()` or a new virtual with a default no-op.
4. **If it is a new virtual, call it from every one of the seven save functions**, before the
   checksum pass.
5. **Expose it in the UI** (chapter 09).
6. **Round-trip it**: write a real save back unedited and require a byte-identical file, then edit
   the field, save, reload, and confirm the value survives.

Step 6 is not optional. Writing real saves back unedited found five bugs in Gen 1 alone that were
purely PKSE rewriting bytes it had no business touching — item quantities clamped to the game's cap
of 99 on a save that legitimately carried 158, a level byte written where a party record leaves it
stale, an empty name field re-terminated with `0x50` where the game left `0x00`. No checksum catches
that class of bug; only the byte comparison does.

---

## Rules: title ids, backup folders and the save picker

### What a title id decides, and what it does not

`include/Enums/GameVersion.h` is the dispatch hub: `getGameVersion(titleId)` maps a Switch title id
→ game, `getGameGroup()` buckets games sharing a save format, `getSaveFileName()` gives the on-disk
name (`main` vs `savedata.bin`). `isGameSupported()` is derived from `getGameGroup()` and **gates
nothing** — a hand-kept second list once sat at twelve Switch titles while twenty-seven pre-Switch
ones were implemented around it. What exposes a game is its entry point: a title id in
`getGameVersion()`, or a `LOOSE_FORMATS` row.

`Save::GetSaveFileContents` provides `readTrainerInfo()` / `saveTrainerInfo()`, which auto-detect the
game from the title id and return a `TrainerVariant` (a `std::variant` over the seven Switch
`Trainer*` classes). The SCBlock games parse into `Save::Block`s (`include/Save/Block.h`) — key +
`SCTypeCode` (`include/Enums/SCTypeCode.h`) + bytes; edits mutate block bytes in place, and the list
is re-encrypted and re-hashed on save. Backups live under `sdmc:/PKSE` (`BASE_SAVE_DIRECTORY`) and
are never pruned automatically.

### FireRed/LeafGreen ship one title per language

**No other Pokemon title does.** The GBA originals have no in-game language option, so the Switch
release is a separate application per localisation — **twelve ids for two games**, one per GBA
language (JP/EN/FR/DE/IT/ES) × FR/LG. `Enums::PER_LANGUAGE_TITLES` is the one table carrying them,
and it carries the id, the game AND the language in one row on purpose: those are the same fact
about the same SKU, so a localisation cannot land with a version but no language. **It holds all
twelve**, cross-checked against the US, GB, DE, IT and JP eShop indexes: twelve distinct ids and no
more. Korea has none — the GBA originals never had a Korean release — and the US and EU share one
English SKU rather than having an id each.

**A title id must never be guessed**; a wrong one silently claims some other application's save.
The six ids added last were taken from a source that independently reproduced all six PKSE already
had, byte for byte, across three separate region files — that agreement is what made it usable, not
the source's reputation.

Listing only the English pair made a French LeafGreen owner see **"0 saves found"**:
`SaveSelectScreen` drops any id `getGameVersion()` does not know, so the save was present, readable,
and never offered. All twelve map to the same `GameVersion` because the save CONTAINER is identical —
same 128 KiB image, same fourteen rotated sectors, same PK3. **The Japanese SKU does not share the
charset**, and does not need to: nothing in a Gen 3 save records its language, so
`Trainer3FRLG::detectLanguage()` measures it from the trainer-name field width (PKHeX's
`Small+0x06 == 0`) whatever title id the save arrived under. The SKU is not the authority on that and
must not become one.

### One `GameVersion` per SKU is right for reading and wrong for telling titles apart

**That mapping is correct for everything that reads a save and wrong for everything that tells two
installed titles apart**, and both existed. The picker's tile label and its backup directory were
both built from the `GameVersion`, so every LeafGreen SKU drew a tile reading "LeafGreen" with
nothing to choose between them, and every one of them pointed at `sdmc:/PKSE/Pokemon LeafGreen`.
Opening the French title loaded whatever English bytes were already in that folder — and
`readTrainerInfo` reads the DIRECTORY, the title id only picks the parser — so saving with inject on
would have written an English save into the French title. Nothing failed and nothing logged, because
the two saves are the same format.

**`Save::TitleFolder.h` owns all three names, and they are deliberately different strings.**
`titleDisplayLabel()` is the picker tile and has 184 pixels to be readable in; `titleDisplayName()`
is prose (the backup screen's title bar, and the save dialog's *"This replaces X's save data"*);
`titleFolderName()` is a directory under `sdmc:/PKSE`. Conflating the last two is what let the
display name be a path in the first place.

**Every backup folder carries the whole title id — every title, no exceptions**
(`sdmc:/PKSE/Pokemon LeafGreen 010087C02342E000/`). Not just the SKUs known to collide: a rule that
covers the cases somebody thought of is precisely the rule that broke here. An unrecognised id is
identified by CONTENT, which answers with a GROUP — "FireRed/LeafGreen" — so without the id an
Italian FireRed and an Italian LeafGreen collide with each other as well as with every other unlisted
Gen 3 SKU. **The id is distinct by definition; a name is not**, and no amount of curating the name
list changes that. The id stays out of both display strings, because it is not there to be read — it
is there so the directory cannot lie about which title it belongs to. The TILE still needs to be
distinguishable, so it takes the localisation where PKSE knows it and a short id fragment where it
does not.

**A title's display name can contain `/`, and that name becomes a directory.** Every group name is a
pair or triple. A recognised id always resolves to a single game whose name has no slash, so this
surfaces only on the content-probe path — `directorySafeName()` maps `/` to `-` while the on-screen
label keeps the pair.

### Migrating folders made before the id was added

**Renaming orphans every backup already on the card, so `Save::migrateLegacyTitleFolders` moves
them** — not a convenience: it is the difference between a fix and a fix that silently hides every
user's history. It runs from the save picker once every launch, after ALL users are loaded, because
the backup tree is shared between users and whether a folder is ambiguous depends on every title the
console has rather than one account's.

**Ambiguity is refused, not guessed.** A legacy folder moves only when exactly ONE installed title
could have produced its name: `Pokemon Shield` was Shield's and nothing else's, while
`Pokemon LeafGreen` on a console with two LeafGreen SKUs installed could be either — and the saves are
the same format, so nothing in the bytes settles it. Those are left alone and named in the log.
Owning one SKU of each GAME is not ambiguous; the test keys on the name, not on "more than one Gen 3
title".

Three further guards, each the difference between a migration and a data-loss bug:

- it never **merges** — a legacy folder whose destination already exists is skipped, because that
  destination holds backups made since the rename;
- it never **deletes** anything it has not first copied successfully;
- any failure leaves the legacy folder exactly where it was, which is what makes it safe to
  re-attempt on every launch.

A `rename` does the whole job where the devoptab can do one, with copy-then-delete as the fallback —
in that order, never the reverse.

Both failure directions matter, because each hides the other: a migration that moves nothing leaves
every user's history invisible, and one that moves everything files somebody's French backups under
English. `legacyFolderIsAmbiguous` lives in `TitleFolder.h`, pure and away from the filesystem, for
the same reason `Save/RtcFooter.h` is its own header: the interesting half can then be checked on a
PC without a console.

### An unknown title id falls back to content

**A hand-kept list of somebody else's title ids cannot be complete.** `Save::detectGroupInDirectory()`
answers "what Pokemon save is in this directory" for both callers that need it (the picker mounts and
asks; `readTrainerInfo`/`saveTrainerInfo` ask the backup folder when the id resolves to nothing). It
returns a **group, never a game** — a Gen 3 save holds no byte saying FireRed or LeafGreen. Gen 3 is
the only family it can discover, which is not an omission: every other title has one id.

`Trainer3FRLG::detect()` is `Trainer3RSE::detect()` with its last comparison inverted (the fixed `1`
at `Small+0xAC` that FR/LG write and Hoenn does not), and the discriminator has to hold **both ways**
— a detector that merely said "this is Gen 3" would open a Ruby save with Kanto's offsets and show a
plausible, wrong trainer. Ruby and Sapphire are the reverse case: byte-identical with no version
byte, so the bytes cannot tell them apart and PKSE reports the first of the pair.

### Two rules every editor of a save inherits

**A non-null box slot is not an occupied box slot.** Several games keep a live entity in *every*
slot rather than a null pointer — SwSh's box writer stamps one canonical encrypted blank over each
unoccupied slot (see [The encrypted blank](#the-encrypted-blank)), because a slot holding leftover
bytes that decrypt to species 0 re-encrypts into a Bad Egg. So "empty" is
`!pk || pk->speciesID() == 0` everywhere in the UI; code testing the pointer alone finds a Pokemon in
all thirty slots of an empty box.

**A save writes straight into the backup directory, and `g_injectToGameSave` decides whether it
*also* goes to the real game save.** Edits used to land in a `<backup>/ModifiedSave/` subfolder only
the inject path read back, so saving to a backup wrote a file the loader never looked at and edits
vanished on reload. That directory is gone; `Save::purgeLegacyModifiedSave` deletes a surviving one
after the first successful save. Don't reintroduce a staging copy: the backup *is* the working file
(see [The backup *is* the working file](#the-backup-is-the-working-file)).

---

## Rules: loose save files (Gens 1, 2, 3-GBA, 4, 5, 6, 7-3DS)

*Every offset, per generation: [`docs/integration/`](../integration/) (read its `README.md` for
dependency order).*

### The entry path

**These saves are opened by browsing to a path, not by picking a title.** A Game Boy, DS or 3DS save
is a loose file with no title id and no account. `SaveSelectScreen` offers **Y → Open Save File**,
which raises `UI::Dialogs::FileBrowserDialog` and OPENS the pick with `Save::openExternalSave`
*before* leaving the picker, then hands the opened trainer to `UIManager::handleExternalSave`.

**The open is the check.** It used to be a lighter `detect()`-only test, after which
`handleExternalSave` opened the file a second time: a file that passed the first and failed the
second (a Ruby save whose newer slot was half-written did exactly that) closed the browser and
dropped the user on the title grid with nothing said. Nothing may pass a lighter test and then fail
in silence.

Such a session has `titleId == 0`, which `TrainerViewScreen::externalFileSession` keys on: the save
dialog's three normal destinations are meaningless with no backup tree and no live game save, so it
collapses to one — write the file back where it came from. `Save::backupOriginalFile` copies the
ORIGINAL bytes to `sdmc:/PKSE/FileBackups` *before* writing, because the user pointed PKSE at a file
it did not create and may have no other copy of. `saveTrainerInfo` sends every `titleId == 0` session
to `saveExternalSave` before looking at the group, because FireRed/LeafGreen arrive both ways: routed
by group, a GBA FR/LG file reached `saveTrainerInfoFRLG`, which reads its path as a backup DIRECTORY.

**A refusal is drawn, and `..` is not content.** Every refusal writes `FileBrowserState::status`
(`refusalText` from the open, after the file's name: *"Pokemon Ruby.srm" is not a save PKSE can open
(139264 bytes).*), and it is shown under the list whatever the folder holds. `drawFileBrowser` used
to read `status` only as the text for an EMPTY folder — and a refused file is a row of the listing,
so the folder is never empty when there is a refusal to show. Every refusal was invisible: the frame
before and after `A` was byte-identical, and a user reported it as "pressed A and nothing happened".
The line now sits in the strip under the last row, which the footer leaves free, so nothing moves and
no row is covered. The same counting hid the empty-folder hints: `..` made every folder but the root
non-empty, so "No save files here. Press X to show every file." never appeared exactly where a user
browsing to a save with an unlisted extension needed it. The browser also said "Bank files" in the
save picker — strings written for the PKSM import — so the owner now names the filter
(`filterLabel`) when it opens the dialog. The state was right the whole time; only the drawn frame
was wrong, so that is what has to be checked.

**The browser remembers folders, and the owner decides which.** Without this, every open started
from the same fixed guesses and the user walked down to their emulator's save folder each time.
`FileBrowserState::open` takes the first start folder that exists, so each owner simply puts
`Utils::lastFolder(kind)` first (an empty one is skipped) and calls `Utils::setLastFolder` on the
pick, refused or not, since that is where the user went looking. The kind is per browser, not
global: the save picker and the trade-partner picker share `SaveFiles`, and the import browser keeps
`ImportFiles`, so opening a PKSM bank does not start in a GBA folder. Favorites are one shared list
(`Utils::favoriteFolders`): ZR toggles the current folder (or, in the list, the selected one), ZL
shows the list, B leaves it. A favorite is stored without its trailing slash and matched either way,
because a start folder is spelled `sdmc:/PKSE/saves/` and an entered one has no slash. Picking a
favorite that no longer exists says so and stays in the list rather than opening an empty folder.
All of it lives in `settings.cfg` (`lastSaveFolder=`, `lastImportFolder=`, one `favoriteFolder=` per
folder), which is why its line buffer is sized for a path. The self test points
`Utils::setSettingsFileOverride` at a scratch file before touching any of it, and reloads the real
file afterwards.

**`Save::openExternalSave` owns its trainer through the BASE POINTER**, because the concrete class is
not known until the probe chain runs. These classes cannot join `TrainerVariant` — a value type, and
they are large, polymorphic and non-copyable — so the write path dispatches on `getGameGroup()` and
`static_cast`, never RTTI. Adding a format is one row in `LOOSE_FORMATS` and one `case` in
`saveExternalSave`.

### What PKSE accepts is what PKHeX accepts

**Each `detect()` is PKHeX's own test for that format (`SaveUtil.GetTypeInfo` and its `IsG*`
helpers): the same file sizes, the same offsets, nothing stricter.** There are too many emulators,
flashcarts and dumpers to chase one by one, and PKHeX's tests are the ones its users' files have
already been measured against. Anything a file needs beyond that (an emulator's wrapper, padding)
is a SAVE ENVELOPE, below. Three checks that went further than PKHeX all refused real files:

- **Gen 3** required the sector footer magic (`0x08012025` at `+0xFF8`) in all 14 sectors of a slot.
  PKHeX's `SAV3.IsAllMainSectorsPresent` checks only that sector ids 0-13 are each present.
- **Gen 2** tied the layout to the file size (international at 0x8000 only, Japanese at 0x10000
  only). PKHeX's `IsG2` probes all four layouts at either size, and a Japanese cartridge's SRAM is
  32 KiB like everyone else's.
- **Gens 6 and 7** also required every BEEF-table entry to match the generated block table. PKHeX
  checks only the size and the BEEF magic.

Where PKSE deliberately accepts LESS than PKHeX, it says so: Korean Gold/Silver (no Korean Gen 2
codec), the Omega Ruby/Alpha Sapphire demo, and saves inside a `.zip` (refused with a message saying
to extract them).

### Probe order

**Probe order is load-bearing and it is PKHeX's** (`SaveUtil.GetTypeInfo`). Several formats share a
size and are told apart only by which offsets hold well-formed data: Gen 1 international and Gen 2
international are **both 0x8000 bytes**, and Gen 4 and Gen 5 are **both 0x80000**. Gen 1 before
Gen 2, Gen 4 before Gen 5, Gen 3 between Gen 2 and Gen 4. Getting the order wrong does not fail — it
opens a real save as the wrong game.

### An RTC footer is not corruption

Gold, Silver and Crystal have a real-time clock, and emulators, flashcarts and dumpers append its
state after the save proper — a real Silver save is commonly `0x8010`, not `0x8000`. Every size check
in the probe chain is EXACT, so such a file matched nothing and PKSE reported "not a save PKSE can
open" for a perfectly good save. `Save::stripRtcFooter` (`include/Save/RtcFooter.h`) is PKHeX's
`SaveHandlerFooterRTC` verbatim: the footer is the low six bits of the length, it must be 7 bytes
(one flashcart writes an odd one) or an even `0x0C`–`0x30`, and what remains must be `0x8000`,
`0x10000` or `0x20000`. It runs ONLY when nothing recognised the file as it stands.

**Do not loosen it**: guessing more freely starts truncating files that are merely corrupt, and a
truncated save parses and then quietly holds the wrong data. It lives in its own header, free of the
Switch SDK, so it can be checked on a PC.

### Save envelopes

**An RTC footer is one kind of save envelope** (`include/Save/SaveEnvelope.h`): bytes an emulator, a
flashcart, a dumper or an online service writes around a save, which the game never sees. PKHeX's
four handlers come first; everything after them PKHeX refuses, and each was read out of the source of
the program that writes it:

| Envelope | Shape | Written by | Source |
|---|---|---|---|
| `RtcFooter` | the save, then 7 or an even 0x0C-0x30 bytes of clock | mGBA (0x10 after a GBA save), BGB/VBA-M/SameBoy/Gearboy (0x30 after a Game Boy one), the Analogue Pocket (0x10), BizHawk (0x16), flashcarts | PKHeX `SaveHandlerFooterRTC` |
| `LibretroGbaSaveRam` | 0x22000 bytes: the 128 KiB flash save, then 8 KiB of EEPROM space | RetroArch's VBA Next and Beetle GBA cores | their own source, below |
| `DesmumeFooter` | what DeSmuME held — the 512 KiB DS save — then 0x7A bytes ending `\|-DESMUME SAVE-\|` | DeSmuME, DraStic, OpenEmu (`.dsv`) | PKHeX `SaveHandlerDeSmuME`; found by the signature, not the total |
| `ActionReplayHeader` | 0xA4 bytes, then the 512 KiB DS save | Action Replay DSi and DS Media Edition (`.duc`) | PKHeX `SaveHandlerARDS`; DeSmuME's "version 2" |
| `SwitchOnlineHeader` | `SRAM`, a ROM hash, a build string, an optional 0x20-byte clock, the save's SHA-1 as hex text, then a Game Boy save | Nintendo Switch Online's Game Boy app | PKHeX `SaveHandlerNSO` |
| `TgbDualClockFooter` | a Game Boy save, then a 4-byte signed clock offset | TGB Dual for Windows, for every MBC3 cartridge — Red and Blue as well as Gen 2 (`.sav`, `.sa2`) | `win32_ui/dialogs.h` |
| `MisterClockSector` | the save, then one 512-byte SD sector of clock state | MiSTer's Game Boy and GBA cores, once the game has used the clock | `Gameboy.sv`, `GBA.sv` ("Assume one extra sector of RTC data") |
| `MeteorSizeFooter` | the 128 KiB GBA save, then its own size as a u32 | RetroArch's meteor core | `libretro.cpp` (`CartMem::MAX_SIZE + 4`), `LoadCartInferred` |
| `BizHawkBatteryHeader` | `GBABATT\0`, two s32 lengths, the flash save, then the EEPROM | BizHawk 1.x's VBA Next core; its mGBA core still reads them | `SyncBatteryRam`, `MGBAHawk.LegacyFix` |
| `ActionReplayMaxDriveHeader` | `ARDS000000000001` in a 500-byte header, then the DS save | the first Action Replay DS's PC software and MAX Drive DS (`.duc`, `.dss`) | DeSmuME `import_duc`, "version 1" |
| `NocashGbaContainer` | a 0x40-byte header, then entries: `SRAM` (the save, stored or run-length coded), `LIFE`/`LIFF`, `STOP` | no$gba, GBA and DS (`BATTERY\*.SAV`) | DeSmuME `no_gba_unpackSAV`, and no$gba's own changelog and option strings |

**Padding is not a kind; it can follow the save in any of them** (`SaveEnvelope::paddingBytes`). A
flashcart or dumper hands back the whole chip, and a loader that grows a file never shrinks it: GBE+
pads a Game Boy save to 128 KiB, TWiLight Menu++ grows a file with zeros, and YSMenu kernels make
every Pokemon DS save 1 MiB (`savlib.dat` gives them `FF 02`, two 512 KiB units) and write their
per-game flags over the file's last 8 bytes, ending `NMSY`. Padding comes off only when every byte of
it is the same 00 or FF (bar YSMenu's eight) — what unwritten flash reads back as — and only after
every kind has failed on the file as it stands, so it can never shorten a save that opens without it.
The sizes are tried DS, then GBA, then the Game Boy sizes smallest first: Gen 2's probe accepts every
layout at 0x8000 or 0x10000, as PKHeX's does, so trying 0x10000 first would open a padded
international Gold/Silver/Crystal save with half its padding inside the image.
It goes back on verbatim, which is why this does not break the RTC rule above: guessing is dangerous
because it TRUNCATES, and nothing here is truncated. A DeSmuME `.dsv` around a 1 MiB YSMenu save is
the nested case, and opens. PKSM-Core's `SaveRecognition.cpp` makes the same uniform-tail test.

**No$gba is the one envelope written back in different bytes.** Its `SRAM` entry holds the save as
it is (method 0, "Uncompressed") or in a run-length code (method 1, "Compressed (fast/rlu)": `00`
ends, `01`-`7F` copy that many bytes, `81`-`FF` repeat the next byte `n - 0x80` times, `80` repeats it
by a u16 count). On save the save is coded again — same method, same header, every entry after `SRAM`
byte for byte — but a code can cut a save many ways and PKSE's greedy encoder picks its own. That is
verified rather than assumed: **DeSmuME's own `no_gba_unpackSAV`, compiled from its source, read back
every file PKSE wrote**, both methods, edited and unedited, across nine real GBA and DS saves; and
PKSE's encoder and decoder were each cross-checked against a second implementation that shares only
the format. The entry's length is read two ways — no$gba's loader counts the u32 of unpacked length
in front of the code, DeSmuME's does not — and whichever a file uses is accepted and kept. no$gba
stores only as much save as the game has touched, so a short entry is filled with FF to the save's
size, as DeSmuME does, and trimmed back on write unless the save now reaches past it. **Method 2
("Compressed (good/lz)") is refused**: no$gba 2.7c (July 2014) added it, later versions default to
it, nothing documents it, and there is no real file to test a decoder against. DS saves from the
2.6a era are run-length coded and open.

**A container PKSE cannot read says what to change** (`describeUnreadableSaveContainer`), in place of
"not a save PKSE can open (N bytes)": RetroArch's opt-in SaveRAM Compression (`#RZIPv`, zlib chunks,
or zstd since the 2026-09 nightlies — RetroArch reads an uncompressed `.srm` either way, so turning it
off and saving once is the whole fix), no$gba's LZ, Goomba Color (`0x57A731D8`/`0x57A731D9`; LZO, at
the same 32 and 64 KiB sizes as a raw Game Boy save), Retron 5 (`RTN5`, zlib), a ZIP archive, and
GodMode9's whole 3DS save container (`DISA` at 0x100). Each needs a decompressor PKSE does not ship —
zlib is not part of this build — so the advice is the honest answer. The text sits at the END of the
browser's one-line strip, which trims from the left: a long file name loses its start, never the
advice.

**The VBA Next and Beetle GBA rule is PKSE's own; PKHeX refuses those files too.** Both cores declare
`uint8_t libretro_save_buf[0x20000 + 0x2000]` (flash at offset 0, EEPROM at `+0x20000`) and report
the whole buffer as the save RAM. `adjust_save_ram()` would trim it, but VBA Next calls it only from
`retro_init`, right after filling the buffer with `0xFF` so no case matches, and Beetle GBA never
calls it. So RetroArch writes every `.srm` from VBA Next at 0x22000 bytes, Ruby's included, and
Beetle GBA's too when its save method is "libretro" — its default, "mednafen", writes a raw
`<rom>.<md5>.sav` instead and no `.srm` at all. VBA Next ships its own converter,
`libretro/gbaconv/gbaconv.c`, which turns a 1 Mbit flash `.srm` into a `.sav` by keeping the first
0x20000 bytes — the layout this rule reads. A user with one of these cores was refused outright, which
is how the missing refusal message was found. Both cores list Spanish FireRed and LeafGreen as EEPROM
games (`"BPRS", 131072, 1`), so those two cannot save on either — an emulator fault no reader can
repair.

**Checked against their source and needing nothing**: the mGBA libretro core (never writes the
clock), VBA-M standalone and libretro, gpSP, NanoBoyAdvance, SkyEmu, open_agb_firm, the EZ-Flash
Omega, the Pocket's openFPGA cores, melonDS (and Delta's `.dsv`, which is RAW despite the name — found
by content), NooDS, TWiLight Menu++ / nds-bootstrap, Wood R4 and AKAIO, and every 3DS save manager and
emulator (Checkpoint, JKSM, GodMode9's save export, Citra/Azahar: a raw `main`). The Game Boy cores
that keep the clock beside the save — Gambatte, SameBoy's and TGB Dual's libretro cores, mGBA's —
write a separate `.rtc` and a raw save.

**The order is PKHeX's, and the file as it stands always goes first.** `findSaveEnvelope` tries the
bare file, then each kind in `SAVE_ENVELOPE_KINDS`, then all of them again with padding taken off
what each holds; the first split whose inside the probe chain accepts wins. An envelope is only ever
tried on a file that is not a save, so it can neither shorten a good save nor reorder the probe
chain. Every kind added after PKHeX's is recognised by an exact size or a magic, so no two can claim
one file. The split checks what each writer's reader checks and leaves the rest to the probe chain —
with one addition: a Switch Online header must leave exactly a Game Boy save behind, which PKHeX also
requires, one step later.

**The envelope goes back on, or the emulator loses the save.** `saveExternalSave` finds the envelope
again in the file on disk, by the same rule (`findExternalSaveEnvelope`), and joins it around the new
image; it is kept only around a save the size of the one being written. A Switch Online header's
SHA-1 is taken again over the new save, as PKHeX's `Finalize` does, because the service checks it.
Measured through the real browser and editor: an unedited VBA Next `.srm`, DeSmuME `.dsv`, MAX Drive
`.dss`, MiSTer, meteor and BizHawk file and a 1 MiB YSMenu save with its flags all came back byte for
byte; a TGB Dual Gold save kept its clock offset and moved exactly the 211 bytes of Gen 2's backup
refresh (below); a run-length no$gba file decoded back to the original save with its `LIFE` and
`STOP` entries. Real Gen 1-5 saves wrapped in every envelope and padding of their size open and write
back the same way, and near-miss files are refused.

### Writing a save back unedited must give back the same file

Every finding below came from loading a real save, writing it back unedited and requiring a
byte-identical file. No checksum catches "PKSE rewrote bytes it had no business touching"; only the
byte comparison does.

- **A box record must be grown to party size on load.** Gens 4-7 store boxes at the shorter stored
  length and keep level + battle stats in the party tail, and the accessors index straight into that
  tail — so a stored-size entity reports **level 0 and zero stats** to the box grid, the details
  modal and the legality checker, and moving one into the party made the party writer encrypt a
  party-length span over a box-length allocation (a heap over-read AddressSanitizer catches and a
  console does not). `makeEntity()` in each Trainer grows the record and calls `recalculateStats()`,
  which fills the tail *and* seeds the level byte. The stored half is untouched, so the round trip
  stays byte-exact. Gen 1, Gen 2 and the PKSM import already did this; Gens 4-7 did not at first.
- **An empty slot is not zeros, and what it *is* differs per generation.** Gen 4/5 store
  `encrypt(zeros)` — header zero, then the fixed seed-0 LCG pattern beginning `7E E9 71 52` (each
  group's `Encryption::blankRecord<group>`, e.g. `blankRecord4DP`). Gen 6 stores a species-0 record
  whose nickname field reads **"Egg"**, with its own valid checksum. Stamping a blank of PKSE's own
  devising rewrote ~67 KB per Gen 4 save and ~1.8 KB per Gen 6 save. The general fix: **re-encrypt
  the record actually read back** (encryption is a bijection given the record's own key, so an
  untouched slot reproduces itself), and keep a verbatim copy of the file's first empty slot as the
  template for genuinely clearing one.
- **Bag pouches are POSITIONAL, not packed, in every one of Gens 4-7.** Ultra Sun's Items pouch
  holds 164 entries, then 248 empty slots, then **15 more with a count of 0** — items the player used
  up, parked at the end and still carrying their free-space sort index. Stopping at the first zero id
  loses them; re-packing moves them. PKHeX's `InventoryPouch4` reads the whole pouch for Gens 4 and 5
  too, so they get the same treatment: each Trainer records the slot every item came from and puts
  unchanged ones back where they were (`writePouchPositional` for 4/5, each Gen 6/7 container's own
  `updateItemBlock` for 6/7, where the Gen 7 ones also carry the free-space and "new" bits).

  **Sword/Shield's pouches are positional as well**, which is not a loose-file fact but belongs with
  this one. A real Sword save keeps used-up entries (count 0) in the last slots of five pouches — Max
  Potion in Medicine slot 59 with the pouch's first empty slot at 17, three items in Other Items slots
  547-549 with the first empty slot at 115 — and a Shield `backup` keeps a Miracle Seed it still
  holds one of in Other Items slot 549. PKHeX's `InventoryPouch8` reads and writes every slot; PKSE
  packed each pouch from slot 0 and so rewrote 26 bytes of Sword's unedited bag and 12 of that
  Shield save's. The placement rule is shared rather than copied: `placePouchPositional`
  (`Trainer/Inventory.h`) decides the slot per item without knowing a slot's encoding,
  `writePouchPositional` applies Gen 4/5's `u16 id, u16 count` codec on top of it, and
  `Trainer8SWSH::updateItemBlock` applies the Gen 8 `u32`.

  **An item is matched to its slot by ITEM ID, never by its position in the list**, and that is what
  makes a removal safe. The editor erases the entry from `items[pouch]`, which shifts every later
  entry down a place, so a position-matched writer finds a different item at each of those positions
  and sends the whole tail of the pouch to new slots: removing the fourth medicine from a real Sword
  save moved a used-up Max Potion from slot 59 to slot 16, and 580 bytes of that save's bag with it.
  Matching by id, only the removed slot changes. Where a pouch holds one id more than once the nth
  entry takes the nth recorded slot, so an untouched pouch still reproduces itself exactly.
- **The Gen 7 item word is one packed u32**, not two u16s: 10 bits id, 10 bits count, 10 bits
  free-space sort index, 1 bit "new". Reading it as two u16s gives a plausible id and a count with the
  sort index folded into its top bits.
- **Gen 4's counter offset and its CRC footer size are two different constants.** Block counters are
  always 0x14 from a block's end; `FooterSize` is 0x14 for DP/Pt but **0x10 for HGSS**. Unifying them
  corrupts every HeartGold save. And `0xFFFFFFFF` in a counter means "never written", not "newest" —
  a naive `a > b` picks the uninitialised partition and loads an empty save from a real file.
- **Gen 5 stores every block's CRC TWICE**, once past the block and once in the trailing checksum
  block, and that block **checksums itself** — so it must be stamped last, after every mirror lands.
- **A Gen 4 text field is CLEARED TO ZERO and then given exactly ONE terminator.** PKHeX writes with
  `StringConverterOption.ClearZero`, so a short name leaves the rest at 0x00 with a single 0xFFFF
  after the text — not a field padded with terminators. Padding is invisible to the game but rewrites
  bytes it left alone. `Utils::writeGen4Field` (`include/Utils/Gen4Text.h`) is the one place that
  does it.
- **Gen 6 checksums with CRC16-CCITT, Gen 7 with CRC16Invert.** Same width, same bytes, different
  algorithm; the wrong one produces a well-formed number the game rejects. Each container has its own
  `blockCrc()`.

### Gen 7 signs its saves

**`memecrypto/` is vendored for it.** SM and USUM store an RSA signature over a SHA-256 of the
checksum table inside **block 36 (TurtleSalmonSave)** at `+0x100`. Any edit moves the checksum
table, so a save written without re-signing is rejected by the game. `sign()` (`Trainer7SM::sign`,
`Trainer7USUM::sign`) runs last in `serialize()` — after every checksum, because the signature covers
them. It is verified end to end: edit a save, decrypt the signature PKSE wrote, and check the hash
inside against a fresh SHA-256 of the table PKSE wrote, which is the check the 3DS performs.

Two things here were established by measurement and contradict the obvious reading:

- **Block 36 IS checksummed, with the signature region blanked first.** Not "stale by design": SM
  stores 0x44BE and USUM 0xE25C, exactly the CRC over the block with `+0x100..+0x180` zeroed.
  `checksumBlank()` reproduces that; skipping the block leaves a stale checksum.
- **The signature's plaintext is NOT zeros**, whatever PKHeX's `SAV7` comment says. Decrypting a real
  Ultra Sun signature gives the SHA-256 followed by ~0x58 bytes that look like 3DS pointers — leftover
  console memory the game signed along with the hash. PKHeX blanks it and re-signs, which the game
  accepts; PKSE **recovers** it with `memecrypto_verify` and re-signs, reproducing the original
  signature byte for byte, which is what lets an unedited Gen 7 save round-trip exactly.
  `reverseCrypt()` is deliberately not used: it is written for a 0x60 payload, reads past its own
  buffer at 0x80, and is not the inverse of signing one.

`memecrypto/` is vendored third-party C (SciresM's memecrypto + tiny-AES + libsha1 + mini-gmp;
GPLv3/LGPL/public-domain, all compatible with PKSE's AGPLv3) and **must not be edited** — re-sync from
upstream. See `memecrypto/README.md`. It is in the Makefile's `SOURCES`/`INCLUDES`; anything building
it outside the Makefile must compile it with **`gcc`, not `g++`**, because it uses `xor` as an
identifier and that is an alternative token for `^` in C++.

### Gen 2

**A Gen 2 save does not come back byte for byte, and that is correct.** Gen 2 keeps a BACKUP of the
trainer block — Crystal one contiguous copy at `0x1209`, Gold/Silver five separate pieces — and the
games read it back, so writing a save refreshes it from the primary (PKHeX's `SAV2.GetFinalData`).
Real saves ship with a STALE backup, so writing one unedited corrects it: 211, 250 and 29 bytes on
the three real saves measured. It is the Gen 2 counterpart of Gen 1's current-box mirror flush and is
allowed on the same terms — **confined and incapable of losing anything**. The invariant is exact:
every byte an unedited write changes lies inside a mirror DESTINATION (or is one of the two checksum
words), and afterwards every mirror equals its SOURCE. The ranges live in
`Trainer2GSC::mirrorRegions()`, which `writeMirrors()` also reads, so the writer and anything checking
it cannot disagree about where they are.

Three Gen 2 facts are the opposite of Gen 1's:

- the checksum is a **16-bit wrapping sum, not inverted**, stored **little endian** in a format whose
  every entity field is big endian;
- the **current-box mirror is the STALE copy** (PKHeX: *"Stadium ignores it and will de-synchronize
  it"*), so it is an output on save and never an input;
- **money is a 3-byte big-endian integer, not BCD** (Gen 1's is BCD, and reading Gen 2's that way
  gives a plausible wrong number).

Gen 2's box stride is the list length **plus 2**. **Korean Gen 2 is deliberately not claimed** — it
needs the double-width `StringConverter2KOR` table and a second, differently-shaped checksum — so
`detect()` returns false rather than half-recognising a file.

**Gen 2's TM/HM pouch is positionally indexed and its id list has three gaps** — 191-194, 196-219,
221-242, then HMs 243-249, so 190, 195 and **220** are absent (220 is "TM28 (Unused)"). An arithmetic
mapping assuming one contiguous run puts every TM from 28 on in the wrong slot. `Trainer2GSC` reads
both directions out of the generated `Names::getPouchItems(GSC, 0)`.

### Learn pools for the loose-file games

**`tools/gen_learnsets.py` covers all eighteen games**, so `isLearnable()` answers everywhere and the
move picker highlights legal moves. Five things about the assemblers (the tables themselves are in
[chapter 07](07-data-tables.md)):

- **Gold/Silver and Crystal are UNIONED**, as Red/Blue and Yellow are: one Gen 2 group, and a PK2
  records nowhere which game it came from, so Crystal's three tutor moves are in the pool for a GS
  Pokemon too. Narrowing would report a legitimate Crystal tutor move as illegal — and the transfer
  sanitizer deletes what `isLearnable()` denies.
- **Gen 4's type tutors are NOT in the personal table.** They live in `tutors_g4.pkl`, indexed by the
  same form index; PKHeX loads it into the table at runtime. Reading bits out of the personal entry
  finds whatever happens to sit at that offset.
- **The TM/HM move list moves between files by generation.** Gens 5 and 7 keep theirs in
  `PersonalInfo*`; **Gen 6 keeps its in the LearnSource**, and B2W2 has none of its own (it does
  `using static PersonalInfo5BW`). A wrong file is a "not found", which is the good outcome — a wrong
  *list* would silently mis-name every TM.
- **The four tutor shops are B2W2-and-ORAS only.** BW and XY have no such tutors and their personal
  entries are shorter than those offsets — reading them from a BW table walks off the end of every
  entry.
- **SM stops at species 802 and USUM at 807.** One number for "Gen 7" quietly grants Sun/Moon six
  Pokemon it does not have.
