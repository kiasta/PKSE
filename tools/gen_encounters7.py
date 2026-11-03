#!/usr/bin/env python3
"""Generate include/Legality/Encounters/EncounterData7.h + src/Legality/Encounters/EncounterData7.cpp:
PKHeX's Generation 7 (Sun, Moon, Ultra Sun, Ultra Moon) and Let's Go encounter templates, unmerged and in
PKHeX's own order, as the encounter engine's template classes.

SOURCES (PKHeX, via tools/pkhex_source.py; PKHEX_LOCAL points it at a checkout)
    Resources/legality/wild/Gen7/encounter_{sn,mn,us,um}.pkl    EncounterArea7.GetAreas
    Resources/legality/wild/Gen7/encounter_{gp,ge}.pkl          EncounterArea7b.GetAreas
    Legality/Encounters/Data/Gen7/Encounters7SM.cs, Encounters7USUM.cs, Encounters7GG.cs   statics and trades
    Resources/legality/gen7/text_trade{sm,usum}_{ja,en,fr,it,de,es,ko,zh-Hans,zh-Hant}.txt  trade names
    Resources/legality/mgdb/wc7full.pkl, wc7.pkl                EncounterEvent.MGDB_G7 (Wonder Cards)
    Resources/legality/mgdb/wb7full.pkl                         EncounterEvent.MGDB_G7GG (Let's Go cards)

Only the Wonder Cards that give a Pokemon are emitted: the others (items, Battle Points, Poke Beans) can
never match an evolution chain.

Regenerate:  python tools/gen_encounters7.py
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_encounters import WILD, _load, split_blobs_uint32, read_cs, strip_comments, parse_array, load_enum, ribbon_set_text, ribbons_from_bit_bytes, CARD_RIBBONS_67, CARD_RIBBON_OFFSET_67  # noqa: E402
from pkhex_source import pkhex_path  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_H = os.path.join(ROOT, "include", "Legality", "Encounters", "EncounterData7.h")
OUT_CPP = os.path.join(ROOT, "src", "Legality", "Encounters", "EncounterData7.cpp")

SM_SOURCE = "Legality/Encounters/Data/Gen7/Encounters7SM.cs"
USUM_SOURCE = "Legality/Encounters/Data/Gen7/Encounters7USUM.cs"
GG_SOURCE = "Legality/Encounters/Data/Gen7/Encounters7GG.cs"

NATURE = load_enum("Game/Enums/Nature.cs", "Nature")
VERSION = {"SN": "SN", "MN": "MN", "US": "US", "UM": "UM", "SM": "SM", "USUM": "USUM", "GP": "GP", "GE": "GE", "GG": "GG",
           "X": "X", "Y": "Y", "AS": "AS", "OR": "OR", "XY": "XY", "ORAS": "ORAS"}
ABILITY = {"OnlyFirst", "OnlySecond", "OnlyHidden", "Any12", "Any12H"}
SHINY = {"Random", "Never", "Always"}
# PKHeX GetLanguageStrings(file, 10): language id -> code; 0 and 6 have none
LANGUAGE_CODES = {1: "ja", 2: "en", 3: "fr", 4: "it", 5: "de", 7: "es", 8: "ko", 9: "zh-Hans", 10: "zh-Hant"}
LANGUAGE_COUNT = 11
SLOT_TYPE7 = ["Standard", "SOS"]

WONDER_CARD_SIZE = 0x108
WONDER_CARD_FULL_SIZE = 0x310
WONDER_CARD_GIFT_START = WONDER_CARD_FULL_SIZE - WONDER_CARD_SIZE
CARD_TYPE_POKEMON = 0
WB7_SIZE = 0x310
WB7_CARD_START = 0x208


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


def moveset_text(moves):
    moves = list(moves) + [0] * (4 - len(moves))
    return "Moveset{{%s}}" % ", ".join(str(move) for move in moves)


def moveset(expr):
    match = re.fullmatch(r"new\s*\(([^)]*)\)", expr.strip())
    if not match:
        raise SystemExit("not a moveset: %r" % expr)
    return moveset_text([number(part) for part in match.group(1).split(",")])


def individual_values(expr):
    match = re.fullmatch(r"new\s*\(([^)]*)\)", expr.strip())
    if not match:
        raise SystemExit("not an IV set: %r" % expr)
    return "RNG::IndividualValueSet{%s, true}" % ", ".join(str(number(part)) for part in match.group(1).split(","))


def boolean(expr):
    return {"true": "true", "false": "false"}[expr.strip()]


def last_token(expr):
    return expr.strip().split(".")[-1]


def ability(expr):
    name = last_token(expr)
    if name not in ABILITY:
        raise SystemExit("unknown ability permission %r" % name)
    return "RNG::AbilityPermission::%s" % name


def shiny(expr):
    name = last_token(expr)
    if name not in SHINY:
        raise SystemExit("unknown shiny %r" % name)
    return "RNG::Shiny::%s" % name


def designated(fields, order, context):
    """C++20 designated initializers must follow the member order."""
    unknown = set(fields) - set(order)
    if unknown:
        raise SystemExit("%s: no member for %r" % (context, unknown))
    return ", ".join(".%s = %s" % (member, fields[member]) for member in order if member in fields)


def form_value(expr):
    expr = expr.strip()
    if last_token(expr) == "FormVivillon":
        return "FORM_VIVILLON"
    return str(number(expr))


# ------------------------------------------------------------------------------------------- wild
def emit_wild7(name, file_name, version):
    """PKHeX EncounterArea7: header (u16 location, slot type, pad), then 4-byte slots (species|form<<11, min, max)."""
    areas = split_blobs_uint32(_load(WILD("Gen7", file_name)))
    slot_lines, area_lines = [], []
    for area_index, area in enumerate(areas):
        location = struct.unpack_from("<H", area, 0)[0]
        slot_type = area[2]
        body = area[4:]
        first_slot = len(slot_lines)
        for offset in range(0, (len(body) // 4) * 4, 4):
            packed, level_min, level_max = struct.unpack_from("<HBB", body, offset)
            slot_lines.append("                EncounterSlot7(AREAS_%s[%d], %d, %d, %d, %d)," % (name, area_index, packed & 0x3FF, packed >> 11, level_min, level_max))
        area_lines.append("                EncounterArea7(Enums::GameVersion::%s, SlotType7::%s, %d, &SLOTS_%s[%d], %d)," % (version, SLOT_TYPE7[slot_type], location, name, first_slot, len(slot_lines) - first_slot))
    return ("            extern const EncounterArea7 AREAS_%s[%d];\n" % (name, len(areas))
            + "            const EncounterSlot7 SLOTS_%s[] = {\n%s\n            };\n" % (name, "\n".join(slot_lines))
            + "            const EncounterArea7 AREAS_%s[%d] = {\n%s\n            };\n" % (name, len(areas), "\n".join(area_lines)))


def emit_wild7b(name, file_name, version):
    """PKHeX EncounterArea7b: header (location, 0, two crossover locations), then 4-byte slots (species, flags, min, max)."""
    areas = split_blobs_uint32(_load(WILD("Gen7", file_name)))
    slot_lines, area_lines = [], []
    for area_index, area in enumerate(areas):
        location, _, crossover_first, crossover_second = struct.unpack_from("<BBBB", area, 0)
        body = area[4:]
        first_slot = len(slot_lines)
        for offset in range(0, (len(body) // 4) * 4, 4):
            species, flags, level_min, level_max = struct.unpack_from("<BBBB", body, offset)
            slot_lines.append("                EncounterSlot7b(AREAS_%s[%d], %d, %d, %d, %d)," % (name, area_index, species, flags, level_min, level_max))
        area_lines.append("                EncounterArea7b(Enums::GameVersion::%s, %d, %d, %d, &SLOTS_%s[%d], %d)," % (version, location, crossover_first, crossover_second, name, first_slot, len(slot_lines) - first_slot))
    return ("            extern const EncounterArea7b AREAS_%s[%d];\n" % (name, len(areas))
            + "            const EncounterSlot7b SLOTS_%s[] = {\n%s\n            };\n" % (name, "\n".join(slot_lines))
            + "            const EncounterArea7b AREAS_%s[%d] = {\n%s\n            };\n" % (name, len(areas), "\n".join(area_lines)))


# ---------------------------------------------------------------------------------------- statics
STATIC7_ORDER = ["version", "speciesId", "formId", "level", "location", "eggLocation", "relearnMoves", "individualValues", "isFatefulEncounter",
                 "flawlessIVCount", "shiny", "ability", "gender", "nature", "fixedBall"]


def static_fields(version, props, context):
    fields = {"version": "Enums::GameVersion::%s" % VERSION[last_token(version)], "speciesId": str(number(props.pop("Species"))),
              "level": str(number(props.pop("Level")))}
    if "Form" in props:
        fields["formId"] = form_value(props.pop("Form"))
    if "Location" in props:
        fields["location"] = str(number(props.pop("Location")))
    if "EggLocation" in props:
        fields["eggLocation"] = str(number(props.pop("EggLocation")))
    if "Relearn" in props:
        fields["relearnMoves"] = moveset(props.pop("Relearn"))
    if "IVs" in props:
        fields["individualValues"] = individual_values(props.pop("IVs"))
    if "FatefulEncounter" in props:
        fields["isFatefulEncounter"] = boolean(props.pop("FatefulEncounter"))
    if "FlawlessIVCount" in props:
        fields["flawlessIVCount"] = str(number(props.pop("FlawlessIVCount")))
    if "Shiny" in props:
        fields["shiny"] = shiny(props.pop("Shiny"))
    if "Ability" in props:
        fields["ability"] = ability(props.pop("Ability"))
    if "Gender" in props:
        fields["gender"] = str(number(props.pop("Gender")))
    if "Nature" in props:
        fields["nature"] = str(NATURE[last_token(props.pop("Nature"))])
    if "FixedBall" in props:
        fields["fixedBall"] = "Enums::Ball::%s" % last_token(props.pop("FixedBall"))
    if props:
        raise SystemExit("%s: unhandled properties %r" % (context, props))
    return fields


def emit_statics7(text, array_name, name):
    lines = []
    for args, props in parse_array(text, array_name):
        fields = static_fields(args[0], props, array_name)
        lines.append("                EncounterStatic7({%s})," % designated(fields, STATIC7_ORDER, array_name))
    return "            const EncounterStatic7 %s[] = {\n%s\n            };\n" % (name, "\n".join(lines))


STATIC7B_ORDER = ["version", "speciesId", "formId", "level", "location", "individualValues", "flawlessIVCount", "shiny", "fixedBall"]


def emit_statics7b(text, array_name, name):
    lines = []
    for args, props in parse_array(text, array_name):
        fields = static_fields(args[0], props, array_name)
        lines.append("                EncounterStatic7b({%s})," % designated(fields, STATIC7B_ORDER, array_name))
    return "            const EncounterStatic7b %s[] = {\n%s\n            };\n" % (name, "\n".join(lines))


# ----------------------------------------------------------------------------------------- trades
def read_trade_names(directory, resource, codes):
    names = {}
    for language, code in codes.items():
        path = pkhex_path("Resources/legality/%s/text_%s_%s.txt" % (directory, resource, code))
        with open(path, encoding="utf-8-sig") as handle:
            names[language] = handle.read().replace("\r", "").split("\n")
    return names


def emit_language_names(name, values):
    entries = [cpp_text(value) for value in values]
    return "            constexpr LanguageNamesSwitch %s = {%s};" % (name, ", ".join(entries))


def emit_name_tables(prefix, names_by_language, indices, language_count):
    """PKHeX EncounterUtil.GetNamesForLanguage: one row per index, empty where a language lacks it; nullptr past the table."""
    lines = []
    for index in sorted(indices):
        values = []
        for language in range(LANGUAGE_COUNT):
            if language > language_count:
                values.append(None)
                continue
            language_names = names_by_language.get(language, [])
            values.append(language_names[index] if index < len(language_names) else "")
        entries = ["nullptr" if value is None else cpp_text(value) for value in values]
        lines.append("            constexpr LanguageNamesSwitch %s_%d = {%s};" % (prefix, index, ", ".join(entries)))
    return "\n".join(lines) + "\n"


TRADE7_ORDER = ["nicknames", "trainerNames", "version", "speciesId", "formId", "level", "nature", "ID32", "ability", "gender", "trainerGender",
                "individualValues", "evolveOnTrade"]


def emit_trades7(text, array_name, name, prefix, names_by_language):
    lines = []
    used = set()
    half = len(names_by_language[1]) >> 1
    for args, props in parse_array(text, array_name):
        index = number(args[1])
        used.update((index, index + half))
        fields = {"nicknames": "&%s_%d" % (prefix, index), "trainerNames": "&%s_%d" % (prefix, index + half),
                  "version": "Enums::GameVersion::%s" % VERSION[last_token(args[2])], "speciesId": str(number(props.pop("Species"))),
                  "formId": str(number(props.pop("Form"))), "level": str(number(props.pop("Level"))), "ability": ability(props.pop("Ability")),
                  "ID32": str(number(props.pop("ID32"))), "individualValues": individual_values(props.pop("IVs")),
                  "trainerGender": str(number(props.pop("OTGender"))), "gender": str(number(props.pop("Gender"))),
                  "nature": str(NATURE[last_token(props.pop("Nature"))])}
        if "EvolveOnTrade" in props:
            fields["evolveOnTrade"] = boolean(props.pop("EvolveOnTrade"))
        if props:
            raise SystemExit("%s: unhandled properties %r" % (array_name, props))
        lines.append("                EncounterTrade7({%s})," % designated(fields, TRADE7_ORDER, array_name))
    block = "            const EncounterTrade7 %s[] = {\n%s\n            };\n" % (name, "\n".join(lines))
    return emit_name_tables(prefix, names_by_language, used, 10) + block


def read_string_arrays(text):
    arrays = {}
    for match in re.finditer(r"string\[\]\s+(\w+)\s*=\s*\[(.*?)\];", text, re.S):
        values = []
        for item in re.findall(r'string\.Empty|"((?:[^"\\]|\\.)*)"', match.group(2)):
            values.append(item)
        # re.findall yields "" for string.Empty and the literal's body otherwise.
        arrays[match.group(1)] = values
    return arrays


TRADE7B_ORDER = ["trainerNames", "version", "speciesId", "formId", "level", "ID32", "trainerGender", "individualValues"]


def emit_trades7b(text, array_name, name):
    lines = []
    for args, props in parse_array(text, array_name):
        fields = {"version": "Enums::GameVersion::%s" % VERSION[last_token(args[0])], "speciesId": str(number(props.pop("Species"))),
                  "formId": str(number(props.pop("Form"))), "level": str(number(props.pop("Level"))),
                  "trainerNames": "&TRADE_NAMES_GG_%s" % props.pop("TrainerNames").strip(), "ID32": str(number(props.pop("ID32"))),
                  "trainerGender": str(number(props.pop("OTGender"))), "individualValues": individual_values(props.pop("IVs"))}
        if props:
            raise SystemExit("%s: unhandled properties %r" % (array_name, props))
        lines.append("                EncounterTrade7b({%s})," % designated(fields, TRADE7B_ORDER, array_name))
    return "            const EncounterTrade7b %s[] = {\n%s\n            };\n" % (name, "\n".join(lines))


def emit_trade_names7b(text):
    """Encounters7GG's T1 .. T8: one name per language id, as PKHeX lists them."""
    lines = []
    for array_name, values in read_string_arrays(text).items():
        if len(values) != LANGUAGE_COUNT:
            raise SystemExit("%s: expected %d names, found %d" % (array_name, LANGUAGE_COUNT, len(values)))
        lines.append(emit_language_names("TRADE_NAMES_GG_" + array_name, values))
    return "\n".join(lines) + "\n"


