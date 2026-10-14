#!/usr/bin/env python3
"""Generate PKSE's Generation 1 (Red/Blue/Yellow) data tables from PKHeX.

Gen 1 gets its OWN tables. Not out of tidiness -- every table below differs from the modern
one in a way that is silent rather than loud, which is the failure mode this codebase keeps
paying for. Four concrete reasons, each with a precedent in docs/BUGS.md:

1. BASE STATS. Gen 1 has a single **Special** stat, not SpA/SpD. Gyarados is SPC 100 in Gen 1;
   the modern row says SpA 60 / SpD 100. Reading the modern row computes a wrong special stat
   for essentially every Pokemon in the game, and PKSE writes the computed battle stats back
   into the record -- so the wrongness persists. (Same shape as the Gen 3 base-stat split that
   gen_personal.py already carves out: Dugtrio's Attack went 80 -> 100 after Gen 3.)

2. SPECIES INDEX. Gen 1 stores an internal index, not a National Dex number, and the ordering
   is arbitrary -- Ivysaur is 0x09, Bulbasaur is 0x99. This is exactly BUGS Issue 36, where
   Gen 3's index was assumed to be a flat +25 shift and 109 of 135 Hoenn species were stored as
   a different Pokemon. There is no formula here at all; it is a lookup or it is wrong.

3. TYPE IDS. Gen 1's ROM numbering is not the modern one and is not even contiguous: Normal 0,
   Fighting 1, Flying 2, Poison 3, Ground 4, Rock 5, **Bug 7, Ghost 8** (6 is the unused "Bird"
   slot), then **Fire 20, Water 21, Grass 22, Electric 23, Psychic 24, Ice 25, Dragon 26**.
   PKSE's TYPE_* constants are the contiguous MoveType numbering (Bug 6, Ghost 7, Fire 9...).
   Handing a raw Gen 1 type byte to the type-icon lookup draws a Bug as Ghost, a Ghost as Steel,
   and every special type as an out-of-range id. We map to PKSE ids HERE, once, at generation
   time, so no call site can forget. (BUGS Issue 26 is the same bug with forms.)

4. MOVE PP. MoveInfo1.PP is Gen 1's own table. PP is stored per-move in the record and is what
   the game shows; a modern PP value is simply a different number.

Deliberately NOT generated here, because Gen 1 does not have them and inventing a zero would be
worse than having nothing: abilities, natures, gender, egg groups, friendship, held items, balls,
met location, origin game, shininess, Pokerus. A Gen 1 Bulbasaur has no ability; the details view
must say so rather than print "Overgrow".

Reused rather than duplicated, because they are genuinely identical: species names #1-151 and
move names #1-165 (same strings), and the six growth-rate EXP curves in Experience.cpp. The
growth-rate *index per species* is still emitted below -- it is one byte and it removes the
question -- and this script cross-checks it against Gen 3 and reports any species that disagree.

Run:  python tools/gen_gen1.py
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pkhex_source import pkhex_path  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_H = os.path.join(ROOT, "include", "Pokemon", "Gen1Tables.h")
OUT_CPP = os.path.join(ROOT, "src", "Pokemon", "Gen1Tables.cpp")

MAX_SPECIES_1 = 151
PERSONAL_SIZE = 0x1C          # PersonalInfo1.SIZE -- and PersonalInfo3.SIZE, same shape

# Gen 1 ROM type id -> PKSE TYPE_* (the contiguous MoveType numbering in PokemonTypes.h).
# 6 is absent on purpose: it is Gen 1's unused "Bird" type and no species carries it.
G1_TYPE_TO_PKSE = {
    0: 0,    # Normal
    1: 1,    # Fighting
    2: 2,    # Flying
    3: 3,    # Poison
    4: 4,    # Ground
    5: 5,    # Rock
    7: 6,    # Bug     <- shifts
    8: 7,    # Ghost   <- shifts
    20: 9,   # Fire    <- block moves 20-26 -> 9-15
    21: 10,  # Water
    22: 11,  # Grass
    23: 12,  # Electric
    24: 13,  # Psychic
    25: 14,  # Ice
    26: 15,  # Dragon
}
TYPE_NAMES = {0: "Normal", 1: "Fighting", 2: "Flying", 3: "Poison", 4: "Ground", 5: "Rock",
              6: "Bug", 7: "Ghost", 9: "Fire", 10: "Water", 11: "Grass", 12: "Electric",
              13: "Psychic", 14: "Ice", 15: "Dragon"}


def parse_byte_span(cs_text, name):
    """Pull `private static ReadOnlySpan<byte> <name> => [ ... ];` out of a PKHeX .cs file."""
    m = re.search(r"ReadOnlySpan<byte>\s+" + re.escape(name) + r"\s*=>\s*\[(.*?)\];",
                  cs_text, re.S)
    if not m:
        raise SystemExit("could not find %s -- did PKHeX rename it?" % name)
    return [int(v, 0) for v in re.findall(r"0x[0-9A-Fa-f]+|\d+", m.group(1))]


def parse_moveinfo1_pp():
    txt = open(pkhex_path("Moves/MoveInfo1.cs"), encoding="utf-8-sig").read()
    m = re.search(r"ReadOnlySpan<byte>\s+PP\s*=>\s*\[(.*?)\];", txt, re.S)
    if not m:
        raise SystemExit("MoveInfo1.PP not found")
    # The literal is zero-padded decimal ("05", "35"); int(x, 0) would read 0-prefixed as octal.
    return [int(v, 10) for v in re.findall(r"\d+", m.group(1))]


MAX_ITEM_1 = 254              # Gen 1 item ids run 0..254 (one byte, 0xFF terminates a pouch)


def parse_item_names():
    """PKHeX text_ItemsG1_en.txt -> 255 Gen 1 item names.

    Gen 1's item ids are ITS OWN SPACE and share nothing with the modern table: id 20 is a Potion
    here and a completely different item now, and the Gen 1 TMs are real bag items (201-250) where
    modern TMs are not. Reading a Gen 1 bag through the modern names is the same class of mistake
    as reading its species through the modern dex.

    The file is UTF-16 (PKHeX ships all its text resources that way) and holds a name for every id
    including the ones the games never use, which come through as empty strings.
    """
    raw = open(pkhex_path("Resources/text/items/gen1/text_ItemsG1_en.txt"), "rb").read()
    lines = raw.decode("utf-16").replace("\r\n", "\n").split("\n")
    names = [ln.strip() for ln in lines[:MAX_ITEM_1 + 1]]
    while len(names) <= MAX_ITEM_1:
        names.append("")
    return names


def parse_item_legal():
    """PKHeX ItemStorage1.General -> the set of ids that may sit in a Gen 1 bag.

    Not the same as "has a name". 84-195 and 251-254 are unused slots with no name at all; 7 and 44
    ARE named (as "????? (n)") but are still not obtainable; and 8/9 (Safari Ball, Pokedex) have
    real names yet are not bag items. The picker offers this list; the display names everything,
    because a save can contain a byte the picker would not offer and the user needs to see what.
    """
    txt = open(pkhex_path("Items/ItemStorage1.cs"), encoding="utf-8-sig").read()
    m = re.search(r"ReadOnlySpan<ushort> General =>\s*\[(.*?)\];", txt, re.S)
    if not m:
        raise SystemExit("could not find ItemStorage1.General")
    body = re.sub(r"//[^\n]*", "", m.group(1))
    legal = sorted({int(v) for v in re.findall(r"\d+", body)})
    if not legal or max(legal) > MAX_ITEM_1:
        raise SystemExit("ItemStorage1.General parsed oddly: %r" % legal[:10])
    return set(legal)


def build():
    conv = open(pkhex_path("PKM/Util/Conversion/SpeciesConverter.cs"), encoding="utf-8-sig").read()
    i2n = parse_byte_span(conv, "Table1InternalToNational")
    n2i = parse_byte_span(conv, "Table1NationalToInternal")

    with open(pkhex_path("Resources/byte/personal/personal_rb"), "rb") as fh:
        personal1 = fh.read()
    with open(pkhex_path("Resources/byte/personal/personal_fr"), "rb") as fh:
        personal3 = fh.read()

    pp = parse_moveinfo1_pp()
    item_names = parse_item_names()
    item_legal = parse_item_legal()

    rows = []
    growth_diffs = []
    for sp in range(0, MAX_SPECIES_1 + 1):
        e = personal1[sp * PERSONAL_SIZE:(sp + 1) * PERSONAL_SIZE]
        if len(e) < PERSONAL_SIZE:
            raise SystemExit("personal_rb truncated at species %d" % sp)
        t1raw, t2raw = e[0x06], e[0x07]
        for raw in (t1raw, t2raw):
            if sp != 0 and raw not in G1_TYPE_TO_PKSE:
                raise SystemExit("species %d carries unmapped Gen 1 type id %d" % (sp, raw))
        t1 = G1_TYPE_TO_PKSE.get(t1raw, 0)
        t2 = G1_TYPE_TO_PKSE.get(t2raw, 0)
        growth = e[0x13]
        rows.append(dict(sp=sp, gratio=e[0x00], hp=e[0x01], atk=e[0x02], dfn=e[0x03], spe=e[0x04],
                         spc=e[0x05], t1=t1, t2=t2, catch=e[0x08], bexp=e[0x09], growth=growth))
        if sp != 0:
            g3 = personal3[sp * PERSONAL_SIZE:(sp + 1) * PERSONAL_SIZE]
            if len(g3) >= PERSONAL_SIZE and g3[0x13] != growth:
                growth_diffs.append((sp, growth, g3[0x13]))

    # --- self-checks: a wrong table here is invisible at runtime -------------------------
    for sp in range(1, MAX_SPECIES_1 + 1):
        idx = n2i[sp]
        if idx == 0 or i2n[idx] != sp:
            raise SystemExit("species %d does not round-trip: national->%02X->national %d"
                             % (sp, idx, i2n[idx] if idx < len(i2n) else -1))
    if len(pp) < 166:
        raise SystemExit("MoveInfo1.PP has %d entries, expected 166 (moves 0-165)" % len(pp))

    return rows, i2n, n2i, pp, growth_diffs, item_names, item_legal


HDR = """/**
 * GENERATED -- do not hand-edit. Generation 1 (Red/Blue/Yellow) data tables.
 *
 * Regenerate with `python tools/gen_gen1.py`. Source of truth is PKHeX.
 *
 * Gen 1 is not a special case of a modern Pokemon, it is a different format that happens to
 * share species names. It has ONE Special stat instead of SpA/SpD, a species index that is a
 * lookup rather than an offset, a type numbering that is neither modern nor contiguous, and its
 * own move PP. It has no abilities, natures, gender, egg groups, friendship, held items, balls,
 * met location, origin game, shininess or Pokerus -- those fields do not exist in the format and
 * must read as ABSENT, never as 0.
 *
 * Type ids in this table are already mapped to PKSE's TYPE_* constants (PokemonTypes.h). The raw
 * Gen 1 ROM numbering -- Bug 7, Ghost 8, Fire 20..Dragon 26, with 6 unused -- never escapes the
 * generator, so no call site can accidentally hand a ROM byte to the type-icon lookup.
 */
