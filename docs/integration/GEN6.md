# Generation 6 — X / Y / Omega Ruby / Alpha Sapphire (PK6, SAV6)

> **Status: implemented.** This is the PKHeX transcription Gen 6 was built from, kept as the
> reference for that code. It was written as a plan, so where §10 and §13 name files, the tree
> differs in one systematic way: **every layer has one class per group, with no shared base** —
> `Trainer6XY` / `Trainer6ORAS`, `Pokemon6XY` / `Pokemon6ORAS`,
> `Encryption6XY` / `6ORAS`, `Inventory6XY` / `6ORAS` — rather than the `Trainer6` base, single PK6
> class, `Encryption6` and `Inventory6` sketched below. Each container's block table, offsets,
> footer size and CRC are plain members, and it has its own `blockCrc()`; a Gen 6 container has no
> Gen 7 codec compiled into it. The block tables (`Blocks6XY.h`, `Blocks6ORAS.h`) come from
> `tools/gen_blocktables.py`; personal data from `tools/gen_personaltables.py`, not a `gen_gen6.py`;
> text goes through `Utils/Utf16Text.h`. The groups are `XY = 87`, `ORAS = 88`. The bank stores every
> PK6 under one tag, `BTAG_G6`, and Gen 6 is not in `Conversion::gate`'s `inFamily` — banked, not
> converted. See [manual ch. 04](../manual/04-generation-pattern.md) and
> [ch. 05](../manual/05-save-pipeline.md).

> **Source of truth: PKHeX.** Every offset below was transcribed at commit `6501f0ab46e8f8ca048539dbaf8cae8cb104e722`;
> PKSE now tracks PKHeX's default branch, so re-verify against the file you fetch.
> Files: `Saves/SAV6.cs`, `Saves/SAV6XY.cs`, `Saves/SAV6AO.cs`,
> `Saves/Access/SaveBlockAccessor6XY.cs`, `Saves/Access/SaveBlockAccessor6AO.cs`,
> `Saves/Blocks/BlockInfo3DS.cs`, `Saves/Blocks/BlockInfo.cs`,
> `Saves/Substructures/Gen6/BoxLayout6.cs` / `MyStatus6.cs` / `Misc6XY.cs` / `MyItem6XY.cs`,
> `PKM/PK6.cs`, `PKM/Shared/G6PKM.cs`, `PKM/Util/PokeCrypto.cs`,
> `PKM/Strings/StringConverter6.cs`, `Saves/Util/Checksums.cs`, `Saves/Util/SaveUtil.cs`,
> `Items/Bags/PlayerBag6XY.cs`, `Legality/Legal.cs`.

---

## 0. Read this first

Gen 6 is where the modern format begins. Three things become true here and stay true through Gen 9:

1. **The entity crypt is the one PKSE already implements** for Gen 8/9 — LCG XOR + 4-block shuffle,
   seeded on the **PID** for both the body *and* the party tail. No Gen 4/5 checksum-as-key wrinkle.
2. **Text is UTF-16** with a `0x0000` terminator.
3. **The save is a flat file of independently-checksummed blocks** described by a metadata table at
   a fixed offset — conceptually the same shape as Gen 8/9's SCBlocks, just with a fixed table
   instead of a self-describing one.

So the difficulty ranking across this document set is roughly:

```
Gen 5  <  Gen 6  ≈  Gen 7  <  Gen 2  <  Gen 4
(easiest, given Gen 4)                (hardest: two partitions)
```

**Gen 6 and Gen 7 are near-identical** — same entity size, same block-metadata format, same text
encoding. [GEN7.md](GEN7.md) is written as a delta against this document. **Do Gen 6 first**, then
Gen 7 is a short follow-up.

Prerequisites: manual [04](../manual/04-generation-pattern.md), [05](../manual/05-save-pipeline.md),
[06](../manual/06-pokemon-entity.md), [13](../manual/13-adding-a-game.md). Reading
[GEN4.md](GEN4.md) first is useful but not required.

---

## 1. Scope

| | |
|---|---|
| Games | X, Y (`XY`), Omega Ruby, Alpha Sapphire (`AO`) |
| Groups | `GameVersion::XY`, `GameVersion::ORAS` |
| Entity | **PK6**, `SIZE_6STORED` = `0xE8` (232), `SIZE_6PARTY` = `0x104` (260) |
| Save file | `main`, **XY `0x65600`**, **ORAS `0x76000`** |
| Entity encryption | LCG XOR + 4-block shuffle, **seed = PID** (both halves) |
| Entity checksum | `Add16` over `data[0x08 .. 0xE8)` |
| Save checksum | **CRC16-CCITT** per block |
| Endianness | little endian |
| Boxes | **31 × 30** |
| Max species | 721 |
| Max move id | XY 617, ORAS 621 |
| Max item id | XY 717, ORAS 775 |
| Max ability id | XY 188, ORAS 191 |
| Max ball id | `0x19` (25) |
| OT / nickname | **12 chars**, 26-byte fields |
| Box name | **17 chars**, `0x22`-byte field |

