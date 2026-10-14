# Generation 4 — Diamond / Pearl / Platinum / HeartGold / SoulSilver (PK4, SAV4)

> **Status: implemented.** This is the PKHeX transcription Gen 4 was built from, kept as the
> reference for that code. It was written as a plan, so where §11 and §14 name files, the tree
> differs in one systematic way: **every layer has one class per group, with no shared base** —
> `Trainer4DP` / `Trainer4PT` / `Trainer4HGSS`, `Pokemon4DP` / `Pokemon4PT` / `Pokemon4HGSS`,
> `Encryption4DP` / `4PT` / `4HGSS`, `Inventory4DP` / `4PT` / `4HGSS` — rather than the single
> `Pokemon4DPPt`, `Trainer4` base, `Encryption4` and `Inventory4` proposed below. Per-group
> geometry (partition sizes, `FooterSize`, `GEN4_SAVE_SIZE`, `GEN4_PARTITION`, the SDK magics) is a
> `static constexpr` member of each class. Personal data comes from `tools/gen_personaltables.py`
> (`PersonalInfo4DP/4PT/4HGSS.cpp`), not a `gen_gen4.py`; the text table from
> `tools/gen_gen4text.py` (`Utils/Gen4Text.h`). The bank stores every PK4 under one tag, `BTAG_G4`,
> one tag per entity format. See [manual ch. 04](../manual/04-generation-pattern.md) and
> [ch. 05](../manual/05-save-pipeline.md).

> **Source of truth: PKHeX.** Every offset below was transcribed at commit `6501f0ab46e8f8ca048539dbaf8cae8cb104e722`;
> PKSE now tracks PKHeX's default branch, so re-verify against the file you fetch.
> Files this document is derived from: `Saves/SAV4.cs`, `Saves/SAV4Sinnoh.cs`, `Saves/SAV4DP.cs`,
> `Saves/SAV4PT.cs`, `Saves/SAV4HGSS.cs`, `Saves/Substructures/Gen4/SAV4BlockDetection.cs`,
> `PKM/PK4.cs`, `PKM/Shared/G4PKM.cs`, `PKM/Util/PokeCrypto.cs`, `Saves/Util/Checksums.cs`,
> `Saves/Util/SaveUtil.cs`, `PKM/Strings/StringConverter4.cs`, `PKM/Strings/StringConverter4Util.cs`,
> `Items/Bags/PlayerBag4DP.cs` / `4PT.cs` / `4HGSS.cs`,
> `Saves/Substructures/Inventory/Pouch/InventoryPouch4.cs`, `Legality/Legal.cs`.

---

## 0. Read this first

Gen 4 is the **first generation where PKSE's existing encryption machinery genuinely applies**, and
also the first where the *save container* is more complicated than anything in the tree. Two
independent difficulties, and they are worth separating in your head:

| Layer | Difficulty | Why |
|---|---|---|
| **Entity (PK4)** | **Low** | The block-shuffle + LCG crypt PKSE already implements for Gen 8/9, with a different block size and a different seed. Flat little-endian offsets after that. |
| **Save (SAV4)** | **High** | Two mirrored 256 KiB partitions, each holding two independently-checksummed blocks with counter footers. You must pick the newest of each. |

The entity is a day's work. **The save container is the project.**

Prerequisites in the manual: [04](../manual/04-generation-pattern.md),
[05](../manual/05-save-pipeline.md), [06](../manual/06-pokemon-entity.md),
[13](../manual/13-adding-a-game.md).

