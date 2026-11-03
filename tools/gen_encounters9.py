#!/usr/bin/env python3
"""Generate include/Legality/Encounters/EncounterData9.h + src/Legality/Encounters/EncounterData9.cpp:
PKHeX's Scarlet/Violet encounter templates, unmerged and in PKHeX's own order, as the encounter engine's
template classes.

SOURCES (PKHeX, via tools/pkhex_source.py; PKHEX_LOCAL points it at a checkout)
    Resources/legality/wild/Gen9/encounter_wild_paldea.pkl      Encounters9.Slots (EncounterArea9.GetAreas)
    Resources/legality/wild/Gen9/encounter_gem_*.pkl            Encounters9.TeraBase / TeraDLC1 / TeraDLC2
    Resources/legality/wild/Gen9/encounter_dist_paldea.pkl      Encounters9.Dist
    Resources/legality/wild/Gen9/encounter_might_paldea.pkl     Encounters9.Might
    Resources/legality/wild/Gen9/encounter_fixed_paldea.pkl     Encounters9.Fixed
    Resources/legality/wild/Gen9/encounter_outbreak_paldea.pkl  Encounters9.Outbreak
    Legality/Encounters/Data/Gen9/Encounters9.cs                statics and trades
    Resources/legality/gen9/text_tradesv_<language>.txt         trade nicknames and OT names
    Resources/legality/mgdb/wc9.pkl                             EncounterEvent.MGDB_G9 (Wonder Cards)

Only the Wonder Cards that give a Pokemon are emitted: the others (items, BP, clothing) can never match an
evolution chain.

Regenerate:  python tools/gen_encounters9.py
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_encounters import WILD, _load, split_blobs_uint32, read_cs, strip_comments, parse_array, load_enum, ribbon_set_text, ribbons_from_index_bytes  # noqa: E402
from gen_encounters8 import cpp_text, number, moveset_text, moveset, individual_values, individual_values_text, boolean, last_token, designated, card_text  # noqa: E402
from pkhex_source import pkhex_path  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_H = os.path.join(ROOT, "include", "Legality", "Encounters", "EncounterData9.h")
OUT_CPP = os.path.join(ROOT, "src", "Legality", "Encounters", "EncounterData9.cpp")

ENCOUNTERS_SOURCE = "Legality/Encounters/Data/Gen9/Encounters9.cs"

NATURE = load_enum("Game/Enums/Nature.cs", "Nature")
VERSION = {"SV": "SV", "SL": "SL", "VL": "VL"}
ABILITY = {"OnlyFirst": "OnlyFirst", "OnlySecond": "OnlySecond", "OnlyHidden": "OnlyHidden", "Any12": "Any12", "Any12H": "Any12H"}
# PKHeX EncounterTera9/Dist9/Might9.GetAbility: the raid blobs' ability byte.
RAID_ABILITY = {0: "Any12", 1: "Any12H", 2: "OnlyFirst", 3: "OnlySecond", 4: "OnlyHidden"}
# PKHeX AbilityPermission stored as a signed byte (EncounterFixed9).
ABILITY_VALUE = {0xFF: "Any12H", 0: "Any12", 1: "OnlyFirst", 2: "OnlySecond", 4: "OnlyHidden"}
RAID_SHINY = {0: "Random", 1: "Never", 2: "Always"}
SHINY = {"Random": "Random", "Never": "Never", "Always": "Always"}
GEM_TYPE = load_enum("Legality/Encounters/Templates/Gen9/IGemType.cs", "GemType")
GEM_NAME = {value: name for name, value in GEM_TYPE.items()}
SIZE_TYPE = load_enum("Legality/Encounters/Templates/Gen9/SizeType9.cs", "SizeType9")
SIZE_NAME = {value: name for name, value in SIZE_TYPE.items()}
# PKHeX GetLanguageStrings(tradeSV, 10): language id -> code; 0 and 6 have none.
LANGUAGE_CODES = {1: "ja", 2: "en", 3: "fr", 4: "it", 5: "de", 7: "es", 8: "ko", 9: "zh-Hans", 10: "zh-Hant"}
LANGUAGE_COUNT = 11
TERA_SIZE = 0x18
RAID_STAGE_SIZE = 0x14 + 2 * 2 * 2 * 4 + 10
FIXED_SIZE = 0x14
OUTBREAK_SIZE = 0x0C + 16
WONDER_CARD_SIZE = 0x2C8
GIFT_TYPE_POKEMON = 1


def read_gen9_internal_to_national():
    """PKHeX SpeciesConverter.GetNational9: a Wonder Card stores the game's internal species id."""
    text = read_cs("PKM/Util/Conversion/SpeciesConverter.cs")
    match = re.search(r"Table9InternalToNational\s*=>\s*\[(.*?)\];", text, re.S)
    if not match:
        raise SystemExit("SpeciesConverter.cs: Table9InternalToNational not found")
    return [int(value) for value in re.findall(r"-?\d+", strip_comments(match.group(1)))]