> **`SIZE_6PARTY` is `0x104` = 260 — exactly the size PKSE already uses for Let's Go (`PB7`).**
> That is not a coincidence: PB7 is a Gen 7 record. The Gen 6/7 entity layout is the ancestor of the
> one already in the tree, which is why much of `Pokemon7LGPE` reads as familiar.

---

## 2. The save container — the BEEF block table

### 2.1 Structure

The last `0x200` bytes of the file are a **metadata chunk**:

```csharp
// BlockInfo3DS.cs
// ==chunk def== @ BlockInfoOffset
// u64 timestamp1
// u64 timestamp2
// u8[4] BEEF magic
// n*{blockInfo}, where n varies per sav type
//
// ==block info def==
// u32 length
// u16 id
// u16 checksum
//
// when stored, each block size is rounded up to nearest 0x200, and the next chunk is immediately after
```

```
BlockMetadataOffset = saveSize - 0x200

  +0x00   u64  timestamp 1
  +0x08   u64  timestamp 2
  +0x10   u8[4] "BEEF"  = 0x42 0x45 0x45 0x46  (0x42454546 little-endian)
  +0x14   blockInfo[0]   { u32 length; u16 id; u16 checksum }
  +0x1C   blockInfo[1]
  …       8 bytes per entry
```

The checksum for block `id` therefore lives at:

```csharp
private int ChecksumOffset => BlockInfoOffset + 0x14 + ((int)ID * 8) + 6;
```

and Gen 6 computes it as:

```csharp
public sealed class BlockInfo6(int bo, uint id, int ofs, int len) : BlockInfo3DS(bo, id, ofs, len)
{
    protected override ushort GetChecksum(ReadOnlySpan<byte> data) => Checksums.CRC16_CCITT(data.Slice(Offset, Length));
}
```

> ### ⚠ Gen 6 and Gen 7 use DIFFERENT CRC16 variants
>
> ```csharp
> BlockInfo6  -> Checksums.CRC16_CCITT(...)     // bitwise, top/bot = 0xFF
> BlockInfo7  -> Checksums.CRC16Invert(...)     // table-driven, ~CRC16(data, 0xFFFF)
> BlockInfo7b -> Checksums.CRC16NoInvert(...)   // table-driven, CRC16(data, 0)   [Let's Go]
> ```
>
> These produce **completely different values**. Using the Gen 6 function for a Gen 7 save writes
> checksums the game rejects, and there is nothing in the structure to warn you — the write
> succeeds. **Implement all three, name them clearly, and pick per generation.**

PKSE will need both `crc16ccitt` (Gen 4 and Gen 6) and `crc16Invert` (Gen 7):

```cpp
// include/Utils/CRC16.h
namespace Utils {
    /// PKHeX Checksums.CRC16_CCITT -- bitwise. Used by Gen 4 save blocks and Gen 6 save blocks.
    uint16_t crc16ccitt(const uint8_t* data, size_t len) noexcept;      // see GEN4.md §2.4

    /// PKHeX Checksums.CRC16Invert -- table-driven, ~CRC16(data, 0xFFFF). Gen 7 save blocks.
    /// NOT interchangeable with crc16ccitt: same name, different polynomial handling, different
    /// output. Gen 7 written with the CCITT variant produces a save the game refuses.
    uint16_t crc16Invert(const uint8_t* data, size_t len) noexcept;     // see GEN7.md §2.2
}
```

### 2.2 Saving

```csharp
protected override void SetChecksums() => AllBlocks.SetChecksums(Data);
```

For each block: compute the CRC over `[Offset, Offset + Length)` and write it into the metadata
table at `BlockMetadataOffset + 0x14 + id*8 + 6`. **The block data itself carries no checksum** —
it all lives in the trailing table. Nothing else to update: no counters, no partitions, no mirrors.

The `u32 length` in each metadata entry should already match the table; PKHeX does not rewrite it.
**Leave it alone.**

### 2.3 Detection

```csharp
private static bool IsG6XY (data) => data.Length == SIZE_G6XY   && HasSaveFooterBEEF(data);
private static bool IsG6AO (data) => data.Length == SIZE_G6ORAS && HasSaveFooterBEEF(data);

private static bool HasSaveFooterBEEF(ReadOnlySpan<byte> data)
    => ReadUInt32LittleEndian(data[^0x1F0..]) == 0x42454546; // BEEF
```

`saveLength - 0x1F0` is `BlockMetadataOffset + 0x10`, i.e. exactly where the magic sits.

```cpp
/// True when `bytes` is a Gen 6 save of `expectedSize`. The BEEF magic sits at
/// BlockMetadataOffset + 0x10 == size - 0x1F0. Sizes are distinct per game, so the pair
/// (size, magic) identifies the game outright.
static bool detectGen6(const std::vector<uint8_t>& b, size_t expectedSize) noexcept {
    if (b.size() != expectedSize) return false;
    return Utils::readUInt32LittleEndian(&b[b.size() - 0x1F0]) == 0x42454546u;
}
// XY: 0x65600   ORAS: 0x76000   (ORAS demo 0x5A00 -- refuse it, it is not a full save)
```

> **Every 3DS-generation save PKSE will ever open carries the same BEEF magic** — XY, ORAS, SM,
> USUM and Let's Go alike. **The size is what discriminates**, so the probe chain must check size
> first and treat a size collision as a refusal rather than a guess. The five sizes are all
> distinct: `0x65600`, `0x76000`, `0x6BE00`, `0x6CC00`, `0x100000`.

