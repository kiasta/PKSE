# Generation 7 (3DS) — Sun / Moon / Ultra Sun / Ultra Moon (PK7, SAV7)

> **Status: implemented.** This is the PKHeX transcription Gen 7's 3DS titles were built from, kept
> as the reference for that code. It was written as a plan, so where §10 and §13 name files, the
> tree differs in one systematic way: **every layer has one class per group, with no shared base** —
> `Trainer7SM` / `Trainer7USUM`, `Pokemon7SM` / `Pokemon7USUM`, `Encryption7SM` / `7USUM`,
> `Inventory7SM` / `7USUM` — rather than the `Trainer7`/`Trainer67` base, single PK7 class and
> `Inventory7` sketched below (§10.3 says why). The block tables (`Trainer/Blocks7SM.h`,
> `Blocks7USUM.h`) come from `tools/gen_blocktables.py`; personal data from
> `tools/gen_personaltables.py`, not a `gen_gen7.py`; text goes through `Utils/Utf16Text.h`. The
> groups are `SM = 89`, `USUM = 90`. **Saves are signed** (§10.3a). The bank stores every PK7 under
> one tag, `BTAG_G7`, and **SM/USUM do convert**: they are in `Conversion::gate`'s `inFamily`
> (`remapPK7toPK8` / `remapPK8toPK7`) and are the landing point for Gen 1/2 Pokemon arriving through
> the Poke Transporter rule ([manual ch. 10](../manual/10-bank-and-conversion.md)).

> **Source of truth: PKHeX.** Every offset below was transcribed at commit `6501f0ab46e8f8ca048539dbaf8cae8cb104e722`;
> PKSE now tracks PKHeX's default branch, so re-verify against the file you fetch.
> Files: `Saves/SAV7.cs`, `Saves/SAV7SM.cs`, `Saves/SAV7USUM.cs`,
> `Saves/Access/SaveBlockAccessor7SM.cs`, `Saves/Access/SaveBlockAccessor7USUM.cs`,
> `Saves/Blocks/BlockInfo3DS.cs`, `Saves/Substructures/Gen7/BoxLayout7.cs` / `MyStatus7.cs` /
> `Misc7.cs` / `MyItem7SM.cs`, `PKM/PK7.cs`, `PKM/Shared/G6PKM.cs`, `PKM/Util/PokeCrypto.cs`,
> `PKM/Strings/StringConverter7.cs`, `Saves/Util/Checksums.cs`, `Saves/Util/SaveUtil.cs`,
> `Items/Bags/PlayerBag7SM.cs`, `Legality/Legal.cs`.

---

## 0. Read this first

> **This document is a DELTA on [GEN6.md](GEN6.md). Read that first.** Everything not contradicted
> here is identical: the BEEF metadata chunk, the block-table structure, the entity crypt
> (`Decrypt67`/`Encrypt67`), the `SIZE_6STORED`/`SIZE_6PARTY` record sizes, the `IV32` packing, the
> `Add16` entity checksum, UTF-16 text, and most of the PK offset map.

**Gen 7 is the closest of these five generations to what PKSE already does**, because Let's Go
(`7LGPE`, PB7) is already in the tree and is a Gen 7 format. Expect `Pokemon7LGPE` and
`Trainer7LGPE` to be useful references, not just PKHeX.

The differences that matter are exactly five:

