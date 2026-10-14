# 01 — Orientation

## What PKSE actually is

PKSE (Pokémon Save Editor) is a single Nintendo Switch homebrew executable, written in C++20 against
devkitPro/libnx and built to `PKSE.nro` (~155 MB, almost all of which is the bundled sprite art in
ROMFS). Its save-editing logic is derived from [PKHeX](https://github.com/kwsch/PKHeX). It:

1. Finds Pokémon save files — either by enumerating an account's **save data** by title ID, or by
   letting you **browse to a loose save file** on the SD card.
2. **Copies** the save into a backup folder under `sdmc:/PKSE/`.
3. **Parses** it — decrypt, unshuffle, unpack — into `Trainer` and `Pokémon` objects.
4. Lets you **edit** through a controller/touch UI: party, boxes, items, trainer info, and a
   per-Pokémon details editor.
5. **Writes it back** — re-serialise, re-encrypt, re-checksum, re-hash — into the backup, and
   optionally injects it over the live game save.

Plus three features that are not "edit a save" but hang off the same machinery: a **Pokémon
creator**, an informational **legality checker**, and a **cross-game bank** that converts a Pokémon
into the destination game's format when you withdraw it.

### What it covers

**All seven mainline Switch titles are implemented** and were validated on real hardware as of
v1.0.2: FireRed/LeafGreen, Let's Go, Sword/Shield, BDSP, Legends: Arceus, Scarlet/Violet,
Legends: Z-A.

**Every pre-Switch generation is implemented too**: Gen 1 (RBY), Gen 2 (GSC), Gen 3's GBA half
(RS/E, plus FR/LG as a GBA file), Gen 4 (DP/Pt/HGSS), Gen 5 (BW/B2W2), Gen 6 (XY/ORAS) and Gen 7's 3DS
titles (SM/USUM). Same capability — open, edit, write back — for a file from an emulator, a
cartridge dump or a 3DS save manager.

**That is a different ENTRY PATH, not just a different format.** PKSE finds a Switch save by title
id; a Game Boy, DS or 3DS save is a loose file with no title id and no account, so it goes through
`UI::Dialogs::FileBrowserDialog` and `Save::openExternalSave` instead. Chapter 03 shows the two paths
side by side and chapter 05 covers the loose-file pipeline.

## Where truth comes from

**PKHeX is the source of truth for CODE. The GAMES THEMSELVES AND POKÉMON HOME are the source of
truth for anything a user reads.** Offsets, checksums, encryption, block layouts, id spaces,
conversion tables, form indices, encounter data — PKHeX, every time.

For on-screen strings PKHeX is usually the games' own text verbatim, so this is a rule about
VERIFYING, not distrust. The gap is narrow: a few of its labels are disambiguations it added for its
own UI, like `Champion (Gen3)`, which no game has displayed. **When the two disagree about a
user-facing string, the game wins.** Where the games are genuinely ambiguous (two ribbons shown
under one name), say so in a comment and disambiguate deliberately rather than inheriting somebody
else's disambiguation.

This is why replacing a hand-written name table with a generated one is gated on **"the English
output must be identical"** — that gate is what stops a PKHeX-internal label from shipping (chapter
07 has the cases it caught).

## The documents

Docs are deliberately few. Do not add a new one — extend one of these.

| Document | What it is |
|---|---|
| `CLAUDE.md` (repo root) | The terse rules and traps, and a map into this manual. |
| `docs/CODING_STANDARDS.md` | Naming, layout and the verification block. Binding for every change. |
| `docs/manual/` (this) | How every layer works, the full rules with the reasoning behind them, worked recipes. |
| `docs/integration/` | One guide per generation, every offset transcribed from PKHeX. Its `README.md` gives the dependency order. |

## The mental model

Read this once and most of the codebase stops being surprising.

```
     ┌──────────────────────────────────────────────────────────────────────┐
     │  UI          immediate mode: no widget tree, redraw everything,      │
     │  src/UI      every frame. State lives in TrainerViewScreen.          │
     └───────────────────────────────┬──────────────────────────────────────┘
                                     │  reads + mutates directly
     ┌───────────────────────────────▼──────────────────────────────────────┐
     │  Trainer     one class per game (Trainer8SWSH, Trainer9SV, …)        │
     │  src/Trainer owns party[], boxes[][], items[][], trainerName, money  │
     └───────────────────────────────┬──────────────────────────────────────┘
                                     │  owns unique_ptr<Pokemon>
     ┌───────────────────────────────▼──────────────────────────────────────┐
     │  Pokemon     one class per game (Pokemon8SWSH, Pokemon9SV, …)        │
     │  src/Pokemon a DECRYPTED byte buffer + typed accessors over offsets  │
     └───────────────────────────────┬──────────────────────────────────────┘
                                     │  built by / serialised through
     ┌───────────────────────────────▼──────────────────────────────────────┐
     │  Encryption  one per game. XOR/LCG, block shuffle, hashing.          │
     │  Save        Block model (SCBlock), file I/O, backup, injection.     │
     └──────────────────────────────────────────────────────────────────────┘

     ┌──────────────────────────────────────────────────────────────────────┐
     │  Names / Pokemon tables    generated from PKHeX. Species, moves,     │
     │  src/Names, src/Pokemon    items, abilities, learnsets, encounters,  │
     │                            personal info, form names/sprites.        │
     └──────────────────────────────────────────────────────────────────────┘
```