# ---------------------------------------------------------------------------------- Wonder Cards
def card_text(card, offset, length=0x1A):
    """PKHeX StringConverter6/7.GetString: UTF-16LE up to the first null, the halfwidth gender signs normalized."""
    characters = []
    for index in range(0, length, 2):
        code = struct.unpack_from("<H", card, offset + index)[0]
        if code == 0:
            break
        characters.append({0xE08E: 0x2642, 0xE08F: 0x2640}.get(code, code))
    return "".join(chr(code) for code in characters)


def wonder_card_67_fields(card, restrict_version, restrict_language, has_met_level):
    """The WC6/WC7 Pokemon fields; both cards share the layout, WC6 lacks the met level byte."""
    tid, sid = struct.unpack_from("<HH", card, 0x68)
    moves = struct.unpack_from("<4H", card, 0x7A)
    relearn = struct.unpack_from("<4H", card, 0xD8)
    egg_location, location = struct.unpack_from("<HH", card, 0xA4)
    is_egg = card[0xD1] == 1
    nickname = card_text(card, 0x86)
    fields = {"cardId": str(struct.unpack_from("<H", card, 0)[0]), "restrictVersion": str(restrict_version), "restrictLanguage": str(restrict_language),
              "TID16": str(tid), "SID16": str(sid), "originGame": str(card[0x6C]), "encryptionConstant": "0x%08X" % struct.unpack_from("<I", card, 0x70)[0],
              "ball": str(card[0x76]), "heldItem": str(struct.unpack_from("<H", card, 0x78)[0]), "moves": moveset_text(moves),
              "speciesId": str(struct.unpack_from("<H", card, 0x82)[0]), "formId": str(card[0x84]), "language": str(card[0x85]),
              "isNicknamed": "true" if nickname or is_egg else "false", "nature": str(card[0xA0]), "gender": str(card[0xA1]),
              "abilityType": str(card[0xA2]), "pidType": "static_cast<ShinyType6>(%d)" % card[0xA3], "eggLocation": str(egg_location),
              "location": str(location), "contestStats": "{%s}" % ", ".join(str(value) for value in card[0xA9:0xAF]),
              "individualValues": "{%s}" % ", ".join(str(value) for value in card[0xAF:0xB5]), "trainerGender": str(card[0xB5]),
              "trainerName": cpp_text(card_text(card, 0xB6)), "level": str(card[0xD0]), "isEgg": "true" if is_egg else "false",
              "PID": "0x%08X" % struct.unpack_from("<I", card, 0xD4)[0], "relearnMoves": moveset_text(relearn),
              "trainerMemoryIntensity": str(card[0xE0]), "trainerMemory": str(card[0xE1]), "trainerMemoryVariable": str(struct.unpack_from("<H", card, 0xE2)[0]),
              "trainerMemoryFeeling": str(card[0xE4])}
    if has_met_level:
        fields["metLevel"] = str(card[0xA8])
    return fields


