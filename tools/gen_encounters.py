#!/usr/bin/env python3
"""Generate include/Legality/EncounterTable.h + src/Legality/EncounterTable.cpp -- the
per-game encounter templates Layer 3 legality matches a Pokemon against.

WHAT THIS IS
------------
PKHeX models an encounter as a rich template plus an RNG correlation: it can prove a
specific seed produced a specific PID/IV spread. PKSE does not attempt that. What it
keeps is the part that answers the questions a save editor can actually be wrong about:

    * is there any encounter in this game that produces this species+form,
    * at the met location the Pokemon claims,
    * at the met level it claims,
    * and does that encounter's fixed data (shiny lock, ball, gender, nature,
      guaranteed-31 count, alpha, fateful) agree with what the Pokemon holds?

So every template collapses to one flat row. Wild slots additionally MERGE: all the
slots for one (species, form, location) in one game fold into a single row whose level
span is their union. That is deliberately permissive -- a merged span can admit a level
that no individual slot offered -- and permissive is the right direction for a checker
that reports problems rather than granting approval.

WHAT IS DELIBERATELY NOT MODELLED
---------------------------------
* Ability slot. Ability Capsule (1<->2) and Ability Patch (->hidden) exist from Gen 8
  on, so a template's AbilityPermission says nothing about what the Pokemon holds now.
  Storing the column would only invite a check that false-flags legitimate saves.
  Layer 2 already verifies the ability is one the species can have at all.
* Mystery Gift / event distributions. That is its own database (PKHeX's mgdb) and its
  own workstream; see docs/FUTURE_VERSIONS.md. The matcher compensates by softening its
  verdict for a Pokemon flagged as a fateful encounter.
* Seed / PID correlations, marks, ribbons, relearn moves, HOME trackers.

SOURCES (all PKHeX, pinned by tools/pkhex_source.py)
----------------------------------------------------
Wild slots come from the binary .pkl area blobs, each with its own layout:
    FRLG  Resources/legality/wild/Gen3/encounter_{fr,lg}.pkl        u32 container
    LGPE  Resources/legality/wild/Gen7/encounter_{gp,ge}.pkl        u32 container
    SWSH  .../Gen8/encounter_{sw,sh}_{symbol,hidden}.pkl            u32 container
    BDSP  .../Gen8/encounter_{bd,sp}[_underground].pkl              u32 container
    PLA   .../Gen8/encounter_la.pkl                                 u32 container
    SV    .../Gen9/encounter_wild_paldea.pkl, encounter_outbreak_paldea.pkl
    ZA    .../Gen9/encounter_za.pkl, encounter_hyperspace_za.pkl    u16 container
Statics / gifts / trades are C# object-initializer arrays in
Legality/Encounters/Data/Gen{3,7,8,9}/Encounters*.cs and are parsed from source.
Raids come from both: SWSH nests/distribution/Max Lair from .pkl, SV Tera/distribution/
Mightiest/Fixed from .pkl, plus the C# crystal list.

Regenerate:  python tools/gen_encounters.py
Pulls the PKHeX resources + sources it reads from GitHub on demand; no local checkout.
"""
import collections
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pkhex_source import pkhex_path  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUTPUT_HEADER = os.path.join(ROOT, "include", "Legality", "EncounterTable.h")
OUTPUT_SOURCE = os.path.join(ROOT, "src", "Legality", "EncounterTable.cpp")

MAX_SPECIES = 1025          # matches Pokemon::PERSONAL_MAX_SPECIES
SPECIES_INDEX_LENGTH = MAX_SPECIES + 2


def WILD(*parts):
    return pkhex_path("Resources/legality/wild/" + "/".join(parts))


def CORE(relpath):
    return pkhex_path(relpath)


# Games in table order. Each maps to an Enums::GameVersion group plus the version ids
# that occupy bit 0, bit 1 (and for Ruby/Sapphire/Emerald, bit 2) of a row's `versions` mask.
#
# NEW GROUPS ARE APPENDED, never inserted. The emitted tables are ordered by this list, so
# inserting one rewrites every table after it and buries the real change in the diff --
# and reviewing that diff is the only gate the generated data has.
GAMES = ["FRLG", "GG", "SWSH", "BDSP", "PLA", "SV", "ZA", "RSE", "DP", "PT", "HGSS",
         "BW", "B2W2", "XY", "ORAS", "SM", "USUM", "RBY", "GSC"]
GROUP_ENUM = {"FRLG": "FRLG", "GG": "GG", "SWSH": "SWSH", "BDSP": "BDSP", "PLA": "PLA", "SV": "SV", "ZA": "ZA",
              "RSE": "RSE", "DP": "DP", "PT": "PT", "HGSS": "HGSS",
              "BW": "BW", "B2W2": "B2W2", "XY": "XY", "ORAS": "ORAS", "SM": "SM", "USUM": "USUM",
              "RBY": "RBY", "GSC": "GSC"}
# Enums::GameVersion names in bit order: the first occupies bit 0, the second bit 1, and so
# on. Ruby/Sapphire/Emerald is the only group with three games -- every other one in every
# generation is a pair or a single title.
GROUP_VERSIONS = {
    "FRLG": ("FR", "LG"), "GG": ("GP", "GE"), "SWSH": ("SW", "SH"),
    "BDSP": ("BD", "SP"), "PLA": ("PLA",), "SV": ("SL", "VL"), "ZA": ("ZA",),
    "RSE": ("RU", "SA", "EM"), "DP": ("D", "P"), "PT": ("Pt",), "HGSS": ("HG", "SS"),
    "BW": ("B", "W"), "B2W2": ("B2", "W2"), "XY": ("X", "Y"), "ORAS": ("AS", "OR"),
    "SM": ("SN", "MN"), "USUM": ("US", "UM"),
    # Red/Green/Blue/Yellow is the only group of FOUR. PKHeX tags the international Blue's slot
    # data as Green, because they are the same game, and keeps the Japanese Blue apart as BU.
    "RBY": ("RD", "GN", "YW", "BU"), "GSC": ("GD", "SI", "C"),
}
BOTH = 0b11   # a template both versions of a PAIR share; use all_versions() for a group


def all_versions(game):
    """Mask with every one of `game`'s versions set -- 0b11 for a pair, 0b111 for RSE."""
    return (1 << len(GROUP_VERSIONS[game])) - 1


# PKHeX's C# GameVersion tokens, WHERE THEY DIFFER from the Enums::GameVersion names above.
# Everywhere else the two coincide (SW, SH, BD, SP, HG, SS, D, P, Pt, ...) and version_mask
# matches by name. Gen 3's Hoenn trio is the one divergence: PKHeX spells the games R/S/E and
# writes RS for the pair that excludes Emerald, while PKSE's enum calls them RU/SA/EM. Matching
# those by hand rather than by string surgery -- "RS" contains neither "RU" nor "SA", so anything
# clever here would map them to the whole group and hand Emerald every Ruby-only legendary.
#
# A token mapped to 0 means "this template belongs to no game in this group" -- Gen 4 needs that,
# because PKHeX keeps Diamond, Pearl and Platinum in ONE source file while PKSE splits them into
# two groups with different save layouts. Without it, Platinum's table would inherit Dialga.
# GEN 3'S TWO GROUPS SHARE ONE HAND-WRITTEN DISTRIBUTION ARRAY, so each has to recognise the
# OTHER group's tokens and answer 0 -- "belongs to no game in this group" -- rather than let them
# fall through to "unrecognised", which keeps the row for every game in the group. PKHeX compares
# the version exactly (EncounterGift3.IsMatchExact opens with "Gen3 Version MUST match"), so a
# Ruby-only distribution sitting in FireRed's table accepts a Pokemon no cartridge could produce.
# EFL is Emerald + FireRed + LeafGreen, which is the whole of FR/LG but only Emerald of the trio.
CSHARP_VERSION_ALIASES = {
    "RSE": {"R": 0b001, "S": 0b010, "E": 0b100, "RS": 0b011, "RSE": 0b111,
            "FR": 0, "LG": 0, "FRLG": 0, "EFL": 0b100, "Gen3": 0b111},
    "FRLG": {"FR": 0b01, "LG": 0b10, "FRLG": 0b11, "EFL": 0b11, "Gen3": 0b11,
             "R": 0, "S": 0, "E": 0, "RS": 0, "RSE": 0},
    "DP":   {"D": 0b01, "P": 0b10, "DP": 0b11, "DPPt": 0b11, "Pt": 0},
    "PT":   {"Pt": 0b1, "DPPt": 0b1, "D": 0, "P": 0, "DP": 0},
    "HGSS": {"HG": 0b01, "SS": 0b10, "HGSS": 0b11},
    # Omega Ruby/Alpha Sapphire is the one later group whose PKHeX token is not its PKSE name:
    # PKHeX writes AO for the pair. Every other Gen 5-7 token (B, W, B2, W2, X, Y, SN, MN, US, UM,
    # and the group spellings BW / B2W2 / XY / SM / USUM) already matches.
    "ORAS": {"AO": 0b11, "ORAS": 0b11},
    # Gen 1 and Gen 2 spell their pairings the way the games were sold: RB is Red and Blue (whose
    # data is Green's), GS is Gold and Silver without Crystal.
    "RBY": {"RD": 0b0001, "GN": 0b0010, "YW": 0b0100, "BU": 0b1000,
            "RB": 0b0011, "RBY": 0b1111},
    "GSC": {"GD": 0b001, "SI": 0b010, "C": 0b100, "GS": 0b011, "GSC": 0b111},
}

# Row field encodings -- mirrored by the enums in the emitted header.
KIND = {"wild": 0, "static": 1, "gift": 2, "trade": 3, "raid": 4, "event": 5}
SHINY = {"random": 0, "never": 1, "always": 2}
FORM_ANY, GENDER_ANY, NATURE_ANY = 0xFF, 0xFF, 0xFF
F_FATEFUL, F_ALPHA, F_BOOST60, F_EGG, F_MET_LEVEL_ZERO, F_CATCH_COMBO = 1, 2, 4, 8, 16, 32

# Location sentinels for templates whose met location is a whole region rather than one
# id. SW/SH distribution raids are the only case: the den is anonymous, so any Wild Area
# location of the right DLC tier is legal (PKHeX EncounterStatic8ND.IsMatchLocation).
ENCOUNTER_LOCATION_SWSH_WILDAREA               = 0xFFF0  # Wild Area (Galar)
ENCOUNTER_LOCATION_SWSH_WILDAREA_ISLE_OF_ARMOR = 0xFFF1  # + Isle of Armor
ENCOUNTER_LOCATION_SWSH_WILDAREA_ALL           = 0xFFF2  # + Crown Tundra

# A ROAMER IS MET WHERE YOU CATCH IT, not where its template says. Latias/Latios in Gen 3 and the
# six Gen 4 roamers wander a fixed set of routes, so their template's Location is a starting point
# rather than a constraint, and matching it literally would flag every roamer anyone has ever
# caught. PKHeX tests a route RANGE instead (EncounterStatic3/4.IsMatchLocation); these sentinels
# carry that range through the table so the row stays one flat row.
#
# Gen 4 additionally narrows the set by the GroundTile the Pokemon was standing on (grass or
# water). PKSE takes the UNION of the two, which can only ever accept a location PKHeX would
# reject -- the permissive direction this whole table is built in.
ENCOUNTER_LOCATION_ROAMER3        = 0xFFF3  # Hoenn routes 101-138 (met 16-49)
ENCOUNTER_LOCATION_ROAMER4_SINNOH = 0xFFF4  # PKHeX FirstS 16  + PermitGrassS | PermitWaterS
ENCOUNTER_LOCATION_ROAMER4_JOHTO  = 0xFFF5  # PKHeX FirstJ 177 + PermitGrassJ | PermitWaterJ
ENCOUNTER_LOCATION_ROAMER4_KANTO  = 0xFFF6  # PKHeX FirstH 149 + PermitGrassH | PermitWaterH
ENCOUNTER_LOCATION_ROAMER5        = 0xFFF7  # Unova routes; PKHeX EncounterStatic5.IsRoamerMet
ENCOUNTER_LOCATION_ROAMER2        = 0xFFF8  # Johto routes; PKHeX EncounterStatic2.RoamLocations

# The three roamer route sets, as (first location, permitted bitmask) with bit N meaning
# `first + N`. PKHeX EncounterStatic4: grass and water masks, OR'd here into one set.
ROAMER4_RANGES = {
    ENCOUNTER_LOCATION_ROAMER4_SINNOH: (16,  0x2_8033FFFF | 0x2_803E3B9E),
    ENCOUNTER_LOCATION_ROAMER4_JOHTO:  (177, 0x0003E7FF | 0x0001E06E),
    ENCOUNTER_LOCATION_ROAMER4_KANTO:  (149, 0x0AB3FFFF | 0x0ABC1B28),
}
# Which region a Gen 4 roamer's template Location belongs to -- PKHeX switches on exactly these
# three values and treats anything else as no match.
ROAMER4_LOCATION_TO_SENTINEL = {16: ENCOUNTER_LOCATION_ROAMER4_SINNOH,
                                177: ENCOUNTER_LOCATION_ROAMER4_JOHTO,
                                149: ENCOUNTER_LOCATION_ROAMER4_KANTO}
# Tornadus and Thundurus wander one flat set rather than a per-region one, so Gen 5 needs no
# location-keyed lookup -- every roamer maps onto the same sentinel. PKHeX IsRoamerMet: locations
# below 32, permitted by this mask.
ROAMER5_PERMITTED = 0b10111111111111111000000000000000
# Every Gen 5 roamer template starts at location 25; the sentinel stands for the whole set.
ROAMER5_SENTINELS = {25: ENCOUNTER_LOCATION_ROAMER5}
# Raikou, Entei and Gold/Silver's Suicune roam Johto, so their templates' Location 2 is only where
# they start. Gen 2's C# has no IsRoaming property to read -- EncounterStatic2 COMPUTES it from the
# species and "not Tin Tower", which is why these are transcribed rather than parsed.
# PKHeX EncounterStatic2.RoamLocations, bit N = met location N: "Routes 29-46, except 40 & 41". Checked
# against that list, because a mistyped bit still builds and simply rejects one route.
ROAMER2_PERMITTED = 0b10_1000_1010_0100_0000_0110_0011_0100_1000_1001_0011_0100
if [location for location in range(64) if (ROAMER2_PERMITTED >> location) & 1] != \
        [2, 4, 5, 8, 11, 15, 18, 20, 21, 25, 26, 34, 37, 39, 43, 45]:
    raise SystemExit("ROAMER2_PERMITTED does not expand to PKHeX's sixteen roaming routes")
ROAMER2_SPECIES = (243, 244, 245)   # Raikou, Entei, Suicune
TIN_TOWER_2 = 23                    # where Crystal's Suicune waits instead of roaming

LINK_TRADE_NPC_2 = 126       # PKHeX Locations.LinkTrade2NPC
LINK_TRADE_NPC_3 = 254       # PKHeX Locations.LinkTrade3NPC
LINK_TRADE_NPC_4 = 2001      # PKHeX Locations.LinkTrade4NPC
LINK_TRADE_NPC_5 = 30002     # PKHeX Locations.LinkTrade5NPC
FRIEND_SAFARI_LOCATION = 148  # X/Y only; PKHeX EncounterArea6XY's hand-written area
FRIEND_SAFARI_LEVEL = 30
# Dream Radar (B2W2, via the 3DS). PKHeX EncounterStatic5Radar: one location, one level band.
DREAM_RADAR_LOCATION = 30015
DREAM_RADAR_LEVEL_MIN = 5
DREAM_RADAR_LEVEL_MAX = 40
LINK_TRADE_NPC_6 = 30001     # PKHeX Locations.LinkTrade6NPC -- Gen 7b/8/9 in-game trades
TERA_CAVERN_9 = 30024        # PKHeX Locations.TeraCavern9
SHARED_NEST_8 = 162          # PKHeX Encounters8Nest.SharedNest
MAX_LAIR_8 = 244             # PKHeX Encounters8Nest.MaxLair
BDSP_NONE = 65535            # PKHeX Locations.Default8bNone -- BDSP's "no location"

# THE LEVEL AN EGG HATCHES AT AND THE MET LEVEL IT CARRIES ARE TWO DIFFERENT NUMBERS. PKHeX's
# EggLevel23 (5) is the CURRENT level an egg hatches at in Gens 2 and 3, not the met level;
# conflating the two claims every hatched Gen 3 Pokemon is met at level 5.
# EggStateLegality.GetEggLevelMet is the one that answers this:
#
#     2 => version is C ? EggMetLevel : 0   // Gold/Silver store no met data at all
#     3 or 4 => EggMetLevel34               // = 0
#     _ => EggMetLevel                      // = 1
#
# Measured rather than only read: the one hatched Pokemon in a real Ruby save (a Wynaut) carries
# met level 0 and met location 26, Route 111 -- where it hatched, several routes from the Route 117
# day care it was handed over at.
#
# Fields are named rather than positional. Adding Gen 5's third egg location once turned an
# `EGG_RULES[game][2]` that meant "hatch level" into one that meant "second alternate", and BDSP's
# two gift eggs quietly moved to met level 0; a namedtuple makes that class of edit impossible.
EggRuleSpec = collections.namedtuple(
    "EggRuleSpec",
    "egg_location alternate second_alternate hatch_met_level hatch_current_level hatch_location "
    "hatch_location_extra")


def _egg(egg_location, alternate, second_alternate, hatch_met_level, hatch_location,
         hatch_location_extra=0):
    # PKHeX EggStateLegality.GetEggLevel: EggLevel23 = 5 for Gens 2 and 3, EggLevel = 1 after.
    # Derived from the met level rather than listed, because the two move together: only Gens 3
    # and 4 stamp met level 0, and of those only Gen 3 hatches at 5.
    hatch_current_level = 5 if hatch_location in (16, 32, 146) else 1
    return EggRuleSpec(egg_location, alternate, second_alternate, hatch_met_level,
                       hatch_current_level, hatch_location, hatch_location_extra)


