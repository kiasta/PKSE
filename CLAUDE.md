# CLAUDE.md

Guidance for Claude Code (and any other AI tool) working in this repository. This file holds the
rules that apply everywhere and a map to where the rest live. **The full rules are in
[`docs/manual/`](docs/manual/README.md)**: each chapter explains how its layer works and ends with
that layer's rules, the traps behind them, and what went wrong when they were ignored. Before you
change a layer, read its chapter. The table under "Before you touch…" lists them.

## What this is

PKSE (Pokemon Save Editor) is a **Nintendo Switch homebrew** app (C++20, devkitPro/libnx) that
builds to `PKSE.nro`. It edits the saves of every mainline Pokemon game from Red/Blue to Legends:
Z-A. It also includes a Pokemon creator, an informational legality checker, and a cross-game bank
that converts a Pokemon to the destination game's format when it is withdrawn. Switch titles are
found by title id. Older saves (Game Boy, GBA, DS, 3DS) are loose files that the user browses to;
they have no title id and no account. User-facing docs are in `README.md`.

## Where truth comes from

**PKHeX is the source of truth for CODE.** Offsets, checksums, encryption, block layouts, id spaces,
conversion tables, form indices and encounter data all come from PKHeX. **The games themselves and
Pokemon HOME are the source of truth for anything a user reads.** PKHeX's strings are usually the
games' own text verbatim. A few are labels PKHeX added for its own UI (e.g. `Champion (Gen3)`), and
when PKHeX and a game disagree, **the game wins**. Where the games are ambiguous, disambiguate
deliberately and leave a comment saying so. PKHeX is the only data source. Nothing comes from
pokemondb or any other site.

## Coding standards (binding)

**Read [`docs/CODING_STANDARDS.md`](docs/CODING_STANDARDS.md) in full before writing or editing
code.** Where it and this file disagree, it wins. Where it and existing code disagree, follow its
precedence rules (§1) and say so in your final message.

- **Before creating ANY identifier or file, run the discovery greps in §2** and reuse the name that
  exists. One concept has one name in every file. See `docs/manual/15-reference.md` § Naming.
- **Before reporting a task complete, run §14**: the build, the violation greps and the self-review.
- The rules broken most often: a name states its SUBJECT (`panelWidth`, never bare `width`); no
  abbreviations (`framebuffer`, not `fb`); no single-letter names, loop counters included
  (`boxIndex`, not `i`); no `k`-prefixed constants; `g_` on a mutable global is the ONLY prefix (no
  `m_`, no trailing `_`); include guards, never `#pragma once`. Names that belong to outside code
  (libnx, vendored libraries, specs) are exempt. **PKHeX-derived names are not exempt**, except
  `TID`/`SID`/`TID16`/`SID16`/`ID32`, which stay as PKHeX spells them.
- **RTTI and exceptions are disabled** (`-fno-rtti -fno-exceptions`). Never use `dynamic_cast` or
  `throw`. Dispatch on `virtual GameVersion getGameGroup()`.
- `Pokemon` and `Trainer` are move-only. Pass them by reference or move them.
- Logging goes through `Utils::logInfoToFile` / `logErrorToFile` / `logEventToFile`, never
  `printf`. **Every sink checks `g_debugLogging`**. Wrap expensive `[EVENT]` arguments
  (`describePokemon`) in `if (g_debugLogging)`. Log before a destructive change and after a conversion.
  See chapter 03.
- **No issue, PR or bug numbers in code or comments.** Describe the problem itself.
- Never hand-edit generated tables (`src/Names/`, generated `src/Pokemon/` tables,
  `EncounterTable.cpp`) or vendored code (`nanovg/`, `memecrypto/`). Rerun the `tools/gen_*.py`
  generator, or re-sync from upstream.

## Build and verify

Requires `DEVKITPRO` and a POSIX shell (Linux, or MSys2 on Windows):
`pacman -S switch-dev switch-sdl2 switch-glad switch-lz4`. The full toolchain notes and traps are in
[`docs/manual/02-build-and-verify.md`](docs/manual/02-build-and-verify.md).

```bash
python tools/gen_fonts.py && python tools/gen_typeicons.py \
  && python tools/gen_marks.py && python tools/gen_hdsprites.py   # once per checkout (romfs/)
make                                   # -> PKSE.nro; refuses to start if a romfs asset is missing
python tools/syntax_check.py           # seconds: does it compile (real cross compiler)
python tools/syntax_check.py --warnings   # ~1 min: -O2 pass; the build is kept WARNING-FREE
```

- **The build downloads nothing.** Each asset group has its own fetcher, which you run by hand.
  Don't add network access to `make`, and don't add a download list for sprites (the fetcher
  mirrors the whole upstream tree).
- **There is one build.** Debug logging is a runtime setting (`g_debugLogging`, default off).
  Don't reintroduce a separate release build. `Utils::loadSettings()` stays the first statement
  in `main()`.
- **The version is set in the `Makefile` only**: `APP_VERSION` (15 characters at most, because
  `nacptool` truncates silently) and `APP_VERSION_FULL`. Also update the copies in
  `CppProperties.json` and `.vscode/c_cpp_properties.json`.
- A new source directory must be added to `SOURCES` in the Makefile (it does not glob recursively).
- Before calling a change done, run `syntax_check.py`, then `--warnings`, then `make`. Changes
  to save handling must also pass a real save round trip: write an unedited save back and require
  identical bytes (chapter 12 lists the few documented exceptions).

## Architecture in one screen