The `main` file may be wrapped in a 3DS save container by whatever dumped it. PKSE takes the file
the user browses to as-is; if the length does not match exactly, refuse and say so.

### 2.4 The XY block table (55 blocks)

`(id, offset, length)` from `SaveBlockAccessor6XY.cs`. `BlockMetadataOffset = 0x65600 - 0x200 =
0x65400`.

```
 id  offset    length    what
 00  0x00000   0x002C8   Puff
 01  0x00400   0x00B88   MyItem                  <- the bag
 02  0x01000   0x0002C   ItemInfo (select-bound items)
 03  0x01200   0x00038   GameTime
 04  0x01400   0x00150   Situation
 05  0x01600   0x00004   RandomGroup (rand seeds)
 06  0x01800   0x00008   PlayTime
 07  0x01A00   0x001C0   Fashion
 08  0x01C00   0x000BE   Amie minigame records
 09  0x01E00   0x00024   temp variables
 10  0x02000   0x02100   FieldMoveModelSave
 11  0x04200   0x00140   Misc                    <- MONEY at +0x08
 12  0x04400   0x00440   BOX (box names/wallpapers/current box)
 13  0x04A00   0x00574   BattleBox
 14  0x05000   0x04E28   PSS1
 15  0x0A000   0x04E28   PSS2
 16  0x0F000   0x04E28   PSS3
 17  0x14000   0x00170   MyStatus                <- TRAINER
 18  0x14200   0x0061C   PokePartySave           <- PARTY
 19  0x14A00   0x00504   EventWork
 20  0x15000   0x006A0   ZukanData (Pokedex)
 21  0x15800   0x00644   hologram clips
 22  0x16000   0x00104   UnionPokemon (Fused)
 23  0x16200   0x00004   ConfigSave
 24  0x16400   0x00420   Amie decoration
 25  0x16A00   0x00064   OPower
 26  0x16C00   0x003F0   Strength Rock positions
 27  0x17000   0x0070C   Trainer PR Video
 28  0x17800   0x00180   GtsData
 29  0x17A00   0x00004   Packed Menu Bits
 30  0x17C00   0x0000C   PSS Profile Q&A
 31  0x17E00   0x00048   Repel / roamer info
 32  0x18000   0x00054   BOSS data fetch history
 33  0x18200   0x00644   Streetpass history
 34  0x18A00   0x005C8   LiveMatchData / BattleSpotData
 35  0x19000   0x002F8   MAC address + network log
 36  0x19400   0x01B40   Dendou (Hall of Fame)
 37  0x1B000   0x001F4   BattleHouse (Maison)
 38  0x1B200   0x001F0   Sodateya (Daycare)
 39  0x1B400   0x00216   TrialHouse (Battle Institute)
 40  0x1B800   0x00390   BerryField
 41  0x1BC00   0x01A90   MysteryGiftSave
 42  0x1D800   0x00308   SubEvent Log
 43  0x1DC00   0x00618   PokeDiarySave
 44  0x1E400   0x0025C   Record
 45  0x1E800   0x00834   Friend Safari
 46  0x1F200   0x00318   SuperTrain
 47  0x1F600   0x007D0   Unused
 48  0x1FE00   0x00C48   LinkInfo
 49  0x20C00   0x00078   PSS usage info
 50  0x20E00   0x00200   GameSyncSave
 51  0x21000   0x00C84   PSS Icon
 52  0x21E00   0x00628   ValidationSave
 53  0x22600   0x34AD0   Box                     <- BOX STORAGE
 54  0x57200   0x0E058   JPEG
```

### 2.5 The ORAS block table (58 blocks)

`BlockMetadataOffset = 0x76000 - 0x200 = 0x75E00`. Blocks 0–52 are the same *ids* with mostly the
same lengths (block 01 `MyItem` grows to `0x00B90`), then ORAS adds three:

```
 52  0x22E00   0x00628   ValidationSave
 53  0x23600   0x00400   Contest         <- ORAS only
 54  0x23A00   0x07AD0   SecretBase      <- ORAS only
 55  0x2B600   0x078B0   EonTicket       <- ORAS only
 56  0x33000   0x34AD0   Box             <- BOX STORAGE (different offset from XY!)
 57  0x67C00   0x0E058   JPEG
```

> **Take both tables verbatim from PKHeX and generate them**, exactly as recommended for Gen 5.
> A `tools/gen_blocktables.py` that parses the four accessor files (`6XY`, `6AO`, `7SM`, `7USUM`)
> and emits one header with four `constexpr` arrays covers Gen 6 and Gen 7 in one step. Hand-copying
> 55 + 58 + 37 + 40 rows is 190 chances for a typo that a CRC will validate happily — over the wrong
> range.

---

## 3. Storage