Four things follow from this picture and they explain most of the design:

**1. There is no generic Pokémon.** `Pokemon::Pokemon` is an abstract base with ~190 virtuals, 55
of them pure. The concrete class *is* the format. `Pokemon8SWSH::speciesID()` reads offset `0x08`;
`Pokemon3FRLG` reads a completely different place with a different width. Nothing above the entity
layer knows an offset.

**2. There is no retained widget tree.** Every frame, every screen redraws itself from scratch out
of plain state. There is no `Button` object to attach a handler to. "Adding a control" means
drawing it in `draw()` and handling its button in `update()` — two places, and keeping them in sync
is manual. (Chapter 08 explains why this is the right trade here, and chapter 09 shows how to do it
without getting it wrong.)

**3. The UI reads and writes the model directly.** `TrainerViewScreen` holds a `Trainer&` and every
panel takes `TrainerViewScreen&`. There is no view-model, no observer, no dirty-tracking beyond a
single `hasUnsavedChanges` bool. This is deliberate and documented in the header — follow it rather
than introducing a new pattern.

**4. The data tables are load-bearing and generated.** A missing table entry is the single most
common source of wrong behaviour in this codebase, because callers historically read "no data" as
"definitely no". See chapter 07 and chapter 14.

## Directory map

```
PKSE/
├── CLAUDE.md                   the terse rules, and a map into this manual
├── Makefile                    the ONLY build. The version lives here.
├── README.md                   user-facing: features, install, build
├── assets/                     icon, banner, screenshots (only the icon goes into the .nro)
├── docs/
│   ├── CODING_STANDARDS.md     binding naming and layout rules
│   ├── integration/            one offset guide per generation, from PKHeX
│   └── manual/                 ← you are here
├── include/                    headers, mirroring src/ exactly
│   ├── Conversion/             cross-generation entity conversion
│   ├── Encryption/             per-group crypt
│   ├── Enums/                  GameVersion, SCTypeCode, Ball, LanguageID
│   ├── Legality/               the 3-layer checker + generated encounter tables
│   ├── Libs/                   stb_image.h (vendored)
│   ├── Names/                  generated string tables
│   ├── Pokemon/                entity classes + generated data tables
│   ├── Save/                   Block model, save read/write entry points, envelopes, title folders
│   ├── Trainer/                save-file classes + Inventory + Bank + the PKSM bank reader
│   ├── UI/                     framebuffer, screens, Panels/, Dialogs/, Modals/
│   ├── Utils/                  logging, strings, files, keyboard, settings, hashes, charsets
│   └── Globals.h               version string, runtime setting flags, paths
├── memecrypto/                 vendored C: Gen 7 save signing. Never edited — re-sync from upstream.
├── nanovg/                     vendored NanoVG (vector renderer)
├── romfs/                      GITIGNORED. sprites + fonts + type icons + marks. See ch. 02.
├── src/                        implementations, mirroring include/
└── tools/
    ├── gen_*.py                table and asset generators. Run by hand, occasionally.
    ├── asset_fetch.py          the shared download half of the four romfs fetchers
    ├── pkhex_source.py         fetches + caches PKHeX resources; tracks the default branch, PKHEX_REF pins
    ├── regenerate.py           runs every table generator in one go
    ├── formdump/               a .NET tool that calls PKHeX's real FormConverter (ch. 07)
    └── syntax_check.py         the fast compile check. Use it constantly.
```

### The nineteen groups

Every per-game class is suffixed with the code of its **save-format group** — the `GameVersion`
group's own name, so a class and the value it dispatches on cannot drift. There are nineteen and
every one has its own class in every layer, pre-Switch included (chapter 04 has the full argument).
Memorise these; they are everywhere.

| Suffix | Game(s) | Gen | Entity (box / party bytes) | Arrives as |
|---|---|---|---|---|
| `1RBY` | Red / Blue / Yellow (Green in Japan) | 1 | PK1, 33/44, in a 59/69-byte list | loose file |
| `2GSC` | Gold / Silver / Crystal | 2 | PK2, 32/48, in a 63/73-byte list | loose file |
| `3RSE` | Ruby / Sapphire / Emerald | 3 | PK3, 80/100 | loose file |
| `3FRLG` | FireRed / LeafGreen | 3 | PK3, 80/100 | Switch title **or** loose file |
| `4DP` | Diamond / Pearl | 4 | PK4 | loose file |
| `4PT` | Platinum | 4 | PK4 | loose file |
| `4HGSS` | HeartGold / SoulSilver | 4 | PK4 | loose file |
| `5BW` | Black / White | 5 | PK5 | loose file |
| `5B2W2` | Black 2 / White 2 | 5 | PK5 | loose file |
| `6XY` | X / Y | 6 | PK6 | loose file (3DS `main`) |
| `6ORAS` | Omega Ruby / Alpha Sapphire | 6 | PK6 | loose file (3DS `main`) |
| `7SM` | Sun / Moon | 7 | PK7 | loose file (3DS `main`) |
| `7USUM` | Ultra Sun / Ultra Moon | 7 | PK7 | loose file (3DS `main`) |
| `7LGPE` | Let's Go Pikachu / Eevee | 7 | PB7, 260 | Switch title |
| `8SWSH` | Sword / Shield | 8 | PK8, 328/344 | Switch title |
| `8BDSP` | Brilliant Diamond / Shining Pearl | 8 | PB8 | Switch title |
| `8LA` | Legends: Arceus | 8 | PA8, 360/376 | Switch title |
| `9SV` | Scarlet / Violet | 9 | PK9 | Switch title |
| `9LZA` | Legends: Z-A | 9 | PK9 | Switch title |

