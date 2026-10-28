# 15 — Reference

## Glossary

| Term | Meaning |
|---|---|
| **PK1 … PK9, PB7, PB8, PA8, PA9** | The Pokémon entity formats, PKHeX's names. `PK<n>` is the mainline record of generation *n*; `PB7` is Let's Go (codename "Beluga"), `PB8` BDSP, `PA8` Legends: Arceus, `PA9` Legends: Z-A. |
| **PokeList1** | Gen 1's self-describing wrapper: header + party body + two name fields. What `Pokemon1RBY` actually holds. |
| **SCBlock** | The key/type/bytes record making up a Gen 8/9 save. Parsed into `Save::Block`. |
| **SwishCrypto** | The SCBlock container's encryption + SHA-256 integrity scheme. |
| **Personal table** | Per-(species, form) data: abilities, gender ratio, friendship, form count, types, per-game presence. |
| **Learnset bitset** | 116 bytes per (species, form, game); bit *M* set = move *M* is legally knowable. |
| **Encounter row** | One template in the Layer 3 legality tables: species, location, level span, constraints. |
| **DV** | Gen 1/2's 4-bit Individual Value. Caps at 15; HP's is derived. |
| **Stat Experience** | Gen 1/2's 16-bit-per-stat trainable value. Cap 65535, no total, contributes via a square root. Not an EV. |
| **AV (Awakening Value)** | Let's Go's stat training, 0–200 per stat, no total cap. Replaces EVs there. |
| **Hyper Training** | Gen 7+ Bottle Caps: a flag that makes the game **play** a stat at max IV without changing the stored IV. |
| **EC (Encryption Constant)** | Gen 6+ per-entity seed, also used for some form correlations. |
| **PID** | Personality Value. In Gen 3 it determines nature, gender, shininess and Unown's form. |
| **id32** | `SID16 << 16 \| TID16` — the full 32-bit trainer identity. Ownership comparisons use this, never the visible TID. |
| **OT / HT** | Original Trainer / Handling Trainer (Gen 7+). |
| **Game group** | A `GameVersion` value naming a save-format family (`SWSH`, `SV`, `RBY`), as opposed to an individual game. There are nineteen; each has its own class in every layer, suffixed with the group's name (`Trainer8SWSH`, `Pokemon3RSE`). |
| **Pane** | Storage view: `0` = the save's boxes, `1` = the bank. |
| **The carried block** | `moveMon` — the rectangle of Pokémon currently in hand. A single pick-up is a 1×1 block. |

## Naming

**One concept, one name, everywhere. This is a hard rule, not a preference** — it applies to every
line of new code, not just tidy-ups. The point is that a reader moving between files never has to
re-learn what a variable is called: a byte offset is `offset` in the Gen 1 entity and in the Gen 7
save container alike, and a width always says whose width it is. `docs/CODING_STANDARDS.md` is the
binding version of these rules (with the discovery greps to run before inventing a name); this
section is the quick reference.

### The rules

1. **No single-character names and no abbreviations.** Not in locals, parameters or struct fields.
   `i`, `w`, `pk`, `ofs`, `fb` and `tb` are all out. The only exemption is a name belonging to
   something outside PKSE — a libnx field (`HidTouchState::x`), a vendored library's API, or a spec
   the file is transcribed from. If PKSE owns the name, PKSE spells it out. **PKHeX-derived names
   are not exempt** unless listed under "Where a short name is still correct" below.
2. **A name says what the value IS, not what type it has.** `cardWidth`, not `w`; `boxIndex`, not
   `i`; `packedItemWord`, not `w`.
3. **Geometry carries its subject.** The old `drawCard(int x, int y, int w, int h)` never said
   whose width that was. Every drawing routine names its own: `drawCard(cardX, cardY, cardWidth,
   cardHeight)`, `drawPill(pillX, …)`, `setClipRect(clipX, …)`. The subject comes from the
   function's own name, so it is derivable rather than invented.
4. **A loop counter is named after what it counts.** The bound says it: `for (size_t boxIndex = 0;
   boxIndex < getBoxCount(); ++boxIndex)`. Where nothing more specific is true, `index`.
5. **Two things that are the same are spelled the same.** `TR_ID32` and `ST_ID32` were the same
   field in adjacent files; a width was `w`, `width`, `imgWidth` and `srcW` depending on where you
   opened. Pick the name from the table below before inventing one.
