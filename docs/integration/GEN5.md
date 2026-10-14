# Generation 5 — Black / White / Black 2 / White 2 (PK5, SAV5)

> **Status: implemented.** This is the PKHeX transcription Gen 5 was built from, kept as the
> reference for that code. It was written as a plan, so where §9 and §12 name files, the tree
> differs in one systematic way: **every layer has one class per group, with no shared base** —
> `Trainer5BW` / `Trainer5B2W2`, `Pokemon5BW` / `Pokemon5B2W2`, `Encryption5BW` / `5B2W2`,
> `Inventory5BW` / `5B2W2` — rather than the `Trainer5` base, single PK5 class and `Inventory5`
> sketched below. `GEN5_FILE_SIZE` is a `static constexpr` class member. The block tables
> (`Blocks5BW.h`, `Blocks5B2W2.h`) come from `tools/gen_blocktables.py`, and each group's named
> block indices are derived from its own table by block name — the two orders diverge at block 32.
> Personal data comes from `tools/gen_personaltables.py`, not a `gen_gen5.py`; text goes through
> `Utils/Utf16Text.h`. **The save names its own game** (`PlayerData5 +0x1F`, read by
> `getGameVersion()`), so Black and White are told apart rather than named as a pair. The bank stores
> every PK5 under one tag, `BTAG_G5`. See [manual ch. 04](../manual/04-generation-pattern.md) and
> [ch. 05](../manual/05-save-pipeline.md).

> **Source of truth: PKHeX.** Every offset below was transcribed at commit `6501f0ab46e8f8ca048539dbaf8cae8cb104e722`;
> PKSE now tracks PKHeX's default branch, so re-verify against the file you fetch.
> Files: `Saves/SAV5.cs`, `Saves/SAV5BW.cs`, `Saves/SAV5B2W2.cs`,
> `Saves/Access/SaveBlockAccessor5BW.cs`, `Saves/Access/SaveBlockAccessor5B2W2.cs`,
> `Saves/Blocks/BlockInfo.cs`, `Saves/Blocks/BlockInfoNDS.cs`,
> `Saves/Substructures/Gen5/BoxLayout5.cs` / `PlayerData5.cs` / `Misc5.cs` / `MyItem5BW.cs` /
> `Daycare5.cs`, `PKM/PK5.cs`, `PKM/Util/PokeCrypto.cs`, `PKM/Strings/StringConverter5.cs`,
> `Saves/Util/Checksums.cs`, `Saves/Util/SaveUtil.cs`, `Items/Bags/PlayerBag5BW.cs`,
> `Saves/Substructures/Inventory/Pouch/InventoryPouch4.cs`, `Legality/Legal.cs`.

---

## 0. Read this first

**Do Gen 4 first.** Gen 5 shares its entity crypto (`Decrypt45`/`Encrypt45`), its 32-byte block
shuffle, its stat formula, its `IV32` packing, and most of its PK offset map. Having Gen 4 in the
tree turns Gen 5 into "a different save container and one changed field".

And the save container is **much simpler than Gen 4's**:

| | Gen 4 | Gen 5 |
|---|---|---|
| Partitions | 2, chosen independently per block | **1** |
| Block selection | counter comparison | **none — there is one copy** |
| Checksum | CRC16-CCITT per block | CRC16-CCITT per block, **written twice** |
| Blocks | 2 (+ extras) | **70 (BW) / 74 (B2W2)**, but PKHeX gives you the whole table |
| Text | game code page | **plain UTF-16** |

> **Gen 5 is the easiest generation in this set to add**, provided Gen 4 exists first.

Prerequisites: [GEN4.md](GEN4.md) (read it — this document refers back to it constantly), plus
manual [04](../manual/04-generation-pattern.md), [05](../manual/05-save-pipeline.md),
[06](../manual/06-pokemon-entity.md), [13](../manual/13-adding-a-game.md).

---

## 1. Scope

| | |
|---|---|
| Games | Black, White (`BW`), Black 2, White 2 (`B2W2`) |
| Groups | `GameVersion::BW`, `GameVersion::B2W2` — two save layouts, two groups |
| Entity | **PK5** |
| Save file | loose `.sav`, **`0x80000` = 512 KiB** (`SIZE_G5RAW`) — same as Gen 4 |
| Main save region | BW `0x24000`, B2W2 `0x26000` |
| Entity encryption | **identical to Gen 4** (`Decrypt45` / `Encrypt45`) |
| Entity checksum | 16-bit `Add16` over `data[0x08 .. 0x88)` |
| Save checksum | CRC16-CCITT per block, stored at **two** offsets |
| Endianness | little endian |
| Boxes | **24 × 30** |
| Max species | 649 |
| Max move id | 559 |
| Max item id | BW 632, B2W2 638 |
| Max ability id | 164 |
| Max ball id | `0x19` (25) |
| OT name | 7 chars, 16-byte field |
| Nickname | 10 chars, 22-byte field |
| Box name | **13 chars**, 0x14-byte field |

---

## 2. The save container

### 2.1 One flat file, many blocks

No partitions, no counters, no backup halves. A Gen 5 save is a list of **fixed blocks**, each with
a length and **two** checksum offsets:

```csharp
// BlockInfoNDS.cs
public BlockInfoNDS(int offset, int length, int chkOffset, int chkMirror);

private ushort GetChecksum(ReadOnlySpan<byte> data) => Checksums.CRC16_CCITT(data.Slice(Offset, Length));

protected override bool ChecksumValid(ReadOnlySpan<byte> data)
{
    ushort chk = GetChecksum(data);
    if (chk != ReadUInt16LittleEndian(data[ChecksumOffset..])) return false;
    if (chk != ReadUInt16LittleEndian(data[ChecksumMirror..])) return false;
    return true;
}

protected override void SetChecksum(Span<byte> data)
{
    ushort chk = GetChecksum(data);
    WriteUInt16LittleEndian(data[ChecksumOffset..], chk);
    WriteUInt16LittleEndian(data[ChecksumMirror..], chk);
}
```

`ChecksumOffset` sits just after the block's data; `ChecksumMirror` sits in the trailing **checksum
block** (BW block 69 at `0x23F00`, B2W2 block 73 at `0x25F00`). **Both must be written.**

Saving is therefore:

```csharp
protected override void SetChecksums() => AllBlocks.SetChecksums(Data);   // every block, both copies
```

No counters to bump, no partition to flip. Round-tripping an unedited save is trivially byte-exact.

### 2.2 The BW block table (70 blocks)

`(offset, length, checksumOffset, checksumMirror)` — verbatim from `SaveBlockAccessor5BW.cs`.

```
 #  offset   length   chkOfs   chkMirror   what
 00 0x00000  0x03E0   0x003E2  0x23F00     Box Names
 01 0x00400  0x0FF0   0x013F2  0x23F02     Box 1
 02 0x01400  0x0FF0   0x023F2  0x23F04     Box 2
 03 0x02400  0x0FF0   0x033F2  0x23F06     Box 3
 04 0x03400  0x0FF0   0x043F2  0x23F08     Box 4
 05 0x04400  0x0FF0   0x053F2  0x23F0A     Box 5
 06 0x05400  0x0FF0   0x063F2  0x23F0C     Box 6
 07 0x06400  0x0FF0   0x073F2  0x23F0E     Box 7
 08 0x07400  0x0FF0   0x083F2  0x23F10     Box 8
 09 0x08400  0x0FF0   0x093F2  0x23F12     Box 9
 10 0x09400  0x0FF0   0x0A3F2  0x23F14     Box 10
 11 0x0A400  0x0FF0   0x0B3F2  0x23F16     Box 11
 12 0x0B400  0x0FF0   0x0C3F2  0x23F18     Box 12
 13 0x0C400  0x0FF0   0x0D3F2  0x23F1A     Box 13
 14 0x0D400  0x0FF0   0x0E3F2  0x23F1C     Box 14
 15 0x0E400  0x0FF0   0x0F3F2  0x23F1E     Box 15
 16 0x0F400  0x0FF0   0x103F2  0x23F20     Box 16
 17 0x10400  0x0FF0   0x113F2  0x23F22     Box 17
 18 0x11400  0x0FF0   0x123F2  0x23F24     Box 18
 19 0x12400  0x0FF0   0x133F2  0x23F26     Box 19
 20 0x13400  0x0FF0   0x143F2  0x23F28     Box 20
 21 0x14400  0x0FF0   0x153F2  0x23F2A     Box 21
 22 0x15400  0x0FF0   0x163F2  0x23F2C     Box 22
 23 0x16400  0x0FF0   0x173F2  0x23F2E     Box 23
 24 0x17400  0x0FF0   0x183F2  0x23F30     Box 24
 25 0x18400  0x09C0   0x18DC2  0x23F32     Inventory
 26 0x18E00  0x0534   0x19336  0x23F34     Party Pokemon
 27 0x19400  0x0068   0x1946A  0x23F36     Trainer Data
 28 0x19500  0x009C   0x1959E  0x23F38     Trainer Position
 29 0x19600  0x1338   0x1A93A  0x23F3A     Unity Tower + survey
 30 0x1AA00  0x07C4   0x1B1C6  0x23F3C     Pal Pad Player Data
 31 0x1B200  0x0D54   0x1BF56  0x23F3E     Pal Pad Friend Data
 32 0x1C000  0x002C   0x1C02E  0x23F40     Skin Info
 33 0x1C100  0x0658   0x1C75A  0x23F42     Gym badge data
 34 0x1C800  0x0A94   0x1D296  0x23F44     Mystery Gift
 35 0x1D300  0x01AC   0x1D4AE  0x23F46     Dream World Catalog
 36 0x1D500  0x03EC   0x1D8EE  0x23F48     Chatter
 37 0x1D900  0x005C   0x1D95E  0x23F4A     Adventure Info
 38 0x1DA00  0x01E0   0x1DBE2  0x23F4C     Trainer Card Records
 39 0x1DC00  0x00A8   0x1DCAA  0x23F4E     ???
 40 0x1DD00  0x0460   0x1E162  0x23F50     Mail
 41 0x1E200  0x1400   0x1F602  0x23F52     Overworld State
 42 0x1F700  0x02A4   0x1F9A6  0x23F54     Musical
 43 0x1FA00  0x02DC   0x1FCDE  0x23F56     White Forest + Black City
 44 0x1FD00  0x034C   0x2004E  0x23F58     IR
 45 0x20100  0x03EC   0x204EE  0x23F5A     EventWork
 46 0x20500  0x00F8   0x205FA  0x23F5C     GTS
 47 0x20600  0x02FC   0x208FE  0x23F5E     Regulation Tournament
 48 0x20900  0x0094   0x20996  0x23F60     Gimmick
 49 0x20A00  0x035C   0x20D5E  0x23F62     Battle Box
 50 0x20E00  0x01CC   0x20FCE  0x23F64     Daycare
 51 0x21000  0x0168   0x2116A  0x23F66     Strength Boulder Status
 52 0x21200  0x00EC   0x212EE  0x23F68     Misc: Badge Flags, MONEY, Trainer Sayings
 53 0x21300  0x01B0   0x214B2  0x23F6A     Entralink
 54 0x21500  0x001C   0x2151E  0x23F6C     ???
 55 0x21600  0x04D4   0x21AD6  0x23F6E     Pokedex
 56 0x21B00  0x0034   0x21B36  0x23F70     Encount (swarm, repel)
 57 0x21C00  0x003C   0x21C3E  0x23F72     Battle Subway Play Info
 58 0x21D00  0x01AC   0x21EAE  0x23F74     Battle Subway Score Info
 59 0x21F00  0x0B90   0x22A92  0x23F76     Battle Subway Wi-Fi Info
 60 0x22B00  0x009C   0x22B9E  0x23F78     Online Records
 61 0x22C00  0x0850   0x23452  0x23F7A     Entralink Forest Pokemon
 62 0x23500  0x0028   0x2352A  0x23F7C     ???
 63 0x23600  0x0284   0x23886  0x23F7E     Answered Questions
 64 0x23900  0x0010   0x23912  0x23F80     Unity Tower
 65 0x23A00  0x005C   0x23A5E  0x23F82     Battle Institute
 66 0x23B00  0x016C   0x23C6E  0x23F84     ???
 67 0x23D00  0x0040   0x23D42  0x23F86     ???
 68 0x23E00  0x00FC   0x23EFE  0x23F88     ???
 69 0x23F00  0x008C   0x23F9A  0x23F9A     CHECKSUM BLOCK (mirror == own offset)
```