# egg_location is the nursery id stamped into the egg-location field; 0 means the format HAS no
# such field (Gens 2 and 3). alternate is what a traded egg carries instead.
# PKHeX: Locations.Daycare5 / Daycare8b / Picnic9 / LinkTrade6.
# hatch_location is the id PKHeX stamps when it BUILDS an egg for that game (Locations.HatchLocation*)
# -- the day care's own map, which is the canonical answer inside the permitted set and what the
# Gen 3 rebuild writes rather than scanning for the lowest permitted id.
EGG_RULES = {
    # Gen 3 has no egg-location field, a hatched egg is met at level 0, and the day care is Four
    # Island in Kanto / Route 117 in Hoenn.
    "FRLG": _egg(0, 0, 0, 0, 146),          # Locations.HatchLocationFRLG
    "RSE": _egg(0, 0, 0, 0, 32),            # Locations.HatchLocationRSE
    "DP": _egg(2000, 2002, 0, 0, 4),        # Daycare4, LinkTrade4; HatchLocationDPPt
    "PT": _egg(2000, 2002, 0, 0, 4),
    "HGSS": _egg(2000, 2002, 0, 0, 182),    # HatchLocationHGSS
    # GEN 5 HAS THREE legal bred-egg locations, which is why the row carries two alternates rather
    # than one: PKHeX's IsEggLocationBred5 accepts Daycare5, LinkTrade5 and LinkTrade5NPC, the last
    # being the id Spin Trade writes ("incorrectly used ... two legal values!", its own comment).
    # Every other generation uses at most two and leaves the third slot 0.
    # GEN 2 IS CRYSTAL-ONLY HERE: it invented breeding and has no egg-location field, and Gold and
    # Silver record no met data at all, so only a Crystal egg leaves a trace -- met level 1 wherever
    # it hatched. Locations.HatchLocationC is only what PKHeX's EncounterEgg2 STAMPS when it builds
    # one; see hatch_mask for the set a hatched egg may really carry.
    "GSC": _egg(0, 0, 0, 1, 16),
    "BW": _egg(60002, 30003, 30002, 1, 64),     # Daycare5, LinkTrade5, LinkTrade5NPC; HatchLocation5
    "B2W2": _egg(60002, 30003, 30002, 1, 64),
    # Gen 6 and Gen 7 share one rule (PKHeX IsEggLocationBred6, and GetDaycareLocation falls through
    # to Daycare5 for both): the nursery id, or LinkTrade6 for a traded egg.
    # POKE PELAGO IS A HATCH LOCATION NO MASK CAN HOLD: EggHatchLocation7 answers yes for 30016 as
    # well as for everything in its byte table, and a byte-per-location mask reaching 30016 would be
    # thirty kilobytes of zeros. It rides alongside as a single extra id instead.
    "XY": _egg(60002, 30002, 0, 1, 38),         # HatchLocation6XY
    "ORAS": _egg(60002, 30002, 0, 1, 318),      # HatchLocation6AO
    "SM": _egg(60002, 30002, 0, 1, 78, 30016),  # HatchLocation7; Locations.Pelago7
    "USUM": _egg(60002, 30002, 0, 1, 78, 30016),
    "SWSH": _egg(60002, 30002, 0, 1, 40),       # Daycare5, LinkTrade6; HatchLocation8
    "BDSP": _egg(60010, 30002, 0, 1, 446),      # Daycare8b, LinkTrade6; HatchLocation8b
    "SV": _egg(30023, 30002, 0, 1, 6),          # Picnic9, LinkTrade6; HatchLocation9
}
NO_BREEDING = ("RBY", "GG", "PLA", "ZA")  # Gen 1, Let's Go, Legends: Arceus and Z-A: no daycare


def hatch_level(game):
    """The MET level a hatched egg carries in `game` -- 0 in Gens 3 and 4, 1 from Gen 5 on."""
    return EGG_RULES[game].hatch_met_level


def hatch_current_level(game):
    """The level a Pokemon is AT when its egg hatches -- 5 in Gens 2 and 3, 1 from Gen 4 on.

    Not the same number as hatch_level(), which is the MET level, and the two disagree in every
    generation: Gen 3 hatches at 5 and stamps 0, Gen 5 hatches at 1 and stamps 1.
    """
    return EGG_RULES[game].hatch_current_level


def hatch_location(game):
    """The met location PKHeX stamps when it builds an egg for `game` (its day care's own map)."""
    return EGG_RULES[game].hatch_location


def hatch_location_extra(game):
    """One further permitted hatch location too far up the id space for a mask; 0 when none."""
    return EGG_RULES[game].hatch_location_extra


# ---------------------------------------------------------------------------
# Binary containers
# ---------------------------------------------------------------------------
def _load(path):
    with open(path, "rb") as binary_file:
        return binary_file.read()


def split_blobs_uint32(data):
    """PKHeX BinLinkerAccessor -- magic[2], u16 count, then count+1 u32 offsets."""
    blob_count = struct.unpack_from("<H", data, 2)[0]
    offsets = struct.unpack_from("<%dI" % (blob_count + 1), data, 4)
    return [data[offsets[blob_index]:offsets[blob_index + 1]] for blob_index in range(blob_count)]


def split_blobs_uint16(data):
    """PKHeX BinLinkerAccessor16 -- same, with u16 offsets."""
    blob_count = struct.unpack_from("<H", data, 2)[0]
    offsets = struct.unpack_from("<%dH" % (blob_count + 1), data, 4)
    return [data[offsets[blob_index]:offsets[blob_index + 1]] for blob_index in range(blob_count)]


# ---------------------------------------------------------------------------
# C# source parsing
# ---------------------------------------------------------------------------
def read_cs(relpath):
    with open(CORE(relpath), encoding="utf-8") as source_file:
        return source_file.read()


def strip_comments(text):
    """Drop // and /* */ comments, leaving string literals intact."""
    out = []
    index, length = 0, len(text)
    while index < length:
        character = text[index]
        if character == '"':
            end_index = index + 1
            while end_index < length and text[end_index] != '"':
                end_index += 2 if text[end_index] == '\\' else 1
            out.append(text[index:end_index + 1])
            index = end_index + 1
        elif character == '/' and index + 1 < length and text[index + 1] == '/':
            end_index = text.find("\n", index)
            index = length if end_index < 0 else end_index
        elif character == '/' and index + 1 < length and text[index + 1] == '*':
            end_index = text.find("*/", index)
            index = length if end_index < 0 else end_index + 2
        else:
            out.append(character)
            index += 1
    return "".join(out)


def load_enum(relpath, enum_name):
    """Parse a flat C# enum into {member: value}, honouring explicit `= n` members.

    The identifier pattern is Unicode-aware (`[^\\W\\d]\\w*`), not `[A-Za-z_]...`, because
    PKHeX's Species enum contains **Flabébé**. An ASCII-only pattern does not merely miss
    that one name -- the member fails to match, the running counter never advances past it,
    and EVERY species above #669 comes out one too low. Silent, too: the dict still has the
    right number of keys and every dex number below the accent is correct.
    """
    text = strip_comments(read_cs(relpath))
    enum_match = re.search(r"enum\s+" + enum_name + r"\b[^{]*\{", text)
    if not enum_match:
        raise SystemExit("enum %s not found in %s" % (enum_name, relpath))
    depth, index = 1, enum_match.end()
    while depth:
        if text[index] == '{':
            depth += 1
        elif text[index] == '}':
            depth -= 1
        index += 1
    body = text[enum_match.end():index - 1]
    out, next_value = {}, 0
    for member in body.split(","):
        member = member.strip()
        if not member:
            continue
        member_match = re.match(r"([^\W\d]\w*)\s*(?:=\s*(-?\w+))?$", member)
        if not member_match:
            # Never skip silently: a dropped member shifts every later value by one.
            raise SystemExit("%s: cannot parse enum member %r" % (enum_name, member))
        if member_match.group(2) is not None:
            next_value = int(member_match.group(2), 0)
        out[member_match.group(1)] = next_value
        next_value += 1
    return out


def split_top(text, opener="([{", closer=")]}"):
    """Split a C# argument/initializer list on top-level commas."""
    parts, depth, current_part, index, length = [], 0, [], 0, len(text)
    while index < length:
        character = text[index]
        if character == '"':
            end_index = index + 1
            while end_index < length and text[end_index] != '"':
                end_index += 2 if text[end_index] == '\\' else 1
            current_part.append(text[index:end_index + 1])
            index = end_index + 1
            continue
        if character in opener:
            depth += 1
        elif character in closer:
            depth -= 1
        if character == ',' and depth == 0:
            parts.append("".join(current_part).strip())
            current_part = []
        else:
            current_part.append(character)
        index += 1
    tail = "".join(current_part).strip()
    if tail:
        parts.append(tail)
    return parts


def _match(text, index, open_character, close_character):
    """Index just past the bracket group starting at text[i] == open_ch."""
    depth, length = 0, len(text)
    while index < length:
        character = text[index]
        if character == '"':
            index += 1
            while index < length and text[index] != '"':
                index += 2 if text[index] == '\\' else 1
        elif character == open_character:
            depth += 1
        elif character == close_character:
            depth -= 1
            if depth == 0:
                return index + 1
        index += 1
    raise SystemExit("unbalanced %s%s" % (open_character, close_character))


def parse_template(text, name):
    """Parse a single `... Name = new(..) { .. };` declaration into one (pos_args, props) entry.

    The base a `with` expression copies from -- pass the result to parse_array as `bases`. Separate
    from parse_array because this is one template rather than a list, and because a caller has to
    name it deliberately rather than have it discovered.
    """
    declaration = re.search(re.escape(name) + r"\s*=\s*new\b", text)
    if not declaration:
        raise SystemExit("template %s not found" % name)
    index = declaration.end()
    args = []
    while index < len(text) and text[index] in " \t\r\n":
        index += 1
    if index < len(text) and text[index] == '(':
        end = _match(text, index, '(', ')')
        args = split_top(text[index + 1:end - 1])
        index = end
    while index < len(text) and text[index] in " \t\r\n":
        index += 1
    props = {}
    if index < len(text) and text[index] == '{':
        end = _match(text, index, '{', '}')
        for part in split_top(text[index + 1:end - 1]):
            if '=' in part:
                key, value = part.split("=", 1)
                props[key.strip()] = value.strip()
    return args, props


def parse_array(text, array_name, bases=None):
    """Parse `<array_name> = [ new(..) { .. }, .. ];` into [(pos_args, props)] entries.

    `bases` maps an identifier to an already-parsed template, which is what makes C# RECORD COPIES
    readable: PKHeX writes ORAS's Cosplay Pikachu as `BaseCosplay with {Form = 1, Moves = new(..)}`
    and, for the last one, as a bare `BaseCosplay`. Neither starts with `new`, so without this the
    scanner walks straight past them to the `new(` inside the override's own Moves list and parses
    THAT as the entry -- an entry with no Species, which is how it announced itself.
    """
    array_match = re.search(re.escape(array_name) + r"\s*=\s*\[", text)
    if not array_match:
        raise SystemExit("array %s not found" % array_name)
    start = text.index("[", array_match.end() - 1)
    body = text[start + 1:_match(text, start, '[', ']') - 1]
    base_pattern = None
    if bases:
        base_pattern = re.compile(r"\b(" + "|".join(re.escape(key) for key in bases) + r")\b")

    entries, index, length = [], 0, len(body)
    while index < length:
        new_match = re.compile(r"\bnew\b").search(body, index)
        base_match = base_pattern.search(body, index) if base_pattern else None
        if base_match and (not new_match or base_match.start() < new_match.start()):
            base_args, base_props = bases[base_match.group(1)]
            props = dict(base_props)
            index = base_match.end()
            with_match = re.compile(r"\s*with\s*\{").match(body, index)
            if with_match:
                brace = body.index('{', with_match.end() - 1)
                end = _match(body, brace, '{', '}')
                for part in split_top(body[brace + 1:end - 1]):
                    if '=' in part:
                        key, value = part.split("=", 1)
                        props[key.strip()] = value.strip()
                index = end
            entries.append((list(base_args), props))
            continue
        if not new_match:
            break
        index = new_match.end()
        # An explicit `new EncounterX(...)` names the type before the argument list.
        type_match = re.compile(r"\s*[A-Za-z_][A-Za-z0-9_]*").match(body, index)
        if type_match and body[type_match.end():type_match.end() + 1] in "( {":
            index = type_match.end()
        args = []
        while index < length and body[index] in " \t\r\n":
            index += 1
        if index < length and body[index] == '(':
            end = _match(body, index, '(', ')')
            args = split_top(body[index + 1:end - 1])
            index = end
        while index < length and body[index] in " \t\r\n":
            index += 1
        props = {}
        if index < length and body[index] == '{':
            end = _match(body, index, '{', '}')
            for part in split_top(body[index + 1:end - 1]):
                if '=' not in part:
                    continue
                key, value = part.split("=", 1)
                props[key.strip()] = value.strip()
            index = end
        entries.append((args, props))
    return entries


def assert_all_arrays_used(relpath, used):
    """Fail if the file declares an inline encounter array this generator ignores.

    PKHeX splits its statics by version -- Encounter_SV / StaticSL / StaticVL -- and
    adds arrays as games get DLC. Reading only the ones that existed when this was
    written is how a whole game's box legendary silently vanishes from the table, so
    the file itself is the list and this asserts we consume all of it. Arrays built
    from a .pkl (`= GetBase(...)`) are excluded: those are read as binary elsewhere,
    as are EncounterArea[] lists, which only ever compose those binary loads.
    """
    text = strip_comments(read_cs(relpath))
    declared = set(re.findall(
        r"static readonly Encounter(?!Area)\w*\[\]\s+(\w+)\s*=\s*\[", text))
    # A SINGLE TEMPLATE IS JUST AS EASY TO MISS. Gen 2's Virtual Console Celebi is declared as one
    # `EncounterStatic2 CelebiVC = new(..)` rather than inside an array, so it never reached the
    # table and every Crystal Celebi read as unobtainable.
    declared |= set(re.findall(
        r"static readonly Encounter(?!Area)\w*\s+(\w+)\s*=\s*new\b", text))
    missing = declared - set(used)
    if missing:
        raise SystemExit("%s declares encounter arrays this generator ignores: %s"
                         % (relpath, ", ".join(sorted(missing))))


SPECIES = load_enum("Game/Enums/Species.cs", "Species")
BALL = load_enum("Game/Enums/Ball.cs", "Ball")
NATURE = load_enum("Game/Enums/Nature.cs", "Nature")

# Anchor the species enum against known dex numbers. Bulbasaur alone would not catch the
# failure this guards -- everything BELOW Flabébé (#669) parses correctly, and only the ids
# above the accented name shift. Pecharunt is the one that matters.
for _name, _want in (("Bulbasaur", 1), ("Flabébé", 669), ("Pecharunt", 1025)):
    if SPECIES.get(_name) != _want:
        raise SystemExit("Species enum parsed wrong: %s = %s, expected %d"
                         % (_name, SPECIES.get(_name), _want))
ABILITY_ALIAS = {"A0": "OnlyFirst", "A2": "OnlyHidden", "A3": "Any12", "A4": "Any12H"}
# Encounters8a's height/weight shorthand -- irrelevant to matching, but they occupy
# positional constructor slots so the parser has to resolve them.
LA_SCALARS = {"M": 127, "A": 255, "U": 128}


def csharp_value(expr, default=None):
    """Evaluate a C# literal/enum reference used in the encounter tables."""
    expr = expr.strip()
    if not expr:
        return default
    if re.fullmatch(r"-?0[xX][0-9A-Fa-f]+", expr):
        return int(expr, 16)
    if re.fullmatch(r"-?\d+", expr):
        return int(expr)
    if expr in LA_SCALARS:
        return LA_SCALARS[expr]
    if expr in ("true", "True"):
        return 1
    if expr in ("false", "False", "default"):
        return 0
    for prefix, table in (("Ball.", BALL), ("Nature.", NATURE), ("Species.", SPECIES),
                          ("Core.Species.", SPECIES)):
        if expr.startswith(prefix):
            return table[expr[len(prefix):]]
    if expr.startswith("Locations."):
        return {"Default8bNone": BDSP_NONE, "TeraCavern9": TERA_CAVERN_9}[expr[len("Locations."):]]
    # VIVILLON'S PATTERN FOLLOWS THE CONSOLE'S REGION, not the encounter, so PKHeX writes a
    # sentinel form here rather than a number. Any of the twenty patterns is legal for such a
    # template, which is exactly what FORM_ANY means in a row.
    if expr.split(".")[-1] == "FormVivillon":
        return FORM_ANY
    if expr in BALL:
        return BALL[expr]
    if default is not None:
        return default
    raise SystemExit("cannot evaluate C# value %r" % expr)


def shiny_of(props):
    shiny_name = props.get("Shiny", "")
    shiny_name = shiny_name.split(".")[-1]
    return SHINY.get(shiny_name.lower(), SHINY["random"])


def gender_of(props):
    if "Gender" not in props:
        return GENDER_ANY
    return csharp_value(props["Gender"], GENDER_ANY)


def nature_of(props):
    if "Nature" not in props:
        return NATURE_ANY
    nature_name = props["Nature"].split(".")[-1]
    if nature_name == "Random":
        return NATURE_ANY
    return NATURE[nature_name]


def flags_of(props, extra=0):
    flags = extra
    if csharp_value(props.get("FatefulEncounter", "0")):
        flags |= F_FATEFUL
    if csharp_value(props.get("IsAlpha", "0")):
        flags |= F_ALPHA
    if csharp_value(props.get("IsEgg", "0")):
        flags |= F_EGG
    return flags