6. **`g_` on a mutable global is the ONLY prefix in this codebase.** A data member carries none —
   not `m_`, not a trailing underscore — and that rule beat the "existing code wins" precedence
   when the whole tree was converted at once, so there is no prefixed member left to be consistent
   with. Where the bare name would collide with an accessor (`layout()`, `securityKey()`,
   `fileName()`), the member takes its subject instead: `saveLayout`, `saveSecurityKey`,
   `saveFileName`.
7. **No `k`-prefixed constants and no `#pragma once`** (`docs/CODING_STANDARDS.md` §3.1, §6.4).

### The table

| Concept | Name | Not |
|---|---|---|
| Byte offset into a buffer | `offset`, or `<thing>Offset` | `o`, `ofs`, `off` |
| A byte buffer / span | `bytes`, `recordBytes`, `saveBytes` | `p`, `b`, `d`, `buf` |
| Count of things | `count`, or `<thing>Count` | `n`, `cnt` |
| Length in bytes | `byteCount`, `length` | `len`, `sz` |
| Index into a collection | `<thing>Index` (`boxIndex`, `slotIndex`, `blockIndex`) | `i`, `j`, `idx` |
| A Pokémon | `pokemon` (a copy: `clone`, `copiedPokemon`) | `pk`, `mon`, `c`, `p` |
| A trainer / save | `trainer` | `tr`, `t` |
| The value being written by a setter | `value`, `newValue` | `v` |
| A concrete title | `version` / `gameVersion` | `g`, `gv` |
| A save-format group | `group` | `g` |
| Framebuffer | `framebuffer` | `fb`, `canvas`, `surface` |
| Screen size | `screenWidth`, `screenHeight` | `W`, `H` |
| A drawn rectangle | `<subject>X/Y/Width/Height` | `x`, `y`, `w`, `h` |
| A circle's radius | `radius`; a rounded rect's is `cornerRadius` | `r` |
| Centre of a shape | `centerX`, `centerY` | `cx`, `cy` |
| Measured text | `textWidth`, `textHeight` | `tw`, `th` |
| A tap target | `hitX`, `hitY`, `hitWidth`, `hitHeight` | `x`, `y`, `w`, `h` |
| Colour channels | `red`, `green`, `blue`, `alpha` | `r`, `g`, `b`, `a` |
| Encryption constant | `encryptionConstant` | `ec` |
| PID | `pidValue` | `p`, `pid` |
| Level | `levelValue` (`level()` is the accessor) | `lv`, `lvl` |
| Checksum | `checksum`, `<thing>Checksum` | `chk`, `crc` |
| A rounded rect's corner | `cornerRadius` (not the same thing as a circle's `radius`) | `r` |

### Where a short name is still correct

Four, and each says so in a comment where it lives, so a future sweep does not "fix" it:

| Where | Names | Why |
|---|---|---|
| `src/Utils/MD5.cpp` | `A B C D F g M S K` | RFC 1321's own names. The file exists to be checked line by line against the standard. |
| `src/Utils/SHA256.cpp` | `K w a..h` | FIPS 180-4, same reason. |
| `include/Utils/NXTypes.h` | `u8`..`s64` | libnx's spellings. On a Switch this header *is* `<switch.h>`. |
| `Trainer::TID` / `SID` | `TID`, `SID`, `TID16`, `SID16`, `ID32` | PKHeX's names, and every offset in this layer is cross-checked against PKHeX by name. |

Two names that belong to somebody else are also left alone wherever they appear:
`FsSaveDataInfo::uid` and `HidTouchState::x/y` are libnx's fields, not PKSE's.

## Code conventions

- **C++20, namespaced by domain** — `Trainer::`, `Pokemon::`, `Save::`, `Encryption::`, `UI::`,
  `Enums::`, `Utils::`, `Names::`, `Legality::`, `Conversion::`. Headers in `include/` mirror the
  `src/` layout.
- **RTTI and exceptions are disabled** (`-fno-rtti`, `-fno-exceptions`). No `dynamic_cast`, no
  `throw`. Type dispatch is `virtual GameVersion getGameGroup()` plus the `GameVersion` enum, then
  `static_cast` (chapter 04).
