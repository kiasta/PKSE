# 06 — The Pokémon entity

## The shape

`Pokemon::Pokemon` (`include/Pokemon/Pokemon.h`) is an abstract base wrapping a **decrypted byte
buffer** with ~160 virtual members (55 of them pure). Concrete classes read and write raw offsets into that buffer.

```cpp
class Pokemon {
protected:
    std::byte*           buffer = nullptr;   // owned; deleted by the base dtor
    std::span<std::byte> data;               // view over buffer, for bounds-checked-ish access
    size_t               dataSize;
public:
    virtual ~Pokemon() { if (buffer) { delete[] buffer; buffer = nullptr; } }
    Pokemon(const Pokemon&) = delete;
    Pokemon& operator=(const Pokemon&) = delete;
    Pokemon(Pokemon&&) noexcept = default;
    Pokemon& operator=(Pokemon&&) noexcept = default;

    size_t getDataSize() const noexcept { return dataSize; }
    std::span<std::byte>       getData() noexcept       { return data; }
    std::span<const std::byte> getData() const noexcept { return data; }
    virtual std::unique_ptr<Pokemon> clone() const { return nullptr; }
protected:
    Pokemon() = default;
};
```

A derived constructor takes **encrypted** bytes, decrypts them, and points the span at the result:

```cpp
explicit Pokemon8SWSH(std::span<const std::byte> raw) {
    buffer   = decryptArray8SWSH(raw);       // returns new[]'d decrypted bytes
    dataSize = raw.size();
    data     = std::span<std::byte>(buffer, dataSize);
}
```

Accessors are one-liners over offsets:

```cpp
uint16_t speciesID() const noexcept override {
    return readUInt16LittleEndian(reinterpret_cast<const uint8_t*>(data.data() + 0x08));
}
```

`Utils::readUInt16LittleEndian` / `readUInt32LittleEndian` / `writeUInt16LittleEndian` /
`writeUInt32LittleEndian` (in `Utils/HelperUtilities.h`) are the only sanctioned way to touch
multi-byte fields. Do not `memcpy` into a `uint16_t*` — alignment is not guaranteed and Gen 1 is
big-endian anyway.

## Sizes

| Format | Class | Stored | Party | Notes |
|---|---|---|---|---|
| PK1 | `Pokemon1RBY` | 33 | 44 | Wrapped in a **PokeList1** — 69 B international, 59 B Japanese |
| PK2 | `Pokemon2GSC` | 32 | 48 | Wrapped in a **PokeList2** — 73 B international, 63 B Japanese |
| PK3 | `Pokemon3RSE` / `Pokemon3FRLG` | 80 | 100 | Same record byte for byte; party adds 20 battle-stat bytes |
| PK4 | `Pokemon4DP` / `4PT` / `4HGSS` | 136 | 236 (`0xEC`) | |
| PK5 | `Pokemon5BW` / `5B2W2` | 136 | 220 (`0xDC`) | |
| PK6 | `Pokemon6XY` / `6ORAS` | 232 | 260 (`0x104`) | |
| PK7 | `Pokemon7SM` / `7USUM` | 232 | 260 (`0x104`) | |
| PB7 | `Pokemon7LGPE` | 260 | 260 | |
| PK8 | `Pokemon8SWSH` | 328 (`0x148`) | 344 (`0x158`) | |
| PB8 | `Pokemon8BDSP` | 328 | 344 | |
| PA8 | `Pokemon8LA` | 360 | 376 | Own Block B/C/D layout |
| PK9 | `Pokemon9SV` / `Pokemon9LZA` | 328 | 344 | |

```cpp
// Encryption8SWSH.h — the sizes come from the crypt layer, which owns the block geometry
constexpr size_t BLOCK_COUNT8_SWSH = 4;
constexpr size_t SIZE_BLOCK8_SWSH  = 0x50;                                    // 80
constexpr size_t SIZE_STORED8_SWSH = 8 + (BLOCK_COUNT8_SWSH * SIZE_BLOCK8_SWSH); // 328
constexpr size_t SIZE_PARTY8_SWSH  = SIZE_STORED8_SWSH + 0x10;                // 344
```

### A party record's tail is real storage, not padding

For PK8/PK9 the last 16 bytes (`0x148`–`0x157`) hold **level plus the five battle stats**, and
`level()` / `statXXX()` index straight into them.

> **Hand one of those classes a stored-size buffer and it reads past the allocation.** Any code that
> builds an entity from scratch must allocate the **party** size.

PA8, PB7 and PK3 instead derive level from EXP, which is the safer pattern — a box mon there still
reports a correct level. Gens 4–7 keep level and battle stats in the party tail too, which is why
each Gen 4–7 Trainer's `makeEntity()` grows a box record to party size and calls
`recalculateStats()` on load (chapter 05).

This is why the details modal has:

```cpp
// Box mons carry no party-stat block, so level() reads 0 for them in the packed formats
// (SwSh/BDSP/SV/Z-A); fall back to the EXP-derived level (authoritative) for display.
uint8_t subLvl = p->level();
if (subLvl == 0) subLvl = Pokemon::getLevelFromExp(p->exp(), Pokemon::getGrowthRate(p->speciesID()));
```

## The mutate → recalculate → checksum contract

**After mutating any stat-affecting field you must call `recalculateStats()` then
`refreshChecksum()`.** In that order.

```cpp
pk->setIV(0, 31);
pk->setEV(1, 252);
pk->recalculateStats();
pk->refreshChecksum();
```

