# 10 — The bank and cross-generation conversion

## What the bank is

A HOME-style persistent storage box that lives **outside any save file**, on the SD card at
`sdmc:/PKSE/bank/bank.dat`. **200 boxes × 30 slots**, unified across every game: each slot carries
its own game-group tag plus the Pokémon's **native encrypted bytes**.

```cpp
// include/Trainer/Bank.h
class Bank {
    static constexpr size_t BANK_BOX_COUNT     = 200;
    static constexpr size_t BANK_SLOTS_PER_BOX = 30;
    static constexpr size_t MAX_BOX_NAME_LEN   = 24;

    Bank();                                   // constructs + load()s, and migrates legacy banks
    void load();                              // (re)load from SD, discarding memory changes — also "discard"
    bool save() const;
    bool hasChanged() const;                  // in-memory != last saved/loaded state

    std::vector<std::array<std::unique_ptr<Pokemon::Pokemon>, BANK_SLOTS_PER_BOX>> boxes;
    std::array<std::string, BANK_BOX_COUNT> boxNames;   // empty = the default "Bank N" label
    std::string boxDisplayName(size_t box) const;

    uint16_t currentBox = 0;                      // the bank pane's open box -- see below

    size_t lastLoadRejects() const noexcept;      // slots dropped at load (bad record)
    size_t lastVerifyFailures() const noexcept;   // slots that failed the save-time round trip

    static Enums::GameVersion groupAsBanked(Enums::GameVersion group) noexcept;
};
```

### The one rule

> **Deposit is passive. The bank never converts or mutates a stored Pokémon. Byte-in == byte-out.**

Only **withdraw into a save** converts, and that conversion is the *caller's* job
(`TrainerViewScreen::convertForPane`), not the bank's. The bank is storage, not a transfer service.

That is also why the same-group withdrawal path still does a little work — see `convertForPane`
below.

## The on-disk format

```
Header, 16 bytes:  char magic[8] = "PKSEBANK"
                   u32  version  = 1
                   u32  boxCount                        <- the count the file was WRITTEN with

Then boxCount * 30 fixed-size records:
                   u32  groupTag                        <- BTAG_*, 0 = empty
                   u8   payload[maxPayload]             <- native ENCRYPTED per-gen bytes

Optional names section, appended AFTER the records:
                   u8   marker[4] = "NAMS"
                   per box: u16 LE length + that many UTF-8 bytes

Optional open-box section, appended AFTER the names:
                   u8   marker[4] = "CBOX"
                   u16  LE box index (clamped on read)    <- Bank::currentBox
```

Records are **fixed-size** (payload sized to the largest party record across all games) so slot *N*
is always at a computable offset.

The names section is appended after the records deliberately: old readers stop at the last record
and ignore trailing bytes, so it needed **no version bump** — a v1 file without the section simply
has no custom names, and a v1 file *with* it is still read correctly by code that predates it. Copy
that pattern for any future optional section.

`CBOX` follows the same rule, and its position **after** `NAMS` is load-bearing: an older PKSE looks
for `NAMS` immediately past the record table, so putting `CBOX` first would make it find the wrong
marker there and quietly drop every box name the user had set.

### The bank's open box lives in the bank image

