#!/usr/bin/env python3
"""Generate the fixed save-block tables -- ONE FILE PER SAVE-FORMAT GROUP.

WHICH GROUPS GET ONE, AND WHY ONLY THESE SIX. A block table describes a save that is a fixed list
of blocks, each with its own offset, length and checksum. Six groups are shaped that way:
Black/White and Black 2/White 2 on DS, and X/Y, Omega Ruby/Alpha Sapphire, Sun/Moon and Ultra
Sun/Ultra Moon on 3DS -- 333 blocks between them, which is 333 chances to mistype a hex number,
and a wrong (offset, length) still produces a perfectly valid CRC over the wrong range. Nothing
downstream can catch that, which is why these are generated rather than typed.

Nothing else needs one. Gens 1 and 2 are flat SRAM whose offsets shift with the locale, Gen 3 is
fourteen rotated sectors each carrying its own id, Gen 4 is two partitions holding a General and a
Storage block, and BDSP is a flat fixed-offset image -- all a handful of constants or arithmetic.
Let's Go, Sword/Shield, Legends: Arceus, Scarlet/Violet and Legends: Z-A use SCBlocks, where the
block list is data inside the save and a static table is impossible rather than merely redundant.

ONE FILE PER GROUP, not one per generation. The tables used to live in Gen5Blocks.h and
Gen67Blocks.h, the second of which covered four games across two generations. The DATA was already
split per group there, so this is a move rather than a repair -- but a header that serves four
games is the shape that lets a fifth quietly share a table it should not.

THE GEN 5 NAMED INDICES ARE NOW DERIVED PER GAME. They used to be five hardcoded constants shared
by Black/White and Black 2/White 2, correct because the two block orders agree up to #27 -- a claim
that lived in a comment with nothing checking it, while the only static_asserts pinned the block
COUNTS (70 and 74). A reorder of either game's first 28 blocks would have left the counts matching
and silently pointed Black/White's inventory at Black 2's block. Each game's indices are now looked
up in ITS OWN table by block name, and a name that is missing or ambiguous fails the run.

Run:  python tools/gen_blocktables.py
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pkhex_source import pkhex_path  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INC = os.path.join(ROOT, "include", "Trainer")

# ------------------------------------------------------------------------------------------
# DS-era (Gen 5): four numbers per block, the CRC stored twice.
# ------------------------------------------------------------------------------------------
NDS_SOURCES = [
    ("5BW", "Black/White", "Saves/Access/SaveBlockAccessor5BW.cs", "BlocksBW", 0x24000, 0x8C),
    ("5B2W2", "Black 2/White 2", "Saves/Access/SaveBlockAccessor5B2W2.cs", "BlocksB2W2", 0x26000, 0x94),
]

NDS_ENTRY = re.compile(
    r"new\(\s*(0x[0-9A-Fa-f]+)\s*,\s*(0x[0-9A-Fa-f]+)\s*,\s*"
    r"(0x[0-9A-Fa-f]+)\s*,\s*(0x[0-9A-Fa-f]+)\s*\)\s*,?\s*(?://\s*(.*))?")

# The blocks the save layer reaches for, found in each game's OWN table by the name PKHeX gives
# them. These are anchored regexes, not prefixes: "Box " also matches "Box Names", and "Box 1"
# also matches "Box 10" through "Box 19", either of which silently resolves to the wrong block.
NDS_WANTED = [
    ("BOX_NAMES", r"Box Names$"),
    ("BOX_FIRST", r"Box 1$"),
    ("INVENTORY", r"Inventory$"),
    ("PARTY", r"Party Pok"),          # "Party Pokemon", accented in PKHeX
    ("TRAINER", r"Trainer Data$"),
]

# A storage block, as opposed to the box-NAMES block that shares the word.
NDS_BOX_BLOCK = re.compile(r"^Box \d+$")


def parse_nds(path, array):
    text = open(path, encoding="utf-8-sig").read()
    start = text.index(array)
    body = text[start:text.index("];", start)]
    out = []
    for match in NDS_ENTRY.finditer(body):
        offset, length, checksum, mirror = (int(match.group(k), 16) for k in (1, 2, 3, 4))
        note = re.sub(r"^\d+\s+", "", (match.group(5) or "").strip())
        out.append((offset, length, checksum, mirror, note))
    if not out:
        sys.exit(f"no entries parsed from {array}")
    return out


def verify_nds(code, blocks, saveSize, infoLength):
    """Everything checkable without a save file. Ported unchanged from gen_gen5blocks.py."""
    bad = []
    for index, (offset, length, checksum, _mirror, _note) in enumerate(blocks):
        if offset + length > saveSize:
            bad.append(f"{code} #{index}: block runs past the save "
                       f"({offset:#x}+{length:#x} > {saveSize:#x})")
        if not (offset + length <= checksum < offset + length + 0x10):
            bad.append(f"{code} #{index}: checksum {checksum:#x} not just after data end "
                       f"{offset + length:#x}")
    for index in range(1, len(blocks)):
        if blocks[index][0] < blocks[index - 1][0] + blocks[index - 1][1]:
            bad.append(f"{code} #{index}: overlaps #{index - 1}")

    last = blocks[-1]
    if last[2] != last[3]:
        bad.append(f"{code}: last block is not self-checksumming ({last[2]:#x} != {last[3]:#x})")
    if last[0] != saveSize - 0x100:
        bad.append(f"{code}: checksum block at {last[0]:#x}, expected {saveSize - 0x100:#x}")
    if last[1] != infoLength:
        bad.append(f"{code}: checksum block length {last[1]:#x}, expected {infoLength:#x}")
    for index, (_o, _l, _c, mirror, _n) in enumerate(blocks[:-1]):
        want = last[0] + index * 2
        if mirror != want:
            bad.append(f"{code} #{index}: mirror {mirror:#x}, expected {want:#x}")
    if last[2] < last[0] + last[1]:
        bad.append(f"{code}: self-checksum {last[2]:#x} overlaps its own data")

    if bad:
        for line in bad:
            print("  FAIL " + line)
        sys.exit(f"{code} block table failed verification")


def nds_indices(code, blocks):
    """Each game's named block indices, from ITS OWN table. Ambiguity is a failure, not a guess."""
    found = {}
    for key, pattern in NDS_WANTED:
        matcher = re.compile("^" + pattern)
        hits = [i for i, b in enumerate(blocks) if matcher.match(b[4])]
        if not hits:
            sys.exit(f"{code}: no block matching '{pattern}' -- PKHeX renamed it, or the table is wrong.")
        if len(hits) > 1:
            sys.exit(f"{code}: '{pattern}' matches blocks {hits}; the name is no longer unique.")
        found[key] = hits[0]
    # Boxes must be one contiguous run starting at BOX_FIRST -- the storage reader indexes into it.
    boxes = [i for i, b in enumerate(blocks) if NDS_BOX_BLOCK.match(b[4])]
    if boxes != list(range(found["BOX_FIRST"], found["BOX_FIRST"] + len(boxes))):
        sys.exit(f"{code}: box blocks are not contiguous: {boxes}")
    found["BOX_COUNT"] = len(boxes)
    return found


