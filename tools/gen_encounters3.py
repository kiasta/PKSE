#!/usr/bin/env python3
"""Generate include/Legality/Encounters/EncounterData3.h + src/Legality/Encounters/EncounterData3.cpp:
PKHeX's Generation 3 (Ruby, Sapphire, Emerald, FireRed, LeafGreen) encounter templates, unmerged and in
PKHeX's own order, as the encounter engine's template classes.

SOURCES (PKHeX, pinned by tools/pkhex_source.py; PKHEX_LOCAL points it at a checkout)
    Resources/legality/wild/Gen3/encounter_{r,s,e,fr,lg}.pkl     EncounterArea3.GetAreas
    Resources/legality/wild/Gen3/encounter_rse_swarm.pkl         EncounterArea3.GetAreasSwarm
    Legality/Encounters/Data/Gen3/Encounters3RSE.cs, Encounters3FRLG.cs   statics, trades, Colosseum gifts
    Resources/legality/gen3/text_trade{rse,frlg}_{ja,en,fr,it,de,es}.txt  trade nicknames and OT names
    Legality/Encounters/Data/Gen3/EncountersWC3.cs               Encounter_WC3 (event gifts)

    Resources/legality/wild/Gen3/encounter_pcny.pkl              EncountersWC3.PCNY (EncounterGift3NY.GetArray)
    Legality/Encounters/Templates/Gen3/Gifts/Distribution3JPN.cs   EncountersWC3.PCJP (Gen3PCJP.GetArray)

Regenerate:  python tools/gen_encounters3.py
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_encounters import (WILD, CORE, _load, split_blobs_uint32, read_cs, strip_comments,  # noqa: E402
                            parse_array, split_top)
from pkhex_source import pkhex_path  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_H = os.path.join(ROOT, "include", "Legality", "Encounters", "EncounterData3.h")
OUT_CPP = os.path.join(ROOT, "src", "Legality", "Encounters", "EncounterData3.cpp")

RSE_SOURCE = "Legality/Encounters/Data/Gen3/Encounters3RSE.cs"
FRLG_SOURCE = "Legality/Encounters/Data/Gen3/Encounters3FRLG.cs"
WC3_SOURCE = "Legality/Encounters/Data/Gen3/EncountersWC3.cs"
PCJP_SOURCE = "Legality/Encounters/Templates/Gen3/Gifts/Distribution3JPN.cs"

# PKHeX GameVersion token -> PKSE Enums::GameVersion (a single game, or the PKSE group standing for a lump)
# PKSE has no Ruby/Sapphire-only group, so PKHeX's RS lump stands as RSE (no template here matches on it).
VERSION = {"R": "RU", "S": "SA", "E": "EM", "FR": "FR", "LG": "LG", "RS": "RSE", "RSE": "RSE", "FRLG": "FRLG"}
# PKHeX GameVersion token -> Version3Mask
VERSION_MASK = {"R": "VERSION3_RUBY", "S": "VERSION3_SAPPHIRE", "E": "VERSION3_EMERALD", "FR": "VERSION3_FIRERED",
                "LG": "VERSION3_LEAFGREEN", "RS": "VERSION3_RUBY_SAPPHIRE", "FRLG": "VERSION3_FIRERED_LEAFGREEN",
                "EFL": "VERSION3_EMERALD_FIRERED_LEAFGREEN", "Gen3": "VERSION3_ALL"}
SLOT_TYPE = ["Grass", "Surf", "OldRod", "GoodRod", "SuperRod", "RockSmash", "SwarmGrass50", "SwarmFish50"]
ABILITY = {"OnlyFirst": "OnlyFirst", "OnlySecond": "OnlySecond", "OnlyHidden": "OnlyHidden", "Any12": "Any12", "Any12H": "Any12H"}
# PKHeX PIDType token -> PKSE RNG::PIDType
PID_TYPE = {"BACD_U": "BACDUnrestricted", "BACD_R": "BACDRestricted", "BACD_R_A": "BACDRestrictedAntishiny",
            "BACD_U_AX": "BACDUnrestrictedAntishinyXor", "BACD_TA": "BACDTableAntishiny", "BACD_TS": "BACDTableShiny",
            "BACD_RBCD": "BACDRestrictedBinaryCodedDecimal", "BACD_M": "BACDMystryMew", "Channel": "Channel",
            "Method_2": "Method2", "Method_1": "Method1"}
SHINY = {"Random": "Random", "Never": "Never", "Always": "Always"}
GIFT_GENDER = {"Recipient", "Only0", "Only1", "RandAlgo", "RandD3", "RandS3", "RandS7", "RandSG15", "RandD3_0", "RandD3_1"}
LANGUAGE = {"Japanese": 1, "English": 2, "French": 3, "Italian": 4, "German": 5, "Spanish": 7, "Korean": 8}
# PKHeX GetLanguageStrings: language id -> code; 0 and 6 have none
LANGUAGE_CODES = {1: "ja", 2: "en", 3: "fr", 4: "it", 5: "de", 7: "es"}


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


def csharp_string(expr, constants):
    expr = expr.strip()
    if expr in constants:
        return constants[expr]
    match = re.fullmatch(r'"((?:[^"\\]|\\.)*)"', expr)
    if not match:
        raise SystemExit("not a string literal: %r" % expr)
    return match.group(1)


def number(expr):
    expr = expr.strip()
    if expr.lower().startswith("0x"):
        return int(expr, 16)
    return int(expr, 10)


def moveset(expr):
    match = re.fullmatch(r"new\s*\(([^)]*)\)", expr.strip())
    if not match:
        raise SystemExit("not a moveset: %r" % expr)
    moves = [number(part) for part in match.group(1).split(",")]
    moves += [0] * (4 - len(moves))
    return "Moveset{{%s}}" % ", ".join(str(move) for move in moves)


def individual_values(expr):
    match = re.fullmatch(r"new\s*\(([^)]*)\)", expr.strip())
    values = [number(part) for part in match.group(1).split(",")]
    return "RNG::IndividualValueSet{%s, true}" % ", ".join(str(value) for value in values)


def boolean(expr):
    return {"true": "true", "false": "false"}[expr.strip()]


# ------------------------------------------------------------------------------------------- wild
def emit_wild(name, file_name, version, swarm=False):
    """PKHeX EncounterArea3: header (location, _, type, rate), then 10-byte slots (14 for a swarm)."""
    areas = split_blobs_uint32(_load(WILD("Gen3", file_name)))
    slot_lines, area_lines = [], []
    slot_class = "EncounterSlot3Swarm" if swarm else "EncounterSlot3"
    slot_size = 14 if swarm else 10
    for area_index, area in enumerate(areas):
        location, slot_type, rate = area[0], (6 if swarm else area[2]), area[3]
        body = area[4:]
        first_slot = len(slot_lines)
        for offset in range(0, len(body) - slot_size + 1, slot_size):
            if swarm:
                species, _, slot_number, level_min, level_max = struct.unpack_from("<HBBBB", body, offset)
                moves = struct.unpack_from("<4H", body, offset + 6)
                slot_lines.append("                EncounterSlot3Swarm(AREAS_%s[%d], %d, %d, %d, %d, Moveset{{%s}})," % (name, area_index, species, level_min, level_max, slot_number, ", ".join(str(move) for move in moves)))
            else:
                species, form, slot_number, level_min, level_max, magnet_index, magnet_count, static_index, static_count = struct.unpack_from("<HBBBBBBBB", body, offset)
                slot_lines.append("                EncounterSlot3(AREAS_%s[%d], %d, %d, %d, %d, %d, %d, %d, %d, %d)," % (name, area_index, species, form, level_min, level_max, slot_number, magnet_index, magnet_count, static_index, static_count))
        area_lines.append("                EncounterArea3(Enums::GameVersion::%s, RNG::SlotType3::%s, %d, %d, &SLOTS_%s[%d], %d)," % (version, SLOT_TYPE[slot_type], rate, location, name, first_slot, len(slot_lines) - first_slot))
    return ("            extern const EncounterArea3 AREAS_%s[%d];\n" % (name, len(areas))
            + "            const %s SLOTS_%s[] = {\n%s\n            };\n" % (slot_class, name, "\n".join(slot_lines))
            + "            const EncounterArea3 AREAS_%s[%d] = {\n%s\n            };\n" % (name, len(areas), "\n".join(area_lines))), len(areas)


# ---------------------------------------------------------------------------------------- statics
def emit_statics(text, array_name, name):
    lines = []
    for args, props in parse_array(text, array_name):
        species, level, version = number(args[0]), number(args[1]), args[2].strip()
        fields = [".speciesId = %d" % species, ".level = %d" % level, ".version = Enums::GameVersion::%s" % VERSION[version], ".location = %d" % number(props.pop("Location"))]
        if "Form" in props:
            fields.append(".formId = %d" % number(props.pop("Form")))
        if "FixedBall" in props:
            fields.append(".fixedBall = Enums::Ball::%s" % props.pop("FixedBall").split(".")[-1])
        if "IsRoaming" in props:
            fields.append(".isRoaming = %s" % boolean(props.pop("IsRoaming")))
        if "FatefulEncounter" in props:
            fields.append(".isFatefulEncounter = %s" % boolean(props.pop("FatefulEncounter")))
        if "IsEgg" in props:
            fields.append(".isEgg = %s" % boolean(props.pop("IsEgg")))
        if "Moves" in props:
            fields.append(".moves = %s" % moveset(props.pop("Moves")))
        if props:
            raise SystemExit("%s: unhandled properties %r" % (array_name, props))
        lines.append("                EncounterStatic3({%s})," % ", ".join(fields))
    return "            const EncounterStatic3 %s[] = {\n%s\n            };\n" % (name, "\n".join(lines)), len(lines)


# ----------------------------------------------------------------------------------------- trades
def read_trade_names(resource):
    names = {}
    for language, code in LANGUAGE_CODES.items():
        path = pkhex_path("Resources/legality/gen3/text_%s_%s.txt" % (resource, code))
        with open(path, encoding="utf-8-sig") as handle:
            names[language] = handle.read().replace("\r", "").split("\n")
    return names


def emit_name_arrays(prefix, names_by_language, indices):
    """One LanguageNames per index; language ids 0 and 6 are PKHeX's empty lists."""
    lines = []
    for index in indices:
        entries = []
        for language in range(8):
            values = names_by_language.get(language, [])
            entries.append(cpp_text(values[index]) if index < len(values) else "nullptr")
        lines.append("            const LanguageNames %s_%d = {%s};" % (prefix, index, ", ".join(entries)))
    return "\n".join(lines) + "\n"