**Three games, three offset sets.** Per PKSE's per-game-class rule (manual ch. 04) that means three
`Trainer` classes: `Trainer4DP`, `Trainer4PT`, `Trainer4HGSS`. They share the container logic
(a `Trainer4` base, mirroring PKHeX's `SAV4`) but not their offsets.

---

## 1. Scope

| | |
|---|---|
| Games | Diamond, Pearl (`DP`), Platinum (`Pt`), HeartGold, SoulSilver (`HGSS`) |
| Groups | `GameVersion::DPPT`… see §11.1 — **three** groups, one per save layout |
| Entity | **PK4** |
| Save file | loose `.sav`, **`0x80000` = 512 KiB** (`SIZE_G4RAW`) |
| Entity encryption | LCG XOR + 4-block shuffle, **seed = the checksum** |
| Entity checksum | 16-bit `Add16` over `data[0x08 .. 0x88)` |
| Save checksum | **CRC16-CCITT** per block |
| Endianness | little endian throughout |
| Boxes | **18 × 30** in every Gen 4 game |
| Max species | 493 |
| Max move id | 467 |
| Max item id | DP 464, Pt 467, HGSS 536 |
| Max ability id | 123 |
| Max ball id | `0x18` (24) |
| Max money | 999,999 |
| OT name | 7 chars, 16-byte field |
| Nickname | 10 chars, 22-byte field |

---

## 2. The save container

### 2.1 Two partitions, and why

A Gen 4 save is `0x80000` bytes: **two `0x40000` partitions**, a primary and a backup. The game
alternates between them on each save. Within each partition there are **two blocks**:

```
partition 0 (0x00000)                     partition 1 (0x40000)
┌──────────────────────────┐              ┌──────────────────────────┐
│ General block            │              │ General block            │
│   (trainer, party, bag,  │              │   (backup copy)          │
│    pokedex, flags …)     │              │                          │
│   … footer               │              │   … footer               │
├──────────────────────────┤              ├──────────────────────────┤
│ Storage block            │              │ Storage block            │
│   (18 boxes, box names,  │              │   (backup copy)          │
│    wallpapers)           │              │                          │
│   … footer               │              │   … footer               │
├──────────────────────────┤              ├──────────────────────────┤
│ Extra blocks             │              │ Extra blocks             │
│   (Hall of Fame, Battle  │              │                          │
│    Hall, Battle Videos)  │              │                          │
└──────────────────────────┘              └──────────────────────────┘
```

**The General and Storage blocks are chosen INDEPENDENTLY.** It is entirely normal for the newest
General block to be in partition 0 while the newest Storage block is in partition 1.

```csharp
// SAV4.cs
private const int PartitionSize = 0x40000;

protected SAV4(Memory<byte> data, int gSize, int sSize, int sStart) : base(data)
{
    var GeneralBlockPosition = GetActiveBlock(Data, 0,      gSize);
    var StorageBlockPosition = GetActiveBlock(Data, sStart, sSize);

    var gbo = (GeneralBlockPosition == 0 ? 0 : PartitionSize);
    var sbo = (StorageBlockPosition == 0 ? 0 : PartitionSize) + sStart;
    GeneralBuffer = Buffer.Slice(gbo, gSize);
    StorageBuffer = Buffer.Slice(sbo, sSize);

    // …and the OTHER copy of each is kept as the backup buffer
    var gboBackup = (GeneralBlockPosition != 0 ? 0 : PartitionSize);
    var sboBackup = (StorageBlockPosition != 0 ? 0 : PartitionSize) + sStart;
}
```

### 2.2 Block sizes, per game

```
                GeneralSize   StorageSize   sStart (storage offset within a partition)
DP              0xC100        0x121E0       0xC100                = GeneralSize
Pt              0xCF2C        0x121E4       0xCF2C                = GeneralSize
HGSS            0xF628        0x12310       0xF700                = GeneralSize + 0xD8
```

`GeneralGap = 0xD8` is **HGSS only** — there is unclaimed space between the two blocks there.
DP and Pt have none.

Sanity check that both blocks fit a partition:
`0xC100 + 0x121E0 = 0x1E2E0 < 0x40000` ✓ (DP) — plenty of room, which is where the extra blocks live.

### 2.3 Picking the active block

Each block ends with a **footer**. `GetActiveBlock` reads the footer of the same block in both
partitions and compares counters:

```csharp
private static int GetActiveBlock(ReadOnlySpan<byte> data, int begin, int length)
{
    int offset = begin + length - 0x14;                    // ALWAYS 0x14 from the block end
    return SAV4BlockDetection.CompareFooters(data, offset, offset + PartitionSize);
}
```

> **Note the `0x14` is hardcoded here**, even for HGSS whose `FooterSize` is `0x10`. The two
> constants are answering different questions: `0x14` is where the *counters* are; `FooterSize` is
> how much of the block tail is **excluded from the CRC**. Do not unify them.

Footer layout, relative to `blockEnd - 0x14`:

```
+0x00  u32   major counter          <- compared first
+0x04  u32   minor counter          <- tiebreak
+0x08  …     (block-specific)
       u32   magic       at blockEnd - 0x08
       u16   size/other  at blockEnd - 0x04
       u16   CRC16-CCITT at blockEnd - 0x02
```

```csharp
// SAV4BlockDetection.cs
public static int CompareFooters(ReadOnlySpan<byte> data, int offset1, int offset2)
{
    var major1 = ReadUInt32LittleEndian(data[offset1..]);
    var major2 = ReadUInt32LittleEndian(data[offset2..]);
    var result1 = CompareCounters(major1, major2);
    if (result1 != Same) return result1;

    var minor1 = ReadUInt32LittleEndian(data[(offset1 + 4)..]);
    var minor2 = ReadUInt32LittleEndian(data[(offset2 + 4)..]);
    var result2 = CompareCounters(minor1, minor2);
    return result2 == Second ? Second : First;   // Same -> First
}

private static int CompareCounters(uint c1, uint c2)
{
    // Uninitialized -- only continue if a rollover case (humanly impossible)
    if (c1 == uint.MaxValue && c2 != uint.MaxValue - 1) return Second;
    if (c2 == uint.MaxValue && c1 != uint.MaxValue - 1) return First;
    if (c1 > c2) return First;
    if (c1 < c2) return Second;
    return Same;
}
```

> **The `0xFFFFFFFF` handling is not decoration.** A fresh save has one partition never written, so
> its counter reads `0xFFFFFFFF` — which is numerically *larger* than any real counter. A naive
> `c1 > c2` picks the uninitialised garbage partition and you load an empty save from a real one.
> Port `CompareCounters` exactly.

### 2.4 Checksums

```csharp
// SAV4.cs
private ushort CalcBlockChecksum(ReadOnlySpan<byte> data) => Checksums.CRC16_CCITT(data[..^FooterSize]);
private static ushort GetBlockChecksumSaved(ReadOnlySpan<byte> data) => ReadUInt16LittleEndian(data[^2..]);

protected sealed override void SetChecksums()
{
    WriteUInt16LittleEndian(General[^2..], CalcBlockChecksum(General));
    WriteUInt16LittleEndian(Storage[^2..], CalcBlockChecksum(Storage));
    // Only refresh a backup block that is INITIALISED (magic != 0xFFFFFFFF)
    if (ReadUInt32LittleEndian(BackupGeneral[^8..^4]) != 0xFFFFFFFF)
        WriteUInt16LittleEndian(BackupGeneral[^2..], CalcBlockChecksum(BackupGeneral));
    if (ReadUInt32LittleEndian(BackupStorage[^8..^4]) != 0xFFFFFFFF)
        WriteUInt16LittleEndian(BackupStorage[^2..], CalcBlockChecksum(BackupStorage));
    ExtraBlocks.SetChecksums(Data);
    ExtraBlocks.SetChecksums(Data[PartitionSize..]);
}
```

```
FooterSize:  DP 0x14   Pt 0x14   HGSS 0x10
Magic:       0x20060623 (Japanese + International)
             0x20070903 (Korean)
Magic offset: blockEnd - 0x08 .. blockEnd - 0x04
CRC offset:   blockEnd - 0x02
```

**CRC16-CCITT** — the bitwise variant in `Checksums.cs`, not the table-driven `CRC16Invert`:

```csharp
public static ushort CRC16_CCITT(ReadOnlySpan<byte> data)
{
    byte top = 0xFF, bot = 0xFF;
    foreach (var b in data)
    {
        var x = b ^ top;
        x ^= (x >> 4);
        top = (byte)(bot ^ (x >> 3) ^ (x << 4));
        bot = (byte)(x ^ (x << 5));
    }
    return (ushort)((top << 8) | bot);
}
```

PKSE has no CRC16 yet — add it to `Utils/`. Direct C++ port:

```cpp
// include/Utils/CRC16.h
namespace Utils {
    /// CRC16-CCITT, PKHeX Checksums.CRC16_CCITT. Gen 4 save blocks and Gen 6/7 save blocks use
    /// DIFFERENT CRC16 variants -- this is the Gen 4 one. See Gen 6/7's CRC16Invert.
    inline uint16_t crc16ccitt(const uint8_t* data, size_t len) noexcept {
        uint8_t top = 0xFF, bot = 0xFF;
        for (size_t i = 0; i < len; ++i) {
            int x = data[i] ^ top;
            x ^= (x >> 4);
            top = static_cast<uint8_t>(bot ^ (x >> 3) ^ (x << 4));
            bot = static_cast<uint8_t>(x ^ (x << 5));
        }
        return static_cast<uint16_t>((top << 8) | bot);
    }
}
```

### 2.5 What "saving" means

PKHeX writes into **the block it loaded** (whichever partition that was) and refreshes that block's
CRC, plus the backup block's CRC **if the backup is initialised**. It does **not** flip partitions
or bump counters.

That is the behaviour to copy: it keeps the file byte-identical when nothing changed, which is what
an unedited round trip requires.

> **Do not "helpfully" bump the counter or write the other partition.** Neither is needed for the
> game to load your edits, and both make the round trip un-diffable.

### 2.6 Detection

`SaveUtil` identifies Gen 4 by size **and by the General block's footer**, and it deliberately
checks the block in the **second** partition:

```csharp
private static bool IsG4DP  (data) => data.Length == SIZE_G4RAW && IsValidGeneralFooter2(data, SAV4DP.GeneralSize);
private static bool IsG4PT  (data) => data.Length == SIZE_G4RAW && IsValidGeneralFooter2(data, SAV4PT.GeneralSize);
private static bool IsG4HGSS(data) => data.Length == SIZE_G4RAW && IsValidGeneralFooter2(data, SAV4HGSS.GeneralSize);

private static bool IsValidGeneralFooter2(ReadOnlySpan<byte> data, int length)
{
    // Check the other save -- the first save is written to the latter half of the binary.
    // The second save should be all that is needed to check.
    const int generalOffset = 0x40000;
    var general = data.Slice(generalOffset, length);

    // The block footers contain a 32-bit 'size' followed by a 32-bit binary-coded-decimal timestamp.
    // Korean saves have a different timestamp from other localizations.
    var size = ReadUInt32LittleEndian(general[^0xC..]);
    if (size != general.Length)
        return false;
    var sdk = ReadUInt32LittleEndian(general[^0x8..]);
    return sdk is SAV4.MAGIC_JAPAN_INTL or SAV4.MAGIC_KOREAN;
}
```

So the tail of every Gen 4 block is:

```
blockEnd - 0x14   u32  major counter        (block selection)
blockEnd - 0x10   u32  minor counter        (block selection tiebreak)
blockEnd - 0x0C   u32  BLOCK SIZE           == the block's own length
blockEnd - 0x08   u32  SDK magic            0x20060623 or 0x20070903
blockEnd - 0x04   u16  (unused / padding)
blockEnd - 0x02   u16  CRC16-CCITT
```

The **size field equalling the block length** is what makes this discriminate DP from Pt from HGSS:
their `GeneralSize` values differ, so at most one probe matches.

Direct port:

```cpp
/// True when `bytes` is a Gen 4 save with a General block of `generalSize`. Mirrors PKHeX
/// SaveUtil.IsValidGeneralFooter2 -- it checks the block in the SECOND partition, because the
/// first save a game makes goes into the latter half of the binary.
///
/// Conservative on purpose: 0x80000 is ALSO Gen 5's size, and a false accept produces an empty
/// editor with no explanation.
static bool detectGen4(const std::vector<uint8_t>& b, size_t generalSize) noexcept {
    constexpr size_t kSecondPartition = 0x40000;
    if (b.size() != 0x80000) return false;
    const size_t end = kSecondPartition + generalSize;
    if (end > b.size()) return false;

    if (Utils::readUInt32LittleEndian(&b[end - 0x0C]) != generalSize) return false;
    const uint32_t sdk = Utils::readUInt32LittleEndian(&b[end - 0x08]);
    return sdk == 0x20060623u || sdk == 0x20070903u;
}
```

Probe DP (`0xC100`), then Pt (`0xCF2C`), then HGSS (`0xF628`).

> **`0x80000` is also `SIZE_G5RAW`.** Gen 4 and Gen 5 saves are the same length, so length alone
> tells you nothing. Run the Gen 4 probes *and* the Gen 5 probes (GEN5.md §2.4) and require exactly
> one to match.

## 3. Storage layout

**Every Gen 4 game: 18 boxes × 30 slots**, stored-size (`0x88` = 136 byte) records.

### 3.1 DP and Platinum (`SAV4Sinnoh`)

```csharp
// SAV4Sinnoh.cs
// u32 currentBox
// box{pk4[30]}[18]
// g4str[18] boxNames
// byte[18]  boxWallpapers
private const int BOX_COUNT = 18;
private const int BOX_SLOTS = 30;
private const int BOX_NAME_LEN = 40;                               // 20 UTF-16 units

private const int BOX_DATA_LEN = BOX_SLOTS * PokeCrypto.SIZE_4STORED;   // 0xFF0, NO padding
private const int BOX_END      = BOX_COUNT * BOX_DATA_LEN;              // 18 * 0xFF0
private const int BOX_NAME     = 4 + BOX_END;
private const int BOX_WP       = BOX_NAME + (BOX_COUNT * BOX_NAME_LEN); // 0x121B4
private const int BOX_FLAGS    = 18 + BOX_WP;                           // 0x121C6

public override int GetBoxOffset(int box) => 4 + (box * BOX_DATA_LEN);
public override int CurrentBox { get => Storage[0]; set => Storage[0] = (byte)value; }
```

### 3.2 HGSS

```csharp
// SAV4HGSS.cs
// box{pk4[30]}[18]
// u32 currentBox
// u32 counter
// g4str[18] boxNames
// byte[18]  boxWallpapers
// -- each box is chunked, PADDED to the nearest 0x100 (0x10 trailing zeroes per box)
// -- the final 0x16 bytes of the Storage block are unused padding
private const int BOX_DATA_LEN = (BOX_SLOTS * PokeCrypto.SIZE_4STORED) + 0x10;  // 0x1000
private const int BOX_END      = BOX_COUNT * BOX_DATA_LEN;                      // 18 * 0x1000
private const int BOX_NAME     = 0x12008;      // after currentBox + counter
private const int BOX_WP       = BOX_NAME + (BOX_COUNT * BOX_NAME_LEN);         // 0x122D8
private const int BOX_FLAGS    = 18 + BOX_WP;                                   // 0x122EA

public override int GetBoxOffset(int box) => box * 0x1000;
public override int CurrentBox { get => Storage[BOX_END]; set => Storage[BOX_END] = (byte)value; }
```

> **Three differences from DP/Pt in one structure**, and every one of them will silently misplace
> Pokémon if you copy the wrong layout:
> 1. **Box data starts at offset 0**, not 4.
> 2. **Each box is padded to `0x1000`**, not packed at `0xFF0` — 16 bytes of slack per box.
> 3. **`currentBox` is at the END** of the box data, not the start.

### 3.3 Box names and wallpapers

Names are 40-byte (20 UTF-16 unit) fields, max 8 characters:

```csharp
private Span<byte> GetBoxNameSpan(int box) => Storage.Slice(BOX_NAME + (box * 40), 40);
public string GetBoxName(int box) => GetString(GetBoxNameSpan(box));
public void SetBoxName(int box, ReadOnlySpan<char> value)
    => SetString(GetBoxNameSpan(box), value, 8, StringConverterOption.ClearZero);
```

Wallpapers are one byte per box, but **HGSS and Platinum shift their special wallpapers**:

```csharp
// HGSS special wallpapers 1-8 (Primo Phrases) are shifted by +0x10;
// Pt's special wallpapers 1-8 are shifted by +0x8.
private static int AdjustWallpaper(int value, int shift)
    => value >= 0x10 ? value + shift : value;

public int  GetBoxWallpaper(int box) => AdjustWallpaper(Storage[GetBoxWallpaperOffset(box)], -0x10);
public void SetBoxWallpaper(int box, int value) => Storage[…] = (byte)AdjustWallpaper(value, 0x10);
```

PKSE does not currently show wallpapers. **Preserve the byte and do not touch it** — that is enough
for a clean round trip.

`BOX_FLAGS` and, in HGSS, `FlagsBoxContentChanged` (`int32` at `BOX_END + 4`, all-boxes value
`0x3_FFFF`) are the game's "which boxes changed since last save" bitfield. Preserve them verbatim.

### 3.4 Party

Party lives in the **General** block, not Storage:

```csharp
public sealed override int GetPartyOffset(int slot) => Party + (SIZE_PARTY * slot);   // SIZE_4PARTY = 236
public override int PartyCount { get => General[Party - 4]; set => General[Party - 4] = (byte)value; }
```

Note `PartyCount` is at **`Party - 4`**, i.e. immediately before the party records.

---

## 4. Trainer block (General)

All offsets relative to `Trainer1`:

```
Trainer1 + 0x00   16   OT name (UTF-16-ish, 7 chars max, see §7)
Trainer1 + 0x10   4    ID32   (TID16 at +0x10, SID16 at +0x12)
Trainer1 + 0x14   4    Money  (max 999999)
Trainer1 + 0x18   1    Gender
Trainer1 + 0x19   1    Language
Trainer1 + 0x1A   1    Badges
Trainer1 + 0x1B   1    Sprite (trainer avatar)
Trainer1 + 0x1C   1    ROMCode        (unused by D/P)
Trainer1 + 0x1D   1    ProgressFlags  (bit 0 = GameClear)
```

Per game:

| | **DP** | **Pt** | **HGSS** |
|---|---|---|---|
| `Trainer1` | `0x64` | `0x68` | `0x64` |
| `Party` | `0x98` | `0xA0` | `0x98` |
| `Box` (storage start) | `4` | `4` | `0` |
| `EventWork` | `0xD9C` | `0xDAC` | `0xDE4` |
| `EventFlag` | `0xFDC` | `0xFEC` | `0x10C4` |
| `DaycareOffset` | `0x141C` | `0x1654` | `0x15FC` |
| `PokeDex` | `0x12DC` | `0x1328` | `0x12B8` |
| `AdventureInfo` | `0x0` | `0x0` | `0x0` |
| `FashionCase` | `0x4BA8` | — | — |
| `OFS_Record` | `0x5F08` | `0x61B0` | `0x4B3C` |
| `OFS_Chatter` | `0x61CC` | `0x64EC` | `0x4E74` |
| `OFS_Groups` | `0x5374` | `0x5610` | `0x440C` |
| `Geonet` | `0x96D8` | — | — |
| `Seal` | `0x6178` | — | — |
| `OFS_PoffinCase` | `0x5050` | `0x52E8` | — |
| `OFS_HONEY` | `0x72E4` | `0x7F38` | — |
| `OFS_UG_Stats` | `0x3A2C` | `0x3CB4` | — |
| `OFS_UG_Items` | `0x42B0` | `0x4538` | — |
| `PoketchStart` | `0x114C` | *(set in Pt)* | — |
| `Mystery` block | `0xA6D0` | — | — |
| Bag base offset | `0x624` | `0x630` | `0x644` |

Fields marked `—` either do not exist in that game or are outside PKSE's scope (Poketch,
Underground, Poffins, Honey trees, seals, Geonet). **Preserve those regions untouched.**

