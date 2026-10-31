#!/usr/bin/env python3
"""Generate include/Utils/Gen2Text.h from PKHeX's StringConverter2 tables.

Gen 2 keeps Gen 1's SHAPE -- one byte per glyph, 0x50 terminator, 0x5D in the first position
marking an in-game-trade OT -- but not Gen 1's TABLE. The Latin block is the same; what differs is
0xBA-0xCF (accented characters, reorganised when the European releases landed) and 0xD0-0xD6,
which Gen 2 uses for LIGATURES -- single bytes standing for two characters.

The ligatures are emitted as their PKHeX code points (the full-width forms PKHeX substitutes) and
deliberately NOT expanded here. Expanding them is a display concern, and a table that silently
turned one stored byte into two characters could not round-trip: re-encoding would have to guess
which pair to re-combine. Preserving the byte keeps the round trip exact; the pair is a
presentation problem for later, and is noted as deferred.

The sibling of Gen1Text.h / Gen3Text.h / Gen4Text.h: one table, one header, every call site routed
through it.
"""
from __future__ import annotations

import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pkhex_source import pkhex_path  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(REPO, "include", "Utils", "Gen2Text.h")


CONSTS: dict[str, int] = {}


def collect_consts() -> None:
    """Resolve `const char X = ...` across StringConverter1 and 2.

    Gen 2's table is written almost entirely in named constants that ALIAS into
    StringConverter1 (`NUL = StringConverter1.NUL`), so both files have to be read and the
    aliases chased. Doing it by hand is how a table ends up one glyph out.
    """
    raw: dict[str, str] = {}
    for f in ("PKM/Strings/StringConverter1.cs", "PKM/Strings/StringConverter2.cs"):
        with open(pkhex_path(f), encoding="utf-8") as fh:
            t = fh.read()
        for m in re.finditer(r"const char (\w+)\s*=\s*([^;]+);", t):
            raw.setdefault(m.group(1), m.group(2).strip())

    def resolve(name: str, depth: int = 0) -> int:
        if depth > 8:
            raise SystemExit(f"alias loop resolving {name}")
        v = raw.get(name)
        if v is None:
            raise SystemExit(f"undefined char constant {name}")
        v = v.split("//")[0].strip()
        if v.startswith("'"):
            lit = v[1:-1]
            if lit.startswith("\\u"):
                return int(lit[2:], 16)
            if lit.startswith("\\"):
                return {"n": 10, "t": 9, "r": 13, "0": 0, "\\": 92, "'": 39}[lit[1]]
            return ord(lit)
        if v.startswith("(char)"):
            inner = v[len("(char)"):].strip()
            return int(inner, 0) if re.fullmatch(r"0[xX][0-9A-Fa-f]+|\d+", inner) else resolve(inner.split(".")[-1], depth + 1)
        return resolve(v.split(".")[-1], depth + 1)

    for k in raw:
        try:
            CONSTS[k] = resolve(k)
        except SystemExit:
            pass   # a constant these two tables never reference


def parse_table(text: str, name: str) -> list[int]:
    m = re.search(rf"{name}\s*=>\s*\[(.*?)\n    \];", text, re.S)
    if not m:
        raise SystemExit(f"could not find {name}")
    body = re.sub(r"//[^\n]*", "", m.group(1))
    consts = CONSTS
    out: list[int] = []
    for tok in re.findall(r"'(?:\\u[0-9A-Fa-f]{4}|\\.|[^'])'|\b[A-Z][A-Z0-9_]*\b", body):
        if tok.startswith("'"):
            lit = tok[1:-1]
            if lit.startswith("\\u"):
                out.append(int(lit[2:], 16))
            elif lit.startswith("\\"):
                out.append({"n": 10, "t": 9, "r": 13, "0": 0, "\\": 92, "'": 39}[lit[1]])
            else:
                out.append(ord(lit))
        elif tok == "NUL":
            out.append(0)
        elif tok in consts:
            out.append(consts[tok])
        else:
            raise SystemExit(f"{name}: unknown symbol {tok!r}")
    if len(out) != 256:
        raise SystemExit(f"{name}: got {len(out)} entries, expected 256")
    return out


def emit(name: str, vals: list[int]) -> str:
    rows = []
    for i in range(0, 256, 8):
        rows.append("        " + " ".join("0x%04X," % v for v in vals[i:i + 8])
                    + "   // %02X-%02X" % (i, i + 7))
    return "    inline constexpr char16_t %s[256] = {\n%s\n    };\n" % (name, "\n".join(rows))


