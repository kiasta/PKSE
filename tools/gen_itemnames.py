#!/usr/bin/env python3
"""Generate src/Names/ItemNames.cpp -- the modern (Gen 4+) and Gen 3 item name tables.

Replaces a hand-maintained file. Before the swap both English tables were compared against PKHeX
line by line: the 377-entry Gen 3 table matched exactly, and the 2685-entry modern table matched at
every id BUT EIGHT -- 420..427, where PKSE said TM93..TM100.

THOSE EIGHT WERE A REAL BUG, not a naming preference. Ids 420..427 are the HM block of the Gen 4
item space, and they are reachable: POUCH_DP_TMHM, POUCH_PT_TMHM, POUCH_HGSS_TMHM and
POUCH_BDSP_TMs each hold exactly 100 contiguous ids 328..427 -- TM01..TM92 followed by HM01..HM08.
So a Diamond/Pearl/Platinum/HeartGold/SoulSilver/BDSP player's TM&HM pouch showed "TM93" where the
game shows "HM01" (Cut), through to "TM98"/HM06. The later generations confirm the reading from the
other side: POUCH_BW_TMHM stops the block at 425 because Gen 5 has only six HMs, and POUCH_ORAS_TMHM
carries 420..425 plus 737 -- and 737 is exactly where PKHeX names HM07.

HM07 AND HM08 ARE SYNTHESISED, because PKHeX cannot supply them at these ids. Its flat table is
Gen 5-aligned in this range: 426 and 427 are "???", and HM07 lives at 737 with no HM08 anywhere in
any language. So id 426 takes id 737's string verbatim (exact, not invented) and id 427 increments
its trailing digit. That works in every language because all nine spell an HM as a prefix plus a
two-digit number -- HM07, CS07, VM07, ひでんマシン０７, 비전머신07, 秘传学习器０７ -- and the
generator ASSERTS the string ends in a 7 (ASCII or fullwidth) rather than guessing, so an upstream
change breaks the build instead of silently emitting HM07 twice.

Regenerate:  python tools/gen_itemnames.py
Pulls the PKHeX text resources from GitHub on demand (tools/pkhex_source.py); no local checkout.
Note the Gen 3 resource is UTF-16; the modern one is UTF-8 with a BOM.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pkhex_source import pkhex_path  # noqa: E402
from name_languages import LANGUAGES, ENGLISH_INDEX, emit_tables  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "src", "Names", "ItemNames.cpp")

MODERN_TEMPLATE = "Resources/text/items/text_Items_{lang}.txt"
GEN3_TEMPLATE = "Resources/text/items/gen3/text_ItemsG3_{lang}.txt"

# Gen 4 HM ids PKHeX's flat table cannot name, and where to take the wording from.
HM07_ID = 426
HM08_ID = 427
HM07_SOURCE_ID = 737   # PKHeX names HM07 here (the Gen 6 id ORAS's pouch actually uses)

# Gen 3 item id -> Gen 4+ item id. PKHeX ItemConverter.Item3to4; 128 (NaN) = no equivalent.
ITEM3_TO_MODERN = [
      0,   1,   2,   3,   4,   5,   6,   7,   8,   9,  10,  11,  12,  17,  18,  19,
     20,  21,  22,  23,  24,  25,  26,  27,  28,  29,  30,  31,  32,  33,  34,  35,
     36,  37,  38,  39,  40,  41,  42,  65,  66,  67,  68,  69,  43,  44,  70,  71,
     72,  73,  74,  75, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,  45,
     46,  47,  48,  49,  50,  51,  52,  53, 128,  55,  56,  57,  58,  59,  60,  61,
     63,  64, 128,  76,  77,  78,  79, 128, 128, 128, 128, 128, 128,  80,  81,  82,
     83,  84,  85, 128, 128, 128, 128,  86,  87, 128,  88,  89,  90,  91,  92,  93,
    128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
    128, 128, 128, 128, 128, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159,
    160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175,
    176, 177, 178, 179, 180, 181, 182, 183, 201, 202, 203, 204, 205, 206, 207, 208,
    128, 128, 128, 213, 214, 215, 216, 217, 218, 219, 220, 221, 222, 223, 224, 225,
    226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 239, 240, 241,
    242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254, 255, 256, 257,
    258, 259, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
    128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 260, 261,
    262, 263, 264, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
    128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
    128, 328, 329, 330, 331, 332, 333, 334, 335, 336, 337, 338, 339, 340, 341, 342,
    343, 344, 345, 346, 347, 348, 349, 350, 351, 352, 353, 354, 355, 356, 357, 358,
    359, 360, 361, 362, 363, 364, 365, 366, 367, 368, 369, 370, 371, 372, 373, 374,
    375, 376, 377,
]

ITEM3_CONVERTERS = """    uint16_t itemG3ToModern(uint16_t g3Id)
    {
        if (g3Id >= ITEM3_COUNT)
            return 0;
        const uint16_t modernId = ITEM3_TO_MODERN[g3Id];
        return (modernId == ITEM3_NAN) ? 0 : modernId;
    }

    uint16_t itemModernToG3(uint16_t modernId)
    {
        if (modernId == 0 || modernId == ITEM3_NAN)
            return 0;
        for (uint16_t i = 0; i < ITEM3_COUNT; ++i)
            if (ITEM3_TO_MODERN[i] == modernId)
                return i;
        return 0; // no Gen 3 equivalent -> drop
    }
