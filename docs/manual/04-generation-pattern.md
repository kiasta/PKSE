# 04 — The generation-per-class pattern

This is the core abstraction. Everything else in the codebase is downstream of it: parallel class
hierarchies keyed by a save-format suffix. Adding a game means a new derived class in each layer —
base classes and UI never hardcode a game.

## The rule

**Every save-format group gets its own class in every layer, even when the logic is identical.**

There are no exceptions, pre-Switch included. Four parallel hierarchies, all keyed by the same
suffix — **`1RBY`, `2GSC`, `3RSE`, `3FRLG`, `4DP`, `4PT`, `4HGSS`, `5BW`, `5B2W2`, `6XY`, `6ORAS`,
`7SM`, `7USUM`, `7LGPE`, `8SWSH`, `8BDSP`, `8LA`, `9SV`, `9LZA`**, nineteen in all — which is the
`GameVersion` group's own name, so a class and the value the rest of PKSE dispatches on cannot
drift apart:

```
Trainer::Trainer          (abstract)
  ├── Trainer1RBY     Red/Blue/Yellow
  ├── Trainer2GSC     Gold/Silver/Crystal
  ├── Trainer3RSE     Ruby/Sapphire/Emerald
  ├── Trainer3FRLG    FireRed/LeafGreen
  ├── Trainer4DP      Diamond/Pearl
  ├── Trainer4PT      Platinum
  ├── Trainer4HGSS    HeartGold/SoulSilver
  ├── Trainer5BW      Black/White
  ├── Trainer5B2W2    Black 2/White 2
  ├── Trainer6XY      X/Y
  ├── Trainer6ORAS    Omega Ruby/Alpha Sapphire
  ├── Trainer7SM      Sun/Moon
  ├── Trainer7USUM    Ultra Sun/Ultra Moon
  ├── Trainer7LGPE    Let's Go
  ├── Trainer8SWSH    Sword/Shield
  ├── Trainer8BDSP    Brilliant Diamond/Shining Pearl
  ├── Trainer8LA      Legends: Arceus
  ├── Trainer9SV      Scarlet/Violet
  └── Trainer9LZA     Legends: Z-A

Pokemon::Pokemon          (abstract)
  ├── Pokemon1RBY  … Pokemon9LZA          — the same nineteen suffixes

Encryption::                              — free functions, one file per group
  Encryption3RSE (forwards to 3FRLG — see below), Encryption3FRLG, Encryption4DP, Encryption4PT, Encryption4HGSS,
  Encryption5BW, Encryption5B2W2, Encryption6XY, Encryption6ORAS,
  Encryption7SM, Encryption7USUM, Encryption7LGPE, Encryption8SWSH,
  Encryption8BDSP, Encryption8LA, Encryption9SV, Encryption9LZA
                       (NO Encryption1RBY / 2GSC — Gens 1 and 2 aren't encrypted; a real
                        difference, not an omission)

Trainer::Inventory*                       — structs + pouch tables, one header per group
  Inventory1RBY, Inventory3RSE, Inventory3FRLG … Inventory9LZA
```

What each layer owns:

- **`Trainer::Trainer`** (`include/Trainer/Trainer.h`) — trainer info, items, box names, and the
  polymorphic `party` / `boxes` (vectors of `unique_ptr<Pokemon>`). The pure virtuals
  (`updatePartyBlock()`, `updateBoxBlock()`, `updateItemBlock()`, `getBoxCount()`, …) handle the
  generation-specific serialisation; the full contract is below.
- **`Pokemon::Pokemon`** (`include/Pokemon/Pokemon.h`) — wraps a decrypted byte buffer; getters and
  setters read and write raw offsets. Sizes, the party tail, the stat formulas and the
  mutate → recalculate → checksum contract are chapter 06.
- **`Encryption`** — block decrypt/encrypt, XOR pad, block shuffle/unshuffle, hashing. Gen 3 is the
  odd one out: no encryption constant, keyed off the PID.
- **`Trainer::Inventory*`** — the per-group pouch structs and tables.

**One deliberate exception to the copy:** `Encryption3RSE.h` *forwards* to `Encryption3FRLG` rather
than copying it. The PK3 crypt has no per-game constants, and a second copy of the 24-entry shuffle
table is the one Gen 3 transcription error that would fail silently — a wrong permutation still
decrypts to *something*, and the checksum is computed over the result.