Adventure info / playtime, relative to `AdventureInfo`:

```
+0x22  u16  PlayedHours          (see SAV4.cs; also +0x24 minutes, +0x25 seconds)
+0x34  u32  SecondsToStart
+0x3C  u32  SecondsToFame
```

Daycare, relative to `DaycareOffset`:

```
slot i (i = 0,1):  DaycareOffset + i*236, SIZE_4STORED (136) bytes of entity
                   EXP: last 4 bytes of each 236-byte slot
DaycareEnd = DaycareOffset + 2*236
DaycareEnd + 0     u32  egg seed
DaycareEnd + 4     u8   step counter
```

Mystery-gift unlock is `General[72] & 1` in every game.

---

## 5. The PK4 entity

### 5.1 Sizes and crypto

```
SIZE_4STORED = 136  (0x88)
SIZE_4PARTY  = 236  (0xEC)
SIZE_4BLOCK  = 32   (0x20)   — four blocks of 32 bytes at 0x08..0x88
```

```csharp
// PokeCrypto.cs
public static void Decrypt45(Span<byte> data)
{
    uint pv  = ReadUInt32LittleEndian(data);          // PID at 0x00
    uint chk = ReadUInt16LittleEndian(data[6..]);     // CHECKSUM at 0x06  <-- the seed
    uint sv  = (pv >> 13) & 31;                       // shuffle value from the PID

    var shuffle = data[8..SIZE_4STORED];
    CryptArray(shuffle, chk);                         // main body keyed on the CHECKSUM
    if (data.Length > SIZE_4STORED)
        CryptArray(data[SIZE_4STORED..], pv);         // party stats keyed on the PID
    Shuffle45(shuffle, sv);
}

public static void Encrypt45(Span<byte> data)
{
    uint pv  = ReadUInt32LittleEndian(data);
    uint chk = ReadUInt16LittleEndian(data[6..]);
    uint sv  = (pv >> 13) & 31;
    sv = BlockPositionInvert[(int)sv];                // INVERT before shuffling on the way out

    var shuffle = data[8..SIZE_4STORED];
    Shuffle45(shuffle, sv);
    CryptArray(shuffle, chk);
    if (data.Length > SIZE_4STORED)
        CryptArray(data[SIZE_4STORED..], pv);
}
```