def read_wonder_cards_67(full_name, plain_name):
    """PKHeX WC6Full/WC7Full.GetArray: the full cards (with their restrictions) first, then the bare cards."""
    cards = []
    full = _load(pkhex_path("Resources/legality/mgdb/" + full_name))
    plain = _load(pkhex_path("Resources/legality/mgdb/" + plain_name))
    if len(full) % WONDER_CARD_FULL_SIZE or len(plain) % WONDER_CARD_SIZE:
        raise SystemExit("%s / %s are not whole cards" % (full_name, plain_name))
    for offset in range(0, len(full), WONDER_CARD_FULL_SIZE):
        cards.append((full[offset + WONDER_CARD_GIFT_START:offset + WONDER_CARD_FULL_SIZE], full[offset], full[offset + 0x1FF]))
    for offset in range(0, len(plain), WONDER_CARD_SIZE):
        cards.append((plain[offset:offset + WONDER_CARD_SIZE], 0, 0))
    return cards


def add_card_ribbons(fields, card):
    """PKHeX WC7/WB7 RIB0 and RIB1: the ribbons the card gives."""
    ribbons = ribbon_set_text(ribbons_from_bit_bytes(card, CARD_RIBBON_OFFSET_67, CARD_RIBBONS_67))
    if ribbons:
        fields["ribbons"] = ribbons