```csharp
// SAV6.cs
public sealed override int BoxCount => 31;
public override int GetBoxOffset(int box)   => Box + (SIZE_STORED * box * 30);   // 232 * 30 = 0x1B30
public override int GetPartyOffset(int slot) => Party + (SIZE_PARTY * slot);
public override int PartyCount {
    get => Data[Party + (6 * SIZE_PARTY)];      // AFTER the six party records
    protected set => Data[Party + (6 * SIZE_PARTY)] = (byte)value;
}

// SAV6XY.cs
Party = 0x14200;    // block 18
Box   = 0x22600;    // block 53
```

**31 boxes × 30 slots**, **no padding between boxes** — `0x22600 + box * 0x1B30`. Total
`31 * 0x1B30 = 0x34AD0`, which is exactly block 53's length. ✓

For ORAS, `Box = 0x33000` (block 56) and `Party = 0x14200` (block 18, same as XY).

> **`PartyCount` sits AFTER the six records**, not before them like Gen 4/5. `Party + 6*260 =
> Party + 0x618`, and block 18's length is `0x61C` — the count plus three padding bytes.

### 3.1 Box names / wallpapers / current box — block 12 (`0x04400`)

```csharp
// BoxLayout6.cs -- offsets relative to the BOX block
// gfstr5[31] boxNames;  byte[31] wallpapers;  byte Flags:7|FinalBoxUnlocked:1;  byte UnlockedCount;  byte CurrentBox;
private const int StringMaxByteCount = SAV6.LongStringLength;      // 0x22 = 34 bytes
private const int StringMaxLength    = StringMaxByteCount / 2;     // 17 characters
private const int BoxCount           = 31;
private const int PCBackgrounds      = BoxCount * StringMaxByteCount;  // 0x41E
private const int PCFlags            = PCBackgrounds + BoxCount;       // 0x43D
private const int Unlocked           = PCFlags + 1;                    // 0x43E
private const int LastViewedBoxOffset= Unlocked + 1;                   // 0x43F

private static int GetBoxNameOffset(int box) => StringMaxByteCount * box;   // 0x22 * box
```

| Field | Offset in block 12 |
|---|---|
| Box names | `0x22 * box`, 34 bytes each, **17 characters** |
| Box wallpapers | `0x41E + box` |
| PC flags (7 wallpaper-unlock bits + final-box bit) | `0x43D` |
| Boxes unlocked | `0x43E` |
| **Current box** | `0x43F` |

> **17-character box names.** Gen 6 and Gen 7 both use `LongStringLength = 0x22`. PKSE's
> `getMaxBoxNameLength()` must return 17 here, not 8 (Gen 1–4) and not 13 (Gen 5).

---

## 4. Trainer data

### 4.1 MyStatus — block 17 (`0x14000`, `0x170`)

Offsets relative to the block:

```
+0x00   4    ID32   (TID16 at +0x00, SID16 at +0x02)
+0x04   1    Game (version byte)
+0x05   1    Gender
+0x06   1    MultiplayerSpriteID_1
+0x07   1    MultiplayerSpriteID_2
+0x08   8    GameSyncID (16 hex chars)
+0x26   1    Region
+0x27   1    Country
+0x28   2    Latitude    (signed, * 180 / 0x8000)
+0x2A   2    Longitude
+0x2C   1    ConsoleRegion
+0x2D   1    Language
+0x48   26   OT name (UTF-16, 12 chars max)
+0x62…  …    Sayings 1-4
```

### 4.2 Money — block 11 (`0x04200`), offset `+0x08`

```csharp
// Misc6XY.cs
public uint Money { get => ReadUInt32LittleEndian(Data[0x8..]); set => WriteUInt32LittleEndian(Data[0x8..], value); }
```

ORAS's `Misc6AO` uses the same `+0x08`.

---

## 5. The PK6 entity

### 5.1 Crypto — the one PKSE already has

```csharp
// PokeCrypto.cs
public static void Decrypt67(Span<byte> data)
{
    uint pv = ReadUInt32LittleEndian(data);        // EncryptionConstant at 0x00
    uint sv = (pv >> 13) & 31;

    var shuffle = data[8..SIZE_6STORED];
    CryptArray(shuffle, pv);                       // body keyed on the PV
    if (data.Length > SIZE_6STORED)
        CryptArray(data[SIZE_6STORED..], pv);      // party tail, SAME key
    Shuffle67(shuffle, sv);
}

private static void Shuffle67(Span<byte> data, uint sv) => Shuffle<ulong>(data, sv, SIZE_6BLOCK);
private const int SIZE_6BLOCK = 56;                // 0x38
```

**Identical in shape to `Decrypt8`**, which PKSE already implements — the only change is the block
size (56 instead of 80) and therefore the stored size (`0xE8` instead of `0x148`). Copy
`Encryption8SWSH` and change two constants.

Encrypt inverts the shuffle value first, exactly as Gen 8 does:

```csharp
sv = BlockPositionInvert[(int)sv];
Shuffle67(shuffle, sv);
CryptArray(shuffle, pv);
if (data.Length > SIZE_6STORED) CryptArray(data[SIZE_6STORED..], pv);
```

Checksum:

```csharp
private ushort CalculateChecksum() => Checksums.Add16(Data[8..PokeCrypto.SIZE_6STORED]);
```

> **Unlike Gen 4/5, the checksum is NOT the crypt key here.** Order is the familiar one:
> `recalculateStats()` → `refreshChecksum()` → encrypt. Same as Gen 8/9.