Stat-affecting means: IVs, EVs, AVs, level, EXP, nature, stat-nature, species, form, hyper-training
flags. Anything that feeds the stat formula. (AVs are Let's Go-only; the default virtuals return 0.)

**Gens 4–7 (outside Let's Go) also keep the checksum fresh inside every setter.** Their `wr8` /
`wr16` / `wr32` helpers recompute it whenever a write lands inside the checksummed range (0x08 to the
stored size). The checksum itself (0x06) and the party tail are outside that range, so this does not
recurse. An edit path that forgot `refreshChecksum()` once left every edited Gen 4–7 Pokémon reading
as a Bad Egg, and the bank dropped it on the next launch. Keep calling `recalculateStats()` and
`refreshChecksum()` anyway: the other formats still need it. `SuiteSetters` checks the checksum after
every probe write in those formats.

**The nature rule is one function.** `Pokemon::natureStatPercent(nature, statIndex)` computes 90, 100
or 110 (raised stat = nature / 5, lowered = nature % 5, in the order Attack, Defense, Speed, Sp. Atk,
Sp. Def). Do not add a per-class nature table: one such copy had Docile lowering Speed.
**Shedinja's HP is always 1** (`Pokemon::hitPointsForBase`, base HP 1), in every generation from 3 on.

`recalculateStats()` for a Gen 3+ mainline format is the standard formula:

```
HP    = ((2*Base + IV + EV/4) * Level) / 100 + Level + 10
Other = (((2*Base + IV + EV/4) * Level) / 100 + 5) * NatureMod / 100      NatureMod ∈ {90,100,110}
```

The real implementation has two subtleties worth copying:

```cpp
// Pokemon8SWSH::recalculateStats()
int hp = ((2*baseHP() + effectiveIV(HT_HP) + (evHP()/4)) * levelValue) / 100 + levelValue + 10;
const uint16_t oldMax = readUInt16LittleEndian(... data.data() + 0x14A);   // BEFORE it is overwritten
writeUInt16LittleEndian(... data.data() + 0x14A, (uint16_t)hp);            // max HP
if (oldMax != (uint16_t)hp)
    writeUInt16LittleEndian(... data.data() + 0x8A, (uint16_t)hp);         // current HP, only if max moved
```

**Current HP is only re-topped when max HP actually changed.** Blindly writing current = max on
every recalculation heals a fainted Pokémon behind the player's back.

```cpp
// effectiveIV, not ivXXX: a Hyper Trained stat is PLAYED at a maximal IV while the stored IV
// stays as it was. Named constants because this array is in STAT-WRITE order (ATK, DEF, SPE,
// SPA, SPD) and the flag bits are in a DIFFERENT one (HP, ATK, DEF, SPA, SPD, SPE).
int ivs[5] = {effectiveIV(HT_ATK), effectiveIV(HT_DEF), effectiveIV(HT_SPE),
              effectiveIV(HT_SPA), effectiveIV(HT_SPD)};
```

**Two different stat orders exist in this codebase and they do not line up.** Positional indexing
across them silently produces wrong-but-plausible numbers. Always use the named constants.

| Order | Sequence | Used by |
|---|---|---|
| Stat-write / accessor order | HP, ATK, DEF, **SPE**, SPA, SPD | `setIV(i,…)`, `setEV(i,…)`, `evWide(i)`, `setAV(i,…)` |
| Hyper-train bit order (PKHeX) | HP, ATK, DEF, **SPA**, SPD, SPE | `hyperTrainFlags()`, `effectiveIV(i)`, `HT_*` |

### Legends: Arceus does not use the mainline stat formula

And it is not a variant of it. PKHeX `PA8.LoadStats`, which `Pokemon8LA::recalculateStats()`
follows:

```
stat = ganbaru(base, iv, gv, level) + scaled(base, level [, nature])
```

- **The IV never enters the base term.** It only biases the ganbaru (effort-level) lookup.
- **There are no EVs.** PLA has GRIT VALUES, 0–10 per stat, at `0xA4`–`0xA9`; the EV bytes at
  `0x26` are inherited struct space the game ignores.
- **The level term scales the base stat directly** rather than `* level / 100`.
- **Nature amplifies only the scaled term**; the ganbaru term is added after it, unamplified.
  Applying the modifier to the sum inflates a boosted stat.

The mainline formula was wrong by roughly 3x at low level — a real level-7 Oshawott stores HP 74
where the old code computed 26 — and it was not cosmetic: the constructor recomputes the stat cache
for every **boxed** PLA Pokemon on load, so every one of them was rewritten with wrong stats.

## Hyper Training

Bottle Caps make the *game* treat a stat's IV as maximal without changing the stored IV. One byte,
one bit per stat, PKHeX's order. The layout is identical everywhere it exists; only the **offset**
moves — PK8/PB8/PK9 `0x126`, PB7 `0xDE`, PA8 `0x13E`.

```cpp
virtual bool    hasHyperTraining() const noexcept { return false; }
virtual uint8_t hyperTrainFlags()  const noexcept { return 0; }
virtual void    setHyperTrainFlags(uint8_t) noexcept {}
virtual uint8_t hyperTrainMinimumLevel() const noexcept { return 100; }        // 50 in 9SV / 9LZA
virtual bool    isHyperTrainingAvailable() const noexcept { return hasHyperTraining(); }

bool    isHyperTrained(int statIndex) const noexcept;   // non-virtual, built on the three above
bool    isHyperTrained() const noexcept;                // any stat at all
void    setHyperTrained(int statIndex, bool on) noexcept;
uint8_t effectiveIV(int statIndex) const noexcept;      // stored IV, or maxIV() if trained
```

This was a real, silent bug: a Hyper Trained Meowscarada storing IV 20 in HP/Def/SpD with flags
`0x15` is played at 31 and the game stores HP 356 / Def 176 / SpD 176. PKSE recomputed from the raw
IVs as 345 / 165 / 165 and wrote that back — eleven points low on each, on any edit that touched a
stat. It surfaced as party Pokémon whose stored battle stats did not match a recalculation.

`ivHP()` and friends stay the **raw stored value**, because that is what the legality tables match an
encounter against and what an IV editor must show and write. `effectiveIV()` is what every stat
*calculation* must read.

`setHyperTrainFlags()` writes the byte and the checksum and **does not** recompute the stats, so a
caller that changes a flag calls `recalculateStats()` itself. The stat editor's Hyper Trained row
does, and it offers to *set* a flag only where PKHeX's `HyperTrainingVerifier` would accept it:
`isHyperTrainingAvailable()` (Legends: Arceus sells no Bottle Caps, so only a Pokémon with a HOME
tracker can carry the flags), at least `hyperTrainMinimumLevel()`, and an IV below `maxIV()`.
Clearing a flag is always allowed, and "Allow illegal values" lifts the gate, as it does the EV caps.

## Checksums, and formats without one

```cpp
virtual uint16_t checksum() const noexcept = 0;
virtual uint16_t calculateChecksum() const noexcept = 0;
virtual void     refreshChecksum() noexcept = 0;
virtual bool     checksumValid() const noexcept = 0;
virtual bool     isStructurallyValid() const noexcept { return checksum() == calculateChecksum(); }
```

`isStructurallyValid()` exists because **"every format carries a checksum" stopped being true** when
Gen 1 arrived. `Bank::load` gates on this virtual, not on `checksumValid()`.

Gen 1 repurposes `refreshChecksum()` rather than leaving it empty:

```cpp
// Pokemon1RBY — nothing here is a checksum. refreshChecksum() is repurposed to restore the
// PokeList1 header invariant, so the established "mutate, then refreshChecksum()" call pattern
// keeps a record well-formed at ~200 existing call sites.
void refreshChecksum() noexcept override { syncListHeader(); }
```

That is the pattern to follow for any future format with no checksum: **make the existing call
pattern do the right thing**, rather than adding an `if (format has a checksum)` at every call site.

## Capability predicates — "0 is a real value"

```cpp
virtual bool hasNature()        const noexcept { return true; }
virtual bool hasAbility()       const noexcept { return true; }
virtual bool hasHeldItem()      const noexcept { return true; }
virtual bool hasFriendship()    const noexcept { return true; }
virtual bool hasBall()          const noexcept { return true; }
virtual bool hasMetData()       const noexcept { return true; }   // met location / level / date
virtual bool hasOriginGame()    const noexcept { return true; }
virtual bool hasPID()           const noexcept { return true; }
virtual bool hasStoredGender()  const noexcept { return true; }
virtual bool hasAwakeningValues() const noexcept { return false; }   // Let's Go only
virtual bool hasPokerus()       const noexcept { return false; }
virtual bool hasHyperTraining() const noexcept { return false; }
virtual bool isAlpha()          const noexcept { return false; }    // PLA / Z-A
virtual bool hasAlphaFlag()     const noexcept { return false; }    // PLA / Z-A
```

These exist because **0 is a real value for every one of those fields**. Nature 0 is Hardy, ability
0 is a real slot, ball 0 is a real ID, friendship 0 is a real number. A getter returning 0 cannot
distinguish "none" from "absent", so an ungated details row *invents data* rather than looking
blank. "Nature: Hardy" on a Red/Blue Bulbasaur is a made-up fact sitting in the same column as real
ones.

Defaults are `true` for the fields every modern format has and `false` for the ones only some
formats have, so adding a predicate never changes existing behaviour.

**Any UI row, legality check or conversion step that reads one of these fields must gate on the
predicate.** From the details modal:

```cpp
if (p->hasAbility())    editRow("Ability",    getAbilityName(p->ability()), 15);
if (p->hasFriendship()) editRow("Friendship", std::to_string(p->friendship()), 16);
if (p->hasBall())       editRow("Ball",       Enums::getBallName(p->ball()), 19);
```

`hasStoredGender()` is subtly different from the rest: Gen 1 stores no gender, but what a Pokémon
*becomes* on transfer is derived from its Attack DV against the species ratio — so `gender()` has a
real answer while `setGender()` has nothing to write to. The editor shows the row **read-only**
rather than offering a picker whose every choice is silently discarded.

## IV / EV width is per format

```cpp
virtual uint8_t  maxIV() const noexcept { return 31; }
virtual uint16_t maxEV() const noexcept { return 255; }
virtual uint16_t evWide(int statIndex) const noexcept;          // default: forwards to evXXX()
virtual void     setEVWide(int statIndex, uint16_t value) noexcept;  // default: clamps into setEV()
```

Gen 1 overrides all four:

```cpp
uint8_t  maxIV() const noexcept override { return 15; }      // 4-bit DVs
uint16_t maxEV() const noexcept override { return 65535; }   // 16-bit Stat Experience
uint16_t evWide(int statIndex) const noexcept override;
void     setEVWide(int statIndex, uint16_t value) noexcept override;
uint8_t  evHP() const noexcept override { return sat8(evWide(0)); }   // SATURATES
```

> **Anything that displays, edits or validates an EV must use `evWide()` / `setEVWide()` /
> `maxEV()`.** The 8-bit accessors saturate, and 300 Stat Exp truncated to 8 bits reads as 44, which
> is worse than 255. This is not cosmetic: the legality checker used to read a saturated 255 as an
> illegal EV and report seven Invalid findings on a legitimately trained Gen 1 Pokémon.

## The Gen 1 entity, in brief

`Pokemon1RBY` wraps a **PokeList1 single-entry list**, not a bare record — 69 bytes international,
59 Japanese (`3` header + `44` party body + two name fields of 11 or 6). That wrapper is what the
games themselves store and the only *self-describing* form: the names live outside the body, and the
record's **length** is the only thing that says which locale it is.

```cpp
inline constexpr size_t SIZE_1STORED = 33;   // box body
inline constexpr size_t SIZE_1PARTY  = 44;   // party body
inline constexpr size_t SIZE_1ULIST  = 69;   // 3 + 44 + 11 + 11   international
inline constexpr size_t SIZE_1JLIST  = 59;   // 3 + 44 +  6 +  6   Japanese

bool   japanese() const noexcept { return dataSize == SIZE_1JLIST; }
size_t nameLen()  const noexcept { return japanese() ? 6u : 11u; }
```

Five things differ from every other format here, and each is a trap if you carry a habit across:

1. **Every multi-byte field is BIG ENDIAN.** A byte-swapped read yields a plausible wrong number,
   not an error.
2. **No encryption and no checksum.** Nothing validates a bad parse — which is why round-tripping
   real saves (chapter 12) matters more here than anywhere else. `refreshChecksum()` is repurposed
   to re-sync the list header, and `isStructurallyValid()` is what `Bank::load` gates on (above).
3. **DVs are 4 bits, and HP's is DERIVED** from the low bits of the other four. Not stored, not
   settable. `maxIV()` reports 15.
4. **There are no EVs.** Gen 1 has Stat Experience: 16 bits per stat, cap 65535, no combined
   total, contributing through a square root — see `evWide()` above.
5. **Special is ONE stat.** Both the SpA and SpD slots read and write the same field.

Offsets are PKHeX's `PK1.cs` / `PokeList1.cs` / `GBPKM.cs`.

The tables, the save container and the facts that fail quietly are in "Rules: Gen 1 (PK1)" at
the end of this chapter.

Shininess is not a Gen 1 concept, but the DV pattern that makes a transferred mon shiny is
checkable, so `isShiny()` answers it (as PKHeX does). Read it as *"will be shiny when it leaves"*.

## Names and text

Nicknames and OT names are `std::u16string` in the interface, whatever the format stores:

```cpp
virtual std::u16string nickname() const = 0;
virtual void setNickname(const std::u16string& value) noexcept {}
virtual int  getMaxNicknameLength() const noexcept { return 12; }
virtual bool canStoreNickname(const std::u16string& value) const noexcept { return true; }
virtual bool isNicknamed() const noexcept { return false; }
virtual void setIsNicknamed(bool value) noexcept {}
virtual std::u16string otName() const { return std::u16string(); }
virtual std::u16string htName() const { return std::u16string(); }
```

`canStoreNickname()` is the important one. The Switch keyboard happily produces accents, CJK and
emoji. Gen 8/9 store UTF-16 and take essentially anything; **Gen 3 has a ~70-glyph table**, so a
perfectly ordinary-looking name can be unstorable there. The UI must ask **before** accepting rather
than letting the write path silently drop characters:

```cpp
const std::u16string want = Utils::utf8ToUtf16(kr.text);
if (!pokemon->canStoreNickname(want)) {
    postStatus("This game can't store one of those characters.");
} else {
    pokemon->setNickname(want);
    pokemon->setIsNicknamed(true);
    hasUnsavedChanges = true;
    mirrorEditedPartyMember();
}
```

Conversion helpers live in `Utils/StringHelpers.h`: `utf16ToUtf8`, `utf8ToUtf16`, `getString`,
`setString`, `loadString`.

### Legacy charsets

- **Gen 3: `include/Utils/Gen3Text.h`** — `GEN3_EN` plus `gen3ToChar` / `charToGen3`, PKHeX's
  `StringConverter3.G3_EN` verbatim. GBA saves use a bespoke single-byte encoding, not ASCII: `A` is
  `0xBB`, the apostrophe `0xB4`, the gender signs `0xB5`/`0xB6`.

  > This once existed as **four independent ad-hoc subsets** covering only `A-Z 0-9 ! ? . -`, in
  > `Pokemon3FRLG`, `Trainer3FRLG` (twice) and `Conversion/Convert`. Fixing one left the others
  > wrong in *different* ways: names were written correctly and read back through a copy that
  > substituted `?`, which is itself a real Gen 3 character, so the damage was invisible.
  > **Route all new Gen 3 name code through the shared header.** What legitimately differs per call
  > site is the *field* policy — who writes a terminator, who preserves bytes after it — not the
  > alphabet.

- **Gen 1: `include/Utils/Gen1Text.h`** — two tables (EN and JP; the record width picks). Unlike
  Gen 3, **a glyphless byte terminates rather than being skipped**. Byte `0x5D` in the first
  position is not a character: it marks an in-game-trade OT, which is legality-relevant.

## Creating an entity from scratch

`buildDefaultMon()` in `TrainerViewScreen.cpp` is the reference implementation and shows the full
sequence. The essentials:

```cpp
auto p = tr.createBlankPokemon();              // generation-correct, checksum-valid, species 0
p->setSpecies(species);
p->setForm(form);
p->setPID(Utils::rand32());
p->setEncryptionConstant(
    Pokemon::correctEncryptionConstantForForm(species, form, p->encryptionConstant()));
p->setLevel(startLevel);                       // FRLG starts at 5, everything else at 1
p->setNature(...); p->setAbility(...); p->setBall(...); p->setLanguage(...);
p->setOTName(...); p->setId32(tr.ID32); p->setOTGender(tr.trainerGender);
p->setMetDate(...); p->setOriginGame(version);
p->recalculateStats();
p->refreshChecksum();
```

Two traps this function documents:

**`AffixedRibbon`'s "none" value is `0xFF`, not `0`.** Zero is a real index — the Kalos Champion
ribbon — so any path that leaves the byte at its zero-initialised default hands the mon a ribbon it
does not own. This has bitten twice, from two different directions (the creator, then cross-gen
conversion), so the offset lives in one place:

```cpp
// Conversion/Convert.h
size_t affixedRibbonOffset(Enums::GameVersion group) noexcept;   // 0 for PK3 and PB7 (no such field)
inline constexpr uint8_t AFFIXED_RIBBON_NONE = 0xFF;
bool normalizeAffixedRibbon(Pokemon::Pokemon& pk);   // repairs it in place, re-checksums if changed
```

**Allocate the party size, not the stored size** — see the party-tail note above.

## Form handling

Three different questions, three different answers, and conflating them is a bug:

| Question | Answered by |
|---|---|
| What is this form **called**? | `Names::getFormName(species, form)` — names *every* form a buffer can hold, battle-only included |
| What does this form **look like**? | `FormSpriteMapping` — draws every form that has art |
| May this form be **selected**? | `Pokemon::isBattleOnlyForm` (`include/Pokemon/FormInfo.h`), via `buildFormPickerOrder` |

The details view has to render what is actually there, so naming and drawing are unfiltered — an
unnamed form showing as "Form N" is how a wrong index hides. Only *selection* is filtered.

**The picker offers permanent forms only, with no override.** `g_allowIllegalEdits` does **not**
lift it: that setting is for values the games can hold but shouldn't (a 255 EV, an off-dex species),
whereas a temporary form gets overwritten by the game regardless, so offering one would misrepresent
what was saved. Mega Evolution and Primal Reversion count as temporary in **every** game, Z-A
included — PKHeX exposes megas in its Z-A dropdown, but that is editor permissiveness, and PKHeX's
own tables class every mega as battle-only. **Take PKHeX as truth for form indices, not for editor
policy.**

The mon's current form is always listed even when temporary, so an existing one stays visible and
reversible.

`formCount` in `PersonalInfoTable` counts *defined* forms, not *storable* ones.

## What the PID decides, and changing it

From Gen 3 to Gen 5 the PID is more than an encryption key. It decides:

| | Shiny | Gender | Nature | Ability slot |
|---|---|---|---|---|
| Gen 3 | PID ⊕ trainer ids < 8 | low byte vs. the species' ratio | `PID % 25` | bit 0 |
| Gen 4 | the same | the same, **and** stored at `0x40`; PKHeX requires the two to agree | `PID % 25` | bit 0 |
| Gen 5 | the same | the same, also stored | stored at `0x41`, except a Pokémon from Gens 3–4 (`PID % 25`) | bit 16 (bit 0 for one from Gens 3–4) |
| Gen 6 on | PID ⊕ trainer ids < 16 | stored | stored | stored |

So a setter that has to change the PID must hold everything it was not asked to change. Gen 3 does it
in `Pokemon3FRLG::rerollPID`, which also holds an Unown's letter. Gens 4 and 5 call
`Pokemon::findPID` (`include/Pokemon/PIDSearch.h`) through each class's own `rerollPID`, and write the
stored gender to match the new PID. Gens 6 and 7 use the same function to keep shininess. Each setter
then pins the one thing it is changing:

- `setShiny` pins shininess. `setNature` (Gen 4, and Gens 5–7 for a Pokémon from Gens 3–4) pins the
  nature. `setGender` (Gens 4 and 5) pins the gender, and only rebuilds the PID when the current one
  gives the other gender.
- `setAbilityNumber` (Gens 4 and 5) pins the PID's ability bit and writes that slot's ability id, as
  PKHeX's `RefreshAbility` does. In Gen 5, slot 4 sets the hidden-ability flag (0x42 bit 0).
- In Gens 6 and 7, `setShiny` and `setNature` on a Pokémon from Gens 3–5 rebuild the PID and set the
  encryption constant to the same value (PKHeX `PKM.SetShiny`).
- Every reroll also holds which line a Wurmple evolves into (`(PID >> 16) % 10 / 5`, PKHeX
  `WurmpleUtil`). In Gen 5 it also holds the PID/ID correlation bit when the current PID has it.
- `regeneratePID` is a touch-up that was asked to change nothing a user can see, so it holds all of
  them. A Gen 6/7 Pokémon from Gens 3–5 keeps its PID: its encryption constant *is* that PID.

**The PID is constructed, not drawn at random until one fits.** `findPID` walks the low half from a
random start and, when shininess is wanted, derives the high half inside the shiny window, so it finds
a PID whenever one exists. Gens 4 and 5 used to draw a million random PIDs and give up silently, which
happened about once in eleven Make Shiny presses (1 PID in 409,600 met all three of their tests). The
draws also ignored the gender, and about one shiny in eight of a 7:1 species came out with a PID that
contradicted its stored gender.

**Nothing rerolls the PID after an IV or EV edit.** The stat editor used to call `regeneratePID`
"to maintain legality". A new PID cannot restore a PID–IV link the edit broke, and in Gens 4–7 the
random replacement turned every shiny Pokémon plain on its first IV edit. An IV or EV edit now changes
that IV or EV and nothing else.

## Fields one setter keeps in step with another

Some fields are derived from another one, by the game or by PKHeX's checks. The setter of the source
field writes the derived one too, so no caller has to know:

- **Gen 1 `setExp`** writes both level bytes (the box record's and the party record's), as PKHeX's
  `PK1.Stat_Level` does.
- **Gen 2 names** always end in one 0x50, even in a zero-padded field: the Game Boy's text printer
  does not stop at 0x00.
- **Gen 3 `refreshChecksum`** clears the Bad Egg flag (0x13 bit 0), `setSpecies` sets or clears the
  has-species flag (bit 1), and clearing the egg flag on the placeholder egg restores the species
  name and the save's language.
- **Gen 4 `setBall`** splits the ball between 0x83 and 0x86 as PKHeX's `G4PKM.Ball` does (see
  `docs/integration/GEN4.md` §5.5).
- **Gens 4–7 names** store the half-width ♂/♀ codes when the text is half-width (PKHeX's
  `StringConverter4/5/6` `UnNormalizeGenderSymbol`) and read them back as ♂/♀.
- **Gens 6 and 7 `friendship()`** is the current handler's: 0xCA while the OT holds the Pokémon,
  0xA2 after a trade (PKHeX `G6PKM.CurrentFriendship`), as in Gen 8.
- **Let's Go `setSpecies`/`setForm`** recompute the stored height and weight floats through
  `Pokemon7LGPE::heightAbsoluteFor` / `weightAbsoluteFor`. Those floats are compared bit for bit, so
  the build passes `-ffp-contract=off` (no fused multiply-add) and the ratios are unfused.
- **Scarlet/Violet and Z-A `setMetLevel`** keep the obedience level (0x11F) equal to the met level
  while the Pokémon is untraded, and at least the met level after a trade.
- **Z-A `setMove(slot, 0)`** gives the empty slot 35 PP and no PP Ups (PKHeX `PA9.FixMoves`).

## The full virtual surface, grouped

For orientation when implementing a new format. Pure virtual (`= 0`) means you must implement it.

**Identity** — `speciesID`\*, `species`\*, `nickname`\*, `formID`\*, `form`\*, `heldItem`\*, `id32`\*,
`exp`\*, `ability`\*, `nature`\*, `statNature`\*, `level`\*, `gender`\*, `genderSymbol`\*, `pid`\*,
`encryptionConstant`\*, `getGameGroup`\*

**Stats** — `ivHP/ATK/DEF/SPE/SPA/SPD`\*, `setIV`\*, `evHP/…`\*, `setEV`\*, `maxIV`, `maxEV`,
`evWide`, `setEVWide`, `avHP/…`, `setAV`, `hasAwakeningValues`, `baseHP/…`\*, `statHPMax`\*,
`statATK/…`\*, `statHPCurrent`, `setStatHPCurrent`, `recalculateStats`\*

**Moves** — `move`, `setMove`, `movePP`, `setMovePP`, `movePPUps`, `setMovePPUps`, `relearnMove`,
`setRelearnMove`

**Trainer / origin** — `tid16`, `sid16`, `setId32`, `otGender`, `otFriendship`, `language`, `ball`,
`originGame`, `otName`, `htName`, `htGender`, `htFriendship`, `currentHandler`

**Met / egg** — `metLocation`, `metLevel`, `eggLocation`, `metYear/Month/Day`, `eggYear/Month/Day`,
`isEgg`\*, `setEgg`, `isFatefulEncounter`

**Misc** — `friendship`\*, `isShiny`\*, `setShiny`\*, `regeneratePID`\*, `isPokerusInfected`\*,
`isPokerusCured`\*, `setPokerus`, `hasPokerus`, `isAlpha`, `hasAlphaFlag`, `setAlpha`, `abilityNumber`,
`setAbilityNumber`

**Integrity** — `checksum`\*, `calculateChecksum`\*, `refreshChecksum`\*, `checksumValid`\*,
`isStructurallyValid`

\* = pure virtual.

Defaults for the non-pure ones are chosen so **"not overridden" is the safe answer**: getters return
0/false, setters are no-ops. That is also what makes a setter checkable: write a value, read it back
— the written value is a pass, the *previous* value means the format has no such field, and
**anything else is a failure**, because a partial write is never right.


---

## Rules: Gen 1 (PK1)

The entity's five traps are above ("The Gen 1 entity, in brief"). This section is the rest: the
tables, the save container and the facts that fail quietly. Offsets are PKHeX's `PK1.cs` /
`PokeList1.cs` / `GBPKM.cs` for the entity and `SAV1.cs` + `SAV1Offsets.cs` for the save.

**What Gen 1 lacks is reported through the `hasXXX()` predicates** (`hasNature`, `hasAbility`,
`hasHeldItem`, `hasFriendship`, `hasBall`, `hasMetData`, `hasOriginGame`, `hasPID`,
`hasStoredGender`), all defaulting true — see "Capability predicates" above for why a getter
returning 0 is not an answer.

### Gen 1 needs its own tables, and gets them

`tools/gen_gen1.py` → `Gen1Tables.h/.cpp`, for four reasons, each of which shipped as a bug before
the table existed:

- **Base stats have a single Special** (Gyarados is Special 100, not SpA 60 / SpD 100).
- **The species index is arbitrary** (Bulbasaur `0x99`, Ivysaur `0x09`).
- **ROM type ids are not the modern ones** (Bug 7, Ghost 8, the special types at 20–26). They are
  converted at generation time, so raw numbering never escapes the generator.
- **Move PP is Gen 1's own**, with PP Ups adding `min(7, base/5)`.

Growth rates were **checked** and are identical to Gen 3 for all 151, so `Experience.cpp` is
genuinely reused — the generator still emits the per-species byte and reports any future divergence.

Two table facts that fail quietly:

- **PKHeX's `personal_rb` is not the raw ROM table.** Byte `0x00` is a ROM dex number, but PKHeX
  **substitutes a gender ratio** there (Bulbasaur 31, Gyarados 127, Mew 255) because Gen 1 has no
  gender data. That is where `genderRatio` comes from, and it is only for the transfer path.
- **A species' first moves are in the PERSONAL table (`0x0F`), not the learnset.** `lvlmove_rb.pkl`
  for Bulbasaur starts at level 7 — Tackle and Growl live in the personal entry. Omitting them makes
  every Gen 1 Pokemon's starting moves read as unlearnable, and the transfer sanitizer *deletes*
  whatever `isLearnable()` denies. The RBY learn pool is level-up (**Red/Blue UNIONED with
  Yellow**, since a PK1 cannot say which game it came from) + TM/HM + the Stadium Surf-Pikachu +
  those initial moves, then folded with the pre-evolution chain.

**Items are Gen 1's own id space** with its own names and legal set (`Pokemon::getItemNameGen1` /
`isItemLegalGen1`, `Names::getPouchItems(RBY, ...)`); Gen 1 TMs are ordinary bag items at 201–250,
which the modern numbering has nothing at. **`Names::getItemNameFor(group, id)` is the one place
that picks a table** — every generation names every id, so the wrong table gives a plausible wrong
answer rather than a blank.