# ---------------------------------------------------------------------------
# Row assembly
# ---------------------------------------------------------------------------
FIELDS = ("species", "location", "eggLocation", "form", "levelMin", "levelMax",
          "versions", "kind", "shiny", "gender", "fixedBall", "flawlessIVs",
          "nature", "flags")


def row(species, location, form, level_min, level_max, versions, kind,
        shiny=0, gender=GENDER_ANY, ball=0, flawless=0, nature=NATURE_ANY,
        flags=0, egg_location=0):
    # PKHeX spells "no fixed gender" as both 3 (the Gender enum's Random) and 0xFF
    # (FixedGenderUtil.GenderRandom), and the raid blobs store gender biased by +1 so
    # that 0 can mean "any" -- which underflows to -1. One sentinel out the far side.
    if not 0 <= gender <= 2:
        gender = GENDER_ANY
    return (species, location, egg_location, form, level_min, level_max, versions,
            KIND[kind], shiny, gender, ball, flawless, nature, flags)


def wild_form(form):
    """A slot form at/above FormDynamic (30) means the game randomises it."""
    return FORM_ANY if form >= 30 else form


def merge_rows(rows):
    """Fold wild/raid rows sharing a (species, form, location, constraint) key.

    Two stages, and the order matters. First the level spans of slots that share a
    version are unioned -- Sword's Route 5 Nickit rows become one 15-20 row. Only then
    are the two versions folded together, and only when everything else about the row
    is already identical. Doing it the other way round would union Sword's levels with
    Shield's and invent a span neither game offers.

    Within a stage the guaranteed-31 count takes the minimum: the permissive direction,
    so merging can never invent a problem the unmerged slots did not have.

    Static/gift/trade rows are only deduplicated, never merged: each of those IS a
    distinct template and its fixed data has to stay exact.
    """
    mergeable = (KIND["wild"], KIND["raid"])
    merged, plain = {}, set()
    for encounter_row in rows:
        if encounter_row[FIELDS.index("kind")] not in mergeable:
            plain.add(encounter_row)
            continue
        fields = dict(zip(FIELDS, encounter_row))
        key = (fields["species"], fields["location"], fields["eggLocation"], fields["form"], fields["kind"],
               fields["shiny"], fields["gender"], fields["fixedBall"], fields["nature"], fields["flags"],
               fields["versions"])
        existing = merged.get(key)
        if existing is None:
            merged[key] = [fields["levelMin"], fields["levelMax"], fields["flawlessIVs"]]
        else:
            existing[0] = min(existing[0], fields["levelMin"])
            existing[1] = max(existing[1], fields["levelMax"])
            existing[2] = min(existing[2], fields["flawlessIVs"])

    by_version = {}
    for key, (level_min, level_max, flawless) in merged.items():
        rest = key[:-1]
        merged_key = rest + (level_min, level_max, flawless)
        by_version[merged_key] = by_version.get(merged_key, 0) | key[-1]

    out = list(plain)
    for key, version_bits in by_version.items():
        (species, location, egg_location, form, kind, shiny, gender, ball, nature,
         flags, level_min, level_max, flawless) = key
        out.append((species, location, egg_location, form, level_min, level_max,
                    version_bits, kind, shiny, gender, ball, flawless, nature, flags))
    out.sort()
    return out


# ---------------------------------------------------------------------------
# Wild slots, per game
# ---------------------------------------------------------------------------
def wild_gen12(generation, files, has_slot_rates):
    """Gen 1 and Gen 2 wild slots.

    A 4-byte area header (location, time-of-day, slot type, rate) then 4-byte slots of
    `species, slotNumber, levelMin, levelMax` -- the species is a single BYTE here, which is all
    Gen 1 and Gen 2 need. PKHeX EncounterArea1 / EncounterArea2.

    GEN 2 PUTS A RATE TABLE IN FRONT OF THE SLOTS for every type past Surf (the rods, Rock Smash,
    Headbutt, the Bug Contest): one byte per slot, ahead of the slots themselves. Reading those
    bytes as slot data turns rates into species, so the count has to be worked out first --
    `length / 5` with the rates, `length / 4` without.
    """
    out = []
    for file_name, version_bits in files:
        for area in split_blobs_uint32(_load(WILD(generation, file_name))):
            location, slot_type, body = area[0], area[2], area[4:]
            if has_slot_rates and slot_type > GEN2_SLOT_TYPE_SURF:
                slot_count = len(body) // 5
                body = body[slot_count:]
            for offset in range(0, len(body) - 3, 4):
                species, _slot, level_min, level_max = struct.unpack_from("<BBBB", body, offset)
                if species == 0:
                    continue
                # Unown's letter is chosen by the game rather than the slot (PKHeX FormRandom),
                # so any of its forms is legal for one of these.
                form = FORM_ANY if species == 201 else 0
                out.append(row(species, location, form, level_min, level_max, version_bits, "wild"))
    return out


def wild_rby():
    # "blue" is the INTERNATIONAL Blue, which is the Japanese Green's data -- PKHeX tags that file
    # GN and keeps the Japanese Blue separately as blue_jp.
    return wild_gen12("Gen1", (("encounter_red.pkl", 1), ("encounter_blue.pkl", 2),
                               ("encounter_yellow.pkl", 4), ("encounter_blue_jp.pkl", 8)), False)


def wild_gsc():
    return wild_gen12("Gen2", (("encounter_gold.pkl", 1), ("encounter_silver.pkl", 2),
                               ("encounter_crystal.pkl", 4)), True)


def static_rby():
    """Gen 1 statics, gifts and in-game trades.

    EVERY ONE IS MET AT LOCATION 0, because a PK1 has nowhere to record where it was caught --
    PKHeX's EncounterStatic1 and EncounterTrade1 both hardcode `Location => 0`. The matcher knows
    not to ask (see hasMetData); the rows carry 0 so nothing here pretends otherwise.
    """
    source_path = "Legality/Encounters/Data/Gen1/Encounters1.cs"
    text = strip_comments(read_cs(source_path))
    assert_all_arrays_used(source_path, ["StaticRBY", "StaticRB", "StaticYW", "StaticBU",
                                 "TradeGift_RB", "TradeGift_YW", "TradeGift_BU"])
    out = []
    for name in ("StaticRBY", "StaticRB", "StaticYW", "StaticBU"):
        for args, properties in parse_array(text, name):
            version_bits = version_from_args(args, "RBY")
            if version_bits == 0:
                continue
            species, level = csharp_value(args[0]), csharp_value(args[1])
            out.append(row(species, 0, 0, level, level, version_bits, "static",
                           flags=flags_of(properties)))
    # EncounterTrade1(names, index, species, version, levelRBY) -- species third, version fourth.
    for name in ("TradeGift_RB", "TradeGift_YW", "TradeGift_BU"):
        for args, properties in parse_array(text, name):
            version_bits = version_from_args(args, "RBY")
            if version_bits == 0:
                continue
            level = csharp_value(args[4], 0)
            out.append(row(csharp_value(args[2]), 0, 0, level, level, version_bits, "trade",
                           gender=gender_of(properties)))
    return out


def static_gsc():
    """Gen 2 statics, gifts, the Odd Egg and in-game trades.

    Gen 2 DOES record a location -- but only Crystal writes it, so a Gold or Silver Pokemon has
    none and takes the same species-and-level path Gen 1 does. The rows carry the location either
    way; which half of the check runs is the matcher's decision, not the table's.
    """
    source_path = "Legality/Encounters/Data/Gen2/Encounters2.cs"
    text = strip_comments(read_cs(source_path))
    assert_all_arrays_used(source_path, ["StaticGSC", "StaticGS", "StaticGD", "StaticSI",
                                 "StaticC", "StaticOddEggC", "TradeGift_GSC", "CelebiVC"])
    out = []
    # THE VIRTUAL CONSOLE CELEBI IS ONE TEMPLATE, NOT AN ARRAY, which is how it went unread: the
    # GS Ball event, caught at Ilex Forest in Crystal. PKHeX yields it as the Gen 2 VC event, and it
    # fixes no trainer (the player catches it), so a plain row states it exactly.
    entries = [(name, entry) for name in ("StaticGSC", "StaticGS", "StaticGD", "StaticSI", "StaticC",
                                          "StaticOddEggC")
               for entry in parse_array(text, name)]
    entries.append(("CelebiVC", parse_template(text, "CelebiVC")))
    for name, (args, properties) in entries:
        version_bits = version_from_args(args, "GSC")
        if version_bits == 0:
            continue
        species, level = csharp_value(args[0]), csharp_value(args[1])
        is_egg = csharp_value(properties.get("IsEgg", "0"))
        location = csharp_value(properties.get("Location", "0"))
        # A ROAMER IS MET WHERE IT WAS CORNERED -- see ROAMER2_PERMITTED.
        if species in ROAMER2_SPECIES and location != TIN_TOWER_2:
            location = ENCOUNTER_LOCATION_ROAMER2
        out.append(row(species, location, csharp_value(properties.get("Form", "0")), level, level,
                       version_bits, "static", shiny=shiny_of(properties),
                       gender=gender_of(properties),
                       flags=flags_of(properties, F_EGG if is_egg else 0)))
    # EncounterTrade2(names, index, species, level, tid16). No version argument: every Gen 2
    # in-game trade is in all three games, and a PK2 could not say which anyway.
    #
    # AN IN-GAME TRADE IS MET AT LinkTrade2NPC, AT MET LEVEL 0 -- EncounterTrade2 matches exactly
    # that pair in Crystal's caught data. Its Level is the lowest level the received Pokemon can
    # have, with no ceiling (LevelMax 100), so it bounds the current level rather than the met one.
    # Emitted at location 0 with one level, every trade in a Crystal save was "not a place" for
    # its species: Kyle's Onix and Kirk's Shuckle among them.
    for args, properties in parse_array(text, "TradeGift_GSC"):
        level = csharp_value(args[3])
        out.append(row(csharp_value(args[2]), LINK_TRADE_NPC_2, 0, level, 100, all_versions("GSC"),
                       "trade", gender=gender_of(properties), flags=F_MET_LEVEL_ZERO))
    return out


def wild_gen34(generation, header_bytes, files):
    """Gen 3 and Gen 4 wild slots -- one reader, because the SLOT is the same shape in both.

    Each area is a header followed by 10-byte slots of
    `species u16, form, slotNumber, levelMin, levelMax, ...`. Only the header length differs:
    Gen 3 is 4 bytes (location, _, type, rate) and Gen 4 is 6 (the same plus a u16 GroundTile).
    PKHeX EncounterArea3 / EncounterArea4 -- the latter slices its slots from `data[6..]`.

    The trailing 4 bytes of a Gen 4 slot (the mass-outbreak and swarm indices) are not read:
    they say which slot a swarm REPLACES, which is a question about that day's game state, not
    about whether the Pokemon in hand could have come from here.
    """
    out = []
    for file_name, version_bits in files:
        for area in split_blobs_uint32(_load(WILD(generation, file_name))):
            location, body = area[0], area[header_bytes:]
            for offset in range(0, len(body) - 9, 10):
                species, form, _slot, level_min, level_max = struct.unpack_from("<HBBBB", body, offset)
                out.append(row(species, location, form, level_min, level_max, version_bits, "wild"))
    return out


def wild_frlg():
    return wild_gen34("Gen3", 4, (("encounter_fr.pkl", 1), ("encounter_lg.pkl", 2)))


def wild_rse():
    # encounter_rse_swarm.pkl is deliberately not read: a swarm is a daily slot SUBSTITUTION,
    # and PKHeX models it as its own area whose species are already reachable by other means in
    # the same games. Skipping it can only narrow what the table accepts for four species.
    return wild_gen34("Gen3", 4, (("encounter_r.pkl", 1), ("encounter_s.pkl", 2),
                                  ("encounter_e.pkl", 4)))


def wild_dp():
    return wild_gen34("Gen4", 6, (("encounter_d.pkl", 1), ("encounter_p.pkl", 2)))


def wild_pt():
    return wild_gen34("Gen4", 6, (("encounter_pt.pkl", 1),))


def wild_hgss():
    return wild_gen34("Gen4", 6, (("encounter_hg.pkl", 1), ("encounter_ss.pkl", 2)))


def wild_gen567(generation, files):
    """Gen 5, 6 and 7 wild slots -- one reader for all three, because the layout never changed.

    Each area is a 4-byte header (location u16, slot type, pad) followed by 4-byte slots of
    `species|form<<11` u16, levelMin, levelMax. PKHeX EncounterArea5 / EncounterArea6XY /
    EncounterArea6AO / EncounterArea7 are the same twenty lines four times over, all slicing their
    slots from `data[4..]` and masking the species with 0x3FF.
    """
    out = []
    for file_name, version_bits in files:
        for area in split_blobs_uint32(_load(WILD(generation, file_name))):
            location = struct.unpack_from("<H", area, 0)[0]
            body = area[4:]
            for offset in range(0, len(body) - 3, 4):
                packed, level_min, level_max = struct.unpack_from("<HBB", body, offset)
                species = packed & 0x3FF
                # A ZERO SLOT IS PADDING, NOT AN ENCOUNTER. Gen 5 areas are a fixed number of slots
                # per type and the unused ones are left zeroed -- 574 of Black's 2974. PKHeX builds
                # a slot object for each and never matches them, because no Pokemon is species 0;
                # here they would be 1148 rows claiming species 0 is catchable.
                if species == 0:
                    continue
                out.append(row(species, location, wild_form(packed >> 11),
                               level_min, level_max, version_bits, "wild"))
    return out


def wild_bw():
    return wild_gen567("Gen5", (("encounter_b.pkl", 1), ("encounter_w.pkl", 2)))


def wild_b2w2():
    return wild_gen567("Gen5", (("encounter_b2.pkl", 1), ("encounter_w2.pkl", 2)))


def wild_xy():
    # THE FRIEND SAFARI IS NOT IN THE BINARY. PKHeX appends it as a hand-written area in
    # EncounterArea6XY's parameterless constructor, so reading only the .pkl leaves location 148
    # empty -- and every Friend Safari Pokemon, which is a great many of them, would report that
    # 148 is not a place it can be encountered. Level is a flat 30 and the species list is the
    # `AllFriendSafariSpecies` span, plus Floette's three colours and a random-region Vivillon.
    out = wild_gen567("Gen6", (("encounter_x.pkl", 1), ("encounter_y.pkl", 2)))
    text = strip_comments(read_cs("Legality/Encounters/Templates/Gen6/EncounterArea6XY.cs"))
    span_match = re.search(r"ReadOnlySpan<ushort>\s+AllFriendSafariSpecies\s*=>\s*\[(.*?)\];", text, re.S)
    if not span_match:
        raise SystemExit("AllFriendSafariSpecies not found in EncounterArea6XY.cs")
    for species in sorted({int(token) for token in re.findall(r"\d+", span_match.group(1))}):
        out.append(row(species, FRIEND_SAFARI_LOCATION, 0, FRIEND_SAFARI_LEVEL, FRIEND_SAFARI_LEVEL,
                       BOTH, "wild"))
    for form in (0, 1, 3):  # Floette keeps three of its flower colours here
        out.append(row(670, FRIEND_SAFARI_LOCATION, form, FRIEND_SAFARI_LEVEL, FRIEND_SAFARI_LEVEL,
                       BOTH, "wild"))
    # Vivillon's pattern follows the console's region, so any of them is legal here.
    out.append(row(666, FRIEND_SAFARI_LOCATION, FORM_ANY, FRIEND_SAFARI_LEVEL, FRIEND_SAFARI_LEVEL,
                   BOTH, "wild"))
    return out


def wild_oras():
    return wild_gen567("Gen6", (("encounter_as.pkl", 1), ("encounter_or.pkl", 2)))


def wild_sm():
    return wild_gen567("Gen7", (("encounter_sn.pkl", 1), ("encounter_mn.pkl", 2)))


def wild_usum():
    return wild_gen567("Gen7", (("encounter_us.pkl", 1), ("encounter_um.pkl", 2)))


def wild_gg():
    out = []
    for file_name, version_bits in (("encounter_gp.pkl", 1), ("encounter_ge.pkl", 2)):
        for area in split_blobs_uint32(_load(WILD("Gen7", file_name))):
            # A Let's Go area feeds up to two neighbours; a catch there can be met in
            # either (PKHeX EncounterArea7b.IsMatchLocation), so emit a row for each.
            locations = {area[0]} | {area[2], area[3]} - {0}
            body = area[4:]
            for offset in range(0, len(body) - 3, 4):
                species, _flags, level_min, level_max = struct.unpack_from("<BBBB", body, offset)
                for location in locations:
                    out.append(row(species, location, 0, level_min, level_max, version_bits, "wild",
                                   flags=F_CATCH_COMBO))
    return out


def _swsh_wander(text):
    """EncounterArea8.GetAreasCanWanderTo -> {location: [reachable locations]}."""
    switch_match = re.search(r"GetAreasCanWanderTo\(byte location\)\s*=>\s*location switch\s*\{", text)
    body = text[switch_match.end():_match(text, text.index("{", switch_match.end() - 1), '{', '}') - 1]
    out = {}
    for source, destinations in re.findall(r"(\d+)\s*=>\s*\[([^\]]*)\]", body):
        out[int(source)] = [int(token) for token in re.findall(r"\d+", destinations)]
    return out


def _swsh_wild_area(location):
    """EncounterArea8.IsWildArea -- levels there are boosted to 60 post-game."""
    return (122 <= location <= 154) or (164 <= location <= 194) or (204 <= location <= 234 and location != 206)


