# 13 — Adding a game or generation

The complete checklist, in dependency order. **Budget it as a full game implementation, not a
variant of an existing one** — each generation is its own save + entity + crypto family.

Gen 1 is the best worked example to read alongside this (chapter 06 has its rules in full); Gen 3
is the other, and the only one where two groups share a record (FireRed/LeafGreen and
Ruby/Sapphire/Emerald). Both are in the tree and both are complete.

> **For Gens 2 and 4–7 (3DS) the research is written down.** [`docs/integration/`](../integration/)
> has one document per generation with every offset, block table, checksum algorithm and PKSE-side
> file listed — transcribed from PKHeX at the commit each guide names. This chapter is the general
> method; those are the specifics, and a worked record of how each of those generations was added.

## Before you write anything

**Get a real save.** Not a description of one, not a sample from a forum — a dump from a console or
an emulator you can round-trip against. Everything below is verified against it, and writing it
back unedited (chapter 12) is what turns "I think this offset is right" into "the file came back
byte-identical".

**Find PKHeX's implementation.** `SAV<n><Game>.cs` for the save, `PK<n>.cs` for the entity,
`PokeCrypto.cs` for the crypt. Read them before writing. PKHeX is the source of truth for every
offset and algorithm in this tree.

**Decide the suffix.** `<gen><GROUP>`, where `<GROUP>` is the `GameVersion` group's own name, e.g.
`2GSC`, `4PT`, `5BW` — so a class and the value it dispatches on cannot drift. Use it in every
layer, and give the group its own class in every layer even if its record is byte-identical to a
neighbour's (chapter 04 says why).

---

## 1. `GameVersion`

`include/Enums/GameVersion.h`.

```cpp
enum class GameVersion {
    /* … */
    GD = 39, SI = 40, C = 41,     // stored Version bytes (MATCH PKHEX)
    /* … */
    GSC = 78,                     // the new GROUP (next free grouping value; RSE = 91 is the last)
};
```

Then update, in order:

```cpp
getGameVersion(titleId)     // only if it is an installed Switch title. Title IDs come from a
                            // REAL BACKUP, not a database.
getGameGroup(version)       // GD/SI/C -> GSC
getGameVersionName(v)       // both the individual games and the group (name the PAIR/TRIO)
getSaveFileName(group)      // "main" / "savedata.bin" / … — irrelevant for a loose .sav
isGameSupported(version)    // NOTHING TO DO -- derived from getGameGroup(); see below
getGroupRepVersion(group)   // a representative stored version, for table routing
getVersionGeneration(byte)  // the origin bytes -> the generation number
getOriginGameName(byte)     // already covers Gen 1-9; check yours is there
```

> **`isGameSupported` is not a gate and never was.** It once read as one, and seven documents
> including this one told you to flip it last — but nothing has ever consulted it to decide what a
> user can open, so flipping it early exposed nothing and leaving it false hid nothing. That is
> exactly how it sat at twelve Switch titles while twenty-seven pre-Switch ones were implemented
> around it. It is now derived from `getGameGroup()`, so adding your game's group case in step 1
> makes it true for free and there is no separate switch to remember.
>
> **What actually exposes a game to users is the ENTRY POINT**, and that is what to leave until the
> end: a Switch title becomes reachable the moment `getGameVersion(titleId)` returns something other
> than `Invalid`, and a loose file the moment its row lands in `LOOSE_FORMATS`. Add either one only
> when the format is finished.

**Make sure every title routes to a group.** `getGameGroup()` returning `Invalid` is silent: once
Gen 1 had no case there at all, and every Red, Blue and Yellow save was refused as *"a Red/Blue/Yellow
save, not Red"* while the bytes were fine. Adding a title means adding its `getGameGroup()` case.

If your group joins a family that already exists (a second Gen 3 group, say), find every
`group == <the old group>` comparison and turn it into a predicate — `isGen3Group()` exists because
eleven call sites silently treated `== FRLG` as "is Gen 3" (chapter 04).