### Verifying Gen 1 against real data

A PK1 party record *stores* the five battle stats the game computed, so recomputing them from the
tables, the DV unpack and the Gen 1 formula and comparing proves the whole chain at once. Against a
real bank of Gen 1 Pokemon the stats, a byte-exact round trip, name re-encoding and learn pools all
check out, and the legality analyzer produces **no** Invalid finding on a Pokemon caught on real
hardware.

**Stored party stats are a BAND, so compare on a CLONE.** They lag the formula by the Stat
Experience gained since the last level-up. Recomputing the save's own entity to compare rewrote
Blue's Charizard's Defense — which a later write-back then blamed on the save layer.

### The Gen 1 save: `Trainer::Trainer1RBY`

32 KiB, with several properties no later generation has:

- **Locale changes the LAYOUT, not just the text.** A Japanese save has 6-byte name fields, 8 boxes
  of 30 instead of 12 of 20, and every offset from the trainer block onward shifts. It is recorded
  nowhere: `detect()` infers it by testing which offsets hold a well-formed Pokemon list, Japanese
  first, exactly as PKHeX does.
- **Only YELLOW is nameable.** `Trainer1RBY::gameTitle()` can say exactly two things. Yellow writes a
  starter/Pikachu-friendship pair nothing else does; Red, Green and Blue share an engine and an SRAM
  layout with **no version byte anywhere in the 32 KiB**. Both reference implementations stop in the
  same place — PKHeX resolves a non-Yellow save to `GameVersion.RB`, and PKSM hardcodes
  `GameVersion::RD`. **The filename is not a fallback**: a 3DS Virtual Console backup is just
  `sav.dat`. Japanese saves get `Red/Green/Blue` — Green was Japan's third title and Blue the later
  re-release, so `Red/Blue` would omit the likelier game. Two consequences: `saveRevisionString`
  must stay `"Base"` (it is the DLC/revision field and the title bar appends it in parentheses —
  putting the version there rendered as `Pokemon Red/Blue  (Red/Blue)`), and the details modal's
  Origin row names *the save the mon is sitting in*, dropping to `Gen 1` for a bank record.