> **Block 69 checksums itself**: `chkOffset == chkMirror == 0x23F9A`. Write it last, after every
> other block's mirror has landed inside it — otherwise you checksum stale mirror bytes.

### 2.3 The B2W2 block table (74 blocks)

Blocks 0–24 (box names + 24 boxes) are **identical** to BW. From block 25 the offsets diverge, the
mirrors move to `0x25F00+`, and four blocks are added.

```
 #  offset   length   chkOfs   chkMirror   what                        (delta vs BW)
 25 0x18400  0x09EC   0x18DEE  0x25F32     Inventory                   length +0x2C
 26 0x18E00  0x0534   0x19336  0x25F34     Party Pokemon               same
 27 0x19400  0x00B0   0x194B2  0x25F36     Trainer Data                length +0x48
 28 0x19500  0x00A8   0x195AA  0x25F38     Trainer Position
 29 0x19600  0x1338   0x1A93A  0x25F3A     Unity Tower + survey
 30 0x1AA00  0x07C4   0x1B1C6  0x25F3C     Pal Pad Player Data
 31 0x1B200  0x0D54   0x1BF56  0x25F3E     Pal Pad Friend Data
 32 0x1C000  0x0094   0x1C096  0x25F40     Options / Skin Info         length +0x68
 33 0x1C100  0x0658   0x1C75A  0x25F42     Trainer Card
 34 0x1C800  0x0A94   0x1D296  0x25F44     Mystery Gift
 35 0x1D300  0x01AC   0x1D4AE  0x25F46     Dream World Catalog
 36 0x1D500  0x03EC   0x1D8EE  0x25F48     Chatter
 37 0x1D900  0x005C   0x1D95E  0x25F4A     Adventure data
 38 0x1DA00  0x01E0   0x1DBE2  0x25F4C     Trainer Card Records
 39 0x1DC00  0x00A8   0x1DCAA  0x25F4E     ???
 40 0x1DD00  0x0460   0x1E162  0x25F50     Mail
 41 0x1E200  0x1400   0x1F602  0x25F52     Overworld State
 42 0x1F700  0x02A4   0x1F9A6  0x25F54     Musical
 43 0x1FA00  0x00E0   0x1FAE2  0x25F56     White Forest/Black City + Fused legend
 44 0x1FB00  0x034C   0x1FE4E  0x25F58     IR
 45 0x1FF00  0x04E0   0x203E2  0x25F5A     EventWork
 46 0x20400  0x00F8   0x204FA  0x25F5C     GTS
 47 0x20500  0x02FC   0x207FE  0x25F5E     Regulation Tournament
 48 0x20800  0x0094   0x20896  0x25F60     Gimmick
 49 0x20900  0x035C   0x20C5E  0x25F62     Battle Box
 50 0x20D00  0x01D4   0x20ED6  0x25F64     Daycare
 51 0x20F00  0x01E0   0x210E2  0x25F66     Strength Boulder Status
 52 0x21100  0x00F0   0x211F2  0x25F68     Misc: Badge Flags, MONEY, Trainer Sayings
 53 0x21200  0x01B4   0x213B6  0x25F6A     Entralink
 54 0x21400  0x04DC   0x218DE  0x25F6C     Pokedex
 55 0x21900  0x0034   0x21936  0x25F6E     Encount
 56 0x21A00  0x003C   0x21A3E  0x25F70     Battle Subway Play Info
 57 0x21B00  0x01AC   0x21CAE  0x25F72     Battle Subway Score Info
 58 0x21D00  0x0B90   0x22892  0x25F74     Battle Subway Wi-Fi Info
 59 0x22900  0x00AC   0x229AE  0x25F76     Online Records
 60 0x22A00  0x0850   0x23252  0x25F78     Entralink Forest Pokemon
 …  (blocks 61-70 continue; see SaveBlockAccessor5B2W2.cs for the full list)
 71 0x25A00  0x03E4   0x25DE6  0x25F8E     Pokestar Studios
 72 0x25E00  0x00F0   0x25EF2  0x25F90     ???
 73 0x25F00  0x0094   0x25FA2  0x25FA2     CHECKSUM BLOCK
```