def emit_trades(text, array_name, name, names_prefix, names_by_language, contests, used_indices):
    lines = []
    half = len(names_by_language[1]) >> 1
    for args, props in parse_array(text, array_name):
        index, version, pid, species, level = number(args[1]), args[2].strip(), number(args[3]), number(args[4]), number(args[5])
        used_indices.update((index, index + half))
        contest = contests[props.pop("Contest").strip()]
        fields = [".nicknames = &%s_%d" % (names_prefix, index), ".trainerNames = &%s_%d" % (names_prefix, index + half),
                  ".version = Enums::GameVersion::%s" % VERSION[version], ".PID = 0x%08X" % pid, ".speciesId = %d" % species, ".level = %d" % level,
                  ".ability = RNG::AbilityPermission::%s" % ABILITY[props.pop("Ability").strip()], ".TID16 = %d" % number(props.pop("TID16"))]
        if "SID16" in props:
            fields.append(".SID16 = %d" % number(props.pop("SID16")))
        fields.append(".trainerGender = %d" % number(props.pop("OTGender")))
        fields.append(".gender = %d" % number(props.pop("Gender")))
        fields.append(".individualValues = %s" % individual_values(props.pop("IVs")))
        fields.append(".contestStats = {%s}" % ", ".join(str(value) for value in contest))
        if props:
            raise SystemExit("%s: unhandled properties %r" % (array_name, props))
        lines.append("                EncounterTrade3({%s})," % ", ".join(fields))
    return "            const EncounterTrade3 %s[] = {\n%s\n            };\n" % (name, "\n".join(lines)), len(lines)


