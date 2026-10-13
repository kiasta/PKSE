# 14 — Pitfalls

Every entry here is a real bug this codebase has already had. Read this chapter before shipping
anything.

The general shape, stated once:

> Most of them are the same underlying mistake at different layers — **an assumption that holds
> for the common case and fails silently for the family that does not fit**.

---

## 1. A missing table entry read as a definitive negative

**The single most common bug class in this codebase.**

A lookup that finds nothing returns something that reads as *"no"*: `nullptr`, `0`, `""`, an empty
set. Callers then act on it as a decision.

- An empty move pool is not "unknown moves", it is **"every move is illegal"** — and the bank's
  transfer sanitizer then **deletes all four**.
- `getVersionGeneration()` was missing versions 35–41 (the Virtual Console re-releases) while
  `getOriginGameName()` already named all seven, so a transferred Gen 1/2 mon showed a real game
  beside generation `0` — and `0` is that function's "unknown", which callers read as a negative.
- A `cbrt()` level approximation outlived its real table for months and produced wrong stats **plus**
  bogus legality errors across a whole generation.

**Do:** when adding a lookup, write down what absence means and branch on it separately from a real
negative. Never leave a "simplified" or "approximate" placeholder in a getter once the real table
exists.

---

## 2. A non-null box slot is not an occupied box slot

Several games keep a live entity in **every** slot. Sword/Shield's box writer deliberately stamps one
canonical encrypted blank over each unoccupied slot, because a slot holding leftover bytes that
decrypt to species 0 re-encrypts into a **Bad Egg**.

```cpp
const bool empty = (!pk || pk->speciesID() == 0);      // ALWAYS
```

Testing the pointer alone finds a Pokémon in all thirty slots of an empty box. Getting it wrong once
reported 1849 phantom Pokémon across a set of real saves, and 936 more phantom differences when
those saves were written back unedited.

---

## 3. `0` is a real value

Nature 0 is Hardy. Ability 0 is a real slot. Ball 0 is a real ID. Friendship 0 is a real number.
Dynamax level 0 is real. `AffixedRibbon` 0 is the **Kalos Champion ribbon**, which is why "none" is
`0xFF`.

A getter returning 0 cannot distinguish "none" from "absent". Use the capability predicates
(`hasNature()`, `hasBall()`, `hasPID()`, …), and add one when you add a field a format might lack.

An ungated details row does not look blank — **it invents data**. "Nature: Hardy" on a Red/Blue
Bulbasaur is a fabricated fact sitting in the same column as real ones.

---

## 4. Species alone is not a key. Form is part of it

Regional variants have different stats, abilities, learnsets, types and sprites. **Pass `form`
alongside `species`.**

A whole run of shipped bugs had this shape: *a fact keyed on species when it is really keyed on
species and form* — and one was the exact inverse, a fact keyed on both when it is really keyed on
species alone.

---

## 5. Form 1 is not reliably the regional variant

Form indices are assigned in the order forms joined the *series*, so a species that already had an
alternate form keeps it at 1 and the regional variant lands later.

- **Slowbro**: Normal / Mega / **Galarian**
- **Darmanitan**: Standard / Zen / **Galarian** / Galarian Zen

Before adding a species to a `case formId == 1:` group, check `formCount`. Anything above 2 needs its
own `case`.

When PKHeX and intuition disagree, the tiebreak is `PersonalInfoTable`: the forms have different
abilities, so Slowbro form 1 = Shell Armor proves it is the Mega.

---

## 6. A sprite id is not guessable

PokeAPI form ids are not contiguous per generation and do not follow dex order. **A wrong-but-existing
id renders a completely different Pokémon with no error.** Thirteen species drew another Pokémon
before this was caught.

Resolve against `https://pokeapi.co/api/v2/pokemon/<id>/` and check the returned `name`.
Near-duplicate names are the sharp edge: `10118` is `zygarde-10-power-construct`, while plain
`zygarde-10` is `10181`.

And remember there are **two** lookups: `getFormSpriteId` for variant families,
`getFormSpriteName` for the ~200 peer families PokeAPI keys by name.

---

## 7. Every generation numbers items differently, and the wrong table never errors