> **Take the full B2W2 table straight out of `SaveBlockAccessor5B2W2.cs` rather than retyping it.**
> Better still: **generate both tables.** Add a `tools/gen_blocktables.py` that parses the two
> accessor files and emits one `Blocks<Gen><Code>.h` per group, each with its own `constexpr` array. Seventy-four hand-copied
> 4-tuples is seventy-four chances for a typo that a CRC will happily hide, because a wrong
> `(offset, length)` still produces *a* valid CRC over *some* range.

### 2.4 Detection

```csharp
private static bool IsG5BW  (data) => data.Length == SIZE_G5RAW && IsValidFooter5(data, SIZE_G5BW,   0x8C);
private static bool IsG5B2W2(data) => data.Length == SIZE_G5RAW && IsValidFooter5(data, SIZE_G5B2W2, 0x94);

private static bool IsValidFooter5(ReadOnlySpan<byte> data, int mainSize, int infoLength)
{
    var footer = data.Slice(mainSize - 0x100, infoLength + 0x10);
    var stored = ReadUInt16LittleEndian(footer[^2..]);
    var actual = Checksums.CRC16_CCITT(footer[..infoLength]);
    return stored == actual;
}
```

with `SIZE_G5BW = 0x24000` and `SIZE_G5B2W2 = 0x26000`. So the checksum block starts at
`mainSize - 0x100` (`0x23F00` / `0x25F00`) — matching the block tables above — and the probe
validates that block's self-checksum.

```cpp
/// True when `bytes` is a Gen 5 save of the given flavour. Mirrors PKHeX SaveUtil.IsValidFooter5:
/// the trailing CHECKSUM BLOCK checksums itself, so validating it identifies the layout.
///
/// mainSize: 0x24000 (BW) or 0x26000 (B2W2);  infoLength: 0x8C (BW) or 0x94 (B2W2)
static bool detectGen5(const std::vector<uint8_t>& b, size_t mainSize, size_t infoLength) noexcept {
    if (b.size() != 0x80000) return false;
    const size_t base = mainSize - 0x100;
    if (base + infoLength + 0x10 > b.size()) return false;
    const uint16_t stored = Utils::readUInt16LittleEndian(&b[base + infoLength + 0x10 - 2]);
    return Utils::crc16ccitt(&b[base], infoLength) == stored;
}
```

> **Gen 4 and Gen 5 saves are both `0x80000` bytes.** Run all five probes — DP, Pt, HGSS, BW,
> B2W2 — and require **exactly one** to match. If two match, refuse the file rather than guess.

---

## 3. Storage layout

**24 boxes × 30 slots.** Box data lives in the top of the file, one box per `0x1000` region:

```csharp
// SAV5.cs
private void Initialize()
{
    Box   = 0x400;
    Party = 0x18E00;
}

public override int GetBoxOffset(int box) => Box + (SIZE_STORED * box * 30) + (box * 0x10);
//                                        = 0x400 + box * (30*136 + 16)
//                                        = 0x400 + box * 0x1000

public override int GetPartyOffset(int slot) => Party + 8 + (SIZE_PARTY * slot);
public override int PartyCount { get => Data[Party + 4]; set => Data[Party + 4] = (byte)value; }
```

So box *N* starts at `0x400 + N*0x1000`, holds 30 × 136 = `0xFF0` bytes of records, then **16 bytes
of slack** — which is why each box block's `Length` is `0x0FF0` while the stride is `0x1000`.

> **Do not zero the 16-byte tail of a box.** It is outside the block's CRC range, so corrupting it
> will not fail a checksum — it will just show up as a round-trip diff.

Party: `PartyCount` at `Party + 4`, records from `Party + 8`, `SIZE_5PARTY` = 220 each.

### 3.1 Box names, wallpapers, current box — block 0 (`0x00000`, `0x03E0`)

```csharp
// BoxLayout5.cs — offsets are RELATIVE TO BLOCK 0
public int CurrentBox { get => Data[0]; set => Data[0] = (byte)value; }
private static int GetBoxNameOffset(int box)      => (0x28 * box) + 4;
private static int GetBoxWallpaperOffset(int box) => 0x3C4 + box;
public byte BoxesUnlocked { get => Data[0x3DD]; set => …; }   // clamped to BoxCount

private Span<byte> GetBoxNameSpan(int box) => Data.Slice(GetBoxNameOffset(box), 0x14);
public void SetBoxName(int box, ReadOnlySpan<char> value)
    => SAV.SetString(GetBoxNameSpan(box), value, 13, StringConverterOption.ClearZero);
```

| Field | Offset in block 0 |
|---|---|
| Current box | `0x000` |
| Box names | `0x004 + 0x28*box`, **`0x14` bytes read** (13 chars max) |
| Box wallpapers | `0x3C4 + box` |
| Boxes unlocked | `0x3DD` |