#ifndef POKEMON_GEN1TABLES_H
#define POKEMON_GEN1TABLES_H

#include <cstdint>

namespace Pokemon {{

    /// Highest National Dex id Gen 1 knows.
    inline constexpr uint16_t MAX_SPECIES_GEN1 = {MAXSP};

    /// Number of moves Gen 1 defines (1..165; index 0 is the "no move" slot).
    inline constexpr uint16_t MAX_MOVE_GEN1 = {MAXMV};

    // GEN 1'S BASE STATS ARE NOT HERE. They live in PersonalInfo1RBY, with every other
    // group's, one table per save-format group -- including the gender ratio PKHeX substitutes
    // into byte 0x00 of a personal_rb row (Gen 1 has no gender; the value exists only so a
    // transferred Pokemon has one) and the catch rate, which is also the byte that becomes a
    // held item on a trade up to Gen 2. What stays here is what is Gen 1's ALONE and has no
    // counterpart in any other generation: the arbitrary internal species index, the ROM type
    // numbering, Gen 1's own move PP, and its own item id space.

    /**
     * Gen 1 internal species index -> National Dex id, and back.
     *
     * The two orderings are unrelated: Bulbasaur is internal 0x99, Ivysaur is 0x09. Both
     * directions return 0 for an id with no counterpart, which for `g1ToNational` means the
     * index is one of the ~39 unused slots (the "MissingNo." range) and the record is not a
     * real Pokemon.
     */
    uint16_t g1ToNational(uint8_t internalIndex) noexcept;
    uint8_t  nationalToG1(uint16_t species) noexcept;