def read_contests(text):
    out = {}
    for match in re.finditer(r"ReadOnlySpan<byte>\s+(TradeContest_\w+)\s*=>\s*\[([^\]]*)\]", text):
        out[match.group(1)] = [number(part) for part in match.group(2).split(",")]
    return out


# ----------------------------------------------------------------------------- Colosseum gifts
def emit_colo_gifts(text, array_name, name, trainer_arrays):
    lines = []
    for args, props in parse_array(text, array_name):
        species, level, trainers, version = number(args[0]), number(args[1]), args[2].strip(), args[3].strip()
        fields = [".speciesId = %d" % species, ".level = %d" % level, ".trainerNames = &COLOSSEUM_%s" % trainer_constant(trainers),
                  ".version = Enums::GameVersion::%s" % VERSION[version], ".location = %d" % number(props.pop("Location")),
                  ".TID16 = %d" % number(props.pop("TID16")), ".trainerGender = %d" % number(props.pop("OriginalTrainerGender"))]
        if "Moves" in props:
            fields.append(".moves = %s" % moveset(props.pop("Moves")))
        if props:
            raise SystemExit("%s: unhandled properties %r" % (array_name, props))
        lines.append("                EncounterGift3Colo({%s})," % ", ".join(fields))
    return "            const EncounterGift3Colo %s[] = {\n%s\n            };\n" % (name, "\n".join(lines)), len(lines)