| # | Difference from Gen 6 | Consequence |
|---|---|---|
| 1 | **Save-block CRC is `CRC16Invert`, not `CRC16_CCITT`** | different function, no warning if wrong |
| 2 | **32 boxes**, not 31 | box count, box-layout offsets |
| 3 | **Hyper Training at `0xDE`** (Gen 6's ground-tile byte) | `hasHyperTraining()` → true; `effectiveIV()` matters |
| 4 | **`AbilityNumber` is masked to 3 bits** (`Data[0x15] & 7`) | not the whole byte |
| 5 | **`MarkingValue` is 16-bit** at `0x16`; `0x2A` becomes `ResortEventStatus` | two fields move |

Plus: bigger dex, more items, and USUM's own block table.

---

## 1. Scope

| | |
|---|---|
| Games | Sun, Moon (`SM`), Ultra Sun, Ultra Moon (`USUM`) |
| Groups | `GameVersion::SM`, `GameVersion::USUM` |
| Entity | **PK7** — same sizes as PK6: `0xE8` stored, `0x104` party |
| Save file | `main`, **SM `0x6BE00`**, **USUM `0x6CC00`** |
| Entity encryption | `Decrypt67` / `Encrypt67` — **identical to Gen 6** |
| Entity checksum | `Add16` over `data[0x08 .. 0xE8)` |
| Save checksum | **`CRC16Invert`** per block ⚠ |
| Boxes | **32 × 30** |
| Max species | SM 802, USUM 807 |
| Max move id | SM 719, USUM 728 |
| Max item id | SM 920, USUM 959 |
| Max ability id | SM 232, USUM 233 |
| Max ball id | `0x1A` (26) |
| OT / nickname | 12 chars, 26-byte fields |
| Box name | 17 chars, `0x22`-byte field |
| Max money | 9,999,999 |

---

## 2. The save container

### 2.1 Same BEEF chunk, different CRC

Structure is byte-for-byte Gen 6's (see [GEN6.md](GEN6.md) §2.1):

```
BlockMetadataOffset = saveSize - 0x200
  +0x00  u64 timestamp1
  +0x08  u64 timestamp2
  +0x10  u8[4] "BEEF"
  +0x14  blockInfo[i] { u32 length; u16 id; u16 checksum }   (8 bytes each)

ChecksumOffset(id) = BlockMetadataOffset + 0x14 + id*8 + 6
```

```
SM   BlockMetadataOffset = 0x6BE00 - 0x200 = 0x6BC00
USUM BlockMetadataOffset = 0x6CC00 - 0x200 = 0x6CA00
```

### 2.2 ⚠ The CRC variant

```csharp
// BlockInfo3DS.cs
public sealed class BlockInfo6 (…) { GetChecksum => Checksums.CRC16_CCITT(...);   }  // Gen 6
public sealed class BlockInfo7 (…) { GetChecksum => Checksums.CRC16Invert(...);   }  // Gen 7  <--
public sealed class BlockInfo7b(…) { GetChecksum => Checksums.CRC16NoInvert(...); }  // Let's Go
```

`CRC16Invert` is a **table-driven** CRC-16 with a 256-entry polynomial table, initial value
`0xFFFF`, result inverted:

```csharp
private static ushort CRC16(ReadOnlySpan<byte> data, ushort initial)
{
    uint chk = initial;
    foreach (var b in data)
        chk = crc16[(byte)(b ^ chk)] ^ (chk >> 8);
    return (ushort)chk;
}
public static ushort CRC16Invert  (ReadOnlySpan<byte> data) => (ushort)~CRC16(data, 0xFFFF);
public static ushort CRC16NoInvert(ReadOnlySpan<byte> data) =>          CRC16(data, 0);
```

The 256-entry `crc16` table is in `Saves/Util/Checksums.cs`. **Copy it verbatim** — it starts
`0x0000, 0xC0C1, 0xC181, 0x0140, 0xC301, 0x03C0, 0x0280, 0xC241, …`. This is the standard
CRC-16/ARC (IBM) table; the reversed polynomial is `0xA001`, so you can generate it instead of
transcribing it:

```cpp
// include/Utils/CRC16.h -- generate the table rather than transcribing 256 constants.
namespace Utils {
    namespace detail {
        inline constexpr auto kCrc16Table = [] {
            std::array<uint16_t, 256> t{};
            for (uint32_t i = 0; i < 256; ++i) {
                uint32_t c = i;
                for (int k = 0; k < 8; ++k) c = (c & 1) ? ((c >> 1) ^ 0xA001u) : (c >> 1);
                t[i] = static_cast<uint16_t>(c);
            }
            return t;
        }();

        inline uint16_t crc16(const uint8_t* d, size_t n, uint16_t initial) noexcept {
            uint32_t chk = initial;
            for (size_t i = 0; i < n; ++i)
                chk = kCrc16Table[static_cast<uint8_t>(d[i] ^ chk)] ^ (chk >> 8);
            return static_cast<uint16_t>(chk);
        }
    }

    /// PKHeX Checksums.CRC16Invert. GEN 7 save blocks.
    /// NOT the same as crc16ccitt() (Gen 4 / Gen 6) -- different algorithm, different output.
    inline uint16_t crc16Invert(const uint8_t* d, size_t n) noexcept {
        return static_cast<uint16_t>(~detail::crc16(d, n, 0xFFFFu));
    }

    /// PKHeX Checksums.CRC16NoInvert. Let's Go save blocks (not needed unless PKSE reworks LGPE).
    inline uint16_t crc16NoInvert(const uint8_t* d, size_t n) noexcept {
        return detail::crc16(d, n, 0);
    }
}
```

> **Verify the generated table against PKHeX's first eight entries before trusting it.** A
> `static_assert` on `kCrc16Table[1] == 0xC0C1 && kCrc16Table[2] == 0xC181` costs nothing and turns
> a silent wrong-polynomial bug into a build failure.

### 2.3 Detection

```csharp
private static bool IsG7SM  (data) => data.Length is SIZE_G7SM   && HasSaveFooterBEEF(data);
private static bool IsG7USUM(data) => data.Length is SIZE_G7USUM && HasSaveFooterBEEF(data);
// SIZE_G7SM = 0x6BE00,  SIZE_G7USUM = 0x6CC00
```

Same BEEF magic at `size - 0x1F0` as Gen 6 and as Let's Go (`0x100000`). **Size discriminates.**
Use the shared `detectGen6`-shaped helper from [GEN6.md](GEN6.md) §2.3 with the Gen 7 sizes.

### 2.4 The SM block table (37 blocks)

`(id, offset, length)` from `SaveBlockAccessor7SM.cs`.

```
 id  offset    length    what
 00  0x00000   0x00DE0   MyItem                  <- the bag
 01  0x00E00   0x0007C   Situation
 02  0x01000   0x00014   RandomGroup
 03  0x01200   0x000C0   MyStatus                <- TRAINER
 04  0x01400   0x0061C   PokePartySave           <- PARTY
 05  0x01C00   0x00E00   EventWork
 06  0x02A00   0x00F78   ZukanData (Pokedex)
 07  0x03A00   0x00228   GtsData
 08  0x03E00   0x00104   UnionPokemon
 09  0x04000   0x00200   Misc                    <- MONEY at +0x04
 10  0x04200   0x00020   FieldMenu
 11  0x04400   0x00004   ConfigSave
 12  0x04600   0x00058   GameTime
 13  0x04800   0x005E6   BOX (names/wallpapers/current box/battle teams)
 14  0x04E00   0x36600   BoxPokemon              <- BOX STORAGE
 15  0x3B400   0x0572C   ResortSave
 16  0x40C00   0x00008   PlayTime
 17  0x40E00   0x01080   FieldMoveModelSave
 18  0x42000   0x01A08   Fashion
 19  0x43C00   0x06408   JoinFestaPersonalSave
 20  0x4A200   0x06408   JoinFestaPersonalSave
 21  0x50800   0x03998   JoinFestaDataSave
 22  0x54200   0x00100   BerrySpot
 23  0x54400   0x00100   FishingSpot
 24  0x54600   0x10528   LiveMatchData
 25  0x64C00   0x00204   BattleSpotData
 26  0x65000   0x00B60   PokeFinderSave
 27  0x65C00   0x03F50   MysteryGiftSave
 28  0x69C00   0x00358   Record
 29  0x6A000   0x00728   ValidationSave
 30  0x6A800   0x00200   GameSyncSave
 31  0x6AA00   0x00718   PokeDiarySave
 32  0x6B200   0x001FC   BattleInstSave
 33  0x6B400   0x00200   Sodateya (Daycare)
 34  0x6B600   0x00120   WeatherSave
 35  0x6B800   0x001C8   QRReaderSaveData
 36  0x6BA00   0x00200   TurtleSalmonSave
```

Box storage check: `32 boxes × 30 slots × 232 bytes = 0x36600` — exactly block 14's length. ✓

### 2.5 The USUM block table (39 blocks)

```
 id  offset    length    what                        (delta vs SM)
 00  0x00000   0x00E28   MyItem                      length +0x48
 01  0x01000   0x0007C   Situation                   offset +0x200
 02  0x01200   0x00014   RandomGroup
 03  0x01400   0x000C0   MyStatus
 04  0x01600   0x0061C   PokePartySave
 05  0x01E00   0x00E00   EventWork
 06  0x02C00   0x00F78   ZukanData
 07  0x03C00   0x00228   GtsData
 08  0x04000   0x0030C   UnionPokemon                length +0x208
 09  0x04400   0x001FC   Misc
 10  0x04600   0x0004C   FieldMenu                   length +0x2C
 11  0x04800   0x00004   ConfigSave
 12  0x04A00   0x00058   GameTime
 13  0x04C00   0x005E6   BOX
 14  0x05200   0x36600   BoxPokemon
 15  0x3B800   0x0572C   ResortSave
 16  0x41000   0x00008   PlayTime
 17  0x41200   0x01218   FieldMoveModelSave          length +0x198
 18  0x42600   0x01A08   Fashion
 19  0x44200   0x06408   JoinFestaPersonalSave
 20  0x4A800   0x06408   JoinFestaPersonalSave
 21  0x50E00   0x03998   JoinFestaDataSave
 22  0x54800   0x00100   BerrySpot
 23  0x54A00   0x00100   FishingSpot
 24  0x54C00   0x10528   LiveMatchData
 25  0x65200   0x00204   BattleSpotData
 26  0x65600   0x00B60   PokeFinderSave
 27  0x66200   0x03F50   MysteryGiftSave
 28  0x6A200   0x00358   Record
 29  0x6A600   0x00728   ValidationSave
 30  0x6AE00   0x00200   GameSyncSave
 31  0x6B000   0x00718   PokeDiarySave
 32  0x6B800   0x001FC   BattleInstSave
 33  0x6BA00   0x00200   Sodateya
 34  0x6BC00   0x00120   WeatherSave
 35  0x6BE00   0x001C8   QRReaderSaveData
 36  0x6C000   0x00200   TurtleSalmonSave
 37  0x6C200   0x0039C   BattleFesSave               <- USUM only
 38  0x6C600   0x00400   FinderStudioSave            <- USUM only
```

> **The block IDs are the same but the offsets all shift.** SM's block 01 is `Situation` at
> `0x00E00`; USUM's is `Situation` at `0x01000`. Reusing SM's table for a USUM save reads every
> block from the wrong place, and every CRC then validates *the wrong range* — you get a save the
> game rejects with nothing on screen to say why. **Generate both tables**
> (see [GEN6.md](GEN6.md) §2.5 — one script covers all four 3DS tables).

---

## 3. Storage

```csharp
// SAV7.cs
public override int BoxCount => 32;                              // ▲ Gen 6 is 31
public override int GetBoxOffset(int box)    => Box + (SIZE_STORED * box * 30);
public override int GetPartyOffset(int slot) => Party + (SIZE_PARTY * slot);
public override int PartyCount {
    get => Data[Party + (6 * SIZE_PARTY)];                       // same as Gen 6: AFTER the records
    protected set => Data[Party + (6 * SIZE_PARTY)] = (byte)value;
}

// SAV7SM.cs / SAV7USUM.cs
Party = Blocks.BlockInfo[04].Offset;      // SM 0x01400   USUM 0x01600
Box   = Blocks.BlockInfo[14].Offset;      // SM 0x04E00   USUM 0x05200
```

> **The offsets are taken FROM the block table**, not hardcoded. Do the same in PKSE — look them up
> by block id — and USUM comes out right for free.

### 3.1 Box layout — block 13 (`BOX`)

```csharp
// BoxLayout7.cs -- offsets relative to the BOX block
private const int BoxCount            = 32;
private const int BattleBoxFlags      = 0x4C4;
private const int PCBackgrounds       = 0x5C0;
private const int PCFlags             = 0x5E0;
private const int Unlocked            = 0x5E1;
private const int LastViewedBoxOffset = 0x5E3;

private static int GetBoxNameOffset(int box) => SAV6.LongStringLength * box;   // 0x22 * box
private const int StringMaxLength = SAV6.LongStringLength / 2;                 // 17 characters

private const int TeamCount = 6;
public readonly int[] TeamSlots = new int[TeamCount * 6];        // battle-team slot indices
```

| Field | Offset in block 13 |
|---|---|
| Box names | `0x22 * box`, 34 bytes, **17 characters** |
| Battle-team slots | `0x000`–`0x4C3` region, `TeamSlots` (6 teams × 6) |
| Battle-box flags | `0x4C4` |
| Box wallpapers | `0x5C0 + box` |
| PC flags | `0x5E0` |
| Boxes unlocked | `0x5E1` |
| **Current box** | `0x5E3` |

> **Compare Gen 6**: names at `0x22*box` (same), wallpapers `0x41E` → `0x5C0`, current box `0x43F` →
> `0x5E3`. The *shape* is the same and the *values* are not. Do not carry Gen 6's constants across.

**Battle teams** are new: `TeamSlots` records which box slots are locked into a registered battle
team, and `GetBoxSlotFlags` marks them. PKSE has an existing analogue — Let's Go's party-linked
slots, handled by `storageSlotLocked()`. Wire Gen 7's locked slots into the same predicate so the
storage view refuses to move them, or at minimum preserve the region untouched.

---

## 4. Trainer data

### 4.1 MyStatus — block 03 (`SM 0x01200`, `USUM 0x01400`, length `0xC0`)

Offsets relative to the block:

```
+0x00   4    ID32   (TID16 at +0x00, SID16 at +0x02)
+0x04   1    Game (version byte)
+0x05   1    Gender
+0x10   8    GameSyncID
+0x18   16   NexUniqueID
+0x28   4    (see MyStatus7.cs)
+0x2E   1    Region
+0x2F   1    Country
+0x34   1    ConsoleRegion
+0x35   1    Language
+0x38   26   OT name (UTF-16, 12 chars max)
+0x54   1    bits 2-4: (see MyStatus7)
+0x58   1    …
+0x78   1    bit 0: …
```

> Gen 6 has OT at `+0x48`; **Gen 7 has it at `+0x38`**. Language is `+0x2D` in Gen 6 and `+0x35`
> here. Same fields, different places.

### 4.2 Money — block 09 (`Misc`), offset `+0x04`

```csharp
// Misc7.cs
public uint Money {
    get => ReadUInt32LittleEndian(Data[0x4..]);
    set { if (value > 9_999_999) value = 9_999_999; WriteUInt32LittleEndian(Data[0x4..], value); }
}
```

Gen 6 puts money at `+0x08`; **Gen 7 at `+0x04`**.

---

## 5. The PK7 entity

### 5.1 Identical to PK6 except…

The crypt, sizes, checksum, block boundaries, `IV32` packing, name field positions and most offsets
are **exactly** PK6's ([GEN6.md](GEN6.md) §5.2). Only these differ:

| Off | Gen 6 | **Gen 7** |
|---|---|---|
| `0x15` | `AbilityNumber` — whole byte | **`AbilityNumber = Data[0x15] & 7`** (low 3 bits; the rest is reserved) |
| `0x16` | `TrainingBagHits` (1 byte) | **`MarkingValue` — `u16` at `0x16`** |
| `0x17` | `TrainingBag` | *(part of the `u16` above)* |
| `0x2A` | `MarkingValue` (1 byte) | **`ResortEventStatus`** |
| `0x36` | *(ribbon byte)* | `RIB6` — Gen 7 uses one more ribbon byte; `0x37` is unused |
| `0xDE` | `GroundTile` | **`HyperTrainFlags`** ⚠ |
| `0xED` | `FormArgumentRemain` | **`DirtType`** |
| `0xEE` | `FormArgumentElapsed` | **`DirtLocation`** |
| `0xEF` | `TrainingBagEffect` | *(cleared on PK6→PK7 transfer)* |

Everything else — `EncryptionConstant 0x00`, `Sanity 0x04`, `Checksum 0x06`, `Species 0x08`,
`HeldItem 0x0A`, `TID16 0x0C`, `SID16 0x0E`, `EXP 0x10`, `Ability 0x14`, `PID 0x18`, `Nature 0x1C`,
`Fateful/Gender/Form 0x1D`, `EVs 0x1E–0x23`, contest `0x24–0x29`, `Pokerus 0x2B`, super-train
`0x2C–0x2F`, ribbons `0x30–0x37`, `FormArgument 0x3C`, `Nickname 0x40`, moves `0x5A–0x61`,
PP `0x62–0x65`, PP-Ups `0x66–0x69`, relearn `0x6A–0x71`, `IV32 0x74`, `HT name 0x78`,
`HT gender 0x92`, `CurrentHandler 0x93`, geo `0x94–0x9D`, `HT friendship 0xA2`,
`OT name 0xB0`, `OT friendship 0xCA`, dates `0xD1–0xD6`, `EggLocation 0xD8`, `MetLocation 0xDA`,
`Ball 0xDC`, `MetLevel/OTGender 0xDD`, `Version 0xDF`, `Country/Region/ConsoleRegion 0xE0–0xE2`,
`Language 0xE3`, party tail `0xE8–0x103` — is **unchanged**.

### 5.2 ⚠ Hyper Training at `0xDE`

```csharp
public byte HyperTrainFlags { get => Data[0xDE]; set => Data[0xDE] = value; }
public bool HT_HP  { get => ((HyperTrainFlags >> 0) & 1) == 1; … }
public bool HT_ATK { get => ((HyperTrainFlags >> 1) & 1) == 1; … }
public bool HT_DEF { get => ((HyperTrainFlags >> 2) & 1) == 1; … }
public bool HT_SPA { get => ((HyperTrainFlags >> 3) & 1) == 1; … }
public bool HT_SPD { get => ((HyperTrainFlags >> 4) & 1) == 1; … }
public bool HT_SPE { get => ((HyperTrainFlags >> 5) & 1) == 1; … }
```

**This is where Hyper Training enters the series.** PKSE already implements it fully
(manual [06](../manual/06-pokemon-entity.md) — `hasHyperTraining()`, `hyperTrainFlags()`,
`effectiveIV()`, `HT_*` constants), so Gen 7 just wires it up:

```cpp
bool    hasHyperTraining() const noexcept override { return true; }
uint8_t hyperTrainFlags()  const noexcept override { return rd8(0xDE); }
void    setHyperTrainFlags(uint8_t v) noexcept override { wr8(0xDE, v); }
```

The flag bit order is PKHeX's — **HP, ATK, DEF, SPA, SPD, SPE** — which is *not* the accessor
order (HP, ATK, DEF, SPE, SPA, SPD). PKSE's `HyperTrainStat` enum already encodes this. Use the
named constants; positional indexing across the two orders produces wrong-but-plausible stats.

> **`recalculateStats()` MUST use `effectiveIV()`, not `ivXXX()`.** This is the exact bug that hit
> Scarlet/Violet: a Hyper Trained mon storing IV 20 is *played* at 31, and recomputing from the raw
> value wrote stats eleven points low, silently, on any edit that touched a stat. It surfaced as a
> party Pokemon's stored battle stats not matching a recalculation — which is exactly the
> cross-check that catches it here.

> **`0xDE` is the ground-tile byte in Gen 6.** If you build `Pokemon7SM` by copying `Pokemon6XY`,
> this is the single most likely thing to be left wrong, and it fails quietly: hyper-trained mons
> just compute low.

### 5.3 `AbilityNumber` is masked

```csharp
public override int AbilityNumber { get => Data[0x15] & 7; set => Data[0x15] = (byte)((Data[0x15] & ~7) | (value & 7)); }
```

Read-modify-write the low 3 bits; the upper 5 belong to something else. Gen 6 uses the whole byte.

---

## 6. Text

`StringConverter7` — UTF-16, as Gen 6. One addition: **`StringConverter7ZH`** handles the Chinese
private-use remapping introduced when SM added Chinese. PKSE can ignore it for a first pass (the
characters round-trip as their stored code units); note it as deferred rather than silently wrong.

Field widths are Gen 6's: OT/nickname/HT 26 bytes / 12 chars, box name `0x22` / 17 chars.

---

## 7. Items

Bag is block 00 (`SM 0x00DE0`, `USUM 0x00E28`). Pouch offsets in `Items/Bags/PlayerBag7SM.cs` /
`PlayerBag7USUM.cs`, and the codec is `InventoryPouch7`.

**PKSE already has a Gen 7 item model** — `Trainer::Inventory7LGPE` and `InventoryPouch7b` are
close relatives. Read `PlayerBag7b.cs` alongside `PlayerBag7SM.cs` to see how much carries over.

`itemsAreIdIndexed()` → **false**. Legal sets: `ItemStorage7SM` / `ItemStorage7USUM`.

---

## 8. Data tables

`tools/gen_gen7.py` → `Gen7Tables.h/.cpp`.

| Table | Source | Notes |
|---|---|---|
| Base stats, abilities, gender, friendship, form count | `personal_sm`, `personal_uu` | species 0–807 |
| Types | same | Fairy present |
| Growth rate | same | verify before reusing |
| Learnsets | `lvlmove_sm/uu.pkl` + TM + tutors + egg moves | union with pre-evolutions |
| Move info | `MoveInfo7` | |
| Item names | PKHeX item strings index 7 | 0–959 |
| Item pouch sets | `ItemStorage7SM` / `7USUM` | |
| Met location names | `text_sm_*`, `text_uu_*` | two namespaces |
| Ribbon bit offsets | `PK7.cs` `RIB0`–`RIB6` | one more byte than Gen 6 |
| **Block tables** | `SaveBlockAccessor7SM/USUM.cs` | generate with the Gen 6 ones |

> **PKSE already carries Gen 7 data for Let's Go** — `PersonalInfo7LGPE`, generated from PKHeX's
> `personal_gg`. **Do not reuse it for Sun/Moon.** Every save-format group gets its OWN table,
> even where the games agree, and Let's Go is not one of the games that agrees: it is a different
> roster with its own stat changes. Add `7SM` and `7USUM` rows to `tools/gen_personaltables.py`
> (`personal_sm` and `personal_uu`) and the generator emits the pair; the shape and the plumbing
> are already there.

Species 1–807, moves 1–728 and abilities 1–233 are strict prefixes of the modern tables — names
work unchanged. Only items, locations, ribbons and block tables are new.

---

## 9. Legality

Same as Gen 6, plus:

- **Hyper Training**: an IV below 31 with its HT bit set is legal. **Do not flag it.** And a
  hyper-trained *egg* is illegal (`IsEgg && HyperTrainFlags != 0`) — PKHeX checks this.
- **`AbilityNumber` masked to 3 bits** — values above 4 are impossible.
- Presence gate: `species <= 802` (SM) / `<= 807` (USUM).

---

## 10. PKSE integration

### 10.1 `GameVersion`

```cpp
enum class GameVersion {
    // … Gen 7 (Nintendo 3DS). Values match PKHeX; getOriginGameName() ALREADY names all four
    // and getVersionGeneration() ALREADY returns 7 for 30..33.
    SN = 30, // Sun
    MN = 31, // Moon
    US = 32, // Ultra Sun
    UM = 33, // Ultra Moon
    // …
    SM   = 89,   // Sun/Moon group          (above the existing Gen7B/Gen8/Gen9 = 84-86)
    USUM = 90,   // Ultra Sun/Ultra Moon group
};
```

> **`usesSixDigitTrainerID()` already returns true for 30–33** — that function exists in the tree
> precisely because a Gen 7 origin displays a six-digit trainer ID. Nothing to change.
>
> **`GameVersion::GG` (73) is Let's Go and is already a Gen 7 group.** Do not conflate them: PB7 and
> PK7 are different formats with different sizes. Two separate groups, two separate entity classes,
> as the per-game rule requires.

Add `getGameGroup` (`SN`/`MN` → `SM`; `US`/`UM` → `USUM`), `getGameVersionName`
("Sun/Moon", "Ultra Sun/Ultra Moon"), `getGroupRepVersion` (`SM → 30`, `USUM → 32`),
`isGameSupported` needs no change -- it is derived from `getGameGroup`.

Gen 7 saves carry the game byte at `MyStatus + 0x04`, so name the exact game, not the pair.

### 10.2 New files

```
include/Utils/CRC16.h                       + crc16Invert   (§2.2)
include/Pokemon/Pokemon7SM.h    + .cpp      PK7 (one class; the record is identical SM/USUM)
include/Pokemon/Gen7Tables.h    + .cpp      generated
include/Trainer/Blocks6XY.h .. Blocks7USUM.h               extend with SM + USUM tables
include/Trainer/Inventory7.h                (distinct from Inventory7LGPE)
include/Trainer/Trainer7.h      + .cpp      base -- can share Trainer6's, parameterised on the CRC
include/Trainer/Trainer7SM.h    + .cpp
include/Trainer/Trainer7USUM.h  + .cpp
tools/gen_gen7.py
```

`Encryption6` is reused **verbatim** — `Decrypt67` covers both generations.

### 10.3 Not sharing the Gen 6 base

This section originally proposed a shared `Trainer67` base with a virtual `blockCrc()`. **That is
not what landed, and should not be reintroduced.** A group's geometry is a constant in the class
that uses it, not a virtual call: `Trainer6XY`, `Trainer6ORAS`, `Trainer7SM` and `Trainer7USUM` are
self-contained containers whose block table, offsets, footer size and CRC are plain members, each
with its own `blockCrc()`. A Gen 6 container has no Gen 7 codec compiled into it — that is the mode
flag the copy-per-group rule exists to prevent ([manual ch. 04](../manual/04-generation-pattern.md)).

The one fact the shared-base sketch was protecting still holds: **Gen 6 checksums with CRC16-CCITT,
Gen 7 with CRC16Invert.** Same width, same bytes, different algorithm; a Gen 7 save written with the
Gen 6 function produces well-formed checksums the game rejects, with nothing in the structure to
hint at it. Every block's CRC lives at `fileSize - 0x200 + 0x14 + id*8 + 6`.

### 10.3a Signing — what this document originally left out

**Gen 7 signs its saves.** SM and USUM store an RSA signature (MemeCrypto) over a SHA-256 of the
checksum table inside **block 36 (TurtleSalmonSave)** at `+0x100`, `0x80` bytes long. Any edit moves
the checksum table, so a save written without re-signing is rejected by the game. `sign()` runs last
in `serialize()` — after every checksum, because the signature covers them. The vendored
`memecrypto/` (SciresM's memecrypto + tiny-AES + libsha1 + mini-gmp) does the work and must not be
edited; re-sync it from upstream. Two facts here were established by measurement and contradict the
obvious reading:

- **Block 36 IS checksummed, with the signature region blanked first** (`checksumBlank()`). SM
  stores `0x44BE` and USUM `0xE25C`, which is what the CRC gives blanked and not what it gives over
  the block as it sits on disk. Skipping the block leaves a stale checksum.
- **The signature's plaintext is NOT zeros**, whatever PKHeX's `SAV7` comment says: a real Ultra Sun
  signature decrypts to the SHA-256 followed by ~0x58 bytes of leftover 3DS console memory. PKHeX
  blanks it and re-signs, which the game accepts; PKSE **recovers** it with `memecrypto_verify` and
  re-signs, reproducing the original byte for byte, which is what lets an unedited Gen 7 save
  round-trip exactly. `reverseCrypt()` is deliberately not used: it is written for a `0x60` payload,
  reads past its own buffer at `0x80`, and is not the inverse of signing one.

### 10.4 The rest

- **Save write**: the shared loose-file path (`Save::saveExternalSave`), original bytes backed up
  to `sdmc:/PKSE/FileBackups` first. Not in `TrainerVariant`.
- **Entry point**: `Trainer7SM::detect` / `Trainer7USUM::detect` in the probe chain, size first.
- **Box grid**: 32 × 30 → `boxGridColumns(30) == 6`. **No UI change.**
- `Names::getItemNameFor()`: `SM` / `USUM` branches.
- `PickerKind`: `ItemG7`, `PouchItemG7`.
- Ball ids cap at 26.
- **Locked battle-team slots** → `storageSlotLocked()`.
- `Bank.cpp`: `BTAG_G7` — **one tag per ENTITY format, not per game.** Every PK7 is stored
  under it and reads back as the format's representative group (`groupForBankTag`). Never compare
  that group to the entity's own; the invariant is `tag == bankTagForEntity(entity)`. Add-only.
- **Conversion**: PK7 is the format HOME transfers *from* into Gen 8, and PKSE's conversion hub is
  the PK8 layout — so **PK7 ↔ PK8 is the most tractable cross-gen route in this whole document
  set**, and it is the one that shipped: `remapPK7toPK8` / `remapPK8toPK7`. SM and USUM share the
  record and interconvert by copying; Sun/Moon stops at species 802 and USUM at 807.

---

### The Pokedex form region NEEDS a generated index table, and it is already there

The seen/caught bits are arithmetic — `species - 1`, like every other dex — but the **alternate-form
flags are not**. This generation indexes them by a hand-authored per-species ordering that exists
only in PKHeX's data: `Zukan6.GetFormIndexXY` / `GetFormIndexAO` for Gen 6, `DexFormUtil`'s parallel
`DexSpeciesWithForm_*` / `DexSpeciesCount_*` arrays for Gen 7. Vivillon at 83, Flabébé at 103, the
Megas from 131 — no formula produces that.

**`tools/gen_dexformtables.py` already emits it**, ahead of the writer that will use it:
`DexTable6XY`, `DexTable6ORAS`, `DexTable7SM` and `DexTable7USUM`, each exposing
`getDexFormBitIndex<CODE>(species)` and `getDexFormCount<CODE>(species)`. Use those rather than
re-deriving; `Trainer7LGPE` shows the shape, and its tables used to be hand-transcribed inline
before they were moved into the same generator.

## 11. Verification

```bash
python tools/syntax_check.py && python tools/syntax_check.py --warnings
```

**The gate is an unedited round trip of a real SM and a real USUM `main`**: open, write straight
back, byte-identical — signature included (§10.3a). Gen-7-specific round-trip risks, in likelihood
order:

1. **`crc16ccitt` used instead of `crc16Invert`.** Every block checksum wrong; the file still
   "saves". This is the #1 Gen 7 bug.
2. **SM's block table used for a USUM save** (or vice versa) — every offset shifted.
3. **`0xDE` treated as the ground tile**, so hyper-trained mons compute low stats.
4. **`AbilityNumber` written as a whole byte**, clobbering the upper 5 bits.
5. **`MarkingValue` read as 1 byte** at `0x16` instead of 2, or `0x2A` overwritten
   (`ResortEventStatus`).
6. **Gen 6's box-layout offsets** (`0x41E`/`0x43F`) used instead of Gen 7's (`0x5C0`/`0x5E3`).
7. **Money read at `+0x08`** (Gen 6) instead of `+0x04`.
8. **OT name read at `+0x48`** (Gen 6) instead of `+0x38`.
9. **Battle-team slot region disturbed.**

Then the party-stat cross-check (recompute a party record's battle stats and compare to what it
stores) — **which is the test that catches #3** — and edit persistence.

---

## 12. Trap list

1. **`CRC16Invert`, not `CRC16_CCITT`.** Verify the generated table against PKHeX's first entries.
2. **SM and USUM block tables differ from block 01 onward.** Generate both.
3. **`0xDE` is HyperTrainFlags in Gen 7, GroundTile in Gen 6.**
4. **`recalculateStats()` must use `effectiveIV()`**, and the HT bit order (HP, ATK, DEF, **SPA,
   SPD, SPE**) is not the accessor order.
5. **`AbilityNumber` is `Data[0x15] & 7`.**
6. **`MarkingValue` is a `u16` at `0x16`**; `0x2A` is `ResortEventStatus`.
7. **32 boxes, not 31.**
8. **Box-layout offsets moved**: wallpapers `0x5C0`, flags `0x5E0`, unlocked `0x5E1`, current box
   `0x5E3`.
9. **Money at `Misc + 0x04`**, OT at `MyStatus + 0x38`, Language at `MyStatus + 0x35`.
10. **Take `Party` and `Box` from the block table**, not from constants.
11. **Battle-team slots are locked slots.** Treat like LGPE party members or leave alone.
12. **Every 3DS save has BEEF.** Size discriminates; a collision is a refusal.
13. **`GameVersion::GG` is also generation 7.** Two Gen 7 groups, two entity formats, no conflation.
14. **Group enum values 84–86 are taken**; Gen 7 (3DS) is `89`/`90`.
15. **`compactStorage()` stays false.**
16. **Gen 7 saves say which game they are** (`MyStatus + 0x04`).
17. **Re-sign last.** The MemeCrypto signature covers the checksum table, and block 36 is
    checksummed with its signature region blanked (§10.3a).

---

## 13. Suggested order

Assuming Gen 6 is done:

1. `crc16Invert` + the `static_assert` on the table. Validate a real SM save's CRCs before anything
   else.
2. Extend `gen_blocktables.py` with the SM and USUM tables.
3. Generated tables (`gen_personaltables.py`); check them against PKHeX.
4. `Pokemon7SM` — copy `Pokemon6XY`, then change **exactly the nine rows in §5.1** and wire up
   hyper training. Every setter reads back what it wrote.
5. `Trainer7SM` (its own container, not a shared base — §10.3) + signing. Round trip to zero diff.
6. `Trainer7USUM` — block table only.
7. Items, box names, trainer fields, locked team slots.
8. Entry point (this is what exposes the format), UI, bank, docs.

Step 4 is short but is where the silent bugs are. Run the party-stat cross-check the moment the
entity parses — it is the only thing that will tell you `0xDE` is wrong.