GEN9_INTERNAL_TO_NATIONAL = read_gen9_internal_to_national()
GEN9_FIRST_UNALIGNED_INTERNAL = 917


def gen9_national(raw):
    shift = raw - GEN9_FIRST_UNALIGNED_INTERNAL
    if shift < 0 or shift >= len(GEN9_INTERNAL_TO_NATIONAL):
        return raw
    return raw + GEN9_INTERNAL_TO_NATIONAL[shift]


def gem(value):
    if value not in GEM_NAME:
        raise SystemExit("unknown GemType %d" % value)
    return "RNG::GemType::%s" % GEM_NAME[value]


def size_type(value):
    if value not in SIZE_NAME:
        raise SystemExit("unknown SizeType9 %d" % value)
    return "RNG::SizeType9::%s" % SIZE_NAME[value]


def raid_ability(value):
    if value not in RAID_ABILITY:
        raise SystemExit("unknown raid ability byte %d" % value)
    return "RNG::AbilityPermission::%s" % RAID_ABILITY[value]


def raid_shiny(value):
    if value not in RAID_SHINY:
        raise SystemExit("unknown raid shiny byte %d" % value)
    return "RNG::Shiny::%s" % RAID_SHINY[value]


def raid_individual_values(record, offset):
    """PKHeX `new IndividualValueSet((sbyte)..x6, (IndividualValueSetType)type)`: HP, Atk, Def, Spe, SpA, SpD."""
    values = struct.unpack_from("<6b", record, offset)
    specified = record[offset + 6] != 0
    return "RNG::IndividualValueSet{%s, %s}" % (", ".join(str(value) for value in values), "true" if specified else "false")


# ------------------------------------------------------------------------------------------- wild
def emit_wild(name):
    """PKHeX EncounterArea9: location, (unused), cross-from location, (unused), then 8-byte slots."""
    areas = split_blobs_uint32(_load(WILD("Gen9", "encounter_wild_paldea.pkl")))
    slot_lines, area_lines = [], []
    for area_index, area in enumerate(areas):
        location, cross_from = area[0], area[2]
        first_slot = len(slot_lines)
        body = area[4:]
        for offset in range(0, len(body) - 7, 8):
            species, form, gender, level_min, level_max, time, area_weather = struct.unpack_from("<HBBBBBB", body, offset)
            slot_lines.append("                EncounterSlot9(AREAS_%s[%d], %d, %d, %d, %d, %d, 0x%02X, 0x%02X)," % (name, area_index, species, form, level_min, level_max, gender, time, area_weather))
        area_lines.append("                EncounterArea9(Enums::GameVersion::SV, %d, %d, &SLOTS_%s[%d], %d)," % (location, cross_from, name, first_slot, len(slot_lines) - first_slot))
    return ("            extern const EncounterArea9 AREAS_%s[%d];\n" % (name, len(areas))
            + "            const EncounterSlot9 SLOTS_%s[] = {\n%s\n            };\n" % (name, "\n".join(slot_lines))
            + "            const EncounterArea9 AREAS_%s[%d] = {\n%s\n            };\n" % (name, len(areas), "\n".join(area_lines)))


# ---------------------------------------------------------------------------------------- statics
STATIC_ORDER = ["version", "fixedBall", "location", "eggLocation", "speciesId", "level", "formId", "gender", "ability", "flawlessIVCount", "shiny", "moves",
                "individualValues", "nature", "teraType", "scalarValue", "isTitan", "isFatefulEncounter"]


def gem_from_source(expr):
    return "RNG::GemType::%s" % last_token(expr)