def wild_swsh():
    text = strip_comments(read_cs("Legality/Encounters/Templates/Gen8/EncounterArea8.cs"))
    wander = _swsh_wander(text)
    out = []
    for file_name, version_bits, crossover in (("encounter_sw_symbol.pkl", 1, True),
                                  ("encounter_sh_symbol.pkl", 2, True),
                                  ("encounter_sw_hidden.pkl", 1, False),
                                  ("encounter_sh_hidden.pkl", 2, False)):
        for area in split_blobs_uint32(_load(WILD("Gen8", file_name))):
            home = area[0]
            # Only visible ("symbol") encounters roam between areas.
            locations = [home] + (wander.get(home, []) if crossover else [])
            slot_count, offset, slots_read = area[1], 2, 0
            while slots_read != slot_count:
                _weather, level_min, level_max, count, _type = struct.unpack_from("<HBBBB", area, offset)
                offset += 6
                for _ in range(count):
                    packed = struct.unpack_from("<H", area, offset)[0]
                    offset += 2
                    slots_read += 1
                    for location in locations:
                        flags = F_BOOST60 if _swsh_wild_area(location) else 0
                        out.append(row(packed & 0x3FF, location, packed >> 11, level_min,
                                       level_max, version_bits, "wild", flags=flags))
    return out


# EncounterArea8b.CanCrossoverTo -- the only surf pairs whose slot lists differ.
BDSP_SURF_CROSS = {486: 167, 167: 486, 420: 489, 489: 420}
BDSP_SURF = 2  # SlotType8b.Surf


def wild_bdsp():
    out = []
    for file_name, version_bits in (("encounter_bd.pkl", 1), ("encounter_sp.pkl", 2),
                       ("encounter_bd_underground.pkl", 1),
                       ("encounter_sp_underground.pkl", 2)):
        for area in split_blobs_uint32(_load(WILD("Gen8", file_name))):
            home = struct.unpack_from("<H", area, 0)[0]
            locations = [home]
            if area[2] == BDSP_SURF and home in BDSP_SURF_CROSS:
                locations.append(BDSP_SURF_CROSS[home])
            body = area[4:]
            for offset in range(0, len(body) - 3, 4):
                packed, level_min, level_max = struct.unpack_from("<HBB", body, offset)
                for location in locations:
                    out.append(row(packed & 0x3FF, location, packed >> 11, level_min, level_max, version_bits, "wild"))
    return out


def wild_pla():
    out = []
    for area in split_blobs_uint32(_load(WILD("Gen8", "encounter_la.pkl"))):
        count = area[0]
        locations = list(area[1:1 + count])
        align = count + 1
        align += align & 1          # the slot block is 2-byte aligned
        slot_block = area[align:]
        slot_count, body = slot_block[1], slot_block[2:]
        for slot_index in range(slot_count):
            (species, form, alpha, level_min, level_max, gender,
             flawless) = struct.unpack_from("<HBBBBBB", body, slot_index * 8)
            # PLA slots are the ONE source that spells "no gender restriction" as 2, because
            # they store PKHeX's `Gender` enum, where `Random = Genderless = 2` -- and
            # EncounterSlot8a checks `Gender is not Gender.Random`. Every other blob uses
            # FixedGenderUtil.GenderRandom (0xFF) and means genderless by 2. Reading 2 as
            # genderless here would put a gender lock on 7130 of the 7132 PLA slots.
            if gender == 2:
                gender = GENDER_ANY
            flags = F_ALPHA if alpha else 0
            for location in locations:
                out.append(row(species, location, form, level_min, level_max, 1, "wild", gender=gender,
                               flawless=flawless, flags=flags))
    return out


def wild_sv():
    out = []
    for area in split_blobs_uint32(_load(WILD("Gen9", "encounter_wild_paldea.pkl"))):
        location = area[0] if area[2] == 0 else area[2]   # CrossFrom overrides the nominal area
        body = area[4:]
        for offset in range(0, len(body) - 7, 8):
            species, form, gender, level_min, level_max = struct.unpack_from("<HBBBB", body, offset)
            out.append(row(species, location, wild_form(form), level_min, level_max, BOTH, "wild", gender=gender))

    # Mass outbreaks: one record, a base location and a 120-bit mask of offsets from it.
    data = _load(WILD("Gen9", "encounter_outbreak_paldea.pkl"))
    size = 0x1C
    for offset in range(0, len(data) - size + 1, size):
        record = data[offset:offset + size]
        species, form, gender, level_min, level_max = struct.unpack_from("<HBBBB", record, 0)
        shiny = SHINY["always"] if record[0x0B] else SHINY["random"]
        base = record[0x0C]
        mask = int.from_bytes(record[0x0C:0x1C], "little") >> 8
        for bit in range(128):
            if (mask >> bit) & 1:
                out.append(row(species, base + bit, wild_form(form), level_min, level_max, BOTH, "wild",
                               gender=gender, shiny=shiny))
    return out


def wild_za():
    out = []
    for file_name in ("encounter_za.pkl", "encounter_hyperspace_za.pkl"):
        for area in split_blobs_uint16(_load(WILD("Gen9", file_name))):
            location = struct.unpack_from("<H", area, 0)[0]
            body = area[4:]
            for offset in range(0, len(body) - 7, 8):
                species, form, gender, level_min, level_max, alpha, shiny = struct.unpack_from("<HBBBBBB", body, offset)
                out.append(row(species, location, wild_form(form), level_min, level_max, 1, "wild", gender=gender,
                               shiny=shiny, flawless=3 if alpha else 0,
                               flags=F_ALPHA if alpha else 0))
    return out


# ---------------------------------------------------------------------------
# Statics / gifts / trades, parsed from the C# tables
# ---------------------------------------------------------------------------
def named_static(entries, versions, kind, default_form=0, location_override=None,
                 roamer_sentinels=None):
    """Rows for a `new(Version) { Species = .., Level = .., Location = .. }` table.

    `roamer_sentinels` maps a roamer's template Location onto the sentinel standing for the route
    set it actually wanders (see ENCOUNTER_LOCATION_ROAMER4_*). Without it a roamer would be
    pinned to the one route its template names, and every roamer ever caught would read illegal.
    """
    out = []
    for args, properties in entries:
        version_bits = versions(args)
        if version_bits == 0:
            continue  # recognised, and belongs to no game in this group
        species = csharp_value(properties["Species"])
        level = csharp_value(properties["Level"])
        level_max = csharp_value(properties.get("LevelMax", str(level)))
        # Location is OPTIONAL in the C# record, and a gift EGG is where it is left out -- Sun/Moon
        # has one whose only placement is its EggLocation, because a gift egg is met wherever the
        # player happens to hatch it. Reading it as required crashed the generator on that one entry.
        location = location_override if location_override is not None \
            else csharp_value(properties.get("Location", "0"))
        if roamer_sentinels and csharp_value(properties.get("IsRoaming", "0")):
            location = roamer_sentinels.get(location, location)
        egg_location = csharp_value(properties.get("EggLocation", "0"))
        row_kind = "gift" if (kind == "static" and csharp_value(properties.get("FixedBall", "0"))) else kind
        flags = flags_of(properties, F_EGG if egg_location not in (0, BDSP_NONE) else 0)
        if location == BDSP_NONE and egg_location not in (0, BDSP_NONE):
            # THIS ASKED hatch_level("BDSP") AND GOT A LEVEL. Same trap the EGG_RULES note describes,
            # in a new disguise: the reader whose name is closest is not the reader that is meant, and
            # BDSP's happened to answer 1, a plausible location id. Nothing downstream reads an egg
            # row's location -- the sweep skips egg-flagged rows and matchesGiftEgg keys on
            # eggLocation -- so it was dead data either way, which is exactly why it survived.
            location = hatch_location("BDSP")    # the nursery's own map; a gift egg is met on hatching
        out.append(row(species, location, csharp_value(properties.get("Form", str(default_form))), level, level_max,
                       version_bits, row_kind, shiny=shiny_of(properties), gender=gender_of(properties),
                       ball=csharp_value(properties.get("FixedBall", "0")),
                       flawless=csharp_value(properties.get("FlawlessIVCount", "0")),
                       nature=nature_of(properties), flags=flags, egg_location=egg_location))
    return out


def version_mask(names, group):
    """Map C# GameVersion tokens (SW / SH / SWSH / ...) to this group's bit mask.

    A token naming the GROUP rather than one game means every game in it, and so does a token
    this group does not recognise -- an unmatched name falls through to the whole group, which
    keeps a template rather than silently dropping it.

    Returns 0 only when every token was RECOGNISED and none of them names a game in this group;
    the caller drops such a row. That is a different answer from "no idea", which is why the two
    cannot both be spelled 0.
    """
    mask = 0
    recognised = False
    for name in names:
        one = version_token_mask(name, group)
        if one is not None:
            mask |= one
            recognised = True
    return mask if recognised else all_versions(group)


def version_token_mask(name, group):
    """The mask ONE C# token means for `group`, or None when it does not name a game at all."""
    name = name.strip().split(".")[-1]
    aliases = CSHARP_VERSION_ALIASES.get(group, {})
    if name in aliases:
        return aliases[name]
    versions = GROUP_VERSIONS[group]
    if name in versions:
        return 1 << versions.index(name)
    if name == GROUP_ENUM[group] or name == group:
        return all_versions(group)
    return None


def version_from_args(args, group):
    """The version mask from whichever positional argument actually names a game.

    Most tables put it third, but PKHeX's B2W2 Join Avenue trades take only an OT-name array and
    the version, so there it sits second. Scanning for the argument that IS a version token is the
    only reading that works for both without hardcoding a position per array -- and it cannot
    mistake a PID for a game, because a PID is not one of this group's version names. An entry with
    no version token at all belongs to the whole group, which is what the templates carrying only a
    PID (N's Pokemon) mean."""
    for arg in args:
        one = version_token_mask(arg, group)
        if one is not None:
            return one
    return all_versions(group)


def static_frlg():
    source_path = "Legality/Encounters/Data/Gen3/Encounters3FRLG.cs"
    text = strip_comments(read_cs(source_path))
    assert_all_arrays_used(source_path, ["StaticFRLG", "StaticFR", "StaticLG",
                                 "TradeGift_FRLG", "TradeGift_FR", "TradeGift_LG"])
    out = []
    for name in ("StaticFRLG", "StaticFR", "StaticLG"):
        for args, properties in parse_array(text, name):
            species, level = csharp_value(args[0]), csharp_value(args[1])
            version_bits = version_mask([args[2]], "FRLG")
            is_egg = csharp_value(properties.get("IsEgg", "0"))
            ball = csharp_value(properties.get("FixedBall", "0"))
            out.append(row(species, csharp_value(properties["Location"]),
                           csharp_value(properties.get("Form", "0")), level, level,
                           version_bits, "gift" if ball else "static", ball=ball,
                           flags=flags_of(properties, F_EGG if is_egg else 0)))
    # Trades: EncounterTrade3(names, index, version, pid, species, level).
    #
    # A Gen 3 in-game trade hands the Pokemon over at the LEVEL OF THE ONE YOU GAVE, so
    # its met level is not fixed -- the ctor's `level` is only the floor, being the lowest
    # level the requested species can be obtained at. PKHeX states exactly this as
    # `LevelMin => Level; LevelMax => 100`, and its own annotations corroborate the rule:
    # "Abra (Level 5 Breeding)" for the Mr. Mime trade (bred Gen 3 mons hatch at 5),
    # "Spearow (Level 3 Capture)" for Farfetch'd (the lowest wild Spearow in Kanto).
    # A real FireRed save has that Mr. Mime met at level 8 and that Farfetch'd at 13.
    #
    # EncounterTrade3 is the ONLY template PKSE reads whose LevelMax is not its Level --
    # everything else, statics and every other generation's trades, is a single level.
    for name in ("TradeGift_FRLG", "TradeGift_FR", "TradeGift_LG"):
        for args, properties in parse_array(text, name):
            version_bits = version_mask([args[2]], "FRLG")
            out.append(row(csharp_value(args[4]), LINK_TRADE_NPC_3, 0, csharp_value(args[5]), 100,
                           version_bits, "trade", gender=gender_of(properties)))
    return out


def static_rse():
    """Ruby / Sapphire / Emerald statics, gifts, trades and the Colosseum bonus gifts.

    The same EncounterStatic3 / EncounterTrade3 shapes FireRed uses -- one Gen 3 entity format,
    one set of templates -- so this reads like static_frlg() with a different file and the extra
    roamer handling Hoenn needs (Kanto has no roaming legendary).
    """
    source_path = "Legality/Encounters/Data/Gen3/Encounters3RSE.cs"
    text = strip_comments(read_cs(source_path))
    assert_all_arrays_used(source_path, ["StaticRSE", "StaticR", "StaticS", "StaticE",
                                 "TradeGift_RS", "TradeGift_E", "ColoGiftsR", "ColoGiftsS"])
    out = []
    for name in ("StaticRSE", "StaticR", "StaticS", "StaticE"):
        for args, properties in parse_array(text, name):
            species, level = csharp_value(args[0]), csharp_value(args[1])
            version_bits = version_mask([args[2]], "RSE")
            if version_bits == 0:
                continue
            is_egg = csharp_value(properties.get("IsEgg", "0"))
            ball = csharp_value(properties.get("FixedBall", "0"))
            location = csharp_value(properties["Location"])
            if csharp_value(properties.get("IsRoaming", "0")):
                location = ENCOUNTER_LOCATION_ROAMER3
            out.append(row(species, location, csharp_value(properties.get("Form", "0")),
                           level, level, version_bits, "gift" if ball else "static", ball=ball,
                           flags=flags_of(properties, F_EGG if is_egg else 0)))
    # Pokemon Colosseum's bonus-disc gifts are stamped with Ruby or Sapphire as their ORIGIN,
    # not with Colosseum's own version, so they belong in this table rather than nowhere.
    # EncounterGift3Colo(species, level, trainerNames, version).
    for name in ("ColoGiftsR", "ColoGiftsS"):
        for args, properties in parse_array(text, name):
            version_bits = version_mask([args[3]], "RSE")
            if version_bits == 0:
                continue
            out.append(row(csharp_value(args[0]), csharp_value(properties["Location"]), 0,
                           csharp_value(args[1]), csharp_value(args[1]), version_bits, "gift",
                           flags=flags_of(properties)))
    # Trades: see the note in static_frlg -- a Gen 3 trade hands the Pokemon over at the level of
    # the one you gave, so its met level runs from the template's floor to 100.
    for name in ("TradeGift_RS", "TradeGift_E"):
        for args, properties in parse_array(text, name):
            version_bits = version_mask([args[2]], "RSE")
            if version_bits == 0:
                continue
            out.append(row(csharp_value(args[4]), LINK_TRADE_NPC_3, 0, csharp_value(args[5]), 100,
                           version_bits, "trade", gender=gender_of(properties)))
    return out


def static_dppt(group):
    """Diamond/Pearl and Platinum statics, gifts, Ranch gifts and in-game trades.

    ONE PKHeX FILE, TWO PKSE GROUPS. Diamond/Pearl and Platinum are different save layouts and so
    different groups here, but PKHeX keeps them together and tags each template DPPt / DP / D / P
    / Pt. version_mask resolves which of those name a game in `group` and returns 0 for the rest,
    which is what stops Platinum's table inheriting Dialga.
    """
    source_path = "Legality/Encounters/Data/Gen4/Encounters4DPPt.cs"
    text = strip_comments(read_cs(source_path))
    assert_all_arrays_used(source_path, ["StaticDPPt", "StaticDP", "StaticPt", "StaticD", "StaticP",
                                 "RanchGifts", "TradeGift_DPPtIngame"])
    out = []
    for name in ("StaticDPPt", "StaticDP", "StaticPt", "StaticD", "StaticP"):
        out += named_static(parse_array(text, name),
                            lambda args: version_mask([args[0]], group), "static",
                            roamer_sentinels=ROAMER4_LOCATION_TO_SENTINEL)
    # My Pokemon Ranch gifts: Wii transfers stamped with Diamond as their origin, so Platinum
    # gets none. EncounterTrade4RanchGift(pid, species, metLevel, level) -- and a three-argument
    # form without the PID. The MET level is what a row's level span is compared against.
    if group == "DP":
        for args, properties in parse_array(text, "RanchGifts"):
            species, met_level = (csharp_value(args[1]), csharp_value(args[2])) if len(args) >= 4 \
                else (csharp_value(args[0]), csharp_value(args[1]))
            out.append(row(species, csharp_value(properties["Location"]), 0, met_level, met_level,
                           version_mask(["D"], group), "gift",
                           gender=gender_of(properties), flags=flags_of(properties)))
    out += trades_gen4(text, "TradeGift_DPPtIngame", group)
    return out


def static_dp():
    return static_dppt("DP")


def static_pt():
    return static_dppt("PT")


def static_hgss():
    source_path = "Legality/Encounters/Data/Gen4/Encounters4HGSS.cs"
    text = strip_comments(read_cs(source_path))
    assert_all_arrays_used(source_path, ["Encounter_HGSS", "StaticHG", "StaticSS", "TradeGift_HGSS"])
    out = []
    for name in ("Encounter_HGSS", "StaticHG", "StaticSS"):
        out += named_static(parse_array(text, name),
                            lambda args: version_mask([args[0]], "HGSS"), "static",
                            roamer_sentinels=ROAMER4_LOCATION_TO_SENTINEL)
    out += trades_gen4(text, "TradeGift_HGSS", "HGSS")
    return out


def trades_gen4(text, array_name, group):
    """EncounterTrade4PID(names, index, version, pid, species, level).

    A Gen 4 in-game trade is met at the trade NPC marker unless the template names a real
    MetLocation, and its met level is fixed only in that second case -- otherwise the Pokemon
    arrives at whatever level the one you handed over was, exactly as in Gen 3 (PKHeX
    EncounterTrade4PID: `LevelMax => IsMetUnset ? Level : 100`, read the other way round).
    """
    out = []
    for args, properties in parse_array(text, array_name):
        version_bits = version_mask([args[2]], group)
        if version_bits == 0:
            continue
        level = csharp_value(args[5])
        met_location = properties.get("MetLocation")
        if met_location is not None:
            out.append(row(csharp_value(args[4]), csharp_value(met_location), 0, level, level,
                           version_bits, "trade", gender=gender_of(properties)))
        else:
            out.append(row(csharp_value(args[4]), LINK_TRADE_NPC_4, 0, level, 100,
                           version_bits, "trade", gender=gender_of(properties)))
    return out