- **The checksum is ONE byte** — `~(sum of 0x2598..checksumOfs-1)` — covering the trainer block and
  everything after, so a party edit changes it.
- **The current box is stored TWICE.** Boxes live in two banks (`0x4000`, `0x6000`), but whichever
  box the player last had open is *also* mirrored at `currentBox`, and that copy is the fresh one —
  the game only flushes to the bank on a box change. Read the wrong one and the player's most recent
  box reads stale. Both are written back, matching PKHeX's `GetFinalData`. The `boxesInitialized`
  bit (0x80 of the current-box index) says whether the bank has ever been flushed; before that it
  holds uninitialised memory, not empty boxes.
- **Boxes are 20 wide, not 30, and are drawn 4x5 CENTRED** (a Japanese save holds 30 and comes out
  6x5). `UI::boxGridColumns()` is the single source; chapter 04 ("Box geometry") has the centring
  rule and why stretching is wrong.
- **A box record is 33 bytes, a party record 44.** The missing 11 are level + the five battle stats,
  which the game recomputes when a Pokemon leaves the PC — so box mons are grown to party size and
  recalculated on load.
- **A counted list entry PKSE cannot read is kept, and it still takes a slot.** Gen 1 and Gen 2
  (`Trainer2GSC` too) store the party and every box as a *counted* list, and the games and PKHeX's
  PokeList1/PokeList2 keep every counted entry whatever it holds — MissingNo., a Gen 2 species past
  251 that is not the egg marker. PKSE reads such an entry as a null slot and carries it as an
  `UnmappedListEntry` (raw bytes, written back verbatim at its position by `orderListEntries`).
  Null is not free: the entry is written back there, so it counts toward the list's capacity.
  `isBoxSlotUnmapped(box, slot)` names a box slot that holds one, and `getUnmappedPartySlots()` the
  party slots (party slot N is not `party[N]` once one precedes it: use `partyIndexAtSlot`). The UI
  locks those box slots through `storageSlotLocked` and draws them as a "?" placeholder labelled
  "Unknown species"; the Party view shows the entry in its own slot. When a slot read as empty, a
  Pokemon dropped on it overfilled the list and the last entry was cut on save.
  `orderListEntries` still keeps what fits and logs an error if a caller hands it more, and Gen 2's
  `compactStorage()` leaves an overfull box unpacked rather than drop anything. Gen 1 has no
  `compactStorage()`: its view keeps holes, and the write packs them (kept entries included) as
  the game's list requires.