---

## 2. Encryption (skip if the format has none)

`include/Encryption/Encryption<suffix>.h` + `src/Encryption/Encryption<suffix>.cpp`.

Model it on `Encryption8SWSH`:

```cpp
constexpr size_t BLOCK_COUNT<suffix> = 4;
constexpr size_t SIZE_BLOCK<suffix>  = 0x50;
constexpr size_t SIZE_STORED<suffix> = 8 + (BLOCK_COUNT * SIZE_BLOCK);
constexpr size_t SIZE_PARTY<suffix>  = SIZE_STORED + 0x10;

void      shuffleArray<suffix>(std::span<const std::byte> in, std::span<std::byte> out, uint32_t shuffle);
std::byte* decryptArray<suffix>(std::span<const std::byte> encrypted);       // caller delete[]s
std::byte* encryptArray<suffix>(std::span<const std::byte> decrypted, uint32_t seed);
```

Shared machinery already exists in `Encryption/Encryption.h`: `cryptArray` (the LCG XOR),
`cryptPokemon`, `blockPosition` / `blockPositionInvert` (the shuffle tables).

**Gen 1 has no encryption layer at all.** If your format has none, do not create an empty one — the
absent layer is a real difference, and the codebase already handles it.

---

## 3. The entity

`include/Pokemon/Pokemon<suffix>.h` + `.cpp`. This is the bulk of the work.

Start from the closest existing class and **change every offset deliberately**. A copied class with
one offset left at the sibling's value compiles, runs, and gives plausible wrong answers — so check
every setter by writing a value and reading it back (chapter 06, "The full virtual surface").

### Must implement (pure virtual)

```
speciesID  species  nickname  formID  form  heldItem  id32  exp  ability  nature
statNature  level  gender  genderSymbol  pid  encryptionConstant
ivHP/ATK/DEF/SPE/SPA/SPD  setIV  evHP/…  setEV
baseHP/…  statHPMax  statATK/…
friendship  isEgg  isShiny  isPokerusInfected  isPokerusCured
checksum  calculateChecksum  refreshChecksum  checksumValid
recalculateStats  regeneratePID  setShiny  getGameGroup
```

### Should override where the format differs

```cpp
maxIV(), maxEV(), evWide(), setEVWide()          // if not 31 / 255
hasNature/Ability/HeldItem/Friendship/Ball/MetData/OriginGame/PID/StoredGender()
hasAwakeningValues(), hasPokerus(), hasHyperTraining(), isAlpha(), hasAlphaFlag(), setAlpha()
hyperTrainMinimumLevel(), isHyperTrainingAvailable()   // if not level 100 / not "the field exists"
getMaxNicknameLength(), canStoreNickname()
isStructurallyValid()                            // if there is no checksum
clone()                                          // ALWAYS. The base returns nullptr.
```

### Constructor

```cpp
explicit Pokemon<suffix>(std::span<const std::byte> raw) {
    buffer   = decryptArray<suffix>(raw);   // or a plain copy, if unencrypted
    dataSize = raw.size();
    data     = std::span<std::byte>(buffer, dataSize);
}
```

### The traps, all of which have bitten before

- **Endianness.** Gen 1 is big-endian throughout. A byte-swapped read yields a plausible wrong
  number, not an error.
- **Party tail.** If `level()` / `statXXX()` index into a party-only tail, **anything constructing an
  entity from scratch must allocate the party size**.
- **Capability predicates.** Add one for every field the format lacks. `0` is a real value for
  nature, ability, ball and friendship (chapter 06).
- **`refreshChecksum()` on a format with no checksum.** Repurpose it to restore whatever invariant
  the format does have, so the ~200 existing "mutate then refreshChecksum" call sites stay correct —
  Gen 1 uses it to re-sync the PokeList header.
