# 12 — Verifying a change

Each check answers a different question, and a passing one does not answer the next. Chapter 02
introduced the build; this chapter is what to run before calling a change done, and what to send
when something is wrong.

| Check | Time | Answers |
|---|---|---|
| `python tools/syntax_check.py` | ~3 s | does it compile? |
| `python tools/syntax_check.py --warnings` | ~1 min | does the optimizer object? |
| `make` | minutes | does the `.nro` link, with every asset in it? |
| host harnesses (`tools/harness/`) | seconds each | is the byte work right, against real saves? |
| the self test (`PKSE-dev.nro`, `-` on the save picker) | about a minute | does it all still work on a console, for every generation? |
| the `.nro` on a console, by hand | as long as it takes | does it do the right thing to a real save? |

There is **no linter** and no unit-test framework in the usual sense. The harnesses and the self
test are PKSE's automated tests; they are summarised below, and
[`docs/DEV_TESTING.md`](../DEV_TESTING.md) is their full reference.

---

## Does it compile? `syntax_check.py`

```bash
python tools/syntax_check.py
```

The real cross compiler, `-fsyntax-only`, with flags derived from the Makefile — the whole project in
about three seconds. Use it instead of a full build while you work.

## Does the optimizer object? `--warnings`

```bash
python tools/syntax_check.py --warnings
```

A full `-O2` codegen pass that exits non-zero on **any** warning. It is a different question, and a
passing syntax check does not answer it: `-fsyntax-only` stops before the optimizer, and
`-Wstringop-overflow`, `-Wmaybe-uninitialized` and `-Warray-bounds` only exist *after* it.

**The build is kept warning-free.** Run this before calling a change done; a new warning is a
regression even when it looks spurious.

The trap it exists for: GCC inlines every branch before checking, so a `vector` built one way in
branch A and another in branch B is analysed as though both ran. `resize()` after an `assign()`
elsewhere reads as *appending* to the already-allocated buffer and reports an overflow. Build the
buffer in one step (`assign` from a pointer range) rather than `resize()`-then-fill.

## Does it link? `make`

```bash
make            # or: make clean && make
```

Only a real build proves the `.nro` links and carries its assets — the build refuses to start if a
romfs asset is missing (chapter 02 names the fetcher to run). If you added a source directory, it
has to be in the Makefile's `SOURCES`.

---

## The automated tests

Both run against **real saves you supply**. None ship in the repository, because a save holds its
owner's trainer name and id.

- **The host harnesses** (`tools/harness/build-*.sh`) compile the save, entity and conversion
  layers with the host compiler and run them against a folder of saves, one sub-folder per title
  (default `~/pkse-test-saves`). `build.sh` is the unedited round trip described below, done for
  every save at once. Others cover Gen 1, Gens 2 and 4–7 with every save envelope, the PKSM bank
  import, Pokemon files against PKHeX's own test files, the legality sweep, the self test's own
  suites, and (`uihost/run.sh`) the real screens driven headless. They stub the filesystem layer, so **only `make` proves the `.nro` links.**
- **The self test** is in the development build only (chapter 02). Press `-` on the save picker and
  it opens, edits, converts and writes back one save per title on hardware: an installed Switch
  title's own save, and for a loose-file game the save under `sdmc:/PKSE/tests/saves/<Title>/`. It
  works on copies: nothing the user owns is written.

Run the harnesses whenever a change can be decided from data (a parse, an offset, a conversion),
and the self test before a release.

---

## Testing the `.nro` on a console

Nothing above runs PKSE. Copy the `.nro` to the console and exercise what you changed **in every
game it touches** — a change that is right for Sword can be wrong for FireRed, and the generation
pattern (chapter 04) means a fix in one game's class does nothing for its siblings.

### What to click through

For each game your change can reach, open a save and visit:

- **Party** and **Boxes** — sprites, levels, an empty slot reading as empty (not as a Bad Egg).
- **Items** — every pouch, including counts, adding and removing an item.
- **Trainer info** — name, id, money.
- **The details page** — every row, then edit something and back out.
- **Legality** — `R` in the editor, and the verdict in the box summary panel.
- **Bank** and **Storage** — deposit, withdraw, and the Save/Discard prompt on the way out.

