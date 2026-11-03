#!/usr/bin/env python3
"""Generate include/Legality/Encounters/EncounterData8b.h + src/Legality/Encounters/EncounterData8b.cpp:
PKHeX's Brilliant Diamond/Shining Pearl encounter templates, unmerged and in PKHeX's own order, as the
encounter engine's template classes.

SOURCES (PKHeX, via tools/pkhex_source.py; PKHEX_LOCAL points it at a checkout)
    Resources/legality/wild/Gen8/encounter_{bd,sp}.pkl                   EncounterArea8b.GetAreas (overworld)
    Resources/legality/wild/Gen8/encounter_{bd,sp}_underground.pkl       EncounterArea8b.GetAreas (Grand Underground)
    Legality/Encounters/Data/Gen8/Encounters8b.cs                        statics and trades
    Resources/legality/gen8/text_tradebdsp_<language>.txt                trade nicknames and OT names
    Resources/legality/mgdb/wb8.pkl                                      EncounterEvent.MGDB_G8B (Wonder Cards)

PKHeX's SlotsBD / SlotsSP are the overworld areas followed by the Underground ones; the generator keeps
that order in one array per game. Only the Wonder Cards that give a Pokemon are emitted.

Regenerate:  python tools/gen_encounters8b.py
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_encounters import WILD, _load, split_blobs_uint32, read_cs, strip_comments, parse_array, load_enum, ribbon_set_text, ribbons_from_index_bytes  # noqa: E402
from gen_encounters8 import (cpp_text, number, moveset, individual_values, boolean, last_token, designated,  # noqa: E402
                             emit_language_names, moveset_text, card_text, LANGUAGE_CODES, LANGUAGE_COUNT, GIFT_TYPE_POKEMON)
from pkhex_source import pkhex_path  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_H = os.path.join(ROOT, "include", "Legality", "Encounters", "EncounterData8b.h")
OUT_CPP = os.path.join(ROOT, "src", "Legality", "Encounters", "EncounterData8b.cpp")

ENCOUNTERS_SOURCE = "Legality/Encounters/Data/Gen8/Encounters8b.cs"

NATURE = load_enum("Game/Enums/Nature.cs", "Nature")
VERSION = {"BD": "BD", "SP": "SP", "BDSP": "BDSP"}
ABILITY = {"OnlyFirst": "OnlyFirst", "OnlySecond": "OnlySecond", "OnlyHidden": "OnlyHidden", "Any12": "Any12", "Any12H": "Any12H"}
SHINY = {"Random": "Random", "Never": "Never", "Always": "Always"}
# PKHeX Locations.Default8bNone
LOCATION_NONE8B = 65535
WONDER_CARD_SIZE = 0x2DC


def location(expr):
    expr = expr.strip()
    if last_token(expr) == "Default8bNone":
        return str(LOCATION_NONE8B)
    return str(number(expr))


# ------------------------------------------------------------------------------------------- wild
def emit_wild(name, file_names, version):
    """PKHeX EncounterArea8b: u16 location, u8 type, pad, then 4-byte slots (u16 species | form << 11, min, max).

    The slots are emitted as EncounterSlotRecord8b records; EncounterArea8b.slotAt builds the template."""
    areas = []
    for file_name in file_names:
        areas.extend(split_blobs_uint32(_load(WILD("Gen8", file_name))))
    slot_lines, area_lines = [], []
    for area in areas:
        area_location = struct.unpack_from("<H", area, 0)[0]
        slot_type = area[2]
        first_slot = len(slot_lines)
        for offset in range(4, len(area) - 3, 4):
            packed, level_min, level_max = struct.unpack_from("<HBB", area, offset)
            slot_lines.append("                {%d, %d, %d, %d}," % (packed & 0x3FF, packed >> 11, level_min, level_max))
        area_lines.append("                EncounterArea8b(Enums::GameVersion::%s, %d, static_cast<SlotType8b>(%d), &SLOTS_%s[%d], %d)," % (version, area_location, slot_type, name, first_slot, len(slot_lines) - first_slot))
    return ("            constexpr EncounterSlotRecord8b SLOTS_%s[] = {\n%s\n            };\n" % (name, "\n".join(slot_lines))
            + "            const EncounterArea8b AREAS_%s[%d] = {\n%s\n            };\n" % (name, len(areas), "\n".join(area_lines)))


# ---------------------------------------------------------------------------------------- statics
STATIC_ORDER = ["version", "speciesId", "location", "eggLocation", "level", "fixedBall", "flawlessIVCount", "isRoaming", "ability", "shiny",
                "isFatefulEncounter"]


def emit_statics(text, array_name, name):
    lines = []
    for args, props in parse_array(text, array_name):
        fields = {"version": "Enums::GameVersion::%s" % VERSION[last_token(args[0])]}
        fields["speciesId"] = str(number(props.pop("Species")))
        fields["location"] = location(props.pop("Location"))
        fields["level"] = str(number(props.pop("Level")))
        if "EggLocation" in props:
            fields["eggLocation"] = location(props.pop("EggLocation"))
        if "FixedBall" in props:
            fields["fixedBall"] = "Enums::Ball::%s" % last_token(props.pop("FixedBall"))
        if "FlawlessIVCount" in props:
            fields["flawlessIVCount"] = str(number(props.pop("FlawlessIVCount")))
        if "IsRoaming" in props:
            fields["isRoaming"] = boolean(props.pop("IsRoaming"))
        if "Ability" in props:
            fields["ability"] = "RNG::AbilityPermission::%s" % ABILITY[last_token(props.pop("Ability"))]
        if "Shiny" in props:
            fields["shiny"] = "RNG::Shiny::%s" % SHINY[last_token(props.pop("Shiny"))]
        if "FatefulEncounter" in props:
            fields["isFatefulEncounter"] = boolean(props.pop("FatefulEncounter"))
        if props:
            raise SystemExit("%s: unhandled properties %r" % (array_name, props))
        lines.append("                EncounterStatic8b({%s})," % designated(fields, STATIC_ORDER, array_name))
    return "            const EncounterStatic8b %s[] = {\n%s\n            };\n" % (name, "\n".join(lines))


# ----------------------------------------------------------------------------------------- trades
TRADE_ORDER = ["nicknames", "trainerNames", "version", "speciesId", "encryptionConstant", "PID", "level", "ability", "gender", "trainerGender",
               "TID16", "individualValues", "moves", "heightScalar", "weightScalar", "nature"]


def read_trade_names():
    names = {}
    for language, code in LANGUAGE_CODES.items():
        path = pkhex_path("Resources/legality/gen8/text_tradebdsp_%s.txt" % code)
        with open(path, encoding="utf-8-sig") as handle:
            names[language] = handle.read().replace("\r", "").split("\n")
    return names


def emit_trades(text, array_name, name, names_by_language, used_indices):
    lines = []
    half = len(names_by_language[1]) >> 1
    for args, props in parse_array(text, array_name):
        if args[0].strip() != "TradeNames":
            raise SystemExit("%s: unknown trade constructor %r" % (array_name, args[0]))
        index = number(args[1])
        used_indices.update((index, index + half))
        fields = {"nicknames": "&TRADE_NAMES_BDSP_%d" % index, "trainerNames": "&TRADE_NAMES_BDSP_%d" % (index + half),
                  "version": "Enums::GameVersion::%s" % VERSION[last_token(args[2])]}
        fields["speciesId"] = str(number(props.pop("Species")))
        fields["encryptionConstant"] = "0x%08X" % number(props.pop("EncryptionConstant"))
        fields["PID"] = "0x%08X" % number(props.pop("PID"))
        fields["level"] = str(number(props.pop("Level")))
        fields["ability"] = "RNG::AbilityPermission::%s" % ABILITY[last_token(props.pop("Ability"))]
        fields["gender"] = str(number(props.pop("Gender")))
        fields["trainerGender"] = str(number(props.pop("OTGender")))
        fields["TID16"] = str(number(props.pop("TID16")))
        fields["individualValues"] = individual_values(props.pop("IVs"))
        fields["moves"] = moveset(props.pop("Moves"))
        fields["heightScalar"] = str(number(props.pop("HeightScalar")))
        fields["weightScalar"] = str(number(props.pop("WeightScalar")))
        fields["nature"] = str(NATURE[last_token(props.pop("Nature"))])
        if props:
            raise SystemExit("%s: unhandled properties %r" % (array_name, props))
        lines.append("                EncounterTrade8b({%s})," % designated(fields, TRADE_ORDER, array_name))
    return "            const EncounterTrade8b %s[] = {\n%s\n            };\n" % (name, "\n".join(lines))


# ------------------------------------------------------------------------------------------- WB8
CARD_ORDER = ["cardId", "TID16", "SID16", "originGame", "encryptionConstant", "PID", "eggLocation", "location", "ball", "moves", "relearnMoves",
              "speciesId", "formId", "gender", "level", "isEgg", "nature", "abilityType", "shinyType", "metLevel", "individualValues",
              "trainerGender", "contestStats", "languageWords", "trainerNames", "nicknamedSlots", "ribbons"]

CARD_RIBBON_OFFSET = 0x292  # PKHeX WB8.RibbonBytesOffset


def emit_wonder_cards(name):
    data = _load(pkhex_path("Resources/legality/mgdb/wb8.pkl"))
    if len(data) % WONDER_CARD_SIZE:
        raise SystemExit("wb8.pkl is not a whole number of Wonder Cards")
    lines = []
    for offset in range(0, len(data), WONDER_CARD_SIZE):
        card = data[offset:offset + WONDER_CARD_SIZE]
        if card[0x11] != GIFT_TYPE_POKEMON:
            continue
        card_id = struct.unpack_from("<H", card, 0x8)[0]
        tid, sid, origin_game = struct.unpack_from("<HHi", card, 0x20)
        encryption_constant, pid = struct.unpack_from("<II", card, 0x28)
        egg_location, met_location, ball = struct.unpack_from("<HHH", card, 0x270)
        moves = struct.unpack_from("<4H", card, 0x278)
        relearn = struct.unpack_from("<4H", card, 0x280)
        species = struct.unpack_from("<H", card, 0x288)[0]
        form, gender, level, is_egg, nature, ability_type, shiny_type, met_level = struct.unpack_from("<8B", card, 0x28A)
        individual_values_raw = struct.unpack_from("<6B", card, 0x2B2)
        trainer_gender = card[0x2B8]
        contest = struct.unpack_from("<6B", card, 0x2BF)
        language_words, trainer_names, nicknamed = [], [], 0
        for slot in range(9):
            nickname_offset = 0x30 + slot * 0x20
            language_words.append(struct.unpack_from("<H", card, nickname_offset + 0x1A)[0])
            if struct.unpack_from("<H", card, nickname_offset)[0] != 0:
                nicknamed |= 1 << slot
            trainer_names.append(cpp_text(card_text(card, 0x150 + slot * 0x20)))
        fields = {"cardId": str(card_id), "TID16": str(tid), "SID16": str(sid), "originGame": str(origin_game),
                  "encryptionConstant": "0x%08X" % encryption_constant, "PID": "0x%08X" % pid, "eggLocation": str(egg_location),
                  "location": str(met_location), "ball": str(ball & 0xFF), "moves": moveset_text(moves), "relearnMoves": moveset_text(relearn),
                  "speciesId": str(species), "formId": str(form), "gender": str(gender), "level": str(level), "isEgg": "true" if is_egg == 1 else "false",
                  "nature": str(nature), "abilityType": str(ability_type), "shinyType": str(shiny_type), "metLevel": str(met_level),
                  "individualValues": "{%s}" % ", ".join(str(value) for value in individual_values_raw), "trainerGender": str(trainer_gender),
                  "contestStats": "{%s}" % ", ".join(str(value) for value in contest),
                  "languageWords": "{%s}" % ", ".join(str(word) for word in language_words),
                  "trainerNames": "{%s}" % ", ".join(trainer_names), "nicknamedSlots": "0x%03X" % nicknamed}
        ribbons = ribbon_set_text(ribbons_from_index_bytes(card, CARD_RIBBON_OFFSET))
        if ribbons:
            fields["ribbons"] = ribbons
        lines.append("                MysteryGiftWB8({%s})," % designated(fields, CARD_ORDER, name))
    return "            const MysteryGiftWB8 %s[] = {\n%s\n            };\n" % (name, "\n".join(lines))


def main():
    encounters = strip_comments(read_cs(ENCOUNTERS_SOURCE))
    blocks, accessors = [], []

    def accessor(function, element, array):
        accessors.append((function, element, array))

    for name, function, file_names, version in (("BD", "BD", ("encounter_bd.pkl", "encounter_bd_underground.pkl"), "BD"),
                                                ("SP", "SP", ("encounter_sp.pkl", "encounter_sp_underground.pkl"), "SP")):
        blocks.append(emit_wild(name, file_names, version))
        accessor("getAreas8b" + function, "EncounterArea8b", "AREAS_" + name)

    for array_name, name, function in (("Encounter_BDSP", "STATIC_BDSP", "getStatic8bBDSP"), ("StaticBD", "STATIC_BD", "getStatic8bBD"),
                                       ("StaticSP", "STATIC_SP", "getStatic8bSP")):
        blocks.append(emit_statics(encounters, array_name, name))
        accessor(function, "EncounterStatic8b", name)

    names = read_trade_names()
    used = set()
    trade_block = emit_trades(encounters, "TradeGift_BDSP", "TRADE_BDSP", names, used)
    accessor("getTrade8bBDSP", "EncounterTrade8b", "TRADE_BDSP")
    name_lines = []
    for index in sorted(used):
        values = []
        for language in range(LANGUAGE_COUNT):
            language_names = names.get(language, [])
            values.append(language_names[index] if index < len(language_names) else "")
        name_lines.append(emit_language_names("TRADE_NAMES_BDSP_%d" % index, values))
    blocks.append("\n".join(name_lines) + "\n")
    blocks.append(trade_block)

    blocks.append(emit_wonder_cards("WONDER_CARDS_8B"))
    accessor("getGifts8bWB8", "MysteryGiftWB8", "WONDER_CARDS_8B")

    declarations = "\n".join("        std::span<const %s> %s() noexcept;" % (element, function) for function, element, _ in accessors)
    definitions = "\n\n".join("        std::span<const %s> %s() noexcept\n        {\n            return %s;\n        }" % (element, function, array) for function, element, array in accessors)

    header = """/**
 * Auto-generated by tools/gen_encounters8b.py from PKHeX's Brilliant Diamond/Shining Pearl encounter data. DO NOT
 * EDIT BY HAND -- rerun the generator instead.
 *
 * One accessor per PKHeX array (Encounters8b, EncounterEvent.MGDB_G8B), unmerged and in PKHeX's order; a game's
 * areas are its overworld areas followed by its Grand Underground ones, as PKHeX's SlotsBD / SlotsSP.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERDATA8B_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERDATA8B_H

#include <span>

#include "Legality/Encounters/EncounterArea8b.h"
#include "Legality/Encounters/EncounterStatic8b.h"
#include "Legality/Encounters/EncounterTrade8b.h"
#include "Legality/Encounters/MysteryGiftWB8.h"

namespace Legality
{
    namespace Encounters
    {
%s
    }
}

#endif
""" % declarations
    source = """// Auto-generated by tools/gen_encounters8b.py. DO NOT EDIT BY HAND.
#include "Legality/Encounters/EncounterData8b.h"

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