- **Stat order.** Two different orders exist (chapter 06). Use the named constants.
- **The stat formula itself.** Do not assume the mainline one: Legends: Arceus's is a different
  formula entirely, with no EVs (chapter 06).
- **Size checks.** A group's `isSize*()` accepts only its own two record lengths, never "any length
  of this generation except the other group's".
- **Constants.** A generation-wide constant (`GEN4_SAVE_SIZE` and kin) is a `static constexpr`
  member of the class, not a namespace-scope definition, or two group headers in one translation
  unit redefine it.

---

## 4. Data tables

If the generation's data genuinely differs from the modern tables, it needs its own — and it usually
does. Gen 1 needed all of these, each for a reason that shipped as a bug first:

| Table | Why Gen 1 needed its own |
|---|---|
| base stats | a single **Special** stat (Gyarados is Special 100, not SpA 60 / SpD 100) |
| species index | arbitrary ordering (Bulbasaur `0x99`, Ivysaur `0x09`) |
| type ids | not the modern ones (Bug 7, Ghost 8, specials at 20–26). **Converted at generation time**, so raw numbering never escapes. |
| move PP | Gen 1's own, with PP Ups adding `min(7, base/5)` |
| items | its own id space, names and legal set; TMs are ordinary bag items at 201–250 |
| learnsets | level-up (**Red/Blue UNIONED with Yellow**, since a PK1 cannot say which game it came from) + TM/HM + the Stadium Surf-Pikachu + the personal table's initial moves, then folded with the pre-evolution chain |

Growth rates were **checked** and turned out identical to Gen 3 for all 151, so `Experience.cpp` is
genuinely reused — but the generator still emits the per-species byte and reports any future
divergence. **Check before reusing; do not assume.**

Write one generator (`tools/gen_<suffix>.py`), emit one header + `.cpp` pair with a
`DO NOT EDIT BY HAND` banner, and record provenance and any deliberate deviation from PKHeX in the
module docstring (chapter 07).

Adding a group to an existing generator has two rules of its own:

- **Run every generator whose game list you touched, not just the one you came for — and diff the
  output.** A generator nobody has run in a while may not run at all: `gen_evolutions.py` once threw
  `KeyError: 'GSC'` from the day Gens 1/2 joined its game list, which is why `EvolutionTable.cpp`
  carried no Gen 1, 2, 4, 5, 6 or 7 table. A table that changes where you did not expect it is the
  finding. Regenerate at the PKHeX commit that reproduces today's tables (`PKHEX_REF`) so upstream
  data does not ride along, and compare tables semantically — an index shift moves thousands of
  lines without changing one answer.
- **New groups are APPENDED to `gen_encounters.py`'s `GAMES` list, never inserted.** The tables are
  emitted in that order, so inserting one rewrites every table after it and buries the real change
  in a diff that is the only review the generated data gets.

### Text

If the generation has its own charset, add `include/Utils/<Gen>Text.h` as a sibling of `Gen1Text.h` /
`Gen3Text.h`. **One table per generation, in one place.** The Gen 3 charset once existed as four
independent ad-hoc subsets and the damage was invisible for months.

Note the two existing ones differ in a real way: in Gen 3 a glyphless byte is **skipped**; in Gen 1
it **terminates**.

---

## 5. Inventory

`include/Trainer/Inventory<suffix>.h`. A struct with `fromValue`/`toValue` if items are packed, plus
a `PouchType` enum and a `getPouchInfo` table:

```cpp
struct PouchInfo<suffix> { PouchType<suffix> type; const char* name; int offset; int maxCount; };
inline const PouchInfo<suffix>& getPouchInfo<suffix>(PouchType<suffix> type);
```

Then add the branch to `Panels::pouchDisplayName()` and to `Names::getPouchItems()`.

**Decide the storage model** and report it through `itemsAreIdIndexed()` (chapter 04) — the editor
removes and retypes items in opposite ways for the two models.

---

## 6. The trainer / save class

`include/Trainer/Trainer<suffix>.h` + `.cpp`.

