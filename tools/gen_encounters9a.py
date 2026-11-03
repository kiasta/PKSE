#!/usr/bin/env python3
"""Generate include/Legality/Encounters/EncounterData9a.h + src/Legality/Encounters/EncounterData9a.cpp:
PKHeX's Legends: Z-A encounter templates, unmerged and in PKHeX's own order, as the encounter engine's
template classes.

SOURCES (PKHeX, via tools/pkhex_source.py; PKHEX_LOCAL points it at a checkout)
    Resources/legality/wild/Gen9/encounter_za.pkl              Encounters9a.Slots (EncounterArea9a.GetAreas)
    Resources/legality/wild/Gen9/encounter_hyperspace_za.pkl   Encounters9a.Hyperspace
    Legality/Encounters/Data/Gen9/Encounters9a.cs              gifts, statics and trades
    Resources/legality/gen9a/text_tradeza_<language>.txt       trade nicknames and OT names
    Resources/legality/mgdb/wa9.pkl                            EncounterEvent.MGDB_G9A (Wonder Cards)

Only the Wonder Cards that give a Pokemon are emitted: the others can never match an evolution chain.

Regenerate:  python tools/gen_encounters9a.py
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_encounters import WILD, _load, split_blobs_uint16, read_cs, strip_comments, parse_array, load_enum, ribbon_set_text, ribbons_from_index_bytes  # noqa: E402
from gen_encounters8 import cpp_text, number, moveset_text, moveset, individual_values, boolean, last_token, designated, card_text  # noqa: E402
from gen_encounters9 import gen9_national  # noqa: E402
from pkhex_source import pkhex_path  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_H = os.path.join(ROOT, "include", "Legality", "Encounters", "EncounterData9a.h")
OUT_CPP = os.path.join(ROOT, "src", "Legality", "Encounters", "EncounterData9a.cpp")

ENCOUNTERS_SOURCE = "Legality/Encounters/Data/Gen9/Encounters9a.cs"

NATURE = load_enum("Game/Enums/Nature.cs", "Nature")
SHINY_NAMES = ["Random", "Never", "Always", "AlwaysStar", "AlwaysSquare", "FixedValue"]
TRAINER_GIFT = {"Lucario", "Floette", "Stunfisk", "Gimmighoul", "Magearna"}
# PKHeX GetLanguageStrings(tradeZA, 11): languages 0 and 6 have no file; 11 is Latin American Spanish.
LANGUAGE_CODES = {1: "ja", 2: "en", 3: "fr", 4: "it", 5: "de", 7: "es", 8: "ko", 9: "zh-Hans", 10: "zh-Hant", 11: "es-419"}
LANGUAGE_COUNT = 12
WONDER_CARD_SIZE = 0x2C8
WONDER_CARD_NAME_SLOTS = 10
GIFT_TYPE_POKEMON = 1


def shiny(value):
    if value >= len(SHINY_NAMES):
        raise SystemExit("unknown Shiny %d" % value)
    return "RNG::Shiny::%s" % SHINY_NAMES[value]


# ------------------------------------------------------------------------------------------- wild
def emit_wild(name, file_name, slot_type):
    """PKHeX EncounterArea9a: u16 location, two reserved bytes, then 8-byte slots."""
    areas = split_blobs_uint16(_load(WILD("Gen9", file_name)))
    slot_lines, area_lines = [], []
    for area_index, area in enumerate(areas):
        location = struct.unpack_from("<H", area, 0)[0]
        first_slot = len(slot_lines)
        body = area[4:]
        for offset in range(0, len(body) - 7, 8):
            species, form, gender, level_min, level_max, alpha, shiny_value = struct.unpack_from("<HBBBBBB", body, offset)
            slot_lines.append("                EncounterSlot9a(AREAS_%s[%d], %d, %d, %d, %d, %s, %d, %s)," % (name, area_index, species, form, level_min, level_max, "true" if alpha == 1 else "false", gender, shiny(shiny_value)))
        area_lines.append("                EncounterArea9a(%d, SlotType9a::%s, &SLOTS_%s[%d], %d)," % (location, slot_type, name, first_slot, len(slot_lines) - first_slot))
    return ("            extern const EncounterArea9a AREAS_%s[%d];\n" % (name, len(areas))
            + "            const EncounterSlot9a SLOTS_%s[] = {\n%s\n            };\n" % (name, "\n".join(slot_lines))
            + "            const EncounterArea9a AREAS_%s[%d] = {\n%s\n            };\n" % (name, len(areas), "\n".join(area_lines)))


# ------------------------------------------------------------------------------- gifts and statics
FIXED_ORDER = ["speciesId", "formId", "level", "scale", "location", "gender", "nature", "flawlessIVCount", "individualValues", "moves", "shiny", "isAlpha", "trainer"]


def emit_fixed(text, array_name, name, class_name):
    """PKHeX `new(species, form, level, size) { .. }` for EncounterGift9a and EncounterStatic9a."""
    lines = []
    for args, props in parse_array(text, array_name):
        fields = {"speciesId": str(number(args[0])), "formId": str(number(args[1])), "level": str(number(args[2]))}
        if len(args) > 3:
            fields["scale"] = str(number(args[3]))
        fields["location"] = str(number(props.pop("Location")))
        if "Gender" in props:
            fields["gender"] = str(number(props.pop("Gender")))
        if "Nature" in props:
            fields["nature"] = str(NATURE[last_token(props.pop("Nature"))])
        if "FlawlessIVCount" in props:
            fields["flawlessIVCount"] = str(number(props.pop("FlawlessIVCount")))
        if "IVs" in props:
            fields["individualValues"] = individual_values(props.pop("IVs"))
        if "Moves" in props:
            fields["moves"] = moveset(props.pop("Moves"))
        if "Shiny" in props:
            shiny_name = last_token(props.pop("Shiny"))
            if shiny_name not in SHINY_NAMES:
                raise SystemExit("%s: unknown Shiny %r" % (array_name, shiny_name))
            fields["shiny"] = "RNG::Shiny::%s" % shiny_name
        if "IsAlpha" in props:
            fields["isAlpha"] = boolean(props.pop("IsAlpha"))
        if "Trainer" in props:
            trainer = last_token(props.pop("Trainer"))
            if trainer not in TRAINER_GIFT or class_name != "EncounterGift9a":
                raise SystemExit("%s: unknown TrainerGift9a %r" % (array_name, trainer))
            fields["trainer"] = "TrainerGift9a::%s" % trainer
        if props:
            raise SystemExit("%s: unhandled properties %r" % (array_name, props))
        lines.append("                %s({%s})," % (class_name, designated(fields, FIXED_ORDER, array_name)))
    return "            const %s %s[] = {\n%s\n            };\n" % (class_name, name, "\n".join(lines))


# ----------------------------------------------------------------------------------------- trades
TRADE_ORDER = ["nicknames", "trainerNames", "speciesId", "formId", "level", "ID32", "trainerGender", "gender", "nature", "flawlessIVCount", "individualValues", "moves"]


def read_trade_names():
    names = {}
    for language, code in LANGUAGE_CODES.items():
        path = pkhex_path("Resources/legality/gen9a/text_tradeza_%s.txt" % code)
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
        fields = {"nicknames": "&TRADE_NAMES_ZA_%d" % index, "trainerNames": "&TRADE_NAMES_ZA_%d" % (index + half)}
        used_indices.update((index, index + half))
        fields["speciesId"] = str(number(args[2]))
        fields["formId"] = str(number(args[3]))
        fields["level"] = str(number(args[4]))
        fields["ID32"] = str(number(props.pop("ID32")))
        fields["trainerGender"] = str(number(props.pop("OTGender")))
        if "Gender" in props:
            fields["gender"] = str(number(props.pop("Gender")))
        if "Nature" in props:
            fields["nature"] = str(NATURE[last_token(props.pop("Nature"))])
        if "FlawlessIVCount" in props:
            fields["flawlessIVCount"] = str(number(props.pop("FlawlessIVCount")))
        if "IVs" in props:
            fields["individualValues"] = individual_values(props.pop("IVs"))
        if "Moves" in props:
            fields["moves"] = moveset(props.pop("Moves"))
        if props:
            raise SystemExit("%s: unhandled properties %r" % (array_name, props))
        lines.append("                EncounterTrade9a({%s})," % designated(fields, TRADE_ORDER, array_name))
    return "            const EncounterTrade9a %s[] = {\n%s\n            };\n" % (name, "\n".join(lines))


# ---------------------------------------------------------------------------------- Wonder Cards
CARD_ORDER = ["cardId", "ID32", "originGame", "encryptionConstant", "PID", "eggLocation", "location", "ball", "moves", "relearnMoves", "speciesId",
              "formId", "gender", "level", "isEgg", "nature", "abilityType", "shinyType", "metLevel", "individualValues", "trainerGender", "scale",
              "isAlpha", "languageWords", "trainerNames", "nicknamedSlots", "ribbons"]

CARD_RIBBON_OFFSET = 0x27A  # PKHeX WA9.RibbonBytesOffset


def emit_wonder_cards(name):
    """PKHeX WA9: the offsets its properties read."""
    data = _load(pkhex_path("Resources/legality/mgdb/wa9.pkl"))
    if len(data) % WONDER_CARD_SIZE:
        raise SystemExit("wa9.pkl is not a whole number of Wonder Cards")
    lines = []
    for offset in range(0, len(data), WONDER_CARD_SIZE):
        card = data[offset:offset + WONDER_CARD_SIZE]
        if card[0x11] != GIFT_TYPE_POKEMON:
            continue
        card_id = struct.unpack_from("<H", card, 0x8)[0]
        id32, origin_game, encryption_constant, pid = struct.unpack_from("<IiII", card, 0x18)
        egg_location, location, ball = struct.unpack_from("<HHH", card, 0x258)
        moves = struct.unpack_from("<4H", card, 0x260)
        relearn = struct.unpack_from("<4H", card, 0x268)
        species = gen9_national(struct.unpack_from("<H", card, 0x270)[0])
        form, gender, level, is_egg, nature, ability_type, shiny_type, met_level = struct.unpack_from("<8B", card, 0x272)
        individual_values_stored = struct.unpack_from("<6B", card, 0x29A)
        trainer_gender = card[0x2A0]
        scale = struct.unpack_from("<H", card, 0x2AC)[0]
        is_alpha = card[0x2AE]
        language_words, trainer_names, nicknamed = [], [], 0
        for slot in range(WONDER_CARD_NAME_SLOTS):
            nickname_offset = 0x28 + slot * 0x1C
            trainer_offset = 0x140 + slot * 0x1C
            # PKHeX GetLanguageOffset reads the language word from the OT name block.
            language_words.append(struct.unpack_from("<H", card, trainer_offset + 0x1A)[0])
            if struct.unpack_from("<H", card, nickname_offset)[0] != 0:
                nicknamed |= 1 << slot
            trainer_names.append(cpp_text(card_text(card, trainer_offset)))
        fields = {"cardId": str(card_id), "ID32": "%du" % id32, "originGame": str(origin_game), "encryptionConstant": "0x%08X" % encryption_constant,
                  "PID": "0x%08X" % pid, "eggLocation": str(egg_location), "location": str(location), "ball": str(ball & 0xFF),
                  "moves": moveset_text(moves), "relearnMoves": moveset_text(relearn), "speciesId": str(species), "formId": str(form),
                  "gender": str(gender), "level": str(level), "isEgg": "true" if is_egg == 1 else "false", "nature": str(nature),
                  "abilityType": str(ability_type), "shinyType": "static_cast<ShinyType8>(%d)" % shiny_type, "metLevel": str(met_level),
                  "individualValues": "{%s}" % ", ".join(str(value) for value in individual_values_stored), "trainerGender": str(trainer_gender),
                  "scale": str(scale), "isAlpha": "true" if is_alpha == 1 else "false",
                  "languageWords": "{%s}" % ", ".join(str(word) for word in language_words), "trainerNames": "{%s}" % ", ".join(trainer_names),
                  "nicknamedSlots": "0x%03X" % nicknamed}
        ribbons = ribbon_set_text(ribbons_from_index_bytes(card, CARD_RIBBON_OFFSET))
        if ribbons:
            fields["ribbons"] = ribbons
        lines.append("                MysteryGiftWA9({%s})," % designated(fields, CARD_ORDER, name))
    return "            const MysteryGiftWA9 %s[] = {\n%s\n            };\n" % (name, "\n".join(lines))


def main():
    encounters = strip_comments(read_cs(ENCOUNTERS_SOURCE))
    blocks, accessors = [], []

    def accessor(function, element, array):
        accessors.append((function, element, array))

    blocks.append(emit_wild("ZA", "encounter_za.pkl", "Standard"))
    accessor("getAreas9aStandard", "EncounterArea9a", "AREAS_ZA")
    blocks.append(emit_wild("HYPERSPACE_ZA", "encounter_hyperspace_za.pkl", "Hyperspace"))
    accessor("getAreas9aHyperspace", "EncounterArea9a", "AREAS_HYPERSPACE_ZA")

    blocks.append(emit_fixed(encounters, "Gifts", "GIFT_ZA", "EncounterGift9a"))
    accessor("getGifts9a", "EncounterGift9a", "GIFT_ZA")
    blocks.append(emit_fixed(encounters, "Static", "STATIC_ZA", "EncounterStatic9a"))
    accessor("getStatic9a", "EncounterStatic9a", "STATIC_ZA")

    names = read_trade_names()
    used = set()
    trade_block = emit_trades(encounters, "Trades", "TRADE_ZA", names, used)
    name_lines = []
    for index in sorted(used):
        values = []
        for language in range(LANGUAGE_COUNT):
            language_names = names.get(language, [])
            values.append(cpp_text(language_names[index] if index < len(language_names) else ""))
        name_lines.append("            constexpr LanguageNames9a TRADE_NAMES_ZA_%d = {%s};" % (index, ", ".join(values)))
    blocks.append("\n".join(name_lines) + "\n")
    blocks.append(trade_block)
    accessor("getTrade9a", "EncounterTrade9a", "TRADE_ZA")

    blocks.append(emit_wonder_cards("WONDER_CARDS_9A"))
    accessor("getGifts9aWA9", "MysteryGiftWA9", "WONDER_CARDS_9A")

    declarations = "\n".join("        std::span<const %s> %s() noexcept;" % (element, function) for function, element, _ in accessors)
    definitions = "\n\n".join("        std::span<const %s> %s() noexcept\n        {\n            return %s;\n        }" % (element, function, array) for function, element, array in accessors)

    header = """/**
 * Auto-generated by tools/gen_encounters9a.py from PKHeX's Legends: Z-A encounter data. DO NOT EDIT BY HAND --
 * rerun the generator instead.
 *
 * One accessor per PKHeX array (Encounters9a, EncounterEvent.MGDB_G9A), unmerged and in PKHeX's order.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERDATA9A_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERDATA9A_H

#include <span>

#include "Legality/Encounters/EncounterArea9a.h"
#include "Legality/Encounters/EncounterGift9a.h"
#include "Legality/Encounters/EncounterStatic9a.h"
#include "Legality/Encounters/EncounterTrade9a.h"
#include "Legality/Encounters/MysteryGiftWA9.h"

namespace Legality
{
    namespace Encounters
    {
%s
    }
}

#endif
""" % declarations
    source = """// Auto-generated by tools/gen_encounters9a.py. DO NOT EDIT BY HAND.
#include "Legality/Encounters/EncounterData9a.h"

#include "Legality/Encounters/EncounterSlot9a.h"

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
