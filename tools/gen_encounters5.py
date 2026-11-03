#!/usr/bin/env python3
"""Generate include/Legality/Encounters/EncounterData5.h + src/Legality/Encounters/EncounterData5.cpp:
PKHeX's Generation 5 (Black, White, Black 2, White 2) encounter templates, unmerged and in PKHeX's own
order, as the encounter engine's template classes.

SOURCES (PKHeX, via tools/pkhex_source.py; PKHEX_LOCAL points it at a checkout)
    Resources/legality/wild/Gen5/encounter_{b,w,b2,w2}.pkl        EncounterArea5.GetAreas
    Legality/Encounters/Data/Gen5/Encounters5BW.cs, Encounters5B2W2.cs, Encounters5DR.cs
                                                                 statics, Entree Forest, N, Dream Radar, trades
    Resources/legality/gen5/text_trade{bw,b2w2}_{ja,en,fr,it,de,es,ko}.txt   trade nicknames and OT names
    Resources/byte/personal/personal_b2w2                         DreamWorldEntry's hidden-ability test
    Resources/legality/mgdb/pgf.pkl                               EncounterEvent.MGDB_G5 (PGF gifts)

Wild slots of species 0 are padding (Gen 5 areas have a fixed slot count) and are not emitted; PKHeX
builds them but no evolution chain can match them. Only gifts with a species are emitted, for the same
reason.

Regenerate:  python tools/gen_encounters5.py
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_encounters import WILD, _load, split_blobs_uint32, read_cs, strip_comments, parse_array, load_enum, ribbon_set_text, ribbons_from_bit_bytes, CARD_RIBBONS_PGF, CARD_RIBBON_OFFSET_PGF  # noqa: E402
from pkhex_source import pkhex_path  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_H = os.path.join(ROOT, "include", "Legality", "Encounters", "EncounterData5.h")
OUT_CPP = os.path.join(ROOT, "src", "Legality", "Encounters", "EncounterData5.cpp")

BW_SOURCE = "Legality/Encounters/Data/Gen5/Encounters5BW.cs"
B2W2_SOURCE = "Legality/Encounters/Data/Gen5/Encounters5B2W2.cs"
DR_SOURCE = "Legality/Encounters/Data/Gen5/Encounters5DR.cs"

# PKHeX GameVersion token -> PKSE Enums::GameVersion. PKSE has no Gen 5 lump, so PKHeX's Gen5 is Any.
VERSION = {"B": "B", "W": "W", "B2": "B2", "W2": "W2", "BW": "BW", "B2W2": "B2W2", "Gen5": "Any"}
SLOT_TYPE = ["Standard", "Grass", "Surf", "SuperRod", "Swarm", "HiddenGrotto"]
ABILITY = {"OnlyFirst", "OnlySecond", "OnlyHidden", "Any12", "Any12H"}
SHINY = {"Random", "Never", "Always"}
# PKHeX GetLanguageStrings(file, 8): language id -> code; 0 and 6 have none
LANGUAGE_CODES = {1: "ja", 2: "en", 3: "fr", 4: "it", 5: "de", 7: "es", 8: "ko"}
LANGUAGE_COUNT = 9

NATURE = load_enum("Game/Enums/Nature.cs", "Nature")
PROMOTION = load_enum("Legality/Encounters/Templates/Gen5/GlobalLinkPromotion.cs", "GlobalLinkPromotion")

# EncounterTrade5B2W2::Properties in declaration order (designated initializers must follow it).
B2W2_TRADE_FIELDS = ["nicknames", "trainerNames", "version", "isFixedNickname", "speciesId", "level", "ability", "trainerGender", "ID32", "formId", "gender",
                     "individualValues", "nature"]
PERSONAL_SIZE_B2W2 = 0x4C
PGF_SIZE = 0xCC


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


def individual_values(expr):
    match = re.fullmatch(r"new\s*\(([^)]*)\)", expr.strip())
    values = [number(part) for part in match.group(1).split(",")]
    return "RNG::IndividualValueSet{%s, true}" % ", ".join(str(value) for value in values)


def boolean(expr):
    return {"true": "true", "false": "false"}[expr.strip()]


def ball(expr):
    return "Enums::Ball::%s" % expr.strip().split(".")[-1]


def ability(expr):
    name = expr.strip().split(".")[-1]
    if name not in ABILITY:
        raise SystemExit("unknown ability permission %r" % name)
    return "RNG::AbilityPermission::%s" % name


def nature(expr):
    return NATURE[expr.strip().split(".")[-1]]


def shiny(expr):
    name = expr.strip().split(".")[-1]
    if name not in SHINY:
        raise SystemExit("unknown shiny %r" % name)
    return "RNG::Shiny::%s" % name


# ------------------------------------------------------------------------------------------- wild
def emit_wild(name, file_name, version):
    """PKHeX EncounterArea5: header (u16 location, type, pad), then 4-byte slots (species|form<<11, min, max)."""
    areas = split_blobs_uint32(_load(WILD("Gen5", file_name)))
    slot_lines, area_lines = [], []
    for area_index, area in enumerate(areas):
        location, slot_type = struct.unpack_from("<H", area, 0)[0], area[2]
        body = area[4:]
        first_slot = len(slot_lines)
        for offset in range(0, (len(body) // 4) * 4, 4):
            packed, level_min, level_max = struct.unpack_from("<HBB", body, offset)
            species = packed & 0x3FF
            if species == 0:
                continue
            slot_lines.append("                EncounterSlot5(AREAS_%s[%d], %d, %d, %d, %d)," % (name, area_index, species, packed >> 11, level_min, level_max))
        area_lines.append("                EncounterArea5(Enums::GameVersion::%s, SlotType5::%s, %d, &SLOTS_%s[%d], %d)," % (version, SLOT_TYPE[slot_type], location, name, first_slot, len(slot_lines) - first_slot))
    return ("            extern const EncounterArea5 AREAS_%s[%d];\n" % (name, len(areas))
            + "            const EncounterSlot5 SLOTS_%s[] = {\n%s\n            };\n" % (name, "\n".join(slot_lines))
            + "            const EncounterArea5 AREAS_%s[%d] = {\n%s\n            };\n" % (name, len(areas), "\n".join(area_lines)))


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
        if "Ability" in props:
            fields.append(".ability = %s" % ability(props.pop("Ability")))
        if "Shiny" in props:
            fields.append(".shiny = %s" % shiny(props.pop("Shiny")))
        if "EggLocation" in props:
            fields.append(".eggLocation = %d" % number(props.pop("EggLocation")))
        if "Gender" in props:
            fields.append(".gender = %d" % number(props.pop("Gender")))
        if "IsRoaming" in props:
            fields.append(".isRoaming = %s" % boolean(props.pop("IsRoaming")))
        if props:
            raise SystemExit("%s: unhandled properties %r" % (array_name, props))
        lines.append("                EncounterStatic5({%s})," % ", ".join(fields))
    return "            const EncounterStatic5 %s[] = {\n%s\n            };\n" % (name, "\n".join(lines))


def emit_n(text):
    lines = []
    for args, props in parse_array(text, "Encounter_B2W2_N"):
        fields = [".PID = 0x%08X" % number(args[0]), ".speciesId = %d" % number(props.pop("Species")), ".level = %d" % number(props.pop("Level")),
                  ".location = %d" % number(props.pop("Location")), ".nature = %d" % nature(props.pop("Nature")), ".ability = %s" % ability(props.pop("Ability"))]
        if props:
            raise SystemExit("Encounter_B2W2_N: unhandled properties %r" % props)
        lines.append("                EncounterStatic5N({%s})," % ", ".join(fields))
    return "            const EncounterStatic5N STATIC_N[] = {\n%s\n            };\n" % "\n".join(lines)


def emit_radar(text):
    lines = []
    for args, props in parse_array(text, "Encounter_DreamRadar"):
        if props or len(args) not in (2, 3):
            raise SystemExit("Encounter_DreamRadar: unexpected entry %r %r" % (args, props))
        values = ["%d" % number(args[0]), "%d" % number(args[1])]
        if len(args) == 3:
            values.append(ability(args[2]))
        lines.append("                EncounterStatic5Radar(%s)," % ", ".join(values))
    return "            const EncounterStatic5Radar DREAM_RADAR[] = {\n%s\n            };\n" % "\n".join(lines)


# ---------------------------------------------------------------------------------- Entree Forest
def load_hidden_ability_species():
    """PKHeX PersonalInfo5B2W2.HasHiddenAbility (AbilityH != Ability1) for each species' base row."""
    data = _load(pkhex_path("Resources/byte/personal/personal_b2w2"))
    result = set()
    for species in range(len(data) // PERSONAL_SIZE_B2W2):
        row = data[species * PERSONAL_SIZE_B2W2:(species + 1) * PERSONAL_SIZE_B2W2]
        if row[0x1A] != row[0x18]:
            result.add(species)
    return result


def emit_dream_world(text, array_name, name, hidden_species):
    """PKHeX DreamWorldEntry.GetArray: one template per permitted special move (or one without)."""
    match = re.search(re.escape(array_name) + r"\s*=\s*DreamWorldEntry\.GetArray\(\s*(\w+)\s*,", text)
    if not match:
        raise SystemExit("array %s not found" % array_name)
    version = VERSION[match.group(1)]
    rewritten = text[:match.start()] + array_name + " = " + text[match.end():]
    lines = []
    for args, props in parse_array(rewritten, array_name):
        positional, named = [], {}
        for arg in args:
            arg = arg.strip()
            named_match = re.fullmatch(r"(\w+)\s*:\s*(.+)", arg)
            if named_match:
                named[named_match.group(1)] = named_match.group(2).strip()
            else:
                positional.append(arg)
        if props:
            raise SystemExit("%s: unhandled properties %r" % (array_name, props))
        species, level = number(positional[0]), number(positional[1])
        moves = [number(move) for move in positional[2:5]] + [number(named.pop(key)) for key in ("Move1", "Move2", "Move3") if key in named]
        form = number(named.pop("Form", "0"))
        gender = number(named.pop("Gender", "255"))
        promotion = named.pop("Promotion", "NotPromotion").split(".")[-1]
        if named:
            raise SystemExit("%s: unhandled arguments %r" % (array_name, named))
        if promotion not in PROMOTION:
            raise SystemExit("unknown promotion %r" % promotion)
        permission = "OnlyHidden" if species in hidden_species else "OnlyFirst"
        base = [".version = Enums::GameVersion::%s" % version, ".speciesId = %d" % species, ".level = %d" % level, ".formId = %d" % form, ".gender = %d" % gender,
                ".ability = RNG::AbilityPermission::%s" % permission]
        # A Pokemon can only bring one of its permitted special moves.
        special_moves = []
        for move in moves:
            if move == 0:
                break
            special_moves.append(move)
        for move in special_moves or [0]:
            fields = base + [".moveId = %d" % move, ".promotion = GlobalLinkPromotion::%s" % promotion]
            lines.append("                EncounterStatic5Entree({%s})," % ", ".join(fields))
    return "            const EncounterStatic5Entree %s[] = {\n%s\n            };\n" % (name, "\n".join(lines))


# ----------------------------------------------------------------------------------------- trades
def read_trade_names(resource):
    names = {}
    for language, code in LANGUAGE_CODES.items():
        path = pkhex_path("Resources/legality/gen5/text_%s_%s.txt" % (resource, code))
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


def emit_trades_bw(text, array_name, name, names_by_language, used):
    lines = []
    half = len(names_by_language[1]) >> 1
    for args, props in parse_array(text, array_name):
        index, version, pid = number(args[1]), args[2].strip(), number(args[3])
        used.update((index, index + half))
        fields = [".nicknames = &TRADE_NAMES_BW_%d" % index, ".trainerNames = &TRADE_NAMES_BW_%d" % (index + half),
                  ".version = Enums::GameVersion::%s" % VERSION[version], ".PID = 0x%08X" % pid, ".speciesId = %d" % number(props.pop("Species")),
                  ".level = %d" % number(props.pop("Level")), ".ability = %s" % ability(props.pop("Ability")), ".TID16 = %d" % number(props.pop("TID16")),
                  ".trainerGender = %d" % number(props.pop("OTGender")), ".gender = %d" % number(props.pop("Gender")),
                  ".individualValues = %s" % individual_values(props.pop("IVs")), ".nature = %d" % nature(props.pop("Nature"))]
        if "Form" in props:
            fields.append(".formId = %d" % number(props.pop("Form")))
        if props:
            raise SystemExit("%s: unhandled properties %r" % (array_name, props))
        lines.append("                EncounterTrade5BW({%s})," % ", ".join(fields))
    return "            const EncounterTrade5BW %s[] = {\n%s\n            };\n" % (name, "\n".join(lines))


def parse_string_array(text, array_name):
    match = re.search(re.escape(array_name) + r"\s*=\s*\[(.*?)\];", text, re.S)
    if not match:
        raise SystemExit("string array %s not found" % array_name)
    entries = []
    for part in match.group(1).split(","):
        part = part.strip()
        if part == "string.Empty":
            entries.append("")
        else:
            entries.append(re.fullmatch(r'"(.*)"', part).group(1))
    return entries


def emit_trades_b2w2(text, array_name, name, names_by_language, used):
    lines = []
    half = len(names_by_language[1]) >> 1
    constants = {key: number(value) for key, value in re.findall(r"const ushort (\w+)\s*=\s*(\d+);", text)}
    for args, props in parse_array(text, array_name):
        fields = {}
        if len(args) == 3:
            index, version = number(args[1]), args[2].strip()
            used.update((index, index + half))
            fields["nicknames"] = "&TRADE_NAMES_B2W2_%d" % index
            fields["trainerNames"] = "&TRADE_NAMES_B2W2_%d" % (index + half)
        else:
            fields["trainerNames"] = "&%s" % args[0].strip().upper().replace("TRADEOT_B2W2_", "TRADE_TRAINER_NAMES_B2W2_")
            fields["isFixedNickname"] = "false"
            version = args[1].strip()
        fields["version"] = "Enums::GameVersion::%s" % VERSION[version]
        id32 = props.pop("ID32").strip()
        fields["speciesId"] = "%d" % number(props.pop("Species"))
        fields["level"] = "%d" % number(props.pop("Level"))
        fields["ability"] = ability(props.pop("Ability"))
        fields["trainerGender"] = "%d" % number(props.pop("OTGender"))
        fields["ID32"] = "%d" % (constants[id32] if id32 in constants else number(id32))
        if "Form" in props:
            fields["formId"] = "%d" % number(props.pop("Form"))
        if "Gender" in props:
            fields["gender"] = "%d" % number(props.pop("Gender"))
        if "IVs" in props:
            fields["individualValues"] = individual_values(props.pop("IVs"))
        if "Nature" in props:
            fields["nature"] = "%d" % nature(props.pop("Nature"))
        if "IsFixedNickname" in props:
            fields["isFixedNickname"] = boolean(props.pop("IsFixedNickname"))
        if props:
            raise SystemExit("%s: unhandled properties %r" % (array_name, props))
        ordered = ", ".join(".%s = %s" % (key, fields[key]) for key in B2W2_TRADE_FIELDS if key in fields)
        lines.append("                EncounterTrade5B2W2({%s})," % ordered)
    return "            const EncounterTrade5B2W2 %s[] = {\n%s\n            };\n" % (name, "\n".join(lines))


def emit_fixed_trainer_names(text):
    lines = []
    for array_name in ("TradeOT_B2W2_F", "TradeOT_B2W2_M"):
        entries = parse_string_array(text, array_name)
        if len(entries) != LANGUAGE_COUNT:
            raise SystemExit("%s: expected %d names" % (array_name, LANGUAGE_COUNT))
        name = array_name.upper().replace("TRADEOT_B2W2_", "TRADE_TRAINER_NAMES_B2W2_")
        lines.append("            const LanguageNames4 %s = {%s};" % (name, ", ".join(cpp_text(entry) for entry in entries)))
    return "\n".join(lines) + "\n"


# ------------------------------------------------------------------------------------------ gifts
def decode_gen5_text(data):
    """PKHeX StringConverter5.GetString: stop at 0xFFFF or 0, gender signs normalized."""
    out = []
    for offset in range(0, len(data), 2):
        raw = struct.unpack_from("<H", data, offset)[0]
        if raw in (0xFFFF, 0):
            break
        out.append({0x246D: 0x2642, 0x246E: 0x2640}.get(raw, raw))
    return "".join(chr(code) for code in out)


def emit_gifts():
    """PKHeX PGF.GetArray: 0xCC-byte cards, then one receivability byte per card."""
    data = _load(pkhex_path("Resources/legality/mgdb/pgf.pkl"))
    count = len(data) // (PGF_SIZE + 1)
    if count * (PGF_SIZE + 1) != len(data):
        raise SystemExit("pgf.pkl is not a whole number of gifts")
    tail = count * PGF_SIZE
    lines = []
    for card_index in range(count):
        card = data[card_index * PGF_SIZE:(card_index + 1) * PGF_SIZE]
        species = struct.unpack_from("<H", card, 0x1A)[0]
        if species == 0:
            continue
        receivability = data[tail + card_index]
        tid, sid = struct.unpack_from("<HH", card, 0x00)
        moves = struct.unpack_from("<4H", card, 0x12)
        fields = [".TID16 = %d" % tid, ".SID16 = %d" % sid, ".originGame = %d" % card[0x04], ".PID = 0x%08X" % struct.unpack_from("<I", card, 0x08)[0],
                  ".ball = %d" % card[0x0E], ".heldItem = %d" % struct.unpack_from("<H", card, 0x10)[0],
                  ".moves = Moveset{{%s}}" % ", ".join(str(move) for move in moves), ".speciesId = %d" % species, ".formId = %d" % card[0x1C],
                  ".language = %d" % card[0x1D], ".nickname = %s" % cpp_text(decode_gen5_text(card[0x1E:0x1E + 22])), ".nature = %d" % card[0x34],
                  ".gender = %d" % card[0x35], ".abilityType = %d" % card[0x36], ".PIDType = %d" % card[0x37],
                  ".eggLocation = %d" % struct.unpack_from("<H", card, 0x38)[0], ".location = %d" % struct.unpack_from("<H", card, 0x3A)[0],
                  ".metLevel = %d" % card[0x3C], ".contestStats = {%s}" % ", ".join(str(value) for value in card[0x3D:0x43]),
                  ".individualValues = {%s}" % ", ".join(str(value) for value in card[0x43:0x49]),
                  ".trainerName = %s" % cpp_text(decode_gen5_text(card[0x4A:0x4A + 16])), ".trainerGender = %d" % card[0x5A], ".level = %d" % card[0x5B],
                  ".isEgg = %s" % ("true" if card[0x5C] == 1 else "false"), ".restrictVersion = %d" % (receivability & 0x0F),
                  ".restrictLanguage = %d" % ((receivability >> 4) & 0x0F)]
        ribbons = ribbon_set_text(ribbons_from_bit_bytes(card, CARD_RIBBON_OFFSET_PGF, CARD_RIBBONS_PGF))
        if ribbons:
            fields.append(".ribbons = %s" % ribbons)
        lines.append("                MysteryGiftPGF({%s})," % ", ".join(fields))
    return "            const MysteryGiftPGF MYSTERY_GIFTS5[] = {\n%s\n            };\n" % "\n".join(lines)


def main():
    bw = strip_comments(read_cs(BW_SOURCE))
    b2w2 = strip_comments(read_cs(B2W2_SOURCE))
    dream_radar = strip_comments(read_cs(DR_SOURCE))
    hidden_species = load_hidden_ability_species()

    blocks, accessors = [], []

    def accessor(function, element, array):
        accessors.append((function, element, array))

    for name, file_name, version in (("B", "encounter_b.pkl", "B"), ("W", "encounter_w.pkl", "W"), ("B2", "encounter_b2.pkl", "B2"), ("W2", "encounter_w2.pkl", "W2")):
        blocks.append(emit_wild(name, file_name, version))
        accessor("getAreas5" + version, "EncounterArea5", "AREAS_" + name)

    for text, array_name, name, function in ((bw, "Encounter_BW", "STATIC_BW", "getStatic5BW"), (bw, "StaticB", "STATIC_B", "getStatic5B"),
                                             (bw, "StaticW", "STATIC_W", "getStatic5W"), (b2w2, "Encounter_B2W2_Regular", "STATIC_B2W2", "getStatic5B2W2"),
                                             (b2w2, "StaticB2", "STATIC_B2", "getStatic5B2"), (b2w2, "StaticW2", "STATIC_W2", "getStatic5W2")):
        blocks.append(emit_statics(text, array_name, name))
        accessor(function, "EncounterStatic5", name)

    blocks.append(emit_n(b2w2))
    accessor("getStatic5N", "EncounterStatic5N", "STATIC_N")
    blocks.append(emit_radar(dream_radar))
    accessor("getDreamRadar5", "EncounterStatic5Radar", "DREAM_RADAR")

    for text, array_name, name, function in ((bw, "DreamWorld_BW", "DREAM_WORLD_BW", "getDreamWorld5BW"), (b2w2, "DreamWorld_B2W2", "DREAM_WORLD_B2W2", "getDreamWorld5B2W2"),
                                             (dream_radar, "DreamWorld_Common", "DREAM_WORLD_COMMON", "getDreamWorld5Common")):
        blocks.append(emit_dream_world(text, array_name, name, hidden_species))
        accessor(function, "EncounterStatic5Entree", name)

    names_bw, used_bw, trade_blocks_bw = read_trade_names("tradebw"), set(), []
    for array_name, name, function in (("TradeGift_BW", "TRADE_GIFT_BW", "getTradeGift5BW"), ("TradeGift_B", "TRADE_GIFT_B", "getTradeGift5B"), ("TradeGift_W", "TRADE_GIFT_W", "getTradeGift5W")):
        trade_blocks_bw.append(emit_trades_bw(bw, array_name, name, names_bw, used_bw))
        accessor(function, "EncounterTrade5BW", name)
    blocks.append(emit_name_arrays("TRADE_NAMES_BW", names_bw, sorted(used_bw)) + "".join(trade_blocks_bw))

    names_b2w2, used_b2w2, trade_blocks_b2w2 = read_trade_names("tradeb2w2"), set(), []
    for array_name, name, function in (("TradeGift_B2W2", "TRADE_GIFT_B2W2", "getTradeGift5B2W2"), ("TradeGift_W2", "TRADE_GIFT_W2", "getTradeGift5W2"), ("TradeGift_B2", "TRADE_GIFT_B2", "getTradeGift5B2")):
        trade_blocks_b2w2.append(emit_trades_b2w2(b2w2, array_name, name, names_b2w2, used_b2w2))
        accessor(function, "EncounterTrade5B2W2", name)
    blocks.append(emit_name_arrays("TRADE_NAMES_B2W2", names_b2w2, sorted(used_b2w2)) + emit_fixed_trainer_names(b2w2) + "".join(trade_blocks_b2w2))

    blocks.append(emit_gifts())
    accessor("getMysteryGifts5", "MysteryGiftPGF", "MYSTERY_GIFTS5")

    declarations = "\n".join("        std::span<const %s> %s() noexcept;" % (element, function) for function, element, _ in accessors)
    definitions = "\n\n".join("        std::span<const %s> %s() noexcept\n        {\n            return %s;\n        }" % (element, function, array) for function, element, array in accessors)

    header = """/**
 * Auto-generated by tools/gen_encounters5.py from PKHeX's Generation 5 encounter data. DO NOT EDIT BY HAND --
 * rerun the generator instead.
 *
 * One accessor per PKHeX array (Encounters5BW, Encounters5B2W2, Encounters5DR, EncounterEvent.MGDB_G5),
 * unmerged and in PKHeX's order.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERDATA5_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERDATA5_H

#include <span>

#include "Legality/Encounters/EncounterArea5.h"
#include "Legality/Encounters/EncounterStatic5.h"
#include "Legality/Encounters/EncounterStatic5Entree.h"
#include "Legality/Encounters/EncounterStatic5N.h"
#include "Legality/Encounters/EncounterStatic5Radar.h"
#include "Legality/Encounters/EncounterTrade5B2W2.h"
#include "Legality/Encounters/EncounterTrade5BW.h"
#include "Legality/Encounters/MysteryGiftPGF.h"

namespace Legality
{
    namespace Encounters
    {
%s
    }
}

#endif
""" % declarations
    source = """// Auto-generated by tools/gen_encounters5.py. DO NOT EDIT BY HAND.
#include "Legality/Encounters/EncounterData5.h"

#include "Legality/Encounters/EncounterSlot5.h"

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