- **`Pokemon` and `Trainer` are move-only.** They delete copy and allow move, because they hold large
  buffers and `unique_ptr`s. Pass by reference or move; never copy.
- **Logging goes through `Utils::logInfoToFile` / `logErrorToFile`**, never `printf` — there is no
  console — and user actions through `Utils::logEventToFile` (`[EVENT]`). Every sink is gated on
  `g_debugLogging`. The rules for both, including guarding `describePokemon` call sites, are in
  chapter 03.
- **A new source directory must be added to `SOURCES` in the `Makefile`.** It globs each listed
  directory, not recursively, so a file in an unlisted subdirectory is silently not compiled (and
  the failure shows up as an undefined symbol at link, far from the cause).
- **IntelliSense config is committed**: `.vscode/` (VS Code) and `CppProperties.json` (Visual Studio,
  via **File > Open > Folder**, not the `.sln`). Both hardcode a native devkitPro path —
  `/opt/devkitpro` as committed; a Windows checkout needs `C:/devkitPro` — and never
  `${env:DEVKITPRO}`, which the Windows installer sets to the MSys2-only path `/opt/devkitpro`. Both
  also hardcode a copy of the version string, because they never run the Makefile; bump them with
  it (chapter 02).
- **`PKSE.sln` / `PKSE.vcxproj` are vestigial for IntelliSense** — MSVC-mode IntelliSense cannot
  parse devkitA64's libstdc++. They are kept only as the one project pair.
- **Generated tables are never hand-edited** — rerun the generator (chapter 07).


## Sizes and counts

Record sizes are bytes. "Loose file" means opened through the file browser (chapter 05); every
other row is an installed Switch title.

| Game | Group | Boxes | Slots/box | Grid | Stored | Party | Save file |
|---|---|---|---|---|---|---|---|
| Red/Blue/Yellow | `RBY` | 12 (JP 8) | 20 (JP 30) | 4×5 (JP 6×5) | 33 | 44 | loose file |
| Gold/Silver/Crystal | `GSC` | 14 (JP 9) | 20 (JP 30) | 4×5 (JP 6×5) | 32 | 48 | loose file |
| Ruby/Sapphire/Emerald | `RSE` | 14 | 30 | 6×5 | 80 | 100 | loose file |
| FireRed/LeafGreen | `FRLG` | 14 | 30 | 6×5 | 80 | 100 | the 128 KiB `*.sav` in the title's save (or a loose file) |
| Diamond/Pearl | `DP` | 18 | 30 | 6×5 | 136 | 236 | loose file |
| Platinum | `PT` | 18 | 30 | 6×5 | 136 | 236 | loose file |
| HeartGold/SoulSilver | `HGSS` | 18 | 30 | 6×5 | 136 | 236 | loose file |
| Black/White | `BW` | 24 | 30 | 6×5 | 136 | 220 | loose file |
| Black 2/White 2 | `B2W2` | 24 | 30 | 6×5 | 136 | 220 | loose file |
| X/Y | `XY` | 31 | 30 | 6×5 | 232 | 260 | loose file (`main`) |
| Omega Ruby/Alpha Sapphire | `ORAS` | 31 | 30 | 6×5 | 232 | 260 | loose file (`main`) |
| Sun/Moon | `SM` | 32 | 30 | 6×5 | 232 | 260 | loose file (`main`) |
| Ultra Sun/Ultra Moon | `USUM` | 32 | 30 | 6×5 | 232 | 260 | loose file (`main`) |
| Let's Go | `GG` | 40 | **25** | 5×5 | 260* | 260* | `savedata.bin` |
| Sword/Shield | `SWSH` | 32 | 30 | 6×5 | 328 | 344 | `main` |
| BDSP | `BDSP` | 40 | 30 | 6×5 | 328 | 344 | `SaveData.bin` + `Backup.bin` |
| Legends: Arceus | `PLA` | 32 | 30 | 6×5 | 360 | 376 | `main` |
| Scarlet/Violet | `SV` | 32 | 30 | 6×5 | 328 | 344 | `main` |
| Legends: Z-A | `ZA` | 32 | 30 | 6×5 | 328 | 344 | `main` |
| **The bank** | — | **200** (`BANK_BOX_COUNT`) | 30 | 6×5 | — | — | `bank/bank.dat` |