### Why a copy, when two groups share a record byte for byte

Diamond, Pearl, Platinum, HeartGold and SoulSilver all store a PK4; Black/White and Black 2/White 2
a PK5; X/Y and ORAS a PK6; Sun/Moon and Ultra Sun/Moon a PK7. Scarlet/Violet and Legends: Z-A share
the Gen 9 record just as completely. The copy is still the rule, and sharing these cost something
concrete before they were separated:

- **`getGameGroup()` lied.** One class per FORMAT had to report one group, and it reported the
  superset: every PK4 said HGSS, every PK5 B2W2, every PK6 ORAS, every PK7 USUM. So a Black/White
  Pokemon was checked against B2W2's learn pool — which has Move Tutor moves Black/White never had —
  and a Sun/Moon save's dex checks used 807 species instead of 802. Separating them fixed a real
  bug, not just the shape. One consequence: `Bank.cpp` maps all nine of those groups, because its
  on-disk tag is one per entity FORMAT (frozen, and a banked record has no game of origin) — see
  "Bank tags name a format" below.
- **The size test had to know about its neighbour.** `Pokemon4DPPt`'s constructor read
  `isGen45Size(size) && size != SIZE_PARTY5` — "any Gen 4/5 length except the *other* generation's
  party record". A group's own `isSize*()` accepts only its own two lengths, so the exclusion is gone.
- **The Gen 6/7 container was a mode flag.** `Trainer67` chose its entity class, its item codec, its
  OT offset, its money offset and its current-box offset from `generation() == 7`. Each container now
  has only its own.

### A group's geometry is a constant in its class, not a virtual call

The nine pre-Switch block containers are self-contained: block table, offsets, footer size and CRC
are plain members of the class that uses them. The Gen 6/7 containers carry no
`generation() == 7 ? … : …` — a Gen 6 container has no Gen 7 item codec compiled into it, which is
exactly the mode flag the copy-per-game rule exists to prevent. Gen 7 keeps its MemeCrypto signing;
Gen 6 simply does not have it.

### A generation-wide constant belongs to the class, not the namespace

`GEN4_SAVE_SIZE`, `GEN4_PARTITION`, the SDK magics and `GEN5_FILE_SIZE` are `static constexpr`
members of each group's Trainer class (`Trainer4DP`, `Trainer4PT`, `Trainer4HGSS`, `Trainer5BW`,
`Trainer5B2W2`). At namespace scope they were redefinitions the moment two of a generation's group
headers met in one translation unit — which is every file dispatching over the loose formats.

### Bank tags name a format, so never compare one to the entity's own group

The bank's on-disk tag is one per entity **format**, so `groupForBankTag()` returns the format's
*representative* group — a Ruby PK3 reads back as FR/LG, a Platinum PK4 as HGSS, an X/Y PK6 as
ORAS, a Sun PK7 as USUM. **Never compare a bank tag's group to the entity's own
`getGameGroup()`.** `Bank::verifyImage` once asserted `groupForBankTag(tag) ==
source->getGameGroup()` and so rejected every shared-format record *before* the round trip it exists
to perform; `Bank::save()` counts a rejection as damage, so a user banking Gen 3–7 was told their
bank was corrupt while the bytes were fine. The invariant that holds is
`tag == bankTagForEntity(entity)`, then decode through the tag as the loader does.
`Bank::groupAsBanked(group)` is the public form for anything comparing a group across the bank.
Chapter 10 has the rest of the bank.

### Gen 3 has two groups: ask `isGen3Group`, never `== FRLG`

`FRLG` (FireRed/LeafGreen — a Switch title and a GBA file) and `RSE` (Ruby/Sapphire/Emerald, loose
GBA files only) share the PK3 record, the item id space, the dex, the crypt and much more (chapter
06 has the full list). **Ask `Enums::isGen3Group(group)` (`include/Enums/GameVersion.h`), never
`group == GameVersion::FRLG`.** That comparison became a silent bug the moment FR/LG were not the
only Gen 3 group: a Ruby Pokemon would have its held item named out of the modern item table, its
trainer id printed with six digits, an ability *number* written into a format with no field for one,
and its relearn moves sanitized against a mechanic Gen 3 does not have. Eleven call sites said it;
they all ask the predicate now. The general form of the rule: a question about a *family* of groups
gets a predicate, never a comparison against the one member that existed when the code was written.