**Every id names *some* item in every table**, so the wrong table gives a plausible wrong answer
rather than a blank. Resolving a FireRed bag item through the modern table named Potion (g3 13)
"Dusk Ball" (modern 13) — and only the list panel branched, so the panel and the dialog disagreed on
screen.

```cpp
Names::getItemNameFor(group, id)      // ALWAYS. Never the modern table directly.
```

The same reasoning killed the old `bool g3Items` parameter: it stopped being expressible the moment
a third id space existed.

---

## 8. Origin generation is not storage format

`getGameGroup()` on an entity is the **storage format**. `originGame()` is the **version byte the mon
carries**. A FireRed Rattata in a Shield box is `SWSH` / `4`.

This decides real behaviour:

- **Which met-location table names a location** — `Enums::locationTableVersion(origin, format,
  isEgg)`. A Gen 3/4 met location is remapped into the *current* format's numbering on transfer up,
  so it must be named with the *format's* table. Without this a Platinum starter link-traded to
  Scarlet/Violet reads "(none)".
- **Whether the trainer ID renders as six digits or a 16-bit TID** —
  `usesSixDigitTrainerID(originGame())`. The games pick this from the mon's **origin** generation,
  not the save it lives in.
- **Which encounter table Layer 3 checks against** — the origin's, always.

---

## 9. Forgetting the mutate → recalculate → checksum contract

```cpp
pk->setIV(0, 31);
pk->recalculateStats();      // stat-affecting fields only
pk->refreshChecksum();       // ALWAYS
hasUnsavedChanges = true;    // or the edit is silently lost
mirrorEditedPartyMember();   // or an LGPE party member's edit vanishes on save
```

Each omission is a distinct, silent bug:

| Missing | Symptom |
|---|---|
| `recalculateStats()` | stats disagree with IVs/EVs, and with what the game computes |
| `refreshChecksum()` | the game rejects the Pokémon or shows a Bad Egg |
| `hasUnsavedChanges` | the edit vanishes on exit with no prompt |
| `mirrorEditedPartyMember()` | the edit vanishes on save, **for LGPE party members only** |

---

## 10. Hyper Training makes raw IVs the wrong input to a stat calculation

A Hyper Trained stat is **played** at a maximal IV while the stored IV stays as it was.

Real case: a Hyper Trained Meowscarada stores IV 20 in HP / Def / SpD with flags `0x15`; the game
plays all three as 31 and stores HP 356 / Def 176 / SpD 176. PKSE recomputed from the raw IVs as
345 / 165 / 165 and wrote that back — **eleven points low on each, silently, on any edit that touched
a stat**.

Use `effectiveIV(i)` in every stat calculation. Keep `ivHP()` and friends raw, because that is what
the legality tables match an encounter against and what an IV editor must show and write.

And watch the two stat orders (chapter 06) — they do not line up, and positional indexing across
them produces wrong-but-plausible numbers.

---

## 11. Gen 1 EVs saturate

`evXXX()` on a PK1 returns `sat8(evWide(i))`. A well-trained Gen 1 Pokémon reads **255 in all six
slots**.

Applying the 252/510 rule to that produced **seven Invalid findings on a Pokémon legitimately
trained on a Game Boy**.

Anything that displays, edits or validates an EV must use `evWide()` / `setEVWide()` / `maxEV()`.

---

## 12. Writing a whole array back invents data

Every game's box-name parser substitutes a display default ("Box 3") when the save holds an empty
name, so `boxNames` is a mix of real names and placeholders. Writing the whole array back persists
placeholders the player never typed.

That shipped: an untouched Z-A save written back unedited came out with **215 bytes of invented
names**.

`updateBoxNameBlock()` writes only `boxNameDirty` entries. The general rule: **write back only what
the user changed.**

---

## 13. Zeros are not an empty box slot

For any format that encrypts box slots, an empty slot is an **encrypted blank that decrypts to
species 0** — not literal zeros. Zeros decrypt to garbage and render a **Bad Egg** in every empty
slot.

Harvest the game's own blank from an existing empty slot; synthesise one only if every box is full
(chapter 05).

---

## 14. The party tail is real storage

For PK8/PK9 the last 16 bytes (`0x148`–`0x157`) hold level plus the five battle stats, and `level()`
/ `statXXX()` index straight into them. **Hand one of those classes a stored-size buffer and it reads
past the allocation.**