Encryption detection:

```csharp
// the final terminators of Nickname and OT are zero in a decrypted record
private static bool IsEncrypted67(ReadOnlySpan<byte> data)
    => ReadUInt16LittleEndian(data[0xC8..]) != 0 || ReadUInt16LittleEndian(data[0x58..]) != 0;
```

### 5.2 Complete PK6 offset map

Blocks: A = `0x08`, B = `0x40`, C = `0x78`, D = `0xB0`, each 56 bytes.

| Off | Size | Field | Notes |
|---|---|---|---|
| `0x00` | 4 | **EncryptionConstant** | the crypt seed and the shuffle value source |
| `0x04` | 2 | Sanity | must be 0 |
| `0x06` | 2 | Checksum | |
| — | | **Block A** (`0x08`–`0x3F`) | |
| `0x08` | 2 | Species | National Dex |
| `0x0A` | 2 | Held item | |
| `0x0C` | 2 | TID16 | |
| `0x0E` | 2 | SID16 | |
| `0x10` | 4 | EXP | |
| `0x14` | 1 | Ability | id |
| `0x15` | 1 | **AbilityNumber** | 1/2/4 — a real field, not PID-derived |
| `0x16` | 1 | TrainingBagHits | |
| `0x17` | 1 | TrainingBag | |
| `0x18` | 4 | **PID** | separate from the EncryptionConstant |
| `0x1C` | 1 | **Nature** | stored |
| `0x1D` | 1 | Fateful (bit 0) / Gender (bits 1–2) / Form (bits 3+) | |
| `0x1E`–`0x23` | 6 | EVs (HP, ATK, DEF, SPE, SPA, SPD) | |
| `0x24`–`0x29` | 6 | Contest stats | |
| `0x2A` | 1 | Markings | |
| `0x2B` | 1 | Pokérus | |
| `0x2C`–`0x2F` | 4 | Super Training bit flags | |
| `0x30`–`0x37` | 8 | Ribbons | |
| `0x38` | 1 | RibbonCountMemoryContest | |
| `0x39` | 1 | RibbonCountMemoryBattle | |
| `0x3A` | 2 | DistTrainBitFlags | |
| `0x3C` | 4 | **FormArgument** | |
| — | | **Block B** (`0x40`–`0x77`) | |
| `0x40` | 26 | **Nickname** | 13 UTF-16 units, 12 chars max |
| `0x5A` | 2 | Move 1 | |
| `0x5C` | 2 | Move 2 | |
| `0x5E` | 2 | Move 3 | |
| `0x60` | 2 | Move 4 | |
| `0x62`–`0x65` | 4 | Move PP | |
| `0x66`–`0x69` | 4 | Move PP Ups | |
| `0x6A` | 2 | Relearn move 1 | |
| `0x6C` | 2 | Relearn move 2 | |
| `0x6E` | 2 | Relearn move 3 | |
| `0x70` | 2 | Relearn move 4 | |
| `0x72` | 1 | Secret Super Training flags | bit 0 unlocked, bit 1 supremely trained |
| `0x74` | 4 | **IV32** | same packing as Gen 4/5 |
| — | | **Block C** (`0x78`–`0xAF`) | |
| `0x78` | 26 | **Handling Trainer name** | |
| `0x92` | 1 | HT gender | |
| `0x93` | 1 | **CurrentHandler** | 0 = OT, 1 = HT |
| `0x94`–`0x9D` | 10 | Geo region/country ×5 | |
| `0xA2` | 1 | HT friendship | |
| `0xA3` | 1 | HT affection | |
| `0xA4`–`0xA6` | 3 | HT memory intensity / memory / feeling | |
| `0xA8` | 2 | HT memory variable | |
| `0xAE` | 1 | Fullness | |
| `0xAF` | 1 | Enjoyment | |
| — | | **Block D** (`0xB0`–`0xE7`) | |
| `0xB0` | 26 | **OT name** | |
| `0xCA` | 1 | OT friendship | |
| `0xCB` | 1 | OT affection | |
| `0xCC`–`0xCD` | 2 | OT memory intensity / memory | |
| `0xCE` | 2 | OT memory variable | |
| `0xD0` | 1 | OT memory feeling | |
| `0xD1`–`0xD3` | 3 | Egg year / month / day | |
| `0xD4`–`0xD6` | 3 | Met year / month / day | |
| `0xD8` | 2 | Egg location | |
| `0xDA` | 2 | Met location | |
| `0xDC` | 1 | **Ball** | |
| `0xDD` | 1 | Met level (low 7) + OT gender (bit 7) | |
| `0xDE` | 1 | Ground tile type | **▲ Gen 7 puts HyperTrainFlags here** |
| `0xDF` | 1 | **Version** (origin game) | |
| `0xE0` | 1 | Country | |
| `0xE1` | 1 | Region | |
| `0xE2` | 1 | Console region | |
| `0xE3` | 1 | Language | |
| — | | *— end of `SIZE_6STORED` (`0xE8`) —* | |
| `0xE8` | 4 | Status condition | |
| `0xEC` | 1 | Level | |
| `0xED` | 1 | FormArgumentRemain | |
| `0xEE` | 1 | FormArgumentElapsed | |
| `0xEF` | 1 | TrainingBagEffect | |
| `0xF0` | 2 | Current HP | |
| `0xF2` | 2 | Max HP | |
| `0xF4` | 2 | ATK | |
| `0xF6` | 2 | DEF | |
| `0xF8` | 2 | SPE | |
| `0xFA` | 2 | SPA | |
| `0xFC` | 2 | SPD | |
| — | | *— end of `SIZE_6PARTY` (`0x104`) —* | |

