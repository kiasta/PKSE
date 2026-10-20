# PKSE Technical Manual

This is the working manual for the PKSE codebase: what every layer is, why it is shaped the way it
is, and how to add to it without breaking the parts you cannot see from where you are standing.

It is written for contributors — people, and the AI tools they work with — arriving at this code
from outside. Every chapter is meant to answer questions you will actually have, in the order you
will have them, and every code example is copied from the tree rather than invented — so if an
example and the code disagree, the code is right and the manual has drifted. Say so in your change
and fix the manual.

## The documents, and which one you want

| Document | Answers |
|---|---|
| `CLAUDE.md` (repo root) | *What are the rules?* The terse rules and traps, and a map into this manual. |
| [`docs/CODING_STANDARDS.md`](../CODING_STANDARDS.md) | *How must code be written?* Naming, layout, the verification block. Binding. |
| `docs/manual/` (this) | *How does it work, why is it that way, and how do I add to it?* Every layer explained, the full rules with the measurements and the failures behind them, worked examples. |
| [`docs/integration/`](../integration/) | *Where is this byte?* One guide per generation, every offset transcribed from PKHeX. |

`CLAUDE.md` states each rule in a line or two and points here; this manual carries the rule in
full — the reasoning, the measurement and what happened when it was not followed. Each chapter's
rules are part of the chapter, most of them gathered in a `## Rules: <topic>` section near its end.
If the two ever disagree, treat it as a bug in one of them and check the code.

## Reading order

If you are new to the codebase, read 01, 02 and 03 in order. They are short and everything
else assumes them. After that, jump to whatever you are working on.

| # | Chapter | Read it when |
|---|---|---|
| [01](01-orientation.md) | **Orientation** | Always first. The mental model and the directory map. |
| [02](02-build-and-verify.md) | **Building and verifying** | Before your first change. Toolchain, `make`, and the ways to check your work. |
| [03](03-runtime-and-screens.md) | **The runtime** | `main()`, the frame loop, the screen contract, global settings. |
| [04](04-generation-pattern.md) | **The generation-per-class pattern** | The core abstraction. Every game gets its own class in every layer. |
| [05](05-save-pipeline.md) | **The save pipeline** | Reading a save, editing it, writing it back, backups, injection. |
| [06](06-pokemon-entity.md) | **The Pokémon entity** | The byte buffer, the offsets, and the mutate → recalculate → checksum contract. |
| [07](07-data-tables.md) | **Data tables** | Species, moves, items, learnsets, encounters — what is generated and how to regenerate it. |
| [08](08-ui-layer.md) | **The UI layer** | Immediate-mode rendering, screens, panels, dialogs, modals, input, touch, theme. |
| [09](09-ui-recipes.md) | **UI recipes** | **How do I add a…** Step-by-step, with complete diffs. |
| [10](10-bank-and-conversion.md) | **Bank and conversion** | Cross-game storage and the format conversion that makes it work. |
| [11](11-legality.md) | **Legality** | The three-layer checker, and how to add a check. |
| [12](12-testing.md) | **Verifying a change** | The checks available to a contributor, and what to test by hand. |
| [13](13-adding-a-game.md) | **Adding a game or generation** | The complete checklist, in dependency order. |
| [14](14-pitfalls.md) | **Pitfalls** | The failure modes this codebase keeps hitting. Read before shipping. |
| [15](15-reference.md) | **Reference** | File map, glossary, key constants, symbol index. |

## The one-paragraph version

PKSE is a Nintendo Switch homebrew save editor for the mainline Pokémon games, written in C++20
against devkitPro/libnx, building to a single `PKSE.nro`. It covers every mainline generation, from
Red/Blue to Legends: Z-A. It opens a save, decrypts it into typed
objects, lets you edit Pokémon / items / trainer data through an immediate-mode UI drawn with
NanoVG on OpenGL, and writes the whole thing back re-encrypted and re-hashed. Every game gets its
own class at every layer — there is no generic "Pokémon" struct with a version flag — and the
byte-level logic is derived from [PKHeX](https://github.com/kwsch/PKHeX), which is the reference
source of truth for every offset and algorithm in the tree.

## Keeping this honest

Every code example here is copied from the tree. When you change something the manual describes,
the cheapest fix is to grep this directory for the symbol you touched:

```bash
grep -rn "recalculateStats" docs/manual/
```

The chapters most likely to go stale, in order: **09** (recipes name exact files and line-shapes),
**15** (constants and counts), **08** (the state block), **03** (the settings rows).

Facts that were checked against the tree when this was written — if one of these changes, the manual
is wrong until it is updated:

| Fact | Where it lives |
|---|---|
| 8 settings rows defined, 7 drawn (`SETTINGS_ROW_*`, `SETTINGS_ROW_VISIBLE_COUNT`) | `TrainerViewScreen.cpp` |
| details-modal touch ids 0–33, plus 94, 95, 96 and 99 | `PokemonDetailsModal.cpp` + `TrainerViewScreen.cpp` |
| edit-dialog touch ids 0, 1, 2, 30–35, 40 | `Dialogs/EditControls.h`, `Dialogs/ItemEditDialog.cpp` |
| HOME menu ids 100–105 | `Panels/HomeMenuPanel.cpp` |
| box sizes 30 / 25 / 20 | the `static_assert`s in `UI/Common.h` |
| bank tags 0–15, add-only | `src/Trainer/Bank.cpp` |
| ~190 virtuals on `Pokemon`, 55 pure | `include/Pokemon/Pokemon.h` |
