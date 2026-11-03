#!/usr/bin/env python3
"""Generate include/Legality/Encounters/EncounterData4.h + src/Legality/Encounters/EncounterData4.cpp:
PKHeX's Generation 4 (Diamond, Pearl, Platinum, HeartGold, SoulSilver) encounter templates, unmerged and
in PKHeX's own order, as the encounter engine's template classes.

SOURCES (PKHeX, via tools/pkhex_source.py; PKHEX_LOCAL points it at a checkout)
    Resources/legality/wild/Gen4/encounter_{d,p,pt,hg,ss}.pkl   EncounterArea4.GetAreas
    Resources/legality/wild/Gen4/encounter_walker4.pkl         EncounterStatic4Pokewalker.GetAll
    Legality/Encounters/Data/Gen4/Encounters4DPPt.cs, Encounters4HGSS.cs   statics, Ranch gifts, trades
    Resources/legality/gen4/text_trade{dppt,hgss}_{ja,en,fr,it,de,es,ko}.txt  trade nicknames and OT names
    Resources/legality/mgdb/wc4.pkl                            EncounterEvent.MGDB_G4 (Wonder Cards)
    PKM/Strings/StringConverter4Util.cs                        the Gen 4 text table (gift OT names)

Only the Wonder Cards that give a Pokemon are emitted: the others (items, seals, Poketch apps...) have
species 0 and can never match an evolution chain.

Regenerate:  python tools/gen_encounters4.py
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_encounters import WILD, _load, split_blobs_uint32, read_cs, strip_comments, parse_array, load_enum, ribbon_set_text, ribbons_from_pk4  # noqa: E402
from gen_gen4text import parse_table  # noqa: E402
from pkhex_source import pkhex_path  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_H = os.path.join(ROOT, "include", "Legality", "Encounters", "EncounterData4.h")
OUT_CPP = os.path.join(ROOT, "src", "Legality", "Encounters", "EncounterData4.cpp")

DPPT_SOURCE = "Legality/Encounters/Data/Gen4/Encounters4DPPt.cs"
HGSS_SOURCE = "Legality/Encounters/Data/Gen4/Encounters4HGSS.cs"

# PKHeX GameVersion token -> PKSE Enums::GameVersion. PKSE has no D/P/Pt group, so PKHeX's DPPt lump is
# Any (no template matches on its version: the enumerator picks the arrays by the Pokemon's game).
VERSION = {"D": "D", "P": "P", "Pt": "Pt", "HG": "HG", "SS": "SS", "DP": "DP", "DPPt": "Any", "HGSS": "HGSS"}
SLOT_TYPE = ["Grass", "Surf", "OldRod", "GoodRod", "SuperRod", "RockSmash", "Headbutt", "HeadbuttSpecial", "BugContest", "HoneyTree",
             "SafariGrass", "SafariSurf", "SafariOldRod", "SafariGoodRod", "SafariSuperRod"]
GROUND_TILE = {"None": "GROUND_TILE_NONE", "Grass": "GROUND_TILE_GRASS", "Water": "GROUND_TILE_WATER", "Cave": "GROUND_TILE_CAVE",
               "Building": "GROUND_TILE_BUILDING", "Rock": "GROUND_TILE_ROCK", "Distortion": "GROUND_TILE_DISTORTION",
               "Max_DP": "GROUND_TILE_MAX_DP", "Max_Pt": "GROUND_TILE_MAX_PT"}
ABILITY = {"OnlyFirst", "OnlySecond", "OnlyHidden", "Any12", "Any12H"}
SHINY = {"Random", "Never", "Always"}
# PKHeX GetLanguageStrings(file, 8): language id -> code; 0 and 6 have none
LANGUAGE_CODES = {1: "ja", 2: "en", 3: "fr", 4: "it", 5: "de", 7: "es", 8: "ko"}
LANGUAGE_COUNT = 9

MOVE = load_enum("Game/Enums/Move.cs", "Move")
NATURE = load_enum("Game/Enums/Nature.cs", "Nature")

# PKHeX PokeCrypto.BlockPosition (rows 0-23)
BLOCK_POSITION = [0, 1, 2, 3, 0, 1, 3, 2, 0, 2, 1, 3, 0, 3, 1, 2, 0, 2, 3, 1, 0, 3, 2, 1, 1, 0, 2, 3, 1, 0, 3, 2, 2, 0, 1, 3, 3, 0, 1, 2, 2, 0, 3, 1, 3, 0, 2, 1,
                  1, 2, 0, 3, 1, 3, 0, 2, 2, 1, 0, 3, 3, 1, 0, 2, 2, 3, 0, 1, 3, 2, 0, 1, 1, 2, 3, 0, 1, 3, 2, 0, 2, 1, 3, 0, 3, 1, 2, 0, 2, 3, 1, 0, 3, 2, 1, 0]
PCD_SIZE = 0x358
PGT_GIFT_OFFSET = 8
PK4_PARTY_SIZE = 236
PK4_STORED_SIZE = 136
GIFT_TYPE_POKEMON = 1


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


def move_value(expr):
    expr = expr.strip()
    match = re.fullmatch(r"\(int\)\s*Move\.(\w+)", expr)
    if match:
        return MOVE[match.group(1)]
    return number(expr)


def moveset(expr):
    match = re.fullmatch(r"new\s*\((.*)\)", expr.strip(), re.S)
    if not match:
        raise SystemExit("not a moveset: %r" % expr)
    moves = [move_value(part) for part in match.group(1).split(",")]
    moves += [0] * (4 - len(moves))
    return "Moveset{{%s}}" % ", ".join(str(move) for move in moves)


def individual_values(expr):
    match = re.fullmatch(r"new\s*\(([^)]*)\)", expr.strip())
    values = [number(part) for part in match.group(1).split(",")]
    return "RNG::IndividualValueSet{%s, true}" % ", ".join(str(value) for value in values)


def boolean(expr):
    return {"true": "true", "false": "false"}[expr.strip()]


def ground_tile(expr):
    return " | ".join(GROUND_TILE[part.strip()] for part in expr.split("|"))


def ball(expr):
    return "Enums::Ball::%s" % expr.strip().split(".")[-1]


def ability(expr):
    name = expr.strip()
    if name not in ABILITY:
        raise SystemExit("unknown ability permission %r" % name)
    return "RNG::AbilityPermission::%s" % name


# ------------------------------------------------------------------------------------------- wild
def emit_wild(name, file_name, version):
    """PKHeX EncounterArea4: header (location, _, type, rate, u16 ground tile), then 10-byte slots."""
    areas = split_blobs_uint32(_load(WILD("Gen4", file_name)))
    slot_lines, area_lines = [], []
    for area_index, area in enumerate(areas):
        location, slot_type, rate = area[0], area[2], area[3]
        tile = struct.unpack_from("<H", area, 4)[0]
        body = area[6:]
        first_slot = len(slot_lines)
        for offset in range(0, (len(body) // 10) * 10, 10):
            species, form, slot_number, level_min, level_max, magnet_index, magnet_count, static_index, static_count = struct.unpack_from("<HBBBBBBBB", body, offset)
            slot_lines.append("                EncounterSlot4(AREAS_%s[%d], %d, %d, %d, %d, %d, %d, %d, %d, %d)," % (name, area_index, species, form, level_min, level_max, slot_number, magnet_index, magnet_count, static_index, static_count))
        area_lines.append("                EncounterArea4(Enums::GameVersion::%s, RNG::SlotType4::%s, %d, %d, 0x%X, &SLOTS_%s[%d], %d)," % (version, SLOT_TYPE[slot_type], rate, location, tile, name, first_slot, len(slot_lines) - first_slot))
    return ("            extern const EncounterArea4 AREAS_%s[%d];\n" % (name, len(areas))
            + "            const EncounterSlot4 SLOTS_%s[] = {\n%s\n            };\n" % (name, "\n".join(slot_lines))
            + "            const EncounterArea4 AREAS_%s[%d] = {\n%s\n            };\n" % (name, len(areas), "\n".join(area_lines)))


def emit_pokewalker():
    """PKHeX EncounterStatic4Pokewalker.GetAll: 27 courses of 6 slots, 12 bytes each."""
    data = _load(WILD("Gen4", "encounter_walker4.pkl"))
    slot_size, slots_per_course, course_count = 12, 6, 27
    if len(data) != slot_size * slots_per_course * course_count:
        raise SystemExit("encounter_walker4.pkl has an unexpected size %d" % len(data))
    lines = []
    for slot_index in range(slots_per_course * course_count):
        species, level, gender = struct.unpack_from("<HBB", data, slot_index * slot_size)
        moves = struct.unpack_from("<4H", data, slot_index * slot_size + 4)
        lines.append("                EncounterStatic4Pokewalker(%d, %d, %d, %d, Moveset{{%s}})," % (slot_index // slots_per_course, species, level, gender, ", ".join(str(move) for move in moves)))
    return "            const EncounterStatic4Pokewalker POKEWALKER[] = {\n%s\n            };\n" % "\n".join(lines)


# ---------------------------------------------------------------------------------------- statics
def emit_statics(text, array_name, name):
    lines = []
    for args, props in parse_array(text, array_name):
        version = args[0].strip()
        fields = [".version = Enums::GameVersion::%s" % VERSION[version], ".speciesId = %d" % number(props.pop("Species")), ".level = %d" % number(props.pop("Level")),
                  ".location = %d" % number(props.pop("Location"))]
        if "Form" in props:
            fields.append(".formId = %d" % number(props.pop("Form")))
        if "FixedBall" in props:
            fields.append(".fixedBall = %s" % ball(props.pop("FixedBall")))
        if "FatefulEncounter" in props:
            fields.append(".isFatefulEncounter = %s" % boolean(props.pop("FatefulEncounter")))
        if "Shiny" in props:
            shiny = props.pop("Shiny").strip().split(".")[-1]
            if shiny not in SHINY:
                raise SystemExit("unknown shiny %r" % shiny)
            fields.append(".shiny = RNG::Shiny::%s" % shiny)
        if "EggLocation" in props:
            fields.append(".eggLocation = %d" % number(props.pop("EggLocation")))
        if "Gender" in props:
            fields.append(".gender = %d" % number(props.pop("Gender")))
        if "IsRoaming" in props:
            fields.append(".isRoaming = %s" % boolean(props.pop("IsRoaming")))
        if "GroundTile" in props:
            fields.append(".groundTile = %s" % ground_tile(props.pop("GroundTile")))
        if "HeldItem" in props:
            fields.append(".heldItem = %d" % number(props.pop("HeldItem")))
        if "Nature" in props:
            fields.append(".nature = %d" % NATURE[props.pop("Nature").strip().split(".")[-1]])
        if "Moves" in props:
            fields.append(".moves = %s" % moveset(props.pop("Moves")))
        if props:
            raise SystemExit("%s: unhandled properties %r" % (array_name, props))
        lines.append("                EncounterStatic4({%s})," % ", ".join(fields))
    return "            const EncounterStatic4 %s[] = {\n%s\n            };\n" % (name, "\n".join(lines))


# ------------------------------------------------------------------------------------ Ranch gifts
def emit_ranch(text):
    lines = []
    for args, props in parse_array(text, "RanchGifts"):
        values = [number(arg) for arg in args]
        if len(values) == 4:
            fields = [".PID = %d" % values[0], ".speciesId = %d" % values[1], ".metLevel = %d" % values[2], ".level = %d" % values[3]]
        else:
            fields = [".speciesId = %d" % values[0], ".metLevel = %d" % values[1], ".level = %d" % values[2], ".isFatefulEncounter = true"]
        fields.append(".location = %d" % number(props.pop("Location")))
        if "EggLocation" in props:
            fields.append(".eggLocation = %d" % number(props.pop("EggLocation")))
        if "FixedBall" in props:
            fields.append(".fixedBall = %s" % ball(props.pop("FixedBall")))
        fields.append(".moves = %s" % moveset(props.pop("Moves")))
        fields.append(".SID16 = %d" % number(props.pop("SID16")))
        fields.append(".trainerGender = %d" % number(props.pop("OTGender")))
        fields.append(".gender = %d" % number(props.pop("Gender")))
        fields.append(".ability = %s" % ability(props.pop("Ability")))
        if "Form" in props:
            fields.append(".formId = %d" % number(props.pop("Form")))
        if props:
            raise SystemExit("RanchGifts: unhandled properties %r" % props)
        lines.append("                EncounterTrade4RanchGift({%s})," % ", ".join(fields))
    return "            const EncounterTrade4RanchGift RANCH_GIFTS[] = {\n%s\n            };\n" % "\n".join(lines)


# ----------------------------------------------------------------------------------------- trades
def read_trade_names(resource):
    names = {}
    for language, code in LANGUAGE_CODES.items():
        path = pkhex_path("Resources/legality/gen4/text_%s_%s.txt" % (resource, code))
        with open(path, encoding="utf-8-sig") as handle:
            names[language] = handle.read().replace("\r", "").split("\n")
    return names


def emit_name_arrays(prefix, names_by_language, indices):
    """PKHeX EncounterUtil.GetNamesForLanguage: one LanguageNames4 per index, empty where a language lacks it."""
    lines = []
    for index in indices:
        entries = []
        for language in range(LANGUAGE_COUNT):
            values = names_by_language.get(language, [])
            entries.append(cpp_text(values[index] if index < len(values) else ""))
        lines.append("            const LanguageNames4 %s_%d = {%s};" % (prefix, index, ", ".join(entries)))
    return "\n".join(lines) + "\n"


def emit_trades(text, array_name, name, names_prefix, names_by_language):
    lines = []
    used = set()
    half = len(names_by_language[1]) >> 1
    for args, props in parse_array(text, array_name):
        index, version, pid, species, level = number(args[1]), args[2].strip(), number(args[3]), number(args[4]), number(args[5])
        used.update((index, index + half))
        fields = [".nicknames = &%s_%d" % (names_prefix, index), ".trainerNames = &%s_%d" % (names_prefix, index + half),
                  ".version = Enums::GameVersion::%s" % VERSION[version], ".PID = 0x%08X" % pid, ".speciesId = %d" % species, ".level = %d" % level,
                  ".ability = %s" % ability(props.pop("Ability")), ".TID16 = %d" % number(props.pop("TID16")), ".SID16 = %d" % number(props.pop("SID16")),
                  ".trainerGender = %d" % number(props.pop("OTGender")), ".gender = %d" % number(props.pop("Gender")),
                  ".individualValues = %s" % individual_values(props.pop("IVs"))]
        if "Moves" in props:
            fields.append(".moves = %s" % moveset(props.pop("Moves")))
        if "MetLocation" in props:
            fields.append(".metLocation = %d" % number(props.pop("MetLocation")))
        if "Contest" in props:
            fields.append(".contest = %d" % number(props.pop("Contest")))
        if props:
            raise SystemExit("%s: unhandled properties %r" % (array_name, props))
        lines.append("                EncounterTrade4PID({%s})," % ", ".join(fields))
    block = "            const EncounterTrade4PID %s[] = {\n%s\n            };\n" % (name, "\n".join(lines))
    return emit_name_arrays(names_prefix, names_by_language, sorted(used)) + block


# ---------------------------------------------------------------------------------- Wonder Cards
def crypt_array(data, seed):
    """PKHeX PokeCrypto.CryptArray."""
    out = bytearray(data)
    for offset in range(0, len(out), 2):
        seed = (0x41C64E6D * seed + 0x6073) & 0xFFFFFFFF
        struct.pack_into("<H", out, offset, struct.unpack_from("<H", out, offset)[0] ^ (seed >> 16))
    return out


def decrypt_pk4(data):
    """PKHeX PokeCrypto.DecryptIfEncrypted45 for a party-sized PK4."""
    if struct.unpack_from("<I", data, 0x64)[0] == 0:
        return bytes(data)
    pid = struct.unpack_from("<I", data, 0)[0]
    checksum = struct.unpack_from("<H", data, 6)[0]
    shuffle_value = ((pid >> 13) & 31) % 24
    shuffled = crypt_array(data[8:PK4_STORED_SIZE], checksum)
    party = crypt_array(data[PK4_STORED_SIZE:PK4_PARTY_SIZE], pid)
    blocks = [shuffled[block * 32:(block + 1) * 32] for block in range(4)]
    ordered = b"".join(blocks[BLOCK_POSITION[shuffle_value * 4 + block]] for block in range(4))
    return bytes(data[:8]) + ordered + bytes(party)


def decode_gen4_text(data, table, korean_table, korean_start):
    """The Pokemon4DP string reader: stop at the terminator or an unmapped value, gender signs normalized."""
    out = []
    for offset in range(0, len(data), 2):
        raw = struct.unpack_from("<H", data, offset)[0]
        if raw in (0xFFFF, 0):
            break
        if raw < len(table):
            character = table[raw]
        elif 0 <= raw - korean_start < len(korean_table):
            character = korean_table[raw - korean_start]
        else:
            character = 0xFFFF
        if character == 0xFFFF:
            break
        out.append({0x246D: 0x2642, 0x246E: 0x2640}.get(character, character))
    return "".join(chr(code) for code in out)


def emit_wonder_cards():
    with open(pkhex_path("PKM/Strings/StringConverter4Util.cs"), encoding="utf-8") as handle:
        converter = handle.read()
    table = parse_table(converter, "TableINT")
    korean_table = parse_table(converter, "TableKOR")
    korean_start = int(re.search(r"TableKORStart\s*=\s*(0x[0-9A-Fa-f]+|\d+)", converter).group(1), 0)
    data = _load(pkhex_path("Resources/legality/mgdb/wc4.pkl"))
    if len(data) % PCD_SIZE != 0:
        raise SystemExit("wc4.pkl is not a whole number of Wonder Cards")
    lines = []
    for card_index in range(len(data) // PCD_SIZE):
        card = data[card_index * PCD_SIZE:(card_index + 1) * PCD_SIZE]
        if struct.unpack_from("<H", card, 0)[0] != GIFT_TYPE_POKEMON:
            continue
        pk = decrypt_pk4(card[PGT_GIFT_OFFSET:PGT_GIFT_OFFSET + PK4_PARTY_SIZE])
        species = struct.unpack_from("<H", pk, 0x08)[0]
        if species == 0:
            continue
        pid = struct.unpack_from("<I", pk, 0x00)[0]
        tid, sid = struct.unpack_from("<HH", pk, 0x0C)
        iv32 = struct.unpack_from("<I", pk, 0x38)[0]
        egg_location = struct.unpack_from("<H", pk, 0x44)[0] or struct.unpack_from("<H", pk, 0x7E)[0]
        moves = struct.unpack_from("<4H", pk, 0x28)
        shiny_xor = tid ^ sid ^ (pid & 0xFFFF) ^ (pid >> 16)
        fields = [".speciesId = %d" % species, ".formId = %d" % (pk[0x40] >> 3), ".metLevel = %d" % (pk[0x84] & 0x7F), ".currentLevel = %d" % pk[0x8C],
                  ".TID16 = %d" % tid, ".SID16 = %d" % sid, ".trainerName = %s" % cpp_text(decode_gen4_text(pk[0x68:0x78], table, korean_table, korean_start)),
                  ".trainerGender = %d" % (pk[0x84] >> 7), ".language = %d" % pk[0x17], ".eventLocationIndex = %d" % egg_location,
                  ".isEgg = %s" % ("true" if (iv32 >> 30) & 1 else "false"), ".ball = %d" % max(pk[0x83], pk[0x86]),
                  ".contestStats = {%s}" % ", ".join(str(value) for value in pk[0x1E:0x24]), ".PID = 0x%08X" % pid, ".gender = %d" % ((pk[0x40] >> 1) & 3),
                  ".isFatefulEncounter = %s" % ("true" if pk[0x40] & 1 else "false"), ".version = %d" % pk[0x5F],
                  ".moves = Moveset{{%s}}" % ", ".join(str(move) for move in moves), ".heldItem = %d" % struct.unpack_from("<H", pk, 0x0A)[0],
                  ".packedIVs = 0x%08X" % (iv32 & 0x3FFFFFFF), ".isShiny = %s" % ("true" if shiny_xor < 8 else "false"), ".abilityId = %d" % pk[0x15], ".isNicknamed = %s" % ("true" if iv32 >> 31 else "false")]
        ribbons = ribbon_set_text(ribbons_from_pk4(pk))
        if ribbons:
            fields.append(".ribbons = %s" % ribbons)
        lines.append("                MysteryGiftPCD({%s})," % ", ".join(fields))
    return "            const MysteryGiftPCD MYSTERY_GIFTS4[] = {\n%s\n            };\n" % "\n".join(lines)


def main():
    dppt = strip_comments(read_cs(DPPT_SOURCE))
    hgss = strip_comments(read_cs(HGSS_SOURCE))

    blocks, accessors = [], []

    def accessor(function, element, array):
        accessors.append((function, element, array))

    for name, file_name, version in (("D", "encounter_d.pkl", "D"), ("P", "encounter_p.pkl", "P"), ("PT", "encounter_pt.pkl", "Pt"),
                                     ("HG", "encounter_hg.pkl", "HG"), ("SS", "encounter_ss.pkl", "SS")):
        blocks.append(emit_wild(name, file_name, version))
        accessor("getAreas4" + version, "EncounterArea4", "AREAS_" + name)

    for text, array_name, name, function in ((dppt, "StaticDPPt", "STATIC_DPPT", "getStatic4DPPt"), (dppt, "StaticDP", "STATIC_DP", "getStatic4DP"),
                                             (dppt, "StaticPt", "STATIC_PT", "getStatic4Pt"), (dppt, "StaticD", "STATIC_D", "getStatic4D"),
                                             (dppt, "StaticP", "STATIC_P", "getStatic4P"), (hgss, "Encounter_HGSS", "STATIC_HGSS", "getStatic4HGSS"),
                                             (hgss, "StaticHG", "STATIC_HG", "getStatic4HG"), (hgss, "StaticSS", "STATIC_SS", "getStatic4SS")):
        blocks.append(emit_statics(text, array_name, name))
        accessor(function, "EncounterStatic4", name)

    blocks.append(emit_pokewalker())
    accessor("getPokewalker4", "EncounterStatic4Pokewalker", "POKEWALKER")

    blocks.append(emit_ranch(dppt))
    accessor("getRanchGifts4", "EncounterTrade4RanchGift", "RANCH_GIFTS")

    blocks.append(emit_trades(dppt, "TradeGift_DPPtIngame", "TRADE_GIFT_DPPT", "TRADE_NAMES_DPPT", read_trade_names("tradedppt")))
    accessor("getTradeGift4DPPt", "EncounterTrade4PID", "TRADE_GIFT_DPPT")
    blocks.append(emit_trades(hgss, "TradeGift_HGSS", "TRADE_GIFT_HGSS", "TRADE_NAMES_HGSS", read_trade_names("tradehgss")))
    accessor("getTradeGift4HGSS", "EncounterTrade4PID", "TRADE_GIFT_HGSS")

    blocks.append(emit_wonder_cards())
    accessor("getMysteryGifts4", "MysteryGiftPCD", "MYSTERY_GIFTS4")

    declarations = "\n".join("        std::span<const %s> %s() noexcept;" % (element, function) for function, element, _ in accessors)
    definitions = "\n\n".join("        std::span<const %s> %s() noexcept\n        {\n            return %s;\n        }" % (element, function, array) for function, element, array in accessors)

    header = """/**
 * Auto-generated by tools/gen_encounters4.py from PKHeX's Generation 4 encounter data. DO NOT EDIT BY HAND --
 * rerun the generator instead.
 *
 * One accessor per PKHeX array (Encounters4DPPt, Encounters4HGSS, EncounterEvent.MGDB_G4), unmerged and in
 * PKHeX's order.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERDATA4_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERDATA4_H

#include <span>

#include "Legality/Encounters/EncounterArea4.h"
#include "Legality/Encounters/EncounterStatic4.h"
#include "Legality/Encounters/EncounterStatic4Pokewalker.h"
#include "Legality/Encounters/EncounterTrade4PID.h"
#include "Legality/Encounters/EncounterTrade4RanchGift.h"
#include "Legality/Encounters/MysteryGiftPCD.h"

namespace Legality
{
    namespace Encounters
    {
%s
    }
}

#endif
""" % declarations
    source = """// Auto-generated by tools/gen_encounters4.py. DO NOT EDIT BY HAND.
#include "Legality/Encounters/EncounterData4.h"

#include "Legality/Encounters/EncounterSlot4.h"
#include "Legality/Encounters/GroundTileAllowed.h"

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