`IV32` at `0x74`, identical packing to Gen 4/5:

```
IV_HP  bits 0-4     IV_ATK bits 5-9     IV_DEF bits 10-14
IV_SPE bits 15-19   IV_SPA bits 20-24   IV_SPD bits 25-29
IsEgg  bit 30       IsNicknamed bit 31
```

Same read-modify-write rule as Gen 4 — do not rebuild the word from six values.

### 5.3 What is new versus Gen 5

| Field | Meaning for PKSE |
|---|---|
| **EncryptionConstant** (`0x00`) separate from **PID** (`0x18`) | `hasPID()` true; the EC is a distinct value. Form correlations (Maushold/Dudunsparce style) key on the EC. PKSE already has `correctEncryptionConstantForForm`. |
| **AbilityNumber** (`0x15`) is a real field | 1 / 2 / 4. No PID inference, no Gen 5 hidden-ability flag. |
| **Handling Trainer** (`0x78`, `0x92`, `0x93`, `0xA2`…) | `htName()`, `htGender()`, `htFriendship()`, `currentHandler()` all become real. **This is where PKSE's OT/HT re-stamp logic starts to apply** (`restampCaughtPokemonIdentity`). |
| **Relearn moves** (`0x6A`–`0x71`) | `relearnMove()` / `setRelearnMove()` become real. |
| **Memories, affection, fullness, enjoyment** | Preserve; PKSE does not edit them. |
| **Country / Region / ConsoleRegion** (`0xE0`–`0xE2`) | Preserve. |
| **Ribbons** (`0x30`–`0x37`) | PKSE's `RibbonNames` needs Gen 6 bit offsets. |
| **12-char names** | Up from 7/10. `getMaxNicknameLength()` = 12. |

**Hyper Training does not exist in Gen 6** — `hasHyperTraining()` stays `false`. Byte `0xDE` is the
ground tile type here. Gen 7 reuses that byte for the hyper-train flags, which is a genuine trap
when copying the class (see [GEN7.md](GEN7.md)).

### 5.4 Stats

Standard Gen 3+ formula with the stored nature. No hyper training, so `effectiveIV()` is the raw IV.

---

## 6. Text — UTF-16

```csharp
// StringConverter6 -- plain UTF-16, 0x0000 terminator
```

PKSE's existing `utf16ToUtf8` / `utf8ToUtf16` and `getString`/`setString` handle it. `canStoreNickname()`
returns `true` unconditionally.

Field widths:

```
OT name       26 bytes = 13 UTF-16 units, 12 characters
Nickname      26 bytes = 13 units,        12 characters
HT name       26 bytes = 13 units,        12 characters
Box name      0x22 = 34 bytes = 17 units, 17 characters
```

Gen 6 introduced full-width/half-width gender-symbol normalisation on the trainer side; PKSE
already handles the `♂`/`♀` glyphs in its name tables (manual ch. 07).

---

## 7. Items

Bag is block 01 (`0x00400`, XY `0x0B88` / ORAS `0x0B90`). Pouch offsets are in
`Items/Bags/PlayerBag6XY.cs` / `PlayerBag6AO.cs`, relative to the block.

The codec is `u16 id + u16 count` packed into a **32-bit word with flags**, closer to Gen 8's than
to Gen 4's:

- Read `PlayerBag6XY.cs` and `InventoryPouch4`-family classes for the exact bit layout of the game
  you are implementing, and mirror `Trainer::InventoryItem8SWSH::fromValue`/`toValue`, which PKSE
  already has for the same idea.
- `itemsAreIdIndexed()` → **false** (slot-based, region rewritten on save).
- Legal sets: `ItemStorage6XY` / `ItemStorage6AO`.

Free-space / "new" and "favourite" flags exist here as they do in Gen 8; preserve them.

---

## 8. Data tables

`tools/gen_gen6.py` → `Gen6Tables.h/.cpp`.

| Table | Source | Notes |
|---|---|---|
| Base stats | `personal_xy`, `personal_ao` | species 0–721, **with forms** |
| Abilities (3 slots incl. hidden) | same | |
| Gender ratio, friendship, form count | same | |
| Types | same | **Fairy exists from Gen 6** — no type remap needed |
| Growth rate | same | verify before reusing |
| Learnsets | `lvlmove_xy/ao.pkl` + TM/HM + tutors + **egg moves** | union with pre-evolutions |
| Move info | `MoveInfo6` | |
| Item names | PKHeX item strings index 6 | 0–775 |
| Item pouch sets | `ItemStorage6XY` / `6AO` | |
| Met location names | `text_xy_*`, `text_ao_*` | two namespaces |
| Ribbon bit offsets | `PK6.cs` `RIB0`–`RIB5` | for `RibbonNames` |
| **Block tables** | the four accessor files | generate (§2.5) |