> **The name stride is `0x28` (40) but only `0x14` (20) bytes are used.** The other 20 bytes per
> entry belong to something else. Slice `0x14`, not `0x28`, and leave the rest alone.
>
> **Box names are 13 characters, not 8.** Gen 5 is the outlier; every other generation in this set
> caps at 8. `getMaxBoxNameLength()` must return 13.

---

## 4. Trainer data

### 4.1 Player data — BW block 27 (`0x19400`, `0x0068`) · B2W2 block 27 (`0x19400`, `0x00B0`)

Offsets relative to the block:

```
+0x04   16   OT name (UTF-16, 7 chars max)
+0x14    4   ID32   (TID16 at +0x14, SID16 at +0x16)
+0x1E    1   Language
+0x1F    1   Game (version byte)
+0x21    1   Gender
+0x24    2   PlayedHours
+0x26    1   PlayedMinutes      (see PlayerData5.cs)
+0x27    1   PlayedSeconds
+0x30..+0x3E  "magic" 0x21E flags: battle videos, C-Gear skin, musical, Pokedex skin
```

`GameSyncID`, `Country`, `Region` and the `LastSaved*` fields also live here — see
`PlayerData5.cs` for their exact offsets; PKSE does not need them, so **preserve and ignore**.

### 4.2 Money — BW block 52 (`0x21200`) · B2W2 block 52 (`0x21100`)

```csharp
// Misc5.cs
public uint Money { get => ReadUInt32LittleEndian(Data); set => WriteUInt32LittleEndian(Data, value); }
```

**Money is at offset 0 of the Misc block.** The rest of that block is badge flags and trainer
sayings.

### 4.3 Daycare — BW block 50 (`0x20E00`) · B2W2 block 50 (`0x20D00`)

Two slots of `SIZE_5STORED`, plus an EXP value each and a `ulong` seed. Out of PKSE's scope —
preserve.

---

## 5. The PK5 entity

### 5.1 Sizes and crypto

```
SIZE_5STORED = 136  (0x88)    -- identical to Gen 4
SIZE_5PARTY  = 220  (0xDC)    -- Gen 4 is 236 (0xEC)
```

**Encryption is `Decrypt45` / `Encrypt45` — literally the same function as Gen 4.** Body keyed on
the **checksum** at `0x06`, party tail keyed on the PID, shuffle value `(PID >> 13) & 31`, 32-byte
blocks.

```csharp
// PokeCrypto.cs — note SIZE_5PARTY in the assert; everything else is the Gen 4 path
Debug.Assert(data.Length is SIZE_4STORED or SIZE_4PARTY or SIZE_5PARTY);
```

So `Encryption4::decryptArray4` / `encryptArray4` from [GEN4.md](GEN4.md) §11.3 work unchanged —
just pass the 220-byte length. **Do not write a second copy.** Name the functions
`decryptArray45` / `encryptArray45` when you build them for Gen 4, so Gen 5 reads naturally.

Same checksum:

```csharp
private ushort CalculateChecksum() => Checksums.Add16(Data[8..PokeCrypto.SIZE_5STORED]);
public override bool Valid { get => Sanity == 0 && ChecksumValid; … }
```

**Refresh the checksum before encrypting** — same rule, same reason (it is the key).

### 5.2 PK5 offset map

Identical to PK4 except where marked **▲**.

| Off | Size | Field | Notes |
|---|---|---|---|
| `0x00` | 4 | PID | |
| `0x04` | 2 | Sanity | must be 0 |
| `0x06` | 2 | Checksum | the body crypt key |
| `0x08` | 2 | Species | |
| `0x0A` | 2 | Held item | Gen 5 item id space |
| `0x0C` | 2 | TID16 | |
| `0x0E` | 2 | SID16 | |
| `0x10` | 4 | EXP | |
| `0x14` | 1 | OT friendship | |
| `0x15` | 1 | Ability | id, 1–164 |
| `0x16` | 1 | Markings | |
| `0x17` | 1 | Language | |
| `0x18`–`0x1D` | 6 | EVs (HP, ATK, DEF, SPE, SPA, SPD) | |
| `0x1E`–`0x23` | 6 | Contest stats | |
| `0x24`–`0x27` | 4 | Ribbons (Sinnoh 1–4) | |
| `0x28`–`0x2E` | 8 | Moves 1–4 (`u16` each) | |
| `0x30`–`0x33` | 4 | Move PP | |
| `0x34`–`0x37` | 4 | Move PP Ups | |
| `0x38` | 4 | IV32 | same packing as Gen 4 |
| `0x3C`–`0x3F` | 4 | Ribbons (Hoenn) | |
| `0x40` | 1 | Fateful (bit 0) / Gender (bits 1–2) / Form (bits 3+) | |
| `0x41` | 1 | **▲ NATURE — STORED** | Gen 4 derives it from `PID % 25` |
| `0x42` | 1 | **▲ bit 0 HiddenAbility, bit 1 NSparkle** | Gen 5 only |
| `0x43`–`0x47` | 5 | *unused* | |
| `0x48` | 22 | Nickname | 11 UTF-16 units |
| `0x5E` | 1 | *unused* | |
| `0x5F` | 1 | Version (origin game) | |
| `0x60`–`0x63` | 4 | Ribbons (Sinnoh 3–6) | |
| `0x68` | 16 | OT name | 8 UTF-16 units |
| `0x78`–`0x7A` | 3 | Egg year / month / day | |
| `0x7B`–`0x7D` | 3 | Met year / month / day | |
| `0x7E` | 2 | Egg location | **▲ single field**, no DP/Pt-vs-HGSS split |
| `0x80` | 2 | Met location | **▲ single field** |
| `0x82` | 1 | Pokérus | |
| `0x83` | 1 | **▲ Ball — ONE field** | Gen 4 has two |
| `0x84` | 1 | Met level (low 7) + OT gender (bit 7) | |
| `0x85` | 1 | Ground tile type | |
| `0x86` | 1 | *unused* | |
| `0x87` | 1 | **▲ Pokéstar Fame** | Gen 4 has WalkingMood |
| — | | *— end of `SIZE_5STORED` (`0x88`) —* | |
| `0x88` | 4 | Status condition | |
| `0x8C` | 1 | Level | |
| `0x8D` | 1 | Junk byte | |
| `0x8E`–`0x9B` | 14 | HP cur/max, ATK, DEF, SPE, SPA, SPD | |
| `0x9C` | 56 | Held mail | |
| `0xD4` | 8 | Junk data | |
| — | | *— end of `SIZE_5PARTY` (`0xDC`) —* | |