def emit_statics(text, array_name, name):
    lines = []
    for args, props in parse_array(text, array_name):
        fields = {"version": "Enums::GameVersion::%s" % VERSION[last_token(args[0])]}
        fields["location"] = str(number(props.pop("Location")))
        fields["speciesId"] = str(number(props.pop("Species")))
        fields["level"] = str(number(props.pop("Level")))
        if "FixedBall" in props:
            fields["fixedBall"] = "Enums::Ball::%s" % last_token(props.pop("FixedBall"))
        if "EggLocation" in props:
            fields["eggLocation"] = str(number(props.pop("EggLocation")))
        if "Form" in props:
            fields["formId"] = str(number(props.pop("Form")))
        if "Gender" in props:
            fields["gender"] = str(number(props.pop("Gender")))
        if "Ability" in props:
            fields["ability"] = "RNG::AbilityPermission::%s" % ABILITY[last_token(props.pop("Ability"))]
        if "FlawlessIVCount" in props:
            fields["flawlessIVCount"] = str(number(props.pop("FlawlessIVCount")))
        if "Shiny" in props:
            fields["shiny"] = "RNG::Shiny::%s" % SHINY[last_token(props.pop("Shiny"))]
        if "Moves" in props:
            fields["moves"] = moveset(props.pop("Moves"))
        if "IVs" in props:
            fields["individualValues"] = individual_values(props.pop("IVs"))
        if "Nature" in props:
            fields["nature"] = str(NATURE[last_token(props.pop("Nature"))])
        if "TeraType" in props:
            fields["teraType"] = gem_from_source(props.pop("TeraType"))
        if "Size" in props:
            fields["scalarValue"] = str(number(props.pop("Size")))
        if "IsTitan" in props:
            fields["isTitan"] = boolean(props.pop("IsTitan"))
        if "FatefulEncounter" in props:
            fields["isFatefulEncounter"] = boolean(props.pop("FatefulEncounter"))
        if props:
            raise SystemExit("%s: unhandled properties %r" % (array_name, props))
        lines.append("                EncounterStatic9({%s})," % designated(fields, STATIC_ORDER, array_name))
    return "            const EncounterStatic9 %s[] = {\n%s\n            };\n" % (name, "\n".join(lines))


# ----------------------------------------------------------------------------------------- trades
TRADE_ORDER = ["nicknames", "trainerNames", "version", "speciesId", "formId", "level", "shiny", "fixedBall", "nature", "ID32", "ability", "gender",
               "trainerGender", "individualValues", "moves", "evolveOnTrade", "weight", "scale", "teraType", "hasPartnerRibbon"]


def read_trade_names():
    names = {}
    for language, code in LANGUAGE_CODES.items():
        path = pkhex_path("Resources/legality/gen9/text_tradesv_%s.txt" % code)
        with open(path, encoding="utf-8-sig") as handle:
            names[language] = handle.read().replace("\r", "").split("\n")
    return names


def emit_trades(text, array_name, name, names_by_language, used_indices):
    lines = []
    half = len(names_by_language[1]) >> 1
    for args, props in parse_array(text, array_name):
        if args[0].strip() != "TradeNames":
            raise SystemExit("%s: unknown trade constructor %r" % (array_name, args[0]))
        index, version, species, level = number(args[1]), args[2], args[3], args[4]
        fields = {}
        # PKHeX EncounterTrade9: indices 2-31 are the Partner-Ribbon trades, which keep the species name.
        partner = 2 <= index <= 31
        if not partner:
            fields["nicknames"] = "&TRADE_NAMES_SV_%d" % index
            used_indices.add(index)
        fields["trainerNames"] = "&TRADE_NAMES_SV_%d" % (index + half)
        used_indices.add(index + half)
        fields["hasPartnerRibbon"] = "true" if partner else "false"
        fields["version"] = "Enums::GameVersion::%s" % VERSION[last_token(version)]
        fields["speciesId"] = str(number(species))
        fields["level"] = str(number(level))
        fields["fixedBall"] = "Enums::Ball::%s" % last_token(props.pop("FixedBall"))
        fields["ID32"] = str(number(props.pop("ID32")))
        fields["ability"] = "RNG::AbilityPermission::%s" % ABILITY[last_token(props.pop("Ability"))]
        fields["trainerGender"] = str(number(props.pop("OTGender")))
        if "Gender" in props:
            fields["gender"] = str(number(props.pop("Gender")))
        fields["nature"] = str(NATURE[last_token(props.pop("Nature"))])
        fields["teraType"] = gem_from_source(props.pop("TeraType"))
        if "Weight" in props:
            fields["weight"] = "RNG::SizeType9::%s" % last_token(props.pop("Weight"))
        if "Scale" in props:
            fields["scale"] = "RNG::SizeType9::%s" % last_token(props.pop("Scale"))
        fields["individualValues"] = individual_values(props.pop("IVs"))
        if "Moves" in props:
            fields["moves"] = moveset(props.pop("Moves"))
        if "EvolveOnTrade" in props:
            fields["evolveOnTrade"] = boolean(props.pop("EvolveOnTrade"))
        if "Form" in props:
            fields["formId"] = str(number(props.pop("Form")))
        if "Shiny" in props:
            fields["shiny"] = "RNG::Shiny::%s" % SHINY[last_token(props.pop("Shiny"))]
        if props:
            raise SystemExit("%s: unhandled properties %r" % (array_name, props))
        lines.append("                EncounterTrade9({%s})," % designated(fields, TRADE_ORDER, array_name))
    return "            const EncounterTrade9 %s[] = {\n%s\n            };\n" % (name, "\n".join(lines))