def trainer_constant(identifier):
    """PKHeX `TrainersPikachu` -> `TRAINERS_PIKACHU`."""
    return re.sub(r"(?<!^)(?=[A-Z])", "_", identifier).upper()


def emit_colo_trainers(text):
    lines = []
    for match in re.finditer(r"string\[\]\s+(Trainers\w+)\s*=\s*\[([^\]]*)\]", text):
        values = [csharp_string(part.replace("string.Empty", '""'), {}) for part in split_top(match.group(2))]
        entries = [cpp_text(value) for value in values] + ["nullptr"] * (8 - len(values))
        lines.append("            const LanguageNames COLOSSEUM_%s = {%s};" % (trainer_constant(match.group(1)), ", ".join(entries[:8])))
    return "\n".join(lines) + "\n"


# ------------------------------------------------------------------------------------------ WC3
def emit_wc3(text):
    constants = {}
    for match in re.finditer(r"const\s+string\s+(\w+)\s*=\s*\"((?:[^\"\\]|\\.)*)\"", text):
        constants[match.group(1)] = match.group(2)
    lines = []
    for array_name in ("Common", "International", "Japan", "Eggs"):
        for args, props in parse_array(text, array_name):
            species, level, version = number(args[0]), number(args[1]), args[2].strip()
            is_egg = False
            met_level = level
            if len(args) >= 4:
                is_egg = boolean(args[3]) == "true"
                met_level = 0
                if len(args) >= 5:
                    met_level = number(args[4].split(":")[-1])
            fields = [".speciesId = %d" % species, ".level = %d" % level, ".versionMask = %s" % VERSION_MASK[version],
                      ".isEgg = %s" % ("true" if is_egg else "false"), ".metLevel = %d" % met_level,
                      ".method = RNG::PIDType::%s" % PID_TYPE[props.pop("Method").strip()], ".moves = %s" % moveset(props.pop("Moves")),
                      ".originalTrainerName = %s" % cpp_text(csharp_string(props.pop("OriginalTrainerName"), constants))]
            if "OriginalTrainerGender" in props:
                gender = props.pop("OriginalTrainerGender").strip()
                if gender not in GIFT_GENDER:
                    raise SystemExit("unknown GiftGender3 %r" % gender)
                fields.append(".originalTrainerGender = GiftGender3::%s" % gender)
            tid, sid = None, None
            if "ID32" in props:
                id32 = number(props.pop("ID32"))
                tid, sid = id32 & 0xFFFF, id32 >> 16
            if "TID16" in props:
                tid = number(props.pop("TID16"))
            if "SID16" in props:
                sid = number(props.pop("SID16"))
            if tid is not None:
                fields.append(".TID16 = %d" % tid)
            if sid is not None:
                fields.append(".SID16 = %d" % sid)
            if "Language" in props:
                fields.append(".language = %d" % LANGUAGE[props.pop("Language").split(")")[-1].strip()])
            if "Shiny" in props:
                fields.append(".shiny = RNG::Shiny::%s" % SHINY[props.pop("Shiny").strip()])
            if "FatefulEncounter" in props:
                fields.append(".isFatefulEncounter = %s" % boolean(props.pop("FatefulEncounter")))
            if "RibbonNational" in props:
                fields.append(".hasNationalRibbon = %s" % boolean(props.pop("RibbonNational")))
            if props:
                raise SystemExit("%s: unhandled properties %r" % (array_name, props))
            lines.append("                EncounterGift3({%s})," % ", ".join(fields))
    return "            const EncounterGift3 GIFTS_WC3[] = {\n%s\n            };\n" % "\n".join(lines), len(lines)


