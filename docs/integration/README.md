# Generation integration guides

One document per pre-Switch generation PKSE added after Gen 1 and Gen 3: the offsets, block tables,
crypto and traps transcribed from PKHeX that the implementation was built from. **Every generation
here is implemented**, in every layer; these documents are the reference to read when you touch one
of them — to check an offset, understand why a codec looks the way it does, or find the trap a
change is about to walk into. **PKHeX is the source of truth throughout**, and the code wins over a
document if the two ever disagree. The offsets were transcribed at PKHeX commit
`6501f0ab46e8f8ca048539dbaf8cae8cb104e722`; `tools/pkhex_source.py` now tracks PKHeX's default branch
rather than that commit (see the last section).

Each document was written as a step-by-step guide *before* its generation existed, so its later
sections ("PKSE integration", "Suggested order") read as a plan. They are kept because the reasoning
in them is still the reasoning in the code; the class and file names they propose are the ones that
landed unless the status note at the top of the document says otherwise.

| Doc | Games | Group(s) | Entity | Save | Implemented in |
|---|---|---|---|---|---|
| [GEN2.md](GEN2.md) | Gold / Silver / Crystal | `GSC` | PK2 | 32 KiB (JP 64 KiB) `.sav` | `Trainer2GSC`, `Pokemon2GSC` |
| [GEN4.md](GEN4.md) | Diamond / Pearl / Platinum / HeartGold / SoulSilver | `DP`, `PT`, `HGSS` | PK4 | 512 KiB, **two partitions** | `Trainer4DP`/`4PT`/`4HGSS`, `Pokemon4*` |
| [GEN5.md](GEN5.md) | Black / White / Black 2 / White 2 | `BW`, `B2W2` | PK5 | 512 KiB, flat block list | `Trainer5BW`/`5B2W2`, `Pokemon5*` |
| [GEN6.md](GEN6.md) | X / Y / Omega Ruby / Alpha Sapphire | `XY`, `ORAS` | PK6 | `main`, BEEF block table | `Trainer6XY`/`6ORAS`, `Pokemon6*` |
| [GEN7.md](GEN7.md) | Sun / Moon / Ultra Sun / Ultra Moon | `SM`, `USUM` | PK7 | `main`, BEEF block table | `Trainer7SM`/`7USUM`, `Pokemon7*` |

Gen 1 (Red/Blue/Yellow) and Gen 3 (Ruby/Sapphire/Emerald and FireRed/LeafGreen) have no document
here; they predate this series and are described in [`docs/manual/`](../manual/) — chapters
[05](../manual/05-save-pipeline.md) and [06](../manual/06-pokemon-entity.md). Let's Go is Gen 7 but a
different format (PB7, a Switch title) and is not covered here either.

---

## How the generations depend on each other

They were built in the order 2 → 4 → 5 → 6 → 7, because the dependency graph is real:

```
Gen 2  ──────────────────────────────  independent; reuses Gen 1 (already in the tree)

Gen 4  ──┬──►  Gen 5      (shares the entity crypt, the shuffle, IV32, the stat formula)
         │
         └──►  Gen 6  ──►  Gen 7   (Gen 4 contributes only crc16ccitt; Gen 6 contributes everything else)
```

- **Gen 2 reuses Gen 1** almost wholesale — the PokeList wrapper, big-endian fields, DVs, Stat
  Experience — and was the second format through the loose-file entry path.
- **Gen 5 builds on Gen 4**: it reuses `Decrypt45`/`Encrypt45`, `crc16ccitt`, and the whole
  32-byte-block shuffle. Read GEN4.md first when touching Gen 5's entity.
- **Gen 7 builds on Gen 6**: Gen 7 is a nine-field delta on Gen 6's entity plus a different
  block-CRC function (CRC16Invert rather than CRC16-CCITT) and MemeCrypto signing. Gen 6/7's entity
  crypt is the same one PKSE uses for Gen 8/9.

---

## What every document contains

The same twelve or thirteen sections, in the same order:

1. **Read this first** — what is genuinely new versus what you already have
2. **Scope** — sizes, limits, groups, at a glance
3. **The save container** — layout, block tables, checksums, detection
4. **Storage** — box geometry, box names, party
5. **Trainer data** — offsets for OT, ID, money, gender, language
6. **The entity** — complete offset map and crypto
7. **Text** — charset or encoding, field widths, terminator policy
8. **Items** — pouch offsets and the on-disk codec
9. **Data tables** — what a `gen_genN.py` must emit and why
10. **PKSE integration** — file by file, as it was planned
11. **Verification** — what an unedited round trip of a real save catches
12. **Trap list** — the things that fail *quietly*
13. **Suggested order** — the build order that front-loaded the risk