**There is one save-box cursor, not one per view** (`selectedBoxIndex` serves the Boxes view and the
storage view's save pane alike — chapter 08). The **bank** pane's box is genuinely separate — a
different container with 200 boxes — and it lives **in the bank's own image**, as `Bank::currentBox`.
That is what makes moving it an *unsaved bank change*, which is the point: `hasChanged()` is
`serialize() != savedImage`, so the bank image is the only place a cursor position can sit and still
reach the Save/Discard prompt. Scrolling the bank and then choosing Discard returns you to the box you
last *saved*, not the one you wandered to.

It was briefly a `settings.cfg` key (`g_bankBoxIndex`), and that cannot work in either direction: a
setting is written the moment it changes, so a cursor move became a persisted decision with no way to
undo it — and a box change alone left `hasChanged()` false, so the prompt never appeared to save it
either.

### Tags are frozen, add-only

```cpp
// src/Trainer/Bank.cpp
enum : uint32_t {
    BTAG_EMPTY = 0, BTAG_GG = 1, BTAG_SWSH = 2, BTAG_BDSP = 3, BTAG_PLA = 4,
    BTAG_SV = 5, BTAG_ZA = 6,
    BTAG_FRLG = 7,     // PK3 -- FireRed/LeafGreen AND Ruby/Sapphire/Emerald
    BTAG_RBY = 8,      // international, 69-byte record
    BTAG_RBY_JP = 9,   // Japanese,      59-byte record
    BTAG_GSC = 10,     // international, 73-byte record
    BTAG_GSC_JP = 11,  // Japanese,      63-byte record
    BTAG_G4 = 12,      // PK4 (DP/Pt/HGSS), 236-byte party record
    BTAG_G5 = 13,      // PK5 (BW/B2W2), 220
    BTAG_G6 = 14,      // PK6 (XY/ORAS), 260
    BTAG_G7 = 15,      // PK7 (SM/USUM), 260
};
```

**Never renumber these.** They are deliberately independent of `Enums::GameVersion`'s numeric
values, which are title-ID-derived and must stay free to change without invalidating everything
already banked.

**A tag names an ENTITY FORMAT, not a game.** The bank stores Pokémon, and DP/Pt/HGSS all store a
PK4, BW/B2W2 a PK5, XY/ORAS a PK6, SM/USUM a PK7, and all five GBA games a PK3 — so there is one tag
per format, and `Bank.cpp` maps all nine pre-Switch groups onto them. `BTAG_FRLG` is reused for
Ruby/Sapphire/Emerald rather than given a sibling: the tag is frozen and add-only, so a PK3 banked
before RSE existed and one banked after must stay readable as each other.

**Gen 1 and Gen 2 need two tags each, not one.** A PokeList's *length* is the only thing that says
whether its name fields are 11 bytes or 6, and the on-disk record has no length field — so the tag
carries it. Guessing from the payload is not an option: `0xFF` is a real Gen 1/2 character (`'9'`), so
"trailing `0xFF` means Japanese" misreads a legitimately-named international Pokémon. Two tags, no
heuristic.

```cpp
uint32_t bankTagForEntity(const Pokemon::Pokemon& pk) {
    const GameVersion group = pk.getGameGroup();
    if (group == GameVersion::RBY)
        return pk.getDataSize() == Pokemon::SIZE_1JLIST ? BTAG_RBY_JP : BTAG_RBY;
    if (group == GameVersion::GSC)
        return pk.getDataSize() == Pokemon::SIZE_2JLIST ? BTAG_GSC_JP : BTAG_GSC;
    return bankTagFor(group);
}
```

Note it uses the **buffer size** rather than a cast, because RTTI is disabled project-wide.

**Never compare a bank tag's group to the entity's own group.** Because a tag names a format,
`groupForBankTag()` returns the format's *representative* group — the superset whose tables cover the
others': a Ruby PK3 reads back as FR/LG, a Platinum PK4 as HGSS, an X/Y PK6 as ORAS, a Sun PK7 as
USUM. `Bank::verifyImage` once asserted `groupForBankTag(tag) == source->getGameGroup()`, and so
rejected every shared-format record *before* the round trip it exists to perform; `save()` counts
that as damage, so a user banking Gen 3–7 Pokémon was told their bank was corrupt while the bytes were
fine. The invariant that holds is `tag == bankTagForEntity(entity)`, then decode through the tag as
the loader does. `Bank::groupAsBanked(group)` is the public form for anything comparing a group across
the bank.

### `BANK_BOX_COUNT` is a ratchet

**Raising it is safe**: the header stores the count the file was written with and `load()` honours
*that*, so a smaller older bank still opens. **Lowering it is not** — and that is not hypothetical.
The constant was briefly reduced to 100 while a bank written at 200 sat on the SD card, and every
launch logged *"file has more boxes than this build supports"* because the next save would have
dropped boxes 101–200.

200 rather than 100 because a real PKSM bank is commonly 150 boxes, and importing one copies each
non-empty source box into a whole empty destination box — so the destination needs room for the
user's PKSM layout *and* whatever they already had.

### Integrity reporting

```cpp
size_t lastLoadRejects() const noexcept;      // records that didn't decode to a valid Pokemon
size_t lastVerifyFailures() const noexcept;   // slots whose bytes failed encrypt->decrypt on save
```

`save()` re-parses its own serialized image and compares each occupied slot back to the live
Pokémon. Non-zero means a PKSE bug, because byte-in == byte-out is the contract.

It is **reported, not enforced**, on purpose: a failed bank save blocks leaving the storage view, so
treating a verification miss as a save failure would trap the user in the UI over what may be a
false positive. Writing proceeds; the anomaly is surfaced.

`load()` gates each record on `Pokemon::isStructurallyValid()` — the virtual, not
`checksumValid()` — because Gen 1 has no checksum (chapter 06).

## Conversion

`include/Conversion/Convert.h`.

```cpp
enum class Result {
    Ok,           // converted successfully
    SameGroup,    // no conversion needed -- move the original as-is
    Unsupported,  // not one of the interconverting games
    NotInDex,     // species/form doesn't exist in the destination
    Blocked,      // destination refuses this species (BDSP Spinda / Nincada)
};

struct ReceivingTrainer { std::u16string name; uint8_t gender = 0; };

std::unique_ptr<Pokemon::Pokemon> convert(const Pokemon::Pokemon& src, GameVersion destGroup,
                                          Result& result, uint8_t destOriginVersion = 0,
                                          const ReceivingTrainer* receivingTrainer = nullptr);
bool        canConvert(const Pokemon::Pokemon& src, GameVersion destGroup, Result& result);
const char* resultMessage(Result r);

bool          virtualConsoleTransferInvolved(const Pokemon::Pokemon& src, GameVersion destGroup);
GameVersion   virtualConsoleOriginVersion(const Pokemon::Pokemon& src);
bool          virtualConsoleOriginIsGuess(const Pokemon::Pokemon& src);
```

`destOriginVersion` is the **exact destination game's** origin byte, not the group. It matters only
where an origin cannot be carried across and must be restamped — today, the Gen 3 down-convert,
whose origin field is 4 bits wide and cannot hold a modern game's ID. It must be passed because a
game *group* deliberately collapses a version pair into one value: `FRLG` cannot say FireRed from
LeafGreen, and stamping a fixed member of the pair is wrong half the time. `0` falls back to the
group's representative version.

`receivingTrainer` is read by **one** route: the Gen 1/2 transfer, which has to stamp a handling
trainer the source record cannot supply. Pass `nullptr` when there is no save behind the call — the
Pokémon then arrives with no handler at all, which is a readable state; an invented name is not.

### What is preserved and what is not

Following PKHeX/HOME: **origin identity is preserved verbatim** — Version, OT, IDs, met/egg data,
HOME tracker, EC/PID/IVs/nature/gender/language/ribbons. Only game-specific container fields are
rewritten, then the checksum is refreshed.

**PKSE sanitizes the moveset for the destination.** Any move or relearn move the destination species
cannot legally learn — illegal for the species, *or* not present in that game at all — is cleared to
empty.

> This is **required**, not a nicety: **Let's Go turns a mon carrying an impossible move into a Bad
> Egg.**

The held item is sanitized the same way against a per-game holdable-item table, and cleared if the
destination cannot hold that id — which includes *every* id for Let's Go and Legends: Arceus, since
neither game has a held-item mechanic at all.

PKHeX instead regenerates the whole moveset from the destination learnset; PKSE keeps the mon's own
legal moves and drops only the illegal ones.

### The hub

**Every conversion normalises through the PK8 layout.**

```
  PK1 (Gen 1) ─┐
               ├─► Poké Transporter ─► PK7 (Sun/Moon, Ultra Sun/Moon) ─┐
  PK2 (Gen 2) ─┘          (one way)                                    │
                                                                       ▼
   PB7 (Let's Go) ──┐                    ┌── PK9 (S/V)
   PA8 (Arceus)  ───┼──►  PK8 layout ◄───┼── PK9 (Z-A)
   PK3 (FRLG,RSE)───┘        ▲           └── PB8 (BDSP)
                             │
                        PK8 (SwSh)
```

| Pair | What happens |
|---|---|
| Same-gen siblings (SwSh↔BDSP, S/V↔Z-A) | copy + null a couple of divergent fields |
| PK8↔PK9 | field remap: gender bit position, Version/Language/FormArg/AffixedRibbon offsets, height/weight/scale, HOME-tracker offset, record-flag / Tera / ObedienceLevel regions. Tera type is **imported from the species' primary type** (PK8 has none). |
| PA8 (Arceus) | whole Block B/C/D at different offsets — a full field relocation routed through PK8. PA8-only data (GVs, Alpha, move-mastery) is dropped; LA-only balls collapse to a Poké Ball. |
| PB7 (Let's Go) | relocates through PK8, but **stat training is NOT cross-converted**: AVs/EVs are dropped, so a mon entering LGPE gets AVs = 0 and one leaving gets EVs = 0. Level/stats/CP tail is recomputed. |
| PK7 (Sun/Moon ↔ Ultra Sun/Moon) | siblings: the same record byte for byte, so nothing is nulled. The two differ only in how far their dex, move and item spaces run, which the gate already handles. |
| PK7 ↔ PK8 | the PB7 remap plus everything Let's Go dropped — held item, EVs, contest stats, both trainers' memories, the ribbon memory counts. Gen-7-only regions have nowhere to go and are dropped: Super Training, the Poké Pelago resort state, the 3DS geolocation block (plus Country/Region/ConsoleRegion), and affection. Ribbons are dropped too — Gen 8 reordered the bits and added marks, so the blocks are not a byte copy and a wrong one would hand out ribbons nobody earned. Coming back down, Gen 8's own additions (TR flags, Dynamax, Sociability, height/weight scalars, AffixedRibbon, marks, and the **mint** — PK7 has `Nature` but no `StatNature`) have no home. |
| PK3 (FRLG, RSE) | the most divergent. No EC (EC := PID); nature/gender/shiny are all PID-derived; ability is a slot **bit** resolved via the personal table; species/items/text use Gen-3 id spaces; IVs pack the ability bit at 31. Item ids convert via `Item3to4`, names transcode Gen-3↔Unicode, level/stats recomputed. Going **to** Gen 3, nature and gender become PID-derived and custom nicknames collapse to the species name. A Gen 3 destination is a **rebuild**, not a carry — see "Rules: Poké Transporter and the Gen 3 rebuild" below. |

The result: any of the interconverting games ↔ any other, **full two-way** — deliberately beyond
HOME's one-way Let's Go limit — bounded only by destination dex presence.

### Poké Transporter — the one-way door out of Gen 1 and Gen 2

There has never been a route out of Red/Blue/Yellow or Gold/Silver/Crystal except Poké Transporter,
which builds a **Gen 7** record and drops it in Bank. PKSE follows that literally: a PK1 or PK2 is
**not** remapped onto a modern layout. It goes through the transfer first, and the PK7 that comes out
then travels the ordinary hub like any other Gen 7 Pokémon. Doing it in that order is what makes the
invented fields the ones Transporter would have invented.

Most of the step is a **rule**, not a copy, because a PK1 has no nature, ability, PID, ball, met data
or origin game to carry (PKHeX `PK1.ConvertToPK7` / `PK2.ConvertToPK7`):

| Field | What Transporter does |
|---|---|
| Nature | `EXP % 25` — of the **original** EXP, read before the flooring below. Read it after and every transferred Pokémon would share a handful of natures. |
| EXP | floored to the minimum for the level it arrives at. Partial progress toward the next level is lost. |
| Ability | the **hidden** one — the well-known VC quirk, and why a transferred Gengar is a Cursed Body Gengar. The exceptions are the species Bank withholds it from: the Gastly and Koffing lines and Mew, plus Misdreavus, Unown and Celebi out of Gen 2. |
| IVs | rerolled, three guaranteed 31s (five for Mew and Celebi), shuffled so which three is not fixed. A Gen 1/2 DV is 4 bits wide and also drives shininess, so there is no honest widening of it. |
| Shininess | preserved as a **fact**, not a mechanism: the rerolled PID is forced shiny or not shiny to match what the source was. |
| EVs | zero. Gen 1/2 Stat Experience is dropped, not converted. |
| Met data | Poké Ball; met level = the arrival level; met location 30013 ("the Kanto region") or 30017 ("the Johto region"). |
| Nickname | the species name, unless the stored one differs from it. Mew and Celebi take the fateful-encounter branch **instead** of the nickname branch, so a nicknamed one arrives called Mew. |
| OT | carried, except an in-game-trade OT — Gen 1/2 store a marker byte (`0x5D`, which PKSE renders `*`) rather than a name there, and Bank substitutes the localised "Trainer". |
| Moves | carried **verbatim**, PP-Ups included; the PP is healed to the Gen 7 maximum. Dizzy Punch is the single move Transporter refuses to carry out of Gen 2. |
| Handler | the receiving player, with the Gen 6/7 Bank trade memory — it arrives through a trade. |

**The moveset sanitizer does not run on this route.** A transferred Pokémon's moves are legal
*because* it learned them in Gen 1/2, and no Gen 7+ learn pool records that — a Yellow Pikachu really
does know Surf. Only `Names::isMovePresent` applies: a move the destination game does not *have*
cannot be written into it whatever its history. `Legality.cpp` was taught the same thing in the other
direction, and checks a transferred mon's moves against the **union** of the pool it learned in and
the pool it lives in now; without that, every transfer PKSE performed would immediately warn about
itself, and with only the old pool every TM taught after arrival would.

**Three things PKSE cannot reproduce, all for want of data rather than code:**

1. **The origin title is a guess.** Neither a PK1/PK2 nor the save it came from holds a version byte,
   so Red/Green/Blue and Gold/Silver are indistinguishable and a banked record has no save behind it
   to ask. PKSE stamps the group's representative (`getGroupRepVersion` — Red, Gold) and **says so in
   the confirm dialog**, naming the title it is about to write. Gen 2 is the one exception: a record
   carrying Crystal's caught data names Crystal outright, which is what PKHeX does too. (PKHeX offers
   a *setting* for the rest and defaults Gen 2 to Silver; PKSE names the first of the pair, the
   convention it already uses everywhere else.)
2. **A carried name goes through PKSE's own Gen 1/2 character table**, not Bank's transporter one,
   and the two differ for Japanese: PKHeX's `StringConverter12Transporter` shifts katakana to hiragana
   when a name mixes the two, and PKSE writes what its table decodes. (The names PKSE *invents* — the
   species name an un-nicknamed Pokémon arrives holding, and the substituted in-game-trade OT — are
   written in the **record's** language rather than the display one: a Japanese Pikachu transfers as
   ピカチュウ and the trade OT as トレーナー. An English-only name would both read wrong and make the
   Pokémon look deliberately nicknamed to anything comparing the two.)
3. **The 3DS locale block stays 0** (Country / Region / ConsoleRegion, `0xE0`–`0xE2`). PKSE reads no
   console region off the receiving save, and inventing one would be worse than blank.

Randomness is **deterministic**: the PID, the encryption constant and the IV placement come from a
SplitMix64 stream seeded by the source record's own bytes, so the same Pokémon always transfers to
the same Pokémon. A conversion that answered differently every time could not be tested, and a user
who withdrew a mon, undid it and withdrew again would get a different Pokémon each time.

Nothing comes **back**. `gate()` refuses any conversion whose destination is `RBY` or `GSC`: a PK1
has no field to put a modern Pokémon's nature, ability, PID, ball or met data into, so a
down-convert would not be lossy, it would be fabrication.

### The gate

`Conversion::gate()` is the pure check that runs before any allocation.

```cpp
auto inFamily = [](GameVersion gameVersion) {
    return isG8(gameVersion) || isG9(gameVersion) || isG7(gameVersion) || gameVersion == GameVersion::PLA
        || gameVersion == GameVersion::GG || isGen3Group(gameVersion);
};
if (isVirtualConsole(dest))                            return Result::Unsupported;
if (!(inFamily(from) || isVirtualConsole(from))
    || !inFamily(dest))                                return Result::Unsupported;
```

`RBY` and `GSC` are in the family as a **source only** — see the Poké Transporter section above.
Every other pre-Switch group (`DP`/`PT`/`HGSS`, `BW`/`B2W2`, `XY`/`ORAS`) is absent entirely, so a
transfer to or from one returns `Unsupported` and `convertForPane` declines it with a message.
Depositing any of them into PKSE's own bank is fine — that is storage, not conversion.

Then three checks, in order:

**1. Out-of-range form.** Guarded explicitly, because `getPersonalInfo()` clamps an out-of-range form
back to form 0 — so without this, a corrupt form would read form-0's presence, silently false-pass
the dex checks, and get written verbatim into the destination as an out-of-range form (a Bad Egg
in-game).

```cpp
if (form != 0 && form >= Pokemon::getPersonalInfo(species, 0).formCount) return Result::NotInDex;
```

**2. Dex presence.** Every Switch game uses its `PersonalInfo::presence` bit. Two predate the bitmask
and answer from their own tables instead.

Gen 7 asks its own table (`getPersonalInfo7SM` / `getPersonalInfo7USUM`, or
`getPersonalRecord(group, ...)`): the species has to be inside that game's dex range — **Sun/Moon stops at
802 and Ultra Sun/Moon at 807**, and one number for "Gen 7" quietly grants Sun/Moon six Pokémon it
does not have — and the form has to be one Gen 7 defines. A Paldean Tauros or a Hisuian Zorua has no
Gen 7 entry, and writing its form index into a PK7 makes a Bad Egg in-game.

Both Gen 3 groups use the Gen 3 species range, identically — R/S/E and FR/LG share one 386-species
dex and one PK3 layout, so what a Kanto save can hold a Hoenn save can hold (which species are
*catchable* differs, but that is an encounter question, not a storage one) — **and refuse alternate
forms**:

```cpp
if (isGen3Group(destination)) {
    if (Pokemon::gen3NationalToInternal(species) == 0) return Result::NotInDex;
    // Gen 3 stores no general form byte, so an alternate form would be silently FLATTENED to the
    // base form on write -- a Combat Breed Tauros arriving in FireRed as a plain Tauros. Refuse
    // instead of corrupting. Unown is the exception: its form rides on the PID (which the remap
    // carries), so Gen 3 re-derives the same letter.
    if (form != 0 && species != 201 /*Unown*/) return Result::NotInDex;
}
```

**3. Spinda / Nincada into BDSP** — a **legality** rule, not a format one. BDSP holds both species
perfectly well (each is catchable in the Grand Underground, Spinda even has its own spot-pattern
block), but Pokémon HOME refuses to move them in, so one that did not originate there reads as
illegal. PKHeX says the same. That is exactly the "the game can hold it but shouldn't" case, so
`g_allowIllegalEdits` lifts it.

### `AffixedRibbon` — the 0-is-not-none trap

```cpp
size_t affixedRibbonOffset(GameVersion group) noexcept;   // 0 for PK3, PB7 and PK7 (no such field)
inline constexpr uint8_t AFFIXED_RIBBON_NONE = 0xFF;
bool normalizeAffixedRibbon(Pokemon::Pokemon& pk);         // in place; re-checksums if changed
```

`AffixedRibbon` names the single ribbon the game **displays** on the summary screen. Its "none" value
is `0xFF`, **not 0** — 0 is a real index, and index 0 is the Kalos Champion ribbon. Any path that
leaves the byte at its zero-initialised default hands the mon a ribbon it does not own.

This has bitten twice from two different directions (the creator, then cross-gen conversion), so the
offset lives in one place and both call sites read it.

`normalizeAffixedRibbon` only repairs a `0` when the mon does **not** own that ribbon. Verified
against real saves: every genuinely game-caught mon carries `0xFF`, including ones that *do* own
ribbons — so a `0` here is never something the game wrote. The owned-ribbon guard means a
deliberately affixed ribbon is never disturbed.

Call it on the way **into** a save. **Never on deposit** — the bank stores native bytes untouched by
design.

## `convertForPane` — the UI's entry point

```cpp
bool TrainerViewScreen::convertForPane(std::unique_ptr<Pokemon::Pokemon>& pk, int destPane) {
    if (destPane != 0 || !pk) return true;                    // pane 1 = bank: store as-is

    if (pk->getGameGroup() == trainer.getGameGroup()) {
        // Same game, so no conversion -- but repair an invalid AffixedRibbon and re-checksum
        // before it enters the save. A stale checksum reaching the game's box writer is a Bad Egg.
        Conversion::normalizeAffixedRibbon(*pk);
        pk->refreshChecksum();
        return true;
    }

    Conversion::Result res;
    // Only the Gen 1/2 route reads the receiving trainer -- it arrives through a trade, so the
    // player opening this save becomes the handling trainer. Everything else carries its own OT/HT.
    const Conversion::ReceivingTrainer receiving{Utils::utf8ToUtf16(trainer.trainerName),
                                                 trainer.trainerGender};
    auto converted = Conversion::convert(*pk, trainer.getGameGroup(), res,
                                         saveOriginVersion(trainer, titleId), &receiving);
    if (converted) { pk = std::move(converted); return true; }   // convert IN PLACE

    storageStatus = Conversion::resultMessage(res);              // refusal is explained on screen
    storageStatusFrames = 150;
    return false;
}
```

Three things to copy from this:

- **The bank branch returns immediately.** Nothing entering the bank is touched.
- **The same-group branch is not a no-op.** Legacy stock banked before the creator learned to set
  `AffixedRibbon` carries `0`; a cross-gen withdrawal fixes it inside `convert()`, but a same-group
  one never calls `convert()` at all. Repairing here — on the way *into* a save — keeps the bank's
  never-mutate rule intact.
- **A refusal is surfaced to the user**, not just logged.

Every conversion is also traced, because *"cross-gen conversion is where the subtle transfer bugs
have historically lived (fainted arrivals, deleted moves, garbage levels)"*.

## The lossy-move warnings

Three independent warnings about three unrelated losses, all gated by the one `g_moveWarn` setting
("Bank Storage Move Warning") — it covers every bank transfer warning, in every generation:

```cpp
bool gen3ConvertConfirmActive;            // "Convert to Gen 3?"   -- gated by g_moveWarn
bool virtualConsoleTransferConfirmActive; // "Poké Transporter"    -- gated by g_moveWarn
bool lgpeTransferConfirmActive;           // "Let's Go transfer"   -- gated by g_moveWarn
int  moveConfirmIndex = 1;        // cursor: 0 = Cancel, 1 = Continue (default)
enum class PendingMove { None, PlaceHeld };
PendingMove pendingMove;
int pendingMovePane, pendingMoveBox, pendingMoveSlot;   // the stashed action, resumed on Continue
bool moveConfirmActive() const noexcept {
    return gen3ConvertConfirmActive || virtualConsoleTransferConfirmActive || lgpeTransferConfirmActive;
}
```

| Warning | Loss | Severity |
|---|---|---|
| Gen 3 down-convert | rebuilds the PID to preserve nature; drops nickname / ribbons / held item | 1 (highest). Destructive and irreversible. |
| Poké Transporter (Gen 1/2 → later) | rerolls the IVs, invents a PID, floors the EXP, drops Stat Experience, grants the hidden ability | 2. Irreversible, and the only place the user is told the origin title is a guess. |
| Let's Go transfer | resets AV/EV training to 0, drops unlearnable moves | 3. Recoverable by re-training. |

A move can be more than one of these. **Gen 3 supersedes, then Transporter, then Let's Go** — the
order is by severity, and stacking two dialogs on one action reads as a bug.

**Severity decides which dialog is shown, not whether one is.** The first two used to warn
regardless of `g_moveWarn`, which made the setting narrower than its label: a user who had turned
warnings off was still stopped on every FireRed drop, with nothing on the settings screen to explain
it. One switch now covers all three; how destructive a transfer is, each dialog says in its own text.

The predicates that decide, and their block-wide versions:

```cpp
bool lgpeConversionInvolved(int destPane, const Pokemon::Pokemon* pk) const;  // exactly one side is LGPE
bool blockInvolvesLgpe(int destPane) const;
bool gen3DowngradeInvolved(int destPane, const Pokemon::Pokemon* pk) const;
bool blockInvolvesGen3Downgrade(int destPane) const;
bool virtualConsoleTransferInvolved(int destPane, const Pokemon::Pokemon* pk) const;
bool blockInvolvesVirtualConsoleTransfer(int destPane) const;
```

All six return `false` for `destPane != 0`, because only save-pane placements convert — a deposit
never resets anything.

The Transporter dialog also reads two more, which are what make it worth reading:

```cpp
Enums::GameVersion pendingTransferOriginVersion() const;   // the title it will stamp
bool               pendingTransferOriginIsGuess() const;   // ...and whether that is a fact
```

It **names the title** rather than saying "unknown": the user can see whether it matches the game the
Pokémon actually came from, and nothing else in PKSE will ever be able to tell them.

## The storage view

Dual-pane: the save's PC boxes on the left (pane 0), the bank on the right (pane 1).

### Three cursor modes, cycled with Y

| Mode | Colour | A does |
|---|---|---|
| `Menu` | red (`Colors::CursorMenu`) | opens a per-Pokémon menu (Move / Edit / Release) |
| `Move` | blue (`Colors::CursorMove`) | picks up / places / swaps one Pokémon |
| `Multi` | green (`Colors::CursorMulti`) | anchors a rectangle; moving expands it; A again grabs the block |

The cursor arrow is drawn in the active mode's colour, so the mode is always visible.

### One carry state

A Move-mode pick-up is a **1×1 block**, so `moveMon` is the only carry state. Every guard that asks
"are we holding something?" answers for both cases at once.

```cpp
std::vector<std::unique_ptr<Pokemon::Pokemon>> moveMon;   // row-major over selectDimensions
std::pair<int,int> selectDimensions;   // anchor (col,row) while selecting; (w,h) while carrying
int heldPane, heldFromBox, heldFromSlot;    // the block's TOP-LEFT origin, so B puts it all back
```

A **null cell is a hole** — the source slot was empty, or it was locked and deliberately left behind
(Let's Go party members in the save pane are locked).

The operations:

```cpp
void storagePickup();        // the A press: anchor / grab / put down, whichever applies
void pickupSingle();         // Move mode: lift the slot under the cursor as a 1x1 block
void pickupMulti();          // Multi mode: anchor, or grab if already anchoring
void grabSelection(bool remove);   // lift (or COPY, when !remove -- X clones the block)
void scrunchSelection();     // trim all-empty edge rows/columns off the grabbed block
void postPickup();           // drop an all-empty block so the hands read as free
bool checkPutDownBounds() const;   // does the block fit from the cursor cell?
void putDownBlock();         // EXACT-slot placement: cell (x,y) -> cursor slot + x + y*cols
void cancelSelection();
void returnHeldToOrigin();
void sortStorageBox(int pane, int box);   // pack + order one box, pinning party-linked slots
```

**Placement is exact-slot**, not "next free". The one exception is Let's Go, and it is handled by
compaction rather than by placement (below).

### Compaction, and why it is at the top of `update()`

Let's Go stores its boxes as a **gapless list**, so anything that vacated a slot last frame left a
hole the game cannot represent.

```cpp
if (!carrying() && !currentlySelecting && !swapActive && !creator.active && !details.active) {
    trainer.compactStorage();
}
```

It lives here — at the top of `update()`, not at each of the several sites that can vacate a slot —
because `update()` has many early returns (the release path returns before ever reaching the
bottom), so an end-of-update hook would silently skip them, and per-site hooks are exactly the
coverage gap this codebase keeps hitting.

The five exclusions are each a real bug that was fixed by adding one:

- **`carrying` / `currentlySelecting` / `swapActive`** — the board must not shift under an operation
  in progress. An in-progress rectangle is anchored to slot *indices*.
- **`creator.active` and `details.active`** — the open editor re-resolves its target through
  `selectedBoxIndex` / `selectedItemIndex` every frame. The creator drops a new mon on whatever slot
  the cursor is on, which in a gapless game is usually a gap, then opens the editor. Compaction
  fired on the very next frame, slid the mon down to the packed position, and left the editor
  pointing at an **empty slot** — so it drew nothing (the modal bails on a null target) while still
  swallowing input, and **the app looked frozen with only B and + alive.** Deferring is the whole
  fix: the mon still compacts, just once the editor closes.

### Leaving with unsaved bank changes

The bank is its own file and gets its own decision.

```cpp
bool storageExitConfirmActive;
int  storageExitConfirmIndex;   // 0 = Save & Exit, 1 = Discard & Exit, 2 = Cancel
bool exitAfterBankChoice;       // set when + (exit app) raised the prompt
```

Pressing `+` (quit the app) **is not an answer to "save the bank?"**, so it asks first and resumes
the exit once Save or Discard has been picked:

```cpp
if (kDown & HidNpadButton_Plus) {
    if (storageExitConfirmActive) return;      // answer that first
    returnHeldToOrigin();                      // a carried mon goes home before anything is weighed
    if (bank && bank->hasChanged()) {
        storageExitConfirmActive = true;
        storageExitConfirmIndex = 0;
        exitAfterBankChoice = true;
        return;
    }
    beginAppExit();
    return;
}
```

Writing the bank on the way out was a real bug: it committed every transfer even when the user then
**declined** the game save, and the two files disagreed. A deposit became a permanent **clone** (the
bank has it, the save still has it); a withdrawal a permanent **loss** (the bank no longer has it,
the save was never written). Asking lets Discard rewind both sides and write nothing.

`Bank::load()` doubles as "discard": it clears every slot and re-reads the on-disk state.

## Reading PKSM's bank

`Trainer::PKSMBankImport` (`include/Trainer/PKSMBank.h`) reads a PKSM `.bnk` off the SD card and
stages PKSE entities from it; `-` (Minus) in the storage view runs browse → preview → commit. It is
the only place PKSE parses somebody else's format, and it has **two traps that produce
plausible-looking corruption rather than an error**:

- **PKSM stores each Pokémon DECRYPTED** — it dumps its in-memory buffer verbatim. PKSE's bank stores
  native *encrypted* bytes and every `Pokemon` constructor decrypts what it is handed, so the import
  has to encrypt on the way in, and for Gen 3 restore the `PID % 24` substructure shuffle too (PKSM
  undoes that as well). A straight `memcpy` yields a bank full of Pokémon that look almost right and
  are quietly corrupt.
- **The generation tag is not the generation number.** Gens 1–3 were appended to the end of PKSM's
  enum, so Gen 3 is `6` and tag `3` is Gen 7 (`Trainer::PKSMGen`). The full table is in the reference
  below.

PKSM stores *box* records, so each is grown to PKSE's party size and — for PK8, whose level lives in
that tail — seeded from EXP before `recalculateStats()`. **Every one of PKSM's nine generation tags
has a PKSE format**, so nothing a real bank can hold is refused; `PKSMImportReport::unsupported` is a
counter that no longer fires, kept because it is the honest answer if PKSM ever adds a tag, and
because a silently dropped record is the one outcome the importer must never produce.

**A private list of supported formats went stale once, and it is the shape to watch for.** The
importer carried its own set of supported generations — Gen 1, Gen 3, Let's Go, Gen 8 — written when
those were the only formats PKSE had, and never extended as the other fifteen groups landed. A real
bank then reported *"no PKSE format"* for 44% of its contents (101 Gen 2 and 17 Gen 7 of 269) while
the formats it claimed to lack were fully implemented. Worse, the test written against that bank
**locked the bug in**: it asserted `1 unsupported (Gen 6)` as correct behaviour, and its slot-placement
check carried a *second* copy of the same list. Both now cover every tag, and the placement check's
`default:` fails on an unknown tag rather than skipping it. Any list of "what this code supports"
that is not derived from the thing it describes will drift; a test that copies it drifts with it.

Three things differ per tag, all load-bearing:

- **Gens 1 and 2 are PokeLists** — no encryption, no box→party growth, no checksum, and a
  locale-dependent length recovered by `probePokeListLength()` (59/69 bytes for Gen 1, 63/73 for
  Gen 2; see the reference below for how).
- **Gens 3, 4 and 5 seed their crypt from the record's own PID and checksum** rather than an explicit
  encryption constant, which still works after the growth because the appended tail sits outside the
  checksummed block region.
- **Gen 8 alone needs its level seeded by hand**, because `Pokemon8SWSH::level()` reads `0x148` raw
  and `recalculateStats()` bails on a level of 0 — leaving a level 0 import — while Gens 4–7 fall back
  to deriving level from EXP inside `recalculateStats()`.

A tag maps to the format's **representative** group, the same one `Bank::groupForBankTag()` answers,
so the importer and the bank cannot disagree. The parser takes a byte span rather than a path
(`scan()` is filesystem-free), which keeps it testable on an ordinary desktop compiler.

The import lands in the **in-memory** bank only, so the storage view's Save/Discard prompt makes it
permanent — and Discard undoes the whole import, which is why it needs no undo of its own. Each
non-empty source box is copied into a whole empty destination box (with a first-fit fallback when
none is left), which is why `BANK_BOX_COUNT` is 200.

`PKSMImportReport` reports every count to the user — occupied, importable, damaged, overflow, names
carried, and `gen3NonFRLG` (see below) — because "nothing happened" and "23 Pokémon had nowhere to
go" look identical from the outside otherwise.

**Facts about the format are fine to use; PKSM's code is GPL and PKSE is AGPL-3.0.** The reader was
written from the layout description, not transcribed. Keep it that way — consult PKSM to check an
offset or an enum value, never to copy expression. The reference below is written the same way: it
records facts about the file, not PKSM's code.

### Reference: the PKSM `.bnk` format

Established by reading PKSM's source (cross-checked at PKSM commit `b136356c`) and checked byte for
byte against a real 150-box bank (1,512,016 bytes, 273 Pokémon). Where PKSM keeps the pieces:

- `common/include/Bank.hpp` — the file-format structs (`BankHeader`, `BankEntry`), magic, version.
- `3ds/source/Bank.cpp` — load/save/resize/migrate/backup, per-slot read/write.
- `common/include/banks.hpp` + `3ds/source/banks.cpp` — the multi-bank registry (`banks.json`),
  create/rename/resize; `BANK_DEFAULT_SIZE` 50, `BANK_MAX_SIZE` 500.
- `core/source/sav/Sav.cpp` — `pksm::Sav::transfer()` (per-generation conversion dispatch) and
  `invalidTransferReason()` (the capability gate); `core/include/sav/Sav.hpp` — `BadTransferReason`.
- `core/include/pkx/PKX.hpp` + `core/source/pkx/PK*.cpp` — the `convertToGN` virtuals (its
  conversion graph).
- `3ds/source/gui/screen/StorageScreen.cpp` — the dual-grid deposit/withdraw flow.

**Layout** (little endian, packed, unencrypted):

```
0x00  BankHeader (16 B):  char MAGIC[8] = "PKSMBANK";  u32 version = 3;  u32 boxes
0x10  BankEntry[boxes * 30]
      BankEntry (0x150 = 336 B):  u32 generation tag;  u8 data[0x148];  u8 pad[4]
```

- **30 slots per box, always**, game-independent. The box count is variable (default 50, max 500);
  `resize()` reallocates and `0xFF`-fills the grown region. **The whole file is rewritten on every
  save**; the in-RAM entry array is the working copy.
- **An empty slot is `0xFF`-filled**, so its tag reads `0xFFFFFFFF`.
- **No in-file checksum.** PKSM's integrity is a magic check on load plus a SHA-256 of the blob for
  "unsaved changes?" detection. **The file size is exactly `16 + boxes * 30 * 336`** — the only
  structural check the format affords. Backups are `.bnk.bak` / `.old`.
- **Versioned layouts:** v1 (box count inferred from size, 264-byte entries) → v2 (explicit box count,
  264 bytes) → v3 (336 bytes). On load, older entries are copied and `0xFF`-padded up to the new size.
  `PKSMImportReport::containerVersion` records which one was parsed.
- **Each Pokémon is stored in its native per-generation box bytes plus the 4-byte tag — not
  normalised.** `data[0x148]` is sized to the largest box format PKSM supports (PK8, 328 bytes).
- PKSM's own transfer rules, for comparison: deposit stores as-is with no checks; withdraw runs
  `invalidTransferReason` (the destination must have the species, move, form, ability, item and ball)
  then converts **upward only**. Let's Go connects one way, up to Gen 8; nothing transfers *into* a
  Let's Go save. PKSE deliberately goes further — see "The hub" above.

**The generation tag** — a `u32` at entry offset 0. **The values are not the generation numbers**,
which is the single easiest way to misparse a bank:

| Tag | Generation | Record | Payload length | PKSE group |
| --- | --- | --- | --- | --- |
| `0` | Gen 4 | PK4 | 136 | `HGSS` (representative of DP/Pt/HGSS) |
| `1` | Gen 5 | PK5 | 136 | `B2W2` (representative of BW/B2W2) |
| `2` | Gen 6 | PK6 | 232 | `ORAS` (representative of XY/ORAS) |
| `3` | **Gen 7** | PK7 | 232 | `USUM` (representative of SM/USUM) |
| `4` | Let's Go | PB7 | 232 | `GG` |
| `5` | Gen 8 | PK8 | 328 (`0x148`) | `SWSH` |
| `6` | **Gen 3** | PK3 | 80 | `FRLG` (representative of all five GBA games) |
| `7` | Gen 1 | PK1 | 59 (JP) / 69 (INT) | `RBY` |
| `8` | Gen 2 | PK2 | 63 (JP) / 73 (INT) | `GSC` |
| `0xFFFFFFFF` | *empty slot* | — | — | — |

These are **box** lengths — no battle stats are stored. Everything past the length is `0xFF` fill.

- **Payloads are DECRYPTED, and Gen 3 is also UNSHUFFLED** — a PK3's substructures sit in canonical
  G-A-E-M order regardless of PID. Importing re-encrypts and re-shuffles; an exporter would have to
  strip both again.
- **The 4 pad bytes at `0x14C` are uninitialised memory.** 30 distinct values were observed across 273
  occupied slots (51 records of the real bank carry the ASCII `tran` there). Never checksum them,
  never `memcmp` a whole 336-byte entry, and treat them as free on export. Empty slots are the
  exception: `0xFF` throughout.
- **Gen 1/2 carry no language field.** Their layout is `[3-byte prefix][party-length record][OT]
  [nickname]`, names 6 bytes JP / 11 INT, and prefix byte 1 holds the species index — the *internal*
  Gen 1 index for tag 7, the National Dex number for tag 8. PKSM writes the record's own length and
  `0xFF`-fills the rest of the payload, so the **boundary of that fill** is the locale signal:
  `probePokeListLength()` calls a record Japanese only when every byte between the Japanese and
  international lengths is `0xFF`. That is not as naive as it looks — `0xFF` is a real character —
  but for an international record those ten bytes are the tail of the nickname field, a nickname is
  at most 10 characters in 11 bytes, so its terminator always lands inside them; all ten being `0xFF`
  would need an empty nickname with all-`0xFF` trash, which the games do not produce. Gen 1 and Gen 2
  share the probe because they share the geometry (a 10-byte gap in both).
- **The `<name>.json` box-name sidecar has a trailing NUL** (PKSM writes `length + 1` bytes), which
  strict JSON parsers reject — `parsePKSMBoxNames` strips it. The `banks.json` registry does **not**
  have one. The names array may be shorter than the box count — PKSM tops it up with generated
  defaults at load — so a short array is normal.
- **Gen 3 origin is not FireRed/LeafGreen in practice.** PK3 is one format across all five GBA games,
  and PKSM-generated Pokémon are often stamped with a version byte that is no Gen 3 game at all (the
  real bank's 250 Gen 3 records split between versions 7 and 9). They import and behave correctly;
  the importer counts them (`gen3NonFRLG`) because the legality checker has no encounter tables for
  those origins and will say so, and the user deserves to hear it at import time.

---

## Rules: Poké Transporter and the Gen 3 rebuild

The Transporter table and its three limits are in the section above; these are the rest of the rules
for the two routes that cannot be a plain field remap, with the measurements and the defects each
one came from.

### Poké Transporter

**There has never been a route out of RBY or GSC except Poké Transporter, which builds a GEN 7
record.** SM and USUM joined the conversion family to be that landing point, with `remapPK7toPK8` /
`remapPK8toPK7` (the PB7 remap plus everything Let's Go dropped — held item, EVs, contest stats, both
trainers' memories). Nothing comes back: `gate()` refuses any destination in `RBY`/`GSC`.

The rule-not-copy list, in one place (PKHeX `PK1.ConvertToPK7` / `PK2.ConvertToPK7`): nature is
`EXP % 25` of the **original** EXP; EXP is floored to the arrival level; the ability becomes the
**hidden** one bar the species Bank withholds it from (Gastly and Koffing lines, Mew, plus Misdreavus,
Unown and Celebi out of Gen 2); IVs are rerolled with three guaranteed 31s (five for Mew and Celebi);
EVs are zero; ball is a Poké Ball, met level the arrival level, met location 30013 (Kanto) or 30017
(Johto); the nickname becomes the species name unless the stored one differs, except that **Mew and
Celebi take the fateful branch INSTEAD of the nickname branch**; and Dizzy Punch is the one move
Transporter refuses to carry out of Gen 2. **Shininess is preserved as a FACT rather than a
mechanism** — a DV pattern there and a PID/ID xor here, so the rerolled PID is forced to agree.

Five things that fail quietly:

- **The moveset sanitizer must NOT run on this route.** Only `Names::isMovePresent` applies, and
  `Legality.cpp` checks such a Pokémon against the **union** of the pool it learned in and the pool it
  lives in now (chapter 11): with only the modern pool every transfer PKSE performs warns about
  itself; with only the old one, every TM taught after arrival does.
- **The origin title is a GUESS and the UI has to say so.** `virtualConsoleOriginVersion()` stamps the
  group's representative (Red, Gold) and `virtualConsoleOriginIsGuess()` says whether that was a fact;
  the confirm dialog names the title rather than saying "unknown". Gen 2 is the one exception: a record
  carrying Crystal's caught data names Crystal outright.
- **Base friendship and the growth curve come from GEN 7's table**, `getPersonalInfo7USUM(...)`, not
  the modern one. Gen 8 changed base friendship for most species (70 → 50), so the modern table would
  hand every transferred Pokémon the wrong friendship.
- **The randomness is DETERMINISTIC**, seeded by the source record's own bytes (SplitMix64).
- **A PK2 must answer `language()` and `formID()`.** Both were inherited defaults (0 and 0) until the
  transfer landed: language 0 is not a language any later format accepts, and Unown's letter is
  DERIVED from the DVs, so every transferred Unown was an A.

### A met location is not a number you can carry across generations

Gen 3's is a BYTE in its own id space; every later generation's is a u16 in a different one, and they
share nothing — PKHeX's own constants make the point, since **30013** is both the Gen 1 → Gen 7
transfer marker (`Transfer1`) and a Gen 5 Crown Beast marker (`Transfer4_CrownUsed`). Copying it into
Gen 3 truncated it, so a Gen 1 Pokémon arrived at **"Abandoned Ship" (61)** — a *Hoenn* location, in a
Kanto game, on a Pokémon that was never in either. Values under 256 did not wrap and were just as wrong
while looking more plausible.

`remapPK8toPK3` asks the same question the origin VERSION beside it asks, and answers it the same way.
**Origin-preserving** (source version 1–5) carries the location: it is already a Gen 3 id, so it
round-trips exactly, and returning to your origin game is the one backward transfer that is genuinely
legal. **Foreign** DERIVES it from a real encounter in the destination, through the tables Layer 3
already checks against (`gen3LandingFor`) — preferring a slot whose level the Pokémon can satisfy,
then a wild slot over a one-off, then the SMALLEST LEVEL SHORTFALL, then the lowest location id purely
for stability, with met level clamped into that encounter's range. No encounter anywhere in Gen 3 —
the search covers both groups — falls back to the destination's first town: a real place in the right
region, leaving the checker free to report that nothing produces it there.

### A transfer with no official route is a rebuild, and it is opt-in

`g_autoLegalizeTransfers`, Settings → "Auto-Legalize Unsupported Transfers", **off by default**. Where
a real transfer exists PKSE does what the games do and leaves the record alone; where none does —
anything entering Gen 3 from another generation — the Pokémon is being re-created as a native of the
destination, so it is re-created LEGALLY. Off, PKSE does the plain same-species lookup and leaves met
location **0**, the first entry of the game's own picker: an obviously-unset value for a user who would
rather fix it by hand, rather than a plausible one that gets missed. **Off, it never alters a level.
On, it may RAISE one — never lower it** — to the floor of the encounter it lands on.

**A met level below that floor is lifted to it, even when the level is not.** A Pokémon can outgrow an
encounter's floor after being met below it — and a Pokémon hatched in Gen 5 or later is met at level
1: a level 63 Ultra Sun Charizard met at level 1 lands on FireRed's Pallet Town gift, which starts at
5. Keeping the source's met level left it flagged at a row it otherwise fits. The met level is invented
along with the location it belongs to, so it goes to the floor and the level the user trained to stays
put. It can never end above the current level, because the level is raised first whenever it has to
be.

**Nothing is rebuilt onto an event.** Mystery Gift distributions, statics only an event ticket opens
(Mew on Faraway Island), and anything met at Gen 3's event location (`ENCOUNTER_LOCATION_EVENT3`, 255,
PKHeX `Locations.IsEventLocation3`) are not landing candidates. The location test is not redundant:
Pokémon Colosseum's gifts — the Japanese bonus disc's Pikachu and Celebi, and the Ho-Oh Colosseum hands
out after Mt. Battle — are met there while being neither distributions nor fateful. All of them were
candidates once, and did two kinds of damage. They were FALSE: a rebuilt Pokémon received no event,
and claiming one to quiet the checker is what legality exists not to do. And they HID real encounters,
because the search stops at the first game with any row at all: Ruby's only Bulbasaur is the level 70
10th-anniversary distribution, so a level 6 Bulbasaur was raised to 70 there instead of reaching
FireRed's Pallet Town gift, and a level 60 Ho-Oh from Crystal was raised to 70 as the Mt. Battle gift.
Allowing a level to rise is exactly what would let either defect pass a level check, so the exclusion
has to hold on its own.

**Measured.** Against a live PKSM bank of 252 Gen 1/2 Pokémon, before the level raise existed, the
rebuild took 93 clean to **219**; the rest were 30 whose level was below anything the destination
offers — which the raise was added for — and 3 (Celebi, Mew) that nothing can explain. The same bank
withdrawn into FireRed today goes from 92 clean with the toggle off to **247** with it on. The five
left are Mew, Celebi, Lugia and Ho-Oh, which Gen 3 only ever gave out through events or Pokémon
Colosseum, and a level 15 Quagsire whose only Gen 3 story is a Wooper that evolves at 20. Rebuilding
every Pokémon in a real collection into both Gen 3 groups — 2,204 conversions out of Gen 1, 2, 7, 8 and
9 saves — the met-level lift and the location-255 exclusion took 2,046 clean to **2,135**. The eight
that got WORSE are a Ho-Oh and a Celebi from two saves, each into both groups, and every one had been
clean only because it carried a Colosseum gift it never received.

Six rules do the work, each found as a real defect on that bank:

- **The pre-evolution chain is part of the answer**, exactly as for the checker
  (`EncounterMatch::buildChain`). A Wartortle is never caught in FireRed — it is a Squirtle that
  evolved, so the met data belongs to the Squirtle. Looking up the species alone dropped **120 of
  252** on the fallback town.
- **Half of Kanto is version-exclusive.** Sandshrew, Vulpix, Bellsprout, Slowpoke, Staryu, Magmar and
  Pinsir (with their evolutions) exist only in LeafGreen, and the mirror set only in FireRed. In the
  games you TRADE for one, and a traded Pokémon keeps the other game as its origin — so the origin is
  stamped with the version that can actually produce it. The destination's own version is tried
  first, so nothing becomes a trade that did not have to be. **Do NOT spell this as recursion**:
  FireRed's sibling is LeafGreen, whose sibling is FireRed, and it stack-overflowed on the first
  Pokémon neither has.
- **What the game cannot produce, it can still hatch.** FireRed never has the Johto starters, the
  babies, Mareep or Houndour. Gen 3 records no egg location (the field does not exist), so a hatchling
  is recognised by its fixed hatch location and level alone — a story the game cannot contradict.
  Gated on the game's own BREEDABLE set, asked of the **bottom of the chain** (a Togetic was a Togepi
  egg), and refused below the hatch level, since a met level above the current one only trades one
  finding for another. Celebi and Mew are not breedable and keep the fallback.
- **Where nothing fits, the lowest location id is not a choice.** A fine tiebreak for determinism and
  a terrible one for deciding, and with no row fitting it was doing all the deciding. A level 15
  Machop has three FireRed rows, all wild — Victory Road 32–34 (id 132), Rock Tunnel 16–17 (id 138),
  Mt. Ember 31–39 (id 175) — so it lost Rock Tunnel, ONE level short of legal, to Victory Road by six
  id numbers, and the checker then reported *"nearest is 32-34"*: true of the place it was sent, and
  useless. Ranking by level distance names the place the Pokémon nearly belongs and turns the message
  into *"nearest is 16-17"*. **The distance sits BELOW the wild preference, and that ordering is
  measured rather than assumed**: above it, distance wins 82 more cases by stamping an IN-GAME TRADE on
  a low-level Pokémon, because those rows carry wide-open bands (Electrode 3–100, Tangela 5–100) — a
  level 2 Nidoran would be claimed as the Route 11 trade, beating Route 3 (6–6) by a single level. A
  trade is a far stronger and more falsifiable claim than a patch of grass (fixed OT, fixed id, fixed
  nickname), and one level does not buy it. Below the wild preference that swap never happens and 25
  species land strictly closer; a row that FITS is unaffected either way. **Ranking by distance is also
  what makes raising a level safe**: the row a Pokémon is raised into is the one it was nearest to, so
  the change is the smallest one that works — a level 15 Machop becomes a level 16 Rock Tunnel catch,
  not a level 32 Victory Road one.
- **The other Gen 3 group is part of the answer too**, for the same reason the sibling version is.
  Hoenn and Kanto trade over a link cable, and for some species the other group is the ONLY story:
  FireRed has no Hoothoot at any level and Emerald's Safari Zone has one at 35, so stamping FireRed
  leaves a record no Gen 3 game can explain while stamping Emerald leaves one that is simply true.
  Candidates are ordered destination, then its own pair, then the other group's three — a cross-region
  trade is a stronger claim than a same-pair one, so nothing becomes one that did not have to be.
  **The TABLE is per candidate, not per destination**: RSE and FR/LG have separate tables, and asking
  the destination's for an Emerald row finds nothing at all. This also changed which species
  demonstrate the egg branch. Chikorita no longer can, because Emerald hands out a Johto starter at
  Littleroot Town once the Hoenn dex is complete, so a real gift explains it now — the better answer.
  The BABIES are the egg branch's cases instead: Pichu, Cleffa, Igglybuff, Togepi, Tyrogue, Smoochum,
  Elekid and Magby are catchable nowhere in Gen 3 and breedable everywhere in it.
- **An egg row describes the egg, not the meeting.** Togepi's FR/LG row is a gift egg at location
  253; a hatched one is met at 146. Taking the row's own location stamped Togepi as met at "(gift
  egg)", which the checker rightly refuses.

### Moves, Unown, and what the rebuild can and cannot claim

**Moves follow the same supported/unsupported split.** The Poké Transporter rule stands, but a Gen 3
destination is a rebuild rather than a carry, so a move the fabricated encounter could never have
taught is dropped. **Gen 3's own two groups are exempt**: RSE and FR/LG trade on real hardware and
trading does not revalidate a moveset, so an Emerald Battle Frontier tutor move survives a move into a
FireRed save even though FireRed's pool is narrower.

**Unown's letter is the PID in Gen 3** — see `unownFormFromPID` / `withUnownFormValue` in
`Pokemon3FRLG.h`, which the entity layer and the conversion share (there were FIVE copies of that bit
twiddle). A reroll that ignores it RENAMES the Pokémon, and because the Tanoby chambers hold five
letters each, the relettered Unown also lands somewhere its own letter never appears. Preserved by
CONSTRUCTION, not filtering: every candidate gets a bit pattern that already yields the wanted letter,
so the search costs what it did rather than 28× more. The letter also decides PID bit 0 and with it the
Gen 3 ability bit, so for Unown the ability test is dropped — it has one ability, so there is no
second slot to miss.

**That is "possible", not "legal", and the difference is the point.** No transfer into Gen 3 has ever
existed, so a Pokémon that did not originate there is illegal whatever it carries — the PID is
rerolled too and will not match a real Gen 3 RNG frame. Do not reach for a value that merely quiets
PKSE's own checker: a fateful-encounter sentinel would have done exactly that, and legality exists to
predict what HOME will accept, not what PKSE will. This follows PKHeX, which derives a synthesized
location from a matched encounter (`IEntityRejuvenator`: `MetLocation = enc.Location`); PKSM instead
stamps a fixed `0xFD` "(gift egg)" with a comment saying it is a guess.