# ------------------------------------------------------------------------------------- PCNY / PCJP
# PKHeX Distribution3NY / Distribution3JPN, in PKHeX's enum order.
DISTRIBUTIONS_NY = ("Evolution", "Dragon", "Monster", "Halloween", "EXDragon", "UnknownSpring", "Colosseum", "Box",
                    "BabyTrade", "SlitherSwim", "AncientAliens", "Sixth")
DISTRIBUTIONS_JPN = ("First", "Second", "Third", "Fourth", "Fifth", "Sixth")


def emit_pcny():
    """PKHeX EncounterGift3NY.GetArray: 12-byte records (species, level, distribution, four moves)."""
    data = _load(WILD("Gen3", "encounter_pcny.pkl"))
    record_size = 12
    lines = []
    for offset in range(0, len(data) - record_size + 1, record_size):
        species, level, distribution = struct.unpack_from("<HBB", data, offset)
        moves = struct.unpack_from("<4H", data, offset + 4)
        lines.append("                EncounterGift3NY({%d, Distribution3NY::%s, %d, Moveset{{%s}}}),"
                     % (species, DISTRIBUTIONS_NY[distribution], level, ", ".join(str(move) for move in moves)))
    return "            const EncounterGift3NY GIFTS_PCNY[] = {\n%s\n            };\n" % "\n".join(lines)


def emit_pcjp(text):
    """PKHeX Gen3PCJP.GetArray: each distribution's species list, in order."""
    lines = []
    for distribution_index, distribution in enumerate(DISTRIBUTIONS_JPN):
        match = re.search(r"Distro%d\s*=>\s*\[([^\]]*)\]" % (distribution_index + 1), text)
        if match is None:
            sys.exit("Distribution3JPN.cs: Distro%d not found" % (distribution_index + 1))
        for species in (number(entry) for entry in match.group(1).split(",") if entry.strip()):
            lines.append("                EncounterGift3JPN({%d, Distribution3JPN::%s})," % (species, distribution))
    return "            const EncounterGift3JPN GIFTS_PCJP[] = {\n%s\n            };\n" % "\n".join(lines)