- **Generation-per-class.** There is one class per SAVE-FORMAT GROUP in every layer (`Trainer`,
  `Pokemon`, `Encryption`, `Inventory`), suffixed with the group's own `GameVersion` name. There
  are nineteen groups: `1RBY 2GSC 3RSE 3FRLG 4DP 4PT 4HGSS 5BW 5B2W2 6XY 6ORAS 7SM 7USUM 7LGPE 8SWSH
  8BDSP 8LA 9SV 9LZA`. **Prefer a clean copy over a shared class behind a mode flag.** Base classes
  and the UI never hardcode a game. A group's geometry and generation-wide constants are members
  of its class. See chapter 04.
- **`include/Enums/GameVersion.h` is the dispatch hub.** A game is exposed only through its entry
  point: a title id in `getGameVersion()`, or a row in `LOOSE_FORMATS`. Every title must resolve
  through `getGameGroup()`, which returns `Invalid` silently. See chapter 05.
- **Entities wrap a decrypted byte buffer.** After changing a field that affects stats, call
  `recalculateStats()`, then `refreshChecksum()`. A party record's tail holds real data, so always
  allocate the PARTY size and grow box records on load. See chapter 06.
- **The UI is immediate-mode** (NanoVG on GL, drawn through `PKSEFramebuffer`). Screens compose
  Panels, Dialogs and Modals, and `TrainerViewScreen` holds the shared state block. Follow that
  pattern rather than introducing a new one. See chapters 08 and 09.
- **Legality is informational.** PKSE never refuses an edit, and Layer 3 errs toward silence.
  See chapter 11.

## Rules that bite everywhere

- **Ask the entity or a predicate, never a list of games.** Use `Enums::isGen3Group(group)`, not
  `== FRLG`. Use `hasNature()`, `hasMetData()`, `hasEggData()`, `hasHandler()`, etc., because 0 is
  a real value for those fields. A gate that names games will be wrong about the next game.
- **Never keep a private list of supported formats.** Lists like that have gone stale twice and
  silently refused whole generations. Dispatch through the shared function that already covers
  every group.
- **A missing table means "unknown", not "no".** Handle "no data" separately from a real negative.
  Treating them the same has made the legality checker report "illegal" and the transfer sanitizer
  DELETE moves.
- **Pass `form` with `species`** on every lookup. Regional forms have different stats, abilities
  and learnsets. Ability slots are per game: use `Pokemon::getAbilitySlots(species, form, group)`.
- **"Empty" is `!pk || pk->speciesID() == 0`.** Several games keep a live blank entity in every box
  slot.
- **An empty slot is not zeros, and an unedited save must write back byte for byte.** Re-encrypt
  the record you read, write bag pouches back by position, and keep the file's save envelope (RTC
  footer, emulator wrapper). See chapter 05.
- **A loose save file is accepted exactly when PKHeX accepts it**: each `detect()` is PKHeX's own
  test, with nothing stricter. Emulator wrappers are save envelopes. See chapter 05.
- **A bank tag names a FORMAT, not a game.** Never compare `groupForBankTag(tag)` with an entity's
  own group. Bank tags are only ever added, never changed. See chapter 10.
- **Name tables are per language; stored text is not.** A byte is a font index, so the RECORD's
  language selects the codec. Gen 3 text goes through `include/Utils/Gen3Text.h` only. See
  chapter 07.
- **Never guess a title id, a PokeAPI form id or a form index.** Look each one up, against the
  eShop, `pokeapi.co` or PKHeX's `FormConverter` and `PersonalInfoTable` respectively. A wrong
  value that happens to exist renders or opens the wrong thing with no error.
- **Generated tables track PKHeX's default branch.** Run every generator whose game list you
  touched, and compare the output SEMANTICALLY. Append new groups to `gen_encounters.py`'s `GAMES`;
  never insert them. See chapter 07.

## Before you touch…, read

| Area | Chapter |
|---|---|
| Orientation, directory map | [`01-orientation.md`](docs/manual/01-orientation.md) |
| Toolchain, assets, versioning, logging setting | [`02-build-and-verify.md`](docs/manual/02-build-and-verify.md) |
| `main()`, frame loop, settings, logging | [`03-runtime-and-screens.md`](docs/manual/03-runtime-and-screens.md) |
| Adding/changing a group's classes | [`04-generation-pattern.md`](docs/manual/04-generation-pattern.md) |
| Reading/writing saves, backups, loose files, envelopes | [`05-save-pipeline.md`](docs/manual/05-save-pipeline.md) |
| Entity offsets, Gen 1/Gen 3 quirks, stats | [`06-pokemon-entity.md`](docs/manual/06-pokemon-entity.md) |
| Name/data tables, generators, languages | [`07-data-tables.md`](docs/manual/07-data-tables.md) |
| Screens, input, search, sprites/forms, keyboard | [`08-ui-layer.md`](docs/manual/08-ui-layer.md), [`09-ui-recipes.md`](docs/manual/09-ui-recipes.md) |
| Bank, conversion, Poke Transporter, PKSM import | [`10-bank-and-conversion.md`](docs/manual/10-bank-and-conversion.md) |
| Legality layers, encounter tables | [`11-legality.md`](docs/manual/11-legality.md) |
| Verifying a change | [`12-testing.md`](docs/manual/12-testing.md) |
| Adding a game or generation | [`13-adding-a-game.md`](docs/manual/13-adding-a-game.md) |
| Recurring failure modes | [`14-pitfalls.md`](docs/manual/14-pitfalls.md) |
| File map, glossary, naming table | [`15-reference.md`](docs/manual/15-reference.md) |
| Per-generation offsets from PKHeX | [`docs/integration/`](docs/integration/README.md) |

## Keeping this file small

This file is kept to **about 150 lines**. To add something, put the rule and its evidence in the
relevant manual chapter, and add a single line here only if the rule applies across layers. If a
manual chapter and the code disagree, the code is right: fix the chapter in the same change.