"""

GET_ITEM_NAME_FOR = """    const char *getItemNameFor(Enums::GameVersion group, uint16_t itemId)
    {
        switch (group)
        {
        case Enums::GameVersion::RBY:
            // Gen 1 ids are one byte; anything above that is not an id this game can hold.
            return itemId <= ::Pokemon::MAX_ITEM_GEN1 ? ::Pokemon::getItemNameGen1(static_cast<uint8_t>(itemId)) : "???";
        case Enums::GameVersion::GSC:
        case Enums::GameVersion::GD:
        case Enums::GameVersion::SI:
        case Enums::GameVersion::C:
            // Gen 2 ids are one byte and their own space, like Gen 1's. Its TMs are ordinary
            // bag items at 191-249, which the modern table has nothing at.
            return itemId <= ::Pokemon::MAX_ITEM_GEN2 ? ::Pokemon::getItemNameGen2(static_cast<uint8_t>(itemId)) : "???";
        case Enums::GameVersion::FRLG:
        case Enums::GameVersion::RSE:
            // All five GBA games share one item id space, so one table names them all.
            return getItemNameG3(itemId);
        default:
            // Gen 4 onward all share the modern numbering -- PKHeX has one item list from
            // Gen 4 up, and converts only Gens 1-3 into it. So DP/Pt/HGSS, BW/B2W2, XY/ORAS
            // and SM/USUM need no table of their own.
            return getItemName(itemId);
        }
    }