    /**
     * Base PP for a Gen 1 move id (1..165), before PP Ups. 0 for an unknown id.
     *
     * Gen 1 has its own PP table; a modern lookup returns a different number for the same move.
     * PP Ups add `min(7, basePP / 5)` each, up to 3 -- the Gen 1/2 rule, which is not the modern
     * one either.
     */
    uint8_t getMovePPGen1(uint16_t move) noexcept;

    /// Applies `ppUpCount` (0-3) PP Ups to a Gen 1 move's base PP, per the Gen 1/2 rule.
    uint8_t getMovePPGen1WithUps(uint16_t move, uint8_t ppUpCount) noexcept;

    /**
     * Gen 1 ROM type id -> PKSE TYPE_* id. Returns 255 (TYPE_NONE) for an id Gen 1 never uses.
     *
     * The base-stat rows above are already converted, so this is only for type bytes read out
     * of a PK1 RECORD, which are stored raw. The two numberings are not the same and not even
     * close: Bug is 7 and Ghost 8 (6 is the unused "Bird" type), and the special types jump to
     * 20-26. Passing a raw Gen 1 type byte to anything expecting a modern id draws Bug as
     * Ghost, Ghost as Steel, and every special type off the end of the table.
     */
    uint8_t g1TypeToPKSE(uint8_t romType) noexcept;