`CryptArray` is the **same LCG PKSE already has** (`Encryption::cryptArray`):

```
seed = 0x41C64E6D * seed + 0x00006073;  xor = seed >> 16;  u16 ^= xor
```

`BlockPosition` / `BlockPositionInvert` are **the same tables PKSE already has** in
`Encryption/Encryption.h`.

> **The one genuinely new thing is the seed.** Gen 6/7/8/9 crypt the body with the **PID**; Gen 4/5
> crypt the body with the **checksum** and only the party tail with the PID. Reusing the Gen 8
> function verbatim will "work" — it will decrypt to garbage that has no checksum to catch it,
> because you also have to write the checksum yourself.
>
> **Order matters on encrypt**: refresh the checksum → *then* encrypt, because the checksum is the
> key. Refreshing after encryption changes the key the data was encrypted with.

Ordering to implement:

```cpp
// Writing a PK4 back into a box slot
pk->recalculateStats();
pk->refreshChecksum();        // MUST precede encryption -- the checksum is the crypt seed
std::byte* enc = Encryption::encryptArray4(pk->getData(), /*size*/);
std::memcpy(&block[offset], enc, size);
delete[] enc;
```

Detecting an already-decrypted buffer:

```csharp
// unused ribbon bits at 0x64 are zero in a decrypted record
public static bool IsEncrypted45(ReadOnlySpan<byte> data) => ReadUInt32LittleEndian(data[0x64..]) != 0;
```

### 5.2 Checksum

```csharp
protected virtual ushort CalculateChecksum() => Checksums.Add16(Data[8..PokeCrypto.SIZE_4STORED]);
public sealed override void RefreshChecksum() => Checksum = CalculateChecksum();
public override bool Valid { get => Sanity == 0 && ChecksumValid; … }
```

`Add16` is a plain 16-bit little-endian word sum over `0x08..0x88` — PKSE already has this shape in
its Gen 8/9 code.

**`Sanity` (`0x04`) must be 0** for a valid record. Same role as the Gen 8 sanity field.

### 5.3 Complete PK4 offset map

Little endian throughout. `[B]` marks the block a field lives in (A = `0x08`, B = `0x28`,
C = `0x48`, D = `0x68`, each 32 bytes) — relevant only if you are debugging a shuffle.