CARD7_ORDER = ["cardId", "restrictVersion", "restrictLanguage", "TID16", "SID16", "originGame", "encryptionConstant", "ball", "heldItem", "moves",
               "speciesId", "formId", "language", "isNicknamed", "nature", "gender", "abilityType", "pidType", "eggLocation", "location", "metLevel",
               "contestStats", "individualValues", "trainerGender", "trainerName", "level", "isEgg", "PID", "relearnMoves", "trainerMemoryIntensity",
               "trainerMemory", "trainerMemoryVariable", "trainerMemoryFeeling", "ribbons"]


def emit_wonder_cards7(name):
    lines = []
    for card, restrict_version, restrict_language in read_wonder_cards_67("wc7full.pkl", "wc7.pkl"):
        if card[0x51] != CARD_TYPE_POKEMON or struct.unpack_from("<H", card, 0x82)[0] == 0:
            continue
        fields = wonder_card_67_fields(card, restrict_version, restrict_language, True)
        add_card_ribbons(fields, card)
        lines.append("                MysteryGiftWC7({%s})," % designated(fields, CARD7_ORDER, name))
    return "            const MysteryGiftWC7 %s[] = {\n%s\n            };\n" % (name, "\n".join(lines))


WB7_ORDER = ["cardId", "restrictVersion", "TID16", "SID16", "originGame", "encryptionConstant", "ball", "heldItem", "moves", "speciesId", "formId",
             "nature", "gender", "abilityType", "pidType", "eggLocation", "location", "metLevel", "individualValues", "trainerGender", "level",
             "isEgg", "PID", "relearnMoves", "awakeningValues", "languages", "trainerNames", "ribbons"]