def trades_named(text, array_names, group, trade_location):
    """`new(names, index, version, ...) { Species = .., Level = .., .. }` trade tables.

    Gens 5, 6 and 7 all shape their in-game trades this way: the version is the third positional
    argument and everything the row needs is a named property. The met location is the trade NPC
    marker, which is a property of the GENERATION rather than of the template -- PKHeX spells it
    EncounterTrade5BW.Location => LinkTrade5NPC, and Gen 6/7 => LinkTrade6NPC.
    """
    out = []
    for array_name in array_names:
        for args, properties in parse_array(text, array_name):
            version_bits = version_from_args(args, group)
            if version_bits == 0:
                continue
            level = csharp_value(properties["Level"])
            out.append(row(csharp_value(properties["Species"]), trade_location,
                           csharp_value(properties.get("Form", "0")), level, level,
                           version_bits, "trade", gender=gender_of(properties),
                           nature=nature_of(properties)))
    return out


def static_bw():
    source_path = "Legality/Encounters/Data/Gen5/Encounters5BW.cs"
    text = strip_comments(read_cs(source_path))
    assert_all_arrays_used(source_path, ["Encounter_BW", "StaticB", "StaticW",
                                 "TradeGift_BW", "TradeGift_B", "TradeGift_W"])
    out = []
    for name in ("Encounter_BW", "StaticB", "StaticW"):
        out += named_static(parse_array(text, name),
                            lambda args: version_from_args(args, "BW"), "static",
                            roamer_sentinels=ROAMER5_SENTINELS)
    out += trades_named(text, ("TradeGift_BW", "TradeGift_B", "TradeGift_W"), "BW", LINK_TRADE_NPC_5)
    return out


def static_b2w2():
    source_path = "Legality/Encounters/Data/Gen5/Encounters5B2W2.cs"
    text = strip_comments(read_cs(source_path))
    assert_all_arrays_used(source_path, ["Encounter_B2W2_Regular", "StaticB2", "StaticW2",
                                 "Encounter_B2W2_N", "TradeGift_B2W2", "TradeGift_B2", "TradeGift_W2"])
    out = []
    for name in ("Encounter_B2W2_Regular", "StaticB2", "StaticW2"):
        out += named_static(parse_array(text, name),
                            lambda args: version_from_args(args, "B2W2"), "static",
                            roamer_sentinels=ROAMER5_SENTINELS)
    # N'S POKEMON ARE VERSIONLESS IN THE SOURCE. Their only positional argument is a PID, not a
    # game, and PKHeX fixes Version => B2W2 on the template itself -- so asking version_mask for a
    # version here would read the PID as a game name.
    out += named_static(parse_array(text, "Encounter_B2W2_N"),
                        lambda args: version_from_args(args, "B2W2"), "static")
    out += trades_named(text, ("TradeGift_B2W2", "TradeGift_B2", "TradeGift_W2"), "B2W2", LINK_TRADE_NPC_5)
    # Dream Radar lives in its own file and is likewise B2W2-only (EncounterStatic5Radar.Version).
    # `new(species, form)`, met at 30015 with a level band rather than one level.
    radar_path = "Legality/Encounters/Data/Gen5/Encounters5DR.cs"
    radar_text = strip_comments(read_cs(radar_path))
    assert_all_arrays_used(radar_path, ["Encounter_DreamRadar"])
    for args, properties in parse_array(radar_text, "Encounter_DreamRadar"):
        out.append(row(csharp_value(args[0]), DREAM_RADAR_LOCATION, csharp_value(args[1]),
                       DREAM_RADAR_LEVEL_MIN, DREAM_RADAR_LEVEL_MAX, all_versions("B2W2"), "gift",
                       flags=flags_of(properties)))
    return out


def static_xy():
    source_path = "Legality/Encounters/Data/Gen6/Encounters6XY.cs"
    text = strip_comments(read_cs(source_path))
    assert_all_arrays_used(source_path, ["Encounter_XY", "StaticX", "StaticY", "TradeGift_XY"])
    out = []
    for name in ("Encounter_XY", "StaticX", "StaticY"):
        out += named_static(parse_array(text, name),
                            lambda args: version_from_args(args, "XY"), "static")
    out += trades_named(text, ("TradeGift_XY",), "XY", LINK_TRADE_NPC_6)
    return out


def static_oras():
    source_path = "Legality/Encounters/Data/Gen6/Encounters6AO.cs"
    text = strip_comments(read_cs(source_path))
    assert_all_arrays_used(source_path, ["Encounter_AO_Regular", "StaticA", "StaticO", "TradeGift_AO",
                                         "BaseCosplay"])
    out = []
    # ORAS is the one table built on a C# record copy: six Cosplay Pikachu forms written as
    # `BaseCosplay with {Form = N}`. Supplying the base makes them ordinary entries; the count is
    # asserted because a silently-dropped one would simply read as six illegal Pikachu.
    cosplay_bases = {"BaseCosplay": parse_template(text, "BaseCosplay")}
    cosplay_rows = 0
    for name in ("Encounter_AO_Regular", "StaticA", "StaticO"):
        entries = parse_array(text, name, bases=cosplay_bases)
        cosplay_rows += sum(1 for _, properties in entries
                            if csharp_value(properties.get("Species", "0")) == 25)
        out += named_static(entries, lambda args: version_from_args(args, "ORAS"), "static")
    if cosplay_rows != 6:
        raise SystemExit("ORAS: expected 6 Cosplay Pikachu templates, parsed %d" % cosplay_rows)
    out += trades_named(text, ("TradeGift_AO",), "ORAS", LINK_TRADE_NPC_6)
    return out


def static_sm():
    source_path = "Legality/Encounters/Data/Gen7/Encounters7SM.cs"
    text = strip_comments(read_cs(source_path))
    assert_all_arrays_used(source_path, ["StaticSM", "StaticSN", "StaticMN", "TradeGift_SM"])
    out = []
    for name in ("StaticSM", "StaticSN", "StaticMN"):
        out += named_static(parse_array(text, name),
                            lambda args: version_from_args(args, "SM"), "static")
    out += trades_named(text, ("TradeGift_SM",), "SM", LINK_TRADE_NPC_6)
    return out


def static_usum():
    source_path = "Legality/Encounters/Data/Gen7/Encounters7USUM.cs"
    text = strip_comments(read_cs(source_path))
    assert_all_arrays_used(source_path, ["StaticUSUM", "StaticUS", "StaticUM", "TradeGift_USUM"])
    out = []
    for name in ("StaticUSUM", "StaticUS", "StaticUM"):
        out += named_static(parse_array(text, name),
                            lambda args: version_from_args(args, "USUM"), "static")
    out += trades_named(text, ("TradeGift_USUM",), "USUM", LINK_TRADE_NPC_6)
    return out


def static_gg():
    source_path = "Legality/Encounters/Data/Gen7/Encounters7GG.cs"
    text = strip_comments(read_cs(source_path))
    assert_all_arrays_used(source_path, ["Encounter_GG", "StaticGP", "StaticGE",
                                 "TradeGift_GG", "TradeGift_GP", "TradeGift_GE"])
    out = []
    for name in ("Encounter_GG", "StaticGP", "StaticGE"):
        out += named_static(parse_array(text, name),
                            lambda args: version_mask([args[0]], "GG"), "static")
    for name in ("TradeGift_GG", "TradeGift_GP", "TradeGift_GE"):
        for args, properties in parse_array(text, name):
            level = csharp_value(properties["Level"])
            out.append(row(csharp_value(properties["Species"]), LINK_TRADE_NPC_6,
                           csharp_value(properties.get("Form", "0")), level, level,
                           version_mask([args[0]], "GG"), "trade",
                           gender=gender_of(properties)))
    return out


def static_swsh():
    source_path = "Legality/Encounters/Data/Gen8/Encounters8.cs"
    text = strip_comments(read_cs(source_path))
    assert_all_arrays_used(source_path, ["StaticSWSH", "StaticSW", "StaticSH",
                                 "TradeSWSH", "TradeSW", "TradeSH"])
    out = []
    for name, version_bits in (("StaticSWSH", BOTH), ("StaticSW", 1), ("StaticSH", 2)):
        out += named_static(parse_array(text, name), lambda args, version_bits=version_bits: version_bits, "static")
    # Trades: the two overloads differ only in whether arg 0 is the shared name table.
    for name in ("TradeSWSH", "TradeSW", "TradeSH"):
        for args, properties in parse_array(text, name):
            arg_shift = 0 if args[0] == "TradeOT_R1" else 1
            version_bits = version_mask([args[1 + arg_shift]], "SWSH")
            species, level = csharp_value(args[2 + arg_shift]), csharp_value(args[3 + arg_shift])
            out.append(row(species, LINK_TRADE_NPC_6, csharp_value(properties.get("Form", "0")), level, level,
                           version_bits, "trade", gender=gender_of(properties), nature=nature_of(properties),
                           flawless=csharp_value(properties.get("FlawlessIVCount", "0"))))
    return out + raids_swsh()


# EncounterStatic8N.LevelCaps, indexed [rank*2] = min, [rank*2+1] = max.
NEST_LEVEL_CAPS = [15, 20, 25, 30, 35, 40, 45, 50, 55, 60]
SHARED_NEST_MIN_LEVEL = 20
DIST_INDEX_MIN_DLC1 = 25
DIST_INDEX_MIN_DLC2 = 40


def _nest_locations(text):
    switch_match = re.search(r"GetNestLocations\(byte nestIndex\)\s*=>\s*nestIndex switch\s*\{", text)
    body = text[switch_match.end():_match(text, text.index("{", switch_match.end() - 1), '{', '}') - 1]
    out = {}
    for nest_index, locations in re.findall(r"(\d+)\s*=>\s*\[([^\]]*)\]", body):
        out[int(nest_index)] = [int(token) for token in re.findall(r"\d+", locations)]
    return out


def raids_swsh():
    source_path = "Legality/Encounters/Data/Gen8/Encounters8Nest.cs"
    text = strip_comments(read_cs(source_path))
    assert_all_arrays_used(source_path, ["Crystal_SWSH"])
    nests = _nest_locations(text)
    out = []

    # Regular raid dens: 10-byte records, level range from the den's star ranks.
    for file_name, version_bits in (("encounter_sw_nest.pkl", 1), ("encounter_sh_nest.pkl", 2)):
        data = _load(WILD("Gen8", file_name))
        for offset in range(0, len(data) - 9, 10):
            record = data[offset:offset + 10]
            species, form, gender = struct.unpack_from("<HBB", record, 0)
            nest, min_rank, max_rank, flawless = record[6], record[7], record[8], record[9]
            level_min = NEST_LEVEL_CAPS[min_rank * 2]
            level_max = NEST_LEVEL_CAPS[max_rank * 2 + 1]
            for location in nests.get(nest, []):
                out.append(row(species, location, form, level_min, level_max, version_bits, "raid",
                               gender=gender, flawless=flawless))
            # A den entered from someone else's raid reports the shared-nest location
            # and may be down-levelled to 20.
            out.append(row(species, SHARED_NEST_8, form, min(level_min, SHARED_NEST_MIN_LEVEL), level_max,
                           version_bits, "raid", gender=gender, flawless=flawless))

    # Distribution raids: the den is anonymous, so any Wild Area of the right tier.
    for file_name, version_bits in (("encounter_sw_dist.pkl", 1), ("encounter_sh_dist.pkl", 2)):
        data = _load(WILD("Gen8", file_name))
        for offset in range(0, len(data) - 15, 16):
            record = data[offset:offset + 16]
            species, form = struct.unpack_from("<HB", record, 0)[0], record[2]
            level, index = record[12], record[15]
            flags_byte = record[14]
            flawless = flags_byte & 0xF
            shiny = {1: SHINY["never"], 2: SHINY["always"]}.get(flags_byte >> 4, SHINY["random"])
            region = (ENCOUNTER_LOCATION_SWSH_WILDAREA_ALL if index >= DIST_INDEX_MIN_DLC2 else
                      ENCOUNTER_LOCATION_SWSH_WILDAREA_ISLE_OF_ARMOR if index >= DIST_INDEX_MIN_DLC1 else
                      ENCOUNTER_LOCATION_SWSH_WILDAREA)
            level_min = min(level, SHARED_NEST_MIN_LEVEL)
            for location in (region, SHARED_NEST_8):
                out.append(row(species, location, form, level_min, level, version_bits, "raid",
                               shiny=shiny, flawless=flawless))

    # Dynamax Adventures (Max Lair): fixed location, fixed level 70.
    data = _load(WILD("Gen8", "encounter_swsh_underground.pkl"))
    for offset in range(0, len(data) - 13, 14):
        record = data[offset:offset + 14]
        species = struct.unpack_from("<H", record, 0)[0]
        out.append(row(species, MAX_LAIR_8, record[2], 70, 70, BOTH, "raid",
                       shiny=SHINY["never"], flawless=4))

    # Event "crystal" raids are a plain C# list, all met at the shared nest.
    for args, properties in parse_array(text, "Crystal_SWSH"):
        level = csharp_value(properties["Level"])
        out.append(row(csharp_value(properties["Species"]), SHARED_NEST_8, csharp_value(properties.get("Form", "0")),
                       min(level, SHARED_NEST_MIN_LEVEL), level,
                       version_mask([args[0]], "SWSH"), "raid",
                       flawless=csharp_value(properties.get("FlawlessIVCount", "0"))))
    return out


def static_bdsp():
    source_path = "Legality/Encounters/Data/Gen8/Encounters8b.cs"
    text = strip_comments(read_cs(source_path))
    assert_all_arrays_used(source_path, ["Encounter_BDSP", "StaticBD", "StaticSP", "TradeGift_BDSP"])
    out = []
    for name in ("Encounter_BDSP", "StaticBD", "StaticSP"):
        out += named_static(parse_array(text, name),
                            lambda args: version_mask([args[0]], "BDSP"), "static")
    for args, properties in parse_array(text, "TradeGift_BDSP"):
        level = csharp_value(properties["Level"])
        out.append(row(csharp_value(properties["Species"]), LINK_TRADE_NPC_6, 0, level, level,
                       version_mask([args[2]], "BDSP"), "trade",
                       gender=gender_of(properties), nature=nature_of(properties)))
    return out


def static_pla():
    source_path = "Legality/Encounters/Data/Gen8/Encounters8a.cs"
    text = strip_comments(read_cs(source_path))
    assert_all_arrays_used(source_path, ["StaticLA"])
    out = []
    for args, properties in parse_array(text, "StaticLA"):
        species, form, level = csharp_value(args[0]), csharp_value(args[1]), csharp_value(args[2])
        level_max = csharp_value(properties.get("LevelMax", str(level)))
        ball = csharp_value(properties.get("FixedBall", "0"))
        out.append(row(species, csharp_value(properties["Location"]), form, level, level_max, 1,
                       "gift" if ball else "static", shiny=shiny_of(properties),
                       gender=gender_of(properties), ball=ball,
                       flawless=csharp_value(properties.get("FlawlessIVCount", "0")),
                       flags=flags_of(properties)))
    return out


def static_sv():
    source_path = "Legality/Encounters/Data/Gen9/Encounters9.cs"
    text = strip_comments(read_cs(source_path))
    assert_all_arrays_used(source_path, ["Encounter_SV", "StaticSL", "StaticVL", "TradeGift_SV"])
    out = []
    # Scarlet and Violet each add version-exclusive statics on top of the shared list.
    for name in ("Encounter_SV", "StaticSL", "StaticVL"):
        out += named_static(parse_array(text, name),
                            lambda args: version_mask([args[0]], "SV"), "static")
    for args, properties in parse_array(text, "TradeGift_SV"):
        level = csharp_value(args[4])
        out.append(row(csharp_value(args[3]), LINK_TRADE_NPC_6, csharp_value(properties.get("Form", "0")), level, level,
                       version_mask([args[2]], "SV"), "trade", shiny=shiny_of(properties),
                       gender=gender_of(properties), nature=nature_of(properties),
                       ball=csharp_value(properties.get("FixedBall", "0"))))
    return out + raids_sv()


def raids_sv():
    """Tera raids (base + both DLC maps), distribution, Mightiest, and fixed spawns."""
    out = []

    def gem_row(record, size):
        species, form = struct.unpack_from("<H", record, 0)[0], record[2]
        gender = record[3] - 1                      # stored +1 so 0 can mean "any"
        flawless, level = record[5], record[7]
        shiny = {1: SHINY["never"], 2: SHINY["always"]}.get(record[6], SHINY["random"])
        return row(species, TERA_CAVERN_9, wild_form(form), level, level, BOTH, "raid",
                   shiny=shiny, gender=gender, flawless=flawless)

    for file_name, size in (("encounter_gem_paldea.pkl", 0x18),
                        ("encounter_gem_kitakami.pkl", 0x18),
                        ("encounter_gem_blueberry.pkl", 0x18),
                        ("encounter_dist_paldea.pkl", 0x14 + (2 * 2 * 4 * 2) + 10),
                        ("encounter_might_paldea.pkl", 0x14 + (2 * 2 * 4 * 2) + 10)):
        data = _load(WILD("Gen9", file_name))
        for offset in range(0, len(data) - size + 1, size):
            out.append(gem_row(data[offset:offset + size], size))

    # Fixed spawns: up to four met locations per record, 0 = slot unused.
    data = _load(WILD("Gen9", "encounter_fixed_paldea.pkl"))
    for offset in range(0, len(data) - 0x13, 0x14):
        record = data[offset:offset + 0x14]
        species, form, level, flawless = struct.unpack_from("<HBBB", record, 0)
        gender = record[6]
        for location in (record[0x10], record[0x11], record[0x12], record[0x13]):
            if location:
                out.append(row(species, location, form, level, level, BOTH, "static",
                               gender=GENDER_ANY if gender > 2 else gender,
                               flawless=flawless))
    return out