```cpp
class Trainer<suffix> final : public Trainer {
public:
    explicit Trainer<suffix>(std::vector<Save::Block> blocks) : Trainer(std::move(blocks)) {
        party.reserve(MAX_PARTY_SLOTS);
        boxes.resize(BOX_COUNT);
        boxNames.resize(BOX_COUNT);
        for (const auto& b : this->blocks) parseBlock(b);
    }
    // pure virtuals — a missing one is a COMPILE ERROR, which is the point
    void updatePartyBlock() override;
    void updateBoxBlock() override;
    void updateItemBlock() override;
    void updateTrainerInfoBlock() override;
    std::unique_ptr<Pokemon::Pokemon> createBlankPokemon() const override;
    size_t getBoxCount() const noexcept override;
    size_t getSlotsPerBox() const noexcept override;
    size_t getPartySize() const noexcept override;
    GameVersion getGameGroup() const noexcept override;
    // override the defaults only where this game differs
};
```

For a **non-SCBlock** format (a flat blob, GBA sectors, a 32 KiB Game Boy save), the `blocks` vector
may be unused — hold the raw buffer instead and expose `getSaveData()`, exactly as
`Trainer8BDSP`, `Trainer3FRLG` and `Trainer1RBY` do.

### Things to get right

- **`createBlankPokemon()` must not hand raw zeros to the entity constructor.** Synthesise zeroed
  *decrypted* bytes at the **party** size, encrypt with seed 0, construct from those.
- **`updateBoxBlock()` writes the game's own blank for an empty slot**, not zeros, if the format
  encrypts box slots. Gate on `speciesID() != 0`, not on the pointer.
- **`compactStorage()` stays a no-op** unless the game's storage is a genuinely gapless occupancy
  list. Re-packing a positional game shuffles Pokémon the player arranged on purpose.
- **`boxNameDirty`** — write only the names the user changed.
- **`updatePokedexBlock()`** — walk the whole storage at save time, set flags only.

### If the save is a loose file

A pre-Switch save has no title ID and belongs to no account, so:

```cpp
static bool detect(const std::vector<uint8_t>& bytes) noexcept;
```

`detect` must be **conservative**, because it is one link of a probe chain: `Save::openExternalSave`
walks the `LOOSE_FORMATS` table in order and opens the file as the first format whose `detect`
accepts it. The file browser calls that open *before* leaving the picker, so a refusal is explained
with the file still on screen. **Probe order is load-bearing and it is PKHeX's**
(`SaveUtil.GetTypeInfo`): several formats share a size and are told apart only by which offsets hold
well-formed data (Gen 1 and Gen 2 international are both `0x8000`; Gen 4 and Gen 5 both `0x80000`).
Getting the order wrong does not fail — it opens a real save as the wrong game. Where two groups share
a container, each `detect` must refuse the other's file (`Trainer3RSE` and `Trainer3FRLG` are the
model, chapter 06).

Gen 1's `detect` is the model. A Japanese save has 6-byte name fields, 8 boxes of 30 instead of 12
of 20, and every offset from the trainer block onward shifts — and **it is recorded nowhere in the
file**, so `detect` infers it by testing which offsets hold a well-formed Pokémon list, Japanese
first, exactly as PKHeX does. (Its own `detect` takes an `outJapanese` out-parameter; the table row
wraps it.)

---

## 7. The save read/write path

`include/Save/GetSaveFileContents.h` + `.cpp`.

```cpp
Trainer<suffix> readTrainerInfo<Suffix>(const char* backupDir);
bool saveTrainerInfo<Suffix>(Trainer<suffix>&, const char* backupDir,
                             u64 titleId, AccountUid uid, bool injectToTitle);
```

Then:

1. Add a `case` to `readTrainerInfo()`'s group switch.
2. Add an `else if` to `saveTrainerInfo()`'s `trainerGroup` chain.
3. **Add the class to `TrainerVariant`** — *unless* it is a loose-file format, in which case
   deliberately leave it out (as `Trainer1RBY` is), because it never goes through title-ID dispatch
   and adding it would create a state that path can never produce.