**The save layer is verified by round-tripping real saves, not by inspection.** Load a `.sav`,
write it back unedited, diff: an editor that cannot reproduce a file it did not change will corrupt
one it did. That caught five bugs, every one PKSE quietly rewriting bytes it had no business
touching:

1. item quantities clamped to the game's cap of 99 (a real save carried 158 and 203);
2. `recalculateStats()` writing a level byte a party record leaves stale;
3. an empty name field re-terminated with `0x50` where the game left `0x00`;
4. `boxesInitialized` asserted on a brand-new save;
5. list markers filled to capacity where the games write exactly one cap byte.

**The one change an unedited write may make is the current box's FLUSH** — the write-both above.
When the game left the bank copy behind, PKSE flushes the mirror into it (and, on a bank never
flushed, initialises the other box headers to empty). It is provably confined — on two real saves,
exactly the bank slot of the current box, 2 and 57 bytes — and it cannot lose data, since the game
reads the mirror for that box. `Trainer1RBY::currentBoxFlush()` reports exactly those bytes, so the
claim is checkable rather than an excuse for a diff: the change must be confined to them, and
afterwards the bank copy must equal the live one.

### Gen 1 and Gen 2 transfer out, and never back

**Gen 1 and Gen 2 leave through Poke Transporter**, which builds a Gen 7 record (chapter 10). Gens
4, 5 and 6 are absent from `Conversion::gate`'s `inFamily`, and any destination in `RBY`/`GSC` is
refused, so such a drop returns `Unsupported` and `convertForPane` declines with a message.
Depositing any of them into PKSE's own bank is fine — that is storage, not conversion.