    /**
     * PKSE TYPE_* id -> Gen 1 ROM type id. Returns 0 (Normal) for a type Gen 1 does not have.
     *
     * The inverse of g1TypeToPKSE, and needed because a PK1 STORES its types: the games copy
     * them out of the base-stat table when the Pokemon is created, so changing a record's
     * species has to rewrite them or the Pokemon keeps its old typing. Dark, Steel and Fairy
     * have no Gen 1 id at all -- they postdate the generation -- and collapse to Normal, which
     * is the only representable answer.
     */
    uint8_t pkseTypeToG1(uint8_t pkseType) noexcept;

    /// Highest Gen 1 item id. Ids are ONE BYTE and 0xFF terminates a bag, so 254 is the ceiling.
    inline constexpr uint8_t MAX_ITEM_GEN1 = {MAXITEM};

    /**
     * Gen 1 item name by Gen 1 item id. "???" for an id the games never use.
     *
     * Gen 1's item ids are their own space and share nothing with the modern table -- the Gen 1
     * TMs are ordinary bag items at 201-250, which the modern numbering has nothing at. Naming a
     * Gen 1 bag through `Names::getItemName` gives a plausible, wrong answer for every slot, the
     * same way reading its species through the modern dex would.
     */
    const char* getItemNameGen1(uint8_t itemId) noexcept;