**Species 1–721, moves 1–621, abilities 1–191 are strict prefixes of the modern tables** —
`getSpeciesName`, `getMoveName`, `getAbilityName` work unchanged. Only **items**, **locations**,
**ribbons** and the **block tables** are new.

**Gen 6 is the first generation whose forms match PKSE's existing form machinery**, since
`PersonalInfoTable`'s form indices come from PKHeX and Gen 6 is where megas were introduced. Expect
`FormNames` and `FormSpriteMapping` to mostly just work — but check `formCount` per species against
the Gen 6 personal table rather than the S/V one.

---

## 9. Legality

Layers 1 and 2 work with a Gen 6 presence gate (`species <= 721`). Layer 3 needs Gen 6 encounter
tables — a separate project; `getEncounterTable()` returning `nullptr` yields one honest Info note.

New for Gen 6, and worth adding to Layer 2 when you get there:

- **Relearn moves must be in the species' egg-move / level-up pool.**
- **`AbilityNumber` must match the ability id** against the species' slots — PKSE's
  `getAbilitySlots` already answers this.
- **`CurrentHandler` must be 0 when `htName` is empty.**

---

## 10. PKSE integration

### 10.1 `GameVersion`

```cpp
enum class GameVersion {
    // … Gen 6 (Nintendo 3DS). Values match PKHeX; getOriginGameName() ALREADY names all four
    // and getVersionGeneration() ALREADY returns 6 for 24..27.
    X  = 24, Y  = 25,
    AS = 26, // Alpha Sapphire
    OR = 27, // Omega Ruby
    // …
    XY   = 87,   // X/Y group
    ORAS = 88,   // Omega Ruby/Alpha Sapphire group
};
```

> **`84`, `85`, `86` are taken** by the `Gen7B` / `Gen8` / `Gen9` generational groupings in
> `GameVersion.h`, which is why Gen 6/7 sit **above** them (`87`+) rather than renumbering anything.
> Group values are PKSE's own invention and are not stored anywhere; what matters is that they never
> collide.

Add `getGameGroup` (`X`/`Y` → `XY`; `AS`/`OR` → `ORAS`), `getGameVersionName`
("X/Y", "Omega Ruby/Alpha Sapphire"), `getGroupRepVersion` (`XY → 24`, `ORAS → 27`),
`isGameSupported` needs no change -- it is derived from `getGameGroup`.

> **X and Y are indistinguishable from the save alone; so are OR and AS** — except that `MyStatus`
> carries a `Game` byte at `+0x04`. **Gen 6 is the first generation where the save says which game
> it is.** Read it and name the game exactly, rather than naming the pair. Fall back to the pair
> only if the byte is not one of the expected values.

### 10.2 New files

```
include/Utils/CRC16.h                      crc16ccitt (+ crc16Invert for Gen 7)
include/Encryption/Encryption6.h + .cpp    Decrypt67/Encrypt67 (copy Encryption8SWSH, block 56)
include/Pokemon/Pokemon6XY.h     + .cpp    PK6 (one class -- the record is identical XY/ORAS)
include/Pokemon/Gen6Tables.h     + .cpp    generated
include/Trainer/Blocks6XY.h .. Blocks7USUM.h  generated block tables, one per group
include/Trainer/Inventory6.h
include/Trainer/Trainer6.h       + .cpp    base: BEEF metadata walk, CRC, block accessors
include/Trainer/Trainer6XY.h     + .cpp
include/Trainer/Trainer6ORAS.h   + .cpp
tools/gen_gen6.py
tools/gen_blocktables.py
```

### 10.3 `Trainer6` base

> The planned shared base below did not land as a base class: its logic lives, copied, in each of
> `Trainer6XY` and `Trainer6ORAS`, with the geometry as plain members. The algorithm is unchanged.

```cpp
namespace Trainer {

    /// One 3DS-era save block. PKHeX BlockInfo3DS: the block data carries no checksum -- every
    /// CRC lives in the trailing BEEF metadata table.
    struct BlockEntry3DS { uint32_t id, offset, length; };

    class Trainer6 : public Trainer {
    protected:
        std::vector<uint8_t> saveData;
        std::string savePath;

        virtual std::span<const BlockEntry3DS> blocks() const noexcept = 0;
        size_t metadataOffset() const noexcept { return saveData.size() - 0x200; }

        const uint8_t* block(uint32_t id) const noexcept {
            for (const BlockEntry3DS& b : blocks()) if (b.id == id) return &saveData[b.offset];
            return nullptr;
        }

        void writeChecksums() {
            const size_t bo = metadataOffset();
            for (const BlockEntry3DS& b : blocks()) {
                // GEN 6 USES CRC16-CCITT. Gen 7 uses CRC16Invert -- see GEN7.md. The two are not
                // interchangeable and nothing in the format will tell you which one is wrong.
                const uint16_t crc = Utils::crc16ccitt(&saveData[b.offset], b.length);
                Utils::writeUInt16LittleEndian(&saveData[bo + 0x14 + b.id * 8 + 6], crc);
            }
        }

    public:
        size_t getBoxCount()    const noexcept override { return 31; }
        size_t getSlotsPerBox() const noexcept override { return 30; }
        uint32_t getMaxMoney()  const noexcept override { return 9999999; }
        size_t getMaxTrainerNameLength() const noexcept override { return 12; }
        int    getMaxTrainerNameDigits() const noexcept override { return 5; }   // gen 6+
        bool   supportsBoxNames() const noexcept override { return true; }
        size_t getMaxBoxNameLength() const noexcept override { return 17; }
        bool   itemsAreIdIndexed() const noexcept override { return false; }
        bool   compactStorage() override { return false; }
    };
}
```