Gen 1 and Gen 2 records are held as a single-entry **PokeList**, not a bare body: 69/59 bytes
(international/Japanese) for Gen 1, 73/63 for Gen 2 — header + party body + two name fields.

\* PB7 records are **always 260 bytes** (`SIZE_PARTY7_LGPE`, `0x104`) whether stored or party — unlike
every other format, which has two sizes. (`SIZE_STORED7_LGPE` = `0xE8` = 232 exists as the header +
blocks figure but is not the record size PKSE holds.)

Party is always ≤ 6 (`Trainer::MAX_PARTY_SLOTS`). `Trainer::BOX_SLOTS` is 30 — the **array width**,
not necessarily the game's slot count; use `getSlotsPerBox()`.

## Value ranges

| Field | Range | Notes |
|---|---|---|
| IV | 0–31 | Gen 1/2: 0–15 (DV). Ask `maxIV()`. |
| EV | 0–252 each, 510 total | Let's Go: none. Gen 1/2: Stat Experience, 0–65535, no total. Ask `maxEV()`. |
| AV | 0–200 | Let's Go only. `g_allowIllegalEdits` lifts to 255. |
| Nature | 0–24 | |
| Level | 1–100 | |
| Friendship | 0–255 | |
| Ball | 0–37 | 0 = none, 37 = Origin Ball |
| Ability id | 0–310 | through Gen 9 |
| Language | 0–11 | 6 is unused; 11 is Latin American Spanish (Z-A only). Clamp with `safeLanguageForGroup` when moving down |
| Origin version | 0–52 | see `getOriginGameName` |
| Species | 0–1025 | 0 = empty |
| Move id | 0–920 | 920 = Z-A's NihilLight |
| Money | ≤ 9,999,999 | Gen 3: ≤ 999,999. Ask `getMaxMoney()`. |
| Trainer name | ≤ 12 chars | Gen 3: 7. Ask `getMaxTrainerNameLength()`. |
| Digits in a trainer name | ≤ 5 | Gen 3: unlimited. Ask `getMaxTrainerNameDigits()`. |
| Nickname | ≤ 12 chars | Gen 3: 10. Gen 1: 10 (JP 5). Ask `getMaxNicknameLength()`. |

## Key symbols, by task

### "I need to know what game this is"

```cpp
Enums::getGameVersion(titleId)            // title ID -> game
Enums::getGameGroup(version)              // game -> save-format group
trainer.getGameGroup()                    // the open save's group
pokemon.getGameGroup()                    // the ENTITY's storage format group
pokemon.originGame()                      // the version byte the mon CARRIES
Enums::getVersionGeneration(byte)         // 1-9, or 0 = unknown
Enums::getOriginGameName(byte)            // Gen 1-9, including games PKSE can't edit
```

### "I need to read or write bytes"

```cpp
Utils::readUInt16LittleEndian(const uint8_t*)
Utils::readUInt32LittleEndian(const uint8_t*)
Utils::readUInt64LittleEndian(const uint8_t*)
Utils::writeUInt16LittleEndian(uint8_t*, uint16_t)
Utils::writeUInt32LittleEndian(uint8_t*, uint32_t)
pokemon.getData()                         // span<byte>; caller must refreshChecksum()
pokemon.getDataSize()
```

### "I need a name"

```cpp
Trainer::getSpeciesName(id)               // "Unknown" sentinel if out of range
Trainer::getNatureName(id)
Trainer::getAbilityName(id)
Names::getMoveName(id)
Names::getItemNameFor(group, id)          // ALWAYS this, never getItemName directly
Names::getFormName(species, form)
Names::getMetLocationName(version, id)
Names::getTMMove(group, itemId)
Enums::getBallName(id)
Enums::getLanguageName(id)
```

### "I need species data"