The games inside one group genuinely share a record byte for byte, and several groups share a record
FORMAT (DP/Pt/HGSS a PK4, BW/B2W2 a PK5, XY/ORAS a PK6, SM/USUM a PK7, S/V and Z-A a PK9) — they
still get a class each, because their save containers differ and because a shared class answered
`getGameGroup()` with the superset group, which checked a Black/White Pokémon against B2W2's learn
pool. `9SV` deliberately duplicates `9LZA` (same SCBlock + entity format, but S/V *packs* box/party
slots where Z-A *gaps* them) rather than sharing a class behind a mode flag — see chapter 04.

Gens 1 and 2 have **no** `Encryption1RBY` / `Encryption2GSC`, because their saves are not encrypted.
The layer simply does not exist for them. That is a real difference, not an omission.

## Namespaces

Domain-per-namespace, matching the directory:

```cpp
Trainer::    Trainer, Trainer8SWSH, Bank, InventoryItem
Pokemon::    Pokemon, Pokemon8SWSH, PersonalInfo, getPersonalInfo, getLevelFromExp
Save::       Block, readTrainerInfo, saveTrainerInfo, TrainerVariant
Encryption:: decrypt, encrypt, encryptArray8SWSH, cryptPokemon
Names::      getItemName, getMoveName, getFormName, getPouchItems
Enums::      GameVersion, SCTypeCode, Ball, LanguageID
Legality::   analyze, Report, Issue, EncounterRow
Conversion:: convert, canConvert, Result
UI::         UIManager, TrainerViewScreen, PKSEFramebuffer, Colors
UI::Panels:: drawItems, drawStorageView, drawHomeMenu
UI::Dialogs::drawPickerDialog, drawItemEditDialog, PickerKind
UI::Modals:: drawPokemonDetailsModal
Utils::      logInfoToFile, readUInt16LittleEndian, utf16ToUtf8, promptText
```

Several headers do `using namespace X;` at file scope. That is existing style in the older files;
do not add more of it, but do not go on a campaign to remove it either.

## Two hard language constraints

The Makefile builds with:

```make
CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions
CXXFLAGS += -std=c++20
```

**No RTTI.** `dynamic_cast` does not exist here. Type dispatch across generations goes through
`virtual GameVersion getGameGroup()` followed by a `static_cast`:

```cpp
const GameVersion gameGroup = trainer.getGameGroup();
if (gameGroup == GameVersion::SWSH) {
    // safe: the virtual just told us what this is
    auto& swordShieldTrainer = static_cast<Trainer::Trainer8SWSH&>(trainer);
    ...
}
```

**No exceptions.** Nothing throws; nothing catches. Errors are returned (`bool`, `nullptr`, a
`Result` enum) and logged. `std::vector` etc. are still used freely — the build simply terminates if
an allocation fails, which on a console with a fixed heap is an acceptable answer.

## Where the app writes on the SD card

```
sdmc:/PKSE/
├── settings.cfg                         theme, toggles (see ch. 03)
├── bank/bank.dat                        the cross-game bank
├── FileBackups/                         Save::backupOriginalFile: pre-write copies of loose save files
├── logs/debug_YYYY-MM-DD.log            INFO / ERROR / [EVENT], only when debug logging is ON
├── trace.log                            one-line-per-action outcome trace, only when debug logging is ON
└── Pokemon <Game> <TITLE ID>/           one per installed title, e.g. Pokemon LeafGreen 010087C02342E000
    ├── Working/                         the reused backup when auto-backup is OFF
    └── <timestamp>/                     a backup. THIS IS THE WORKING FILE — edits go straight in.
```

Every backup folder carries the **whole title id**, for every title — FireRed/LeafGreen ship one
Switch application per language, all twelve map to the same `GameVersion`, and a folder named from
the game alone let opening the French title load the English one's save. `Save::TitleFolder.h` owns
the folder name and the two display names, and `Save::migrateLegacyTitleFolders` moves backups out
of the older id-less folders (chapter 05).

The last line of the tree is important enough to repeat: **a backup directory is the working save
file**, not a pristine snapshot with a staging copy beside it. `Save::purgeLegacyModifiedSave`
deletes a `ModifiedSave/` subfolder left by older builds. Do not reintroduce a staging copy — see
chapter 05.