`IV32` packing at `0x38` is **exactly** Gen 4's — including the egg bit at 30 and the nicknamed bit
at 31. Same read-modify-write rule.

### 5.3 The three real differences

**1. Nature is stored** (`0x41`).

```cpp
uint8_t nature() const noexcept override { return rd8(0x41); }
void setNature(uint8_t n) noexcept override {
    // Gen 5 STORES the nature. Unlike Gen 3/4, changing it does NOT require rebuilding the PID --
    // so no destructive-PID warning, and no regeneratePID() call.
    wr8(0x41, n < 25 ? n : 0);
    recalculateStats();
    refreshChecksum();
}
```

This is a genuine improvement over Gen 4 for the editor: `hasNature()` is true *and* the setter is a
plain write.

**2. Hidden ability is a flag, not a slot** (`0x42` bit 0).

Gen 5 introduced Dream World abilities. `abilityNumber()` cannot be derived from the PID here:

```cpp
uint8_t abilityNumber() const noexcept override {
    if (rd8(0x42) & 1) return 4;              // hidden
    // otherwise slot 1 or 2, matched against the personal table by the ability ID at 0x15
    const auto slots = Pokemon::getAbilitySlots(speciesID(), form(), Enums::GameVersion::BW);
    return (ability() == slots.ability2 && slots.ability2 != slots.ability1) ? 2 : 1;
}
```

**Gen 5 personal tables have a real hidden-ability slot**, unlike Gen 4. The generator must emit it.

**3. One ball field, one met/egg location field.** Gen 4's DP/Pt-vs-HGSS split is gone.

### 5.4 Stats

Standard Gen 3+ formula, nature modifier from the **stored** nature. No Hyper Training.
`hasHyperTraining()` stays false.

---

## 6. Text — plain UTF-16

```csharp
// StringConverter5.cs
public const char Terminator = (char)0xFFFF;

public static int LoadString(ReadOnlySpan<byte> data, Span<char> result)
{
    for (int i = 0; i < data.Length; i += 2)
    {
        var value = ReadUInt16LittleEndian(data[i..]);
        if (value is Terminator or 0) break;              // BOTH 0xFFFF and 0x0000 terminate
        result[ctr++] = StringConverter4Util.NormalizeGenderSymbol((char)value);
    }
    return ctr;
}
```

**No code page.** The `u16` *is* the UTF-16 code unit. PKSE's existing `utf16ToUtf8` / `utf8ToUtf16`
handle everything.

> **This is the one place you must NOT share code with Gen 4.** Gen 4 runs every value through
> `ConvertValue2CharG4`; Gen 5 does not. Routing Gen 5 text through a Gen 4 converter mangles every
> name in a way that still looks like text.

Two details to copy:

- **`0x0000` terminates as well as `0xFFFF`.** Gen 4 only checks `0xFFFF`.
- Gender symbols are normalised on read (`♂`/`♀` half-width ↔ full-width) and un-normalised on
  write when the string is half-width. Reuse PKSE's existing handling; the species/name tables
  already carry the real glyphs (manual ch. 07).
- The same **"a name that exactly fills its field gets no terminator"** rule applies:

```csharp
int count = value.Length * 2;
if (count == destBuffer.Length) return count;
WriteUInt16LittleEndian(destBuffer[count..], Terminator);
```

`canStoreNickname()` can return `true` unconditionally for Gen 5 — it stores UTF-16 and takes
essentially anything, like Gen 8/9.

Field widths: OT 16 bytes / 7 chars, nickname 22 bytes / 10 chars, **box name `0x14` bytes /
13 chars**.

---

## 7. Items

**Same `u16 id, u16 count` codec as Gen 4** (`InventoryPouch4`), inside the Inventory block —
BW block 25 (`0x18400`, `0x09C0`), B2W2 block 25 (`0x18400`, `0x09EC`).

Take the pouch offsets from `Items/Bags/PlayerBag5BW.cs` and `PlayerBag5B2W2.cs`; they are
relative to the Inventory block. The codec, the "every slot is rewritten" behaviour and
`itemsAreIdIndexed() == false` are all identical to Gen 4, so the Gen 4 inventory code is directly
reusable — only the offset table changes.