"""



def load_rows(path):
    """Decode a PKHeX text resource, choosing the encoding BY ITS BOM.

    These files are not uniform: the Gen 3 item list is UTF-16 while the modern one is UTF-8, and
    it varies per language too. Trying utf-16 first and falling back on UnicodeDecodeError looks
    reasonable and is wrong -- almost any byte sequence of even length IS valid UTF-16, so the
    French and Spanish UTF-8 files decoded "successfully" into one line of CJK-looking garbage
    rather than raising. The BOM is the only reliable discriminator.
    """
    with open(path, "rb") as handle:
        raw = handle.read()
    if raw[:2] in (b"\xff\xfe", b"\xfe\xff"):
        text = raw.decode("utf-16")
    else:
        text = raw.decode("utf-8-sig")
    # A BOM can also turn up INSIDE a line -- the Spanish Gen 3 list once read "(﻿Ningun)" --
    # and it is never a character any game draws.
    text = text.replace("﻿", "")
    rows = text.replace("\r\n", "\n").split("\n")
    if rows and rows[-1] == "":
        rows = rows[:-1]
    if not rows:
        raise SystemExit("text resource was empty: " + path)
    return rows


def nextHiddenMachineName(hm07Name, lang):
    """HM08 from HM07 by incrementing the trailing digit. See the module docstring."""
    if hm07Name.endswith("7"):
        return hm07Name[:-1] + "8"
    if hm07Name.endswith("\uff17"):          # fullwidth 7, used by ja/zh
        return hm07Name[:-1] + "\uff18"
    raise SystemExit("%s: HM07 is %r, which does not end in a 7 -- the HM08 derivation in "
                     "gen_itemnames.py no longer holds and must be revisited" % (lang, hm07Name))


def main():
    modern = []
    for lang in LANGUAGES:
        rows = load_rows(pkhex_path(MODERN_TEMPLATE.format(lang=lang)))
        hm07 = rows[HM07_SOURCE_ID]
        rows[HM07_ID] = hm07
        rows[HM08_ID] = nextHiddenMachineName(hm07, lang)
        modern.append(rows)
    gen3 = [load_rows(pkhex_path(GEN3_TEMPLATE.format(lang=lang))) for lang in LANGUAGES]

    p = []
    p.append("// AUTO-GENERATED item name tables. Regenerate with tools/gen_itemnames.py.\n")
    p.append("// Source: PKHeX.Core/%s and %s, one table per language.\n"
             % (MODERN_TEMPLATE, GEN3_TEMPLATE))
    p.append("//\n")
    p.append("// Ids %d and %d are the Gen 4 HM07/HM08 slots, which PKHeX's flat table leaves as\n"
             % (HM07_ID, HM08_ID))
    p.append("// \"???\" -- it is Gen 5-aligned there. They are filled from id %d (HM07) and its\n"
             % HM07_SOURCE_ID)
    p.append("// successor; see the generator's docstring for why, and for the bug that made it\n")
    p.append("// necessary (these eight ids used to read TM93..TM100 in every Gen 4 pouch).\n")
    p.append("#include <cstdint>\n")
    p.append("#include <cstddef>\n")
    p.append("\n")
    p.append('#include "Names/ItemNames.h"\n')
    p.append('#include "Names/NameLanguage.h"\n')
    p.append('#include "Pokemon/Gen1Tables.h"\n')
    p.append('#include "Pokemon/Gen2Tables.h"\n')
    p.append("\n")
    p.append("namespace Names\n")
    p.append("{\n")
    modernCount = emit_tables(p, "ITEM_NAMES", modern)
    p.append("    const char *getItemName(uint16_t itemId)\n")
    p.append("    {\n")
    p.append("        // Out of range OR a genuinely-unnamed slot (a handful of ids are blank even in\n")
    p.append("        // PKHeX) -> a visible marker rather than an empty string, so a data gap never\n")
    p.append("        // renders as a blank tile (the exact failure that made Z-A key items show up empty).\n")
    p.append("        constexpr size_t count = sizeof(ITEM_NAMES_EN) / sizeof(ITEM_NAMES_EN[0]);\n")
    p.append("        const char *const *table = ITEM_NAMES_BY_LANGUAGE[displayLanguageIndex()];\n")
    p.append("        if (itemId >= count || table[itemId][0] == '\\0')\n")
    p.append('            return "???";\n')
    p.append("        return table[itemId];\n")
    p.append("    }\n")
    p.append("\n")
    p.append("    size_t getItemCount() { return %d; }\n" % modernCount)
    p.append("\n")
    p.append("    // Gen 3 (GBA) item id <-> modern (Gen 4+) item id, PKHeX ItemConverter.Item3to4;\n")
    p.append("    // 128 (NaN) = none.\n")
    p.append("    static const uint16_t ITEM3_TO_MODERN[] = {\n")
    for i in range(0, len(ITEM3_TO_MODERN), 16):
        p.append("        " + " ".join("%3d," % v for v in ITEM3_TO_MODERN[i:i + 16]) + "\n")
    p.append("    };\n")
    p.append("    static constexpr size_t ITEM3_COUNT = sizeof(ITEM3_TO_MODERN) / sizeof(ITEM3_TO_MODERN[0]);\n")
    p.append("    static constexpr uint16_t ITEM3_NAN = 128; // \"no modern equivalent\" sentinel\n")
    p.append("\n")
    p.append(ITEM3_CONVERTERS)
    p.append("\n")
    gen3Count = emit_tables(p, "ITEM3_NAMES", gen3)
    p.append("    static constexpr size_t ITEM3_NAMES_COUNT = %d;\n" % gen3Count)
    p.append("\n")
    p.append("    const char *getItemNameG3(uint16_t g3Id)\n")
    p.append("    {\n")
    p.append("        if (g3Id >= ITEM3_NAMES_COUNT)\n")
    p.append('            return "???";\n')
    p.append("        return ITEM3_NAMES_BY_LANGUAGE[displayLanguageIndex()][g3Id];\n")
    p.append("    }\n")
    p.append("\n")
    p.append("    size_t getItemCountG3() { return ITEM3_NAMES_COUNT; }\n")
    p.append("\n")
    p.append(GET_ITEM_NAME_FOR)
    p.append("}\n")

    with open(OUT, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("".join(p))
    print("Wrote", OUT, "with", modernCount, "modern +", gen3Count, "Gen 3 entries x",
          len(LANGUAGES), "languages")


if __name__ == "__main__":
    main()