The save function's `update*Block()` calls **must all come before** the checksum / hash / encrypt
pass. Copy the order from an existing one.

For a loose-file format the save path is not this chain: add one row to `LOOSE_FORMATS` and one
`case` to `Save::saveExternalSave`'s group switch, both in `GetSaveFileContents.cpp`. That function
takes the **file path**, has no title id, account or inject flag, **backs up the original bytes
before writing** (`Save::backupOriginalFile` — the user pointed PKSE at a file it did not create and
may have no other copy of) and puts back any save envelope the file came in. `saveTrainerInfoRBY` is
the oldest example of the same contract.

---

## 8. Entry point

**Switch title** — add its id to `getGameVersion(titleId)`. That is the whole entry point:
`SaveSelectScreen` lists every title id it recognises and filters on nothing else, so the tile
appears as soon as the id does.

**Loose file** — the `LOOSE_FORMATS` row from step 7 *is* the entry point. The file browser,
`Save::openExternalSave` and `UIManager::handleExternalSave` are generic: the opened trainer is owned
through the base pointer, so nothing in the UI needs a branch per format. If the format's files have
an unusual name, `Save::isSaveFileName` (`include/Save/SaveFileName.h`) decides what the browser
lists.

A loose-file session has `titleId == 0`, which is what `TrainerViewScreen::externalFileSession`
keys on: the save dialog's three normal destinations are meaningless with no backup tree and no live
game save, so it collapses to one — write the file back where it came from.

---

## 9. UI adaptation

Most of the UI needs nothing, because it asks the model rather than assuming. Check:

- **Box geometry.** `boxGridColumns(slotsPerBox)` derives it. A new box size needs a
  `static_assert` in `UI/Common.h` and nothing else, provided rows stay at 5.
- **Capability predicates.** Every details row the format lacks must be gated (chapter 06).
- **Item names.** `Names::getItemNameFor(group, id)` needs a branch for the new id space.
- **Picker kinds.** A new item id space means a new `PickerKind` (`ItemG<n>`, `PouchItemG<n>`) and a
  branch in `pouchPickerKind()`.
- **`gameTitle()`**, if the format cannot say which game of a set it is.

That last one deserves the Gen 1 discussion in full, because it is the model for an honest answer:

> `Trainer1RBY::gameTitle()` is the one place that decides what a Gen 1 save is called, and it can
> say exactly two things. Yellow writes a starter/Pikachu-friendship pair nothing else does, so it
> is detectable; Red, Green and Blue share an engine and an SRAM layout with **no version byte
> anywhere in the 32 KiB**, so nothing tells them apart and the pair is named instead. Both
> references stop in the same place — PKHeX resolves a non-Yellow save to the grouping
> `GameVersion.RB`, and PKSM hardcodes `GameVersion::RD` with the comment *"not even PKHeX tries to
> do better"*.
>
> **The filename is not a fallback**: a 3DS Virtual Console backup is just `sav.dat`, so a
> name-derived guess is a confident wrong answer for everyone whose file is not named the way the
> guesser assumed.
>
> Japanese saves get `Red/Green/Blue` — Green was Japan's third title and Blue the later re-release,
> so `Red/Blue` would omit the likelier game.

Two consequences fall out: `saveRevisionString` must stay `"Base"` (it is the DLC/revision field and
the title bar appends it in parentheses — putting the version there rendered as
`Pokemon Red/Blue  (Red/Blue)`), and the details modal's Origin row names *the save the mon is
sitting in*, dropping to `Gen 1` for a bank record, which has no save context.

---

## 10. Bank support

`src/Trainer/Bank.cpp`:

```cpp
enum : uint32_t { /* … */ BTAG_NEW = 16 };   // ADD-ONLY. Never renumber.
uint32_t bankTagFor(GameVersion group);       // + a case
uint32_t bankTagForEntity(const Pokemon&);    // + a case IF the tag must carry a variant (see Gen 1)
bool     groupForBankTag(uint32_t tag, GameVersion& out);   // + a case
size_t   recordSizeFor(GameVersion group);    // + a case
// plus the per-group record construction + encryption
```

A tag names an entity **format**, not a group. A new group whose record is byte-identical to an
existing one **reuses that tag** — Ruby/Sapphire/Emerald reuse `BTAG_FRLG`, and DP/Pt/HGSS share
one PK4 tag — because the tag is frozen on disk and a record banked before your group existed must
stay readable. It follows that `groupForBankTag()` answers the format's *representative* group, so
**never compare it to the entity's own `getGameGroup()`**; compare `tag == bankTagForEntity(entity)`
or use `Bank::groupAsBanked(group)` (chapter 04).

If the record's *length* carries information the tag must preserve (Gen 1's locale), you need **two
tags**, not a heuristic.

## 11. Conversion (optional, and a separate decision)

Cross-generation transfer is a deliberate scope decision, not an automatic consequence of bank
support. Gens 4, 5 and 6 are banked but **not** convertible: `Conversion::gate`'s `inFamily` does
not list them, so any such drop returns `Unsupported` and `convertForPane` declines it with a
message. Gens 1 and 2 convert **one way only**, through the Poke Transporter step that builds a PK7
(chapter 10); any destination in `RBY`/`GSC` is refused.

If you do want it: add the group to `inFamily`, write the remap to and from the PK8 hub layout, add
a dex-presence rule, and check the conversion in both directions against real Pokemon.

---

## 12. Legality

Layers 1 and 2 mostly work already, because they read the base interface. Check:

- `presenceBit()` in `Legality.cpp` needs a case if the personal table has a bit for the game.
- Any check whose *question* differs for this format needs a branch — see the three-rule EV check
  (chapter 11).
- Layer 3 needs an encounter table, or `getEncounterTable()` returns `nullptr` and it correctly
  emits one Info note and stops.

---

## 13. Verification

- **Make sure every new title routes to a group** (step 1) — an `Invalid` group is silent.
- **Write an unedited real save back until it is byte-identical.** This is the gate. Do not move on
  until it passes. Where the format genuinely must change bytes on an unedited write (Gen 1's
  current-box flush, Gen 2's backup mirrors), confine the change to named regions and check that
  nothing outside them moved.
- **Edit persistence**: change money, the trainer name, a box name, an item quantity and a Pokemon's
  IVs/EVs/nickname; save, re-read, and check each one separately.
- The build checks from chapter 02 (`tools/syntax_check.py`, then `--warnings`) — the build is kept
  warning-free.

---

## 14. Documentation

- **This manual** — the rules for the new format, in the chapter that owns them (chapter 06 for the
  entity, chapter 05 for the save container), in the same style as the Gen 1 and Gen 3 sections:
  what is different, what fails quietly, what a future maintainer will get wrong — and the bug each
  rule came from, with its root cause. Plus chapter 01's game table and anything a recipe now needs
  to say differently.
- **`docs/integration/`** — if the generation has a guide there, keep its offsets in step.
- **`README.md`** — the title-compatibility table.

---

## The order that actually works

1. `GameVersion` — the enum values and the `getGameGroup` case, but **not** the entry point
2. Encryption, if any
3. The entity — and check every setter reads back what it wrote
4. Data tables
5. The trainer class, parse only
6. The read path — open a real save and log what it found
7. **The round trip on an unedited save until it is byte-identical** ← the gate
8. The write path — edit one field, save, reload, verify
9. The entry point — the Switch title id, or the `LOOSE_FORMATS` row
10. UI adaptation
11. Bank, conversion, legality
12. Verification and docs

Steps 7 and 8 are where the bugs are. Everything before them is mechanical; everything after them is
comparatively easy.