| Off | Size | Field | Notes |
|---|---|---|---|
| `0x00` | 4 | **PID** | also the shuffle seed and the party-stat crypt key |
| `0x04` | 2 | Sanity | must be 0 |
| `0x06` | 2 | **Checksum** | **the body crypt key** |
| — | | **Block A** (`0x08`–`0x27`) | |
| `0x08` | 2 | Species | National Dex |
| `0x0A` | 2 | Held item | Gen 4 item id space |
| `0x0C` | 2 | TID16 | |
| `0x0E` | 2 | SID16 | |
| `0x10` | 4 | EXP | |
| `0x14` | 1 | OT friendship | also `CurrentFriendship` |
| `0x15` | 1 | Ability | id, 1–123 |
| `0x16` | 1 | Markings | |
| `0x17` | 1 | Language | |
| `0x18` | 1 | EV HP | |
| `0x19` | 1 | EV ATK | |
| `0x1A` | 1 | EV DEF | |
| `0x1B` | 1 | EV SPE | |
| `0x1C` | 1 | EV SPA | |
| `0x1D` | 1 | EV SPD | |
| `0x1E`–`0x23` | 6 | Contest stats | Cool, Beauty, Cute, Smart, Tough, Sheen |
| `0x24`–`0x27` | 4 | Ribbons (Sinnoh 1–2, Unova 1–2) | |
| — | | **Block B** (`0x28`–`0x47`) | |
| `0x28` | 2 | Move 1 | |
| `0x2A` | 2 | Move 2 | |
| `0x2C` | 2 | Move 3 | |
| `0x2E` | 2 | Move 4 | |
| `0x30`–`0x33` | 4 | Move PP (1 byte each) | |
| `0x34`–`0x37` | 4 | Move PP Ups (1 byte each) | |
| `0x38` | 4 | **IV32** | packed; see below |
| `0x3C`–`0x3F` | 4 | Ribbons (Hoenn) | |
| `0x40` | 1 | Fateful / Gender / Form | bit 0 fateful, bits 1–2 gender, bits 3+ form |
| `0x41` | 1 | Shiny Leaf | |
| `0x42`–`0x43` | 2 | *unused* | |
| `0x44` | 2 | Egg location (extended) | |
| `0x46` | 2 | Met location (extended) | |
| — | | **Block C** (`0x48`–`0x67`) | |
| `0x48` | 22 | **Nickname** | 11 UTF-16 units, terminator `0xFFFF` |
| `0x5E` | 1 | *unused* | |
| `0x5F` | 1 | **Version** (origin game) | |
| `0x60`–`0x63` | 4 | Ribbons (Sinnoh 3–6) | |
| — | | **Block D** (`0x68`–`0x87`) | |
| `0x68` | 16 | **OT name** | 8 UTF-16 units |
| `0x78` | 1 | Egg year | |
| `0x79` | 1 | Egg month | |
| `0x7A` | 1 | Egg day | |
| `0x7B` | 1 | Met year | |
| `0x7C` | 1 | Met month | |
| `0x7D` | 1 | Met day | |
| `0x7E` | 2 | Egg location (DP/Pt) | see §5.5 |
| `0x80` | 2 | Met location (DP/Pt) | |
| `0x82` | 1 | Pokérus | |
| `0x83` | 1 | **Ball (DP/Pt)** | |
| `0x84` | 1 | Met level (low 7) + OT gender (bit 7) | |
| `0x85` | 1 | Ground tile type | |
| `0x86` | 1 | **Ball (HGSS)** | see §5.5 |
| `0x87` | 1 | Walking mood | |
| — | | *— end of `SIZE_4STORED` (`0x88`) —* | |
| `0x88` | 4 | Status condition | |
| `0x8C` | 1 | Level | |
| `0x8D` | 1 | Ball capsule index | |
| `0x8E` | 2 | Current HP | |
| `0x90` | 2 | Max HP | |
| `0x92` | 2 | ATK | |
| `0x94` | 2 | DEF | |
| `0x96` | 2 | SPE | |
| `0x98` | 2 | SPA | |
| `0x9A` | 2 | SPD | |
| `0x9C` | 56 | Held mail | |
| — | | *— end of `SIZE_4PARTY` (`0xEC`) —* | |

### 5.4 IV32 bit layout (`0x38`)

```csharp
IV_HP      = (IV32 >> 00) & 0x1F
IV_ATK     = (IV32 >> 05) & 0x1F
IV_DEF     = (IV32 >> 10) & 0x1F
IV_SPE     = (IV32 >> 15) & 0x1F
IV_SPA     = (IV32 >> 20) & 0x1F
IV_SPD     = (IV32 >> 25) & 0x1F
IsEgg      = (IV32 >> 30) & 1
IsNicknamed= (IV32 >> 31) & 1
```

> **The egg flag and the nicknamed flag live in the IV word.** A naive "write the IVs" that
> rebuilds `IV32` from six values clears both. Read-modify-write the whole `uint32`, exactly as
> PKHeX does.

### 5.5 Two ball fields — the HGSS trap

```csharp
public override byte BallDPPt { get => Data[0x83]; set => Data[0x83] = value; }
public override byte BallHGSS { get => Data[0x86]; set => Data[0x86] = value; }
```

