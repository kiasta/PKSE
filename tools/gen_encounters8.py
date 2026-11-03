#!/usr/bin/env python3
"""Generate include/Legality/Encounters/EncounterData8.h + src/Legality/Encounters/EncounterData8.cpp:
PKHeX's Sword/Shield encounter templates, unmerged and in PKHeX's own order, as the encounter engine's
template classes.

SOURCES (PKHeX, via tools/pkhex_source.py; PKHEX_LOCAL points it at a checkout)
    Resources/legality/wild/Gen8/encounter_{sw,sh}_{symbol,hidden}.pkl   EncounterArea8.GetAreas
    Resources/legality/wild/Gen8/encounter_{sw,sh}_nest.pkl              Encounters8Nest.Nest_SW/SH
    Resources/legality/wild/Gen8/encounter_{sw,sh}_dist.pkl              Encounters8Nest.Dist_SW/SH
    Resources/legality/wild/Gen8/encounter_swsh_underground.pkl          Encounters8Nest.DynAdv_SWSH
    Legality/Encounters/Data/Gen8/Encounters8.cs                         statics and trades
    Legality/Encounters/Data/Gen8/Encounters8Nest.cs                     crystal dens, nest locations, water-only dens
    Legality/Encounters/Templates/Gen8/EncounterArea8.cs                 GetAreasCanWanderTo
    Resources/legality/gen8/text_tradeswsh_<language>.txt                trade nicknames and OT names
    Resources/legality/mgdb/wc8.pkl                                      EncounterEvent.MGDB_G8 (Wonder Cards)

Only the Wonder Cards that give a Pokemon are emitted: the others (items, BP, clothing) can never match an
evolution chain.

Regenerate:  python tools/gen_encounters8.py
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_encounters import (WILD, _load, split_blobs_uint32, read_cs, strip_comments, parse_array,  # noqa: E402
                            split_top, load_enum, _match, ribbon_set_text, ribbons_from_index_bytes)
from pkhex_source import pkhex_path  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_H = os.path.join(ROOT, "include", "Legality", "Encounters", "EncounterData8.h")
OUT_CPP = os.path.join(ROOT, "src", "Legality", "Encounters", "EncounterData8.cpp")

ENCOUNTERS_SOURCE = "Legality/Encounters/Data/Gen8/Encounters8.cs"
NEST_SOURCE = "Legality/Encounters/Data/Gen8/Encounters8Nest.cs"
AREA_SOURCE = "Legality/Encounters/Templates/Gen8/EncounterArea8.cs"

NATURE = load_enum("Game/Enums/Nature.cs", "Nature")
VERSION = {"SW": "SW", "SH": "SH", "SWSH": "SWSH"}
ABILITY = {"OnlyFirst": "OnlyFirst", "OnlySecond": "OnlySecond", "OnlyHidden": "OnlyHidden", "Any12": "Any12", "Any12H": "Any12H",
           "A0": "OnlyFirst", "A1": "OnlySecond", "A2": "OnlyHidden", "A3": "Any12", "A4": "Any12H"}
# PKHeX AbilityPermission stored as a signed byte in the raid blobs.
ABILITY_VALUE = {0xFF: "Any12H", 0: "Any12", 1: "OnlyFirst", 2: "OnlySecond", 4: "OnlyHidden"}
SHINY = {"Random": "Random", "Never": "Never", "Always": "Always"}
# PKHeX AreaWeather8 member -> EncounterRules8.h constant
WEATHER = {"None": "WEATHER8_NONE", "Normal": "WEATHER8_NORMAL", "Overcast": "WEATHER8_OVERCAST", "Raining": "WEATHER8_RAINING",
           "Thunderstorm": "WEATHER8_THUNDERSTORM", "Intense_Sun": "WEATHER8_INTENSE_SUN", "Snowing": "WEATHER8_SNOWING",
           "Snowstorm": "WEATHER8_SNOWSTORM", "Sandstorm": "WEATHER8_SANDSTORM", "Heavy_Fog": "WEATHER8_HEAVY_FOG", "All": "WEATHER8_ALL",
           "Stormy": "WEATHER8_STORMY", "Icy": "WEATHER8_ICY", "All_IoA": "WEATHER8_ALL_ISLE_OF_ARMOR", "All_CT": "WEATHER8_ALL_CROWN_TUNDRA",
           "No_Sun_Sand": "WEATHER8_NO_SUN_SAND", "All_Ballimere": "WEATHER8_ALL_BALLIMERE", "Shaking_Trees": "WEATHER8_SHAKING_TREES",
           "Fishing": "WEATHER8_FISHING"}
# PKHeX GetLanguageStrings(tradeSWSH, 10): language id -> code; 0 and 6 have none.
LANGUAGE_CODES = {1: "ja", 2: "en", 3: "fr", 4: "it", 5: "de", 7: "es", 8: "ko", 9: "zh-Hans", 10: "zh-Hant"}
LANGUAGE_COUNT = 11
# PKHeX EncounterStatic8N.LevelCaps
NEST_LEVEL_CAPS = [15, 20, 25, 30, 35, 40, 45, 50, 55, 60]
WONDER_CARD_SIZE = 0x2D0
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


def moveset_text(moves):
    moves = list(moves) + [0] * (4 - len(moves))
    return "Moveset{{%s}}" % ", ".join(str(move) for move in moves)


def moveset(expr):
    match = re.fullmatch(r"new\s*\(([^)]*)\)", expr.strip())
    if not match:
        raise SystemExit("not a moveset: %r" % expr)
    return moveset_text([number(part) for part in match.group(1).split(",")])


def individual_values_text(values):
    return "RNG::IndividualValueSet{%s, true}" % ", ".join(str(value) for value in values)


def individual_values(expr):
    match = re.fullmatch(r"new\s*\(([^)]*)\)", expr.strip())
    if not match:
        raise SystemExit("not an IV set: %r" % expr)
    return individual_values_text([number(part) for part in match.group(1).split(",")])


def boolean(expr):
    return {"true": "true", "false": "false"}[expr.strip()]


def last_token(expr):
    return expr.strip().split(".")[-1]


def weather(expr):
    parts = [last_token(part) for part in expr.split("|")]
    for part in parts:
        if part not in WEATHER:
            raise SystemExit("unknown AreaWeather8 %r" % part)
    return " | ".join(WEATHER[part] for part in parts)


def crossover(expr):
    match = re.fullmatch(r"new\s*\(([^)]*)\)", expr.strip())
    if not match:
        raise SystemExit("not a Crossover8: %r" % expr)
    values = [number(part) for part in match.group(1).split(",")]
    if len(values) > 7:
        raise SystemExit("Crossover8 holds 7 locations: %r" % expr)
    return "{%s}" % ", ".join(str(value) for value in values)


def designated(fields, order, context):
    """C++20 designated initializers must follow the member order."""
    unknown = set(fields) - set(order)
    if unknown:
        raise SystemExit("%s: no member for %r" % (context, unknown))
    return ", ".join(".%s = %s" % (member, fields[member]) for member in order if member in fields)


# ------------------------------------------------------------------------------------------- wild
def emit_wild(name, file_name, version, permit_crossover):
    """PKHeX EncounterArea8: location, slot count, then (u16 weather, min, max, count, type) blocks of u16 entries."""
    areas = split_blobs_uint32(_load(WILD("Gen8", file_name)))
    slot_lines, area_lines = [], []
    for area_index, area in enumerate(areas):
        location, slot_count = area[0], area[1]
        first_slot = len(slot_lines)
        offset, slots_read = 2, 0
        while slots_read != slot_count:
            area_weather, level_min, level_max, count, slot_type = struct.unpack_from("<HBBBB", area, offset)
            offset += 6
            for _ in range(count):
                packed = struct.unpack_from("<H", area, offset)[0]
                offset += 2
                slots_read += 1
                slot_lines.append("                EncounterSlot8(AREAS_%s[%d], %d, %d, %d, %d, 0x%03X, static_cast<SlotType8>(%d))," % (name, area_index, packed & 0x3FF, packed >> 11, level_min, level_max, area_weather, slot_type))
        area_lines.append("                EncounterArea8(Enums::GameVersion::%s, %d, %s, &SLOTS_%s[%d], %d)," % (version, location, "true" if permit_crossover else "false", name, first_slot, len(slot_lines) - first_slot))
    return ("            extern const EncounterArea8 AREAS_%s[%d];\n" % (name, len(areas))
            + "            const EncounterSlot8 SLOTS_%s[] = {\n%s\n            };\n" % (name, "\n".join(slot_lines))
            + "            const EncounterArea8 AREAS_%s[%d] = {\n%s\n            };\n" % (name, len(areas), "\n".join(area_lines)))


# ------------------------------------------------------------------------------- byte switch tables
def parse_byte_switch(text, signature):
    """`<signature> => x switch { N => [a, b], ... }` -> {N: [a, b]}."""
    match = re.search(signature + r"\s*=>\s*\w+\s+switch\s*\{", text)
    if not match:
        raise SystemExit("switch %r not found" % signature)
    brace = text.index("{", match.end() - 1)
    body = text[brace + 1:_match(text, brace, '{', '}') - 1]
    out = {}
    for key, values in re.findall(r"(\d+)\s*=>\s*\[([^\]]*)\]", body):
        out[int(key)] = [int(token) for token in re.findall(r"\d+", values)]
    return out


def emit_byte_lookup(prefix, function, parameter, table):
    """One constexpr array per key, and a switch returning a span (empty for anything else)."""
    lines = []
    for key in sorted(table):
        values = table[key]
        lines.append("            constexpr std::array<uint8_t, %d> %s_%d = {%s};" % (len(values), prefix, key, ", ".join(str(value) for value in values)))
    cases = []
    for key in sorted(table):
        cases.append("            case %d:\n                return %s_%d;" % (key, prefix, key))
    definition = ("        std::span<const uint8_t> %s(uint8_t %s) noexcept\n        {\n            switch (%s)\n            {\n%s\n            default:\n                return {};\n            }\n        }"
                  % (function, parameter, parameter, "\n".join(cases)))
    return "\n".join(lines) + "\n", definition


# ---------------------------------------------------------------------------------------- statics
STATIC_ORDER = ["version", "location", "speciesId", "formId", "level", "moves", "relearnMoves", "individualValues", "crossover", "weather",
                "dynamaxLevel", "nature", "shiny", "ability", "gender", "fixedBall", "flawlessIVCount", "isScriptedNoMarks", "canGigantamax",
                "isFatefulEncounter"]


def emit_statics(text, array_name, name):
    lines = []
    for args, props in parse_array(text, array_name):
        fields = {"version": "Enums::GameVersion::%s" % (VERSION[last_token(args[0])] if args else "SWSH")}
        fields["location"] = str(number(props.pop("Location")))
        fields["speciesId"] = str(number(props.pop("Species")))
        fields["level"] = str(number(props.pop("Level")))
        if "Form" in props:
            fields["formId"] = str(number(props.pop("Form")))
        if "Moves" in props:
            fields["moves"] = moveset(props.pop("Moves"))
        if "Relearn" in props:
            fields["relearnMoves"] = moveset(props.pop("Relearn"))
        if "IVs" in props:
            fields["individualValues"] = individual_values(props.pop("IVs"))
        if "Crossover" in props:
            fields["crossover"] = crossover(props.pop("Crossover"))
        if "Weather" in props:
            fields["weather"] = weather(props.pop("Weather"))
        if "DynamaxLevel" in props:
            fields["dynamaxLevel"] = str(number(props.pop("DynamaxLevel")))
        if "Nature" in props:
            fields["nature"] = str(NATURE[last_token(props.pop("Nature"))])
        if "Shiny" in props:
            fields["shiny"] = "RNG::Shiny::%s" % SHINY[last_token(props.pop("Shiny"))]
        if "Ability" in props:
            fields["ability"] = "RNG::AbilityPermission::%s" % ABILITY[last_token(props.pop("Ability"))]
        if "Gender" in props:
            fields["gender"] = str(number(props.pop("Gender")))
        if "FixedBall" in props:
            fields["fixedBall"] = "Enums::Ball::%s" % last_token(props.pop("FixedBall"))
        if "FlawlessIVCount" in props:
            fields["flawlessIVCount"] = str(number(props.pop("FlawlessIVCount")))
        if "ScriptedNoMarks" in props:
            fields["isScriptedNoMarks"] = boolean(props.pop("ScriptedNoMarks"))
        if "CanGigantamax" in props:
            fields["canGigantamax"] = boolean(props.pop("CanGigantamax"))
        if "FatefulEncounter" in props:
            fields["isFatefulEncounter"] = boolean(props.pop("FatefulEncounter"))
        if props:
            raise SystemExit("%s: unhandled properties %r" % (array_name, props))
        lines.append("                EncounterStatic8({%s})," % designated(fields, STATIC_ORDER, array_name))
    return "            const EncounterStatic8 %s[] = {\n%s\n            };\n" % (name, "\n".join(lines))


# ----------------------------------------------------------------------------------------- trades
TRADE_ORDER = ["nicknames", "trainerNames", "version", "speciesId", "formId", "level", "trainerMemory", "trainerMemoryVariable", "trainerMemoryFeeling",
               "trainerMemoryIntensity", "dynamaxLevel", "flawlessIVCount", "shiny", "nature", "ID32", "ability", "gender", "trainerGender",
               "individualValues", "relearnMoves"]


def read_trade_names():
    names = {}
    for language, code in LANGUAGE_CODES.items():
        path = pkhex_path("Resources/legality/gen8/text_tradeswsh_%s.txt" % code)
        with open(path, encoding="utf-8-sig") as handle:
            names[language] = handle.read().replace("\r", "").split("\n")
    return names


def emit_language_names(name, values):
    entries = [cpp_text(value) if value is not None else "nullptr" for value in values]
    return "            constexpr LanguageNamesSwitch %s = {%s};" % (name, ", ".join(entries))


def emit_trades(text, array_name, name, names_by_language, used_indices, constants):
    lines = []
    half = len(names_by_language[1]) >> 1
    for args, props in parse_array(text, array_name):
        fields = {}
        if args[0].strip() == "TradeNames":
            index = number(args[1])
            rest = args[2:]
            used_indices.update((index, index + half))
            fields["nicknames"] = "&TRADE_NAMES_SWSH_%d" % index
            fields["trainerNames"] = "&TRADE_NAMES_SWSH_%d" % (index + half)
            fields["shiny"] = "RNG::Shiny::Never"
        elif args[0].strip() == "TradeOT_R1":
            rest = args[1:]
            fields["trainerNames"] = "&TRADE_TRAINER_REGINA"
            fields["shiny"] = "RNG::Shiny::Random"
            fields["gender"] = "GENDER_RANDOM"
            fields["nature"] = "NATURE_RANDOM"
        else:
            raise SystemExit("%s: unknown trade constructor %r" % (array_name, args[0]))
        version, species, level, memory, variable, feeling, intensity = rest
        fields["version"] = "Enums::GameVersion::%s" % VERSION[last_token(version)]
        fields["speciesId"] = str(number(species))
        fields["level"] = str(number(level))
        fields["trainerMemory"] = str(number(memory))
        fields["trainerMemoryVariable"] = str(number(variable))
        fields["trainerMemoryFeeling"] = str(number(feeling))
        fields["trainerMemoryIntensity"] = str(number(intensity))
        fields["ability"] = "RNG::AbilityPermission::%s" % ABILITY[last_token(props.pop("Ability"))]
        fields["ID32"] = str(number(props.pop("ID32")))
        ivs = props.pop("IVs").strip()
        if ivs in constants:
            fields["individualValues"] = individual_values_text(constants[ivs])
        elif ivs != "default":
            fields["individualValues"] = individual_values(ivs)
        fields["dynamaxLevel"] = str(number(props.pop("DynamaxLevel")))
        fields["trainerGender"] = str(number(props.pop("OTGender")))
        if "Gender" in props:
            fields["gender"] = str(number(props.pop("Gender")))
        if "Nature" in props:
            fields["nature"] = str(NATURE[last_token(props.pop("Nature"))])
        if "FlawlessIVCount" in props:
            fields["flawlessIVCount"] = str(number(props.pop("FlawlessIVCount")))
        if "Relearn" in props:
            fields["relearnMoves"] = moveset(props.pop("Relearn"))
        if "Form" in props:
            fields["formId"] = str(number(props.pop("Form")))
        if props:
            raise SystemExit("%s: unhandled properties %r" % (array_name, props))
        lines.append("                EncounterTrade8({%s})," % designated(fields, TRADE_ORDER, array_name))
    return "            const EncounterTrade8 %s[] = {\n%s\n            };\n" % (name, "\n".join(lines))


def read_trade_constants(text):
    constants = {}
    for match in re.finditer(r"IndividualValueSet\s+(\w+)\s*=\s*new\s*\(([^)]*)\)", text):
        constants[match.group(1)] = [number(part) for part in match.group(2).split(",")]
    return constants


def read_regina(text):
    match = re.search(r"string\[\]\s+TradeOT_R1\s*=\s*\[([^\]]*)\]", text)
    values = []
    for part in split_top(match.group(1)):
        part = part.strip()
        if part == "string.Empty":
            values.append("")
        else:
            values.append(re.fullmatch(r'"((?:[^"\\]|\\.)*)"', part).group(1))
    if len(values) != LANGUAGE_COUNT:
        raise SystemExit("TradeOT_R1 has %d languages" % len(values))
    return values


# ------------------------------------------------------------------------------------------ raids
NEST_ORDER = ["version", "speciesId", "formId", "level", "moves", "individualValues", "dynamaxLevel", "shiny", "ability", "gender", "flawlessIVCount",
              "canGigantamax"]


def raid_ability(value):
    if value not in ABILITY_VALUE:
        raise SystemExit("unknown raid AbilityPermission %d" % value)
    return "RNG::AbilityPermission::%s" % ABILITY_VALUE[value]


def emit_nests(name, file_name, version):
    """PKHeX EncounterStatic8N.Read: species, form, gender, ability, gmax, nest, min rank, max rank, flawless."""
    data = _load(WILD("Gen8", file_name))
    lines = []
    for offset in range(0, len(data), 10):
        species, form, gender, ability, gigantamax, nest, min_rank, max_rank, flawless = struct.unpack_from("<HBBBBBBBB", data, offset)
        fields = {"version": "Enums::GameVersion::%s" % version, "speciesId": str(species), "formId": str(form), "level": str(NEST_LEVEL_CAPS[min_rank * 2]),
                  "dynamaxLevel": str(min_rank + 1), "ability": raid_ability(ability), "gender": str(gender), "flawlessIVCount": str(flawless),
                  "canGigantamax": "true" if gigantamax else "false"}
        lines.append("                EncounterStatic8N({%s}, %d, %d, %d)," % (designated(fields, NEST_ORDER, name), nest, min_rank, max_rank))
    return "            const EncounterStatic8N %s[] = {\n%s\n            };\n" % (name, "\n".join(lines))


def emit_distribution(name, file_name, version):
    """PKHeX EncounterStatic8ND.Read (16 bytes)."""
    data = _load(WILD("Gen8", file_name))
    lines = []
    for offset in range(0, len(data), 16):
        species, form, ability = struct.unpack_from("<HBB", data, offset)
        moves = struct.unpack_from("<4H", data, offset + 4)
        level, dynamax, flawless_shiny, index = struct.unpack_from("<BBBB", data, offset + 12)
        shiny = {1: "Never", 2: "Always"}.get(flawless_shiny >> 4, "Random")
        fields = {"version": "Enums::GameVersion::%s" % version, "speciesId": str(species), "formId": str(form), "level": str(level),
                  "moves": moveset_text(moves), "dynamaxLevel": str(dynamax & 0x7F), "shiny": "RNG::Shiny::%s" % shiny, "ability": raid_ability(ability),
                  "flawlessIVCount": str(flawless_shiny & 0xF), "canGigantamax": "true" if dynamax >= 0x80 else "false"}
        lines.append("                EncounterStatic8ND({%s}, %d)," % (designated(fields, NEST_ORDER, name), index))
    return "            const EncounterStatic8ND %s[] = {\n%s\n            };\n" % (name, "\n".join(lines))


def emit_underground(name):
    """PKHeX EncounterStatic8U.Read (14 bytes); every Max Lair raid is Dynamax level 8 with 4 flawless IVs."""
    data = _load(WILD("Gen8", "encounter_swsh_underground.pkl"))
    lines = []
    for offset in range(0, len(data), 14):
        species, form, level = struct.unpack_from("<HBB", data, offset)
        moves = struct.unpack_from("<4H", data, offset + 4)
        ability, gigantamax = struct.unpack_from("<BB", data, offset + 12)
        fields = {"version": "Enums::GameVersion::SWSH", "speciesId": str(species), "formId": str(form), "level": str(level), "moves": moveset_text(moves),
                  "dynamaxLevel": "8", "ability": raid_ability(ability), "flawlessIVCount": "4", "canGigantamax": "true" if gigantamax else "false"}
        lines.append("                EncounterStatic8U({%s})," % designated(fields, NEST_ORDER, name))
    return "            const EncounterStatic8U %s[] = {\n%s\n            };\n" % (name, "\n".join(lines))


def emit_crystal(text, name):
    lines = []
    for args, props in parse_array(text, "Crystal_SWSH"):
        fields = {"version": "Enums::GameVersion::%s" % VERSION[last_token(args[0])]}
        fields["speciesId"] = str(number(props.pop("Species")))
        fields["level"] = str(number(props.pop("Level")))
        fields["ability"] = "RNG::AbilityPermission::%s" % ABILITY[last_token(props.pop("Ability"))]
        fields["individualValues"] = individual_values(props.pop("IVs"))
        fields["dynamaxLevel"] = str(number(props.pop("DynamaxLevel")))
        fields["moves"] = moveset(props.pop("Moves"))
        if "CanGigantamax" in props:
            fields["canGigantamax"] = boolean(props.pop("CanGigantamax"))
        if "Form" in props:
            fields["formId"] = str(number(props.pop("Form")))
        if props:
            raise SystemExit("Crystal_SWSH: unhandled properties %r" % props)
        lines.append("                EncounterStatic8NC({%s})," % designated(fields, NEST_ORDER, name))
    return "            const EncounterStatic8NC %s[] = {\n%s\n            };\n" % (name, "\n".join(lines))


# ------------------------------------------------------------------------------------------- WC8
CARD_ORDER = ["cardId", "restrictVersion", "TID16", "SID16", "originGame", "encryptionConstant", "PID", "eggLocation", "location", "ball", "moves",
              "relearnMoves", "speciesId", "formId", "gender", "level", "isEgg", "nature", "abilityType", "shinyType", "metLevel", "dynamaxLevel",
              "canGigantamax", "individualValues", "trainerGender", "trainerMemoryIntensity", "trainerMemory", "trainerMemoryFeeling",
              "trainerMemoryVariable", "languageWords", "trainerNames", "nicknamedSlots", "ribbons"]


def card_text(card, offset):
    """PKHeX StringConverter8.GetString: UTF-16LE up to the first null, within the 0x1A-byte field."""
    raw = card[offset:offset + 0x1A]
    characters = []
    for index in range(0, len(raw), 2):
        code = struct.unpack_from("<H", raw, index)[0]
        if code == 0:
            break
        characters.append(chr(code))
    return "".join(characters)

CARD_RIBBON_OFFSET = 0x24C  # PKHeX WC8.RibbonBytesOffset


def emit_wonder_cards(name):
    data = _load(pkhex_path("Resources/legality/mgdb/wc8.pkl"))
    if len(data) % WONDER_CARD_SIZE:
        raise SystemExit("wc8.pkl is not a whole number of Wonder Cards")
    lines = []
    for offset in range(0, len(data), WONDER_CARD_SIZE):
        card = data[offset:offset + WONDER_CARD_SIZE]
        if card[0x11] != GIFT_TYPE_POKEMON:
            continue
        card_id = struct.unpack_from("<H", card, 0x8)[0]
        tid, sid, origin_game = struct.unpack_from("<HHi", card, 0x20)
        encryption_constant, pid = struct.unpack_from("<II", card, 0x28)
        egg_location, location, ball = struct.unpack_from("<HHH", card, 0x228)
        moves = struct.unpack_from("<4H", card, 0x230)
        relearn = struct.unpack_from("<4H", card, 0x238)
        species = struct.unpack_from("<H", card, 0x240)[0]
        form, gender, level, is_egg, nature, ability_type, shiny_type, met_level, dynamax, gigantamax = struct.unpack_from("<10B", card, 0x242)
        individual_values = struct.unpack_from("<6B", card, 0x26C)
        trainer_gender = card[0x272]
        memory_intensity, memory, memory_feeling = struct.unpack_from("<BBB", card, 0x279)
        memory_variable = struct.unpack_from("<H", card, 0x27C)[0]
        language_words, trainer_names, nicknamed = [], [], 0
        for slot in range(9):
            nickname_offset = 0x30 + slot * 0x1C
            language_words.append(struct.unpack_from("<H", card, nickname_offset + 0x1A)[0])
            if struct.unpack_from("<H", card, nickname_offset)[0] != 0:
                nicknamed |= 1 << slot
            trainer_names.append(cpp_text(card_text(card, 0x12C + slot * 0x1C)))
        fields = {"cardId": str(card_id), "restrictVersion": str(card[0xE]), "TID16": str(tid), "SID16": str(sid), "originGame": str(origin_game),
                  "encryptionConstant": "0x%08X" % encryption_constant, "PID": "0x%08X" % pid, "eggLocation": str(egg_location),
                  "location": str(location), "ball": str(ball & 0xFF), "moves": moveset_text(moves), "relearnMoves": moveset_text(relearn),
                  "speciesId": str(species), "formId": str(form), "gender": str(gender), "level": str(level), "isEgg": "true" if is_egg == 1 else "false",
                  "nature": str(nature), "abilityType": str(ability_type), "shinyType": "static_cast<ShinyType8>(%d)" % shiny_type,
                  "metLevel": str(met_level), "dynamaxLevel": str(dynamax), "canGigantamax": "true" if gigantamax else "false",
                  "individualValues": "{%s}" % ", ".join(str(value) for value in individual_values), "trainerGender": str(trainer_gender),
                  "trainerMemoryIntensity": str(memory_intensity), "trainerMemory": str(memory), "trainerMemoryFeeling": str(memory_feeling),
                  "trainerMemoryVariable": str(memory_variable), "languageWords": "{%s}" % ", ".join(str(word) for word in language_words),
                  "trainerNames": "{%s}" % ", ".join(trainer_names), "nicknamedSlots": "0x%03X" % nicknamed}
        ribbons = ribbon_set_text(ribbons_from_index_bytes(card, CARD_RIBBON_OFFSET))
        if ribbons:
            fields["ribbons"] = ribbons
        lines.append("                MysteryGiftWC8({%s})," % designated(fields, CARD_ORDER, name))
    return "            const MysteryGiftWC8 %s[] = {\n%s\n            };\n" % (name, "\n".join(lines))


def main():
    encounters = strip_comments(read_cs(ENCOUNTERS_SOURCE))
    nest = strip_comments(read_cs(NEST_SOURCE))
    area = strip_comments(read_cs(AREA_SOURCE))

    blocks, accessors, lookups = [], [], []

    def accessor(function, element, array):
        accessors.append((function, element, array))

    for name, function, file_name, version, permit_crossover in (("SW_SYMBOL", "SWSymbol", "encounter_sw_symbol.pkl", "SW", True),
                                                                 ("SH_SYMBOL", "SHSymbol", "encounter_sh_symbol.pkl", "SH", True),
                                                                 ("SW_HIDDEN", "SWHidden", "encounter_sw_hidden.pkl", "SW", False),
                                                                 ("SH_HIDDEN", "SHHidden", "encounter_sh_hidden.pkl", "SH", False)):
        blocks.append(emit_wild(name, file_name, version, permit_crossover))
        accessor("getAreas8" + function, "EncounterArea8", "AREAS_" + name)

    for array_name, name in (("StaticSWSH", "STATIC_SWSH"), ("StaticSW", "STATIC_SW"), ("StaticSH", "STATIC_SH")):
        blocks.append(emit_statics(encounters, array_name, name))
        accessor("getStatic8%s" % name.split("_", 1)[1], "EncounterStatic8", name)

    names = read_trade_names()
    constants = read_trade_constants(encounters)
    used = set()
    trade_blocks = []
    for array_name, name in (("TradeSWSH", "TRADE_SWSH"), ("TradeSW", "TRADE_SW"), ("TradeSH", "TRADE_SH")):
        trade_blocks.append(emit_trades(encounters, array_name, name, names, used, constants))
        accessor("getTrade8%s" % name.split("_", 1)[1], "EncounterTrade8", name)
    name_lines = []
    for index in sorted(used):
        values = []
        for language in range(LANGUAGE_COUNT):
            language_names = names.get(language, [])
            values.append(language_names[index] if index < len(language_names) else "")
        name_lines.append(emit_language_names("TRADE_NAMES_SWSH_%d" % index, values))
    name_lines.append(emit_language_names("TRADE_TRAINER_REGINA", read_regina(encounters)))
    blocks.append("\n".join(name_lines) + "\n")
    blocks.extend(trade_blocks)

    for name, function, file_name, version in (("NEST_SW", "NestSW", "encounter_sw_nest.pkl", "SW"), ("NEST_SH", "NestSH", "encounter_sh_nest.pkl", "SH")):
        blocks.append(emit_nests(name, file_name, version))
        accessor("getRaids8" + function, "EncounterStatic8N", name)
    for name, function, file_name, version in (("DISTRIBUTION_SW", "DistributionSW", "encounter_sw_dist.pkl", "SW"),
                                               ("DISTRIBUTION_SH", "DistributionSH", "encounter_sh_dist.pkl", "SH")):
        blocks.append(emit_distribution(name, file_name, version))
        accessor("getRaids8" + function, "EncounterStatic8ND", name)
    blocks.append(emit_underground("MAX_LAIR_SWSH"))
    accessor("getRaids8MaxLair", "EncounterStatic8U", "MAX_LAIR_SWSH")
    blocks.append(emit_crystal(nest, "CRYSTAL_SWSH"))
    accessor("getRaids8Crystal", "EncounterStatic8NC", "CRYSTAL_SWSH")

    blocks.append(emit_wonder_cards("WONDER_CARDS_8"))
    accessor("getGifts8WC8", "MysteryGiftWC8", "WONDER_CARDS_8")

    for prefix, function, parameter, signature, source in (("WANDER", "getAreasCanWanderTo8", "location", r"GetAreasCanWanderTo\(byte location\)", area),
                                                           ("NEST_LOCATIONS", "getNestLocations8", "nestIndex", r"GetNestLocations\(byte nestIndex\)", nest),
                                                           ("INACCESSIBLE_NESTS", "getInaccessibleRank12Nests8", "location",
                                                            r"GetInaccessibleRank12Nests\(byte location\)", nest)):
        table = parse_byte_switch(source, signature)
        block, definition = emit_byte_lookup(prefix, function, parameter, table)
        blocks.append(block)
        lookups.append(definition)
    water_match = re.search(r"InaccessibleRank12DistributionLocations\s*=>\s*\[([^\]]*)\]", nest)
    water_locations = [int(token) for token in re.findall(r"\d+", water_match.group(1))]
    blocks.append("            constexpr std::array<uint8_t, %d> INACCESSIBLE_DISTRIBUTION_LOCATIONS = {%s};\n" % (len(water_locations), ", ".join(str(value) for value in water_locations)))

    declarations = "\n".join("        std::span<const %s> %s() noexcept;" % (element, function) for function, element, _ in accessors)
    definitions = "\n\n".join("        std::span<const %s> %s() noexcept\n        {\n            return %s;\n        }" % (element, function, array) for function, element, array in accessors)

    header = """/**
 * Auto-generated by tools/gen_encounters8.py from PKHeX's Sword/Shield encounter data. DO NOT EDIT BY HAND --
 * rerun the generator instead.
 *
 * One accessor per PKHeX array (Encounters8, Encounters8Nest, EncounterEvent.MGDB_G8), unmerged and in PKHeX's
 * order, plus the location tables the templates read (EncounterArea8.GetAreasCanWanderTo,
 * Encounters8Nest.GetNestLocations, GetInaccessibleRank12Nests and InaccessibleRank12DistributionLocations).
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERDATA8_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERDATA8_H

#include <cstdint>
#include <span>

#include "Legality/Encounters/EncounterArea8.h"
#include "Legality/Encounters/EncounterStatic8.h"
#include "Legality/Encounters/EncounterStatic8Nest.h"
#include "Legality/Encounters/EncounterTrade8.h"
#include "Legality/Encounters/MysteryGiftWC8.h"

namespace Legality
{
    namespace Encounters
    {
%s

        /// PKHeX EncounterArea8.GetAreasCanWanderTo: the areas a symbol encounter can walk into.
        std::span<const uint8_t> getAreasCanWanderTo8(uint8_t location) noexcept;
        /// PKHeX Encounters8Nest.GetNestLocations: the met locations a stock den's raids report.
        std::span<const uint8_t> getNestLocations8(uint8_t nestIndex) noexcept;
        /// PKHeX Encounters8Nest.GetInaccessibleRank12Nests: the water-only dens of a met location.
        std::span<const uint8_t> getInaccessibleRank12Nests8(uint8_t location) noexcept;
        /// PKHeX Encounters8Nest.IsInaccessibleRank12Nest.
        bool isInaccessibleRank12Nest8(uint8_t nestIndex, uint8_t location) noexcept;
        /// PKHeX Encounters8Nest.InaccessibleRank12DistributionLocations contains `location`.
        bool isInaccessibleRank12DistributionLocation8(uint8_t location) noexcept;
    }
}

#endif
""" % declarations
    source = """// Auto-generated by tools/gen_encounters8.py. DO NOT EDIT BY HAND.
#include "Legality/Encounters/EncounterData8.h"

#include <array>

#include "Legality/Encounters/EncounterRules8.h"
#include "Legality/Encounters/EncounterSlot8.h"

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

%s

        bool isInaccessibleRank12Nest8(uint8_t nestIndex, uint8_t location) noexcept
        {
            for (const uint8_t inaccessibleNest : getInaccessibleRank12Nests8(location))
            {
                if (inaccessibleNest == nestIndex)
                {
                    return true;
                }
            }
            return false;
        }

        bool isInaccessibleRank12DistributionLocation8(uint8_t location) noexcept
        {
            for (const uint8_t waterLocation : INACCESSIBLE_DISTRIBUTION_LOCATIONS)
            {
                if (waterLocation == location)
                {
                    return true;
                }
            }
            return false;
        }
    }
}
""" % ("".join(blocks), definitions, "\n\n".join(lookups))
    with open(OUT_H, "w", newline="\n") as handle:
        handle.write(header)
    with open(OUT_CPP, "w", newline="\n") as handle:
        handle.write(source)
    print("wrote %s (%d arrays)" % (os.path.relpath(OUT_CPP, ROOT), len(accessors)))


if __name__ == "__main__":
    main()