def static_za():
    source_path = "Legality/Encounters/Data/Gen9/Encounters9a.cs"
    text = strip_comments(read_cs(source_path))
    assert_all_arrays_used(source_path, ["Gifts", "Static", "Trades"])
    out = []
    for name, kind in (("Gifts", "gift"), ("Static", "static")):
        for args, properties in parse_array(text, name):
            species, form, level = csharp_value(args[0]), csharp_value(args[1]), csharp_value(args[2])
            # Z-A gifts are always handed over in a Poke Ball (EncounterGift9a.FixedBall).
            ball = BALL["Poke"] if kind == "gift" else 0
            # Both Z-A tables default to Shiny.Never unless the entry says otherwise.
            out.append(row(species, csharp_value(properties["Location"]), form, level, level, 1, kind,
                           shiny=shiny_of(properties) if "Shiny" in properties else SHINY["never"],
                           gender=gender_of(properties), ball=ball,
                           flawless=csharp_value(properties.get("FlawlessIVCount", "0")),
                           nature=nature_of(properties), flags=flags_of(properties)))
    for args, properties in parse_array(text, "Trades"):
        species, form, level = csharp_value(args[2]), csharp_value(args[3]), csharp_value(args[4])
        out.append(row(species, LINK_TRADE_NPC_6, form, level, level, 1, "trade",
                       gender=gender_of(properties), nature=nature_of(properties),
                       flawless=csharp_value(properties.get("FlawlessIVCount", "0"))))
    return out


WILD_SOURCES = {"FRLG": wild_frlg, "GG": wild_gg, "SWSH": wild_swsh, "BDSP": wild_bdsp,
                "PLA": wild_pla, "SV": wild_sv, "ZA": wild_za,
                "RSE": wild_rse, "DP": wild_dp, "PT": wild_pt, "HGSS": wild_hgss,
                "BW": wild_bw, "B2W2": wild_b2w2, "XY": wild_xy, "ORAS": wild_oras,
                "SM": wild_sm, "USUM": wild_usum, "RBY": wild_rby, "GSC": wild_gsc}
STATIC_SOURCES = {"FRLG": static_frlg, "GG": static_gg, "SWSH": static_swsh,
                  "BDSP": static_bdsp, "PLA": static_pla, "SV": static_sv,
                  "ZA": static_za,
                  "RSE": static_rse, "DP": static_dp, "PT": static_pt, "HGSS": static_hgss,
                  "BW": static_bw, "B2W2": static_b2w2, "XY": static_xy, "ORAS": static_oras,
                  "SM": static_sm, "USUM": static_usum, "RBY": static_rby, "GSC": static_gsc}


# ---------------------------------------------------------------------------
# Mystery Gift distributions (the EVENT half of Layer 3)
# ---------------------------------------------------------------------------
# Every officially distributed Pokemon is a Mystery Gift card, and PKHeX ships the
# whole database as flat arrays of fixed-size records -- one .pkl per format, length
# an exact multiple of the record size. That is all the structure there is, so these
# need no container decoding: seek to the offsets and read.
#
# They become ORDINARY EncounterRows with kind "event". The alternative -- a second
# table with its own matcher -- would have duplicated the species index, the
# pre-evolution walk and every constraint check, and the two would have drifted. A
# distribution IS an encounter template; it differs only in where it came from.
#
# Offsets are transcribed from the PKHeX classes named per format below. They are
# read straight out of the record and are the single thing that would silently
# produce a table of plausible-looking nonsense if they were wrong -- which is why
# the parser VALIDATES every record it keeps (species in range, level 1-100) and
# fails loudly on a file whose length is not a whole number of records.
#
# Gen 3 (FireRed/LeafGreen) is deliberately absent: PKHeX has no mgdb pickle for it.
# Its distributions live in hand-written arrays of a different shape, so a Gen 3
# fateful Pokemon still takes the softened "event Pokemon are not checked" path.

def MGDB(name):
    return pkhex_path("Resources/legality/mgdb/" + name)


def pgf_versions_from_tail(game, tail_byte):
    """Which of `game`'s versions a Gen 5 distribution card can be received by.

    ONE DATABASE SERVES BOTH GEN 5 PAIRS, so without this Black/White's table would carry every
    Black 2/White 2 distribution and vice versa. The trailing byte's low nibble is PKHeX's
    RestrictVersion, one bit per Gen 5 game in GameVersion order -- White, Black, White 2, Black 2
    -- and zero means the card names no restriction at all and every Gen 5 game can take it.

    Which of a PAIR is not narrowed further: the nibble can say Black-only, but a template that is
    merely permissive is the direction this whole table is built in, and a distribution restricted
    to one half of a pair is not a distinction Layer 3 has ever drawn.
    """
    restricted_to = tail_byte & 0x0F
    belongs_to = {"BW": 0b0011, "B2W2": 0b1100}[game]
    if restricted_to == 0:
        return all_versions(game)   # no restriction recorded
    return all_versions(game) if (restricted_to & belongs_to) else 0


SHINY_TYPE_8 = {0: SHINY["never"], 1: SHINY["random"], 2: SHINY["always"],
                3: SHINY["always"], 4: SHINY["random"]}
# PKHeX ShinyType6, whose numbering is NOT ShinyType8's: 0 FixedValue, 1 Random, 2 Always, 3 Never.
SHINY_TYPE_6 = {0: SHINY["random"], 1: SHINY["random"], 2: SHINY["always"], 3: SHINY["never"]}
# PGF.Shiny: 0 is NEVER here and Random in Gen 6's enum. Reading a Gen 5 card through the Gen 6
# map would call every shiny-locked distribution shiny-random, which is a check that never fires.
SHINY_TYPE_5 = {0: SHINY["never"], 1: SHINY["random"], 2: SHINY["always"]}

# size, and offsets WITHIN one record. `card` is added to every other offset (only
# Let's Go's WB7 nests its card inside a larger wrapper). `shiny` maps the record's
# own shiny enum onto SHINY[]; `species9` says the species field is Gen 9 INTERNAL
# numbering and needs converting to National Dex.
EVENT_FORMATS = {
    # PKHeX MysteryGifts/WB7.cs -- Let's Go Pikachu/Eevee
    "GG": dict(files=(("wb7full.pkl", 0x310, 0x208),), species9=False, shiny6=True,
               pokemonType=0,
               cardId=0x00, cardType=0x51, ball=0x76, ballWide=False, species=0x82, form=0x84,
               pidType=0xA3, eggLocation=0xA4, location=0xA6, metLevel=0xA8,
               level=0xD0, isEgg=0xD1),
    # PKHeX MysteryGifts/WC8.cs -- Sword/Shield
    "SWSH": dict(files=(("wc8.pkl", 0x2D0, 0x00),), species9=False, shiny6=False,
                 pokemonType=1,
                 cardId=0x08, cardType=0x11, ball=0x22C, ballWide=True, species=0x240, form=0x242,
                 pidType=0x248, eggLocation=0x228, location=0x22A, metLevel=0x249,
                 level=0x244, isEgg=0x245),
    # PKHeX MysteryGifts/WB8.cs -- Brilliant Diamond/Shining Pearl
    "BDSP": dict(files=(("wb8.pkl", 0x2DC, 0x00),), species9=False, shiny6=False,
                 pokemonType=1,
                 cardId=0x08, cardType=0x11, ball=0x274, ballWide=True, species=0x288, form=0x28A,
                 pidType=0x290, eggLocation=0x270, location=0x272, metLevel=0x291,
                 level=0x28C, isEgg=0x28D),
    # PKHeX MysteryGifts/WA8.cs -- Legends: Arceus. Note the gift-type byte is at 0x0F
    # here and 0x11 everywhere else; reading the wrong one classifies items as Pokemon.
    "PLA": dict(files=(("wa8.pkl", 0x2C8, 0x00),), species9=False, shiny6=False,
                 pokemonType=1,
                cardId=0x08, cardType=0x0F, ball=0x224, ballWide=True, species=0x238, form=0x23A,
                pidType=0x240, eggLocation=0x220, location=0x222, metLevel=0x241,
                level=0x23C, isEgg=0x23D),
    # PKHeX MysteryGifts/WC9.cs -- Scarlet/Violet
    "SV": dict(files=(("wc9.pkl", 0x2C8, 0x00),), species9=True, shiny6=False,
                 pokemonType=1,
               cardId=0x08, cardType=0x11, ball=0x224, ballWide=True, species=0x238, form=0x23A,
               pidType=0x240, eggLocation=0x220, location=0x222, metLevel=0x241,
               level=0x23C, isEgg=0x23D),
    # PKHeX MysteryGifts/PGF.cs -- Black/White and Black 2/White 2, one database for both.
    # THE POKEMON CARD TYPE IS 1 HERE, not 0 as in WC6/WC7 and not 1-means-item as in WB7. Each
    # generation spells it its own way and reading the wrong value silently swaps items for
    # Pokemon. PGF also has no MetLevel of its own distinct from Level.
    "BW": dict(files=(("pgf.pkl", 0xCC, 0x00),), species9=False, shiny6=False,
               tailByte=True, versionsFromTail=pgf_versions_from_tail,
               shinyMap=SHINY_TYPE_5, pokemonType=1,
               cardId=0xB0, cardType=0xB3, ball=0x0E, ballWide=False, species=0x1A, form=0x1C,
               pidType=0x37, eggLocation=0x38, location=0x3A, metLevel=0x3C,
               level=0x5B, isEgg=0x5C),
    "B2W2": dict(files=(("pgf.pkl", 0xCC, 0x00),), species9=False, shiny6=False,
               tailByte=True, versionsFromTail=pgf_versions_from_tail,
                 shinyMap=SHINY_TYPE_5, pokemonType=1,
                 cardId=0xB0, cardType=0xB3, ball=0x0E, ballWide=False, species=0x1A, form=0x1C,
                 pidType=0x37, eggLocation=0x38, location=0x3A, metLevel=0x3C,
                 level=0x5B, isEgg=0x5C),
    # PKHeX MysteryGifts/WC6.cs + WC6Full.cs -- X/Y and Omega Ruby/Alpha Sapphire. The full
    # records are 0x310 with the card at the end (Size - WC6.Size), the bare ones are the card.
    # WC6 has no separate MetLevel: PKHeX matches pk.MetLevel against Level, so both read 0xD0.
    "XY": dict(files=(("wc6full.pkl", 0x310, 0x208), ("wc6.pkl", 0x108, 0x00)),
               species9=False, shiny6=True, pokemonType=0,
               cardId=0x00, cardType=0x51, ball=0x76, ballWide=False, species=0x82, form=0x84,
               pidType=0xA3, eggLocation=0xA4, location=0xA6, metLevel=0xD0,
               level=0xD0, isEgg=0xD1),
    "ORAS": dict(files=(("wc6full.pkl", 0x310, 0x208), ("wc6.pkl", 0x108, 0x00)),
                 species9=False, shiny6=True, pokemonType=0,
                 cardId=0x00, cardType=0x51, ball=0x76, ballWide=False, species=0x82, form=0x84,
                 pidType=0xA3, eggLocation=0xA4, location=0xA6, metLevel=0xD0,
                 level=0xD0, isEgg=0xD1),
    # PKHeX MysteryGifts/WC7.cs + WC7Full.cs -- Sun/Moon and Ultra Sun/Ultra Moon. Same shape as
    # WC6 except that Gen 7 did gain a real MetLevel, at 0xA8.
    "SM": dict(files=(("wc7full.pkl", 0x310, 0x208), ("wc7.pkl", 0x108, 0x00)),
               species9=False, shiny6=True, pokemonType=0,
               cardId=0x00, cardType=0x51, ball=0x76, ballWide=False, species=0x82, form=0x84,
               pidType=0xA3, eggLocation=0xA4, location=0xA6, metLevel=0xA8,
               level=0xD0, isEgg=0xD1),
    "USUM": dict(files=(("wc7full.pkl", 0x310, 0x208), ("wc7.pkl", 0x108, 0x00)),
                 species9=False, shiny6=True, pokemonType=0,
                 cardId=0x00, cardType=0x51, ball=0x76, ballWide=False, species=0x82, form=0x84,
                 pidType=0xA3, eggLocation=0xA4, location=0xA6, metLevel=0xA8,
                 level=0xD0, isEgg=0xD1),
    # PKHeX MysteryGifts/WA9.cs -- Legends: Z-A
    "ZA": dict(files=(("wa9.pkl", 0x2C8, 0x00),), species9=True, shiny6=False,
                 pokemonType=1,
               cardId=0x08, cardType=0x11, ball=0x25C, ballWide=True, species=0x270, form=0x272,
               pidType=0x278, eggLocation=0x258, location=0x25A, metLevel=0x279,
               level=0x274, isEgg=0x275),
}

# What the gift-type byte holds for a POKEMON card. Every Gen 8/9 format uses PKHeX's
# GiftType enum, where Pokemon is 1 and Item is 2 -- but WB7 predates that enum and
# spells the same field the other way round: PKHeX's WB7.IsEntity is `CardType == 0`
# and its IsItem is `CardType == 1`. Assuming one convention for all of them silently
# kept every Let's Go ITEM card and threw away all fifteen of its Pokemon.

# PKHeX ShinyType8: 0 Never, 1 Random, 2 AlwaysStar, 3 AlwaysSquare, 4 FixedValue.
# A FixedValue card carries a literal PID, so whether it comes out shiny depends on the
# trainer id -- "random" is the only honest answer without recomputing it.


def _gen9_internal_to_national():
    """PKHeX SpeciesConverter.Table9InternalToNational -- a signed delta per index."""
    text = strip_comments(read_cs("PKM/Util/Conversion/SpeciesConverter.cs"))
    table_match = re.search(r"Table9InternalToNational\s*=>\s*\[(.*?)\]", text, re.S)
    if not table_match:
        raise SystemExit("SpeciesConverter.Table9InternalToNational not found")
    # Base 10 explicitly: PKHeX pads these to three columns ("065", "-07"), and a leading
    # zero is an OCTAL prefix to int(v, 0) -- so "010" would silently become 8.
    deltas = [int(token, 10) for token in re.findall(r"-?\d+", table_match.group(1))]
    first = re.search(r"FirstUnalignedInternal9\s*=\s*(\w+)", text)
    if not first:
        raise SystemExit("SpeciesConverter.FirstUnalignedInternal9 not found")
    base = 917  # FirstUnalignedNational9, which FirstUnalignedInternal9 is defined as
    return base, deltas


_GEN9_BASE, _GEN9_DELTAS = None, None


def gen9_national(raw):
    global _GEN9_BASE, _GEN9_DELTAS
    if _GEN9_DELTAS is None:
        _GEN9_BASE, _GEN9_DELTAS = _gen9_internal_to_national()
    shift = raw - _GEN9_BASE
    if 0 <= shift < len(_GEN9_DELTAS):
        return raw + _GEN9_DELTAS[shift]
    return raw


def events(game):
    """Distribution templates for one game, as ordinary rows of kind "event"."""
    spec = EVENT_FORMATS.get(game)
    if spec is None:
        return []
    out = []
    # SEVERAL FILES PER GAME. Gens 6 and 7 keep their database in two: `wc6.pkl` holds bare cards
    # and `wc6full.pkl` holds full ones with the card at a fixed offset inside, and PKHeX unions
    # them (WC6Full.GetArray). Reading only the bare file loses every distribution archived with
    # its metadata -- which is most of them.
    for file_name, size, card in spec["files"]:
        data = _load(MGDB(file_name))
        # PGF IS NOT A FLAT ARRAY. Its records are packed at the FRONT and one "receivability" byte
        # per record follows them all (PKHeX PGF.GetArray: count = length / (Size + 1)), which is
        # why reading it as fixed-size records leaves a remainder and refuses the file.
        stride = size + 1 if spec.get("tailByte") else size
        if len(data) % stride:
            raise SystemExit("%s: %s is %d bytes, not a whole number of %d-byte records"
                             % (game, file_name, len(data), stride))
        record_count = len(data) // stride
        tail_base = record_count * size if spec.get("tailByte") else None
        out += _event_rows(game, spec, data, size, card, record_count, tail_base)
    return out


def _event_rows(game, spec, data, size, card, record_count, tail_base):
    out = []
    for index in range(record_count):
        start = index * size
        record = data[start:start + size]
        versions = BOTH
        if tail_base is not None:
            versions = spec["versionsFromTail"](game, data[tail_base + index])
            if versions == 0:
                continue   # a card for the other pair of this generation

        def read_uint8(offset):
            return record[card + offset]

        def read_uint16(offset):
            return struct.unpack_from("<H", record, card + offset)[0]

        if read_uint8(spec["cardType"]) != spec["pokemonType"]:
            continue   # an item, BP, clothing or money card -- no Pokemon to match

        species = read_uint16(spec["species"])
        if spec["species9"]:
            species = gen9_national(species)
        if not 0 < species <= MAX_SPECIES:
            continue   # a card whose Pokemon PKSE's tables do not cover

        level = read_uint8(spec["level"])
        met_level = read_uint8(spec["metLevel"]) or level
        if not 1 <= met_level <= 100:
            continue

        is_egg = read_uint8(spec["isEgg"]) == 1
        ball = read_uint16(spec["ball"]) if spec["ballWide"] else read_uint8(spec["ball"])
        shiny_map = spec.get("shinyMap") or (SHINY_TYPE_6 if spec["shiny6"] else SHINY_TYPE_8)
        shiny = shiny_map.get(read_uint8(spec["pidType"]), SHINY["random"])
        egg_location = read_uint16(spec["eggLocation"])
        location = read_uint16(spec["location"])

        flags = F_FATEFUL | (F_EGG if is_egg else 0)
        out.append(row(species, location, read_uint8(spec["form"]), met_level, met_level,
                       versions, "event", shiny=shiny, ball=ball, flags=flags,
                       egg_location=egg_location))
    return out


