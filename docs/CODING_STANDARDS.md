# PKSE — Coding Standards

Target: C++20, devkitA64 / libnx, built with `-Wall -fno-rtti -fno-exceptions`.

**This document is binding on every contribution to PKSE, whether a person or an AI tool wrote
it.** It is not advice: every rule in it is a hard requirement unless it says otherwise. Read it in
full before writing or editing any code. Two steps it asks of every change:

- **Before creating ANY new identifier or file, run the discovery greps in [§2](#2-prime-directive-consistency-over-preference)**
  and reuse the name that already exists.
- **Before calling a change done, run [§14 Definition of done](#14-definition-of-done)** — the
  build, the warnings pass, the violation greps and the self-review checklist.

When a rule and existing code disagree, see [§1 Precedence](#1-precedence). The technical manual
(`docs/manual/`) explains how the code works; this document says how it must be written.

---

## 1. Precedence

When sources conflict, obey in this order:

1. An explicit instruction in the current task.
2. This document.
3. Common C++ practice.

You never resolve a conflict by inventing another option. If existing code contradicts this document in a way you cannot follow without a wide rename, keep to the coding standards, finish the task, and state the conflict in your final message.

---

## 2. Prime directive: consistency over preference

**A concept has exactly one name in this codebase. That name does not change when it crosses a file boundary, a layer boundary, or a generation boundary.**

If the party array is `party` in `Trainer.h`, it is `party` in `PartyPokemonPanel.cpp`, in `StoragePanel.cpp`, in `Convert.cpp`, and in every file written after this sentence. Not `partyList`, not `partyMons`, not `pkmList`, not `mons`.

### 2.1 Grep before you name — mandatory

Before introducing any new identifier (variable, parameter, function, member, constant, type, file), search for the concept first:

```bash
grep -rn "conceptWord" include src --include=*.h --include=*.cpp
grep -rniE "\b(alt1|alt2|alt3)\b" include src        # candidate synonyms you were about to use
```

- A match exists → **use that exact spelling and casing**, including in a file you are creating from scratch.
- No match, and the concept is in the registry in §5 → use the registry spelling.
- No match and not in the registry → coin a name under §3, and add it to the registry table in §5 in the same change.

### 2.2 Naming is not per-file creativity

Every one of these is a defect, reported as such in review, and is not fixed by "the new name reads better":

- The same value called `speciesId` in one file and `species` / `dexNumber` / `natId` in another.
- A parameter named differently from the member it is assigned to.
- A helper doing the same job as an existing helper under a different verb (`fetchX` next to an existing `getX`).
- A constant redefined locally when it already exists in `UI/Common.h`, `Trainer/Trainer.h`, or `Trainer/Bank.h`.

---

## 3. Naming rules

> **Scope.** These rules govern every name this project introduces, including names carried over from PKHeX. The only exemptions are symbols owned by a library or the platform — libnx/devkitPro, SDL2, nanovg, stb, the C++ standard library, platform macros, the save format's own vocabulary, and any external library added later. See §3.7.

### 3.1 Casing table

| Element | Convention | Example (from this codebase) |
|---|---|---|
| Directory | `PascalCase` | `include/UI/Panels/` |
| File | `PascalCase`, matches the primary type/module | `BoxPokemonPanel.h`, `Encryption9SV.cpp` |
| Namespace | `PascalCase`, matches its directory | `UI`, `UI::Panels`, `Trainer`, `Names` |
| Class / struct | `PascalCase` | `PKSEFramebuffer`, `Bank`, `Trainer8SWSH` |
| `enum class` type | `PascalCase` | `ViewMode`, `SCTypeCode`, `PouchType9SV` |
| Enumerator | `PascalCase` | `ViewMode::Party` |
| Free function | `camelCase` | `getSpeciesName`, `drawPartyPokemon` |
| Member function | `camelCase` | `boxCount`, `hasChanged`, `setPID` |
| Parameter / local | `camelCase` | `selectedIndex`, `formId`, `boxIndex` |
| Data member | `camelCase`, **no prefix** | `party`, `dataSize`, `buffer` |
| Mutable global | `g_` prefix, then `camelCase` | `g_debugLogging`, `g_moveWarn` |
| Namespace- or class-scope constant | `SCREAMING_SNAKE_CASE` | `MAX_PARTY_SLOTS`, `BANK_BOX_COUNT` |
| Function-local `constexpr` | `camelCase` (it is a local) | `constexpr int slotGap = 12;` |
| Macro | `SCREAMING_SNAKE_CASE`, avoid entirely | `LOG_BUFFER_SIZE` |
| Template parameter | `PascalCase` | `template <typename PokemonType>` |

**`g_` IS THE ONLY PREFIX THIS PROJECT USES, and it belongs to mutable globals alone.** A data
member never carries one: not `m_`, not `_`, not a trailing underscore. This rule **overrides
[§1](#1-precedence)'s "existing code"** — it was applied to the whole tree at once, so there is
no prefixed member left to be consistent with, and reintroducing one is a defect rather than a
matter of local style. Where stripping the prefix would collide with an accessor of the same name
(`layout()`, `securityKey()`, `fileName()`), the member takes its subject instead:
`saveLayout`, `saveSecurityKey`, `saveFileName`.

**Banned constant styles.** Do not write `kEditRows`, `kNavBarH`, `kRows`, `k`-anything. That prefix exists in a few UI files; it is legacy, it is not the standard, and no new occurrence may be added. A file-scope or class-scope constant is `SCREAMING_SNAKE_CASE`; a function-local one is a plain `camelCase` local.

### 3.2 No abbreviations

**Write the whole word.** If a reader has to expand it in their head, it is banned.

Mandatory replacements — these are the actual truncations present in or likely to enter this codebase:

| Banned | Required |
|---|---|
| `fb` | `framebuffer` |
| `w`, `h` | the measured subject: `panelWidth`, `slotHeight` — never bare `width`/`height`, see §3.4 |
| `idx`, `i` (outside a loop header) | `index`, or a specific name: `slotIndex`, `boxIndex` |
| `buf` | `buffer` |
| `cfg`, `conf` | `settings` |
| `tmp`, `temp` | name what it holds: `decryptedBlock`, `scratchBuffer` |
| `val` | `value`, or the specific quantity |
| `len` | `length` |
| `cnt`, `num` | `count` |
| `msg` | `message` |
| `str` | `text` (or the specific string: `nickname`, `trainerName`) |
| `btn` | `button` |
| `pos` | `position` |
| `col` | `column` (never for colour) |
| `clr`, `colour` | `color` (this codebase uses American spelling) |
| `dst`, `dest` | `destination` |
| `src` (as an identifier) | `source` |
| `ret`, `res` | `result` |
| `ptr` | drop it, or name the pointee: `pokemon`, `activeTrainer` |
| `mgr` | `manager` |
| `ctx` | `context` |
| `attr` | `attribute` |
| `elem` | `element` |
| `prev`, `nxt` | `previous`, `next` |
| `calc` | `calculate` |
| `init` (as a variable) | `initial…`; `initialize()` is fine as a verb |
| `impl` | spell it, or use the existing pattern in the file |
| `pkm`, `mon`, `poke` | `pokemon` |
| `gen` (as an identifier) | `generation` |
| `hH`, `headerH`, `rowH`, `colW`, `lineH`, `slotR` | `headerHeight`, `rowHeight`, `columnWidth`, `lineHeight`, `slotCornerRadius` |

**Permitted acronyms** — domain and platform terms only, kept in their canonical casing:

`PID`, `TID`, `SID`, `OT`, `IV`, `EV`, `AV`, `EXP`, `HP`, `PP`, `TM`, `TR`, `HM`,
`UI`, `RGBA`, `MD5`, `SHA256`, `CRC`, `RNG`, `SD`, `NRO`, `ROM`, `PKSE`,
and the game codes in §5.3.

Acronyms owned by a library or the platform are not on this list and do not need to be — they are out of scope entirely (§3.7). Acronyms arriving from PKHeX are not exempt: they are spelled out unless they appear above.

`Id` is **not** an acronym here: write `speciesId`, `formId`, `itemId`, `titleId`, `blockId` — the dominant spelling in this codebase (600+ `formId` vs 9 `formID`). The all-caps `speciesID()` / `formID()` accessors on `Pokemon` are grandfathered public API: **call them by their existing name, never rename them opportunistically, and never copy that casing into a new identifier.** `PID`, `TID`, `SID` and `OT` stay all-caps because they are acronyms in the source material, not the word "identifier".

### 3.3 Single-letter names

Banned, with this **complete and closed** exception list:

| Allowed | Only where |
|---|---|
| `i`, `j`, `k` | Loop counter in a `for` header whose body is short and whose meaning is purely "position in this container". Nested loops use `i`, `j`, `k` outermost-to-innermost. |
| `x`, `y` | Screen coordinates in the drawing layer only (`UI/`), as parameters or immediate locals of a draw call. Never for anything that is not a pixel coordinate. |
| `r`, `g`, `b`, `a` | The channel members of `UI::Color` only. |

Everything else is spelled out. Specifically banned as declarations: `w`, `h`, `n`, `s`, `t`, `c`, `p`, `e`, `f`, `d`, `N`, `NT`, `T`, `R`.

Two more constraints on the loop exception:

- The moment a loop body is longer than ~15 lines, or the index is passed into another function, or the loop iterates a domain collection, use the domain name instead: `slotIndex`, `boxIndex`, `partyIndex`, `moveIndex`, `pouchIndex`.
- Prefer a range-for with a named element: `for (const auto &pokemon : party)` beats indexing.

```cpp
// WRONG
for (int i = 0; i < 6; i++) {
    const int col = (i >= 3) ? 1 : 0;
    const int w = (width - 3 * gutter) / 2;
    drawSlot(fb, colX[col], i, w);
}

// RIGHT
for (int slotIndex = 0; slotIndex < static_cast<int>(MAX_PARTY_SLOTS); slotIndex++)
{
    const int columnIndex = (slotIndex >= PARTY_SLOTS_PER_COLUMN) ? 1 : 0;
    const int columnWidth = (partyPanelWidth - 3 * columnGutter) / 2;
    drawPartySlot(framebuffer, columnLeft[columnIndex], slotIndex, columnWidth);
}
```

### 3.4 A name states its subject, not just its measurement

Spelling a word out in full is only half the job. **`width` is not a name — it is a unit.** A name must say *what thing* is being measured, counted, indexed, or held, so that a reader who opens the file at a random line knows what the value is without scrolling up and without a comment.

**Form: `<subject><Qualifier><Measurement>`.** Subject first, measurement last, always in that order.

| Not a name (unit or role only) | A name |
|---|---|
| `width`, `height` | `screenWidth`, `panelHeight`, `slotCardWidth`, `spriteHeight`, `dialogWidth` |
| `x`, `y` (outside §3.3) | `cardLeft`, `slotTop`, `cursorX` inside a draw call |
| `top`, `left`, `right`, `bottom` | `gridTop`, `panelLeft`, `contentRight` |
| `padding`, `margin`, `gap` | `cardPadding`, `columnGutter`, `slotGap` |
| `radius` | `slotCornerRadius`, `avatarCornerRadius` |
| `count` | `boxCount`, `selectedSlotCount`, `legalMoveCount` |
| `index` | `slotIndex`, `boxIndex`, `pouchIndex` |
| `size` | `recordSize`, `bankFileSize`, `pokemonDataSize` |
| `offset` | `partyBlockOffset`, `nicknameOffset` |
| `name` | `speciesName`, `trainerName`, `boxName` |
| `color` | `borderColor`, `titleColor`, `cursorColor` |
| `data`, `bytes` | `decryptedPokemonBytes`, `rawSaveBytes` |
| `result` | `conversionResult`, `legalityResult` |
| `value` | `effortValue`, `individualValue`, `enteredValue` |
| `total` | `totalBoxCount`, `totalItemWeight` |
| `text` | `nicknameText`, `errorText` |
| `list`, `items`, `entries` | `bankSlots`, `pouchItems`, `learnableMoves` |
| `flag`, `enabled` | `shinyFlag`, `hdSpritesEnabled` |

**Three tests. A name must pass all three.**

1. **Isolation.** Cover the rest of the function. Does the name alone tell you what it is? `const int h = 48;` fails. `const int rowHeight = 48;` passes.
2. **Second-of-a-kind.** If a second value of the same unit ever enters this scope, does the name still distinguish it? `width` dies the instant a panel and a card are both on screen. `panelWidth` and `cardWidth` survive.
3. **Comment.** If you find yourself writing a trailing comment to say what the value is, the name is wrong. Rename it and delete the comment. Comments explain *why*; names explain *what*.

**Qualify by state as well as by subject.** When the same subject has two values that differ by condition, the difference goes in the name, not in a comment:

`availableCardWidth` / `totalPanelWidth` · `currentBoxIndex` / `targetBoxIndex` · `selectedSlotIndex` / `hoveredSlotIndex` · `originalPID` / `regeneratedPID` · `sourceGameVersion` / `destinationGameVersion`

**The enclosing scope supplies context — do not repeat it.** Inside `class StoragePanel`, a member is `panelWidth`, not `storagePanelWidth`. Inside `Bank`, the accessor is `boxCount()`, not `bankBoxCount()`. Inside a 200-line function, a local carries the context anyway, because the scope is too big to hold in your head.

**Specific does not mean long.** These are equally wrong, in the other direction:

- Restating the type: `pokemonObject`, `speciesIdInt`, `boxNameString`, `slotVector`.
- Restating the layer: `uiPanelWidthPixels` inside `UI/` (that layer is pixels by default — see §3.6).
- Restating the file: `storagePanelStorageBoxIndex`.
- Stacking hedges: `theCurrentlySelectedPokemonSlotIndexValue`.

Aim for the shortest name that passes all three tests — in practice two to four words, under about 30 characters. Reach for a more precise word before reaching for another word.

```cpp
// WRONG — every value is a unit with no subject, so the block needs comments to be read at all
const int w = width - 32;          // card width
const int h = 96;                  // each slot
const int p = 14;                  // inner padding
const int n = party.size();        // how many are filled
for (int i = 0; i < 6; i++) { … }

// RIGHT — no comments needed, and a second width could be added tomorrow without ambiguity
const int cardWidth = panelWidth - 2 * panelPadding;
const int slotHeight = 96;
const int cardPadding = 14;
const int occupiedSlotCount = static_cast<int>(party.size());
for (int slotIndex = 0; slotIndex < static_cast<int>(MAX_PARTY_SLOTS); slotIndex++) { … }
```

This rule binds function parameters too. `drawCard(framebuffer, x, y, width, height, radius)` is acceptable only in the framebuffer primitives, where the subject genuinely is "whatever rectangle you passed". Every caller above that layer names what it is drawing: `drawCard(framebuffer, cardLeft, cardTop, cardWidth, cardHeight, slotCornerRadius)`.

### 3.5 Verbs and shapes

| Kind | Form | Example |
|---|---|---|
| Class accessor (no side effects) | noun, no `get` | `boxCount()`, `slotsPerBox()`, `nickname()` |
| Class mutator | `set<Noun>` | `setPID()`, `setNickname()` |
| Free lookup over static tables | `get<Noun>` | `getSpeciesName()`, `getItemName()` |
| Boolean query | `is` / `has` / `can` / `should` | `hasChanged()`, `isShiny()`, `canLearnMove()` |
| Action | imperative verb | `load()`, `save()`, `deposit()`, `withdraw()` |
| Draw routine | `draw<Thing>` | `drawPartyPokemon()`, `drawTypeIcon()` |
| Input/update routine | `update()`, `handle<Event>` | `update()`, `handleTouch()` |
| Conversion | `to<Format>` / `convert<A>To<B>` | `toRGBA8()`, `convertPokemonToGen9()` |

A function returning `bool` for success/failure is named for the action (`save()`), not the outcome. A function returning a fact is named for the fact (`hasChanged()`).

### 3.6 Suffixes carry meaning

Use these consistently; do not mix them:

- `…Id` — a value identifying a thing in game data (`speciesId`).
- `…Index` — a zero-based position in a container or grid (`slotIndex`).
- `…Count` — a quantity (`boxCount`). Never `numBoxes`, never `boxesTotal`.
- `…Offset` — a byte offset into save data (`partyBlockOffset`).
- `…Size` — a size in bytes (`dataSize`). Sizes of things that are not bytes get an explicit unit: `…SizeInSlots`.
- `…Name` — a display string (`speciesName`).
- `…Bytes` — a raw byte span/array (`encryptedBytes`).

Pixel values need no suffix inside `UI/` (that layer is pixels by default). Any non-pixel unit in `UI/` is suffixed: `…Milliseconds`, `…Percent`.

### 3.7 Out of scope: library and platform names

**Sections 3.1–3.6 apply to every name this project introduces.** The exemption is narrow: a symbol *owned by a library or the platform* is used exactly as its owner spells it — abbreviated, single-lettered, snake_cased, or otherwise. Never "correct" such a name to match this document, and never wrap a foreign API in a shim whose only purpose is to rename it.

Exempt, in full — this list is closed:

| Source | Examples | Rule |
|---|---|---|
| libnx / devkitPro | `PadState`, `padGetButtonsDown`, `HidNpadButton_A`, `Result`, `R_SUCCEEDED`, `FsFileSystem`, `AccountUid` | Verbatim. |
| SDL2 | `SDL_Init`, `SDL_Event`, `SDL_Rect` | Verbatim. |
| nanovg | `NVGcontext`, `nvgBeginPath`, `NVG_ALIGN_LEFT` | Verbatim. `nanovg/` is vendored and exempt from every rule here, including formatting. |
| stb | `stbi_load`, `stbi_uc`, `stbi_image_free` | Verbatim. `include/Libs/stb_image.h` is vendored and exempt. |
| C++ standard library | `size_t`, `npos`, `value_type`, `begin`, `end`, `operator==`, `first`/`second` | Verbatim, including customization points you must name exactly to satisfy the language. |
| Platform macros / paths | `__SWITCH__`, `DEVKITPRO`, `ROMFS`, `sdmc:/` | Verbatim. |
| The save format's own vocabulary | `PID`, `EC`, `IV`, `EV`, `TID`, `SID`, `OT` | Verbatim — these are the games' terms, not PKHeX's identifiers. Already permitted under §3.2. |
| Shipped on-disk layouts | Bank record header fields, config keys already written to SD | Verbatim — renaming a serialized key is a format break, not a rename. |
| Any library added later | — | Same treatment, once it is actually a dependency. |

**PKHeX is explicitly NOT on that list.**

PKHeX is this project's source of truth for **logic** — offsets, checksum algorithms, legality rules, table contents. It is not a source of truth for **naming**, and PKSE is not a port of its codebase. Every identifier that arrives here from PKHeX gets a PKSE name under §3.1–3.6, whether it was transcribed by hand or emitted by a script in `tools/`.

| PKHeX spelling | In this codebase |
|---|---|
| `PersonalInfo` / `PersonalTable` | `PersonalInfoTable` |
| `Legal` | `Legality` |
| `EvoCriteria` | `evolutionCriteria` (illustrative) |
| `EncounterMatch` | `encounterMatch` (illustrative) |
| `SpeciesConverter` | `SpeciesConverter3`, `SpeciesConverter9` |
| `PKM` / `pkm` | `Pokemon` / `pokemon` |
| `GameVersion.GG` / `PLA` | the §5.3 codes: `7LGPE`, `8LA` |

`PersonalInfoTable`, `Legality`, `SpeciesConverter3` and `Pokemon` are what this codebase already does. Follow that pattern for everything else.

Porting logic from PKHeX therefore means: port the behaviour, rename the identifiers, and keep provenance in a comment rather than in a symbol — `// Algorithm from PKHeX PKHeX.Core/Saves/SAV8SWSH.cs (pinned ref in tools/pkhex_source.py)`. Provenance in prose costs nothing and breaks no rule.

**Generated tables follow this document too.** `tools/gen_*.py` emits C++ from hand-written strings inside the generator, so a conforming name is always achievable. If a generated identifier violates §3, fix the emit string in the generator, re-run it, and commit the regenerated file — never hand-edit the output (§6.6). Two known deviations in already-generated files, to correct in the generator the next time it is touched for another reason, never as drive-by work: the `PKM_PERSONAL_INFO_TABLE_H` include guard (should be `POKEMON_…` under §6.4) and the `PERSONAL_GAME_GG` / `PERSONAL_GAME_PLA` bits (should use the §5.3 codes).

**The Python in `tools/` is Python.** The scripts' own code follows PEP 8 — `snake_case` functions, `gen_*.py` filenames, `PKHEX_REF`. The C++ they emit follows this document. Those are two different things and neither leaks into the other.

**Where the boundary sits.** The *symbol* is theirs; the *identifier you declare* is yours.

```cpp
// Their names, used as-is — correct.
if (R_FAILED(fsOpenSdCardFileSystem(&fileSystem))) { … }
const uint64_t buttonsDown = padGetButtonsDown(&pad);

// A variable YOU declare still follows §3.4, even when its type is theirs.
NVGcontext *nanovgContext = …;   // not `vg`
Result mountResult = …;          // not `rc`
```

Two carve-outs on that, so you are not fighting the platform:

1. **Shape-matching wins.** When your code must mirror a foreign shape — a callback signature, an overridden vendor interface, a designated initializer for their struct — their parameter and field names win outright.
2. **Existing signatures win.** `UIScreen::update(const PadState &pad, const TouchInput &touch)` already exists. Per §1, this document outranks existing code: rename at every implementation and call site.

The §14.2 greps do not know about any of this. A hit on a library or platform symbol (`res` in an SDL call, `ctx`, `buf` in a stb signature) is not a violation — leave it and move on. A hit on a name you introduced, including one carried over from PKHeX, is.

---

## 4. Cross-file identity rules

1. **A parameter takes the name of the thing it becomes.** If it is stored into `member.trainerName`, the parameter is `trainerName`.
2. **An override keeps the base declaration's parameter names**, exactly. `Trainer9SV::loadParty(size_t boxIndex)` does not become `loadParty(size_t box)`.
3. **A per-game class keeps the base class's member and method names**, differing only by the game suffix on the type. If `Trainer8SWSH` exposes `boxNames`, then `Trainer9LZA` exposes `boxNames`, not `boxLabels`.
4. **A local holding the result of a call is named after the call**, unless the context demands a qualifier: `const auto &speciesName = getSpeciesName(speciesId);`.
5. **Moving code does not rename it.** Extracting a block into a helper preserves every identifier inside it.
6. **Never rename an existing symbol as a side effect of another task.** Renames are their own change, done deliberately, on explicit instruction, repo-wide, in one commit.

---

## 5. Canonical vocabulary

Maintained here. When you coin a term that will appear in more than one file, add a row.

### 5.1 Domain terms (exact spelling — do not substitute synonyms)

| Concept | Canonical identifier | Never |
|---|---|---|
| A Pokémon entity | `pokemon` | `mon`, `pkm`, `poke`, `creature` |
| Save-file owner / save wrapper | `trainer` | `player`, `save`, `profile` |
| Party collection | `party` | `partyList`, `team`, `activeParty` |
| Storage box | `box`, `boxIndex`, `boxName` | `pcBox`, `storageBox` |
| Slot inside a box or party | `slotIndex` | `pos`, `cell`, `spot` |
| Cross-game persistent storage | `bank` | `vault`, `home`, `depot` |
| Species identifier | `speciesId` | `dexId`, `natDex`, `species` (bare) |
| Species display name | `speciesName` | `name` (bare) |
| Form identifier | `formId` | `variant`, `altForm` |
| Item identifier / name | `itemId`, `itemName` | `heldId`, `bagItem` |
| Held item | `heldItem` | `item` (bare, when it's the held one) |
| Bag pocket | `pouch`, `pouchId`, `pouchType` | `pocket`, `bagSection` |
| Move identifier / name | `moveId`, `moveName` | `attackId` |
| Ability identifier / name | `abilityId`, `abilityName` | `abilId` |
| Nature identifier / name | `natureId`, `natureName` | — |
| Individual values | `individualValues`, element `individualValue` | `ivs` in new API surface (`IV` acceptable inside a name: `hpIV`) |
| Effort values | `effortValues` | `evs` (bare) in new API surface |
| Personality value | `PID`, `currentPID`, `newPID` | `personality`, `pidValue` |
| Trainer identifiers | `trainerId` (`TID`), `secretId` (`SID`) | `tid`, `sid` lowercase |
| Original trainer | `originalTrainerName` / `OT…` | `otName` in new code |
| Met data | `metLocation`, `metLevel`, `metDate` | `caughtAt` |
| Game version enum value | `gameVersion` | `game`, `title` (title = the Switch title record) |
| Switch title record | `titleId` | `gameId` |
| Save data block | `block`, `blockId`, `blockOffset` | `chunk`, `section` |
| What a program writes around a save file | `envelope` (`Save::SaveEnvelope`, `SaveEnvelopeKind`), its parts `headerBytes` / `footerBytes` | `wrapper`, `trailer` |
| The game's own save inside an envelope | `saveImage` | `payload`, `rawSave`, `innerSave` |
| Unwritten space after a save | `paddingBytes` | `slack`, `filler` (that is a test pattern) |
| A part of a no$gba save file | entry (`NocashSaveEntry`) | `chunk`, `block` (a block is the game's) |
| Legality result | `legalityResult`, `severity` | `check`, `flag` |
| Framebuffer | `framebuffer` | `fb`, `canvas`, `surface` |
| Sprite record | `sprite` | `image`, `tex` |
| Selected item in a view | `selectedIndex` | `cursor`, `highlighted`, `activeIdx` |
| PKSE's on-screen keyboard | `keyboard` (`Dialogs::KeyboardState`); its key under the cursor is `selectedKeyIndex` | `kbd`, `osk`, `swkbd` (that is the console's applet) |
| Text caret | `caretOffset` — a byte offset, always on a character boundary | `cursor`, `caretPos`, `pos` |

### 5.2 Layer terms

| Concept | Canonical | Where it lives |
|---|---|---|
| Full-screen view | `…Screen` | `include/UI/`, `src/UI/` |
| Sub-region of a screen | `…Panel` | `UI/Panels/` |
| Blocking prompt | `…Dialog` | `UI/Dialogs/` |
| Overlay over a screen | `…Modal` | `UI/Modals/` |
| Per-game save reader/writer | `Trainer<Gen><Code>` | `Trainer/` |
| Per-game Pokémon record | `Pokemon<Gen><Code>` | `Pokemon/` |
| Per-game bag | `Inventory<Gen><Code>` | `Trainer/` |
| Per-game crypto | `Encryption<Gen><Code>` | `Encryption/` |
| Static generated table | `<Thing>Table` / `<Thing>Names` | `Pokemon/`, `Names/` |

### 5.3 Game codes — closed set, immutable casing

`1RBY`, `2GSC`, `3RSE`, `3FRLG`, `4DP`, `4PT`, `4HGSS`, `5BW`, `5B2W2`, `6XY`, `6ORAS`, `7SM`, `7USUM`, `7LGPE`, `8SWSH`, `8BDSP`, `8LA`, `9SV`, `9LZA`

Always `<Generation><Code>`, always in that casing, in type names, file names, enum values, and constant names. `Pokemon8SWSH`, never `Pokemon8SwSh`, `PokemonSWSH`, `PokemonSwordShield`, or `PK8`.

**Adding a game code requires explicit instruction.** Do not invent one — not for a new title, not for a test fixture, not for a placeholder.

---

## 6. File and project structure

### 6.1 Directory map — authoritative

```
include/            Public headers. Mirrors src/ exactly.
  Conversion/       Cross-generation Pokémon conversion.
  Encryption/       Per-game encrypt/decrypt/checksum.
  Enums/            Shared enums only. No logic, no tables.
  Legality/         Legality evaluation. Read-only w.r.t. Pokémon data.
  Libs/             Vendored third-party headers. DO NOT EDIT.
  Names/            Generated name/lookup tables (declarations).
  Pokemon/          Pokémon record types + generated per-species tables.
  Save/             Save block model and raw save I/O.
  Test/             The on-console self test. Development build only (`make BUILD_TYPE=development`).
  Trainer/          Trainer/save types, inventory, bank.
  UI/               Rendering, screens, input.
    Dialogs/  Modals/  Panels/
  Utils/            Cross-cutting helpers: logging, files, strings, hashing, settings.
  Globals.h         Process-wide state. Additions require explicit instruction.
src/                Implementations. Same tree as include/. Plus src/main.cpp.
tools/              Python table generators (gen_*.py), the romfs asset fetchers
                    (gen_fonts/typeicons/marks/hdsprites.py + asset_fetch.py),
                    pkhex_source.py, regenerate.py, syntax_check.py.
  formdump/         .NET tool that calls PKHeX's FormConverter and dumps JSON.
  harness/          Host test programs over real saves; uihost/ drives the real UI headless.
nanovg/             Vendored. DO NOT EDIT.
memecrypto/         Vendored (Gen 7 save signing). DO NOT EDIT; re-sync from upstream.
romfs/              Fetched assets (fonts, sprites). Gitignored; never hand-edited.
assets/             Icon, banner, screenshots.
docs/               manual/, integration/, DEV_TESTING.md, this file.
```

**Rules:**

- No new top-level directory, and no new directory under `include/` or `src/`, without explicit instruction.
- `include/X/Foo.h` implies `src/X/Foo.cpp` if there is an implementation. Never `src/Foo.cpp` for `include/UI/Foo.h`.
- A file's namespace matches its directory path: `include/UI/Panels/ItemsPanel.h` → `namespace UI { namespace Panels { … } }`.
- Header-only is permitted for pure tables, enums, and inline constants. If you write a `.cpp`, it goes in the mirrored directory.

### 6.2 Deciding where new code goes

In order — stop at the first match:

1. Does a file already own this concept? Add to it.
2. Is it per-game logic? → the layer directory of its concern, named `<Concept><Gen><Code>`.
3. Is it drawn on screen? → `UI/`, in `Panels/` / `Dialogs/` / `Modals/` by §5.2.
4. Is it generic and depends on no PKSE type? → `Utils/`.
5. None of the above → ask before creating a new file.

**Never** create: `Helpers.cpp`, `Misc.h`, `Utils2.h`, `Common2.h`, `Manager.h`, or any file whose name does not say what is in it.

### 6.3 One file, one subject

- One primary type per header. Small helper structs used only by that type may live alongside it.
- File name equals the primary type name exactly: `class StoragePanel` → `StoragePanel.h` / `StoragePanel.cpp`.
- A file over ~1200 lines should be split along an existing seam (per-game, per-panel) — propose the split, do not perform it mid-task.

### 6.4 Include guards

- `#ifndef` / `#define` / `#endif`. **No `#pragma once`.**
- New guards are mechanical: path relative to `include/`, uppercased, `/` and `.` become `_`.
  `include/UI/Panels/BoxPokemonPanel.h` → `UI_PANELS_BOXPOKEMONPANEL_H`
- Existing guards stay as they are. Do not mass-rename them.
- Guards must be unique across the repository. Verify with the check in §14.

### 6.5 Includes

Order, separated by one blank line between groups:

1. C++ standard library (`<vector>`, `<cstdint>`)
2. Platform / third-party (`<switch.h>`, nanovg, stb)
3. Project headers, quoted, path-qualified from `include/`: `#include "Pokemon/Pokemon.h"`

Rules:

- Never rely on a transitive include. If you name a type, include its header.
- **`using namespace` is banned at file scope in headers.** Several existing headers violate this; do not add another, and do not depend on one. Inside a `.cpp`, a single `using namespace` for the file's own sibling namespace is tolerated but not preferred.
- Never include a `UI/` header from a non-UI layer. Data layers do not know the UI exists.
- Include what the header needs; forward-declare where a pointer or reference suffices (`class PKSEFramebuffer;` — the existing pattern in `UIScreen.h`).

### 6.6 Generated and vendored code — hands off

- `src/Names/*.cpp`, and the table files under `src/Pokemon/` and `include/Pokemon/` produced by `tools/gen_*.py`, are **generated**. Never hand-edit a generated table. Fix the generator, re-run it, commit the regenerated file.
- A generated file's naming style is set by its generator's emit strings, which are themselves subject to this document. Generated C++ is PKSE code and follows §3.
- `nanovg/`, `memecrypto/` and `include/Libs/stb_image.h` are vendored. Never reformat, rename, or restyle them. Never apply this document to them.
- Library and platform symbols are out of scope for §3 (§3.7). PKHeX-derived names are **not** — a generated identifier that violates §3 is fixed in the generator's emit string, then regenerated.

---

## 7. Formatting

- **4 spaces**, never tabs. Anywhere.
- **No hard wrapping of code. There is no line-length limit.** A statement, declaration, expression, argument list, condition or string concatenation is written on one line however long it gets; the editor soft-wraps for display, and a break typed into code to fit a column count is a defect. This applies to the C++ that `tools/gen_*.py` emits too: a generator writes each statement and declaration on one line, and is fixed and re-run if it does not.
  - A line break in code is allowed only where it is structure, not width: between statements and declarations, around Allman braces and control-flow bodies, and inside a raw string or a preprocessor block.
  - **Comments, and array and object initializers (`{ … }` lists), are exempt.** Either may be hard-wrapped.
- Allman braces: the opening brace goes on its own line, for functions, classes, namespaces and control flow.
- Braces are **required** on every `if` / `for` / `while` body, including single statements. The only permitted exception is an early-return guard on one line: `if (!pokemon) return 0;`.
- One statement per line. One declaration per line.
- `const` on the left: `const int slotHeight`, matching existing code.
- Pointer/reference binds to the name: `PKSEFramebuffer &framebuffer`, `const Pokemon *pokemon`.
- Space after keywords (`if (`), none after function names (`draw(`).
- Namespace bodies are indented one level (existing style).
- Do not reformat lines you did not otherwise change. A diff that is 90% whitespace is a rejected diff.

---

## 8. Language rules for this target

The build uses `-fno-exceptions -fno-rtti`. That is not negotiable and it constrains you:

- **No `throw`, no `try`/`catch`.** Report failure with `bool`, `std::optional`, or an out-parameter plus a logged error.
- **No `dynamic_cast`, no `typeid`.** Dispatch through the existing virtual interfaces on `Pokemon` / `Trainer`.
- **No STL API that reports errors by throwing**, notably `.at()`, `std::stoi`, `std::stod`. Bounds-check explicitly, or use `std::from_chars`.
- Prefer `std::span` for views over save data (existing pattern) and `std::unique_ptr` for ownership. Raw `new[]`/`delete[]` exists only where an existing base class already manages a buffer.
- Fixed-width integer types for anything touching save data: `uint8_t`, `uint16_t`, `uint32_t`. Never bare `int`/`long` for a save field.
- Never dereference a possibly-unaligned pointer into save bytes. Copy through `std::memcpy` into a properly typed local.
- No allocation inside a per-frame draw path. Build strings and buffers before the loop.
- `constexpr` for compile-time constants; `static const` only where `constexpr` is impossible.
- `noexcept` on accessors that cannot fail, matching existing declarations.
- Warnings are errors in practice: a change that adds a `-Wall` warning is not done.

---

## 9. Constants and magic numbers

- **No magic numbers in save-format code.** Every offset, size, and block key is a named `constexpr` next to its format's other constants.
- **No magic numbers in layout code** beyond trivially local geometry; the moment a value appears twice, it becomes a named constant.
- Shared UI geometry and every color live in `include/UI/Common.h`. Never re-declare a spacing, radius, or color literal in a panel. Never write `Color(255, 96, 86)` in a screen — use `Colors::ShinyStar`.
- Party/box/bank sizes come from the existing constants (`MAX_PARTY_SLOTS`, `BOX_SLOTS`, `Bank::BANK_BOX_COUNT`, `Bank::BANK_SLOTS_PER_BOX`). Never write `6`, `30`, or `100` for those quantities.
- A constant is defined **once**. If you need it in a second file, include the header that owns it.

---

## 10. Comments
- **Default to no comment.** Code shows *how*; comment only to carry *why* (non-obvious constraints, deviations, or gotchas). 
- **Never narrate the code.** Do not describe loops, variable types, or function signatures. 
- **Never narrate the change.** Avoid comments like "fixed X" or "updated to Y"; that history belongs in the commit message.
- **Stand alone.** Comments must make sense without the surrounding code; do not use pointers to external docs as substitutes for inline explanation. 
- **Delete by default.** If a comment just restates a decision the code already reflects, delete it. 
- **Occam's Razor.** Keep only the one non-obvious fact a reader needs at that line, in the fewest words.

---

## 11. Logging and error handling

- Use `Utils::logInfoToFile` / `logErrorToFile` for diagnostics, `logEventToFile` for user actions, `logTest` / `logTestSession` for the action trace (`sdmc:/PKSE/trace.log`). Never `printf` / `std::cout`.
- Event and trace lines are one line of `KEY=value` pairs, built with the helpers in `Utils/EventLog.h` — do not hand-roll a line, so field names stay consistent across call sites.
- Field keys are `SCREAMING_SNAKE_CASE` and reused across sites: the same fact is the same key everywhere.
- Every failure path that returns `false` logs one error with enough context to identify the save, box, and slot involved.
- Debug and trace output must remain strippable by the "Enable Debug Logging" settings option. Do not add unconditional verbose logging.

---

## 12. UI layer conventions

- Signature order: framebuffer first, geometry next, state last.
  `void drawListSearchBox(PKSEFramebuffer &framebuffer, int boxX, int boxY, int boxWidth, const ListSearch &search, int matchCount, int totalCount);`
- Screens implement `UIScreen`: `update(const PadState&, const TouchInput&)`, `draw(PKSEFramebuffer&)`, `shouldExit()`. Do not invent parallel entry points.
- Panels are stateless drawing functions over data passed in, unless an existing panel in the same directory is a class — then match it.
- A panel never reads global state it was not handed, never writes to a save, and never performs file I/O.
- Both themes must be considered: any new color goes into `Colors` with values checked against the dark and light palettes.

---

## 13. Change discipline

- **Smallest diff that does the job.** No opportunistic refactors, no reformatting, no "while I was here" renames, no reordering includes in files you did not otherwise touch.
- No new third-party dependency, no new library, no new build step without explicit instruction.
- Do not modify `Makefile`, `PKSE.vcxproj*`, `CppProperties.json`, or `.gitattributes` unless the task is about the build.
- Do not edit `README.md` to describe internal changes unless asked.
- When a task needs a decision this document does not settle, ask before writing 400 lines on a guess.
- Commit subject: `<area>: <imperative summary>` — e.g. `Bank: preserve met data on Gen 3 withdraw`. Area matches a directory or the game code. Body explains why, wrapped at 72 columns.

---

## 14. Definition of done

A change is not complete until all of these pass.

### 14.1 Build

```bash
python tools/syntax_check.py              # seconds: the real cross compiler, -fsyntax-only
python tools/syntax_check.py --warnings   # ~1 min: full -O2 pass, non-zero on ANY warning
make clean && make                        # the .nro must build and link
```

The build is kept warning-free. `--warnings` is not optional: `-fsyntax-only` stops before the
optimizer, and `-Wstringop-overflow`, `-Wmaybe-uninitialized` and `-Warray-bounds` only exist
after it. `make` refuses to start until the romfs assets are fetched (see the top-level `README.md`
and `docs/manual/02-build-and-verify.md`).

### 14.2 Violation greps

Run from the repo root. Each must return **no output** (vendored/generated paths excluded).

A hit on a third-party or platform symbol is a false positive, not a violation (§3.7). Only names this project introduced count.

```bash
# Tabs
grep -rnP '\t' src include --include=*.cpp --include=*.h | grep -v 'include/Libs'

# pragma once
grep -rn '#pragma once' src include

# Duplicate include guards
grep -rh '^#ifndef ' include --include=*.h | sort | uniq -d

# using namespace at file scope in a header you touched
git diff --name-only | grep '^include/' | xargs -r grep -n '^using namespace'

# Banned abbreviations, whole-word, in changed files
git diff --name-only | grep -E '\.(h|cpp)$' | xargs -r grep -nwE \
  'fb|idx|buf|cfg|tmp|val|len|cnt|msg|btn|pos|dst|dest|ret|res|num|str|mgr|ctx|attr|elem|prev|nxt|calc|impl|pkm|mon|poke'

# Prefixed data members -- g_ on a mutable global is the only prefix this project uses
git diff --name-only | grep -E '\.(h|cpp)$' | xargs -r grep -nP '\bm_[A-Za-z]'

# 'k'-prefixed constants
git diff --name-only | grep -E '\.(h|cpp)$' | xargs -r grep -nP '\bk[A-Z]\w*\s*='

# Single-letter declarations outside the allowed set
git diff --name-only | grep -E '\.(h|cpp)$' | xargs -r grep -nP \
  '\b(?:int|float|double|bool|size_t|auto|uint\d+_t|char)\s+(?![ijkxyrgba]\b)[a-zA-Z]\b\s*[=;]'

# Unit-only names with no subject (§3.4)
git diff --name-only | grep -E '\.(h|cpp)$' | xargs -r grep -nE \
  '\b(int|float|double|bool|size_t|auto|uint[0-9]+_t|char)\s+(width|height|count|index|size|offset|length|name|color|data|bytes|result|value|total|padding|margin|gap|radius|top|left|right|bottom|rows|columns)\s*[=;)]'

# Raw color literals outside Common.h
git diff --name-only | grep '^src/UI/' | xargs -r grep -n 'Color(' 

# Exception constructs
git diff --name-only | grep -E '\.(h|cpp)$' | xargs -r grep -nE '\bthrow\b|\btry\s*\{|dynamic_cast|\.at\('
```

### 14.3 Self-review checklist

- [ ] Every new identifier was grepped for first, and reuses an existing name where one exists.
- [ ] No new name is an abbreviation or a single letter outside §3.3.
- [ ] Every new name states its subject, not just a unit or role — no bare `width`, `height`, `count`, `index`, `size`, `name`, `color`, `data`, `result`, `value`.
- [ ] No comment exists to explain what a variable holds; the name was fixed instead.
- [ ] Names for the same concept are identical in every file this change touches.
- [ ] Every new file is in the directory §6.1 assigns it, with a mirrored `include/`↔`src/` path and a matching namespace.
- [ ] Anything ported or generated from PKHeX carries PKSE names, not PKHeX's.
- [ ] No generated table, `nanovg/`, or `include/Libs/` file was hand-edited.
- [ ] No symbol was renamed as a side effect.
- [ ] No constant was duplicated that already exists.
- [ ] Failure paths log, and no `throw`, `try`, or `dynamic_cast` was introduced.
- [ ] Diff contains no unrelated whitespace or reordering.

---

## 15. Quick reference

```
Types/Files/Namespaces  PascalCase          BoxPokemonPanel, Trainer9SV
Functions/Members       camelCase           drawPartyPokemon, boxCount
Params/Locals           camelCase           slotIndex, formId
Scope constants         SCREAMING_SNAKE     MAX_PARTY_SLOTS
Local constexpr         camelCase           slotGap
Game codes              1RBY 2GSC 3RSE 3FRLG 4DP 4PT 4HGSS 5BW 5B2W2 6XY 6ORAS
                        7SM 7USUM 7LGPE 8SWSH 8BDSP 8LA 9SV 9LZA  (closed set, §5.3)

Names state their SUBJECT, not a unit:  panelWidth / slotHeight / boxCount / slotIndex
                                        NOT width / height / count / index
                        <subject><Qualifier><Measurement>, in that order.
                        If it needs a comment to say what it holds, the name is wrong.

NO abbreviations. NO single letters except i/j/k (loop), x/y (draw), r/g/b/a (Color).
NO k-prefixed constants. NO pragma once. NO using namespace in headers.
NO throw/try/dynamic_cast/.at(). NO tabs. NO magic numbers. NO drive-by renames.
NO hard wrapping of code: one statement, one line. No column limit. (Comments and { } initializers may wrap.)

Library / platform symbols are OUT OF SCOPE — verbatim (§3.7).
PKHeX is a LOGIC source, not a naming source — rename everything it gives you (§3.7).

One concept = one name, everywhere, forever.
Grep before you name. Match the file next to you. Ask before you invent.
```