def main() -> int:
    collect_consts()
    with open(pkhex_path("PKM/Strings/StringConverter2.cs"), encoding="utf-8") as fh:
        text = fh.read()

    en = parse_table(text, "TableEN")
    jp = parse_table(text, "TableJP")

    # Pin the letters and digits: a misparse shifts the whole block and every name reads wrong.
    if en[0x80] != ord("A") or en[0xA0] != ord("a") or en[0xF6] != ord("0"):
        raise SystemExit(f"TableEN misparsed: 0x80={en[0x80]:#x} 0xA0={en[0xA0]:#x} 0xF6={en[0xF6]:#x}")
    print(f"  TableEN / TableJP parsed, 256 entries each")
    print(f"  ligature block 0xD0-0xD6: {[hex(v) for v in en[0xD0:0xD7]]}")

    h = []
    h.append("/**\n")
    h.append(" * Auto-generated by tools/gen_gen2text.py from PKHeX's StringConverter2.\n")
    h.append(" * DO NOT EDIT BY HAND -- rerun the generator instead.\n")
    h.append(" *\n")
    h.append(" * Same SHAPE as Gen 1 -- one byte per glyph, 0x50 terminates, 0x5D in the first\n")
    h.append(" * position marks an in-game-trade OT -- but NOT the same table. The Latin block\n")
    h.append(" * matches; 0xBA-0xCF (accented characters) and 0xD0-0xD6 (ligatures) do not.\n")
    h.append(" *\n")
    h.append(" * LIGATURES are single bytes standing for two characters. They are mapped to their\n")
    h.append(" * PKHeX code points and NOT expanded: expanding one stored byte into two characters\n")
    h.append(" * cannot round-trip, because re-encoding would have to guess which pair to recombine.\n")
    h.append(" * Preserving the byte keeps the round trip exact. Rendering the pair is deferred.\n")
    h.append(" *\n")
    h.append(" * INTERNATIONAL MEANS ENGLISH, as in PKHeX. StringConverter2 also has TableFRE (French and\n")
    h.append(" * German) and TableITA (Italian and Spanish), but a Gen 2 save does not record its language,\n")
    h.append(" * and PKHeX loads every international one as English (SaveUtil: new SAV2(data, English)). So\n")
    h.append(" * PKSE uses only the English table too. PKSM guesses the language from the OT name; PKSE\n")
    h.append(" * deliberately does not.\n")
    h.append(" */\n")
    h.append("#ifndef UTILS_GEN2TEXT_H\n#define UTILS_GEN2TEXT_H\n\n")
    h.append("#include <cstdint>\n#include <string>\n\n")
    h.append("namespace Utils {\n\n")
    h.append("    inline constexpr uint8_t GEN2_TERMINATOR = 0x50;\n")
    h.append("    /// First byte of an in-game-trade OT name. Legality-relevant, not a character.\n")
    h.append("    inline constexpr uint8_t GEN2_TRADE_OT   = 0x5D;\n")
    h.append("    inline constexpr int GEN2_NAME_LEN_INT = 11;\n")
    h.append("    inline constexpr int GEN2_NAME_LEN_JP  = 6;\n\n")
    h.append("    /// 0 means the byte has no glyph. Unlike Gen 3, a glyphless byte TERMINATES the\n")
    h.append("    /// string here rather than being skipped -- the same rule Gen 1 follows.\n")
    h.append(emit("GEN2_EN", en))
    h.append("\n")
    h.append(emit("GEN2_JP", jp))
    h.append("""
    inline char16_t gen2ToChar(uint8_t b, bool japanese) noexcept {
        return japanese ? GEN2_JP[b] : GEN2_EN[b];
    }

    /// 0 when the character has no Gen 2 glyph. Callers must REFUSE such a name rather than write
    /// it truncated -- the Gen 3 lesson: silently dropping characters is invisible damage.
    inline uint8_t charToGen2(char16_t c, bool japanese) noexcept {
        const char16_t* tbl = japanese ? GEN2_JP : GEN2_EN;
        for (int i = 0; i < 256; ++i) {
            if (tbl[i] == c && c != 0) return static_cast<uint8_t>(i);
        }
        return 0;
    }

    inline bool gen2CanEncode(const std::u16string& s, bool japanese) noexcept {
        for (char16_t c : s) {
            if (charToGen2(c, japanese) == 0) return false;
        }
        return true;
    }
}

#endif  // UTILS_GEN2TEXT_H
""")
    with open(OUT, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("".join(h))
    print(f"wrote {os.path.relpath(OUT, REPO)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