---

## The rules that apply to every one of them

These come out of [`docs/manual/`](../manual/), and they are not negotiable when changing any of
these generations:

**The unedited round trip is the gate.** Load a real save, write it straight back with no edits,
diff. It must be byte-identical (Gen 1's current-box flush and Gen 2's trainer-block backup refresh
are the sanctioned exceptions, confined to mirror bytes — see GEN2.md). *An editor that cannot reproduce a file it did not change will corrupt one
it did.* That single test found five bugs in Gen 1 — every one of them PKSE quietly rewriting bytes
it had no business touching.

**Every game gets its own class in every layer.** Even when two games are near-identical. See
[manual ch. 04](../manual/04-generation-pattern.md) for why `Trainer9SV` and `Trainer9LZA`
deliberately duplicate each other, and prefer a clean copy over a mode flag.

**No RTTI, no exceptions.** `-fno-rtti -fno-exceptions`. Dispatch on `getGameGroup()`, then
`static_cast`.

**Decide what a missing table entry means, explicitly.** `nullptr` from a learnset lookup means
*"PKSE has no data"*, not *"learns nothing"* — and the transfer sanitizer **deletes** whatever
`isLearnable()` denies. This is the most common bug class in the codebase
([manual ch. 14](../manual/14-pitfalls.md) §1).

**`0` is a real value.** Nature 0 is Hardy, ball 0 is a real ID. Add a `hasXXX()` predicate for
every field a format lacks, rather than letting a getter return 0 and having the UI invent data.

**Mutate → `recalculateStats()` → `refreshChecksum()`.** In that order. Plus
`hasUnsavedChanges = true` and `mirrorEditedPartyMember()`.

**Generate the big tables, do not transcribe them.** Block tables, character tables, personal
tables. A hand-copied `(offset, length)` produces a CRC that validates over the wrong range, and
nothing will tell you.

**A pair of games with no version byte gets named as a pair.** Gold/Silver, Diamond/Pearl and
HeartGold/SoulSilver share a save layout with no version byte. PKSE made the same call for Red/Blue —
`Trainer1RBY::gameTitle()`, and the reasoning in [manual ch. 05](../manual/05-save-pipeline.md).
**The filename is not a fallback.** Gens 5, 6 and 7 are the exception: their saves carry a game byte
(`PlayerData5 +0x1F`, `MyStatus +0x04`), which `getGameVersion()` reads, so they name the exact
game.

**The ENTRY POINT is what exposes a format to users** — the `LOOSE_FORMATS` row, or the Switch title
id in `getGameVersion(titleId)` — so it is the last thing a new format gets. `isGameSupported` reads
like the switch for this and is not one: nothing consults it, and it is derived from `getGameGroup`.

**Cross-generation conversion is a separate decision from bank storage.** Every generation here is
banked; only Sun/Moon and Ultra Sun/Ultra Moon convert. Gens 4, 5 and 6 are absent from
`Conversion::gate`'s `inFamily`, so a move to or from one is refused. Gen 1 and Gen 2 convert ONE way,
out through the Poke Transporter rule into a PK7, and nothing converts back into them
([manual ch. 10](../manual/10-bank-and-conversion.md)).

---

## Verifying a change to any of them

```bash
python tools/syntax_check.py                    # ~3s   does it compile?
python tools/syntax_check.py --warnings         # ~1min does the optimizer object?
make                                            # does the .nro link?
```

Then open a real save of that generation, write it back unedited and compare the files; edit
something, save, reopen and check it stuck. A party record that stores its battle stats (Gens 4-7)
is a free cross-check of the stat formula and the tables: recompute and compare. See
[manual ch. 12](../manual/12-testing.md).

---

## Re-fetching the PKHeX sources

Every offset in these documents was read out of PKHeX at the commit each one names. To read the
originals:

```bash
python -c "
import sys; sys.path.insert(0,'tools')
from pkhex_source import pkhex_path
print(pkhex_path('Saves/SAV4.cs'))
"
```

Files land in `tools/.pkhex_cache/<sha>/` (gitignored) and are cached, so re-reads are offline.
**PKSE tracks PKHeX's default branch**, resolved to a commit once per run, so a re-fetch today may
return a newer revision than the one these documents were transcribed from — **re-verify an offset
against the file you actually fetched before trusting it.** Nothing here is auto-generated. Set
`PKHEX_REF=<sha>` to read the exact commit a document names.