HM count cap and legal sets come from `ItemStorage5BW` / `ItemStorage5B2W2`.

---

## 8. Data tables

`tools/gen_gen5.py` → `Gen5Tables.h/.cpp`.

| Table | Source | Notes |
|---|---|---|
| Base stats | `personal_bw`, `personal_b2w2` | species 0–649 |
| Abilities (3 slots) | same | **Hidden ability exists in Gen 5** — emit all three |
| Gender ratio, friendship | same | |
| Types | same | pre-Fairy; convert to PKSE ids at generation time |
| Growth rate | same | verify before reusing `Experience.cpp` |
| Learnsets | `lvlmove_bw/b2w2.pkl` + TM/HM + tutors | union with pre-evolutions |
| Move info | `MoveInfo5` | Gen 5 PP |
| Item names | PKHeX item strings index 5 | 0–638 |
| Item pouch sets | `ItemStorage5BW` / `5B2W2` | |
| Met location names | `text_bw_*`, `text_b2w2_*` | two namespaces |
| **Block tables** | `SaveBlockAccessor5BW.cs` / `5B2W2.cs` | **generate these too** (§2.3) |

**Ability ids 1–164, move ids 1–559 and species 1–649 are strict prefixes of the modern tables**, so
`getAbilityName`, `getMoveName` and `getSpeciesName` work unchanged. Only **items**, **locations**
and the **block tables** are new.

---

## 9. PKSE integration

### 9.1 `GameVersion`

```cpp
enum class GameVersion {
    // … Gen 5 (Nintendo DS). Values match PKHeX; getOriginGameName() ALREADY names all four
    // and getVersionGeneration() ALREADY returns 5 for 20..23.
    W  = 20, // White
    B  = 21, // Black
    W2 = 22, // White 2
    B2 = 23, // Black 2
    // …
    BW   = 82,   // Black/White group
    B2W2 = 83,   // Black 2/White 2 group
};
```

Add: `getGameGroup` (`B`/`W` → `BW`; `B2`/`W2` → `B2W2`), `getGameVersionName`
("Black/White", "Black 2/White 2"), `getGroupRepVersion` (`BW → 21` Black, `B2W2 → 23` Black 2),
`isGameSupported` needs no change -- it is derived from `getGameGroup`.

> Note PKHeX's ordering: **White is 20 and Black is 21**, White 2 is 22 and Black 2 is 23. The
> pairs are numerically reversed from the way they are usually written. Do not "fix" this.

### 9.2 New files

```
include/Pokemon/Pokemon5BW.h    + .cpp     PK5 (one class -- the record is identical across BW/B2W2)
include/Pokemon/Gen5Tables.h    + .cpp     generated
include/Trainer/Blocks5BW.h                generated block table (Black/White)
include/Trainer/Blocks5B2W2.h              generated block table (Black 2/White 2)
include/Trainer/Inventory5.h               pouch tables
include/Trainer/Trainer5.h      + .cpp     base: block table walk, CRC pairs
include/Trainer/Trainer5BW.h    + .cpp
include/Trainer/Trainer5B2W2.h  + .cpp
tools/gen_gen5.py
tools/gen_blocktables.py
```

Reused from Gen 4, unchanged: `Utils/CRC16.h`, `Encryption4` (rename to `Encryption45`), the
Gen-3+ stat formula, `InventoryPouch4`'s codec.

### 9.3 `Trainer5` base

> The planned shared base below did not land as a base class: its logic lives, copied, in each of
> `Trainer5BW` and `Trainer5B2W2`, with the geometry as plain members. The algorithm is unchanged.

```cpp
namespace Trainer {

    /// One Gen 5 save block. PKHeX BlockInfoNDS: the CRC is stored TWICE -- once just past the
    /// block, once inside the trailing checksum block.
    struct BlockEntryNDS {
        uint32_t offset;
        uint32_t length;
        uint32_t chkOffset;
        uint32_t chkMirror;
    };

    class Trainer5 : public Trainer {
    protected:
        std::vector<uint8_t> saveData;   // the whole 0x80000 file
        std::string savePath;

        virtual std::span<const BlockEntryNDS> blocks() const noexcept = 0;

        void writeChecksums() {
            // Blocks in table order. The CHECKSUM BLOCK is last and checksums ITSELF, so every
            // other block's mirror has already landed inside it by the time we reach it.
            for (const BlockEntryNDS& b : blocks()) {
                const uint16_t crc = Utils::crc16ccitt(&saveData[b.offset], b.length);
                Utils::writeUInt16LittleEndian(&saveData[b.chkOffset], crc);
                Utils::writeUInt16LittleEndian(&saveData[b.chkMirror], crc);
            }
        }

    public:
        size_t getBoxCount()    const noexcept override { return 24; }
        size_t getSlotsPerBox() const noexcept override { return 30; }
        uint32_t getMaxMoney()  const noexcept override { return 9999999; }
        size_t getMaxTrainerNameLength() const noexcept override { return 7; }
        int    getMaxTrainerNameDigits() const noexcept override { return 4; }   // gens 4-5
        bool   supportsBoxNames() const noexcept override { return true; }
        size_t getMaxBoxNameLength() const noexcept override { return 13; }      // NOT 8
        bool   itemsAreIdIndexed() const noexcept override { return false; }
        bool   compactStorage() override { return false; }                       // positional
    };
}
```

Box and party access:

```cpp
size_t Trainer5::boxOffset(size_t box) const noexcept { return 0x400 + box * 0x1000; }
size_t Trainer5::partyOffset(size_t slot) const noexcept { return 0x18E00 + 8 + slot * 220; }
uint8_t Trainer5::partyCount() const noexcept { return saveData[0x18E00 + 4]; }
```

### 9.4 Save read/write, entry point, UI, bank

Same loose-file shape as Gen 1/2/4. Loose-file I/O is shared by every pre-Switch format: `Save::openExternalSave` runs the
`LOOSE_FORMATS` probe chain and `Save::saveExternalSave` dispatches the write on `getGameGroup()`
with a `static_cast` — adding a format is one row and one `case`. `Save::backupOriginalFile` copies
the **original bytes** to `sdmc:/PKSE/FileBackups` **before** writing. These classes are **not** in
`TrainerVariant`: no title ID, no title-ID dispatch.

- Add `Trainer5BW::detect` / `Trainer5B2W2::detect` to the browser probe chain, **after** the three
  Gen 4 probes (all five share `0x80000`).
- Box grid: **24 × 30 → `boxGridColumns(30) == 6`**. No UI change.
- `Names::getItemNameFor()`: `BW` / `B2W2` branches.
- `PickerKind`: `ItemG5`, `PouchItemG5`.
- Ball ids cap at 25.
- **`getMaxBoxNameLength()` is 13** — the rename dialog reads this, so nothing else to do, but
  verify on hardware that a 13-character name renders.
- `Bank.cpp`: `BTAG_G5` — **one tag per ENTITY format, not per game.** Every PK5 is stored
  under it and reads back as the format's representative group (`groupForBankTag`). Never compare
  that group to the entity's own; the invariant is `tag == bankTagForEntity(entity)`. Add-only.
- **Conversion: Gen 5 is not in `inFamily`.** PK5 → PK6 is the Gen 5/6 format break and a
  project in itself. Gen 5 is banked, not converted.

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

## 10. Verification

```bash
python tools/syntax_check.py && python tools/syntax_check.py --warnings
```

**The gate is an unedited round trip of a real Black/White and a real Black 2/White 2 save**: open,
write straight back, byte-identical. Gen-5-specific round-trip risks:

- **A wrong `(offset, length)` in a hand-copied block table.** The CRC still validates — over the
  wrong range. This is why §2.3 says generate the tables.
- **The checksum block written before the other blocks' mirrors land in it.**
- **Only one of the two CRC copies written.**
- **The 16-byte tail of a box zeroed** (outside the CRC, so no checksum failure — just a diff).
- **Box name written with a `0x28` stride instead of a `0x14` slice.**
- **Terminator written into an exactly-full name field.**
- **Gen 5 text routed through the Gen 4 code page.**
- **Nature written to the PID instead of `0x41`.**

Then the party-stat cross-check (a party record stores its battle stats: recompute and compare) and
edit persistence (change each field, save, reopen, check each one separately).

---

## 11. Trap list

1. **`0x80000` is also Gen 4's size.** Five probes; exactly one must match.
2. **Every block's CRC is written TWICE.**
3. **The checksum block checksums itself** and must be written last.
4. **Generate the block tables.** A typo produces a valid CRC over a wrong range.
5. **Box stride is `0x1000`, block length is `0x0FF0`** — 16 bytes of slack per box, outside the CRC.
6. **Box names: `0x28` stride, `0x14` used, 13 characters.** Not 8.
7. **Nature is STORED at `0x41`** — unlike Gen 3/4. No PID rebuild, no destructive warning.
8. **Hidden ability is a flag at `0x42` bit 0**, not a PID-derived slot.
9. **One ball field** (`0x83`) — Gen 4's two-field trap does not apply.
10. **`SIZE_5PARTY` is 220, Gen 4's is 236.** Stored size is 136 in both.
11. **Entity crypt seed is the CHECKSUM**; refresh it before encrypting. (Same as Gen 4.)
12. **`IV32` carries the egg and nicknamed flags.** Read-modify-write.
13. **Gen 5 text is UTF-16, Gen 4 text is a code page.** Do not share the converter.
14. **`0x0000` terminates a Gen 5 string as well as `0xFFFF`.**
15. **PKHeX numbers White 20 / Black 21**, White 2 22 / Black 2 23. Reversed from the usual writing.
16. **`compactStorage()` stays `false`.**
17. **The save names its own game** at `PlayerData5 +0x1F`, so Black and White (and Black 2 and
    White 2) are told apart. Accept the byte only when it belongs to the group whose layout parsed;
    anything else is a misparse and falls back to the group's representative.

---

## 12. Suggested order

Assuming Gen 4 already exists:

1. `gen_blocktables.py` → one `Blocks<Gen><Code>.h` per group. **Verify by CRC-validating a real save with it** before
   writing any other code — that single test proves the whole table.
2. Generated tables (`gen_personaltables.py`); check them against PKHeX.
3. `Pokemon5BW` — copy the PK4 entity, change `SIZE_PARTY`, nature, ability flag, ball, `0x87`.
   Every setter reads back what it wrote.
4. `Trainer5` base + `Trainer5BW`. Round trip to zero diff.
5. `Trainer5B2W2` — offsets only.
6. Items, box names, trainer fields.
7. Entry point (this is what exposes the format), UI, bank, docs.

Step 1 is unusually high-leverage here: if the block table is right, almost everything else follows,
and if it is wrong nothing else will be trustworthy.