WB7_LANGUAGE_SLOTS = 9
WB7_LANGUAGE_OFFSET = 0x1D8
WB7_TRAINER_NAME_OFFSET = 0xEE
WB7_NAME_SIZE = 0x1A
WB7_HOME_CARD_MIN = 9000


def emit_wonder_cards7b(name):
    """PKHeX EncounterEvent.MGDB_G7GG (wb7full.pkl): whole 0x310 cards, the gift at 0x208."""
    data = _load(pkhex_path("Resources/legality/mgdb/wb7full.pkl"))
    if len(data) % WB7_SIZE:
        raise SystemExit("wb7full.pkl is not whole cards")
    lines = []
    for offset in range(0, len(data), WB7_SIZE):
        full = data[offset:offset + WB7_SIZE]
        card = full[WB7_CARD_START:]
        species = struct.unpack_from("<H", card, 0x82)[0]
        if card[0x51] != CARD_TYPE_POKEMON or species == 0:
            continue
        card_id = struct.unpack_from("<H", card, 0)[0]
        tid, sid = struct.unpack_from("<HH", card, 0x68)
        egg_location, location = struct.unpack_from("<HH", card, 0xA4)
        names = [card_text(full, WB7_TRAINER_NAME_OFFSET + slot * WB7_NAME_SIZE) for slot in range(WB7_LANGUAGE_SLOTS)]
        fields = {"cardId": str(card_id), "restrictVersion": str(full[0]), "TID16": str(tid), "SID16": str(sid),
                  "originGame": str(struct.unpack_from("<I", card, 0x6C)[0]), "encryptionConstant": "0x%08X" % struct.unpack_from("<I", card, 0x70)[0],
                  "ball": str(card[0x76]), "heldItem": str(struct.unpack_from("<H", card, 0x78)[0]), "moves": moveset_text(struct.unpack_from("<4H", card, 0x7A)),
                  "speciesId": str(species), "formId": str(card[0x84]), "nature": str(card[0xA0]), "gender": str(card[0xA1]),
                  "abilityType": str(card[0xA2] if card_id >= WB7_HOME_CARD_MIN else 3), "pidType": "static_cast<ShinyType6>(%d)" % card[0xA3],
                  "eggLocation": str(egg_location), "location": str(location), "metLevel": str(card[0xA8]),
                  "individualValues": "{%s}" % ", ".join(str(value) for value in card[0xAF:0xB5]), "trainerGender": str(card[0xB5]),
                  "level": str(card[0xD0]), "isEgg": "true" if card[0xD1] == 1 else "false", "PID": "0x%08X" % struct.unpack_from("<I", card, 0xD4)[0],
                  "relearnMoves": moveset_text(struct.unpack_from("<4H", card, 0xD8)),
                  "awakeningValues": "{%s}" % ", ".join(str(value) for value in card[0xE5:0xEB]),
                  "languages": "{%s}" % ", ".join(str(value) for value in full[WB7_LANGUAGE_OFFSET:WB7_LANGUAGE_OFFSET + WB7_LANGUAGE_SLOTS]),
                  "trainerNames": "{%s}" % ", ".join(cpp_text(value) for value in names)}
        add_card_ribbons(fields, card)
        lines.append("                MysteryGiftWB7({%s})," % designated(fields, WB7_ORDER, name))
    return "            const MysteryGiftWB7 %s[] = {\n%s\n            };\n" % (name, "\n".join(lines))