Anything building an entity from scratch must allocate the **party** size.

---

## 15. `-fsyntax-only` cannot see the warnings that matter

`-Wstringop-overflow`, `-Wmaybe-uninitialized` and `-Warray-bounds` only exist **after** the
optimizer. They are structurally invisible to the default `syntax_check.py` and first appear in a
full build.

```bash
python tools/syntax_check.py --warnings     # ~1 min, non-zero on ANY warning
```

The trap it exists for: GCC inlines every branch before checking, so `resize()` after an `assign()`
elsewhere reads as *appending* to the already-allocated buffer. Build the buffer in one step
(`assign` from a pointer range).

---

## 16. Blocking applets have three rules and breaking any one crashes

1. **Only from `update()`, never `draw()`** — the loop is update → draw → flush, so at `update()`
   time no NanoVG frame is open.
2. **At most one per frame — never chain them.** A library applet cannot be torn down and relaunched
   back to back; it crashes on hardware.
3. **Treat a cancel as "no change", not as an empty string.**

If a tap or a highlight should render before the applet opens, set a `pending*` flag and act at the
top of the next `update()`.

PKSE's own keyboard (chapter 08) is not an applet: rules 1 and 3 bind it too, rule 2 only
`Utils::promptSystemText`, the hand-off to the console's keyboard.

---

## 17. Anything that blocks the frame loop looks like a hang

`appletMainLoop()` must keep being pumped. A long operation must be **chunked** into work sized to a
frame budget and resumed on the next frame, not run to completion inside one `update()`.

If the HOME button stops answering on hardware, something is blocking the loop.

---

## 18. Touch targets are one frame late, and only one overlay owns them

`touchButtons` is populated in `draw()` and hit-tested next `update()`. **Whoever draws last owns the
taps**, which is why every dialog's draw begins with `screen.touchButtons.clear()`.

If a dialog's taps land on the panel behind it, you forgot the clear, or you drew it in the wrong
order.

---

## 19. Draw and handle can drift

Geometry duplicated between a panel's `draw` and the screen's `update` will drift. The Items panel
says so in a comment:

```cpp
// this geometry mirrors the nav math in TrainerViewScreen::update() — keep them in sync
constexpr int rowPitch = 52, tileH = 46;
```

So does the `homeMenuIndex → ViewMode` switch, which **exists twice** (cursor path and tap path).

When you can, derive both from one constant. When you cannot, put a comment on **both** sides naming
the other.

---

## 20. `compactStorage()` must stay a no-op for positional games

Only Let's Go has gapless storage. Re-packing a positional game **shuffles Pokémon the player
arranged on purpose**.

The roadmap once listed Legends: Arceus here. That was wrong: PA8 writes box slots at
`(box*slots + slot) * stride` with a blank record for empties, exactly like Sword/Shield. "Packed" in
the LA code means a byte *stride* with no padding, which is a different thing from a gapless
occupancy list.

---

## 21. Compaction under an open editor freezes the app

The details editor re-resolves its target through `selectedBoxIndex` / `selectedItemIndex` **every
frame**. The creator drops a new mon on whatever slot the cursor is on — in a gapless game, usually a
gap — then opens the editor. Compaction fired on the next frame, slid the mon to the packed
position, and left the editor pointing at an **empty slot**: it drew nothing (the modal bails on a
null target) while still swallowing input, and **the app looked frozen with only B and + alive**.

Hence the five exclusions on the compaction call. Any new modal that resolves its target by index
must be added to that list.

---

## 22. Committing half a transfer

Pressing `+` (quit) is **not** an answer to "save the bank?". Writing the bank on the way out
committed every transfer even when the user then declined the game save, and the two files
disagreed:

- a deposit became a permanent **clone** (bank has it, save still has it)
- a withdrawal a permanent **loss** (bank no longer has it, save was never written)

Ask first, and let Discard rewind both sides.

---

## 23. A failed save that looks like a successful one

This was real. `performSave` now posts a loud status **and re-sets `hasUnsavedChanges`** on failure:

```cpp
postStatus("SAVE FAILED - your changes are still unsaved. Nothing was written.", 480);
hasUnsavedChanges = true;
```

