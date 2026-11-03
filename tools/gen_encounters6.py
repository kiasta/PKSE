#!/usr/bin/env python3
"""Generate include/Legality/Encounters/EncounterData6.h + src/Legality/Encounters/EncounterData6.cpp:
PKHeX's Generation 6 (X, Y, Omega Ruby, Alpha Sapphire) encounter templates, unmerged and in PKHeX's own
order, as the encounter engine's template classes.

SOURCES (PKHeX, via tools/pkhex_source.py; PKHEX_LOCAL points it at a checkout)
    Resources/legality/wild/Gen6/encounter_{x,y,as,or}.pkl     EncounterArea6XY/6AO.GetAreas
    Legality/Encounters/Templates/Gen6/EncounterArea6XY.cs     the Friend Safari (AllFriendSafariSpecies)
    Resources/byte/personal/personal_xy                        EncounterSlot6XY.FlawlessIVCount (egg group)
    Legality/Encounters/Data/Gen6/Encounters6XY.cs, Encounters6AO.cs   statics and trades
    Resources/legality/gen6/text_trade{xy,ao}_{ja,en,fr,it,de,es,ko}.txt  trade names
    Resources/legality/mgdb/wc6full.pkl, wc6.pkl                EncounterEvent.MGDB_G6 (Wonder Cards)

Only the Wonder Cards that give a Pokemon are emitted: the others (items) can never match an evolution
chain.

Regenerate:  python tools/gen_encounters6.py
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_encounters import WILD, _load, split_blobs_uint32, read_cs, strip_comments, parse_array, parse_template, load_enum, ribbon_set_text, ribbons_from_bit_bytes, CARD_RIBBONS_67, CARD_RIBBON_OFFSET_67  # noqa: E402
from gen_encounters7 import (NATURE, VERSION, CARD_TYPE_POKEMON, WONDER_CARD_SIZE, WONDER_CARD_FULL_SIZE, WONDER_CARD_GIFT_START, number,  # noqa: E402
                             moveset_text, individual_values, boolean, last_token, ability, shiny, designated, form_value, static_fields,
                             read_trade_names, emit_name_tables, wonder_card_67_fields)
from pkhex_source import pkhex_path  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_H = os.path.join(ROOT, "include", "Legality", "Encounters", "EncounterData6.h")
OUT_CPP = os.path.join(ROOT, "src", "Legality", "Encounters", "EncounterData6.cpp")

XY_SOURCE = "Legality/Encounters/Data/Gen6/Encounters6XY.cs"
AO_SOURCE = "Legality/Encounters/Data/Gen6/Encounters6AO.cs"
AREA_XY_SOURCE = "Legality/Encounters/Templates/Gen6/EncounterArea6XY.cs"

MOVE = load_enum("Game/Enums/Move.cs", "Move")
# PKHeX GetLanguageStrings(file, 8): language id -> code; 0 and 6 have none
LANGUAGE_CODES = {1: "ja", 2: "en", 3: "fr", 4: "it", 5: "de", 7: "es", 8: "ko"}
LANGUAGE_MAX = 8
SLOT_TYPE6 = ["Standard", "Grass", "Surf", "Old_Rod", "Good_Rod", "Super_Rod", "Rock_Smash", "Horde", "FriendSafari"]

# PKHeX EncounterArea6XY: the Friend Safari
FRIEND_SAFARI_LOCATION = 148
FRIEND_SAFARI_LEVEL = 30
SPECIES_FLOETTE = 670
SPECIES_VIVILLON = 666
EGG_GROUP_UNDISCOVERED = 15
PERSONAL_XY_SIZE = 0x40
PERSONAL_XY_EGG_GROUP1 = 0x16


# ------------------------------------------------------------------------------------------- wild
def read_personal_xy():
    return _load(pkhex_path("Resources/byte/personal/personal_xy"))


def flawless_iv_count_xy(personal, species, is_friend_safari):
    """PKHeX EncounterSlot6XY.FlawlessIVCount, read against PersonalTable.XY[Species] (the base form's row)."""
    egg_group = personal[species * PERSONAL_XY_SIZE + PERSONAL_XY_EGG_GROUP1]
    if egg_group == EGG_GROUP_UNDISCOVERED:
        return 3
    return 2 if is_friend_safari else 0


def read_friend_safari_species():
    text = strip_comments(read_cs(AREA_XY_SOURCE))
    match = re.search(r"AllFriendSafariSpecies\s*=>\s*\[(.*?)\];", text, re.S)
    if not match:
        raise SystemExit("AllFriendSafariSpecies not found")
    return [number(part) for part in match.group(1).split(",") if part.strip()]


def emit_friend_safari(personal):
    """PKHeX EncounterArea6XY(): one area, Version XY, that both X and Y append to their own."""
    slots = [(species, 0) for species in read_friend_safari_species()]
    slots += [(SPECIES_FLOETTE, 0), (SPECIES_FLOETTE, 1), (SPECIES_FLOETTE, 3), (SPECIES_VIVILLON, "FORM_VIVILLON")]
    lines = []
    for species, form in slots:
        lines.append("                EncounterSlot6XY(AREA_FRIEND_SAFARI, %d, %s, %d, %d, %d)," % (species, form, FRIEND_SAFARI_LEVEL, FRIEND_SAFARI_LEVEL,
                                                                                                  flawless_iv_count_xy(personal, species, True)))
    return ("            extern const EncounterArea6XY AREA_FRIEND_SAFARI;\n"
            + "            const EncounterSlot6XY SLOTS_FRIEND_SAFARI[] = {\n%s\n            };\n" % "\n".join(lines)
            + "            constexpr uint16_t FRIEND_SAFARI_SLOT_COUNT = %d;\n" % len(slots)
            + "            const EncounterArea6XY AREA_FRIEND_SAFARI = EncounterArea6XY(Enums::GameVersion::XY, SlotType6::FriendSafari, %d, SLOTS_FRIEND_SAFARI, FRIEND_SAFARI_SLOT_COUNT);\n"
            % FRIEND_SAFARI_LOCATION)


def emit_wild6(name, file_name, version, area_class, slot_class, personal):
    """PKHeX EncounterArea6XY/6AO: header (u16 location, slot type, pad), then 4-byte slots (species|form<<11, min, max).
    X and Y end with the shared Friend Safari area."""
    areas = split_blobs_uint32(_load(WILD("Gen6", file_name)))
    slot_lines, area_lines = [], []
    for area_index, area in enumerate(areas):
        location = struct.unpack_from("<H", area, 0)[0]
        slot_type = area[2]
        body = area[4:]
        first_slot = len(slot_lines)
        for offset in range(0, (len(body) // 4) * 4, 4):
            packed, level_min, level_max = struct.unpack_from("<HBB", body, offset)
            species, form = packed & 0x3FF, packed >> 11
            if personal is not None:
                slot_lines.append("                %s(AREAS_%s[%d], %d, %d, %d, %d, %d)," % (slot_class, name, area_index, species, form, level_min, level_max,
                                                                                         flawless_iv_count_xy(personal, species, False)))
            else:
                slot_lines.append("                %s(AREAS_%s[%d], %d, %d, %d, %d)," % (slot_class, name, area_index, species, form, level_min, level_max))
        area_lines.append("                %s(Enums::GameVersion::%s, SlotType6::%s, %d, &SLOTS_%s[%d], %d)," % (area_class, version, SLOT_TYPE6[slot_type], location, name,
                                                                                                              first_slot, len(slot_lines) - first_slot))
    count = len(areas)
    if personal is not None:
        # The slots' parent is AREA_FRIEND_SAFARI; this row is an identical copy for the location search.
        area_lines.append("                EncounterArea6XY(Enums::GameVersion::XY, SlotType6::FriendSafari, %d, SLOTS_FRIEND_SAFARI, FRIEND_SAFARI_SLOT_COUNT)," % FRIEND_SAFARI_LOCATION)
        count += 1
    return ("            extern const %s AREAS_%s[%d];\n" % (area_class, name, count)
            + "            const %s SLOTS_%s[] = {\n%s\n            };\n" % (slot_class, name, "\n".join(slot_lines))
            + "            const %s AREAS_%s[%d] = {\n%s\n            };\n" % (area_class, name, count, "\n".join(area_lines)))


# ---------------------------------------------------------------------------------------- statics
STATIC6_ORDER = ["version", "speciesId", "formId", "level", "location", "eggLocation", "moves", "individualValues", "isFatefulEncounter",
                 "flawlessIVCount", "shiny", "ability", "gender", "nature", "fixedBall", "eggCycles", "contestStats"]
CONTEST_ORDER = ["ContestCool", "ContestBeauty", "ContestCute", "ContestSmart", "ContestTough"]


def moves6(expr):
    match = re.fullmatch(r"new\s*\((.*)\)", expr.strip(), re.S)
    if not match:
        raise SystemExit("not a moveset: %r" % expr)
    moves = []
    for part in match.group(1).split(","):
        part = part.strip()
        cast = re.fullmatch(r"\(int\)\s*Move\.(\w+)", part)
        moves.append(MOVE[cast.group(1)] if cast else number(part))
    return moveset_text(moves)


def emit_statics6(text, array_name, name, bases=None):
    lines = []
    for args, props in parse_array(text, array_name, bases):
        props = dict(props)
        extra = {}
        if "Moves" in props:
            extra["moves"] = moves6(props.pop("Moves"))
        if "EggCycles" in props:
            extra["eggCycles"] = str(number(props.pop("EggCycles")))
        contest = [props.pop(key, "0") for key in CONTEST_ORDER]
        if any(number(value) for value in contest):
            extra["contestStats"] = "{%s, 0}" % ", ".join(str(number(value)) for value in contest)
        fields = static_fields(args[0], props, array_name)
        fields.update(extra)
        lines.append("                EncounterStatic6({%s})," % designated(fields, STATIC6_ORDER, array_name))
    return "            const EncounterStatic6 %s[] = {\n%s\n            };\n" % (name, "\n".join(lines))


# ----------------------------------------------------------------------------------------- trades
TRADE6_ORDER = ["nicknames", "trainerNames", "version", "trainerMemory", "trainerMemoryIntensity", "trainerMemoryFeeling", "trainerMemoryVariable",
                "speciesId", "level", "nature", "TID16", "ability", "gender", "trainerGender", "individualValues", "isFixedNickname"]


def emit_trades6(text, array_name, name, prefix, names_by_language):
    """PKHeX EncounterTrade6(names, index, version, memory, intensity, feeling, variable)."""
    lines = []
    used = set()
    half = len(names_by_language[1]) >> 1
    for args, props in parse_array(text, array_name):
        index = number(args[1])
        used.update((index, index + half))
        fields = {"nicknames": "&%s_%d" % (prefix, index), "trainerNames": "&%s_%d" % (prefix, index + half),
                  "version": "Enums::GameVersion::%s" % VERSION[last_token(args[2])], "trainerMemory": str(number(args[3])),
                  "trainerMemoryIntensity": str(number(args[4])), "trainerMemoryFeeling": str(number(args[5])),
                  "trainerMemoryVariable": str(number(args[6])), "speciesId": str(number(props.pop("Species"))),
                  "level": str(number(props.pop("Level"))), "ability": ability(props.pop("Ability")), "TID16": str(number(props.pop("TID16"))),
                  "gender": str(number(props.pop("Gender"))), "trainerGender": str(number(props.pop("OTGender"))),
                  "nature": str(NATURE[last_token(props.pop("Nature"))])}
        ivs = props.pop("IVs")
        if ivs.strip() != "default":
            fields["individualValues"] = individual_values(ivs)
        if "IsFixedNickname" in props:
            fields["isFixedNickname"] = boolean(props.pop("IsFixedNickname"))
        if props:
            raise SystemExit("%s: unhandled properties %r" % (array_name, props))
        lines.append("                EncounterTrade6({%s})," % designated(fields, TRADE6_ORDER, array_name))
    block = "            const EncounterTrade6 %s[] = {\n%s\n            };\n" % (name, "\n".join(lines))
    return emit_name_tables(prefix, names_by_language, used, LANGUAGE_MAX) + block


# ---------------------------------------------------------------------------------- Wonder Cards
CARD6_ORDER = ["cardId", "restrictVersion", "restrictLanguage", "TID16", "SID16", "originGame", "encryptionConstant", "ball", "heldItem", "moves",
               "speciesId", "formId", "language", "isNicknamed", "nature", "gender", "abilityType", "pidType", "eggLocation", "location",
               "contestStats", "individualValues", "trainerGender", "trainerName", "level", "isEgg", "PID", "relearnMoves", "trainerMemoryIntensity",
               "trainerMemory", "trainerMemoryVariable", "trainerMemoryFeeling", "hasClassicRibbon", "ribbons"]
RIBBON_BYTE_1 = 0x75
RIBBON_CLASSIC_BIT = 5


def read_wonder_cards6():
    """PKHeX WC6Full.GetArray: the full cards (with their restrictions) first, then the bare cards."""
    cards = []
    full = _load(pkhex_path("Resources/legality/mgdb/wc6full.pkl"))
    plain = _load(pkhex_path("Resources/legality/mgdb/wc6.pkl"))
    if len(full) % WONDER_CARD_FULL_SIZE or len(plain) % WONDER_CARD_SIZE:
        raise SystemExit("wc6full.pkl / wc6.pkl are not whole cards")
    for offset in range(0, len(full), WONDER_CARD_FULL_SIZE):
        cards.append((full[offset + WONDER_CARD_GIFT_START:offset + WONDER_CARD_FULL_SIZE], full[offset], full[offset + 0x1FF]))
    for offset in range(0, len(plain), WONDER_CARD_SIZE):
        cards.append((plain[offset:offset + WONDER_CARD_SIZE], 0, 0))
    return cards


def emit_wonder_cards6(name):
    lines = []
    for card, restrict_version, restrict_language in read_wonder_cards6():
        if card[0x51] != CARD_TYPE_POKEMON or struct.unpack_from("<H", card, 0x82)[0] == 0:
            continue
        fields = wonder_card_67_fields(card, restrict_version, restrict_language, False)
        if card[RIBBON_BYTE_1] & (1 << RIBBON_CLASSIC_BIT):
            fields["hasClassicRibbon"] = "true"
        ribbons = ribbon_set_text(ribbons_from_bit_bytes(card, CARD_RIBBON_OFFSET_67, CARD_RIBBONS_67))
        if ribbons:
            fields["ribbons"] = ribbons
        lines.append("                MysteryGiftWC6({%s})," % designated(fields, CARD6_ORDER, name))
    return "            const MysteryGiftWC6 %s[] = {\n%s\n            };\n" % (name, "\n".join(lines))


def main():
    xy = strip_comments(read_cs(XY_SOURCE))
    ao = strip_comments(read_cs(AO_SOURCE))
    personal = read_personal_xy()

    blocks, accessors = [], []

    def accessor(function, element, array):
        accessors.append((function, element, array))

    blocks.append(emit_friend_safari(personal))
    for name, file_name, version in (("X", "encounter_x.pkl", "X"), ("Y", "encounter_y.pkl", "Y")):
        blocks.append(emit_wild6(name, file_name, version, "EncounterArea6XY", "EncounterSlot6XY", personal))
        accessor("getAreas6" + version, "EncounterArea6XY", "AREAS_" + name)
    for name, file_name, version in (("AS", "encounter_as.pkl", "AS"), ("OR", "encounter_or.pkl", "OR")):
        blocks.append(emit_wild6(name, file_name, version, "EncounterArea6AO", "EncounterSlot6AO", None))
        accessor("getAreas6" + version, "EncounterArea6AO", "AREAS_" + name)

    cosplay = {"BaseCosplay": parse_template(ao, "BaseCosplay")}
    for text, array_name, name, function, bases in ((xy, "Encounter_XY", "STATIC_XY", "getStatic6XY", None), (xy, "StaticX", "STATIC_X", "getStatic6X", None),
                                                    (xy, "StaticY", "STATIC_Y", "getStatic6Y", None),
                                                    (ao, "Encounter_AO_Regular", "STATIC_AO", "getStatic6AO", cosplay),
                                                    (ao, "StaticA", "STATIC_AS", "getStatic6AS", None), (ao, "StaticO", "STATIC_OR", "getStatic6OR", None)):
        blocks.append(emit_statics6(text, array_name, name, bases))
        accessor(function, "EncounterStatic6", name)

    blocks.append(emit_trades6(xy, "TradeGift_XY", "TRADE_GIFT_XY", "TRADE_NAMES_XY", read_trade_names("gen6", "tradexy", LANGUAGE_CODES)))
    accessor("getTradeGift6XY", "EncounterTrade6", "TRADE_GIFT_XY")
    blocks.append(emit_trades6(ao, "TradeGift_AO", "TRADE_GIFT_AO", "TRADE_NAMES_AO", read_trade_names("gen6", "tradeao", LANGUAGE_CODES)))
    accessor("getTradeGift6AO", "EncounterTrade6", "TRADE_GIFT_AO")

    blocks.append(emit_wonder_cards6("WONDER_CARDS_6"))
    accessor("getMysteryGifts6", "MysteryGiftWC6", "WONDER_CARDS_6")

    declarations = "\n".join("        std::span<const %s> %s() noexcept;" % (element, function) for function, element, _ in accessors)
    definitions = "\n\n".join("        std::span<const %s> %s() noexcept\n        {\n            return %s;\n        }" % (element, function, array) for function, element, array in accessors)

    header = """/**
 * Auto-generated by tools/gen_encounters6.py from PKHeX's Generation 6 encounter data. DO NOT EDIT BY HAND --
 * rerun the generator instead.
 *
 * One accessor per PKHeX array (Encounters6XY, Encounters6AO, EncounterEvent.MGDB_G6), unmerged and in
 * PKHeX's order.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERDATA6_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERDATA6_H

#include <span>

#include "Legality/Encounters/EncounterArea6AO.h"
#include "Legality/Encounters/EncounterArea6XY.h"
#include "Legality/Encounters/EncounterStatic6.h"
#include "Legality/Encounters/EncounterTrade6.h"
#include "Legality/Encounters/MysteryGiftWC6.h"

namespace Legality
{
    namespace Encounters
    {
%s
    }
}

#endif
""" % declarations
    source = """// Auto-generated by tools/gen_encounters6.py. DO NOT EDIT BY HAND.
#include "Legality/Encounters/EncounterData6.h"

#include "Legality/Encounters/EncounterSlot6AO.h"
#include "Legality/Encounters/EncounterSlot6XY.h"

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