def emit_nds(code, game, blocks, saveSize, infoLength, indices):
    guard = f"TRAINER_BLOCKS{code.upper()}_H"
    rows = "".join(
        "        { %#07x, %#06x, %#07x, %#07x },  // %02d %s\n"
        % (o, l, c, m, i, n) for i, (o, l, c, m, n) in enumerate(blocks))
    named = "".join(
        f"    inline constexpr size_t BLOCK_{key}_{code} = {value};\n"
        for key, value in (("BOX_NAMES", indices["BOX_NAMES"]), ("BOX_FIRST", indices["BOX_FIRST"]),
                           ("INVENTORY", indices["INVENTORY"]), ("PARTY", indices["PARTY"]),
                           ("TRAINER", indices["TRAINER"])))
    text = f"""/**
 * GENERATED -- do not hand-edit. {game} save block table.
 *
 * Regenerate with `python tools/gen_blocktables.py`. Source: PKHeX SaveBlockAccessor{code[0]}*.
 *
 * ONE GROUP, ONE TABLE. {game} does not borrow a sibling group's geometry, even where the two
 * block orders agree for a while (Black/White and Black 2/White 2 agree up to #27 and diverge at
 * #32): a shared table would read one game's save with the other's offsets past that point. The
 * named indices below are looked up in THIS table by block name, so they cannot drift from the
 * rows above them.
 *
 * See BlockTable.h for the row type, and for why only six groups have a table at all.
 */
#ifndef {guard}
#define {guard}

#include "Trainer/BlockTable.h"

namespace Trainer
{{
    inline constexpr size_t SAVE_SIZE_{code} = {saveSize:#07x};
    inline constexpr size_t CHECKSUM_BLOCK_LENGTH_{code} = {infoLength:#04x};

    inline constexpr BlockEntryNDS BLOCKS_{code}[] = {{
{rows}    }};
    inline constexpr size_t BLOCK_COUNT_{code} = sizeof(BLOCKS_{code}) / sizeof(BlockEntryNDS);

    // Block indices the save layer reaches for, resolved by NAME out of the table above.
{named}    inline constexpr size_t BOX_BLOCK_COUNT_{code} = {indices["BOX_COUNT"]};
}}

#endif  // {guard}
"""
    with open(os.path.join(INC, f"Blocks{code}.h"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write(text)
    print(f"  Blocks{code:<7} {len(blocks):>3} blocks, boxes at {indices['BOX_FIRST']}"
          f"..{indices['BOX_FIRST'] + indices['BOX_COUNT'] - 1}, "
          f"inventory {indices['INVENTORY']}, party {indices['PARTY']}, trainer {indices['TRAINER']}")


# ------------------------------------------------------------------------------------------
# 3DS-era (Gen 6 / Gen 7): two numbers per block, checksums in the BEEF chunk.
# ------------------------------------------------------------------------------------------
DS3_SOURCES = [
    ("6XY", "X/Y", "Saves/Access/SaveBlockAccessor6XY.cs", "BlocksXY", 0x65600, 6, "Saves/SAV6XY.cs"),
    ("6ORAS", "Omega Ruby/Alpha Sapphire", "Saves/Access/SaveBlockAccessor6AO.cs",
     "BlocksAO", 0x76000, 6, "Saves/SAV6AO.cs"),
    ("7SM", "Sun/Moon", "Saves/Access/SaveBlockAccessor7SM.cs", "BlockInfoSM", 0x6BE00, 7, "Saves/SAV7SM.cs"),
    ("7USUM", "Ultra Sun/Ultra Moon", "Saves/Access/SaveBlockAccessor7USUM.cs",
     "BlockInfoUSUM", 0x6CC00, 7, "Saves/SAV7USUM.cs"),
]

DS3_WANTED = {
    "ITEM": r"MyItem\w*\s+Items\s*\{\s*get;\s*\}\s*=\s*new\(sav,\s*Block\(sav,\s*(\d+)\)\)",
    "BOX_LAYOUT": r"BoxLayout[67]\s+BoxLayout\s*\{\s*get;\s*\}\s*=\s*new\(sav,\s*Block\(sav,\s*(\d+)\)\)",
    "STATUS": r"MyStatus\w*\s+(?:My)?Status\s*\{\s*get;\s*\}\s*=\s*new\(sav,\s*Block\(sav,\s*(\d+)\)\)",
    "MISC": r"Misc[67]\w*\s+Misc\s*\{\s*get;\s*\}\s*=\s*new\(sav,\s*Block\(sav,\s*(\d+)\)\)",
}

DS3_ENTRY = re.compile(r"new\(bo\w+,\s*(\d+),\s*(0x[0-9A-Fa-f]+),\s*(0x[0-9A-Fa-f]+)\)\s*,?\s*(?://\s*(.*))?")
DS3_SAV_FIELD = re.compile(
    r"\b(Party|Box)\s*=\s*(?:Blocks\.BlockInfo\[(\d+)\]\.Offset|(0x[0-9A-Fa-f]+))\s*;")


def parse_ds3(path, array):
    text = open(path, encoding="utf-8-sig").read()
    start = text.index(array)
    body = text[start:text.index("];", start)]
    out = []
    for match in DS3_ENTRY.finditer(body):
        blockId, offset, length = int(match.group(1)), int(match.group(2), 16), int(match.group(3), 16)
        note = re.sub(r"^\d+\s+", "", (match.group(4) or "").strip())
        out.append((blockId, offset, length, note))
    if not out:
        sys.exit(f"no entries parsed from {array}")
    return out, text


def resolve_storage_ds3(savText, blocks, code):
    out = {}
    for match in DS3_SAV_FIELD.finditer(savText):
        name = match.group(1).upper()
        if name in out:
            continue
        if match.group(2) is not None:
            out[name] = int(match.group(2))
        else:
            offset = int(match.group(3), 16)
            hit = [b[0] for b in blocks if b[1] == offset]
            if not hit:
                sys.exit(f"{code}: {name} offset {offset:#x} matches no block")
            out[name] = hit[0]
    missing = {"PARTY", "BOX"} - set(out)
    if missing:
        sys.exit(f"{code}: could not resolve {sorted(missing)} from the SAV class")
    return out


def verify_ds3(code, blocks, saveSize):
    """Ported unchanged from gen_gen67blocks.py: ids dense, offsets recomputed, slots in range."""
    bad = []
    for index, (blockId, _o, _l, _n) in enumerate(blocks):
        if blockId != index:
            bad.append(f"{code} entry {index} has id {blockId}")
    running = 0
    for blockId, offset, length, _note in blocks:
        if offset != running:
            bad.append(f"{code} #{blockId}: offset {offset:#x}, recomputed {running:#x} "
                       f"(a length above this one is wrong)")
        running += (length + 0x1FF) & ~0x1FF
    metadata = saveSize - 0x200
    if running > metadata:
        bad.append(f"{code}: blocks run to {running:#x}, past the metadata chunk at {metadata:#x}")
    for blockId, _o, _l, _n in blocks:
        slot = metadata + 0x14 + blockId * 8 + 6
        if not (metadata + 0x14 <= slot < saveSize):
            bad.append(f"{code} #{blockId}: checksum slot {slot:#x} outside the metadata chunk")
    if bad:
        for line in bad:
            print("  FAIL " + line)
        sys.exit(f"{code} block table failed verification")


def emit_ds3(code, game, blocks, saveSize, generation, indices):
    guard = f"TRAINER_BLOCKS{code.upper()}_H"
    boxCount = 31 if generation == 6 else 32
    rows = "".join("        { %#08x, %#07x },  // %02d %s\n" % (o, l, b, n) for b, o, l, n in blocks)
    named = "".join(f"    inline constexpr size_t BLOCK_{key}_{code} = {indices[key]};\n"
                    for key in ("ITEM", "BOX_LAYOUT", "STATUS", "MISC", "PARTY", "BOX"))
    algorithm = "CRC16-CCITT" if generation == 6 else "CRC16Invert"
    text = f"""/**
 * GENERATED -- do not hand-edit. {game} save block table.
 *
 * Regenerate with `python tools/gen_blocktables.py`. Source: PKHeX SaveBlockAccessor{code}.
 *
 * ONE GROUP, ONE TABLE. The four 3DS `main` formats share a row SHAPE and nothing else -- their
 * block counts, lengths and file sizes all differ, so a table serving more than one of them would
 * read one game's save with another's geometry.
 *
 * This group checksums with {algorithm}. Gen 6 and Gen 7 use different algorithms of the same
 * width, and nothing in the data says which produced a given value, so the container that owns
 * this table owns that choice too.
 *
 * The block indices below are read out of PKHeX's own accessor and SAV classes rather than typed,
 * so an index cannot drift from the block it names. See BlockTable.h for the row type.
 */
#ifndef {guard}
#define {guard}

#include "Trainer/BlockTable.h"

namespace Trainer
{{
    inline constexpr size_t SAVE_SIZE_{code} = {saveSize:#08x};
    inline constexpr size_t BOX_COUNT_{code} = {boxCount};

    inline constexpr BlockEntry3DS BLOCKS_{code}[] = {{
{rows}    }};
    inline constexpr size_t BLOCK_COUNT_{code} = sizeof(BLOCKS_{code}) / sizeof(BlockEntry3DS);

    // Block indices the save layer reaches for, resolved from PKHeX's accessor/SAV classes.
{named}}}

#endif  // {guard}
"""
    with open(os.path.join(INC, f"Blocks{code}.h"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write(text)
    print(f"  Blocks{code:<7} {len(blocks):>3} blocks, {boxCount} boxes, {algorithm}")


def main():
    print("DS-era (Gen 5):")
    for code, game, relpath, array, saveSize, infoLength in NDS_SOURCES:
        blocks = parse_nds(pkhex_path(relpath), array)
        verify_nds(code, blocks, saveSize, infoLength)
        emit_nds(code, game, blocks, saveSize, infoLength, nds_indices(code, blocks))

    print("3DS-era (Gen 6 / Gen 7):")
    for code, game, relpath, array, saveSize, generation, savrel in DS3_SOURCES:
        blocks, text = parse_ds3(pkhex_path(relpath), array)
        verify_ds3(code, blocks, saveSize)
        indices = {}
        for key, pattern in DS3_WANTED.items():
            match = re.search(pattern, text)
            if not match:
                sys.exit(f"{code}: could not resolve the block index for {key}")
            indices[key] = int(match.group(1))
        indices.update(resolve_storage_ds3(
            open(pkhex_path(savrel), encoding="utf-8-sig").read(), blocks, code))
        partyLength = blocks[indices["PARTY"]][2]
        boxLength = blocks[indices["BOX"]][2]
        boxCount = 31 if generation == 6 else 32
        if partyLength < 6 * 0x104 + 1:
            sys.exit(f"{code}: party block #{indices['PARTY']} is only {partyLength:#x}")
        if boxLength < boxCount * 30 * 0xE8:
            sys.exit(f"{code}: box block #{indices['BOX']} is only {boxLength:#x}")
        emit_ds3(code, game, blocks, saveSize, generation, indices)


if __name__ == "__main__":
    main()