def main():
    sm = strip_comments(read_cs(SM_SOURCE))
    usum = strip_comments(read_cs(USUM_SOURCE))
    gg = strip_comments(read_cs(GG_SOURCE))

    blocks, accessors = [], []

    def accessor(function, element, array):
        accessors.append((function, element, array))

    for name, file_name, version in (("SN", "encounter_sn.pkl", "SN"), ("MN", "encounter_mn.pkl", "MN"), ("US", "encounter_us.pkl", "US"),
                                     ("UM", "encounter_um.pkl", "UM")):
        blocks.append(emit_wild7(name, file_name, version))
        accessor("getAreas7" + version, "EncounterArea7", "AREAS_" + name)

    for text, array_name, name, function in ((sm, "StaticSM", "STATIC_SM", "getStatic7SM"), (sm, "StaticSN", "STATIC_SN", "getStatic7SN"),
                                             (sm, "StaticMN", "STATIC_MN", "getStatic7MN"), (usum, "StaticUSUM", "STATIC_USUM", "getStatic7USUM"),
                                             (usum, "StaticUS", "STATIC_US", "getStatic7US"), (usum, "StaticUM", "STATIC_UM", "getStatic7UM")):
        blocks.append(emit_statics7(text, array_name, name))
        accessor(function, "EncounterStatic7", name)

    blocks.append(emit_trades7(sm, "TradeGift_SM", "TRADE_GIFT_SM", "TRADE_NAMES_SM", read_trade_names("gen7", "tradesm", LANGUAGE_CODES)))
    accessor("getTradeGift7SM", "EncounterTrade7", "TRADE_GIFT_SM")
    blocks.append(emit_trades7(usum, "TradeGift_USUM", "TRADE_GIFT_USUM", "TRADE_NAMES_USUM", read_trade_names("gen7", "tradeusum", LANGUAGE_CODES)))
    accessor("getTradeGift7USUM", "EncounterTrade7", "TRADE_GIFT_USUM")

    blocks.append(emit_wonder_cards7("WONDER_CARDS_7"))
    accessor("getMysteryGifts7", "MysteryGiftWC7", "WONDER_CARDS_7")

    for name, file_name, version in (("GP", "encounter_gp.pkl", "GP"), ("GE", "encounter_ge.pkl", "GE")):
        blocks.append(emit_wild7b(name, file_name, version))
        accessor("getAreas7b" + version, "EncounterArea7b", "AREAS_" + name)
    for array_name, name, function in (("Encounter_GG", "STATIC_GG", "getStatic7bGG"), ("StaticGP", "STATIC_GP", "getStatic7bGP"),
                                       ("StaticGE", "STATIC_GE", "getStatic7bGE")):
        blocks.append(emit_statics7b(gg, array_name, name))
        accessor(function, "EncounterStatic7b", name)
    blocks.append(emit_trade_names7b(gg))
    for array_name, name, function in (("TradeGift_GG", "TRADE_GIFT_GG", "getTradeGift7bGG"), ("TradeGift_GP", "TRADE_GIFT_GP", "getTradeGift7bGP"),
                                       ("TradeGift_GE", "TRADE_GIFT_GE", "getTradeGift7bGE")):
        blocks.append(emit_trades7b(gg, array_name, name))
        accessor(function, "EncounterTrade7b", name)
    blocks.append(emit_wonder_cards7b("WONDER_CARDS_7B"))
    accessor("getMysteryGifts7b", "MysteryGiftWB7", "WONDER_CARDS_7B")

    declarations = "\n".join("        std::span<const %s> %s() noexcept;" % (element, function) for function, element, _ in accessors)
    definitions = "\n\n".join("        std::span<const %s> %s() noexcept\n        {\n            return %s;\n        }" % (element, function, array) for function, element, array in accessors)

    header = """/**
 * Auto-generated by tools/gen_encounters7.py from PKHeX's Generation 7 encounter data. DO NOT EDIT BY HAND --
 * rerun the generator instead.
 *
 * One accessor per PKHeX array (Encounters7SM, Encounters7USUM, Encounters7GG, EncounterEvent.MGDB_G7 and
 * MGDB_G7GG), unmerged and in
 * PKHeX's order.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERDATA7_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERDATA7_H

#include <span>

#include "Legality/Encounters/EncounterArea7.h"
#include "Legality/Encounters/EncounterArea7b.h"
#include "Legality/Encounters/EncounterStatic7.h"
#include "Legality/Encounters/EncounterStatic7b.h"
#include "Legality/Encounters/EncounterTrade7.h"
#include "Legality/Encounters/EncounterTrade7b.h"
#include "Legality/Encounters/MysteryGiftWB7.h"
#include "Legality/Encounters/MysteryGiftWC7.h"

namespace Legality
{
    namespace Encounters
    {
%s
    }
}

#endif
""" % declarations
    source = """// Auto-generated by tools/gen_encounters7.py. DO NOT EDIT BY HAND.
#include "Legality/Encounters/EncounterData7.h"

#include "Legality/Encounters/EncounterSlot7.h"
#include "Legality/Encounters/EncounterSlot7b.h"

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