## Why duplication is correct here

`Trainer9SV` and `Trainer9LZA` are near-identical. They share the Gen 9 SCBlock container and the
Gen 9 entity format. The only structural difference is that Scarlet/Violet **packs** its box/party
slots while Z-A **gaps** them.

The obvious refactor — one `Trainer9` with a `bool packed` — is the wrong call, and the reason is
worth internalising because it applies to every future game:

- **Each game stays readable in isolation.** You can open `Trainer9SV.cpp` and see what
  Scarlet/Violet does without holding Z-A's differences in your head.
- **A change to one cannot silently break the other.** The shared-class version turns every edit
  into a two-game regression risk, and the two games are validated separately on hardware.
- **The differences are never as small as they look.** They start as one flag and accrete. Block
  keys diverge, a DLC block appears in one, a field moves by four bytes in a patch.

Prefer a clean copy. When you find yourself reaching for a mode flag inside a per-game class, that
is the signal to copy the class instead.

The *exception* is genuinely shared machinery, which lives in the base class or in a free function:
`Encryption::cryptPokemon`, `Pokemon::getPersonalInfo`, `Names::getItemNameFor`. Those are shared
because they are the same algorithm, not because two games happen to resemble each other.

## Dispatch without RTTI

**RTTI and exceptions are disabled** (`-fno-rtti`, `-fno-exceptions`): do not use `dynamic_cast`
or `throw`. Type dispatch is `virtual GameVersion getGameGroup()` plus the `GameVersion` enum, never
RTTI. Every layer exposes its identity through that virtual:

```cpp
// Trainer.h
virtual GameVersion getGameGroup() const noexcept = 0;
// Pokemon.h
virtual Enums::GameVersion getGameGroup() const noexcept = 0;
```

and callers branch on the enum, then `static_cast` if they need the concrete type. The canonical
example is the save dispatcher:

```cpp
// Save/GetSaveFileContents.cpp
GameVersion trainerGroup = trainer.getGameGroup();
if (trainerGroup == GameVersion::GG) {
    ok = saveTrainerInfoLetsGo(static_cast<Trainer::Trainer7LGPE&>(trainer), ...);
} else if (trainerGroup == GameVersion::SWSH) {
    ok = saveTrainerInfoSwSh(static_cast<Trainer::Trainer8SWSH&>(trainer), ...);
} else if (trainerGroup == GameVersion::ZA) {
    ok = saveTrainerInfoLZA(static_cast<Trainer9LZA&>(trainer), ...);
} /* … */
```

The cast is safe *because the virtual just told us what this is*. That is the contract: a class's
`getGameGroup()` must return the group whose concrete class it is, always, unconditionally.

For an **entity**, `getGameGroup()` is the *storage format* group, not the origin `Version` byte:

```cpp
// Pokemon8SWSH.h
/** Storage-format game group (this subclass), NOT the origin Version byte. */
Enums::GameVersion getGameGroup() const noexcept override { return Enums::GameVersion::SWSH; }
```

A FireRed Rattata sitting in a Shield box has `getGameGroup() == SWSH` (it is stored as a PK8) and
`originGame() == 4` (it came from FireRed). Confusing the two is a real bug class — see chapter 07's
note on `locationTableVersion`.

## `GameVersion` — the dispatch hub

`include/Enums/GameVersion.h` is a header-only file of `inline` functions, and it is the single
place that knows the mapping from a console concept to a code path.

