#!/usr/bin/env python3
"""Generate include/Legality/Encounters/EncounterData8a.h + src/Legality/Encounters/EncounterData8a.cpp:
PKHeX's Legends: Arceus encounter templates, unmerged and in PKHeX's own order, as the encounter
engine's template classes.

SOURCES (PKHeX, via tools/pkhex_source.py; PKHEX_LOCAL points it at a checkout)
    Resources/legality/wild/Gen8/encounter_la.pkl       EncounterArea8a.GetAreas (Encounters8a.SlotsLA)
    Legality/Encounters/Data/Gen8/Encounters8a.cs       Encounters8a.StaticLA
    Resources/legality/mgdb/wa8.pkl                     EncounterEvent.MGDB_G8A (Wonder Cards)

Only the Wonder Cards that give a Pokemon are emitted.

Regenerate:  python tools/gen_encounters8a.py
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_encounters import WILD, _load, split_blobs_uint32, read_cs, strip_comments, parse_array, ribbon_set_text, ribbons_from_index_bytes  # noqa: E402
from gen_encounters8 import cpp_text, number, moveset, boolean, last_token, designated, moveset_text, card_text, GIFT_TYPE_POKEMON  # noqa: E402
from pkhex_source import pkhex_path  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_H = os.path.join(ROOT, "include", "Legality", "Encounters", "EncounterData8a.h")
OUT_CPP = os.path.join(ROOT, "src", "Legality", "Encounters", "EncounterData8a.cpp")

ENCOUNTERS_SOURCE = "Legality/Encounters/Data/Gen8/Encounters8a.cs"

# Encounters8a's named scalars: M middle, A maximum (Alphas), U the Unown height.
SCALARS = {"M": 127, "A": 255, "U": 128}
SHINY = {"Random": "Random", "Never": "Never", "Always": "Always"}
WONDER_CARD_SIZE = 0x2C8


# ------------------------------------------------------------------------------------------- wild
def emit_wild():
    """PKHeX EncounterArea8a: location count, the locations as bytes, padding to an even offset, then
    the slot type, the slot count and 8-byte slots (u16 species, form, alpha type, min, max, gender, flawless)."""
    areas = split_blobs_uint32(_load(WILD("Gen8", "encounter_la.pkl")))
    location_lines, slot_lines, area_lines = [], [], []
    location_total = 0
    for area in areas:
        location_count = area[0]
        locations = list(area[1:1 + location_count])
        align = location_count + 1
        align += align & 1
        slot_type, slot_count = area[align], area[align + 1]
        first_location = location_total
        location_lines.append("                %s," % ", ".join(str(location) for location in locations))
        location_total += location_count
        first_slot = len(slot_lines)
        for slot_index in range(slot_count):
            offset = align + 2 + slot_index * 8
            species = struct.unpack_from("<H", area, offset)[0]
            form, alpha, level_min, level_max, gender, flawless = struct.unpack_from("<6B", area, offset + 2)
            slot_lines.append("                {%d, %d, %d, %d, %d, %d, %d}," % (species, form, alpha, level_min, level_max, gender, flawless))
        area_lines.append("                EncounterArea8a(&LOCATIONS_LA[%d], %d, static_cast<SlotType8a>(%d), &SLOTS_LA[%d], %d)," % (first_location, location_count, slot_type, first_slot, slot_count))
    return ("            constexpr uint8_t LOCATIONS_LA[] = {\n%s\n            };\n" % "\n".join(location_lines)
            + "            constexpr EncounterSlotRecord8a SLOTS_LA[] = {\n%s\n            };\n" % "\n".join(slot_lines)
            + "            const EncounterArea8a AREAS_LA[%d] = {\n%s\n            };\n" % (len(areas), "\n".join(area_lines)))


# ---------------------------------------------------------------------------------------- statics
STATIC_ORDER = ["speciesId", "formId", "level", "heightScalar", "weightScalar", "levelMax", "gender", "location", "flawlessIVCount", "shiny",
                "isAlpha", "isFatefulEncounter", "fixedBall", "moves"]


def scalar(expr):
    expr = expr.strip()
    return SCALARS[expr] if expr in SCALARS else number(expr)


def emit_statics(text):
    lines = []
    for args, props in parse_array(text, "StaticLA"):
        fields = {"speciesId": str(number(args[0])), "formId": str(number(args[1])), "level": str(number(args[2]))}
        if len(args) > 3:
            fields["heightScalar"] = str(scalar(args[3]))
        if len(args) > 4:
            fields["weightScalar"] = str(scalar(args[4]))
        fields["location"] = str(number(props.pop("Location")))
        if "LevelMax" in props:
            fields["levelMax"] = str(number(props.pop("LevelMax")))
        if "Gender" in props:
            fields["gender"] = str(number(props.pop("Gender")))
        if "FlawlessIVCount" in props:
            fields["flawlessIVCount"] = str(number(props.pop("FlawlessIVCount")))
        if "Shiny" in props:
            fields["shiny"] = "RNG::Shiny::%s" % SHINY[last_token(props.pop("Shiny"))]
        if "IsAlpha" in props:
            fields["isAlpha"] = boolean(props.pop("IsAlpha"))
        if "FatefulEncounter" in props:
            fields["isFatefulEncounter"] = boolean(props.pop("FatefulEncounter"))
        if "FixedBall" in props:
            fields["fixedBall"] = "Enums::Ball::%s" % last_token(props.pop("FixedBall"))
        if "Moves" in props:
            fields["moves"] = moveset(props.pop("Moves"))
        # EncounterStatic8aCorrelation only steers PKHeX's generator, not its matcher.
        props.pop("Method", None)
        if props:
            raise SystemExit("StaticLA: unhandled properties %r" % props)
        lines.append("                EncounterStatic8a({%s})," % designated(fields, STATIC_ORDER, "StaticLA"))
    return "            const EncounterStatic8a STATIC_LA[] = {\n%s\n            };\n" % "\n".join(lines)


# ---------------------------------------------------------------------------------------- Wonder Cards
CARD_ORDER = ["cardId", "TID16", "SID16", "originGame", "encryptionConstant", "PID", "eggLocation", "location", "ball", "moves", "relearnMoves",
              "speciesId", "formId", "gender", "level", "isEgg", "nature", "abilityType", "shinyType", "metLevel", "individualValues",
              "trainerGender", "dynamaxLevel", "gritValues", "languageWords", "trainerNames", "nicknamedSlots", "ribbons"]

CARD_RIBBON_OFFSET = 0x244  # PKHeX WA8.RibbonBytesOffset


def emit_wonder_cards(name):
    data = _load(pkhex_path("Resources/legality/mgdb/wa8.pkl"))
    if len(data) % WONDER_CARD_SIZE:
        raise SystemExit("wa8.pkl is not a whole number of Wonder Cards")
    lines = []
    for offset in range(0, len(data), WONDER_CARD_SIZE):
        card = data[offset:offset + WONDER_CARD_SIZE]
        if card[0x0F] != GIFT_TYPE_POKEMON:
            continue
        card_id = struct.unpack_from("<H", card, 0x8)[0]
        tid, sid, origin_game = struct.unpack_from("<HHi", card, 0x18)
        encryption_constant, pid = struct.unpack_from("<II", card, 0x20)
        egg_location, met_location, ball = struct.unpack_from("<HHH", card, 0x220)
        moves = struct.unpack_from("<4H", card, 0x228)
        relearn = struct.unpack_from("<4H", card, 0x230)
        species = struct.unpack_from("<H", card, 0x238)[0]
        form, gender, level, is_egg, nature, ability_type, shiny_type, met_level, dynamax_level = struct.unpack_from("<9B", card, 0x23A)
        individual_values_raw = struct.unpack_from("<6B", card, 0x264)
        trainer_gender = card[0x26A]
        grit = struct.unpack_from("<6B", card, 0x276)
        language_words, trainer_names, nicknamed = [], [], 0
        for slot in range(9):
            nickname_offset = 0x28 + slot * 0x1C
            language_words.append(struct.unpack_from("<H", card, nickname_offset + 0x1A)[0])
            if struct.unpack_from("<H", card, nickname_offset)[0] != 0:
                nicknamed |= 1 << slot
            trainer_names.append(cpp_text(card_text(card, 0x124 + slot * 0x1C)))
        fields = {"cardId": str(card_id), "TID16": str(tid), "SID16": str(sid), "originGame": str(origin_game),
                  "encryptionConstant": "0x%08X" % encryption_constant, "PID": "0x%08X" % pid, "eggLocation": str(egg_location),
                  "location": str(met_location), "ball": str(ball & 0xFF), "moves": moveset_text(moves), "relearnMoves": moveset_text(relearn),
                  "speciesId": str(species), "formId": str(form), "gender": str(gender), "level": str(level), "isEgg": "true" if is_egg == 1 else "false",
                  "nature": str(nature), "abilityType": str(ability_type), "shinyType": str(shiny_type), "metLevel": str(met_level),
                  "individualValues": "{%s}" % ", ".join(str(value) for value in individual_values_raw), "trainerGender": str(trainer_gender),
                  "dynamaxLevel": str(dynamax_level), "gritValues": "{%s}" % ", ".join(str(value) for value in grit),
                  "languageWords": "{%s}" % ", ".join(str(word) for word in language_words),
                  "trainerNames": "{%s}" % ", ".join(trainer_names), "nicknamedSlots": "0x%03X" % nicknamed}
        ribbons = ribbon_set_text(ribbons_from_index_bytes(card, CARD_RIBBON_OFFSET))
        if ribbons:
            fields["ribbons"] = ribbons
        lines.append("                MysteryGiftWA8({%s})," % designated(fields, CARD_ORDER, name))
    return "            const MysteryGiftWA8 %s[] = {\n%s\n            };\n" % (name, "\n".join(lines))


def main():
    encounters = strip_comments(read_cs(ENCOUNTERS_SOURCE))
    blocks = [emit_wild(), emit_statics(encounters), emit_wonder_cards("WONDER_CARDS_8A")]
    accessors = [("getAreas8aLA", "EncounterArea8a", "AREAS_LA"), ("getStatic8aLA", "EncounterStatic8a", "STATIC_LA"),
                 ("getGifts8aWA8", "MysteryGiftWA8", "WONDER_CARDS_8A")]

    declarations = "\n".join("        std::span<const %s> %s() noexcept;" % (element, function) for function, element, _ in accessors)
    definitions = "\n\n".join("        std::span<const %s> %s() noexcept\n        {\n            return %s;\n        }" % (element, function, array) for function, element, array in accessors)

    header = """/**
 * Auto-generated by tools/gen_encounters8a.py from PKHeX's Legends: Arceus encounter data. DO NOT EDIT BY
 * HAND -- rerun the generator instead.
 *
 * One accessor per PKHeX array (Encounters8a.SlotsLA and StaticLA, EncounterEvent.MGDB_G8A), unmerged and in
 * PKHeX's order.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERDATA8A_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERDATA8A_H

#include <span>

#include "Legality/Encounters/EncounterArea8a.h"
#include "Legality/Encounters/EncounterStatic8a.h"
#include "Legality/Encounters/MysteryGiftWA8.h"

namespace Legality
{
    namespace Encounters
    {
%s
    }
}

#endif
""" % declarations
    source = """// Auto-generated by tools/gen_encounters8a.py. DO NOT EDIT BY HAND.
#include "Legality/Encounters/EncounterData8a.h"

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
