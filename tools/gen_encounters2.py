#!/usr/bin/env python3
"""Generate include/Legality/Encounters/EncounterData2.h + src/Legality/Encounters/EncounterData2.cpp:
PKHeX's Generation 2 (Gold, Silver, Crystal) encounter templates, unmerged and in PKHeX's own order, as the
encounter engine's template classes.

SOURCES (PKHeX, via tools/pkhex_source.py; PKHEX_LOCAL points it at a checkout)
    Resources/legality/wild/Gen2/encounter_{gold,silver,crystal}.pkl   EncounterArea2.GetAreas
    Legality/Encounters/Data/Gen2/Encounters2.cs                       statics, the Odd Egg, trades, CelebiVC
    Resources/legality/gen2/text_tradegsc_{ja,en,fr,it,de,es,ko}.txt   trade nicknames and OT names
    Resources/legality/mgdb/event2.pkl                                 Encounters2GBEra.Gifts

PKSE has no GameVersion for PKHeX's GS lump, so it is emitted as GD: every rule that reads a Gen 2
template's version asks only "is it C, or the GSC lump", and GD answers those as GS does.

Regenerate:  python tools/gen_encounters2.py
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_encounters import WILD, _load, split_blobs_uint32, read_cs, strip_comments, parse_array, parse_template, load_enum  # noqa: E402
from pkhex_source import pkhex_path  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_H = os.path.join(ROOT, "include", "Legality", "Encounters", "EncounterData2.h")
OUT_CPP = os.path.join(ROOT, "src", "Legality", "Encounters", "EncounterData2.cpp")

SOURCE = "Legality/Encounters/Data/Gen2/Encounters2.cs"

VERSION = {"GD": "GD", "SI": "SI", "C": "C", "GS": "GD", "GSC": "GSC"}
SLOT_TYPE = ["Grass", "Surf", "OldRod", "GoodRod", "SuperRod", "RockSmash", "Headbutt", "HeadbuttSpecial", "BugContest"]
SLOT_TYPE_SURF = 1
SPECIES_UNOWN = 201
FORM_RANDOM = 31
# PKHeX GetLanguageStrings(file, 8): language id -> code; 0 and 6 have none
LANGUAGE_CODES = {1: "ja", 2: "en", 3: "fr", 4: "it", 5: "de", 7: "es", 8: "ko"}
LANGUAGE_COUNT = 9
GIFT_SIZE = 12
MOVE = load_enum("Game/Enums/Move.cs", "Move")


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


def move_id(expr):
    expr = expr.strip()
    match = re.fullmatch(r"\(int\)\s*Move\.(\w+)", expr)
    if match:
        return MOVE[match.group(1)]
    return number(expr)


def moveset(expr):
    match = re.fullmatch(r"new\s*\((.*)\)", expr.strip(), re.S)
    if not match:
        raise SystemExit("not a moveset: %r" % expr)
    moves = [move_id(part) for part in match.group(1).split(",")]
    moves += [0] * (4 - len(moves))
    return "Moveset{{%s}}" % ", ".join(str(move) for move in moves)


def individual_values(expr, named):
    expr = expr.strip()
    if expr in named:
        expr = named[expr]
    match = re.fullmatch(r"new\s*\(([^)]*)\)", expr)
    values = [number(part) for part in match.group(1).split(",")]
    return "RNG::IndividualValueSet{%s, true}" % ", ".join(str(value) for value in values)


def shiny(expr):
    return "RNG::Shiny::%s" % {"Random": "Random", "Never": "Never", "Always": "Always"}[expr.strip().split(".")[-1]]


def emit_wild(name, file_name, version):
    """PKHeX EncounterArea2: header (location, time, type, rate); past Surf a rate byte per slot comes first."""
    areas = split_blobs_uint32(_load(WILD("Gen2", file_name)))
    slot_lines, area_lines = [], []
    for area_index, area in enumerate(areas):
        location, time, slot_type, rate = area[0], area[1], area[2], area[3]
        body = area[4:]
        if slot_type > SLOT_TYPE_SURF:
            count = len(body) // 5
            body = body[count:]
        else:
            count = len(body) // 4
        first_slot = len(slot_lines)
        for slot_index in range(count):
            species, slot_number, level_min, level_max = struct.unpack_from("<BBBB", body, slot_index * 4)
            form = FORM_RANDOM if species == SPECIES_UNOWN else 0
            slot_lines.append("                EncounterSlot2(AREAS_%s[%d], %d, %d, %d, %d, %d)," % (name, area_index, species, form, level_min, level_max, slot_number))
        area_lines.append("                EncounterArea2(Enums::GameVersion::%s, SlotType2::%s, %d, %d, %d, &SLOTS_%s[%d], %d)," % (version, SLOT_TYPE[slot_type], time, location, rate, name, first_slot, len(slot_lines) - first_slot))
    return ("            extern const EncounterArea2 AREAS_%s[%d];\n" % (name, len(areas))
            + "            const EncounterSlot2 SLOTS_%s[] = {\n%s\n            };\n" % (name, "\n".join(slot_lines))
            + "            const EncounterArea2 AREAS_%s[%d] = {\n%s\n            };\n" % (name, len(areas), "\n".join(area_lines)))


def static_line(args, props, named):
    props = dict(props)
    fields = [".speciesId = %d" % number(args[0]), ".level = %d" % number(args[1]), ".version = Enums::GameVersion::%s" % VERSION[args[2].strip()]]
    if "Shiny" in props:
        fields.append(".shiny = %s" % shiny(props.pop("Shiny")))
    if "Location" in props:
        fields.append(".location = %d" % number(props.pop("Location")))
    if "Gender" in props:
        fields.append(".gender = %d" % number(props.pop("Gender")))
    if "IVs" in props:
        fields.append(".individualValues = %s" % individual_values(props.pop("IVs"), named))
    if "Moves" in props:
        fields.append(".moves = %s" % moveset(props.pop("Moves")))
    if "IsEgg" in props:
        fields.append(".isEgg = %s" % props.pop("IsEgg").strip())
    if props:
        raise SystemExit("static: unhandled properties %r" % props)
    return "                EncounterStatic2({%s})," % ", ".join(fields)


def emit_statics(text, array_name, name, named):
    lines = [static_line(args, props, named) for args, props in parse_array(text, array_name)]
    return "            const EncounterStatic2 %s[] = {\n%s\n            };\n" % (name, "\n".join(lines))


def read_trade_names():
    names = {}
    for language, code in LANGUAGE_CODES.items():
        with open(pkhex_path("Resources/legality/gen2/text_tradegsc_%s.txt" % code), encoding="utf-8-sig") as handle:
            names[language] = handle.read().replace("\r", "").split("\n")
    return names


def emit_name_arrays(names_by_language, indices):
    lines = []
    for index in indices:
        entries = []
        for language in range(LANGUAGE_COUNT):
            values = names_by_language.get(language, [])
            entries.append(cpp_text(values[index] if index < len(values) else ""))
        lines.append("            const LanguageNames4 TRADE_NAMES_GSC_%d = {%s};" % (index, ", ".join(entries)))
    return "\n".join(lines) + "\n"


def emit_trades(text, names_by_language, named, used):
    lines = []
    half = len(names_by_language[1]) >> 1
    for args, props in parse_array(text, "TradeGift_GSC"):
        index = number(args[1])
        used.update((index, index + half))
        fields = [".nicknames = &TRADE_NAMES_GSC_%d" % index, ".trainerNames = &TRADE_NAMES_GSC_%d" % (index + half), ".speciesId = %d" % number(args[2]),
                  ".level = %d" % number(args[3]), ".TID16 = %d" % number(args[4])]
        if "Shiny" in props:
            fields.append(".shiny = %s" % shiny(props.pop("Shiny")))
        if "Gender" in props:
            fields.append(".gender = %d" % number(props.pop("Gender")))
        if "OTGender" in props:
            fields.append(".trainerGender = %d" % number(props.pop("OTGender")))
        if "IVs" in props:
            fields.append(".individualValues = %s" % individual_values(props.pop("IVs"), named))
        if props:
            raise SystemExit("TradeGift_GSC: unhandled properties %r" % props)
        lines.append("                EncounterTrade2({%s})," % ", ".join(fields))
    return "            const EncounterTrade2 TRADE_GIFT_GSC[] = {\n%s\n            };\n" % "\n".join(lines)


def emit_gifts():
    data = _load(pkhex_path("Resources/legality/mgdb/event2.pkl"))
    if len(data) % GIFT_SIZE:
        raise SystemExit("event2.pkl is not a whole number of gifts")
    lines = []
    for offset in range(0, len(data), GIFT_SIZE):
        record = data[offset:offset + GIFT_SIZE]
        lines.append("                EncounterGift2(%d, %d, Moveset{{%d, %d, %d, %d}}, %d, %d, %d, %d, %d, %d)," % tuple(record))
    return "            const EncounterGift2 GIFTS_GB_ERA[] = {\n%s\n            };\n" % "\n".join(lines)


def main():
    text = strip_comments(read_cs(SOURCE))
    named = dict(re.findall(r"IndividualValueSet\s+(\w+)\s*=>\s*(new\s*\([^)]*\))\s*;", text))
    blocks, accessors = [], []

    def accessor(function, element, array):
        accessors.append((function, element, array))

    for name, file_name, version in (("GD", "encounter_gold.pkl", "GD"), ("SI", "encounter_silver.pkl", "SI"), ("C", "encounter_crystal.pkl", "C")):
        blocks.append(emit_wild(name, file_name, version))
        accessor("getAreas2" + version, "EncounterArea2", "AREAS_" + name)

    for array_name, name, function in (("StaticGSC", "STATIC_GSC", "getStatic2GSC"), ("StaticGS", "STATIC_GS", "getStatic2GS"), ("StaticGD", "STATIC_GD", "getStatic2GD"),
                                       ("StaticSI", "STATIC_SI", "getStatic2SI"), ("StaticC", "STATIC_C", "getStatic2C"), ("StaticOddEggC", "STATIC_ODD_EGG_C", "getStatic2OddEggC")):
        blocks.append(emit_statics(text, array_name, name, named))
        accessor(function, "EncounterStatic2", name)
    args, props = parse_template(text, "CelebiVC")
    blocks.append("            const EncounterStatic2 STATIC_CELEBI_VC[] = {\n%s\n            };\n" % static_line(args, props, named))
    accessor("getStatic2CelebiVirtualConsole", "EncounterStatic2", "STATIC_CELEBI_VC")

    names, used = read_trade_names(), set()
    trades = emit_trades(text, names, named, used)
    blocks.append(emit_name_arrays(names, sorted(used)) + trades)
    accessor("getTradeGift2GSC", "EncounterTrade2", "TRADE_GIFT_GSC")

    blocks.append(emit_gifts())
    accessor("getGifts2GameBoyEra", "EncounterGift2", "GIFTS_GB_ERA")

    declarations = "\n".join("        std::span<const %s> %s() noexcept;" % (element, function) for function, element, _ in accessors)
    definitions = "\n\n".join("        std::span<const %s> %s() noexcept\n        {\n            return %s;\n        }" % (element, function, array) for function, element, array in accessors)

    header = """/**
 * Auto-generated by tools/gen_encounters2.py from PKHeX's Generation 2 encounter data. DO NOT EDIT BY HAND --
 * rerun the generator instead.
 *
 * One accessor per PKHeX array (Encounters2, Encounters2GBEra), unmerged and in PKHeX's order.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERDATA2_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERDATA2_H

#include <span>

#include "Legality/Encounters/EncounterArea2.h"
#include "Legality/Encounters/EncounterGift2.h"
#include "Legality/Encounters/EncounterStatic2.h"
#include "Legality/Encounters/EncounterTrade2.h"

namespace Legality
{
    namespace Encounters
    {
%s
    }
}

#endif
""" % declarations
    source = """// Auto-generated by tools/gen_encounters2.py. DO NOT EDIT BY HAND.
#include "Legality/Encounters/EncounterData2.h"

#include "Legality/Encounters/EncounterSlot2.h"

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