def emit_language_names(name, values):
    entries = [cpp_text(value) for value in values]
    return "            constexpr LanguageNamesSwitch %s = {%s};" % (name, ", ".join(entries))


# ------------------------------------------------------------------------------------------ raids
TERA_ORDER = ["speciesId", "formId", "gender", "ability", "flawlessIVCount", "shiny", "level", "moves", "teraType", "raidIndex", "stars", "randomRate",
              "randomRateMinimumScarlet", "randomRateMinimumViolet", "map"]


def emit_tera(name, file_name, map_name):
    """PKHeX EncounterTera9.ReadEncounter (0x18 bytes)."""
    data = _load(WILD("Gen9", file_name))
    lines = []
    for offset in range(0, len(data), TERA_SIZE):
        record = data[offset:offset + TERA_SIZE]
        species, form, gender, ability, flawless, shiny, level = struct.unpack_from("<HBBBBBB", record, 0)
        moves = struct.unpack_from("<4H", record, 0x08)
        tera, index, stars, rate = struct.unpack_from("<BBBB", record, 0x10)
        minimum_scarlet, minimum_violet = struct.unpack_from("<hh", record, 0x14)
        fields = {"speciesId": str(species), "formId": str(form), "gender": str((gender - 1) & 0xFF), "ability": raid_ability(ability),
                  "flawlessIVCount": str(flawless), "shiny": raid_shiny(shiny), "level": str(level), "moves": moveset_text(moves), "teraType": gem(tera),
                  "raidIndex": str(index), "stars": str(stars), "randomRate": str(rate), "randomRateMinimumScarlet": str(minimum_scarlet),
                  "randomRateMinimumViolet": str(minimum_violet), "map": "TeraRaidMapParent::%s" % map_name}
        lines.append("                EncounterTera9({%s})," % designated(fields, TERA_ORDER, name))
    return "            const EncounterTera9 %s[] = {\n%s\n            };\n" % (name, "\n".join(lines))


DIST_ORDER = ["speciesId", "formId", "gender", "ability", "flawlessIVCount", "shiny", "nature", "level", "moves", "individualValues", "teraType", "raidIndex",
              "stars", "randomRate", "scaleType", "scale", "rates"]
MIGHT_ORDER = ["moves", "individualValues", "speciesId", "formId", "level", "gender", "flawlessIVCount", "ability", "shiny", "nature", "teraType", "raidIndex",
               "stars", "randomRate", "scaleType", "scale", "rates"]


