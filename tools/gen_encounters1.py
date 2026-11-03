#!/usr/bin/env python3
"""Generate include/Legality/Encounters/EncounterData1.h + src/Legality/Encounters/EncounterData1.cpp:
PKHeX's Generation 1 (Red, Green, Blue, Yellow) encounter templates, unmerged and in PKHeX's own order,
as the encounter engine's template classes.

SOURCES (PKHeX, via tools/pkhex_source.py; PKHEX_LOCAL points it at a checkout)
    Resources/legality/wild/Gen1/encounter_{red,blue,yellow,blue_jp}.pkl   EncounterArea1.GetAreas
    Legality/Encounters/Data/Gen1/Encounters1.cs                           statics and in-game trades
    Resources/legality/gen1/text_traderby_{ja,en,fr,it,de,es}.txt          trade nicknames
    Resources/legality/mgdb/event1.pkl                                     Encounters1GBEra.Gifts
    Legality/Encounters/Data/Gen1/Encounters1VC.cs                         the Virtual Console Mew
    Resources/byte/personal/personal_y                                     PersonalTable.Y catch rates

PKSE has no GameVersion for PKHeX's RB lump, so it is emitted as RD: every rule that reads a Gen 1
template's version asks only "is it YW, BU, or does it contain YW", and RD answers those as RB does.

Regenerate:  python tools/gen_encounters1.py
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_encounters import WILD, _load, split_blobs_uint32, read_cs, strip_comments, parse_array  # noqa: E402
from pkhex_source import pkhex_path  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_H = os.path.join(ROOT, "include", "Legality", "Encounters", "EncounterData1.h")
OUT_CPP = os.path.join(ROOT, "src", "Legality", "Encounters", "EncounterData1.cpp")

SOURCE = "Legality/Encounters/Data/Gen1/Encounters1.cs"
VC_SOURCE = "Legality/Encounters/Data/Gen1/Encounters1VC.cs"

VERSION = {"RD": "RD", "GN": "GN", "BU": "BU", "YW": "YW", "RB": "RD", "RBY": "RBY"}
SLOT_TYPE = ["Grass", "Surf", "OldRod", "GoodRod", "SuperRod"]
# PKHeX GetLanguageStrings(file, 7): language id -> code; 0 and 6 have none
LANGUAGE_CODES = {1: "ja", 2: "en", 3: "fr", 4: "it", 5: "de", 7: "es"}
LANGUAGE_COUNT = 8
GIFT_SIZE = 8
PERSONAL_SIZE_1 = 0x1C
PERSONAL_CATCH_RATE_1 = 0x08


def cpp_text(text):
    out = []
    for character in text:
        code = ord(character)
        if character == '"' or character == "\\":
            out.append("\\" + character)
        elif 0x20 <= code < 0x7F:
            out.append(character)
        else:
            out.append("\\u%04X" % code)
    return 'u"' + "".join(out) + '"'


def number(expr):
    expr = expr.strip()
    if expr.lower().startswith("0x"):
        return int(expr, 16)
    return int(expr, 10)


def emit_wild(name, file_name, version):
    """PKHeX EncounterArea1: header (location, _, type, rate), then 4-byte slots (species, slot, min, max)."""
    areas = split_blobs_uint32(_load(WILD("Gen1", file_name)))
    slot_lines, area_lines = [], []
    for area_index, area in enumerate(areas):
        location, slot_type, rate = area[0], area[2], area[3]
        body = area[4:]
        first_slot = len(slot_lines)
        for offset in range(0, (len(body) // 4) * 4, 4):
            species, slot_number, level_min, level_max = struct.unpack_from("<BBBB", body, offset)
            slot_lines.append("                EncounterSlot1(AREAS_%s[%d], %d, %d, %d, %d)," % (name, area_index, species, level_min, level_max, slot_number))
        area_lines.append("                EncounterArea1(Enums::GameVersion::%s, SlotType1::%s, %d, %d, &SLOTS_%s[%d], %d)," % (version, SLOT_TYPE[slot_type], location, rate, name, first_slot, len(slot_lines) - first_slot))
    return ("            extern const EncounterArea1 AREAS_%s[%d];\n" % (name, len(areas))
            + "            const EncounterSlot1 SLOTS_%s[] = {\n%s\n            };\n" % (name, "\n".join(slot_lines))
            + "            const EncounterArea1 AREAS_%s[%d] = {\n%s\n            };\n" % (name, len(areas), "\n".join(area_lines)))


def emit_statics(text, array_name, name):
    lines = []
    for args, props in parse_array(text, array_name):
        if props or len(args) != 3:
            raise SystemExit("%s: unexpected entry %r %r" % (array_name, args, props))
        lines.append("                EncounterStatic1(%d, %d, Enums::GameVersion::%s)," % (number(args[0]), number(args[1]), VERSION[args[2].strip()]))
    return "            const EncounterStatic1 %s[] = {\n%s\n            };\n" % (name, "\n".join(lines))


def read_trade_names():
    names = {}
    for language, code in LANGUAGE_CODES.items():
        with open(pkhex_path("Resources/legality/gen1/text_traderby_%s.txt" % code), encoding="utf-8-sig") as handle:
            names[language] = handle.read().replace("\r", "").split("\n")
    return names


def emit_name_arrays(names_by_language, indices):
    """PKHeX EncounterUtil.GetNamesForLanguage: one entry per language, empty where a language lacks it."""
    lines = []
    for index in indices:
        entries = []
        for language in range(LANGUAGE_COUNT):
            values = names_by_language.get(language, [])
            entries.append(cpp_text(values[index] if index < len(values) else ""))
        lines.append("            const LanguageNames TRADE_NAMES_RBY_%d = {%s};" % (index, ", ".join(entries)))
    return "\n".join(lines) + "\n"


def emit_trades(text, array_name, name, used):
    lines = []
    for args, props in parse_array(text, array_name):
        index, species, version = number(args[1]), number(args[2]), VERSION[args[3].strip()]
        level_rby = number(args[4])
        level_gsc = number(args[5]) if len(args) > 5 else level_rby
        evolve = props.pop("EvolveOnTrade", "false").strip()
        if props:
            raise SystemExit("%s: unhandled properties %r" % (array_name, props))
        used.add(index)
        lines.append("                EncounterTrade1(&TRADE_NAMES_RBY_%d, %d, Enums::GameVersion::%s, %d, %d, %s)," % (index, species, version, level_rby, level_gsc, evolve))
    return "            const EncounterTrade1 %s[] = {\n%s\n            };\n" % (name, "\n".join(lines))


def gift_line(data):
    return "                EncounterGift1(%d, %d, Moveset{{%d, %d, %d, %d}}, %d, %d)," % tuple(data[:GIFT_SIZE])


def emit_gifts():
    data = _load(pkhex_path("Resources/legality/mgdb/event1.pkl"))
    if len(data) % GIFT_SIZE:
        raise SystemExit("event1.pkl is not a whole number of gifts")
    lines = [gift_line(data[offset:offset + GIFT_SIZE]) for offset in range(0, len(data), GIFT_SIZE)]
    return "            const EncounterGift1 GIFTS_GB_ERA[] = {\n%s\n            };\n" % "\n".join(lines)


def emit_virtual_console_gift(text):
    match = re.search(r"Gift\s*=\s*new\s*\(\s*\[([^\]]*)\]\s*\)", text)
    values = [number(part) for part in match.group(1).split(",")]
    return "            const EncounterGift1 GIFT_VIRTUAL_CONSOLE[] = {\n%s\n            };\n" % gift_line(values)


def emit_yellow_catch_rates():
    data = _load(pkhex_path("Resources/byte/personal/personal_y"))
    rates = [data[species * PERSONAL_SIZE_1 + PERSONAL_CATCH_RATE_1] for species in range(len(data) // PERSONAL_SIZE_1)]
    return "            const uint8_t CATCH_RATES_YELLOW[%d] = {%s};\n" % (len(rates), ", ".join(str(rate) for rate in rates)), len(rates)


def main():
    text = strip_comments(read_cs(SOURCE))
    blocks, accessors = [], []

    def accessor(function, element, array):
        accessors.append((function, element, array))

    for name, file_name, version in (("RD", "encounter_red.pkl", "RD"), ("GN", "encounter_blue.pkl", "GN"), ("YW", "encounter_yellow.pkl", "YW"), ("BU", "encounter_blue_jp.pkl", "BU")):
        blocks.append(emit_wild(name, file_name, version))
        accessor("getAreas1" + version, "EncounterArea1", "AREAS_" + name)

    for array_name, name, function in (("StaticRBY", "STATIC_RBY", "getStatic1RBY"), ("StaticRB", "STATIC_RB", "getStatic1RB"), ("StaticYW", "STATIC_YW", "getStatic1YW"), ("StaticBU", "STATIC_BU", "getStatic1BU")):
        blocks.append(emit_statics(text, array_name, name))
        accessor(function, "EncounterStatic1", name)

    names, used, trade_blocks = read_trade_names(), set(), []
    for array_name, name, function in (("TradeGift_RB", "TRADE_GIFT_RB", "getTradeGift1RB"), ("TradeGift_YW", "TRADE_GIFT_YW", "getTradeGift1YW"), ("TradeGift_BU", "TRADE_GIFT_BU", "getTradeGift1BU")):
        trade_blocks.append(emit_trades(text, array_name, name, used))
        accessor(function, "EncounterTrade1", name)
    blocks.append(emit_name_arrays(names, sorted(used)) + "".join(trade_blocks))

    blocks.append(emit_gifts())
    accessor("getGifts1GameBoyEra", "EncounterGift1", "GIFTS_GB_ERA")
    blocks.append(emit_virtual_console_gift(strip_comments(read_cs(VC_SOURCE))))
    accessor("getGifts1VirtualConsole", "EncounterGift1", "GIFT_VIRTUAL_CONSOLE")

    catch_block, catch_count = emit_yellow_catch_rates()
    blocks.append(catch_block)

    declarations = "\n".join("        std::span<const %s> %s() noexcept;" % (element, function) for function, element, _ in accessors)
    definitions = "\n\n".join("        std::span<const %s> %s() noexcept\n        {\n            return %s;\n        }" % (element, function, array) for function, element, array in accessors)
    definitions += """

        uint8_t getCatchRateYellow(uint16_t speciesId) noexcept
        {
            return speciesId < %d ? CATCH_RATES_YELLOW[speciesId] : 0;
        }""" % catch_count

    header = """/**
 * Auto-generated by tools/gen_encounters1.py from PKHeX's Generation 1 encounter data. DO NOT EDIT BY HAND --
 * rerun the generator instead.
 *
 * One accessor per PKHeX array (Encounters1, Encounters1GBEra, Encounters1VC), unmerged and in PKHeX's order,
 * plus PersonalTable.Y's catch rates (PKSE's Gen 1 personal table is Red/Blue's).
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERDATA1_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERDATA1_H

#include <cstdint>
#include <span>

#include "Legality/Encounters/EncounterArea1.h"
#include "Legality/Encounters/EncounterGift1.h"
#include "Legality/Encounters/EncounterStatic1.h"
#include "Legality/Encounters/EncounterTrade1.h"

namespace Legality
{
    namespace Encounters
    {
%s
        /// PKHeX PersonalTable.Y[species].CatchRate; 0 past the table.
        uint8_t getCatchRateYellow(uint16_t speciesId) noexcept;
    }
}

#endif
""" % declarations
    source = """// Auto-generated by tools/gen_encounters1.py. DO NOT EDIT BY HAND.
#include "Legality/Encounters/EncounterData1.h"

#include "Legality/Encounters/EncounterSlot1.h"

namespace Legality
{
    namespace Encounters
    {
        namespace
        {
            // clang-format off
%s            // clang-format on
        }

%s
    }
}
""" % ("".join(blocks), definitions)
    with open(OUT_H, "w", newline="\n") as handle:
        handle.write(header)
    with open(OUT_CPP, "w", newline="\n") as handle:
        handle.write(source)
    print("wrote %s (%d arrays)" % (os.path.relpath(OUT_CPP, ROOT), len(accessors)))


if __name__ == "__main__":
    main()