# ---------------------------------------------------------------------------
# Gen 3 and Gen 4 distributions -- neither fits the flat-record reader above
# ---------------------------------------------------------------------------
GEN2_SLOT_TYPE_SURF = 1      # PKHeX SlotType2.Surf; every type past it carries per-slot rates

GEN3_EVENT_LOCATION = 255    # PKHeX EncounterGift3.Location, fixed for every Gen 3 distribution
GEN3_EVENT_BALL = 4          # ...and EncounterGift3.FixedBall, likewise fixed

# PKHeX PCD.IsMatchExact: `wc.EggLocation + 3000 != pk.MetLocation`. A Gen 4 card stores its met
# location BIASED, in the egg-location field, and the game adds the bias on redemption -- which is
# why the raw values read 60 and 1 rather than anything that looks like a place.
GEN4_EVENT_LOCATION_BIAS = 3000
PCD_SIZE = 0x358             # PKHeX PCD.Size
PCD_PK4 = 0x08               # the PGT sits at PCD 0, and its PK4 at PGT+8 (PGT.DataGift)
GIFT_TYPE_4_POKEMON = 1      # PKHeX GiftType4.Pokemon; 2 is an egg and 3 an item
# Which group a card belongs to, from the origin version its PK4 carries. A Gen 4 card stamps the
# Pokemon's version itself rather than leaving it to the redeeming game, so this is the group whose
# table a Pokemon from that card will be looked up in. Nothing stamps Platinum.
GEN4_EVENT_VERSION_GROUP = {7: "HGSS", 8: "HGSS", 10: "DP", 11: "DP", 12: "PT"}


def events_gen3(game):
    """Gen 3 distributions, which are a hand-written C# array rather than a binary database.

    They span BOTH Gen 3 groups -- the tokens run R/S/RS for Hoenn, FRLG for Kanto, and EFL and
    Gen3 across them -- so each group filters the same list. Every one is met at location 255 in a
    Poke Ball; only the species, level and version vary.

    Pokemon Center New York and Japan (PCNY, PCJP) are deliberately absent: PKHeX builds those from
    their own resources rather than an inline array, and they are a separate shape again.
    """
    source_path = "Legality/Encounters/Data/Gen3/EncountersWC3.cs"
    text = strip_comments(read_cs(source_path))
    # Encounter_WC3 is `[..Common, ..International, ..Japan, ..Eggs]`, a spread of the four below
    # rather than entries of its own -- reading it as well would double every row.
    assert_all_arrays_used(source_path, ["Common", "Japan", "International", "Eggs", "Encounter_WC3"])
    out = []
    for name in ("Common", "International", "Japan", "Eggs"):
        for args, properties in parse_array(text, name):
            version_bits = version_from_args(args, game)
            if version_bits == 0:
                continue   # a distribution for the other Gen 3 group
            species, level = csharp_value(args[0]), csharp_value(args[1])
            # EncounterGift3(species, level, version) defers to (.., egg: false, met: level), while
            # the longer form defaults `met` to 0. So the three-argument entries are met at their
            # own level and the rest are met at whatever they say, which is usually 0.
            is_egg = len(args) > 3 and csharp_value(args[3]) == 1
            if len(args) <= 3:
                met_level = level
            elif len(args) > 4:
                met_level = csharp_value(args[4].split(":")[-1], 0)
            else:
                met_level = 0
            out.append(row(species, GEN3_EVENT_LOCATION, 0, met_level, met_level, version_bits,
                           "event", shiny=shiny_of(properties), ball=GEN3_EVENT_BALL,
                           flags=F_FATEFUL | (F_EGG if is_egg else 0)))
    return out


def events_gen4(game):
    """Gen 4 distributions: a flat array of PCD cards, each wrapping a whole PK4.

    NOTHING IS AT A FLAT OFFSET IN THE CARD. The species, ball, met level and location all come out
    of the embedded PK4, which is stored DECRYPTED here (PKHeX only re-encrypts on write), so the
    ordinary PK4 offsets read straight through. Its met location is the odd one: a Gen 4 card keeps
    it biased by 3000 in the EGG-location field, and the game adds the bias on redemption.
    """
    data = _load(MGDB("wc4.pkl"))
    if len(data) % PCD_SIZE:
        raise SystemExit("wc4.pkl is %d bytes, not a whole number of %d-byte cards"
                         % (len(data), PCD_SIZE))
    out = []
    for start in range(0, len(data), PCD_SIZE):
        if data[start] != GIFT_TYPE_4_POKEMON:
            continue   # an item, a Poketch app, a Pokewalker course -- no Pokemon to match
        pk4 = start + PCD_PK4

        def read_uint8(offset):
            return data[pk4 + offset]

        def read_uint16(offset):
            return struct.unpack_from("<H", data, pk4 + offset)[0]

        if GEN4_EVENT_VERSION_GROUP.get(read_uint8(0x5F)) != game:
            continue   # a card whose Pokemon carries another Gen 4 group's origin

        species = read_uint16(0x08)
        if not 0 < species <= MAX_SPECIES:
            continue
        # Extended fields first, DP second -- the same resolution the entity layer uses, and the
        # reason a Platinum-era card's location is not simply at 0x7E.
        egg_location_raw = read_uint16(0x44) or read_uint16(0x7E)
        met_level = read_uint8(0x84) & 0x7F
        out.append(row(species, egg_location_raw + GEN4_EVENT_LOCATION_BIAS,
                       read_uint8(0x40) >> 3, met_level, met_level, all_versions(game),
                       "event", ball=read_uint8(0x83), flags=F_FATEFUL))
    return out


EVENT_OVERRIDES = {"FRLG": events_gen3, "RSE": events_gen3,
                   "DP": events_gen4, "PT": events_gen4, "HGSS": events_gen4}


EVENT_SOURCES = {game: (lambda bound_game=game:
                        EVENT_OVERRIDES[bound_game](bound_game) if bound_game in EVENT_OVERRIDES
                        else events(bound_game))
                 for game in GAMES}


# ---------------------------------------------------------------------------
# Breedable species (PKHeX Breeding.IsAbleToHatchFromEgg)
# ---------------------------------------------------------------------------
def unbreedable_species():
    text = strip_comments(read_cs("Legality/Breeding.cs"))
    switch_match = re.search(r"IsAbleToHatchFromEgg\(ushort species\)\s*=>\s*species switch\s*\{", text)
    if not switch_match:
        raise SystemExit("Breeding.IsAbleToHatchFromEgg not found")
    body = text[switch_match.end():_match(text, text.index("{", switch_match.end() - 1), '{', '}') - 1]
    out = set()
    for arm in body.split(","):
        if "=> false" not in arm:
            continue
        for name in re.findall(r"\(int\)([A-Za-z0-9_]+)", arm):
            if name not in SPECIES:
                raise SystemExit("unknown species token %r in Breeding.cs" % name)
            out.add(SPECIES[name])
    if len(out) < 80:
        raise SystemExit("Breeding.cs parse looks wrong (%d species)" % len(out))
    return out


