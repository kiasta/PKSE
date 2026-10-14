# Development testing: the self test and the host harnesses

This is the complete reference for PKSE's two automated test systems: the on-console self test
(`src/Test/`, `include/Test/`) and the host harnesses (`tools/harness/`, including `uihost`). The
checks every change needs — `syntax_check.py`, `--warnings`, `make`, and testing the `.nro` by hand —
are in [`manual/12-testing.md`](manual/12-testing.md); this file assumes them and starts where they
stop.

**How the self test gets into a build.** Only the development build carries it:

```bash
make                          # production: PKSE.nro, objects in build/ -- what a release is
make BUILD_TYPE=development   # development: PKSE-dev.nro, objects in build-development/
```

The development build compiles `DEVELOPMENT_SOURCES` (`src/Test`) and defines
`PKSE_DEVELOPMENT_BUILD`. `SELF_TEST_AVAILABLE` (Globals.h) is true exactly when that macro is
defined, and it gates the `-` binding on the save picker, the nav-bar hint and `handleSelfTest`'s
body. `UI.cpp` asks the same `#ifdef` before including `Test/TestRunnerScreen.h`. The hooks the test
drives are in BOTH builds and inert when unused: `g_suppressKeyboard` (Globals.h),
`Utils::keyboardPromptsSuppressed`/`resetKeyboardSuppressionCount` (Keyboard),
`Bank::setRootOverride`/`rootOverridden`, `SaveSelectScreen::selfTestRequested`, and
`UIManager::handleSelfTest`. The development build also names itself — "PKSE (Development)" in the
home menu, a red DEVELOPMENT at the right of every title bar, and a `-development` version in the
title bar, logs and report — and nothing else may
differ between the two builds; debug logging stays a
runtime setting in both. Each build type has its own object directory and output file, so switching
needs no `make clean` (`make clean` removes both). `syntax_check.py` checks both: every file as
production compiles it, plus `src/Test` and every file that names the gate with the development
define (those rows are labelled `[development]`).

The host harnesses do not use the Makefile at all. Each `build-*.sh` compiles the sources it needs
with the host compiler into `build-harness/`.

---

## The tiers

| Tier | What | Time | Answers |
|---|---|---|---|
| 1 | `python tools/syntax_check.py` | ~3 s | does it compile? |
| 2 | `python tools/syntax_check.py --warnings` | ~1 min | does the optimizer object? |
| 3 | host harnesses (`tools/harness/`) | seconds | is the byte manipulation *right*, against real saves? |
| 4 | on-console self test (`-` on the save picker) | < 1 min | does the whole thing still work on a console, for every generation? |

There is **no linter**, and no unit-test framework in the usual sense. Tiers 1 and 2 are also in
chapter 12 of the manual. **Run the first three before calling a change done; run the fourth before calling a
release done.** Anything the self test cannot reach — the SD card, ROMFS, an installed title's save
— is the only thing left that has to be clicked through by hand.

### Which check to reach for

| You changed | Run |
|---|---|
| anything | tier 1, then tier 2 |
| an offset, a parse, a conversion, a table | `build.sh` / `build-gen1.sh` / `build-oldgens.sh` / `build-selftest.sh` |
| the PKSM importer | `build-pksmimport.sh` |
| Pokemon file import/export (`PokemonFile*.cpp`) | `build-pokemonfile.sh`, then `build-selftest.sh` |
| a legality rule or an encounter/event table | `build-legality.sh` |
| a screen, panel, dialog or modal | `uihost/run.sh` |
| the save write path, the bank, ROMFS assets | the on-console self test |
| you are about to tag a release | all four, in order |

**Reach for a harness whenever a change is decidable from data** — a format parse, an offset, a
conversion — instead of shipping it to a console to be eyeballed.

---

## Tier 3 — the host harnesses

The save layer (Trainer / Pokemon / Encryption / Names) is **pure byte manipulation** and compiles
with an ordinary `g++`, so it can run against real save files on a PC instead of on hardware.

```bash
./tools/harness/build.sh [save-root]                     # round-trip real saves
./tools/harness/build-gen1.sh [pksm_1.bnk] [saves...]    # Gen 1: stats, round trip, names, legality
./tools/harness/build-oldgens.sh [save-root]             # Gens 2, 4-7 + every save envelope
./tools/harness/build-pksmimport.sh [bank.bnk]           # PKSM bank import
./tools/harness/build-pokemonfile.sh [PKHeX Tests dir]   # .pkX files against PKHeX's 200 test files
./tools/harness/build-legality.sh <save-root> [--verbose]  # legality sweep + six bite checks
./tools/harness/build-selftest.sh [report] [save-root]   # 7 of the 13 self-test suites
./tools/harness/uihost/run.sh [report] [save-root]       # headless UI: real screens, synthetic input
```

**What every harness shares.**

- They **stub** the few Switch-SDK entry points they reach (logging, `readAllBytes`) rather than
  linking them. `Utils/NXTypes.h` is what lets the save layer's headers parse off-console: on a
  Switch it *is* `<switch.h>`; elsewhere it declares `u8`..`s64` and an `AccountUid`.
- They **link `memecrypto/` through the shared `tools/harness/memecrypto.sh`**. Any harness pulling in
  a Trainer source needs it, because the Gen 7 containers sign their saves, and it must be compiled
  with **`gcc`, not `g++`** — the vendored C uses `xor` as an identifier.
- The stubs mean **only `make` proves the `.nro` links**. A harness passing says the byte work is
  right, never that the console build is whole.

### The save tree: one folder per TITLE

Default save root is `~/pkse-test-saves`. **One folder per TITLE, any filename inside** — the
harnesses find a save by its folder, never by guessing what a dumper called the file:

```
~/pkse-test-saves/
├── Red/            Pokemon Red.dat
├── Gold/           POKEMON_GLD.sav
├── FireRed/        FireRed_e.sav
├── Ruby/  Sapphire/  Emerald/
├── Diamond/        POKEMON_D.sav
├── Pearl/          POKEMON_P.sav
├── HeartGold/      POKEMON_HG.sav
├── SoulSilver/     POKEMON_SS.sav
├── Black 2/        POKEMON_B2.sav
├── X/              main
├── Omega Ruby/     main
├── Ultra Sun/      main
├── Let's Go Pikachu/  savedata.bin
├── Sword/          main
├── Shield/         main
├── Violet/         main
├── Legends Z-A/    main
└── Legends Arceus/ main
```

`tools/harness/SaveTree.h` is the one place that resolves these, so a tree laid out any other way is
missing from every harness at once rather than from some of them.

**A folder is matched the way the console matches it** (`SaveTree::folderFor`): the exact name first,
then any folder whose name agrees once case, punctuation and a leading "Pokemon" are set aside — so
`LetsGoPikachu/`, `Shining Pearl/` and `Pokemon Silver/` all resolve. It used to be the exact name
only, so the host harnesses and the console disagreed about the same tree: a save the console tested
was silently absent from every host run.