`0x86` is a field HGSS added; DP/Pt leave it zero. PKHeX's `PK4.Ball` getter picks between them
based on origin version. **Implement `ball()` to read `0x86` when non-zero and fall back to
`0x83`**, and write both on set (that is what PKHeX's HGSS path does), or you get an Apricorn ball
reading as a Poké Ball.

### 5.6 Derived values

```csharp
Nature              = PID % 25                    // NOT stored -- derived, like Gen 3
EncryptionConstant  = PID                         // aliased
AbilityNumber       = 1 << PIDAbility             // slot from the PID
PSV                 = ((PID >> 16) ^ (PID & 0xFFFF)) >> 3
TSV                 = (TID16 ^ SID16) >> 3
IsShiny             = PSV == TSV
CurrentHandler      = 0                           // Gen 4 has no handler concept
```

So: **`hasNature()` is true but `setNature()` cannot be a direct write** — changing the nature means
rebuilding the PID, exactly as PKSE already does for Gen 3 (`regeneratePID`). Reuse that pattern
and reuse the Gen 3 warning dialog policy: a PID rebuild is destructive, so its dialog outranks the
others when a move matches more than one — but like all of them it is shown only while `g_moveWarn`
is on.

---

## 6. Stats

Standard Gen 3+ formula — **the one PKSE already implements**:

```
HP    = ((2*Base + IV + EV/4) * Level) / 100 + Level + 10
Other = (((2*Base + IV + EV/4) * Level) / 100 + 5) * NatureMod / 100
```

with `NatureMod ∈ {90, 100, 110}` from `PID % 25`.

Gen 4 has **no Hyper Training**, so `hasHyperTraining()` stays `false` and `effectiveIV()` returns
the raw IV. Nothing to do.

Level comes from EXP via the growth-rate tables — `Pokemon::getLevelFromExp`, already in the tree.
**Verify Gen 4's growth-rate assignment against `personal_dp`/`personal_pt`/`personal_hgss` in the
generator** rather than assuming it matches the modern table.

---

## 7. Text

Gen 4 stores **16-bit character codes in a game-specific code page**, terminated by `0xFFFF`.
It is *not* UTF-16 — the values go through a lookup table.

```csharp
// StringConverter4.cs
public const ushort Terminator = 0xFFFF;

for (; i < data.Length; i += 2)
{
    var value = ReadUInt16LittleEndian(data[i..]);
    if (value == Terminator) break;
    char chr = (char)ConvertValue2CharG4(value);       // <-- 350-line table
    chr = NormalizeGenderSymbol(chr);
    result[ctr++] = chr;
}
```

The table is `PKM/Strings/StringConverter4Util.cs` (`ConvertValue2CharG4` /
`ConvertChar2ValueG4`), ~350 lines.

**Generate it, do not hand-copy it.** Add a `gen_gen4text.py` that parses
`StringConverter4Util.cs` and emits `include/Utils/Gen4Text.h` with two arrays plus
`gen4ToChar()` / `charToGen4()`. Follow the `Gen1Text.h` / `Gen3Text.h` house rule: **one table,
one header, and route every call site through it.**

Field widths:

```
OT name    16 bytes = 8 UTF-16 units,  MaxStringLengthTrainer  = 7
Nickname   22 bytes = 11 UTF-16 units, MaxStringLengthNickname = 10
Box name   40 bytes = 20 units,        max 8 characters
```

Two encoder details to copy exactly:

```csharp
bool isHalfWidth = language == Korean || !StringConverter.GetIsFullWidthString(value);
if (isHalfWidth) chr = UnNormalizeGenderSymbol(chr);
// …
int count = value.Length * 2;
if (count == destBuffer.Length) return count;     // NO terminator when the field is exactly full
WriteUInt16LittleEndian(destBuffer[count..], Terminator);
```

> **A name that exactly fills its field gets no terminator.** Writing one anyway overruns into the
> next field. This is the Gen 4 equivalent of the Gen 1 terminator-style bug an unedited round trip
> of a real save exposed.

`canStoreNickname()` must return false for any character `ConvertChar2ValueG4` cannot map — the
`0xFFFF` fallback would terminate the string mid-name.

---

## 8. Items

Uniform and simple: **`u16 id, u16 count` per entry**, fixed-size pouches, no count prefix and no
terminator.

```csharp
// InventoryPouch4.cs
Index = ReadUInt16LittleEndian(entry);
Count = ReadUInt16LittleEndian(entry[2..]);
// write: the same, 4 bytes per slot, every slot written
```

Pouch table — offsets are **relative to the bag base**, which is itself relative to `General`:

| Pouch | Offset | Max count |
|---|---|---|
| Items | `0x000` | 999 |
| Key Items | `0x294` | 1 |
| TM/HM | `0x35C` | 99 |
| Mail | DP `0x4EC` · Pt `0x4EC` · HGSS `0x4F0` | 999 |
| Medicine | DP `0x51C` · Pt `0x51C` · HGSS `0x520` | 999 |
| Berries | DP `0x5BC` · Pt `0x5BC` · HGSS `0x5C0` | 999 |
| Balls | DP `0x6BC` · Pt `0x6BC` · HGSS `0x6C0` | 999 |
| Battle Items | DP `0x6F8` · Pt `0x6F8` · HGSS `0x720` | 999 |

```
Bag base:  DP 0x624   Pt 0x630   HGSS 0x644     (offsets into the General block)
```

> **DP and Pt share the same pouch offsets; only the bag base differs.** HGSS shifts four pouches.
> Get this wrong and items appear in the wrong pocket, which the game may then refuse to load.

HMs cap at 1 (`ItemConverter.IsItemHM4`). `itemsAreIdIndexed()` returns **false** — these are
slot-based, and every slot is rewritten on save.

Legal item sets: `ItemStorage4DP` / `ItemStorage4PT` / `ItemStorage4HGSS`.

---

## 9. Data tables

`tools/gen_gen4.py` → `include/Pokemon/Gen4Tables.h` + `src/Pokemon/Gen4Tables.cpp`.

| Table | Source | Notes |
|---|---|---|
| Base stats | `personal_dp` / `personal_pt` / `personal_hgss` | Species 0–493. Check for per-game divergence. |
| Abilities (2 slots) | same | **No hidden ability** in Gen 4 |
| Gender ratio, base friendship | same | |
| Types | same | Gen 4 predates Fairy — convert to PKSE ids **at generation time** |
| Growth rate | same | Verify against `Experience.cpp` before reusing |
| Learnsets | `lvlmove_dp/pt/hgss.pkl` + TM/HM + tutors | Union with the pre-evolution chain |
| Move info (type/category/PP) | `MoveInfo4` | Gen 4 PP differs from modern |
| Item names | PKHeX item strings index 4 | 0–536 |
| Item pouch sets | `ItemStorage4*` | |
| Met location names | PKHeX text `text_dp_*`, `text_pt_*`, `text_hgss_*` | **Three separate namespaces** |
| Character table | `StringConverter4Util.cs` | §7 |

**Location tables are per game and PKSE already knows this** —
`Enums::locationTableVersion(origin, format, isEgg)` exists precisely for the Gen 3/4 remap rule
(manual ch. 07). Registering Gen 4 location names is what makes that function's Gen 4 branch mean
something.

**Ability ids 1–123 are a strict prefix of the modern table**, so `Names::getAbilityName` works
unchanged. **Move ids 1–467 likewise.** **Species 1–493 likewise.** Only **items** and
**locations** need new tables.

---

## 10. Legality

`getEncounterTable(GameVersion)` currently returns `nullptr` for anything pre-Gen 3, which yields
one Info note and no findings — a correct, honest answer. Gen 4 encounter tables are a **separate
project**; `tools/gen_encounters.py` already handles the Gen 8/9 shapes and would need Gen 4 wild
slot binaries added.

Ship Gen 4 with layers 1 and 2 only. Layer 3 says "no encounter data for this origin game", which
is true.

Layer 2 needs:

- `presenceBit()` — Gen 4 has no personal-table presence bit in PKSE's generated table, so gate
  species presence on `species <= 493` instead.
- The ability check works unchanged (ids are a prefix).
- **Nature is PID-derived**, so a nature/PID mismatch is impossible by construction — do not add a
  check for it.

---

## 11. PKSE integration

### 11.1 `GameVersion`

```cpp
enum class GameVersion {
    // … Gen 4 (Nintendo DS). Values match PKHeX and are ALREADY in getOriginGameName().
    HG = 7,  SS = 8,
    D  = 10, P  = 11, Pt = 12,
    // …
    // Three groups, because the three save LAYOUTS are genuinely different (block sizes,
    // storage layout, bag offsets). Same reasoning as SV vs ZA: prefer a clean copy.
    DP   = 79,
    PT   = 80,
    HGSS = 81,
};
```

`getVersionGeneration()` **already returns 4** for `7, 8, 10, 11, 12` — no change needed.
`getOriginGameName()` **already names all five**. Add:

```cpp
getGameGroup:        D/P -> DP;  Pt -> PT;  HG/SS -> HGSS
getGameVersionName:  "Diamond/Pearl", "Platinum", "HeartGold/SoulSilver"
getGroupRepVersion:  DP -> 10 (Diamond); PT -> 12; HGSS -> 7 (HeartGold)
isGameSupported:     nothing to do -- derived from getGameGroup
```

> **Diamond and Pearl are indistinguishable from the save alone**, exactly like Red/Blue and
> Gold/Silver. Name the pair. Same for HeartGold/SoulSilver. Platinum stands alone.

### 11.2 New files

```
include/Utils/CRC16.h                  crc16ccitt (new; Gen 6/7 will add crc16Invert)
include/Utils/Gen4Text.h               generated char table
include/Encryption/Encryption4.h       + src/…/Encryption4.cpp   (Decrypt45/Encrypt45)
include/Pokemon/Pokemon4DPPt.h         + .cpp                    (PK4 — ONE class, all three games)
include/Pokemon/Gen4Tables.h           + .cpp                    (generated)
include/Trainer/Inventory4.h                                     (shared pouch struct + 3 tables)
include/Trainer/Trainer4.h             + .cpp   base: partitions, footers, CRC, block selection
include/Trainer/Trainer4DP.h           + .cpp
include/Trainer/Trainer4PT.h           + .cpp
include/Trainer/Trainer4HGSS.h         + .cpp
tools/gen_gen4.py
tools/gen_gen4text.py
```

> **One entity class, three trainer classes.** The PK4 record is byte-identical across all three
> games — only the `Ball` field's *interpretation* differs, which `originGame()` decides. The
> *saves* are genuinely different, so they get three classes. That split is the honest reading of
> the per-game rule in manual ch. 04.

### 11.3 `Encryption4`

```cpp
// include/Encryption/Encryption4.h
namespace Encryption {
    inline constexpr size_t BLOCK_COUNT4 = 4;
    inline constexpr size_t SIZE_BLOCK4  = 0x20;   // 32
    inline constexpr size_t SIZE_STORED4 = 0x88;   // 136 = 8 + 4*32
    inline constexpr size_t SIZE_PARTY4  = 0xEC;   // 236

    /// Decrypts in place. The body is keyed on the CHECKSUM (offset 0x06), the party tail on the
    /// PID -- unlike Gen 6+, where both use the PID. Shuffle value is still (PID >> 13) & 31.
    void decryptArray4(std::span<std::byte> data);

    /// Encrypts in place. CALL refreshChecksum() FIRST: the checksum is the key, so encrypting
    /// with a stale one produces a record that decrypts to garbage.
    void encryptArray4(std::span<std::byte> data);
}
```

Implementation reuses `cryptArray` and `blockPosition` / `blockPositionInvert` verbatim; only the
block size (32) and the seed choice are new.

### 11.4 `Trainer4` base

> The planned shared base below did not land as a base class: its logic lives, copied, in each of
> `Trainer4DP`, `Trainer4PT` and `Trainer4HGSS`, with the geometry as plain members. The algorithm is unchanged.

```cpp
namespace Trainer {

    /// Which 0x40000 partition a block was loaded from. General and Storage are chosen
    /// INDEPENDENTLY -- it is normal for them to differ.
    struct Gen4Blocks {
        size_t generalOffset = 0;   // absolute offset into the 0x80000 buffer
        size_t storageOffset = 0;
        size_t generalBackup = 0;
        size_t storageBackup = 0;
    };

    class Trainer4 : public Trainer {
    protected:
        std::vector<uint8_t> saveData;      // the whole 0x80000 file
        std::string savePath;
        Gen4Blocks  blockOffsets;

        virtual size_t generalSize()  const noexcept = 0;
        virtual size_t storageSize()  const noexcept = 0;
        virtual size_t storageStart() const noexcept = 0;   // within a partition
        virtual size_t footerSize()   const noexcept = 0;   // 0x14 DP/Pt, 0x10 HGSS

        uint8_t*       general()       noexcept { return &saveData[blockOffsets.generalOffset]; }
        uint8_t*       storage()       noexcept { return &saveData[blockOffsets.storageOffset]; }
        const uint8_t* general() const noexcept { return &saveData[blockOffsets.generalOffset]; }
        const uint8_t* storage() const noexcept { return &saveData[blockOffsets.storageOffset]; }

        void selectBlocks();      // CompareFooters for each block, fills blockOffsets
        void writeChecksums();    // CRC16-CCITT into each block, backups only when initialised

    public:
        size_t getBoxCount()    const noexcept override { return 18; }
        size_t getSlotsPerBox() const noexcept override { return 30; }
        uint32_t getMaxMoney()  const noexcept override { return 999999; }
        size_t getMaxTrainerNameLength() const noexcept override { return 7; }
        int    getMaxTrainerNameDigits() const noexcept override { return 4; }  // gens 4-5: four
        bool   supportsBoxNames() const noexcept override { return true; }
        size_t getMaxBoxNameLength() const noexcept override { return 8; }
        bool   itemsAreIdIndexed() const noexcept override { return false; }
        bool   compactStorage() override { return false; }   // positional storage -- MUST stay false
    };
}
```

`selectBlocks`, the piece that matters most:

```cpp
namespace {
    constexpr int kFirst = 0, kSecond = 1, kSame = 2;

    /// PKHeX SAV4BlockDetection.CompareCounters. The 0xFFFFFFFF handling is load-bearing: a fresh
    /// save has one partition never written, and its counter reads LARGER than any real one.
    int compareCounters(uint32_t c1, uint32_t c2) noexcept {
        if (c1 == 0xFFFFFFFFu && c2 != 0xFFFFFFFEu) return kSecond;
        if (c2 == 0xFFFFFFFFu && c1 != 0xFFFFFFFEu) return kFirst;
        if (c1 > c2) return kFirst;
        if (c1 < c2) return kSecond;
        return kSame;
    }

    int compareFooters(const uint8_t* d, size_t o1, size_t o2) noexcept {
        const int major = compareCounters(Utils::readUInt32LittleEndian(d + o1),
                                          Utils::readUInt32LittleEndian(d + o2));
        if (major != kSame) return major;
        const int minor = compareCounters(Utils::readUInt32LittleEndian(d + o1 + 4),
                                          Utils::readUInt32LittleEndian(d + o2 + 4));
        return minor == kSecond ? kSecond : kFirst;   // Same -> First
    }
}

void Trainer4::selectBlocks() {
    constexpr size_t kPartition = 0x40000;

    // Counters sit 0x14 from the block end in EVERY Gen 4 game, including HGSS whose CRC
    // footerSize is 0x10. The two constants answer different questions -- do not unify them.
    const size_t gFoot = 0 + generalSize() - 0x14;
    const size_t sFoot = storageStart() + storageSize() - 0x14;

    const int gPos = compareFooters(saveData.data(), gFoot, gFoot + kPartition);
    const int sPos = compareFooters(saveData.data(), sFoot, sFoot + kPartition);

    blockOffsets.generalOffset = (gPos == 0 ? 0 : kPartition);
    blockOffsets.storageOffset = (sPos == 0 ? 0 : kPartition) + storageStart();
    blockOffsets.generalBackup = (gPos != 0 ? 0 : kPartition);
    blockOffsets.storageBackup = (sPos != 0 ? 0 : kPartition) + storageStart();
}
```

Checksums:

```cpp
void Trainer4::writeChecksums() {
    auto stamp = [&](size_t ofs, size_t len) {
        const uint16_t crc = Utils::crc16ccitt(&saveData[ofs], len - footerSize());
        Utils::writeUInt16LittleEndian(&saveData[ofs + len - 2], crc);
    };
    auto initialised = [&](size_t ofs, size_t len) {
        return Utils::readUInt32LittleEndian(&saveData[ofs + len - 8]) != 0xFFFFFFFFu;
    };

    stamp(blockOffsets.generalOffset, generalSize());
    stamp(blockOffsets.storageOffset, storageSize());
    // Only refresh a backup block that has ever been written -- stamping a CRC into uninitialised
    // 0xFF space makes a garbage partition look valid to the game.
    if (initialised(blockOffsets.generalBackup, generalSize())) stamp(blockOffsets.generalBackup, generalSize());
    if (initialised(blockOffsets.storageBackup, storageSize())) stamp(blockOffsets.storageBackup, storageSize());
}
```

### 11.5 Save read/write

Loose-file I/O is shared by every pre-Switch format: `Save::openExternalSave` runs the
`LOOSE_FORMATS` probe chain and `Save::saveExternalSave` dispatches the write on `getGameGroup()`
with a `static_cast` — adding a format is one row and one `case`. `Save::backupOriginalFile` copies
the **original bytes** to `sdmc:/PKSE/FileBackups` **before** writing. These classes are **not** in
`TrainerVariant`: no title ID, no title-ID dispatch.

### 11.6 Entry point, UI, bank

- `LOOSE_FORMATS`: `Trainer4DP/PT/HGSS::detect` in the probe chain
  after Gen 1 and Gen 2.
- Box grid: **18 × 30 → `boxGridColumns(30) == 6`**. No UI change at all.
- `Names::getItemNameFor()`: add `DP` / `PT` / `HGSS` branches (all three share one Gen 4 item table;
  only the max id differs).
- `PickerKind`: `ItemG4`, `PouchItemG4`.
- Ball picker: Gen 4 max ball id is `0x18` (24), below PKSE's 38 — the existing picker works, but
  the legality check should cap at 24.
- `Bank.cpp`: `BTAG_G4` — **one tag per ENTITY format, not per game.** Every PK4 is stored
  under it and reads back as the format's representative group (`groupForBankTag`). Never compare
  that group to the entity's own; the invariant is `tag == bankTagForEntity(entity)`. Add-only.
- **Conversion: Gen 4 is not in `inFamily`.** PK4 → PK8 is a real project (Gen 5 → 6 is the
  format break, and HOME's transfer rules are involved). Gen 4 is banked, not converted.

---

### The Pokedex needs NO index table

`Trainer::updatePokedexBlock()` is a no-op for this generation today, and implementing it needs no
generated lookup: PKHeX's `Zukan.cs` puts a species' seen/caught bit at **`species - 1`**
(`GetCaught(species) => GetFlag(OFS_CAUGHT, species - 1)`), so the index is arithmetic. Gen 1, Gen 3
and BDSP already write their dexes this way and carry no table either.

Only games whose dex numbering is ARBITRARY need one — Sword/Shield, Scarlet/Violet and Legends:
Arceus, whose tables are generated, and X/Y through Ultra Sun/Moon, whose FORM region is (see
`tools/gen_dexformtables.py`). If you find yourself wanting a lookup for `species - 1`, you do not
need this file.

## 12. Verification

```bash
python tools/syntax_check.py && python tools/syntax_check.py --warnings
```

**The gate is an unedited round trip of a real save from each of the three games**: open it, write
it straight back, and the file must be byte-identical.

Gen-4-specific things the round trip will catch:

- **Wrong partition selected** — the diff will be enormous, which is at least obvious.
- **CRC written with the wrong `FooterSize`** — HGSS is `0x10`, DP/Pt `0x14`.
- **Backup block CRC stamped into uninitialised space.**
- **HGSS box padding** — 16 bytes of slack per box, silently zeroed.
- **`currentBox` written to offset 0 in HGSS** (it is at `BOX_END`).
- **Terminator written into an exactly-full name field.**
- **Ball written to `0x83` only**, losing an HGSS Apricorn ball.
- **Checksum refreshed after encryption** instead of before.
- **`IV32` rebuilt from six IVs**, clearing the egg and nicknamed bits.

Then the entity cross-check: a PK4 **party** record stores its six battle stats, so recompute and
compare. That proves tables + crypt + checksum + IV unpack + formula in one shot.

---

## 13. Trap list

1. **Two partitions; General and Storage are chosen independently.**
2. **`0xFFFFFFFF` counters mean "never written", not "newest".**
3. **The counter offset is `0x14` from the block end in all three games**; `FooterSize` is `0x14`
   (DP/Pt) or `0x10` (HGSS) and is a *different* constant.
4. **Only stamp a backup block's CRC if its magic is not `0xFFFFFFFF`.**
5. **The entity crypt seed is the CHECKSUM, not the PID** — and the party tail uses the PID.
6. **Refresh the checksum BEFORE encrypting.**
7. **`IV32` carries the egg and nicknamed flags.** Read-modify-write.
8. **Two ball fields** (`0x83` DP/Pt, `0x86` HGSS).
9. **Nature is `PID % 25`** — changing it means rebuilding the PID, like Gen 3.
10. **HGSS storage differs three ways** from DP/Pt: origin, padding, `currentBox` position.
11. **HGSS shifts four bag pouches**; DP and Pt share pouch offsets but not the bag base.
12. **A name that exactly fills its field gets no terminator.**
13. **Gen 4 text is a code page, not UTF-16.** Gen 5 *is* UTF-16 — do not share the converter.
14. **`0x80000` is also Gen 5's size.** Detect on the footer size field + SDK magic (§2.6), not on length.
15. **Diamond/Pearl and HeartGold/SoulSilver are pairs.** Name the pair.
16. **Met-location namespaces differ per game** — DP, Pt and HGSS each have their own.
17. **`compactStorage()` must stay `false`.** Gen 4 storage is positional.
18. **Preserve the box-changed flag bitfield** and the wallpaper bytes.

---

## 14. Suggested order

1. `CRC16.h` + `Encryption4` + a standalone test that decrypts a known record.
2. Generated tables (`gen_personaltables.py`, `gen_gen4text.py`); check them against PKHeX.
3. The PK4 entity — offsets, then every setter read back.
4. `Trainer4` base — **partition selection and CRC first**, before any field parsing. Prove it by
   loading and writing back with zero diff.
5. `Trainer4DP`, then `Pt`, then `HGSS`.
6. Items, box names, trainer fields.
7. **Round trip until clean, all three games.**
8. Entry point (the `LOOSE_FORMATS` row — this is what exposes it), UI, bank, docs.

Step 4 is the one that is unlike anything already in PKSE. Everything after it is familiar work.