---

## Rules: Gen 3's two groups

`FRLG` is FireRed/LeafGreen, a **Switch** title arriving with a title id — and a GBA file too.
`RSE` is Ruby/Sapphire/Emerald, **loose GBA files** only. They share the PK3 record byte for byte,
the item id space, the 386-species dex, the five-digit trainer id, the twelve Gen 3 balls, the
TM/HM list and the crypt — and differ only in save-container offsets. A GBA FR/LG file names the
pair, as Ruby/Sapphire do.

**Ask `Enums::isGen3Group(group)`, never `group == GameVersion::FRLG`** — chapter 04 has the rule
and the bugs the comparison caused.

Four things about `RSE` that bite anyone assuming FireRed's shape:

- **Ruby and Sapphire have no security key.** PKHeX's `SaveBlock3SmallRS.SecurityKey` is a literal
  `0`, so their money and item counts are plaintext, while **Emerald** keys both from `Small+0xAC`
  (*not* `Small+0xF20`, where FR/LG keep theirs). XOR with a key of 0 is a no-op, so code written
  and tested on Ruby alone looks perfect and then reads Emerald's money as 1.7 billion.
  `Trainer3RSE::resolveLayout()` settles it once, at load; everything downstream reads `saveLayout`.
- **That same word is the only thing separating RSE from FR/LG.** Both are 128 KiB with the same
  fourteen rotated sectors; FR/LG write a fixed `1` at `Small+0xAC` and nothing else does, which is
  the whole of `Trainer3RSE::detect()` — `Trainer3FRLG::detect()` is the same with its last
  comparison inverted. Both groups sit in `LOOSE_FORMATS` between Gen 2 and Gen 4 (PKHeX's order),
  and each `detect()` refuses the other's file rather than open it with the wrong offsets, so the
  two rows can never both match. A detector that merely said "this is Gen 3" would open a Ruby save
  with Kanto's offsets and show a plausible, wrong trainer, so the discriminator has to be checked
  both ways.
- **`Encryption3RSE.h` forwards to `Encryption3FRLG` rather than copying it** — the one deliberate
  exception to the copy-per-group rule (chapter 04).
- **Emerald names itself; Ruby and Sapphire cannot.** Emerald is the only one with a security key.
  Ruby and Sapphire are byte-identical with no version byte, exactly like Red/Blue, so
  `getGameVersion()` claims the first of the pair.

**Two Gen 3 party-record bugs were fixed here, and both had been shipping in FireRed.** Same mistake
in two shapes — writing bytes PKSE was not asked to write — and only an unedited round trip of a
real save could have found either:

- **An empty Gen 3 party slot is NOT all zeros.** It is zeros with `0xFF` at `+0x55`, the mail-id
  sentinel for "no mail". `updatePartyBlock` stamped a zeroed blank, writing mail #0 into slots
  that have none. Measured in real Ruby, Emerald and FireRed saves, not assumed.
- **Gen 3 stores the stats it last COMPUTED, not the stats the formula gives now.** The games
  refresh battle stats only on level-up, evolution or a PC deposit, so a Pokemon EV-trained since
  its last level-up legitimately stores stats *below* the formula — the game's state, not a stale
  cache. `buildPartyRecord` recomputed unconditionally and drifted six bytes of a real Ruby save (a
  level 92 Starmie storing 246 HP where the formula says 249). It now synthesises a tail **only for
  a record that has none** — one promoted from a box, 80 bytes — and `setIV`/`setEV` call
  `recalculateStats()` so an edited Pokemon still gets fresh stats. Both halves are needed: fixing
  the writer alone stops edits from persisting.

**The envelope survives the round trip.** Every mGBA GBA save is `0x20010` bytes, not `0x20000` —
an RTC footer — and VBA Next writes a `0x22000` `.srm`. Dropping the footer silently deleted a clock
that drives berry growth and the tides. Chapter 05 covers save envelopes.

**The active slot is the complete one, then the newer** (PKHeX `SAV3.GetActiveSlot`). An
interrupted save leaves the newer slot missing a sector and the game loads the other; choosing by
counter alone let `detect()` accept a file the parser then refused. `slotHoldsEverySector` (in
`Trainer3FRLG.cpp` and `Trainer3RSE.cpp`) is the one test for both.

**The RSE learn pool UNIONS Ruby/Sapphire with Emerald**, for the reason RBY and GSC are unioned:
one group, one pool, and narrowing would report a legitimate Emerald Battle Frontier tutor move as
illegal — which the transfer sanitizer then deletes. It carries **two different tutor mechanisms**:
Emerald's 30-move `Tutor_E` list gated per species by the `TypeTutors` bitfield in
`tutors_g3.pkl` (a separate blob, the same shape as Gen 4's), and Ruby/Sapphire's Pokemon XD tutors
— Mew's six, plus flat species lists for Self-Destruct, Sky Attack and Nightmare. The pool is
deliberately **wider than FRLG's**, which carries only the three starter tutors; that is PKHeX's
`LearnSource3FR`.

**The bank reuses `BTAG_FRLG` for RSE.** One tag per ENTITY format is the rule Gens 4–7 already
follow, and all five GBA games store a PK3. The tag is frozen on disk and add-only, so a PK3 banked
before RSE existed and one banked after must stay readable as each other.