More generally: **`postStatus` is the only way to tell the user anything.** A refusal that is only
logged is indistinguishable from nothing happening.

---

## 24. Block parsing that stops early truncates the save

`parseAllBlocks` stops at the first block that fails to decode and discards everything after it.
`serializeAllBlocks` re-emits only what was parsed, and the save path writes that as the **entire
file**.

Check `outConsumed` against `data_size`.

---

## 25. Item counts must be clamped to the game's cap

An oversized Gen 3 stack **overflows the bag into the key-items pocket**. That is save corruption,
not a cosmetic issue.

`currentItemMaxCount()` is the per-pouch ceiling, and the dialog clamps on entry as well as on edit —
a save edited elsewhere can already hold an over-cap count, so opening the dialog must only ever move
it back into range.

---

## 26. DLC revision is not a content gate

Owning a DLC is **not** a precondition for holding its content. The games gate the DLC *areas*, not
the Pokémon. PKHeX agrees: nothing under `PKHeX.Core/Legality` references `SaveRevision`.

Never use `saveRevision` to decide what a save may **contain**. Its one real constraint is the
Pokédex.

---

## 27. Names carry punctuation the tables must keep

`Ho-Oh`, `Farfetch'd`, `Type: Null`, `Flabébé`, `Nidoran♀`. The font stack draws all of it. A
sanitized species table was a real bug, including in the Gen 3 nickname it fed.

Conversely, **Gen 3 has ~70 glyphs** and Gen 1 fewer, so a perfectly ordinary name can be unstorable.
Ask `canStoreNickname` / `canStoreBoxName` **before** accepting, rather than letting the write path
silently drop characters.

---

## 28. Four copies of a rule is four chances to update three of them

`boxGridColumns()` replaced **six** independent copies of `(slots == 25) ? 5 : 6`. `Gen3Text.h`
replaced **four** independent ad-hoc charsets — and fixing one left the others wrong in *different*
ways, invisibly, because `?` is itself a real Gen 3 character.

When you find the same rule in a second place, that is the moment to hoist it, not later.

---

## 29. A new log sink that forgets the flag

Every sink checks `g_debugLogging`. A new one that does not writes to a user's SD card on a normal
run.

And guard `[EVENT]` call sites with `if (g_debugLogging)` — arguments are evaluated before the sink
can check anything, so an unguarded `describePokemon` pays for dozens of lookups and string joins even
with logging off, times thirty on a multi-select drop.

---

## 30. PKHeX is truth for data, not for editor policy

Take PKHeX as authoritative for offsets, algorithms, form **indices** and name tables.

Do **not** take its editor's permissiveness as a specification. PKHeX exposes megas in its Z-A
dropdown; PKHeX's own tables class every mega as battle-only. PKSE follows the tables.

Where PKHeX itself declines to make a claim — its Let's Go EV rule exists but is **disabled** — PKSE
reports a Warning rather than Invalid. **Match the reference's confidence.**

And where PKHeX's per-game rule answers a subtly different question than you need, deviate — loudly,
in the generator's docstring. `gen_personal.py` excludes Gen 7 Totem forms for exactly that reason
(chapter 07).

---

## The pre-ship checklist

```
[ ] python tools/syntax_check.py
[ ] python tools/syntax_check.py --warnings          (build is kept warning-free)
[ ] make                                             (the .nro links, assets present)
[ ] an unedited save written back is byte-identical  (chapter 12; Gen 1/2 mirror exceptions)
[ ] clicked through on a console, in every game the change reaches

[ ] Does it work for EVERY game?
      Gen 1  — no nature/ability/ball/PID, 20-slot boxes, saturating EVs, big-endian
      Gen 3  — 7-char names, ~70-glyph charset, its own item ids, its own base stats
      LGPE   — AVs not EVs, gapless storage, 25-slot boxes, party stored twice
      BDSP   — flat file, two files to write
      LA     — its own entity layout, no held items
[ ] Every new lookup: what does ABSENCE mean, and is it handled separately?
[ ] Every new UI control: drawn, handled, tappable, in the nav hint?
[ ] Every entity edit: recalc, checksum, unsaved flag, party mirror?
[ ] Every destructive action: logged BEFORE, guarded by g_debugLogging?
[ ] Every refusal: does the USER see it (postStatus), not just the log?
```