Then save, reopen, and check that what you edited is what you read back — **each field
separately**. *"The save didn't work"* is not a finding; *"the box name persisted and the item count
didn't"* is.

### Loose saves: Y → Open Save File

Game Boy, GBA, DS and 3DS saves are opened by path, not by title: **`Y` on the save picker →
Open Save File**, then browse to the file. Saving writes the file back where it came from, after
PKSE copies the original bytes to `sdmc:/PKSE/FileBackups/<timestamp>_<name>`.

That copy is what makes the strongest check available to anyone: **open a save, change nothing,
save it, and compare the written file with the backed-up original byte for byte** with any diff
tool (`cmp`, `fc /b`, a hex editor's compare). An editor that cannot reproduce a file it did not
change will corrupt one it did, and no checksum catches PKSE rewriting bytes it had no business
touching.

**Byte-identical is the bar.** The documented exceptions — each one confined, and each one a
write the game itself would make:

- **Gen 1: the current box's flush.** The box the player last had open is mirrored outside the box
  banks, and that mirror is the fresh copy; writing a save copies it into its bank (and, on a bank
  never flushed, initialises the other boxes' headers to empty). Only those bytes and the checksum
  may move (chapter 05).
- **Gen 2: the trainer-block mirrors.** Gen 2 keeps a backup of the trainer block — Crystal one
  contiguous copy at `0x1209`, Gold/Silver five separate pieces — and real saves ship with it stale.
  Writing refreshes it from the primary: 211, 250 and 29 bytes on three real saves. Only bytes inside
  those mirrors and the two checksum words may move (chapter 05).
- **Pokedex registration**, in the games whose writer registers everything in storage (the Switch
  games and Let's Go): a stored Pokemon the dex does not show yet gets its flags, such as a Pokemon an
  editor made shiny after it was caught. Only the Pokedex may move, and a second write must change
  nothing; a write that keeps changing it is registering something the game would not.
- **A no$gba container** comes back in different bytes, because its run-length code is coded again;
  the save inside it must still match.

Anything else that moved is a bug. Every save envelope PKSE reads — an emulator RTC footer, VBA
Next's `.srm`, a Switch Online header — is written back around the save, so an enveloped file is
held to the same bar.

---

## What to put in a bug report

1. **Turn on Settings → "Enable Debug Logging"** (off by default), then reproduce the problem.
2. **Attach `sdmc:/PKSE/logs/debug_<date>.log`.** It holds the INFO/ERROR diagnostics and, in the
   same file on purpose, the **`[EVENT]`** lines: one `KEY=value` line for everything the user did —
   every Pokemon added, released, moved, cloned or modified, every item change, every bank
   load/save/discard, the session header and the save commit. An action log is only worth having next
   to the errors it caused: "the save failed" is not a bug report, "the save failed right after a
   traded Zacian moved out of the bank" is.
3. **Say which game, and which save** — and for a loose file, which emulator or dumper produced it.
   If you can share the save (or the backed-up original from `sdmc:/PKSE/FileBackups/`), do.
4. **Say what you expected and what you saw**, one field at a time.

A refusal is logged too (`result=BLOCKED`, `REFUSED_POUCH_FULL`, `no-free-slot`): from the user's
side a refused action looks identical to nothing happening, so the log is how it is told apart.

---

## Which check to reach for

| You changed | Run |
|---|---|
| anything | `syntax_check.py`, then `--warnings` |
| a file the build lists, a source directory, an asset | `make` |
| an offset, a parse, a save writer | an unedited round trip of a real save in that format, compared byte for byte (`tools/harness/build.sh`, or `build-oldgens.sh` for Gens 2 and 4–7) |
| an editable field | edit it, save, reopen, read it back — in every game that has it |
| a legality rule or an encounter table | the legality view over real saves (`tools/harness/build-legality.sh`): near-silence is right, a wall of findings means the table is wrong, not the save |
| a screen, panel, dialog or modal | `tools/harness/uihost/run.sh`, then click through it, by button **and** by touch, with the left stick as well as the D-pad |
| the bank, conversion | deposit and withdraw across generations, then Discard and confirm both sides rewound |