```cpp
Pokemon::getPersonalRecord(group, species, form)  // THE per-species lookup: stats, types, abilities,
                                                  // gender ratio, friendship, growth, catch, forms.
                                                  // One table per group; ask the predicates in
                                                  // PersonalRecord.h for what a generation HAS.
Pokemon::getPersonalInfo(species, form)   // the CROSS-GAME index only: presence, height/weight, formCount
Pokemon::getAbilitySlots(species, form, group)
Pokemon::isLearnable(species, form, group, moveId)
Pokemon::getLearnableBits(species, form, group)      // nullptr = NO DATA, not "knows nothing"
Pokemon::getGrowthRate(species)
Pokemon::getLevelFromExp(exp, growthRate)
Pokemon::getExpForLevel(level, growthRate)
Pokemon::getPokemonTypes(species, form, group)
Pokemon::getPreEvolutionChain(species, form, …)
Pokemon::isBattleOnlyForm(species, form)
Pokemon::getFormSpriteId(species, form)   // variant families
Pokemon::getFormSpriteName(species, form) // peer families; "" otherwise
```

### "I need to draw"

```cpp
framebuffer.drawFilledRoundedRect(rectX, rectY, rectWidth, rectHeight, cornerRadius, color)
framebuffer.drawPill(pillX, pillY, pillWidth, pillHeight, color)
framebuffer.drawText(textX, textY, text, color, style)     // textY is the TOP of the line box
framebuffer.drawSymbol(symbolX, symbolY, symbol, color, style)
framebuffer.measureText(text, outWidth, outHeight, style)
framebuffer.lineHeight(style)
framebuffer.textYCenteredOn(centerY, style)                // centre a label on its capitals
framebuffer.drawSoftShadow(shadowX, shadowY, shadowWidth, shadowHeight, cornerRadius)
framebuffer.setClipRect(clipX, clipY, clipWidth, clipHeight) / framebuffer.clearClip()
drawDialogFrame(framebuffer, dialogX, dialogY, dialogWidth, …)   // returns the content Y
drawGlyphButton(framebuffer, boxX, boxY, boxWidth, boxHeight, …)
drawHomeTile(framebuffer, homeTileX, homeTileY, homeTileWidth, …)
drawTitleBar(framebuffer, subtitle)  /  drawNavBar(framebuffer, hint)
drawScrollbar(framebuffer, trackX, trackY, trackH, contentH, …)
wrapTextToWidth(framebuffer, text, …)                      // measured, never a character budget
boxGridColumns(slotsPerBox)  /  boxGridCenterOffset(cols, colPitch)
```

### "I need a sprite"

```cpp
UI::SpriteManager::getSprite(species, form, shiny)      // full render
UI::SpriteManager::getIconSprite(species, form, shiny)  // box icon
UI::SpriteManager::getTypeSprite(typeId)                // 19 icons, Stellar included
UI::SpriteManager::getOriginMarkSprite(mark, color)     // tinted per colour
```

### "I need to tell the user something"

```cpp
screen.postStatus("message", frames);     // the ONLY user-visible channel
Utils::logInfoToFile(...) / logErrorToFile(...)
Utils::logEventToFile(line)               // guard with if (g_debugLogging)
Utils::describePokemon(pokemon, save) / briefPokemon(pokemon)
Utils::logField(key, value) / logSlot(key, pane, box, slot) / itemName(itemId, group)   // chapter 03
```

### "I need text input"

```cpp
Utils::promptText(header, guide, initial, maxChars[, textChanged])  // update() only; blocks
Utils::promptNumber(header, initial, min, max)
Utils::promptSystemText(header, guide, initial, maxChars)  // swkbd: CJK input; one per frame
search.promptForQuery(listName, ListSearch::FilterTiming::WhileTyping, queryChanged)
Utils::utf16ToUtf8(u16) / utf8ToUtf16(u8)
pokemon.canStoreNickname(u16) / trainer.canStoreBoxName(u8)
```

## File map

### Entry / runtime

| File | Role |
|---|---|
| `src/main.cpp` | init order, service lifecycle |
| `include/Globals.h` | version string, runtime setting flags, `BASE_SAVE_DIRECTORY` |
| `src/UI/UI.cpp` | `UIManager` — the screen sequence and per-screen frame loops |
| `src/Utils/Settings.cpp` | `settings.cfg` load/save |
| `src/Utils/Logger.cpp` | all log sinks, all gated on `g_debugLogging` |

### Save layer