### 10.4 The rest

- **Save write**: the shared loose-file path (`Save::saveExternalSave`), original bytes backed up
  to `sdmc:/PKSE/FileBackups` first. Not in `TrainerVariant`.
- **Entry point**: add `Trainer6XY::detect` / `Trainer6ORAS::detect` to the browser probe chain.
  Check size first (§2.3).
- **Box grid**: 31 × 30 → `boxGridColumns(30) == 6`. **No UI change.**
- `Names::getItemNameFor()`: `XY` / `ORAS` branches.
- `PickerKind`: `ItemG6`, `PouchItemG6`.
- `Bank.cpp`: `BTAG_G6` — **one tag per ENTITY format, not per game.** Every PK6 is stored
  under it and reads back as the format's representative group (`groupForBankTag`). Never compare
  that group to the entity's own; the invariant is `tag == bankTagForEntity(entity)`. Add-only.
- **Conversion**: Gen 6 → Gen 8 is a *plausible* route, since PK6 → PK7 → PB7/PK8 is how HOME
  actually works and PKSE already has the PK8 hub — but Gen 6 is not in `inFamily` today, so it is
  banked and not converted.

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

**The gate is an unedited round trip of a real X/Y and a real ORAS `main`**: open, write straight
back, byte-identical. Gen-6-specific round-trip risks:

- **A wrong `(offset, length)` in a hand-copied block table** — the CRC validates over the wrong
  range. Generate the tables.
- **CRC written to the wrong metadata slot** (`bo + 0x14 + id*8 + 6` — the `+6` is easy to drop).
- **The `u32 length` in the metadata rewritten** — do not touch it.
- **Gen 7's `CRC16Invert` used for Gen 6** (or vice versa) once both exist.
- **`PartyCount` written before the party records** instead of after.
- **Box name written with a 26-byte slice** (that is the *name* field width; box names are `0x22`).
- **`0xDE` treated as hyper-train flags** — that is Gen 7.

Then the party-stat cross-check (a party record stores its battle stats: recompute and compare) and
edit persistence (change each field, save, reopen, check each one separately).

---

## 12. Trap list

1. **Gen 6 uses `CRC16_CCITT`; Gen 7 uses `CRC16Invert`.** Different values, no warning.
2. **Every 3DS save has the BEEF magic.** Size is what discriminates. A size collision is a refusal.
3. **The CRC lives in the metadata table**, at `metadataOffset + 0x14 + id*8 + 6`.
4. **Generate the block tables.** A typo validates happily over the wrong range.
5. **`PartyCount` is AFTER the six party records** (`Party + 6*260`).
6. **Box names are 17 characters / `0x22` bytes.** Not 8, not 13.
7. **EncryptionConstant (`0x00`) and PID (`0x18`) are different fields.**
8. **`AbilityNumber` (`0x15`) is a stored field**, not PID-derived and not a Gen 5 flag.
9. **`0xDE` is the ground tile in Gen 6 and the hyper-train flags in Gen 7.**
10. **No Hyper Training in Gen 6.** `hasHyperTraining()` stays false.
11. **`IV32` carries the egg and nicknamed flags.** Read-modify-write.
12. **Checksum is NOT the crypt key** (unlike Gen 4/5). Refresh it before or after encryption as you
    like — but keep the Gen 8 order for consistency: recalc → checksum → encrypt.
13. **XY and ORAS put box storage at different offsets** (`0x22600` vs `0x33000`) but share the
    party offset.
14. **Gen 6 saves say which game they are** (`MyStatus + 0x04`). Use it.
15. **Group enum values `84`–`86` are taken**; Gen 6 is `87`/`88`.
16. **The ORAS demo (`0x5A00`) is not a full save.** Refuse it.
17. **`compactStorage()` stays false.**

---

## 13. Suggested order

1. `crc16ccitt` (shared with Gen 4) + `gen_blocktables.py`. **Validate a real save's CRCs with the
   generated table before writing anything else.**
2. `Encryption6` — copy `Encryption8SWSH`, change the block size to 56. Prove it by decrypting a
   known record and checking the checksum.
3. Generated tables (`gen_personaltables.py`); check them against PKHeX.
4. `Pokemon6XY`; every setter reads back what it wrote.
5. `Trainer6` base + `Trainer6XY`. Round trip to zero diff.
6. `Trainer6ORAS` — block table and two offsets.
7. Items, box names, trainer fields.
8. Entry point (this is what exposes the format), UI, bank, docs.
9. **Then [GEN7.md](GEN7.md)**, which is mostly a delta on steps 2–7.