**Why folders and not filenames.** The filename depends entirely on which emulator, cartridge dumper
or save manager produced the file, so a perfectly good HeartGold save named anything else was simply
not found — and the harness reported the generation as *skipped*, not as misfiled. More importantly,
four pairs of games — Red/Blue, Gold/Silver, Diamond/Pearl, HeartGold/SoulSilver — plus Ruby/Sapphire
write saves with **no version byte anywhere in the file**. Nothing a harness can read will ever say
which game one came from. The person who dumped it knows; the folder is where they write it down.
That is why `Pearl/` and `Diamond/` are separate rows in the report rather than one row called
"Diamond/Pearl".

A save tree still on the old layout keeps working — each title falls back to the path it used to
have — but the fallback prints a note naming the folder to move it to, so a stale tree says so
rather than looking identical to a current one.

### `build.sh` — the save round trip

Loads each save, writes it straight back **unedited**, and diffs.

> **An editor that cannot reproduce a file it did not change will corrupt one it did.**

No checksum catches "PKSE rewrote bytes it had no business touching"; only this does.

**Sword and Shield also get an `[item edits]` step**, because an unedited round trip cannot tell a
writer that packs each pouch from slot 0 from one that writes it back positionally — the two agree
on any pouch without a gap. It changes the count of every pouch's last entry, appends an item to
every pouch with room, and requires the written item block to equal the original with exactly those
slots changed. It prints how many of the edited entries sat after a gap; a save with none cannot
tell the two writers apart, and says so.

**FireRed, Ruby and Emerald also get an `[interrupted]` step.** It erases one sector of the save's
NEWER slot — what a save interrupted mid-write leaves — and requires the trainer to open from the
older slot, exactly as it opens when that whole slot is erased. Choosing the slot by counter alone
let `detect()` accept the file and the parser then refuse it; the old rule fails all three.
`slotHoldsEverySector` is the one test for both.

It also caught **215 bytes of invented box names on an untouched Z-A save** — every box-name parser
substitutes a display default ("Box 3") for an empty name, and writing the whole array back
persisted placeholders the player never typed. `updateBoxNameBlock()` now writes only
`boxNameDirty` entries.

Exits non-zero if any game fails to round-trip, so it can gate a release.

### `build-gen1.sh` — Gen 1's only real check

```bash
./tools/harness/build-gen1.sh [path/to/pksm_1.bnk] [saves...]
```

**Gen 1 is verified on the host, not on hardware**, and this harness is decisive in a way the others
cannot be: a PK1 party record **stores** the five battle stats the game computed, so recomputing them
from the tables, the DV unpack and the Gen 1 formula and comparing **proves the whole chain at once**.
PK1 has no checksum, so nothing else would catch a wrong parse.

Against a real bank it checks stats, byte-exact round trip, name re-encoding, learn pools, and that
the legality analyzer produces **no** Invalid finding on a Pokemon caught on real hardware — save a
**glitch Mew**, which it counts separately: every Gen 1 Mew event fixes a trainer (the Virtual
Console Mew is trainer id 22796), so a Mew with any other trainer is the Mew glitch, and PKHeX
refuses it too.

Against a `.sav` it loads, writes back unedited, and diffs. That caught **five bugs**, every one PKSE
quietly rewriting bytes it had no business touching:

- item quantities clamped to the game's cap of 99 (a real save carried 158 and 203)
- `recalculateStats()` writing a level byte a party record leaves stale
- an empty name field re-terminated with `0x50` where the game left `0x00`
- `boxesInitialized` asserted on a brand-new save
- list markers filled to capacity where the games write exactly one cap byte

Three things it gets right that it once got wrong, each a false failure on a real Blue save:

- **Stored party stats are a BAND, not a value.** The game recomputes them only on a level-up, an
  evolution or a trip through the PC, so Stat Experience gained since leaves a stored stat BELOW the
  formula — never above it, and never below the same level with none. Blue's Charizard stores
  Defense 147 where the formula now says 148.
- **They are recomputed on CLONES.** The check called `recalculateStats()` on the save's own party
  entity, which rewrote that Defense byte — and the write-back check that runs next reported it as
  PKSE rewriting a byte it had no business touching.
- **The write-back PROVES the current box's flush instead of demanding byte-exactness.** The one
  change an unedited Gen 1 write may make is the current box's FLUSH (and, on a bank never flushed,
  initialising the other box headers to empty). `Trainer1RBY::currentBoxFlush()` publishes the
  geometry the writer uses, and the harness requires every moved byte to lie in the current box's
  bank copy, the boxes-initialised bit or the checksum — plus, on a bank never flushed, the other
  boxes' two header bytes becoming an empty list — and the bank copy afterwards to equal the live
  one. A writer that emits a wrong cap byte fails it on every save. That is proving the flush, not
  excusing a diff.

### `build-oldgens.sh` — the gate for Gens 2 and 4-7

**Load a real save, write it back unedited, require a byte-identical file**, against real
DP/Pt/HGSS/B2/XY/ORAS/Ultra Sun saves, plus an **edit-persistence** pass. It says loudly which
generations it had to skip for want of a file. Everything in the "Loose save files" rules
(chapter 05) was found this way: box records not grown to party size, blank slots stamped from a
template of PKSE's devising instead of the file's own (~67 KB per Gen 4 save), pouches re-packed
instead of written positionally, Gen 4 text fields padded with terminators, the HGSS footer size,
Gen 5's double CRC, the Gen 6/7 CRC variants, and Gen 7's signature.

**Gen 7 signing is proven end to end**: an unedited Ultra Sun save round-trips byte for byte because
`Trainer7::sign()` recovers the signature's real plaintext with `memecrypto_verify` and re-signs it.