The enum holds three different kinds of value in one namespace, which is confusing until you see
why: the low values are the **stored `Version` byte** in a PKM (they match PKHeX exactly, so a
transferred mon's origin is directly comparable), and the high values are **groupings** PKSE invents
for dispatch.

```cpp
enum class GameVersion {
    Any = 0, Invalid = 255,

    // --- values stored INSIDE a PKM's Version field (match PKHeX) ---
    SA = 1, RU = 2, EM = 3, FR = 4, LG = 5,       // Gen 3
    HG = 7, SS = 8, D = 10, P = 11, Pt = 12,      // Gen 4
    W = 20, B = 21, W2 = 22, B2 = 23,             // Gen 5 (PKHeX's pairing order)
    X = 24, Y = 25, AS = 26, OR = 27,             // Gen 6
    SN = 30, MN = 31, US = 32, UM = 33,           // Gen 7 (3DS)
    RD = 35, GN = 36, BU = 37, YW = 38,           // Gen 1 (write-only: a PK1 stores no version)
    GD = 39, SI = 40, C = 41,                     // Gen 2 (write-only: a PK2 stores none either)
    GP = 42, GE = 43,                             // Let's Go
    SW = 44, SH = 45,
    PLA = 47, BD = 48, SP = 49,
    SL = 50, VL = 51, ZA = 52,

    // --- groupings PKSE uses for dispatch (NOT stored anywhere) ---
    FRLG = 72, GG = 73, SWSH = 74, BDSP = 75, SV = 76, RBY = 77, GSC = 78,
    DP = 79, PT = 80, HGSS = 81, BW = 82, B2W2 = 83,
    Gen7B = 84, Gen8 = 85, Gen9 = 86,
    XY = 87, ORAS = 88, SM = 89, USUM = 90, RSE = 91
};
```

New group values take the next free number; 84–86 were already taken by the generational
groupings, which is why the Gen 6/7 groups resume at 87.

Note `PLA` and `ZA` do double duty: they are both the stored version byte and their own group,
because each is a single game rather than a pair.

### The functions, and when to reach for each

| Function | Use it for |
|---|---|
| `getGameVersion(titleId)` | Switch title ID → the concrete game. The only place title IDs live. |
| `getGameGroup(version)` | A concrete game → its save-format group. `SW`/`SH` → `SWSH`. |
| `getGameVersionName(v)` | Human name. Groups name the pair (`"Sword/Shield"`). |
| `getSaveFileName(group)` | On-disk save file name (`main` vs `savedata.bin` vs `SaveData.bin`). |
| `isGameSupported(v)` | Informational: does PKSE implement this title or group? Derived from `getGameGroup`. **Gates nothing** — what exposes a game is its entry point: a title id in `getGameVersion()`, or a `LOOSE_FORMATS` row. |
| `isGen3Group(group)` | Is this either Gen 3 group? Ask this, never `== FRLG` (above). |
| `getOriginGameName(byte)` | Name a raw origin byte, **Gen 1 through 9**, including games PKSE cannot edit. |
| `getVersionGeneration(byte)` | Generation 1–9 of an origin byte; `0` = unknown. |
| `getGroupRepVersion(group)` | A representative stored version for a group, for table routing. |
| `usesSixDigitTrainerID(byte)` | Whether to render the trainer ID as six digits or as a 16-bit TID. |
| `locationTableVersion(origin, format, isEgg)` | **Which** met-location table names a location. |

`getOriginGameName` covers every game a HOME-transferred mon can carry — Sun/Moon, the Gen 4/5/6
titles, Pokémon GO, the Virtual Console re-releases — not just the ones PKSE edits, so the details
view shows a transferred mon's real game rather than "Unknown".

`getVersionGeneration` had a real bug worth remembering as a shape: versions 35–41 (the Virtual
Console re-releases) were **absent** while `getOriginGameName` already named all seven, so a
transferred Gen 1/2 mon showed a real game name beside generation `0` — and `0` is this function's
"unknown", which callers read as a definitive negative. Same failure shape as a missing data table
(chapter 14).

### Adding a title ID

```cpp
inline GameVersion getGameVersion(uint64_t titleId) {
    switch (titleId) {
        case 0x0100554023408000: return GameVersion::FR;   // FireRed
        case 0x0100ABF008968000: return GameVersion::SW;   // Sword
        /* … */
        default: return GameVersion::Invalid;
    }
}
```

Title IDs come from **real Switch backups**, not from a database. Get them off a console.

## The `Trainer` base class contract

`include/Trainer/Trainer.h`. The base owns everything common; the derived class owns everything
format-specific.

**Owned by the base** (plain public members, mutated directly by the UI):

```cpp
std::string trainerName;
uint32_t    money;
uint32_t    ID32;                    // SID16 << 16 | TID16
uint16_t    TID16, SID16;
uint32_t    TID, SID;                // Gen 7+ six-digit display form
uint8_t     trainerGender;
int         saveRevision;            // DLC tier
std::string saveRevisionString;      // "Base", "IoA", "CT"
std::vector<std::vector<InventoryItem>> items;      // [pouch][slot]
std::vector<std::string> boxNames;
std::vector<bool>        boxNameDirty;
uint8_t     currentBox;
std::vector<std::unique_ptr<Pokemon::Pokemon>> party;
std::vector<std::array<std::unique_ptr<Pokemon::Pokemon>, BOX_SLOTS>> boxes;
protected:
std::vector<Save::Block> blocks;     // the parsed save, for re-serialisation
```

**Pure virtual** — a missing override is a compile error, which is the point. You cannot add a game
and forget to persist its trainer edits:

```cpp
virtual void updatePartyBlock() = 0;
virtual void updateBoxBlock() = 0;
virtual void updateItemBlock() = 0;
virtual void updateTrainerInfoBlock() = 0;
virtual std::unique_ptr<Pokemon::Pokemon> createBlankPokemon() const = 0;
virtual size_t getBoxCount() const noexcept = 0;
virtual size_t getSlotsPerBox() const noexcept = 0;
virtual size_t getPartySize() const noexcept = 0;
virtual GameVersion getGameGroup() const noexcept = 0;
```

**Virtual with a default** — override only where the game differs. Each default was chosen so that
"not overridden" is the *safe* answer:

```cpp
virtual void   updateBoxNameBlock()      {}          // no-op where names aren't stored
virtual void   updateCurrentBoxBlock()   {}
virtual void   updatePokedexBlock()      {}          // no-op where the dex format isn't implemented
virtual bool   supportsBoxNames() const noexcept { return false; }
virtual size_t getMaxBoxNameLength() const noexcept { return 0; }   // 0 = renaming unsupported
virtual bool   canStoreBoxName(const std::string&) const { return true; }
virtual bool   compactStorage() { return false; }    // MUST stay a no-op for positional games
virtual size_t getItemPouchCapacity(int pouch) const { return 0; }  // 0 = appending unsupported
virtual bool   itemsAreIdIndexed() const { return false; }
virtual uint32_t getMaxMoney() const noexcept { return 9999999; }
virtual size_t getMaxTrainerNameLength() const noexcept { return 12; }
virtual int    getMaxTrainerNameDigits() const noexcept { return 5; }
virtual bool   isStarterPokemon(size_t, size_t) const noexcept { return false; }
virtual int    getPartyPosition(size_t, size_t) const noexcept { return 0; }
virtual bool   isPartyPokemonStarter(size_t) const noexcept { return false; }
virtual void   mirrorPartyMemberFromBox(size_t, size_t) {}
virtual void   mirrorPartyMemberFromParty(size_t) {}
virtual void   swapBoxSlots(size_t, size_t, size_t, size_t);   // has a working default
```

### Five of these deserve explanation

**`compactStorage()` must stay a no-op by default.** Only Let's Go needs it: its 1000 storage slots
are one gapless list, and the party and partner are *indices* into it, so a hole is not a state the
game can represent. Every other game stores boxes **positionally** with an explicit blank for an
empty slot — re-packing one of those would shuffle Pokémon the player arranged on purpose. (The
roadmap once listed Legends: Arceus here; that was wrong. PA8 writes box slots at
`(box*slots + slot) * stride` with a blank record for empties, exactly like Sword/Shield. "Packed"
in the LA code means a byte *stride* with no padding between records, which is a different thing
from a gapless occupancy list.)

**`itemsAreIdIndexed()` decides how the editor removes an item**, because the two storage models
save in opposite ways:

| Model | Games | Remove means | Retype means |
|---|---|---|---|
| slot-based (`{id,count}` per slot) | FRLG, LGPE, SWSH, LA | **erase** the entry; the region is rewritten | reassign `itemId` |
| id-indexed (count at `itemId * stride`) | BDSP, S/V, Z-A | set `count = 0` but **keep** the entry | — |

Erasing an id-indexed entry ghosts the old count, because `updateItemBlock()` only touches ids still
present in the vector.

**`boxNameDirty` exists because writing the whole array back is wrong.** Every game's box-name
parser substitutes a display default ("Box 3") when the save holds an empty name, so `boxNames` is a
mix of real names and placeholders. `updateBoxNameBlock()` writes **only dirty entries**. Writing
the whole array back once added 215 bytes of invented names to an untouched Z-A save.

```cpp
void markBoxNameDirty(size_t box);
bool isBoxNameDirty(size_t box) const noexcept;
```

**`updatePokedexBlock()` runs at save time over the whole storage**, not at the point a Pokémon is
created or withdrawn. There are several ways a Pokémon can enter a save (the creator, a bank
withdrawal, a bulk move, a party swap) and hooking each one is how a path gets missed. Walking
storage once catches all of them, and it is self-healing: a Pokémon an older build added without
registering gets picked up next save. It only ever **sets** flags, so a player's dex cannot be
walked backwards.

**`createBlankPokemon()` must not hand raw zeros to an entity constructor.** It synthesises a zeroed
*decrypted* buffer of the generation's slot size, encrypts it with seed/EC 0, then constructs the
entity from those encrypted bytes (whose constructor decrypts them straight back to zeros). Raw
zeros decrypt to garbage and produce a Bad Egg. This mirrors each generation's own
empty-box-slot blank in `updateBoxBlock()`.

## Ownership rules

`Pokemon` and `Trainer` **delete copy and allow move**. They hold large buffers and `unique_ptr`s.

```cpp
Pokemon(const Pokemon&) = delete;
Pokemon& operator=(const Pokemon&) = delete;
Pokemon(Pokemon&&) noexcept = default;
Pokemon& operator=(Pokemon&&) noexcept = default;
```

Respect this: pass by reference or move, never copy. To genuinely duplicate an entity, use
`clone()`:

```cpp
/** Deep-copy: re-encrypt the decrypted buffer and rebuild via the encrypted-span ctor. */
std::unique_ptr<Pokemon> clone() const override {
    uint32_t ec = readUInt32LittleEndian(reinterpret_cast<const uint8_t*>(data.data()));
    std::byte* enc = encryptArray8SWSH(std::span<const std::byte>(data.data(), dataSize), ec);
    auto c = std::make_unique<Pokemon8SWSH>(std::span<const std::byte>(enc, dataSize));
    delete[] enc;
    return c;
}
```

Base `clone()` returns `nullptr`, so a format that has not implemented it fails visibly rather than
producing a shallow copy.

## A non-null box slot is not an occupied box slot

This bites everyone once, so it gets its own section.

Several games keep a **live entity in every slot** rather than a null pointer. Sword/Shield's box
writer deliberately stamps one canonical encrypted blank over each unoccupied slot, because a slot
holding leftover bytes that decrypt to species 0 re-encrypts into a **Bad Egg**.

So the test for "empty" is, everywhere:

```cpp
const bool empty = (!pk || pk->speciesID() == 0);
```

Code that tests the pointer alone finds a Pokémon in all thirty slots of an empty box. Getting this
wrong once reported 1,849 phantom Pokémon across one sweep of real saves' boxes and 936 more in a
save round trip, independently.

## Box geometry

Box sizes vary, and the *layout* rule is not "fill the panel".

```cpp
// UI/Common.h — the single source of truth
constexpr int boxGridColumns(int slotsPerBox) {
    const int cols = slotsPerBox / 5;
    return (cols >= 1 && cols * 5 == slotsPerBox) ? cols : 6;
}
inline constexpr int kBoxGridRows = 5;
inline constexpr int kBoxGridRefCols = 6;      // the bank's width — the spacing reference

constexpr int boxGridCenterOffset(int cols, int colPitch) {
    const int slack = kBoxGridRefCols - cols;
    return slack > 0 ? (slack * colPitch) / 2 : 0;
}

static_assert(boxGridColumns(30) == 6, "a 30-slot box is 6x5");   // most games, and the bank
static_assert(boxGridColumns(25) == 5, "a 25-slot box is 5x5");   // Let's Go
static_assert(boxGridColumns(20) == 4, "a 20-slot box is 4x5");   // Gen 1 international
```

Every box in PKSE is **five rows** with a width that follows the box size. A narrow grid is
**centred at the normal cell pitch, never stretched**. Stretching is the obvious implementation and
it is wrong: a 20-slot box spread over six columns' worth of width gives cells half again the bank's
size (145 px vs 96 px in the storage view), so the cursor visibly changes scale as it crosses from
the save pane to the bank pane and the two grids stop lining up row for row.

Centring makes a 4-wide box look exactly like a 6-wide one with its outer columns removed — the
offset works out to precisely one column. Because the *origin* is what moves, `cellX()`, the
multi-select rectangle, the drag preview and the touch targets all shift together and cannot
disagree with what is drawn.

This replaced **six** independent copies of `(slots == 25) ? 5 : 6` across the box panel, the HOME
menu panel, the storage panel, the storage placer, `paneCols()` and the box-tap hit test.