| File | Role |
|---|---|
| `include/Enums/GameVersion.h` | **the dispatch hub** — title IDs, groups, names, origin bytes |
| `src/Save/GetSaveFileContents.cpp` | per-game read/write; `TrainerVariant` |
| `src/Save/Block.cpp` | SCBlock parse/serialise |
| `include/Save/TitleFolder.h` | a title's display label, display name and backup folder name |
| `src/Trainer/Trainer*.cpp` | one per group (19) |
| `src/Encryption/Encryption*.cpp` | one per group (none for Gen 1 or Gen 2) |
| `src/Utils/FileUtilities.cpp` | backups, save-device mount, restore-to-title |

### Entity layer

| File | Role |
|---|---|
| `include/Pokemon/Pokemon.h` | the abstract base — ~190 virtuals, 57 of them pure, all documented |
| `src/Pokemon/Pokemon*.cpp` | one per group (19) |
| `include/Pokemon/FormInfo.h` | `isBattleOnlyForm` and friends |
| `include/Utils/Gen1Text.h`, `Gen3Text.h` | legacy charsets — **one per generation, in one place** |

### Data (generated — do not hand-edit)

| File | Generator |
|---|---|
| `src/Pokemon/PersonalInfo1RBY.cpp` … `PersonalInfo9LZA.cpp` (19) | `tools/gen_personaltables.py` |
| `src/Pokemon/PersonalInfoTable.cpp` | `tools/gen_personal.py` |
| `src/Pokemon/LearnsetTable.cpp` | `tools/gen_learnsets.py` |
| `src/Pokemon/Gen1Tables.cpp` | `tools/gen_gen1.py` |
| `src/Pokemon/EvolutionTable.cpp` | `tools/gen_evolutions.py` |
| `src/Legality/EncounterTable.cpp` | `tools/gen_encounters.py` |
| `src/Names/LocationNames.cpp` | `tools/gen_locations.py` |
| `src/Names/ItemPouches.cpp` | `tools/gen_itempouches.py` |
| `src/Names/ItemPresence.cpp` | `tools/gen_itempresence.py` |
| `src/Names/MoveInfo.cpp` | `tools/gen_moveinfo.py` |
| `src/Names/MovePresence.cpp` | `tools/gen_movepresence.py` |
| `src/Names/MoveNames.cpp`, `SpeciesNames.cpp` | `gen_movenames.py`, `gen_speciesnames.py` |
| `src/Names/ItemNames.cpp` | `gen_itemnames.py` |
| `src/Names/AbilityNames.cpp`, `NatureNames.cpp`, `TypeNames.cpp` | `gen_simplenames.py` |
| `src/Names/FormNamesLocalized.cpp` | `gen_formnames.py` |
| `src/Names/RibbonNames.cpp`, `RibbonNamesLocalized.cpp` | `gen_ribbonnames.py` |
| `include/Trainer/Blocks5BW.h` … `Blocks7USUM.h` (6) | `gen_blocktables.py` |
| `include/Utils/Gen2Text.h`, `Gen4Text.h` | `gen_gen2text.py`, `gen_gen4text.py` |
| `src/Pokemon/DexTable8SWSH.cpp`, `DexTable9SV.cpp`, `DexTable8LA.cpp` | `gen_dextable8swsh.py`, `gen_dextable9sv.py`, `gen_dextable8la.py` |
| `src/Pokemon/DexTable6XY.cpp`, `DexTable6ORAS.cpp`, `DexTable7SM.cpp`, `DexTable7USUM.cpp`, `DexTable7LGPE.cpp` | `gen_dexformtables.py` |
| `romfs/sprites/pokemon_hd/` | `tools/gen_hdsprites.py` |
| `romfs/sprites/types/` | `tools/gen_typeicons.py` |
| `romfs/sprites/marks/` | `tools/gen_marks.py` |
| `romfs/sprites/ribbons/` | `tools/gen_ribbons.py` |
| `romfs/fonts/` | `tools/gen_fonts.py` |

### UI

| File | Role |
|---|---|
| `src/UI/PKSEFramebuffer.cpp` | **the only file touching SDL / GL / NanoVG** |
| `include/UI/Common.h` | colours, theme, box geometry, `TouchButton` |
| `include/UI/ScreenChrome.h` | title bar, nav bar, badges, tiles, scrollbar |
| `src/UI/TrainerViewScreen.cpp` | the editor — state, input, draw dispatch (~7200 lines) |
| `src/UI/Panels/*.cpp` | content regions |
| `src/UI/Dialogs/*.cpp` | modal cards |
| `src/UI/Modals/PokemonDetailsModal.cpp` | the full-screen entity editor |
| `src/UI/SpriteManager.cpp` | LRU sprite cache over ROMFS |
| `src/Pokemon/FormSpriteMapping.cpp` | form → PokeAPI id / name stem (hand-maintained) |