**A Gen 2 save does not come back byte for byte, and the harness PROVES that is correct.** Gen 2
keeps a backup of the trainer block — Crystal one contiguous copy at `0x1209`, Gold/Silver five
separate pieces — and writing a save refreshes it from the primary (PKHeX's `SAV2.GetFinalData`).
Real saves ship with a stale backup, so writing one unedited corrects it: 211, 250 and 29 bytes on
the three saves here. The harness requires every byte that moved to lie inside a mirror DESTINATION
(or be one of the two checksum words), and afterwards every mirror to equal its SOURCE. Corrupt a
mirror, or write one byte outside every destination, and it fails. The ranges live in
`Trainer2GSC::mirrorRegions()`, which `writeMirrors()` also reads, so the writer and the check cannot
disagree about where they are.

**Gen 2 also has a synthetic check** (`gen2Synthetic()`) for when no file is present: it exercises
detection, the PokeList2 codec, the 16-bit checksum, the box-bank split and the mirror copies and
proves parse and serialise are inverses, but it does **not** prove the offsets match the real games.
Drop a `.sav` in `<root>/Gold/`, `<root>/Silver/` or `<root>/Crystal/` and the harness round-trips that
instead.

**Every save envelope around real Gen 1-5 saves.** The same envelope fixtures the self test's
envelope chunk uses (`include/Test/SaveEnvelopeFixtures.h`, shared), plus what only the host can do:
the same files with a byte-compare predicate as well as the real detectors, a stray byte in the
padding that must make the file refuse, a known answer for the run-length code, near misses for every
kind, and a real save that must NOT be explained away as a container PKSE cannot read.
`Save::stripRtcFooter` lives in its own header (`Save/RtcFooter.h`), and `legacyFolderIsAmbiguous` in
`TitleFolder.h`, precisely so the host can test them without the Switch SDK.

### `build-pksmimport.sh` — the PKSM bank

Parses a real PKSM `.bnk` (the parser takes a byte span rather than a path, which is what makes it
host-testable), checks every record's checksum, its encrypt/decrypt round trip, and where `commit()`
puts it.

**This harness once locked a bug in.** The importer carried a private set of supported generations
— Gen 1, Gen 3, Let's Go, Gen 8 — never extended as the other fifteen groups landed, so a real bank
reported *"no PKSE format"* for 44% of its contents (101 Gen 2 and 17 Gen 7 of 269). The harness
asserted `1 unsupported (Gen 6)` as correct behaviour, and its placement check carried a *second*
copy of the same list. Both now cover every tag, and the placement check's `default:` **fails** on an
unknown tag rather than skipping it. A harness that asserts today's output is a harness that defends
today's bugs; assert the rule.

### `build-pokemonfile.sh` — Pokemon files against PKHeX's

```bash
./tools/harness/build-pokemonfile.sh [PKHeX tests directory]     # default ~/repos/PKHeX/Tests
```

Every `.pkX` / `.pbX` / `.paX` file in PKHeX's test tree (200 of them). PKHeX wrote them, so they are
the reference: each must open (except the files in `KNOWN_REFUSALS`, each with its reason -- the
Korean Gold/Silver one), open as the species its file name starts with, be PKHeX's bytes again when
written back, and be refused when renamed to another format of a different size. The one allowance:
PKSE fills a party record's battle-stat tail where PKHeX's file left it all zero, because PKSE opens
a Pokemon with its level and stats in place.

### `build-legality.sh` — is the checker slandering real saves?

```bash
./tools/harness/build-legality.sh <save-root> [--verbose]
```

Layer 3 is the only part of PKSE that can call a legitimately-obtained Pokemon illegal, so the
measure that matters is what it complains about on saves from actual playthroughs — where nearly
everything IS legal. A healthy run is mostly "matched"; **a wall of findings means the tables are
wrong, not the save.**

**It sweeps every title folder the self test knows** — Red through Legends: Z-A, 35 saves on the
development tree — not one save per generation. The sample it used to take never opened Silver,
Yellow, Sun, Moon, Ultra Moon, either Let's Go or Sword, so every finding the console reported on
those was invisible to the gate. Two loader rules came with that: an emulator **RTC footer is
trimmed** exactly as `Save::openExternalSave` trims it (untrimmed, every Gen 2 save swept as
"0 Pokemon"), and title folders are matched the way the console matches them (`SaveTree::folderFor`).

**A clean sweep proves nothing on its own**, which is why the run ends with **six bite checks**. All
six fail the run, because a checker that never fires is not a clean result.

1. **Met location.** A Pokemon moved to a met location no game has must draw a complaint. Two details
   decide whether it bites for the right reason. The bogus location must be one the FORMAT can hold —
   900 is stored as 132 in Gen 3's byte and as 4 in Gen 2's seven bits, both real places — so the
   first of 900, 250 and 125 that reads back exactly is used. And the donor must be clean BECAUSE it
   matched an encounter: a hatched donor is clean through its egg, and with the egg location cleared a
   species its game never offers can only say "no encounter produces it", which has nothing to do with
   the location (Sun's first Pokemon is exactly such a Shinx). **A format with no met location cannot
   be given a bogus one**, so this check skips Gen 1 and Gold/Silver and says how many it skipped —
   there the Pokemon stays clean because the check is meaningless, not because the table failed to
   bite.
2. **Event.** Reads the game's own table, poses a Pokemon as the first Mystery Gift distribution row
   in it, and requires the verdict to name that distribution. That exercises the whole path — the row
   generated, the species index reaching it, the sweep accepting its constraints, the verdict naming
   it — for data no one can eyeball.
3. **Wild.** Poses a Pokemon at one of its game's own wild slots and requires a match. **This is the
   one that catches a mis-parsed slot table** — the failure mode every new generation risks, since a
   wrong header length still builds, still produces plausible rows and simply never matches
   anything. It needs two things to be worth having: the donor must not be an unhatched EGG (eggs
   route down the egg path before the sweep, which reads as a table failure), and the row must belong
   to the donor's own VERSION (half of every paired generation is version-exclusive). A Gen 1 or
   Gold/Silver donor is posed on its own terms: its LEVEL is raised to the row's floor rather than its
   met level.
4. **Evolution, caught nowhere.** Poses an evolution of a wild slot's species a level below the level
   that evolution needs, and requires a complaint naming the evolution. **Real saves never exercise
   this**: a box of ordinary Pokemon clears every evolution level comfortably, so the pose has to be
   built, and the evolution must be one the game cannot produce on its own or the sweep matches the
   evolution's own row at chain position 0 where no floor applies. For a Gen 1 or Gen 2 format "caught
   nowhere" has to hold in BOTH generations, because Layer 3 asks both: Gold and Crystal have a level 7
   Butterfree, so a level 9 Gen 1 one is rightly explained as a catch from there.
5. **Evolution, caught elsewhere.** The same pose where that evolution IS catchable elsewhere in the
   game must draw the SAME complaint — the stage-ordering rule in chapter 11.
6. **Seadra.** The reported case, built from the tables with no save at all, in both directions.

### `build-selftest.sh` — the self test on the host

```bash
./tools/harness/build-selftest.sh [report-path] [save-root]
```

Runs **seven of the thirteen suites** in seconds — the framework's own regression test, and a fast loop
for whichever half you are changing.

- `tables` and `gentables` need nothing and run for **all nineteen save-format groups at once**.
- `entity`, `setters`, `legality`, `pokemonfile` and `conversion` run against **real saves, read-only**, from a tree
  laid out as `roundtrip.cpp` expects — one folder per title (`FireRed/`, `Red/`, `Ruby/`,
  `Sapphire/`, `Emerald/`, `Let's Go Pikachu/`, `Ultra Sun/`, `Shield/`, `Violet/`, `Legends Z-A/`,
  `Legends Arceus/`); pass the root as the second argument, default `~/pkse-test-saves`. **Ultra Sun**
  is there because PK7 is a conversion endpoint — Gen 1/2 land in it and every Gen 8/9 game converts
  both ways with it — so the matrix needs a real 3DS Pokemon to push through the remap rather than
  only the synthetic ones it builds itself.
- `conversion` has one chunk that needs **no save at all**: a synthetic Poke Transporter pass building
  PK1s and PK2s from scratch. Most consoles have no Game Boy file, so the sample-driven sweep would
  leave the whole Gen 1/2 route untested — and the cases most likely to break are the rare ones a real
  Red save has none of (Unown's letter, an in-game-trade OT, Celebi, the Dizzy Punch removal, Crystal
  naming itself). A blank entity is a legitimate PK1/PK2, so nothing there is a fixture pretending to
  be a save.

It calls those suites **directly** rather than through the registry, which is why the sample pool is
`inline` in `TestSuite.h` and not in a `.cpp`: linking the registry would drag in the ones that need
the Switch SDK.

The six it cannot run are why the other harnesses exist:

| Suite | Needs |
|---|---|
| `save` | writes through `Save::GetSaveFileContents` |
| `bank` | the SD card |
| `picker` | drives `SaveSelectScreen`, which enumerates save data through fs/ns |
| `assets` | the only check anywhere that the `.nro` actually **contains** the 3260 sprites, three fonts and nineteen type icons it will ask for |
| `ui`, `browser` | covered by `uihost/run.sh` |

So only `save`, `bank`, `picker` and `assets` are console-only.

### `uihost/run.sh` — headless UI tests

```bash
./tools/harness/uihost/run.sh [report-path] [save-root]
```

**Development-repo only.** Everything it needs lives in `tools/harness/uihost/`. It makes **no**
change to `include/` or `src/`, adds no header, define, flag or `#ifdef` to the shipped tree, and is
not referenced by the Makefile or the `.vcxproj`. Deleting the directory removes every trace of it.

It compiles the **same** screen, panel, dialog and modal sources the `.nro` does, against real
saves, driven by synthetic input, and asserts on **what they actually drew** — including the details
page end to end (open it, open the species picker, apply a different species, check the resulting
form, level, ability and checksum before discarding).

**How you test an immediate-mode UI.** An immediate-mode UI has no retained widget tree, which is why
"test the UI" looked impossible here — Cypress works because the DOM is a queryable structure and
PKSE keeps nothing between frames. **`RecordingFramebuffer.cpp` rebuilds exactly that structure**:
every primitive is recorded as it is drawn, so each frame becomes a queryable list of shapes, text and
sprites with bounds, colours and clip state. Assertions run against that, so a test says what the
player would *see*.

Three substitutions, each the only file that touches its console service:

| Shipped file | Replaced by | Why |
|---|---|---|
| `src/UI/PKSEFramebuffer.cpp` | `RecordingFramebuffer.cpp` | **every** SDL/GL/NanoVG symbol in the whole UI layer is in that one file |
| `src/UI/TouchInput.cpp` | `HostTouchInput.cpp` | scripted taps; the press/release edge logic copied verbatim |
| `<switch.h>` | `hostnx/switch.h` | first on the include path, so shipped headers are untouched |

Sprites are **real**: `SpriteManager` builds paths as `romfs:/sprites/…`, which off-console is just a
relative path — so `run.sh` symlinks a directory literally named `romfs:` beside the binary pointing
at the repo's own `romfs/`. Sprite presence changes layout (a missing one draws a placeholder), so
this is what keeps host frames faithful to console frames.

Writing a test:

```cpp
Driver d(ctx, screen, "box-view");
d.step("open the box view");            // names the action; appears in every finding
gotoHome(d, screen, 0);
d.press(Btn::A).frame();                // one press = down-edge frame + release frame
d.tilt(0, -1);                          // the left stick, which must act as the D-pad
d.see("Charizard");                     // visible text  (cy.contains)
d.notSee("Bad Egg");
d.seeNear("Lv. 100", 640, 200);         // positional, with tolerance
d.seeSprites(1);
d.type("NEWNAME").press(Btn::A);        // queues the keyboard answer  (cy.type)
d.expect(screen.selectedBoxIndex == 3, "the box did not change");
```

**Every frame is held to invariants for free**, with no test written for them: the frame must draw
something, and nothing may land completely off-screen. That is what makes `testInputFuzz` — a
fixed-seed sweep of random presses — worth running at all.

**Assertions that exist because the rule cannot be eyeballed from the source:**

- **One nav bar per frame** (`expectOneNavBar`): at most one full-width rounded sheet pinned to the
  bottom edge. Three dialogs used to draw a strip inside their own card as well as the screen's bar,
  and the two rows did not agree.
- **`testItemsKeys`** presses AND taps both of the Items pouch's badges (`Y` search, `-` Add Item),
  checking each key for the other's effect too. Add Item sat on `Y` until the search claimed it; the
  search branch returned first, so adding an item became unreachable while the nav bar still
  advertised it. Nothing failed.
- **`Driver::tilt`** checks the on-screen keyboard's left stick against its D-pad. The keyboard runs
  its own loop, so the stick has to be sampled there too.
- **The box summary panel's legality verdict clears the edit hint.** The panel is 452px wide; a
  centred hint leaves the left 159px and the label measures 203, which is why the hint moved to
  right-aligned. `uihost` asserts the clearance.
- **A refused RetroArch-compressed file** draws its advice under a long name, and the advice must
  survive the strip's left trim.
- **The whole `ui` walkthrough runs a second time against an emptied copy of each save** and must add
  no failures at all (see "a step that cannot run is skipped" below).
- The `browser` suite runs here too, since it is host-buildable and needs no save.

**Assert on a phrase from the START of a wrapped message.** `UI::wrapTextToWidth` breaks long text
over several drawn lines, and a later phrase can be split across two of them and never match, which
reads as the message missing.

On the first failure of each test the driver writes an **SVG of the failing frame** to
`build-harness/uitest/shots/`, and the report includes the frame's visible text. A headless UI test
is only useful if you can still see the screen that broke.

**The honest caveats**, from its README:

- **Text advance widths are averaged**, not measured from Nunito, so layout driven by `measureText`
  (centring, right-alignment, ellipsis truncation) lands within a few pixels of the console rather
  than exactly on it. Write positional assertions with a tolerance.
- **Four screens are not drivable**: `SaveSelectScreen`, `SystemIcons`, `UI.cpp`, `TestRunnerScreen`.
  They are **excluded from the build rather than stubbed**, so a missing symbol is a build error that
  names the problem instead of a stub quietly returning a fiction.
- **The save commit is scripted** (`UIHost::setSaveResult`), not real. The write path is console-only,
  but the UI's behaviour around both outcomes is not — a fixed answer either way would leave one
  branch untested.
- **The button bit values in `hostnx/switch.h` are copied from libnx** and nothing here can prove they
  still match. They are stable ABI. If a test passes on the host but the same press does something
  else on hardware, check those first.

---

## Tier 4 — the on-console self test

`src/Test/` is an **automated test pass that runs inside the `.nro`, on hardware, against the
console's own saves**, reached with **`-` on the save picker** of a development build
(`PKSE-dev.nro`). It exists because verifying PKSE by
hand means opening every save and clicking through party, boxes, items, trainer info, the details
modal, the legality panel, the bank and the storage view — per game, after every change. That is
hours of work whose most likely outcome is a wrong byte waved through because the twentieth box
looked like the nineteenth.

### One save per TITLE, and every title listed

**It covers all THIRTY-EIGHT titles** (`TESTED_TITLES` in `include/Test/TestSuite.h`, in release
order, typed as `GameVersion` rather than numbers so a row cannot drift from the game it names). The
plan is one row per title. **The plan screen shows as many rows as fit above its footer and the nav
bar** (six on the console's 720 lines) and scrolls the rest. That count is derived, not chosen: it
was a constant eight, which ran the last row under the nav bar and drew the footer — the note that
nothing the user owns is written — off screen. Its titles are centred on their status dots with
`PKSEFramebuffer::textYCenteredOn`; the hand-picked `-15`/`-11` offsets it had left them 5px high,
measured on a console screenshot.

Grouping by generation was the earlier design and **it hid real differences**: Violet and Legends:
Z-A are both Gen 9 but are not the same save format, so testing one and calling it "Gen 9" left the
other untested and unmentioned.

**Every title names itself, and almost all of them can.** A Switch save's title id IS its title, and
Gens 5, 6 and 7 carry a real game byte (`PlayerData5 +0x1F`, `MyStatus +0x04`), which
`Trainer::getGameVersion()` reads. That method is pure virtual precisely so a new format has to answer
rather than inherit a guess. The byte is accepted only if it names one of that group's own titles;
anything else is a misparse, not a discovery, and the group representative is the honest answer then.

**Four pairs cannot answer it, and THE FOLDER DECIDES.** Red/Blue, Gold/Silver, Diamond/Pearl and
HeartGold/SoulSilver share a save layout with no version byte — PKHeX's `SAV4DP` reports the group for
the same reason. The bytes report the first of the pair (`getGroupRepVersion()`) and a per-title
folder overrides that. **Each title needs its own save**: there is deliberately no "covered by its
sibling" fallback. It read as *"Blue: covered by Red"*, which claimed a test that never ran under that
name — the run opened Red's file and tested it as Red. A title with no folder reads "no save file
found" and is counted as the coverage gap it is. (`Trainer3FRLG` returns FireRed on the same grounds
and loses nothing by it: FR/LG are a Switch title. `Trainer3RSE` is the fifth pair and does need
folders: Ruby and Sapphire are byte-identical loose files. Emerald names itself but still needs a
folder to be found.)

**A title with no save is reported as a skip, loudly** — a pass that quietly covered five titles out
of thirty-eight is worse than no pass. The two gaps are not interchangeable, and
`Test::comesFromLooseFile` decides which message a row shows: a loose-file title says where to put a
`.sav`; an installed title says the game is not on this console and there is nothing to copy.

Where a title has more than one save on the console, **Left/Right cycles between them**. **X drops a
title from the run.**

**EVERY TESTED TITLE MUST RESOLVE TO A GROUP, and `getGameGroup()` returning `Invalid` is silent.**
The runner accepts a file only when `getGameGroup(folderTitle)` equals the group the save parsed as.
`getGameGroup()` is a switch ending in `default: return Invalid`, and **Gen 1 had no case at all**: RD,
GN, BU and YW fell through, so every Red, Blue and Yellow save sitting correctly in its own folder was
REFUSED as *"sav.dat is a Red/Blue/Yellow save, not Red"* — the group name, phrased as though it were
a different game. A refused save and an absent one both end as "this title has no save", so nothing
said so. `SuiteTables::checkTitleRouting` asserts it for every row of `TESTED_TITLES`, which is why
that array lives in `TestSuite.h` rather than `TestRunner.cpp` — the host harness runs that suite
without linking the runner. **Adding a title means adding its `getGameGroup()` case.**

### Where it looks for loose-file saves

**The self test CREATES the folders it accepts.** Opening it runs `ensureTitleSaveFolders()`, which
makes one directory per loose-file title under `sdmc:/PKSE/tests/saves/` — `Red/`, `Blue/`, `Yellow/`,
`Gold/` … `Ultra Moon/`, 23 of them, spelled exactly as the runner reads them. The accepted spellings
used to live only in a doc, so a user had to guess, and the obvious guess ("Pokemon Yellow") matched
nothing and looked exactly like a missing save. Only loose-file titles get one: a Switch title's save
comes from installed save data, so a `Sword/` folder would invite a file that is never read. `mkdir`
on an existing directory is `EEXIST`, so this runs on every open and never disturbs a filled folder.

**`sdmc:/PKSE/tests/saves/<Title>/` is the ONLY place looked in**, and only its per-title subfolders —
a bare file at its top is not a candidate, and nothing else on the card is walked. It used to also
sweep `sdmc:/PKSE/saves/` and `sdmc:/PKSE/` and to accept bare files: a stray `.sav` anywhere near
PKSE's folder then became a candidate for whichever title its bytes could belong to, so a user who had
filed exactly one save under `tests/saves/Red/` was told Red had two and could not find the second.

Saves are matched by extension (one list of every emulator's habits) and a 3DS `main` by name. **That
rule is `Save::isSaveFileName` (`include/Save/SaveFileName.h`) and both the self test and the file
browser call it**, because the two had already drifted. The name test decides only what to LIST;
whether the bytes are a save stays `Save::openExternalSave`'s question. Candidates go through the same
probe chain the manual file browser uses — and are *constructed*, not merely detected — so what the
test accepts and what the picker accepts cannot drift apart.

**A `main` that is a SWITCH title's save is listed and then refused, and that is correct.** SwSh, S/V,
PLA and Z-A all name their save `main` too, and nothing about the NAME can tell one from a Gen 6
`main`. Let's Go's `savedata.bin`, BDSP's `SaveData.bin`, the `backup`/`poke_trade` siblings and PLA's
`main2` are deliberately NOT listed: no name test can make them openable.

**The TITLE FOLDER is not tidiness — it is the only channel one fact has.** A Diamond save and a Pearl
save are the same bytes, so the folder is the only thing that can tell the run which it has. The
folder names are the ones PKSE already prints for those titles (`Enums::getGameVersionName`), matched
case- and punctuation-insensitively, so there is no second list of spellings — `HeartGold`,
`heartgold` and `Heart Gold` are the same folder. **A leading "Pokemon" is accepted too**
(`Pokemon Yellow` → Yellow): that is how the games are titled and how dumped saves are named, no title
begins with the word, and the match after stripping it is still exact rather than a substring hit.
Subdirectories are read one level deep and only when they name a title; anything else under a scan
root that is not a known PKSE folder is **listed on the plan screen**, because a folder skipped in
silence and a title with no save look identical from the outside.

**`d_type` IS NOT FILLED IN BY THE SD CARD'S DEVOPTAB.** `readdir` returns `DT_UNKNOWN` there, so a
directory tested with `entry->d_type == DT_DIR` reads as a file — which is how every per-title folder
on a real console was invisible while the feature worked perfectly on the host (whose `SaveTree`
walks with `stat`). Fall back to `stat`/`S_ISDIR` on `DT_UNKNOWN`, as `FileBrowserDialog` already does.

**THE FOLDER IS AUTHORITATIVE, and a file it does not fit is REFUSED.** A save under `<Title>/` is
that title's or it is nothing — it can never become a candidate for a different title. The earlier
rule ("the bytes win, and the misfiling is reported") sounds more careful and is worse: it kept the
DETECTED version, so a save filed under `Silver/` whose bytes read as Gen 1 turned up as a second
candidate on the **Red** row. `Runner::rejectedSaveReason()` carries the reason and the plan row shows
it, because "no save file found" is the wrong thing to tell someone staring at a folder with a file in
it. Three things land there: not a save PKSE can read (a 512 KB block of zeros is what an unwritten Gen
4 slot looks like), a save of a different format than the folder claims, or one that parsed to
nothing. The same rule is why the scratch copies under `tests/work/` are prefixed with the title: every
3DS save is called `main`, so copying two by name alone had the second land on the first's bytes.

### Nothing the user owns is written

- Every save is copied into `sdmc:/PKSE/tests/work/` and all write tests happen there.
- `injectToTitle` is passed `false` **unconditionally**, and `saveExternalSave`'s `backupOriginal`
  (default true) is passed `false` — the same shape.
- The backup folders are never touched.
- The bank tests run against a scratch bank (`Bank::setRootOverride`, which also skips legacy
  migration).

The report at `sdmc:/PKSE/tests/report_<timestamp>.log` is the one thing a run leaves behind, and it
is written **regardless of `g_debugLogging`** — unlike every other sink, because producing it *is* the
action the user asked for, and it only ever happens when the self test is deliberately started.

### No keyboard during a run

`Runner::start()` sets `g_suppressKeyboard`, and `Utils::promptText` / `promptNumber` then refuse
before any keyboard opens, reporting a cancel — which every call site already treats as "no change".
PKSE's keyboard runs modally until answered and swkbd is a library applet, so either one BLOCKS until a
human types something: one prompt reached by a driven screen stops the pass dead and waits for hands
on the console. That happened.

**Suppression is the floor, not the intent.** The journeys are written to avoid the prompts (the Items
journey presses no A because the quantity keypad is a keyboard prompt; the Trainer journey presses no
A because rows 0 and 1 are the name and money editors), and every step checks
`Utils::keyboardPromptsSuppressed()` afterwards — a step that raised one is a FAILURE naming the
journey and action, because a step reaching a keyboard is a step landing somewhere the script does not
think it is. Those two halves together traced a real fault: a derailed empty-save journey ended up
pressing `A` on the Nickname row.

### The runner is a step machine, not a loop

```cpp
bool Runner::step(int budgetMs);   // does work for up to budgetMs, then returns
```

Suites hand out work in chunks sized to a frame budget, called from `TestRunnerScreen::update`. A
pass that blocked for its full length would stop the app pumping `appletMainLoop`, which on a Switch
means the HOME button stops answering and the system decides the app has hung.

Phases: `GlobalSuites → (TargetLoad → TargetSuites → TargetDone)* → CrossSuites → WriteReport`.

`cancel()` stops early and writes what has been found so far, marked **PARTIAL** rather than
discarded: a run abandoned three suites in because the first one was clearly broken is still a run
whose findings are worth reading.

### The thirteen suites, in run order

Defined once, in `src/Test/TestSuite.cpp` (one `src/Test/Suite*.cpp` each), and **the order is
load-bearing**.

| # | Suite | Scope | Checks |
|---|---|---|---|
| 1 | `tables` | Global | generation-independent data tables |
| 2 | `assets` | Global | ROMFS: fonts, type icons, every sprite |
| 3 | `picker` | Global | drives the save picker (console only) |
| 4 | `browser` | Global | drives the file browser over a fixture directory it builds |
| 5 | `gentables` | PerTarget | per-generation tables (items, learnsets, dex) |
| 6 | `entity` | PerTarget | every Pokemon in the save |
| 7 | `setters` | PerTarget | set → get on a blank entity, field by field |
| 8 | `legality` | PerTarget | the analyzer over the save |
| 9 | `ui` | PerTarget | **drives real screens** with synthetic input |
| 10 | `pokemonfile` | PerTarget | .pkX export, read back, refusal of renamed or corrupted files, bank import |
| 11 | `save` | PerTarget | save structure, round trip, **edit persistence** (an empty slot filled, a new item added), save envelopes |
| 12 | `conversion` | Cross | the cross-generation conversion matrix, plus a synthetic Poke Transporter pass |
| 13 | `bank` | Cross | deposit / save / reload / verify, discard by reload |

Three of them DRIVE the app rather than calling into it: `ui`, `picker` and `browser`.

Why that order:

- **Global first.** A hole in a shared data table explains every downstream failure at once, so it
  belongs at the **top** of the report rather than being rediscovered under five generations.
- **`picker` is Global** because the save picker is the only screen reachable before a save is
  loaded.
- **`ui` before `save`**: the journeys navigate rather than edit, so they need the save as the
  console left it.
- **`save` is LAST of the per-target suites** because its edit-persistence chunk deliberately
  **mutates** the loaded trainer. Every suite above reads the save as found — and `entity` is also
  where the cross-generation sample pool is cloned from — so running `save` first would have them all
  checking, and banking, Pokemon the test itself had just changed.
- **Cross last**, running from the samples the per-target sweep left behind, after every save has
  been closed again.

### Design decisions worth knowing before changing anything here

**1. Findings are grouped, not streamed.** One wrong table produces a failure per species, and a
1025-line report is a report nobody reads. Occurrences of the same `(suite, what)` collapse into one
`FindingGroup` with a count plus the first `MAX_SAMPLE_COUNT` (25) detail lines.

```
[WARN] entity: ability is not one of the species' three slots   (x1)
    Gen 9 / Legends Z-A  box=1 slot=20  species="Fletchinder" form=0 lv=27 ability=145 slots=49/49/177
```

**2. A passing check is counted, not logged.** "0 failures" means nothing without "out of 40,072
checks". A suite that silently checked nothing shows up as a suspiciously small number rather than as
a clean pass.

**3. Legality findings are warnings.** PKSE can edit saves, so a box may genuinely hold something
illegal. **The value is the rate**: one Invalid in a box of thirty is a Pokemon, thirty is a bug
(which is how the Gen 1 saturated-EV misread would have been caught). A checker that never fires would
pass that, so the `legality` suite also **plants defects**: a blank Pokemon with a duplicated move, and
(Gen 3 onward, not Let's Go) an EV total over 510, must be flagged Invalid for exactly that, and the
same Pokemon without the defect must not be. Those are failures.

**3a. Every check can fail.** No `context.check(true, ...)`. A check counts only when its condition was
computed from something independent of the code under test: a stated expectation, PKHeX's value, the
bytes the save started with. Mutation-test a new check before trusting it: break the thing it guards,
on the host, and watch it fail.

**4. A step that cannot run is SKIPPED, not pressed.** Every `ui` journey step carries an optional
precondition (`Step::needs`); a step whose precondition fails is SKIPPED (a `skip`, listed in the
report and on the results screen), once per journey per target. Every step also says what it MUST
change (`Step::mustChange`, a `UISnapshot::diff` field name) or carries a post check, so a button that
does nothing fails: the allow mask bounds what a press may change, `mustChange` requires its effect. Not tidiness: on a save holding no Pokemon, `A` on an empty box slot opens the
**creator**, so the step meaning "open the species picker" instead APPLIES a species and writes a new
Pokemon into the box. The editor is dirty from then on, every later `B` raises the save prompt instead
of going back, and each remaining journey reports its own navigation as broken. Two empty saves
produced **84 failures**, none about the code under test. An empty PARTY does the same one level down.
`uihost/run.sh` runs the whole walkthrough a second time against an emptied copy of each save and
requires it to add no failures.

**5. A suite must not carry its own list of what it supports.** `SuiteSave`'s write and reload both
opened with `if (ctx.group != RBY) return false;` — true when Gen 1 was the only loose format, and
still there after eleven more arrived. Every one reported "writing the save back failed" for a write
never attempted, and **nothing reached the log**: no `[ERROR]` line, none of `saveExternalSave`'s
messages, because no save-layer code ran at all. **A report accusing the save layer while the save
layer was never entered is the shape to recognise** — check the log for the writer's own success line
before believing the failure is where it says. Both halves now call `Save::saveExternalSave` /
`Save::openExternalSave`, which already dispatch over every loose group. The scratch file is carried on
`Context::savePath` rather than found by picking the largest file in the work directory: Silver and
Crystal share `work/gsc/`, Ruby and Emerald share `work/rse/`. The PKSM importer harness shipped the
same mistake (see `build-pksmimport.sh` above).

### The `save` suite's chunks

**Unmodified save round trip** — write the save back unchanged through the app's own write path and
compare the FILE byte for byte, then re-read it and compare semantically. A few rewrites are documented
and each is allowed only where it is proven to be that rewrite:

| Format | Allowed | Proof |
|---|---|---|
| Gen 1 | the current box's flush | every changed byte is in the bank copy, the initialised bit, a first-flush header or the checksum, and the bank copy now equals the live one (as `build-gen1.sh`) |
| Gen 2 | the trainer-block backup refresh | every changed byte is in a mirror or a checksum word, and every mirror equals its source (as `build-oldgens.sh`) |
| Sword/Shield, Legends: Arceus, Scarlet/Violet, Legends: Z-A | empty box slots normalised; Pokedex registration | decrypted block by block: only the box block (in EMPTY slots only) and the Pokedex blocks may differ |
| Let's Go, Brilliant Diamond / Shining Pearl | Pokedex registration | not attributable here, so any change is a WARNING naming the count and first offset |
| everything else | nothing | the file must be identical |

Gen 3's `serialize()` (the app's write path, Pokedex included) reproduces real FireRed, Ruby and
Emerald saves exactly (`build.sh`'s `[serialize]` rows), which is why Gen 3 is held to identical.

**Edit persistence** — change money, trainer name, a box name, the current box (to one other than the
open one; every format stores it except Let's Go, which has none), an item quantity and a Pokemon's
IVs/EVs/nickname; save; re-read; check **each one separately**. *"The save didn't work"* is not a bug
report; *"the box name persisted and the item count didn't"* is.

**Envelopes** puts every save that has a file form inside each envelope of its size, in each layout
that envelope comes in, then padded the ways it really turns up (`include/Test/SaveEnvelopeFixtures.h`,
shared with `build-oldgens.sh`):

| Save | Envelopes | Padding |
|---|---|---|
| Game Boy | RTC footer, Switch Online header (with and without a clock), TGB Dual clock, MiSTer sector | GBE+'s, to 128 KiB with zeros |
| GBA | RTC footer, VBA Next save RAM, MiSTer sector, meteor, BizHawk 1.x, no$gba (run-length and stored) | to 256 KiB with FF |
| DS | DeSmuME footer, both Action Replay headers, no$gba (run-length and stored) | YSMenu's 1 MiB with its flags; a 1 MiB save inside a DeSmuME footer |

Each must open as its own title through `Save::openExternalSave`, survive an unedited
`Save::saveExternalSave`, and come back as the envelope that went in around the save that came out —
one comparison covering "kept", and for Switch Online "re-hashed" and for no$gba "coded again". Taking
the FIXTURE apart must give back exactly the save that went in, and after the write the padding and
(except Switch Online's) the header must equal the fixture's own. The
fixtures are built from each writer's layout, NOT through `joinSaveEnvelope`, except Switch Online's
(its SHA-1 has to be right); no$gba's run-length code is written by a second, deliberately plain
encoder, so PKSE's decoder is tested against something it did not write. FireRed/LeafGreen's INSTALLED
save is a GBA file like any other, so it runs too, through the loose FR/LG row. 3DS saves have no
envelope, and a size with no envelope defined is a skip; Switch-only formats get a note.

### The `setters` suite: every field has a stated expectation

```
read-back == written, field expected            -> correct.
read-back == previous value, field absent       -> correct (Gen 1 has no ball; its no-op setter is
                                                   right). Listed in a note.
read-back == previous value, field EXPECTED     -> FAILURE: the setter stores nothing, or writes
                                                   somewhere its getter does not read.
read-back == written, field listed as absent    -> FAILURE: the predicate or the absence list is wrong.
read-back == anything else                      -> FAILURE: a mask, a shift or an offset is wrong.
```

The expectation never comes from the setter. Where `Pokemon` has a capability predicate (`hasNature()`,
`hasBall()`, `hasMetDate()` ...) the predicate is the expectation, checked in both directions. Every
other field is expected stored except the explicit `(group, field)` absences in `formatLacksField()`,
each from PKHeX's entity class for that format. (An earlier version treated "not stored" as "the format
has no such field" everywhere, so a no-op setter passed on every format: the bug the suite exists for.
A planted no-op `setHeldItem` on Violet now fails it.)

Two probe values per field, both different from the blank entity's, and above 255 for id fields
wherever the format's ids reach that far. The nickname and OT name get different probes and are both
written before either is read. Shininess is checked against the formula (DVs for Gens 1-2, the PID XOR
against 8 for Gens 3-5 and 16 from Gen 6), not only against `isShiny()`, and a Gen 3/4 shiny reroll
must keep the nature. It runs on a blank entity from `createBlankPokemon()`, never on user data.

The problem it exists for:

> Every Pokemon class is a set of raw offsets, one class per game, most near-identical to a sibling.
> That is the codebase's core abstraction and also its sharpest edge: **a copied-and-adjusted class
> with one offset left at the sibling's value compiles, runs, and gives wrong answers that look like
> plausible data.** Nothing in the app notices, because the getter and the setter usually share the
> offset — so a wrong pair round-trips perfectly *within PKSE* and only diverges from the game.

### The `tables` suite: localized tables

- **Testing a localized table needs a table-wide comparison, not a sample.**
  `SuiteTables::checkNameLanguages` asserts, **per table**, that each language differs from English
  somewhere and that wherever English has a name so does every other language. Per table is the whole
  point: pooling every name into one list passes even when an entire table is broken. A sample ("is
  Bulbasaur translated?") FAILS ON CORRECT DATA — only 21 of 1026 species names differ between
  English, Italian and Spanish.
- **A table-wide language check does not bite for forms.** Arceus's plates and Silvally's memories are
  named by calling `getTypeName()`, itself language-indexed, so eight form names shift language even
  when the form dispatcher is completely broken. `SuiteTables` therefore asserts the direct contract:
  wherever the generated table has a name for a (species, form), `getFormName` must return exactly
  that. **When a getter composes part of its answer from another localized getter, the indirect test
  is not a test.** It also asserts that a form English does not name composes to a bare species name
  in every language.
- `SuiteTables` bounds `personal.type1`/`type2` against `Names::getTypeCount()` (18), not
  `getTypeNameCount()` (19) — merging the two would let a corrupt personal type of 18 pass.
- `checkTitleRouting` — every `TESTED_TITLES` row resolves to a group (above).
- `Pokemon::isAbilityLegal(const Pokemon &)` is the one check Layer 2 and the self test share.

### The `browser` suite

**It exists because a UI defect can sit behind perfectly correct logic.** The picker filtered on
`.sav`/`.srm`/`.dat`/`.sgm` alone, so every Gen 6 and Gen 7 3DS save — all called `main`, with no
extension — was never offered, while the bytes were fine and `openExternalSave` opened them. Eight of
the twenty-seven openable saves in a real collection were invisible. No logic test could see it.

So the suite builds one file per REAL-WORLD save filename plus the save-adjacent files that must NOT
be offered, and asserts the listing exactly — including a file called **`domain`**, there to fail if
anyone relaxes the whole-name match for `main` into a suffix. It also cross-checks the browser's own
filter against `Save::isSaveFileName`, because those are two matchers reading one pair of lists and it
was their disagreement that shipped. Unlike `picker` it is host-buildable, so `uihost` runs it too —
and it needs no save.

### Driving the UI: `SuiteUI` and `UIJourney`

The `ui` suite presses buttons. Every other suite calls functions directly.

**How it works without a test hook in shipped code:** a screen receives input as `const PadState&`,
and libnx's button readers are plain inline reads over that struct. **A `PadState` built in memory is
not a mock — it is the same bytes the console hands over**, and the screen cannot tell the difference.
The journey builds one, calls the real `update()`, then the real `draw()` against the **live
framebuffer**, so the rendering path (ROMFS sprite loads, NanoVG text, GL) genuinely executes.

**The central idea: every change is declared.** `Test::UISnapshot` (`include/Test/UIJourney.h`)
records navigation state field by field — so a finding can NAME the field that moved — and data as
FINGERPRINTS (`trainerFp`, `partyFp`, `boxFp`, `itemFp`, `boxNameFp`). The point is only "did this
change", and hashing a box is the difference between a snapshot costing a few hundred bytes and
costing megabytes on a machine with 4 MB of heap to spare. There is no separate save-pane box cursor:
the Storage view's save pane and the Boxes view share one box cursor, which `box` already carries, so
snapshotting it twice would report one move as two findings.

Before each action the journey snapshots; after it, it snapshots again and diffs. **The action
declares which fields it is allowed to change; anything else that moved is a finding.**

```cpp
enum Allow : uint32_t {
    AllowNothing = 0,
    AllowHome, AllowView, AllowCursor, AllowStorage, AllowModal, AllowSwap, AllowExit,
    AllowTrainer, AllowParty, AllowBoxes, AllowItems, AllowBoxNames, AllowUnsaved,
    // plus AllowData (every data bit) and AllowNav (every navigation bit)
};

Journey& act(const char* what, u64 button, uint32_t allow, int presses = 1);
Journey& settle(const char* what, uint32_t allow, int frames = 1);   // a frame with no input
Journey& idle(int frames = 1);                                       // pump WITHOUT verifying
bool     expect(bool passed, const std::string& what, const std::string& detail = {});
```

> **That inverts the usual bargain.** A conventional UI test asserts the handful of things its author
> thought to check and is blind to everything else, so a keypress that also corrupted a box would
> pass. Here silence is meaningful: navigating the box view is declared to change navigation state
> **only**, so if it touches a single byte of Pokemon data the run says so without anyone having
> predicted that particular bug.

`settle()` exists for the one-frame-deferred edits the editor uses so a highlight renders before a
blocking applet opens. Every action's diff goes to the report's **TRACE** section, so the change log
can be read directly rather than inferred from the absence of failures. `idle()` exists for the
holding frames of a paced walkthrough — verifying them would add a trace line and a full re-hash of
every box **sixty times a second**, burying the log under "(hold) → changed: nothing".

**Touch is not driven on the console.** `TouchInput` reads the panel inside `update()` and its state
is private, so a synthetic tap would mean a test hook in shipped code. Everything in PKSE is reachable
by button, so the journeys drive buttons. (`uihost` scripts taps through `HostTouchInput.cpp`.)

The suite also **publishes the screen it is driving** so `TestRunnerScreen` draws that instead of its
own progress view, with a caption strip:

```cpp
static void setDrivenScreen(UI::UIScreen* screen) noexcept;
static void setDriveCaption(const DriveCaption& caption);
static void yieldFrame() noexcept;   // one chunk per FRAME, so the walkthrough is watchable
```

`yieldFrame()` matters: without it the budget loop runs chunks back to back until its milliseconds
are gone, so an entire walkthrough completes inside one or two frames and nothing of it is ever
presented.

### Adding a suite

Three edits:

1. Write `int fooPlan(Context&)` and `void fooRun(Context&, int chunk)` in `src/Test/SuiteFoo.cpp`.
2. Declare both in `include/Test/TestSuite.h`.
3. Add one row to the table in `src/Test/TestSuite.cpp`, **in the right run-order position**.

```cpp
struct Suite {
    const char* name;
    Scope       scope;              // Global | PerTarget | Cross
    int  (*plan)(Context& context); // chunks needed. 0 = skipped SILENTLY
    void (*run)(Context& context, int chunk);
};
```

`plan()` returning **0 means "nothing to do here"** and the suite is skipped silently. A suite that
cannot run for a **reason** should return 1 and report a skip from `run()`, so the gap lands in the
report instead of vanishing.

Inside a chunk, use `Context`:

```cpp
bool context.check(bool passed, what, detail = {});   // Fail on miss; RETURNS passed, so it can guard
bool context.warn (bool passed, what, detail = {});   // Warn on miss
void context.fail (what, detail = {});
void context.note (what, detail = {});
void context.skip (what, detail = {});                // a COVERAGE gap: its own Skip severity, listed
                                                      // in the report and on screen, never a check
```

A suite that plans zero chunks, or runs without recording a single check or skip, is reported as a skip
by the runner. The runner also rewrites the report after every target (marked IN PROGRESS), so a run
that crashes part way still leaves the report up to the last finished target.

`check` returning its argument is what lets a guard read as
`if (!context.check(pokemon != nullptr, "...")) return;` instead of testing the same thing twice.

Detail strings are `KEY=value` pairs in the same spelling as the `[EVENT]` log, so a finding and the
action log that produced it read and grep the same way. Helpers: `num`, `hex`, `slot` (1-based, to
match what the UI shows), `partySlot`, `monLabel`.

Use the shared `Test::isEmptySlot(pokemon)`, never a null check — a non-null slot is not an occupied
slot. Getting that wrong once produced 1849 phantom findings in the entity sweep and 936 more in the
save round trip, independently.

### Reading a report

```
SUMMARY  checks=40072 failures=0 warnings=5 skipped=3 notes=89

------------------------------------------------------------------------
COVERAGE
------------------------------------------------------------------------
TARGET                                   SUITE          CHECKS   FAIL   WARN   SKIP
-                                        tables          17731      0      0      0
Gen 1 / Red/Blue/Yellow                  gentables        1090      0      0      0
Gen 9 / Legends Z-A                      entity           1979      0      3      0
...
```

Read it in this order:

1. **`checks=`** — is the number plausible? A sudden drop means a suite silently stopped checking.
2. **The COVERAGE table** — which generation and which suite the problems are in, before reading a
   single detail line. **Missing rows are as informative as failing ones.**
3. **Failures**, then **warnings**, then **skipped** (what was not checked), then notes.
4. The **TRACE** section, if a UI journey is involved.