def emit_stage_raids(name, file_name, class_name, order):
    """PKHeX EncounterDist9/EncounterMight9.ReadEncounter (0x3E bytes): the Tera9 head, four stages of weights, then IVs and scale."""
    data = _load(WILD("Gen9", file_name))
    lines = []
    for offset in range(0, len(data), RAID_STAGE_SIZE):
        record = data[offset:offset + RAID_STAGE_SIZE]
        species, form, gender, ability, flawless, shiny, level = struct.unpack_from("<HBBBBBB", record, 0)
        moves = struct.unpack_from("<4H", record, 0x08)
        tera, index, stars, rate = struct.unpack_from("<BBBB", record, 0x10)
        weights = struct.unpack_from("<16H", record, 0x14)
        minimum_scarlet = [weights[stage * 4] for stage in range(4)]
        minimum_violet = [weights[stage * 4 + 1] for stage in range(4)]
        total_scarlet = [weights[stage * 4 + 2] for stage in range(4)]
        total_violet = [weights[stage * 4 + 3] for stage in range(4)]
        rates = "RaidStageRates9{{%s}, {%s}, {%s}, {%s}}" % tuple(", ".join(str(value) for value in values) for values in (minimum_scarlet, minimum_violet, total_scarlet, total_violet))
        fields = {"speciesId": str(species), "formId": str(form), "gender": str((gender - 1) & 0xFF), "ability": raid_ability(ability),
                  "flawlessIVCount": str(flawless), "shiny": raid_shiny(shiny), "nature": str(record[0x34]), "level": str(level), "moves": moveset_text(moves),
                  "individualValues": raid_individual_values(record, 0x35), "teraType": gem(tera), "raidIndex": str(index), "stars": str(stars),
                  "randomRate": str(rate), "scaleType": size_type(record[0x3C]), "scale": str(record[0x3D]), "rates": rates}
        lines.append("                %s({%s})," % (class_name, designated(fields, order, name)))
    return "            const %s %s[] = {\n%s\n            };\n" % (class_name, name, "\n".join(lines))


FIXED_ORDER = ["speciesId", "formId", "level", "flawlessIVCount", "teraType", "gender", "ability", "moves", "locations"]


def emit_fixed(name):
    """PKHeX EncounterFixed9.ReadEncounter (0x14 bytes)."""
    data = _load(WILD("Gen9", "encounter_fixed_paldea.pkl"))
    lines = []
    for offset in range(0, len(data), FIXED_SIZE):
        record = data[offset:offset + FIXED_SIZE]
        species, form, level, flawless, tera, gender, ability = struct.unpack_from("<HBBBBBB", record, 0)
        if ability not in ABILITY_VALUE:
            raise SystemExit("unknown EncounterFixed9 ability %d" % ability)
        moves = struct.unpack_from("<4H", record, 0x08)
        locations = record[0x10:0x14]
        fields = {"speciesId": str(species), "formId": str(form), "level": str(level), "flawlessIVCount": str(flawless), "teraType": gem(tera),
                  "gender": str(gender), "ability": "RNG::AbilityPermission::%s" % ABILITY_VALUE[ability], "moves": moveset_text(moves),
                  "locations": "{%s}" % ", ".join(str(value) for value in locations)}
        lines.append("                EncounterFixed9({%s})," % designated(fields, FIXED_ORDER, name))
    return "            const EncounterFixed9 %s[] = {\n%s\n            };\n" % (name, "\n".join(lines))


OUTBREAK_ORDER = ["speciesId", "formId", "gender", "levelMin", "levelMax", "ribbon", "weather", "isForcedScaleRange", "scaleMin", "scaleMax", "isShiny",
                  "metBase", "metFlagsLow", "metFlagsHigh"]


def emit_outbreaks(name):
    """PKHeX EncounterOutbreak9.ReadEncounter (0x1C bytes): MetFlags is the 128 bits from 0x0C shifted past MetBase."""
    data = _load(WILD("Gen9", "encounter_outbreak_paldea.pkl"))
    lines = []
    for offset in range(0, len(data), OUTBREAK_SIZE):
        record = data[offset:offset + OUTBREAK_SIZE]
        species, form, gender, level_min, level_max, ribbon, area_weather = struct.unpack_from("<HBBBBBB", record, 0)
        flags = int.from_bytes(record[0x0C:0x1C], "little") >> 8
        fields = {"speciesId": str(species), "formId": str(form), "gender": str(gender), "levelMin": str(level_min), "levelMax": str(level_max),
                  "ribbon": str(ribbon), "weather": "0x%02X" % area_weather, "isForcedScaleRange": "true" if record[0x08] else "false",
                  "scaleMin": str(record[0x09]), "scaleMax": str(record[0x0A]), "isShiny": "true" if record[0x0B] else "false", "metBase": str(record[0x0C]),
                  "metFlagsLow": "0x%016XULL" % (flags & 0xFFFFFFFFFFFFFFFF), "metFlagsHigh": "0x%016XULL" % (flags >> 64)}
        lines.append("                EncounterOutbreak9({%s})," % designated(fields, OUTBREAK_ORDER, name))
    return "            const EncounterOutbreak9 %s[] = {\n%s\n            };\n" % (name, "\n".join(lines))