def main():
    rse = strip_comments(read_cs(RSE_SOURCE))
    frlg = strip_comments(read_cs(FRLG_SOURCE))
    wc3 = strip_comments(read_cs(WC3_SOURCE))

    blocks, accessors = [], []

    def accessor(function, element, array):
        accessors.append((function, element, array))

    for name, function, file_name, version in (("SWARM_RSE", "SwarmRSE", "encounter_rse_swarm.pkl", "RSE"), ("R", "R", "encounter_r.pkl", "RU"), ("S", "S", "encounter_s.pkl", "SA"),
                                               ("E", "E", "encounter_e.pkl", "EM"), ("FR", "FR", "encounter_fr.pkl", "FR"), ("LG", "LG", "encounter_lg.pkl", "LG")):
        block, _ = emit_wild(name, file_name, version, swarm=name.startswith("SWARM"))
        blocks.append(block)
        accessor("getAreas3" + function, "EncounterArea3", "AREAS_" + name)

    blocks.append(emit_colo_trainers(rse))
    for array_name, name in (("ColoGiftsR", "COLOSSEUM_GIFTS_R"), ("ColoGiftsS", "COLOSSEUM_GIFTS_S")):
        block, _ = emit_colo_gifts(rse, array_name, name, None)
        blocks.append(block)
        accessor("getColosseumGifts3%s" % name[-1], "EncounterGift3Colo", name)

    for text, array_name, name in ((rse, "StaticRSE", "STATIC_RSE"), (rse, "StaticR", "STATIC_R"), (rse, "StaticS", "STATIC_S"), (rse, "StaticE", "STATIC_E"),
                                   (frlg, "StaticFRLG", "STATIC_FRLG"), (frlg, "StaticFR", "STATIC_FR"), (frlg, "StaticLG", "STATIC_LG")):
        block, _ = emit_statics(text, array_name, name)
        blocks.append(block)
        accessor("getStatic3%s" % name.split("_", 1)[1], "EncounterStatic3", name)

    for text, resource, prefix, arrays in ((rse, "traderse", "TRADE_NAMES_RSE", (("TradeGift_RS", "TRADE_GIFT_RS"), ("TradeGift_E", "TRADE_GIFT_E"))),
                                           (frlg, "tradefrlg", "TRADE_NAMES_FRLG", (("TradeGift_FR", "TRADE_GIFT_FR"), ("TradeGift_LG", "TRADE_GIFT_LG"), ("TradeGift_FRLG", "TRADE_GIFT_FRLG")))):
        names = read_trade_names(resource)
        contests = read_contests(text)
        used = set()
        trade_blocks = []
        for array_name, name in arrays:
            block, _ = emit_trades(text, array_name, name, prefix, names, contests, used)
            trade_blocks.append(block)
            accessor("getTradeGift3%s" % name.split("_", 2)[2], "EncounterTrade3", name)
        blocks.append(emit_name_arrays(prefix, names, sorted(used)))
        blocks.extend(trade_blocks)

    block, _ = emit_wc3(wc3)
    blocks.append(block)
    accessor("getGifts3WC3", "EncounterGift3", "GIFTS_WC3")
    blocks.append(emit_pcny())
    accessor("getGifts3PCNY", "EncounterGift3NY", "GIFTS_PCNY")
    blocks.append(emit_pcjp(strip_comments(read_cs(PCJP_SOURCE))))
    accessor("getGifts3PCJP", "EncounterGift3JPN", "GIFTS_PCJP")

    declarations = "\n".join("        std::span<const %s> %s() noexcept;" % (element, function) for function, element, _ in accessors)
    definitions = "\n\n".join("        std::span<const %s> %s() noexcept\n        {\n            return %s;\n        }" % (element, function, array) for function, element, array in accessors)

    header = """/**
 * Auto-generated by tools/gen_encounters3.py from PKHeX's Generation 3 encounter data. DO NOT EDIT BY HAND --
 * rerun the generator instead.
 *
 * One accessor per PKHeX array (Encounters3RSE, Encounters3FRLG, EncountersWC3 with its PCNY and PCJP), unmerged and in PKHeX's order.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERDATA3_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERDATA3_H

#include <span>

#include "Legality/Encounters/EncounterArea3.h"
#include "Legality/Encounters/EncounterGift3.h"
#include "Legality/Encounters/EncounterGift3Colo.h"
#include "Legality/Encounters/EncounterGift3JPN.h"
#include "Legality/Encounters/EncounterGift3NY.h"
#include "Legality/Encounters/EncounterStatic3.h"
#include "Legality/Encounters/EncounterTrade3.h"

namespace Legality
{
    namespace Encounters
    {
%s
    }
}

#endif
""" % declarations
    source = """// Auto-generated by tools/gen_encounters3.py. DO NOT EDIT BY HAND.
#include "Legality/Encounters/EncounterData3.h"

#include "Legality/Encounters/EncounterSlot3.h"

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