    /**
     * May this id legitimately sit in a Gen 1 bag?
     *
     * Narrower than "has a name", on purpose. 84-195 and 251-254 are unused slots with no name at
     * all; 7 and 44 are named (`????? (n)`) but unobtainable; 8 and 9 (Safari Ball, Pokedex) are
     * real named items that are not bag items. The item PICKER offers this set; the DISPLAY names
     * everything, because a save can hold a byte the picker would never offer and the user has to
     * be able to see what it is rather than a blank row.
     */
    bool isItemLegalGen1(uint8_t itemId) noexcept;

}}

#endif  // POKEMON_GEN1TABLES_H
"""


def emit():
    rows, i2n, n2i, pp, growth_diffs, item_names, item_legal = build()

    with open(OUT_H, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(HDR.format(MAXSP=MAX_SPECIES_1, MAXMV=len(pp) - 1, MAXITEM=MAX_ITEM_1))

    def wrap(vals, per, fmt):
        out, line = [], "    "
        for i, v in enumerate(vals):
            line += fmt % v
            if (i + 1) % per == 0:
                out.append(line.rstrip())
                line = "    "
        if line.strip():
            out.append(line.rstrip())
        return "\n".join(out)

    p = []
    p.append('/**\n'
                 ' * GENERATED by tools/gen_gen1.py from PKHeX. Do not hand-edit.\n'
             ' *\n'
             ' * See Gen1Tables.h for why Gen 1 does not share the modern tables.\n'
             ' */\n\n#include "Pokemon/Gen1Tables.h"\n\nnamespace Pokemon {\n\n')

    p.append("namespace {\n\n")

    p.append("    // Gen 1 internal index -> National Dex. 0 marks an unused slot (MissingNo.).\n")
    p.append("    constexpr uint8_t INTERNAL_TO_NATIONAL[] = {\n")
    p.append(wrap(i2n, 16, "%4d,"))
    p.append("\n    };\n\n")

    p.append("    // National Dex -> Gen 1 internal index. 0 marks a species Gen 1 does not have.\n")
    p.append("    constexpr uint8_t NATIONAL_TO_INTERNAL[] = {\n")
    p.append(wrap(n2i[:MAX_SPECIES_1 + 1], 16, "0x%02X,"))
    p.append("\n    };\n\n")

    p.append("    // Base PP per move id, 0..%d. PKHeX MoveInfo1.PP.\n" % (len(pp) - 1))
    p.append("    constexpr uint8_t MOVE_PP[] = {\n")
    p.append(wrap(pp, 20, "%3d,"))
    p.append("\n    };\n\n")

    # Item names: every id gets an entry, including the ones the games never use (empty string ->
    # "???" at the accessor). A save can legitimately contain a byte the picker would never offer,
    # and the display has to name it rather than showing a blank row.
    def cstr(t):
        return '"' + t.replace("\\", "\\\\").replace('"', '\\"') + '"'
    p.append("    // Gen 1 item names by Gen 1 item id, 0..%d. PKHeX text_ItemsG1_en.txt.\n" % MAX_ITEM_1)
    p.append("    // Empty entries are ids the games never use; the accessor reports them as \"???\".\n")
    p.append("    constexpr const char* ITEM_NAMES[] = {\n")
    for i, nm in enumerate(item_names):
        p.append("        %-26s // %3d\n" % (cstr(nm) + ",", i))
    p.append("    };\n")
    p.append("    static_assert(sizeof(ITEM_NAMES) / sizeof(ITEM_NAMES[0]) == %d, "
             "\"Gen 1 item name table must cover every id 0..%d\");\n\n" %
             (MAX_ITEM_1 + 1, MAX_ITEM_1))

    bitmap = [0] * ((MAX_ITEM_1 + 8) // 8)
    for it in sorted(item_legal):
        bitmap[it >> 3] |= 1 << (it & 7)
    p.append("    // Bit set = the id may legitimately sit in a Gen 1 bag (PKHeX ItemStorage1.General).\n")
    p.append("    constexpr uint8_t ITEM_LEGAL[] = {\n")
    p.append(wrap(bitmap, 12, "0x%02X,"))
    p.append("\n    };\n\n}\n\n")

    # The ROM->PKSE type cases come from the one mapping table at the top of this file, so the
    # runtime converter and the pre-converted base-stat rows can never drift apart.
    typecases = "\n".join(
        "        case %2d: return %2d;   // %s" % (rom, pkse, TYPE_NAMES.get(pkse, "?"))
        for rom, pkse in sorted(G1_TYPE_TO_PKSE.items()))
    typecasesinv = "\n".join(
        "        case %2d: return %2d;   // %s" % (pkse, rom, TYPE_NAMES.get(pkse, "?"))
        for pkse, rom in sorted((v, k) for k, v in G1_TYPE_TO_PKSE.items()))

    p.append(("""uint16_t g1ToNational(uint8_t internalIndex) noexcept {
    if (internalIndex >= sizeof(INTERNAL_TO_NATIONAL)) return 0;
    return INTERNAL_TO_NATIONAL[internalIndex];
}