# ------------------------------------------------------------------------------------------- WC9
CARD_ORDER = ["cardId", "restrictVersion", "scale", "ID32", "originGame", "encryptionConstant", "PID", "eggLocation", "location", "ball", "moves",
              "relearnMoves", "speciesId", "formId", "gender", "level", "isEgg", "nature", "abilityType", "shinyType", "metLevel", "teraTypeOriginal",
              "heightValue", "weightValue", "individualValues", "trainerGender", "languageWords", "trainerNames", "nicknamedSlots", "ribbons"]

CARD_RIBBON_OFFSET = 0x248  # PKHeX WC9.RibbonBytesOffset
CARD_IV_OFFSET = 0x268  # PKHeX WC9.IV_HP


def emit_wonder_cards(name):
    data = _load(pkhex_path("Resources/legality/mgdb/wc9.pkl"))
    if len(data) % WONDER_CARD_SIZE:
        raise SystemExit("wc9.pkl is not a whole number of Wonder Cards")
    lines = []
    for offset in range(0, len(data), WONDER_CARD_SIZE):
        card = data[offset:offset + WONDER_CARD_SIZE]
        if card[0x11] != GIFT_TYPE_POKEMON:
            continue
        card_id = struct.unpack_from("<H", card, 0x8)[0]
        scale = struct.unpack_from("<H", card, 0xC)[0]
        id32, origin_game, encryption_constant, pid = struct.unpack_from("<IiII", card, 0x18)
        egg_location, location, ball = struct.unpack_from("<HHH", card, 0x220)
        moves = struct.unpack_from("<4H", card, 0x228)
        relearn = struct.unpack_from("<4H", card, 0x230)
        species = gen9_national(struct.unpack_from("<H", card, 0x238)[0])
        form, gender, level, is_egg, nature, ability_type, shiny_type, met_level, tera = struct.unpack_from("<9B", card, 0x23A)
        height_value, weight_value = struct.unpack_from("<hh", card, 0x244)
        trainer_gender = card[0x26E]
        language_words, trainer_names, nicknamed = [], [], 0
        for slot in range(9):
            nickname_offset = 0x28 + slot * 0x1C
            language_words.append(struct.unpack_from("<H", card, nickname_offset + 0x1A)[0])
            if struct.unpack_from("<H", card, nickname_offset)[0] != 0:
                nicknamed |= 1 << slot
            trainer_names.append(cpp_text(card_text(card, 0x124 + slot * 0x1C)))
        fields = {"cardId": str(card_id), "restrictVersion": str(card[0xE]), "scale": str(scale), "ID32": "%du" % id32, "originGame": str(origin_game),
                  "encryptionConstant": "0x%08X" % encryption_constant, "PID": "0x%08X" % pid, "eggLocation": str(egg_location), "location": str(location),
                  "ball": str(ball & 0xFF), "moves": moveset_text(moves), "relearnMoves": moveset_text(relearn), "speciesId": str(species), "formId": str(form),
                  "gender": str(gender), "level": str(level), "isEgg": "true" if is_egg == 1 else "false", "nature": str(nature),
                  "abilityType": str(ability_type), "shinyType": "static_cast<ShinyType8>(%d)" % shiny_type, "metLevel": str(met_level),
                  "teraTypeOriginal": str(tera), "heightValue": str(height_value), "weightValue": str(weight_value), "individualValues": "{%s}" % ", ".join(str(value) for value in card[CARD_IV_OFFSET:CARD_IV_OFFSET + 6]), "trainerGender": str(trainer_gender),
                  "languageWords": "{%s}" % ", ".join(str(word) for word in language_words), "trainerNames": "{%s}" % ", ".join(trainer_names),
                  "nicknamedSlots": "0x%03X" % nicknamed}
        ribbons = ribbon_set_text(ribbons_from_index_bytes(card, CARD_RIBBON_OFFSET))
        if ribbons:
            fields["ribbons"] = ribbons
        lines.append("                MysteryGiftWC9({%s})," % designated(fields, CARD_ORDER, name))
    return "            const MysteryGiftWC9 %s[] = {\n%s\n            };\n" % (name, "\n".join(lines))