### Features

| File | Role |
|---|---|
| `src/Trainer/Bank.cpp` | the cross-game bank |
| `src/Conversion/Convert.cpp` | cross-generation entity conversion |
| `src/Legality/Legality.cpp` | Layers 1 + 2 |
| `src/Legality/EncounterMatch.cpp` | Layer 3 |

## Commands

```bash
# Build
make                                   # -> PKSE.nro (fails if a romfs asset is missing)
python tools/gen_fonts.py             # the 3 UI fonts
python tools/gen_typeicons.py         # the 19 type icons
python tools/gen_marks.py             # the origin markings
python tools/gen_ribbons.py           # the ribbon and mark images
python tools/gen_hdsprites.py         # the 3260 sprites (needs Pillow)

# Verify, in cost order
python tools/syntax_check.py                       # ~3s   does it compile?
python tools/syntax_check.py --warnings            # ~1min does the optimizer object?
make                                               # the only proof the .nro links

# Regenerate a data table (then REVIEW THE DIFF)
python tools/gen_personal.py
python tools/regenerate.py --tables                # every table generator; lists changed files
PKHEX_REF=<sha> python tools/gen_learnsets.py      # reproduce the tables at an older PKHeX commit
PKHEX_LOCAL=/path/to/PKHeX.Core python tools/gen_encounters.py
```

## SD-card layout

```
sdmc:/PKSE/
├── settings.cfg
├── bank/bank.dat
├── FileBackups/<stamp>_<name>        a loose save's original bytes, copied before each
│                                      write back (Save::backupOriginalFile)
├── logs/debug_YYYY-MM-DD.log          only when debug logging is ON
├── trace.log                          only when debug logging is ON
└── Pokemon <Game> <TITLEID>/          one per installed title (Save::titleFolderName);
                                       the full title id, always
    ├── Working/                       reused when auto-backup is OFF
    └── <timestamp>/                   a backup — THIS IS THE WORKING FILE
```

## External references

| Thing | Where |
|---|---|
| **PKHeX** | <https://github.com/kwsch/PKHeX> — the source of truth for every offset and algorithm |
| PKHeX ref the generators read | `_DEFAULT_REF` in `tools/pkhex_source.py` — `master`, resolved to a SHA once per run; `PKHEX_REF` overrides |
| PokeAPI sprites | <https://github.com/PokeAPI/sprites> (HOME renders, pinned) |
| PokeAPI form lookup | `https://pokeapi.co/api/v2/pokemon/<id>/` — **check the returned `name`** |
| PKSM | the other on-console editor; PKSE reads its `.bnk` bank (chapter 10) |
| devkitPro | <https://devkitpro.org/> |
| libnx | <https://github.com/switchbrew/libnx> |
| NanoVG | vendored in `nanovg/` |

## Where to look when

| Symptom | Start at |
|---|---|
| Wrong value read from a save | the game's `parseXBlock()`, then PKHeX's `SAV*` class |
| Edit does not persist | is `update*Block()` called, and **before** the checksum pass? |
| Bad Egg in-game | checksum not refreshed; or zeros written for an empty slot |
| Stats disagree with IVs/EVs | `recalculateStats()` missing, or raw IVs used instead of `effectiveIV` |
| Wrong sprite | `FormSpriteMapping` — verify the id against PokeAPI |
| Wrong name | which table? `getItemNameFor`, `locationTableVersion` |
| Legality false positive | is a lookup returning "no data" being read as "no"? |
| UI control does nothing | drawn but not handled, or the touch id is not mapped into `kDown` |
| Taps land on the wrong thing | overlay draw order, or a missing `touchButtons.clear()` |
| App appears frozen | a modal resolving a target by index while compaction moved it |
| HOME button unresponsive | something is blocking `appletMainLoop` |
| Undefined SDL/EGL symbols at link | `pkg-config` name — must be lowercase `sdl2` |
| Warning only in a full build | run `syntax_check.py --warnings` |
