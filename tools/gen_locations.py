#!/usr/bin/env python3
"""Generate src/Names/LocationNames.cpp from PKHeX met-location text resources.

Met/egg-location names are copied verbatim from PKHeX's `text_*_<bank>_en.txt` files.
A mon carried into a Gen 8/9 save via Pokemon HOME keeps the met/egg-location id it was
given in its ORIGIN game, and that id is named with the ORIGIN GENERATION's table (PKHeX
GameStrings.GetLocationName keys the table off the mon's generation, not the current game).
So a Sun/Moon or Gen 4/5/6 mon sitting in Violet must be named with its own generation's
table -- otherwise it renders "(none)". We therefore emit one table set per generation and
route on the origin version byte.

Ids are BANKED, and the banking scheme differs by generation:
  * Gen 3            LocationSet0:  bank 0 only.
  * Gen 4            LocationSet4:  bank 0, 2000, 3000            (id - base).
  * Gen 5 onward     LocationSet6:  bank 0, 30000, 40000, 60000  (id - base).
Bank 0 is in-world locations; the higher banks are Link Trade / transfers / events / egg
sources. Routing mirrors PKHeX's LocationSet{4,6}.GetLocationName exactly.

Blank slots are preserved (as "") to keep the indexing aligned, and PKHeX's run-of-em-dashes
"no location" sentinel is emitted as "" too.

Regenerate:  python tools/gen_locations.py
Pulls the PKHeX text resources it reads from GitHub on demand (tools/pkhex_source.py);
no local PKHeX checkout required.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pkhex_source import pkhex_path  # noqa: E402
from name_languages import LANGUAGES, ENGLISH_INDEX, IDENTIFIER_SUFFIX  # noqa: E402
OUT = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "src", "Names", "LocationNames.cpp",
)

EMDASH = "—"

# Per-scheme bank bases: which special banks exist and the id each starts at.
SCHEME_BASES = {
    "set0": {0: 0},                                    # Gen 3
    "set4": {0: 0, 2: 2000, 3: 3000},                  # Gen 4
    "set6": {0: 0, 3: 30000, 4: 40000, 6: 60000},      # Gen 5+
}

# (array prefix, human label, switch-case string, scheme, {bank: relpath})
# One table set per generation; version bytes that share a generation share a set.
# HOME-transferred pre-Switch origins (Gen 4-7) are the reason the older sets exist.
GAMES = [
    # GEN 2 IS CRYSTAL'S TABLE AND GOLD/SILVER SHARE IT, because only Crystal writes caught data at
    # all -- a Gold or Silver record answers hasMetData() false and never reaches a name. One bank:
    # Gen 2 has no egg-location or transfer-marker space, its met location is seven bits wide, and
    # PKHeX ships exactly one file for it.
    ("G2",   "Gen 2 Gold/Silver/Crystal (GD=39, SI=40, C=41)", "39: case 40: case 41", "set0", {
        0: "gen2/text_gsc_00000_en.txt"}),
    ("G3",   "Gen 3 R/S/E + FR/LG (SA=1, RU=2, EM=3, FR=4, LG=5)", "1: case 2: case 3: case 4: case 5", "set0", {
        0: "gen3/text_rsefrlg_00000_en.txt"}),
    ("G4",   "Gen 4 D/P/Pt + HG/SS (HG=7, SS=8, D=10, P=11, Pt=12)", "7: case 8: case 10: case 11: case 12", "set4", {
        0: "gen4/text_hgss_00000_en.txt", 2: "gen4/text_hgss_02000_en.txt", 3: "gen4/text_hgss_03000_en.txt"}),
    ("G5",   "Gen 5 B/W + B2/W2 (W=20, B=21, W2=22, B2=23)", "20: case 21: case 22: case 23", "set6", {
        0: "gen5/text_bw2_00000_en.txt", 3: "gen5/text_bw2_30000_en.txt",
        4: "gen5/text_bw2_40000_en.txt", 6: "gen5/text_bw2_60000_en.txt"}),
    ("G6",   "Gen 6 X/Y + OR/AS (X=24, Y=25, AS=26, OR=27)", "24: case 25: case 26: case 27", "set6", {
        0: "gen6/text_xy_00000_en.txt", 3: "gen6/text_xy_30000_en.txt",
        4: "gen6/text_xy_40000_en.txt", 6: "gen6/text_xy_60000_en.txt"}),
    ("G7",   "Gen 7 (3DS) Sun/Moon + Ultra Sun/Moon (SN=30, MN=31, US=32, UM=33)", "30: case 31: case 32: case 33", "set6", {
        0: "gen7/text_sm_00000_en.txt", 3: "gen7/text_sm_30000_en.txt",
        4: "gen7/text_sm_40000_en.txt", 6: "gen7/text_sm_60000_en.txt"}),
    ("GG",   "Let's Go, Pikachu! / Eevee! + GO (GO=34, GP=42, GE=43)", "34: case 42: case 43", "set6", {
        0: "gen7/text_gg_00000_en.txt", 4: "gen7/text_gg_40000_en.txt"}),
    ("SWSH", "Sword / Shield (SW=44, SH=45)", "44: case 45", "set6", {
        0: "gen8/text_swsh_00000_en.txt", 3: "gen8/text_swsh_30000_en.txt",
        4: "gen8/text_swsh_40000_en.txt", 6: "gen8/text_swsh_60000_en.txt"}),
    ("PLA",  "Legends: Arceus (PLA=47)", "47", "set6", {
        0: "gen8a/text_la_00000_en.txt", 3: "gen8a/text_la_30000_en.txt",
        4: "gen8a/text_la_40000_en.txt", 6: "gen8a/text_la_60000_en.txt"}),
    ("BDSP", "Brilliant Diamond / Shining Pearl (BD=48, SP=49)", "48: case 49", "set6", {
        0: "gen8b/text_bdsp_00000_en.txt", 3: "gen8b/text_bdsp_30000_en.txt",
        4: "gen8b/text_bdsp_40000_en.txt", 6: "gen8b/text_bdsp_60000_en.txt"}),
    ("SV",   "Scarlet / Violet (SL=50, VL=51)", "50: case 51", "set6", {
        0: "gen9/text_sv_00000_en.txt", 3: "gen9/text_sv_30000_en.txt",
        4: "gen9/text_sv_40000_en.txt", 6: "gen9/text_sv_60000_en.txt"}),
    ("ZA",   "Legends: Z-A (ZA=52)", "52", "set6", {
        0: "gen9a/text_za_00000_en.txt", 3: "gen9a/text_za_30000_en.txt",
        4: "gen9a/text_za_40000_en.txt", 6: "gen9a/text_za_60000_en.txt"}),
]


def arr_name(prefix, bank):
    return f"{prefix}_LOC_NAMES" if bank == 0 else f"{prefix}_LOC_NAMES_{bank}"


def load_entries(path):
    # utf-8-SIG, because PKHeX's location files carry a byte-order mark and plain "utf-8" keeps it
    # as a character. It then rode into entry 0 of every table that has a real name there -- ten
    # tables, nine languages each -- so getMetLocationName(HG, 0) answered "﻿Mystery Zone",
    # an invisible glyph the UI font has no business being asked to draw. Same family as the rule
    # for decoding these files by their BOM rather than guessing an encoding.
    with open(path, encoding="utf-8-sig") as fh:
        lines = fh.read().split("\n")
    if lines and lines[-1] == "":
        lines = lines[:-1]
    return ["" if (l and all(c == EMDASH for c in l)) else l for l in lines]


def esc(s):
    return s.replace("\\", "\\\\").replace('"', '\\"')


def main():
    p = []
    p.append('/**\n'
             ' * Auto-generated by tools/gen_locations.py from PKHeX\'s met-location text resources.\n'
             ' *\n'
             ' * ONE table set per generation. A HOME-transferred Pokemon keeps its origin id and is named\n'
             ' * with its ORIGIN generation\'s table, so a Sun/Moon or Gen 4/5/6 Pokemon in a Gen 9 save is\n'
             ' * routed to its own table (else it read "(none)"). getMetLocationName dispatches on the\n'
             ' * origin version byte -> generation table, then banks by id:\n'
             ' *   Gen 3      bank 0 only.\n'
             ' *   Gen 4      bank 0 / 2000 / 3000            (id - base).\n'
             ' *   Gen 5+     bank 0 / 30000 / 40000 / 60000  (id - base).\n'
             ' * Mirrors PKHeX LocationSet{0,4,6}.GetLocationName.\n'
             ' *\n'
             ' * Blank slots and PKHeX\'s em-dash "no location" sentinel are emitted as "".\n'
             ' */\n\n')
    p.append('#include "Names/LocationNames.h"\n')
    p.append('#include "Names/NameLanguage.h"\n\n')
    p.append('#include <cstddef>\n')
    p.append('#include <cstdint>\n\n')
    p.append("namespace Names {\n")

    counts = {}

    def emit_array(prefix, bank, label, scheme, relpath):
        # One table per language, each carrying the per-entry id comment. That comment is what
        # lets a human check a name against the id the save actually stores, and it is wanted most
        # in the languages a reader cannot scan by eye -- dropping it anywhere to keep the file
        # smaller would remove it exactly where it earns its keep.
        perLanguage = []
        for lang in LANGUAGES:
            langPath = relpath.replace("_en.txt", "_%s.txt" % lang)
            perLanguage.append(load_entries(pkhex_path("Resources/text/locations/" + langPath)))
        entries = perLanguage[ENGLISH_INDEX]
        entryCount = len(entries)
        for languageIndex, rows in enumerate(perLanguage):
            if len(rows) != entryCount:
                raise SystemExit("%s bank %d: %s has %d entries, English has %d -- both are indexed "
                                 "by the same location id and must match"
                                 % (prefix, bank, LANGUAGES[languageIndex], len(rows), entryCount))
        counts[(prefix, bank)] = entryCount
        base = SCHEME_BASES[scheme][bank]
        arr = arr_name(prefix, bank)
        note = "" if bank == 0 else f"  [ids {base}+]"
        p.append(f"\n    // {label} -- bank {bank}{note}\n")
        p.append(f"    // Source: PKHeX {os.path.basename(relpath)}  ({entryCount} entries x {len(LANGUAGES)} languages)\n")
        for languageIndex, rows in enumerate(perLanguage):
            suffix = IDENTIFIER_SUFFIX[LANGUAGES[languageIndex]]
            p.append(f"    static const char* const {arr}_{suffix}[] = {{\n")
            for i, e in enumerate(rows):
                p.append(f'        "{esc(e)}",  // {base + i}\n')
            p.append("    };\n")
        p.append(f"    static const char* const* const {arr}_BY_LANGUAGE[] = {{\n")
        for lang in LANGUAGES:
            p.append(f"        {arr}_{IDENTIFIER_SUFFIX[lang]},\n")
        p.append("    };\n")
        p.append(f'    static_assert(sizeof({arr}_BY_LANGUAGE) / sizeof({arr}_BY_LANGUAGE[0]) == LANGUAGE_COUNT, "{arr} must carry one table per language");\n')

    # Bank-0 (in-world) tables first, then the special banks each set has.
    for prefix, label, _cases, scheme, banks in GAMES:
        emit_array(prefix, 0, label, scheme, banks[0])
    for prefix, label, _cases, scheme, banks in GAMES:
        for bank in sorted(b for b in banks if b != 0):
            emit_array(prefix, bank, label, scheme, banks[bank])

    p.append("\n"
             "    // names[id] when in range, else \"\" (also \"\" for an absent bank: table=nullptr, count=0).\n"
             "    static const char* lookup(const char* const* table, size_t count, uint16_t locationId) {\n"
             "        return (table && locationId < count) ? table[locationId] : \"\";\n"
             "    }\n\n")
    p.append("    // LocationSet6 banking (Gen 5+): id / 10000 selects the bank.\n"
             "    static const char* lookupBanked(const char* const* bank0, size_t count0, const char* const* bank3, size_t count3, const char* const* bank4, size_t count4, const char* const* bank6, size_t count6, uint16_t locationId) {\n"
             "        if (locationId >= 60000) return lookup(bank6, count6, static_cast<uint16_t>(locationId - 60000));\n"
             "        if (locationId >= 40000) return lookup(bank4, count4, static_cast<uint16_t>(locationId - 40000));\n"
             "        if (locationId >= 30000) return lookup(bank3, count3, static_cast<uint16_t>(locationId - 30000));\n"
             "        return lookup(bank0, count0, locationId);\n"
             "    }\n\n")
    p.append("    // LocationSet4 banking (Gen 4): banks at 0 / 2000 / 3000.\n"
             "    static const char* lookupBanked4(const char* const* bank0, size_t count0, const char* const* bank2, size_t count2, const char* const* bank3, size_t count3, uint16_t locationId) {\n"
             "        if (locationId >= 3000) return lookup(bank3, count3, static_cast<uint16_t>(locationId - 3000));\n"
             "        if (locationId >= 2000) return lookup(bank2, count2, static_cast<uint16_t>(locationId - 2000));\n"
             "        return lookup(bank0, count0, locationId);\n"
             "    }\n\n")

    def ref(prefix, banks, bank):
        if bank in banks:
            arr = arr_name(prefix, bank)
            # Count comes from the English table because every language's is asserted equal to it.
            return (f"{arr}_BY_LANGUAGE[displayLanguageIndex()], "
                    f"sizeof({arr}_EN) / sizeof({arr}_EN[0])")
        return "nullptr, 0"

    p.append("    const char* getMetLocationName(uint8_t originVersion, uint16_t locationId) {\n")
    p.append("        switch (originVersion) {\n")
    for prefix, _label, cases, scheme, banks in GAMES:
        p.append(f"            case {cases}:\n")
        if scheme == "set0":
            arr = arr_name(prefix, 0)
            p.append(f"                return lookup({arr}_BY_LANGUAGE[displayLanguageIndex()], sizeof({arr}_EN) / sizeof({arr}_EN[0]), locationId);\n")
        elif scheme == "set4":
            args = ", ".join(ref(prefix, banks, b) for b in (0, 2, 3))
            p.append(f"                return lookupBanked4({args}, locationId);\n")
        else:
            args = ", ".join(ref(prefix, banks, b) for b in (0, 3, 4, 6))
            p.append(f"                return lookupBanked({args}, locationId);\n")
    p.append("            default:\n")
    p.append('                return "";\n')
    p.append("        }\n")
    p.append("    }\n")

    p.append("\n    // Raw bank-0 (in-world) name table for an origin, so a picker can enumerate the\n"
             "    // user-assignable met locations. The special banks are trade/transfer/egg markers.\n")
    p.append("    LocationTable getLocationTable(uint8_t originVersion) {\n")
    p.append("        switch (originVersion) {\n")
    for prefix, _label, cases, _scheme, _banks in GAMES:
        arr = arr_name(prefix, 0)
        p.append(f"            case {cases}:\n")
        p.append(f"                return {{ {arr}_BY_LANGUAGE[displayLanguageIndex()], sizeof({arr}_EN) / sizeof({arr}_EN[0]) }};\n")
    p.append("            default:\n")
    p.append("                return { nullptr, 0 };\n")
    p.append("        }\n")
    p.append("    }\n")

    # Bank 6 -- who an egg came FROM. This is what a hatched egg's location field actually holds, so
    # the EGG picker enumerates it; the met picker still shows bank 0 alone, because those are not
    # places a Pokemon is met. Issue #112: BDSP's "Nursery Couple" was nameable but not selectable.
    p.append("\n    // Raw bank-6 (egg received-from) table for an origin, so the EGG LOCATION picker\n"
             "    // can enumerate it. Empty for the origins with no such bank (Gen 3, Gen 4 -- which\n"
             "    // banks its ids differently -- and Let's Go).\n")
    p.append("    LocationTable getEggSourceLocationTable(uint8_t originVersion) {\n")
    p.append("        switch (originVersion) {\n")
    for prefix, _label, cases, _scheme, banks in GAMES:
        if 6 not in banks:
            continue
        arr = arr_name(prefix, 6)
        p.append(f"            case {cases}:\n")
        p.append(f"                return {{ {arr}_BY_LANGUAGE[displayLanguageIndex()], sizeof({arr}_EN) / sizeof({arr}_EN[0]) }};\n")
    p.append("            default:\n")
    p.append("                return { nullptr, 0 };\n")
    p.append("        }\n")
    p.append("    }\n")
    p.append("}\n")

    with open(OUT, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("".join(p))

    print("Wrote", OUT)
    for prefix, _label, _cases, _scheme, banks in GAMES:
        summary = ", ".join(f"b{b}={counts[(prefix, b)]}" for b in sorted(banks))
        print(f"  {prefix}: {summary}")


if __name__ == "__main__":
    main()