def main():
    encounters = strip_comments(read_cs(ENCOUNTERS_SOURCE))
    blocks, accessors = [], []

    def accessor(function, element, array):
        accessors.append((function, element, array))

    blocks.append(emit_wild("SV"))
    accessor("getAreas9SV", "EncounterArea9", "AREAS_SV")

    for array_name, name, function in (("Encounter_SV", "STATIC_SV", "getStatic9SV"), ("StaticSL", "STATIC_SL", "getStatic9SL"), ("StaticVL", "STATIC_VL", "getStatic9VL")):
        blocks.append(emit_statics(encounters, array_name, name))
        accessor(function, "EncounterStatic9", name)

    names = read_trade_names()
    used = set()
    trade_block = emit_trades(encounters, "TradeGift_SV", "TRADE_SV", names, used)
    name_lines = []
    for index in sorted(used):
        values = []
        for language in range(LANGUAGE_COUNT):
            language_names = names.get(language, [])
            values.append(language_names[index] if index < len(language_names) else "")
        name_lines.append(emit_language_names("TRADE_NAMES_SV_%d" % index, values))
    blocks.append("\n".join(name_lines) + "\n")
    blocks.append(trade_block)
    accessor("getTrade9SV", "EncounterTrade9", "TRADE_SV")

    for name, function, file_name, map_name in (("TERA_BASE", "getTera9Base", "encounter_gem_paldea.pkl", "Paldea"),
                                                ("TERA_DLC1", "getTera9Kitakami", "encounter_gem_kitakami.pkl", "Kitakami"),
                                                ("TERA_DLC2", "getTera9Blueberry", "encounter_gem_blueberry.pkl", "Blueberry")):
        blocks.append(emit_tera(name, file_name, map_name))
        accessor(function, "EncounterTera9", name)
    blocks.append(emit_stage_raids("DISTRIBUTION_SV", "encounter_dist_paldea.pkl", "EncounterDist9", DIST_ORDER))
    accessor("getDist9", "EncounterDist9", "DISTRIBUTION_SV")
    blocks.append(emit_stage_raids("MIGHTIEST_SV", "encounter_might_paldea.pkl", "EncounterMight9", MIGHT_ORDER))
    accessor("getMight9", "EncounterMight9", "MIGHTIEST_SV")
    blocks.append(emit_fixed("FIXED_SV"))
    accessor("getFixed9", "EncounterFixed9", "FIXED_SV")
    blocks.append(emit_outbreaks("OUTBREAK_SV"))
    accessor("getOutbreak9", "EncounterOutbreak9", "OUTBREAK_SV")

    blocks.append(emit_wonder_cards("WONDER_CARDS_9"))
    accessor("getGifts9WC9", "MysteryGiftWC9", "WONDER_CARDS_9")

    declarations = "\n".join("        std::span<const %s> %s() noexcept;" % (element, function) for function, element, _ in accessors)
    definitions = "\n\n".join("        std::span<const %s> %s() noexcept\n        {\n            return %s;\n        }" % (element, function, array) for function, element, array in accessors)

    header = """/**
 * Auto-generated by tools/gen_encounters9.py from PKHeX's Scarlet/Violet encounter data. DO NOT EDIT BY HAND --
 * rerun the generator instead.
 *
 * One accessor per PKHeX array (Encounters9, EncounterEvent.MGDB_G9), unmerged and in PKHeX's order.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERDATA9_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERDATA9_H

#include <span>

#include "Legality/Encounters/EncounterArea9.h"
#include "Legality/Encounters/EncounterDist9.h"
#include "Legality/Encounters/EncounterFixed9.h"
#include "Legality/Encounters/EncounterMight9.h"
#include "Legality/Encounters/EncounterOutbreak9.h"
#include "Legality/Encounters/EncounterStatic9.h"
#include "Legality/Encounters/EncounterTera9.h"
#include "Legality/Encounters/EncounterTrade9.h"
#include "Legality/Encounters/MysteryGiftWC9.h"

namespace Legality
{
    namespace Encounters
    {
%s
    }
}

#endif
""" % declarations
    source = """// Auto-generated by tools/gen_encounters9.py. DO NOT EDIT BY HAND.
#include "Legality/Encounters/EncounterData9.h"

#include "Legality/Encounters/EncounterSlot9.h"

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