def breedable_bitset(game, present):
    """Species bitset: can a Pokemon in this game have come from a daycare egg?"""
    blocked = unbreedable_species()
    bits = bytearray((MAX_SPECIES + 8) // 8)
    for species in range(1, MAX_SPECIES + 1):
        if species in blocked or species not in present[game]:
            continue
        bits[species >> 3] |= 1 << (species & 7)
    return bytes(bits)


def _permitted_bytes(relpath, name):
    text = strip_comments(read_cs(relpath))
    span_match = re.search(r"ReadOnlySpan<byte>\s+" + name + r"\s*=>\s*\[(.*?)\];", text, re.S)
    if not span_match:
        raise SystemExit("%s not found in %s" % (name, relpath))
    return [int(token) for token in re.findall(r"\d+", span_match.group(1))]


# PKHeX's own bit for each game inside its EggHatchLocation* table, and the bit this table wants
# in its place. The two orderings have nothing to do with each other -- PKHeX groups R/S together
# and keeps Emerald apart, numbers White before Black, and indexes Gen 4 as DP/Pt/HGSS -- so the
# remap is written out per game rather than inferred. `(pkhex_bit, pkse_bit)` pairs; the pkse bit
# is what encounterVersionBit() answers for that version, ORed for a pair that shares one PKHeX bit.
HATCH_MASK_SOURCES = {
    # EggHatchLocation3: MaskRS = 1, MaskE = 2, MaskFRLG = 4.
    "FRLG": ("Legality/Encounters/Verifiers/EggHatchLocation3.cs", "LocationPermitted3",
             ((4, 0b11),)),                      # FRLG's bit -> FR | LG
    "RSE": ("Legality/Encounters/Verifiers/EggHatchLocation3.cs", "LocationPermitted3",
            ((1, 0b011), (2, 0b100))),           # RS -> RU | SA, E -> EM
    # EggHatchLocation4: MaskDP = 1, MaskPt = 2, MaskHGSS = 4.
    "DP": ("Legality/Encounters/Verifiers/EggHatchLocation4.cs", "LocationPermitted4",
           ((1, 0b11),)),                        # DP -> D | P
    "PT": ("Legality/Encounters/Verifiers/EggHatchLocation4.cs", "LocationPermitted4",
           ((2, 0b1),)),                         # Pt is a group of one
    "HGSS": ("Legality/Encounters/Verifiers/EggHatchLocation4.cs", "LocationPermitted4",
             ((4, 0b11),)),                      # HGSS -> HG | SS
    # EggHatchLocation5: MaskWhite = 1, MaskBlack = 2, MaskWhite2 = 4, MaskBlack2 = 8. PKHeX
    # numbers White before Black and this table numbers Black before White, so the pairs cross.
    "BW": ("Legality/Encounters/Verifiers/EggHatchLocation5.cs", "LocationPermitted5",
           ((2, 0b01), (1, 0b10))),              # Black -> B, White -> W
    "B2W2": ("Legality/Encounters/Verifiers/EggHatchLocation5.cs", "LocationPermitted5",
             ((8, 0b01), (4, 0b10))),            # Black 2 -> B2, White 2 -> W2
    # EggHatchLocation7: MaskSM = 1, MaskUSUM = 2.
    "SM": ("Legality/Encounters/Verifiers/EggHatchLocation7.cs", "LocationPermitted7",
           ((1, 0b11),)),                        # SM -> SN | MN
    "USUM": ("Legality/Encounters/Verifiers/EggHatchLocation7.cs", "LocationPermitted7",
             ((2, 0b11),)),                      # USUM -> US | UM
    # EggHatchLocation8b / 9 are already one byte per location with bit 0 / bit 1 = the two
    # versions, in the same order this table uses (BD/SP, SL/VL), so they need no remap.
    "BDSP": ("Legality/Encounters/Verifiers/EggHatchLocation8b.cs", "LocationPermitted8b", None),
    "SV": ("Legality/Encounters/Verifiers/EggHatchLocation9.cs", "LocationPermitted9", None),
}

# EggHatchLocation6 IS AN ARITHMETIC RULE, NOT A TABLE: every EVEN location in a range, minus one
# unused id. Transcribed as the rule rather than a hand-typed span, because a 185-entry list nobody
# can proofread is exactly the shape that goes wrong silently.
HATCH_RULE_6 = {
    "XY": (6, 168, 80),      # EggHatchLocation6.IsValidMet6XY -- 80 is unused
    "ORAS": (170, 354, 348),  # IsValidMet6AO -- 348 is unused
}


GEN2_HATCH_LOCATION_MAX = 95  # PKHeX EncounterVerifier.VerifyEncounterEgg2


def hatch_mask(game):
    """Per-location version mask: which games' eggs may hatch at this met location?

    An egg hatches wherever the player is walking, so "the" hatch location is a SET, not one
    place -- PKHeX carries one per generation (EggHatchLocation3 through 9) in four different
    encodings, and they are normalised here into one byte per location id whose bits are this
    table's own version bits, so the matcher stays a single indexed test.

    GEN 2 IS CRYSTAL-ONLY (Gold and Silver write no met data) AND IT IS NOT ONE PLACE. PKHeX's
    EncounterEgg2 STAMPS HatchLocationC when it builds an egg, but its verifier accepts any met
    location up to 95 (EncounterVerifier.VerifyEncounterEgg2: "Any met location is fine"). Taking
    the stamp for the rule flagged every Crystal egg hatched anywhere but location 16.
    """
    if game == "GSC":
        mask = bytearray(GEN2_HATCH_LOCATION_MAX + 1)
        for location in range(GEN2_HATCH_LOCATION_MAX + 1):
            mask[location] = version_token_mask("C", game)
        return bytes(mask)
    if game in HATCH_RULE_6:
        first, last, unused = HATCH_RULE_6[game]
        mask = bytearray(last + 1)
        for location in range(first, last + 1, 2):
            if location != unused:
                mask[location] = all_versions(game)
        return bytes(mask)
    if game == "SWSH":
        # EggHatchLocation8: indexed by location >> 1, and every ODD location is invalid.
        permitted = _permitted_bytes("Legality/Encounters/Verifiers/EggHatchLocation8.cs",
                               "LocationPermitted8")
        mask = bytearray(len(permitted) * 2)
        for location_index, permitted_flag in enumerate(permitted):
            if permitted_flag:
                mask[location_index * 2] = BOTH
        return bytes(mask)
    if game not in HATCH_MASK_SOURCES:
        return b""
    relpath, name, remap = HATCH_MASK_SOURCES[game]
    permitted = _permitted_bytes(relpath, name)
    if remap is None:
        return bytes(permitted)
    mask = bytearray(len(permitted))
    for location, flags in enumerate(permitted):
        for pkhex_bit, pkse_bit in remap:
            if flags & pkhex_bit:
                mask[location] |= pkse_bit
    if not any(mask):
        raise SystemExit("%s: hatch mask came out empty -- the bit remap is wrong" % game)
    return bytes(mask)


def present_species():
    """Species each game contains, from the personal tables gen_learnsets already reads.

    Built for GAMES -- the ones this generator emits -- and NOT for gen_learnsets'
    GAMES_TO_EMIT. That list grew to cover every pre-Switch generation, and Gen 1 and
    Gen 2 have no entry in its modern PERSONAL_FMT at all (their personal data is a
    different shape), so asking it for one raised a KeyError and took this whole
    generator down with it. Nothing here has ever needed a game outside GAMES.

    Both Gen 3 groups share one 386-species dex and one personal table, so both ask Personal3 --
    the Hoenn dex is a display order, not a different set of Pokemon.
    """
    from gen_learnsets import Personal, Personal3
    # Gen 1 and Gen 2 have no entry in gen_learnsets' modern PERSONAL_FMT -- their personal data is
    # a different shape entirely -- and they need none: a species is in those games iff its number
    # is inside their dex, with no alternate forms and no per-game presence flag to consult.
    dex_only = {"RBY": 151, "GSC": 251}
    gen3_games = ("FRLG", "RSE")
    personal = {game: Personal(game) for game in GAMES
                if game not in gen3_games and game not in dex_only}
    for game in gen3_games:
        personal[game] = Personal3()
    return {game: (set(range(1, dex_only[game] + 1)) if game in dex_only
                   else {species for species in range(1, MAX_SPECIES + 1)
                         if personal[game].present(species, 0)})
            for game in GAMES}


# ---------------------------------------------------------------------------
# Emit
# ---------------------------------------------------------------------------
HEADER = '''/**
 * Auto-generated by tools/gen_encounters.py from PKHeX's wild-slot binaries and
 * static/gift/trade/raid tables. DO NOT EDIT BY HAND -- rerun the generator instead.
 *
 * One flat row per template. Wild and raid rows are MERGED per
 * (species, form, location, constraints): the level span is the union of the
 * contributing slots and the guaranteed-31 count their minimum, which is permissive
 * by construction -- merging can widen what the table accepts, never narrow it.
 * Static / gift / trade rows are never merged; each is a distinct template whose
 * fixed data has to stay exact.
 *
 * The ability slot is deliberately absent: Ability Capsule and Ability Patch let a
 * legitimate Pokemon hold a slot its encounter never offered, so the column would
 * only enable a check that false-flags real saves. Layer 2 already verifies the
 * ability against the species.
 */
#ifndef LEGALITY_ENCOUNTER_TABLE_H
#define LEGALITY_ENCOUNTER_TABLE_H

#include <cstdint>

#include "Enums/GameVersion.h"

namespace Legality {

    /// How the Pokemon was obtained. Reported verbatim, so keep the wording usable.
    enum EncounterKind : uint8_t {
        ENCOUNTER_KIND_WILD   = 0,
        ENCOUNTER_KIND_STATIC = 1,
        ENCOUNTER_KIND_GIFT   = 2,
        ENCOUNTER_KIND_TRADE  = 3,
        ENCOUNTER_KIND_RAID   = 4,
        /// A Mystery Gift distribution. Kept apart from ENCOUNTER_KIND_GIFT (an in-game NPC gift)
        /// because the two answer different questions for a reader: an event says the
        /// Pokemon came from a real distribution, which is exactly what a fateful-encounter
        /// bit claims and what nothing could previously check.
        ENCOUNTER_KIND_EVENT  = 5,
    };

    /// Shiny policy of a template -- Never is a shiny LOCK, Always a forced shiny.
    enum EncounterShiny : uint8_t {
        ENCOUNTER_SHINY_RANDOM = 0,
        ENCOUNTER_SHINY_NEVER  = 1,
        ENCOUNTER_SHINY_ALWAYS = 2,
    };

    enum EncounterFlag : uint8_t {
        ENCOUNTER_FLAG_FATEFUL = 1 << 0,  // sets the fateful-encounter (event) bit
        ENCOUNTER_FLAG_ALPHA   = 1 << 1,  // Legends Alpha (PLA / Z-A)
        ENCOUNTER_FLAG_BOOST60 = 1 << 2,  // SW/SH Wild Area: post-game boosts levels to 60
        ENCOUNTER_FLAG_EGG     = 1 << 3,  // handed over as an egg
        /// The game writes met level 0 for this encounter, so the level span bounds the CURRENT level
        /// instead. Gen 2's in-game trades: Crystal records them at LinkTrade2NPC and met level 0,
        /// and the received Pokemon is at least the level the template names (EncounterTrade2).
        ENCOUNTER_FLAG_MET_LEVEL_ZERO = 1 << 4,
        /// The slot can be met ONE LEVEL above its range. Let's Go's catch combo spawns a slot's
        /// Pokemon a level higher, so PKHeX matches every Let's Go wild slot up to LevelMax + 1
        /// (EncounterSlot7b.CatchComboBonus).
        ENCOUNTER_FLAG_CATCH_COMBO = 1 << 5,
    };

    constexpr uint8_t ENCOUNTER_FORM_ANY   = 0xFF;  // slot randomises the form
    constexpr uint8_t ENCOUNTER_GENDER_ANY = 0xFF;
    constexpr uint8_t ENCOUNTER_NATURE_ANY = 0xFF;

    /// Locations that stand for a whole region rather than one place. Two kinds use them:
    /// SW/SH distribution raids, where the den is anonymous so any Wild Area location of the
    /// right DLC tier is legal, and ROAMERS, which are met wherever they were finally cornered
    /// rather than where their template starts them.
    constexpr uint16_t ENCOUNTER_LOCATION_SWSH_WILDAREA               = 0xFFF0;  // Galar Wild Area
    constexpr uint16_t ENCOUNTER_LOCATION_SWSH_WILDAREA_ISLE_OF_ARMOR = 0xFFF1;  // + Isle of Armor
    constexpr uint16_t ENCOUNTER_LOCATION_SWSH_WILDAREA_ALL           = 0xFFF2;  // + Crown Tundra
    constexpr uint16_t ENCOUNTER_LOCATION_ROAMER3                     = 0xFFF3;  // Hoenn routes 101-138
    constexpr uint16_t ENCOUNTER_LOCATION_ROAMER4_SINNOH              = 0xFFF4;  // Sinnoh roamer routes
    constexpr uint16_t ENCOUNTER_LOCATION_ROAMER4_JOHTO               = 0xFFF5;  // Johto roamer routes
    constexpr uint16_t ENCOUNTER_LOCATION_ROAMER4_KANTO               = 0xFFF6;  // Kanto roamer routes
    constexpr uint16_t ENCOUNTER_LOCATION_ROAMER5                     = 0xFFF7;  // Unova roamer routes
    constexpr uint16_t ENCOUNTER_LOCATION_ROAMER2                     = 0xFFF8;  // Johto roamer routes

    /// NOT a sentinel: a real Gen 3 met location, and the one where everything that reached a Gen 3
    /// cartridge from OUTSIDE it is met -- every Mystery Gift distribution, and Pokemon Colosseum's
    /// gifts (the Japanese bonus disc's Pikachu and Celebi, and Ho-Oh from Mt. Battle). PKHeX
    /// Locations.IsEventLocation3.
    constexpr uint16_t ENCOUNTER_LOCATION_EVENT3 = 255;

    struct EncounterRow {
        uint16_t species;
        uint16_t location;     // met location, in the ORIGIN game's namespace
        uint16_t eggLocation;  // 0 unless the template hands over an egg
        uint8_t  form;         // ENCOUNTER_FORM_ANY when the slot randomises it
        uint8_t  levelMin;
        uint8_t  levelMax;
        uint8_t  versions;     // one bit per game in the group, in GameVersion-name order.
                               // Two bits everywhere except Ruby/Sapphire/Emerald, which is the
                               // only group of three -- see encounterVersionBit.
        uint8_t  kind;         // EncounterKind
        uint8_t  shiny;        // EncounterShiny
        uint8_t  gender;       // 0 male, 1 female, 2 genderless, ENCOUNTER_GENDER_ANY
        uint8_t  fixedBall;    // 0 = the encounter does not fix the ball
        uint8_t  flawlessIVs;  // guaranteed count of 31 IVs
        uint8_t  nature;       // ENCOUNTER_NATURE_ANY unless the template forces one
        uint8_t  flags;        // EncounterFlag bits
    };

    /// Where a game's eggs come from and where they may hatch.
    struct EggRule {
        uint16_t eggLocation;     // nursery id stamped into the egg-location field.
                                  // 0 means the FORMAT has no such field (Gen 3 alone),
                                  // which is why an egg there has to be inferred instead.
        /// Other ids a BRED egg may legitimately carry, 0 for an unused slot. Two of them because
        /// Gen 5 has three legal values in all: the daycare above, the ordinary link trade, and
        /// the one Spin Trade writes (PKHeX Locations.IsEggLocationBred5). Everywhere else the
        /// second slot is 0.
        uint16_t eggLocationAlternates[2];
        /// Per-location version mask: one byte per met location id, holding this table's own
        /// version bits for the games whose eggs may hatch there. An egg hatches wherever the
        /// player happens to be walking, so this is a permitted SET rather than one place --
        /// PKHeX's EggHatchLocation3 through 9, normalised. Gen 2 is the single fixed case
        /// (Crystal stamps one id and Gold/Silver record nothing at all).
        /// nullptr when the game has no breeding, or when PKSE has no mask for it, which
        /// canHatchAt reads as NOT CHECKED rather than as "nowhere".
        const uint8_t* hatchLocations;
        uint16_t hatchLocationCount;
        /// Where the game itself stamps an egg it hands over -- its day care's own map
        /// (PKHeX Locations.HatchLocation*). Always inside the mask above; it is the canonical
        /// answer of the set, and what a rebuilt Gen 3 record is given.
        uint16_t hatchLocation;
        /// One further permitted hatch location sitting too far up the id space for the mask to
        /// reach, 0 when there is none. Poke Pelago (30016) is the only one: EggHatchLocation7
        /// accepts it alongside its byte table, and a mask stretching to 30016 would be thirty
        /// kilobytes of zeros.
        uint16_t hatchLocationExtra;
        /// MET level of a hatched egg -- 0 in Gens 3 and 4, 1 from Gen 5 on (and in Crystal).
        /// NOT the level it hatches AT, which is the next field; conflating the two made every
        /// bred Gen 3 Pokemon read as having no encounter at all.
        uint8_t  hatchLevel;
        /// The level a Pokemon IS when its egg hatches -- 5 in Gens 2 and 3, 1 from Gen 4 on
        /// (PKHeX EggStateLegality.GetEggLevel). A record below it never hatched, whatever its
        /// met data says.
        uint8_t  hatchCurrentLevel;
        uint8_t  hasBreeding;     // 0 for the games with no daycare at all
    };

    struct EncounterTable {
        const EncounterRow* rows;
        uint32_t rowCount;
        /// speciesIndex[s] .. speciesIndex[s + 1] bounds the rows for species s.
        const uint16_t* speciesIndex;
        uint16_t speciesIndexLength;
        /// Species bitset: could a Pokemon here have hatched from a daycare egg?
        /// nullptr when the game has no breeding.
        const uint8_t* breedable;
        EggRule egg;
    };

    /// Encounter data for a game or game group; nullptr when PKSE has none for it
    /// (every generation before Gen 3, Pokemon GO, and anything unrecognised).
    const EncounterTable* getEncounterTable(Enums::GameVersion versionOrGroup);

    /// Row version bit for an exact version id (0 when it names a group, not a game).
    uint8_t encounterVersionBit(Enums::GameVersion version);

    /// True when a met location satisfies a row's location, resolving the region
    /// sentinels above.
    bool encounterLocationMatches(uint16_t rowLocation, uint16_t metLocation);
}

#endif  // LEGALITY_ENCOUNTER_TABLE_H
'''


def emit_bytes(lines, symbol_name, blob):
    lines.append('        const uint8_t %s[%d] = {' % (symbol_name, len(blob)))
    for offset in range(0, len(blob), 16):
        lines.append('            ' + ' '.join('0x%02X,' % byte for byte in blob[offset:offset + 16]))
    lines.append('        };')


def emit_cpp(tables, indices, breedable, hatch):
    lines = []
    lines.append('/**')
    lines.append(' * Auto-generated by tools/gen_encounters.py. DO NOT EDIT BY HAND.')
    lines.append(' */')
    lines.append('#include "Legality/EncounterTable.h"')
    lines.append('')
    lines.append('namespace Legality {')
    lines.append('')
    lines.append('    namespace {')
    for game in GAMES:
        rows = tables[game]
        lines.append('        const EncounterRow ROWS_%s[%d] = {' % (game, len(rows)))
        for encounter_row in rows:
            lines.append('            {%4d,%6d,%6d,%4d,%4d,%4d,%2d,%2d,%2d,%4d,%3d,%2d,%4d,%2d},'
                     % encounter_row)
        lines.append('        };')
        lines.append('        const uint16_t IDX_%s[%d] = {' % (game, SPECIES_INDEX_LENGTH))
        species_index = indices[game]
        for offset in range(0, len(species_index), 16):
            index_chunk = species_index[offset:offset + 16]
            lines.append('            ' + ' '.join('%5d,' % index_value
                                                   for index_value in index_chunk))
        lines.append('        };')
        if game in breedable:
            emit_bytes(lines, 'BREED_%s' % game, breedable[game])
        if hatch.get(game):
            emit_bytes(lines, 'HATCH_%s' % game, hatch[game])
        lines.append('')

    blank_egg_rule = _egg(0, 0, 0, 0, 0)
    for game in GAMES:
        egg_rule = EGG_RULES.get(game, blank_egg_rule)
        has_breeding = 0 if game in NO_BREEDING else 1
        hatch_symbol = 'HATCH_%s' % game if hatch.get(game) else 'nullptr'
        hatch_location_count = len(hatch.get(game, b''))
        lines.append('        const EncounterTable TABLE_%s = {' % game)
        lines.append('            ROWS_%s, %d, IDX_%s, %d,' % (game, len(tables[game]), game,
                                                           SPECIES_INDEX_LENGTH))
        lines.append('            %s,' % ('BREED_%s' % game if game in breedable else 'nullptr'))
        lines.append('            { %d, { %d, %d }, %s, %d, %d, %d, %d, %d, %d },'
                     % (egg_rule.egg_location, egg_rule.alternate, egg_rule.second_alternate,
                        hatch_symbol, hatch_location_count,
                        hatch_location(game) if game in EGG_RULES else 0,
                        hatch_location_extra(game) if game in EGG_RULES else 0,
                        hatch_level(game) if game in EGG_RULES else 0,
                        hatch_current_level(game) if game in EGG_RULES else 0, has_breeding))
        lines.append('        };')
    lines.append('    }')
    lines.append('')
    lines.append('    const EncounterTable* getEncounterTable(Enums::GameVersion versionOrGroup) {')
    lines.append('        switch (versionOrGroup) {')
    for game in GAMES:
        names = [GROUP_ENUM[game]] + list(GROUP_VERSIONS[game])
        for name in dict.fromkeys(names):
            lines.append('            case Enums::GameVersion::%s:' % name)
        lines.append('                return &TABLE_%s;' % game)
    lines.append('            default:')
    lines.append('                return nullptr;')
    lines.append('        }')
    lines.append('    }')
    lines.append('')
    lines.append('    uint8_t encounterVersionBit(Enums::GameVersion version) {')
    lines.append('        switch (version) {')
    for game in GAMES:
        for bit_index, version_name in enumerate(GROUP_VERSIONS[game]):
            lines.append('            case Enums::GameVersion::%s: return %d;'
                         % (version_name, 1 << bit_index))
    lines.append('            default: return 0;')
    lines.append('        }')
    lines.append('    }')
    lines.append('')
    lines.append('    bool encounterLocationMatches(uint16_t rowLocation, uint16_t metLocation) {')
    lines.append('        switch (rowLocation) {')
    lines.append('            // EncounterArea8.IsWildArea8 / Armor / Crown -- Freezington (206)')
    lines.append('            // is a town inside the Crown Tundra range and holds no dens.')
    lines.append('            case ENCOUNTER_LOCATION_SWSH_WILDAREA:')
    lines.append('                return metLocation >= 122 && metLocation <= 154;')
    lines.append('            case ENCOUNTER_LOCATION_SWSH_WILDAREA_ISLE_OF_ARMOR:')
    lines.append('                return (metLocation >= 122 && metLocation <= 154) || (metLocation >= 164 && metLocation <= 194);')
    lines.append('            case ENCOUNTER_LOCATION_SWSH_WILDAREA_ALL:')
    lines.append('                return (metLocation >= 122 && metLocation <= 154) || (metLocation >= 164 && metLocation <= 194) || (metLocation >= 204 && metLocation <= 234 && metLocation != 206);')
    lines.append('            // A roamer is met where it was cornered. EncounterStatic3 tests one flat')
    lines.append('            // range; EncounterStatic4 tests a bitmask per region, and takes the union')
    lines.append('            // of its grass and water sets here because PKSE does not read GroundTile --')
    lines.append('            // which can only widen what is accepted, never narrow it.')
    lines.append('            case ENCOUNTER_LOCATION_ROAMER3:')
    lines.append('                return metLocation >= 16 && metLocation <= 49;')
    for sentinel_name, sentinel_value in (("ENCOUNTER_LOCATION_ROAMER4_SINNOH", ENCOUNTER_LOCATION_ROAMER4_SINNOH),
                                          ("ENCOUNTER_LOCATION_ROAMER4_JOHTO", ENCOUNTER_LOCATION_ROAMER4_JOHTO),
                                          ("ENCOUNTER_LOCATION_ROAMER4_KANTO", ENCOUNTER_LOCATION_ROAMER4_KANTO)):
        first_location, permitted = ROAMER4_RANGES[sentinel_value]
        lines.append('            case %s:' % sentinel_name)
        lines.append('                return metLocation >= %d && metLocation < %d && ((0x%XULL >> (metLocation - %d)) & 1) != 0;'
                     % (first_location, first_location + permitted.bit_length(), permitted, first_location))
    lines.append('            case ENCOUNTER_LOCATION_ROAMER5:')
    lines.append('                return metLocation < 32 && ((0x%XU >> metLocation) & 1) != 0;'
                 % ROAMER5_PERMITTED)
    lines.append('            case ENCOUNTER_LOCATION_ROAMER2:')
    lines.append('                return metLocation < %d && ((0x%XULL >> metLocation) & 1) != 0;'
                 % (ROAMER2_PERMITTED.bit_length(), ROAMER2_PERMITTED))
    lines.append('            default:')
    lines.append('                return rowLocation == metLocation;')
    lines.append('        }')
    lines.append('    }')
    lines.append('}')
    lines.append('')
    return "\n".join(lines)


def build_index(rows):
    """speciesIndex[s] = first row with species >= s (rows are sorted by species)."""
    species_index = [0] * SPECIES_INDEX_LENGTH
    row_position = 0
    for species in range(SPECIES_INDEX_LENGTH):
        while row_position < len(rows) and rows[row_position][0] < species:
            row_position += 1
        species_index[species] = row_position
    return species_index


def main():
    present = present_species()
    tables, indices, breedable, hatch = {}, {}, {}, {}
    total = 0
    for game in GAMES:
        rows = WILD_SOURCES[game]() + STATIC_SOURCES[game]() + EVENT_SOURCES[game]()
        invalid_rows = [encounter_row for encounter_row in rows if not (0 < encounter_row[0] <= MAX_SPECIES)]
        if invalid_rows:
            raise SystemExit("%s: %d rows with an out-of-range species (e.g. %r)"
                             % (game, len(invalid_rows), invalid_rows[0]))
        rows = merge_rows(rows)
        if len(rows) > 0xFFFF:
            raise SystemExit("%s: %d rows overflows the uint16 species index" % (game, len(rows)))
        tables[game] = rows
        indices[game] = build_index(rows)
        if game not in NO_BREEDING:
            breedable[game] = breedable_bitset(game, present)
            hatch[game] = hatch_mask(game)
            # THE CANONICAL HATCH LOCATION MUST BE INSIDE THE PERMITTED SET, or the rebuild writes
            # a met location the checker then refuses -- the two halves reading one fact differently
            # is exactly how a hatched Gen 3 Pokemon came to be flagged in the first place.
            canonical = hatch_location(game)
            if canonical >= len(hatch[game]) or hatch[game][canonical] == 0:
                raise SystemExit("%s: hatch location %d is not in its own permitted mask"
                                 % (game, canonical))
        total += len(rows)
        kinds = {}
        for encounter_row in rows:
            kinds[encounter_row[FIELDS.index("kind")]] = kinds.get(encounter_row[FIELDS.index("kind")], 0) + 1
        summary = " ".join("%s=%d" % (kind_name, kinds.get(kind_value, 0)) for kind_name, kind_value in KIND.items())
        print("  %-5s rows %6d   %s" % (game, len(rows), summary))

    with open(OUTPUT_HEADER, "w", encoding="utf-8", newline="\n") as output_file:
        output_file.write(HEADER)
    print("Wrote", OUTPUT_HEADER)
    with open(OUTPUT_SOURCE, "w", encoding="utf-8", newline="\n") as output_file:
        output_file.write(emit_cpp(tables, indices, breedable, hatch))
    print("Wrote", OUTPUT_SOURCE)

    row_bytes = total * 16
    index_bytes = len(GAMES) * SPECIES_INDEX_LENGTH * 2
    breed_bytes = sum(len(blob) for blob in breedable.values())
    hatch_bytes = sum(len(blob) for blob in hatch.values())
    table_total = row_bytes + index_bytes + breed_bytes + hatch_bytes
    print("  rows %d B  indices %d B  breedable %d B  hatch %d B"
          % (row_bytes, index_bytes, breed_bytes, hatch_bytes))
    print("  TABLE TOTAL: %d B (%.1f KB)" % (table_total, table_total / 1024))


if __name__ == "__main__":
    main()