uint8_t nationalToG1(uint16_t species) noexcept {
    if (species >= sizeof(NATIONAL_TO_INTERNAL)) return 0;
    return NATIONAL_TO_INTERNAL[species];
}

uint8_t g1TypeToPKSE(uint8_t romType) noexcept {
    switch (romType) {
__TYPECASES__
        default: return 255;   // TYPE_NONE -- includes Gen 1's unused "Bird" type (6)
    }
}

uint8_t pkseTypeToG1(uint8_t pkseType) noexcept {
    switch (pkseType) {
__TYPECASESINV__
        default: return 0;   // Dark / Steel / Fairy do not exist in Gen 1
    }
}

const char* getItemNameGen1(uint8_t itemId) noexcept {
    const char* n = ITEM_NAMES[itemId];
    return n[0] ? n : "???";
}

bool isItemLegalGen1(uint8_t itemId) noexcept {
    return (ITEM_LEGAL[itemId >> 3] >> (itemId & 7)) & 1;
}

uint8_t getMovePPGen1(uint16_t move) noexcept {
    if (move == 0 || move >= sizeof(MOVE_PP)) return 0;
    return MOVE_PP[move];
}

uint8_t getMovePPGen1WithUps(uint16_t move, uint8_t ppUpCount) noexcept {
    const uint8_t base = getMovePPGen1(move);
    if (base == 0) return 0;
    if (ppUpCount > 3) ppUpCount = 3;
    // Gen 1/2 rule: each PP Up adds min(7, base/5). The modern formula differs.
    const uint8_t perUp = static_cast<uint8_t>(base / 5 > 7 ? 7 : base / 5);
    return static_cast<uint8_t>(base + perUp * ppUpCount);
}

}
""").replace("__TYPECASES__", typecases).replace("__TYPECASESINV__", typecasesinv))

    with open(OUT_CPP, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("".join(p))

    print("Wrote", OUT_H)
    print("Wrote", OUT_CPP)
    print("  species rows : %d (0..%d)" % (len(rows), MAX_SPECIES_1))
    print("  move PP      : %d entries (0..%d)" % (len(pp), len(pp) - 1))
    used = sorted({r["t1"] for r in rows[1:]} | {r["t2"] for r in rows[1:]})
    print("  PKSE type ids used: %s" % ", ".join("%d=%s" % (t, TYPE_NAMES.get(t, "?")) for t in used))
    if growth_diffs:
        print("  NOTE: %d species have a different growth rate in Gen 1 than in Gen 3 --"
              " reusing the modern one would have been wrong:" % len(growth_diffs))
        for sp, g1, g3 in growth_diffs:
            print("        #%-4d gen1=%d gen3=%d" % (sp, g1, g3))
    else:
        print("  growth rates: identical to Gen 3 for all 151 species (emitted in PersonalInfo1RBY)")
    named = sum(1 for n in item_names if n)
    print("  items       : %d ids, %d named, %d bag-legal" % (MAX_ITEM_1 + 1, named, len(item_legal)))


if __name__ == "__main__":
    emit()
