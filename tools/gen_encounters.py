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
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pkhex_source import pkhex_path  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_H = os.path.join(ROOT, "include", "Legality", "EncounterTable.h")
OUT_CPP = os.path.join(ROOT, "src", "Legality", "EncounterTable.cpp")

MAX_SPECIES = 1025          # matches Pokemon::PERSONAL_MAX_SPECIES
SPECIES_INDEX_LEN = MAX_SPECIES + 2


def WILD(*parts):
    return pkhex_path("Resources/legality/wild/" + "/".join(parts))


def CORE(relpath):
    return pkhex_path(relpath)


# Games in table order. Each maps to an Enums::GameVersion group plus the version ids
# that occupy bit 0 / bit 1 of a row's `versions` mask.
GAMES = ["FRLG", "GG", "SWSH", "BDSP", "PLA", "SV", "ZA"]
GROUP_ENUM = {"FRLG": "FRLG", "GG": "GG", "SWSH": "SWSH", "BDSP": "BDSP",
              "PLA": "PLA", "SV": "SV", "ZA": "ZA"}
# (bit 0 version, bit 1 version) as Enums::GameVersion names; None = the group has one game.
GROUP_VERSIONS = {
    "FRLG": ("FR", "LG"), "GG": ("GP", "GE"), "SWSH": ("SW", "SH"),
    "BDSP": ("BD", "SP"), "PLA": ("PLA", None), "SV": ("SL", "VL"), "ZA": ("ZA", None),
}
BOTH = 0b11   # a template both versions share

# Row field encodings -- mirrored by the enums in the emitted header.
KIND = {"wild": 0, "static": 1, "gift": 2, "trade": 3, "raid": 4}
SHINY = {"random": 0, "never": 1, "always": 2}
FORM_ANY, GENDER_ANY, NATURE_ANY = 0xFF, 0xFF, 0xFF
F_FATEFUL, F_ALPHA, F_BOOST60, F_EGG = 1, 2, 4, 8

# Location sentinels for templates whose met location is a whole region rather than one
# id. SW/SH distribution raids are the only case: the den is anonymous, so any Wild Area
# location of the right DLC tier is legal (PKHeX EncounterStatic8ND.IsMatchLocation).
LOC_SWSH_WA_BASE = 0xFFF0    # Wild Area (Galar)
LOC_SWSH_WA_IOA = 0xFFF1     # + Isle of Armor
LOC_SWSH_WA_ALL = 0xFFF2     # + Crown Tundra

LINK_TRADE_NPC_3 = 254       # PKHeX Locations.LinkTrade3NPC
LINK_TRADE_NPC_6 = 30001     # PKHeX Locations.LinkTrade6NPC -- Gen 7b/8/9 in-game trades
TERA_CAVERN_9 = 30024        # PKHeX Locations.TeraCavern9
SHARED_NEST_8 = 162          # PKHeX Encounters8Nest.SharedNest
MAX_LAIR_8 = 244             # PKHeX Encounters8Nest.MaxLair
BDSP_NONE = 65535            # PKHeX Locations.Default8bNone -- BDSP's "no location"

# Egg rules: (eggLocation, altEggLocation, hatchLevel).
# eggLocation is the nursery id stamped into the egg-location field; 0 means the format HAS no
# such field (Gen 3). altEggLocation is what a traded egg carries instead.
# PKHeX: Locations.Daycare5 / Daycare8b / Picnic9 / LinkTrade6.
EGG_RULES = {
    "FRLG": (0, 0, 5),           # Gen 3 has no egg-location field; eggs hatch at level 5
    "SWSH": (60002, 30002, 1),   # Daycare5, LinkTrade6
    "BDSP": (60010, 30002, 1),   # Daycare8b, LinkTrade6
    "SV": (30023, 30002, 1),     # Picnic9, LinkTrade6
}
NO_BREEDING = ("GG", "PLA", "ZA")         # Let's Go, Legends: Arceus and Z-A have no daycare

# Gen 3 is the one game with a FIXED hatch location: the met location of a Gen 3 egg is stamped
# when it is received, not where it hatches (PKHeX EncounterVerifier uses HatchLocationFRLG
# directly). Everywhere else the egg hatches wherever the player happens to be walking, so the
# hatch location is a whole permitted SET -- see hatch_mask below.
HATCH_LOCATION_FRLG = 146    # PKHeX Locations.HatchLocationFRLG (Four Island, the day care)


# ---------------------------------------------------------------------------
# Binary containers
# ---------------------------------------------------------------------------
def _load(path):
    with open(path, "rb") as fh:
        return fh.read()


def bin32(data):
    """PKHeX BinLinkerAccessor -- magic[2], u16 count, then count+1 u32 offsets."""
    n = struct.unpack_from("<H", data, 2)[0]
    offs = struct.unpack_from("<%dI" % (n + 1), data, 4)
    return [data[offs[i]:offs[i + 1]] for i in range(n)]


def bin16(data):
    """PKHeX BinLinkerAccessor16 -- same, with u16 offsets."""
    n = struct.unpack_from("<H", data, 2)[0]
    offs = struct.unpack_from("<%dH" % (n + 1), data, 4)
    return [data[offs[i]:offs[i + 1]] for i in range(n)]


# ---------------------------------------------------------------------------
# C# source parsing
# ---------------------------------------------------------------------------
def read_cs(relpath):
    with open(CORE(relpath), encoding="utf-8") as fh:
        return fh.read()


def strip_comments(text):
    """Drop // and /* */ comments, leaving string literals intact."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == '\\' else 1
            out.append(text[i:j + 1])
            i = j + 1
        elif c == '/' and i + 1 < n and text[i + 1] == '/':
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif c == '/' and i + 1 < n and text[i + 1] == '*':
            j = text.find("*/", i)
            i = n if j < 0 else j + 2
        else:
            out.append(c)
            i += 1
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
    m = re.search(r"enum\s+" + enum_name + r"\b[^{]*\{", text)
    if not m:
        raise SystemExit("enum %s not found in %s" % (enum_name, relpath))
    depth, i = 1, m.end()
    while depth:
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
        i += 1
    body = text[m.end():i - 1]
    out, nxt = {}, 0
    for member in body.split(","):
        member = member.strip()
        if not member:
            continue
        mm = re.match(r"([^\W\d]\w*)\s*(?:=\s*(-?\w+))?$", member)
        if not mm:
            # Never skip silently: a dropped member shifts every later value by one.
            raise SystemExit("%s: cannot parse enum member %r" % (enum_name, member))
        if mm.group(2) is not None:
            nxt = int(mm.group(2), 0)
        out[mm.group(1)] = nxt
        nxt += 1
    return out


def split_top(s, opener="([{", closer=")]}"):
    """Split a C# argument/initializer list on top-level commas."""
    parts, depth, cur, i, n = [], 0, [], 0, len(s)
    while i < n:
        c = s[i]
        if c == '"':
            j = i + 1
            while j < n and s[j] != '"':
                j += 2 if s[j] == '\\' else 1
            cur.append(s[i:j + 1])
            i = j + 1
            continue
        if c in opener:
            depth += 1
        elif c in closer:
            depth -= 1
        if c == ',' and depth == 0:
            parts.append("".join(cur).strip())
            cur = []
        else:
            cur.append(c)
        i += 1
    tail = "".join(cur).strip()
    if tail:
        parts.append(tail)
    return parts


def _match(text, i, open_ch, close_ch):
    """Index just past the bracket group starting at text[i] == open_ch."""
    depth, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == '"':
            i += 1
            while i < n and text[i] != '"':
                i += 2 if text[i] == '\\' else 1
        elif c == open_ch:
            depth += 1
        elif c == close_ch:
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    raise SystemExit("unbalanced %s%s" % (open_ch, close_ch))


def parse_array(text, array_name):
    """Parse `<array_name> = [ new(..) { .. }, .. ];` into [(pos_args, props)] entries."""
    m = re.search(re.escape(array_name) + r"\s*=\s*\[", text)
    if not m:
        raise SystemExit("array %s not found" % array_name)
    start = text.index("[", m.end() - 1)
    body = text[start + 1:_match(text, start, '[', ']') - 1]

    entries, i, n = [], 0, len(body)
    while i < n:
        m2 = re.compile(r"\bnew\b").search(body, i)
        if not m2:
            break
        i = m2.end()
        # An explicit `new EncounterX(...)` names the type before the argument list.
        m3 = re.compile(r"\s*[A-Za-z_][A-Za-z0-9_]*").match(body, i)
        if m3 and body[m3.end():m3.end() + 1] in "( {":
            i = m3.end()
        args = []
        while i < n and body[i] in " \t\r\n":
            i += 1
        if i < n and body[i] == '(':
            end = _match(body, i, '(', ')')
            args = split_top(body[i + 1:end - 1])
            i = end
        while i < n and body[i] in " \t\r\n":
            i += 1
        props = {}
        if i < n and body[i] == '{':
            end = _match(body, i, '{', '}')
            for part in split_top(body[i + 1:end - 1]):
                if '=' not in part:
                    continue
                k, v = part.split("=", 1)
                props[k.strip()] = v.strip()
            i = end
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


def num(expr, default=None):
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
    if expr in BALL:
        return BALL[expr]
    if default is not None:
        return default
    raise SystemExit("cannot evaluate C# value %r" % expr)


def shiny_of(props):
    v = props.get("Shiny", "")
    v = v.split(".")[-1]
    return SHINY.get(v.lower(), SHINY["random"])


def gender_of(props):
    if "Gender" not in props:
        return GENDER_ANY
    return num(props["Gender"], GENDER_ANY)


def nature_of(props):
    if "Nature" not in props:
        return NATURE_ANY
    v = props["Nature"].split(".")[-1]
    if v == "Random":
        return NATURE_ANY
    return NATURE[v]


def flags_of(props, extra=0):
    f = extra
    if num(props.get("FatefulEncounter", "0")):
        f |= F_FATEFUL
    if num(props.get("IsAlpha", "0")):
        f |= F_ALPHA
    if num(props.get("IsEgg", "0")):
        f |= F_EGG
    return f


# ---------------------------------------------------------------------------
# Row assembly
# ---------------------------------------------------------------------------
FIELDS = ("species", "location", "eggLocation", "form", "levelMin", "levelMax",
          "versions", "kind", "shiny", "gender", "fixedBall", "flawlessIVs",
          "nature", "flags")


def row(species, location, form, lvmin, lvmax, versions, kind,
        shiny=0, gender=GENDER_ANY, ball=0, flawless=0, nature=NATURE_ANY,
        flags=0, egg_location=0):
    # PKHeX spells "no fixed gender" as both 3 (the Gender enum's Random) and 0xFF
    # (FixedGenderUtil.GenderRandom), and the raid blobs store gender biased by +1 so
    # that 0 can mean "any" -- which underflows to -1. One sentinel out the far side.
    if not 0 <= gender <= 2:
        gender = GENDER_ANY
    return (species, location, egg_location, form, lvmin, lvmax, versions,
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
    acc, plain = {}, set()
    for r in rows:
        if r[FIELDS.index("kind")] not in mergeable:
            plain.add(r)
            continue
        d = dict(zip(FIELDS, r))
        key = (d["species"], d["location"], d["eggLocation"], d["form"], d["kind"],
               d["shiny"], d["gender"], d["fixedBall"], d["nature"], d["flags"],
               d["versions"])
        prev = acc.get(key)
        if prev is None:
            acc[key] = [d["levelMin"], d["levelMax"], d["flawlessIVs"]]
        else:
            prev[0] = min(prev[0], d["levelMin"])
            prev[1] = max(prev[1], d["levelMax"])
            prev[2] = min(prev[2], d["flawlessIVs"])

    versions = {}
    for key, (lo, hi, fl) in acc.items():
        rest = key[:-1]
        versions[rest + (lo, hi, fl)] = versions.get(rest + (lo, hi, fl), 0) | key[-1]

    out = list(plain)
    for key, ver in versions.items():
        sp, loc, egg, form, kind, shiny, gender, ball, nature, flags, lo, hi, fl = key
        out.append((sp, loc, egg, form, lo, hi, ver, kind, shiny, gender, ball, fl,
                    nature, flags))
    out.sort()
    return out


# ---------------------------------------------------------------------------
# Wild slots, per game
# ---------------------------------------------------------------------------
def wild_frlg():
    out = []
    for fname, ver in (("encounter_fr.pkl", 1), ("encounter_lg.pkl", 2)):
        for a in bin32(_load(WILD("Gen3", fname))):
            loc, body = a[0], a[4:]
            for i in range(0, len(body) - 9, 10):
                sp, form, _slot, mn, mx = struct.unpack_from("<HBBBB", body, i)
                out.append(row(sp, loc, form, mn, mx, ver, "wild"))
    return out


def wild_gg():
    out = []
    for fname, ver in (("encounter_gp.pkl", 1), ("encounter_ge.pkl", 2)):
        for a in bin32(_load(WILD("Gen7", fname))):
            # A Let's Go area feeds up to two neighbours; a catch there can be met in
            # either (PKHeX EncounterArea7b.IsMatchLocation), so emit a row for each.
            locs = {a[0]} | {a[2], a[3]} - {0}
            body = a[4:]
            for i in range(0, len(body) - 3, 4):
                sp, _flags, mn, mx = struct.unpack_from("<BBBB", body, i)
                for loc in locs:
                    out.append(row(sp, loc, 0, mn, mx, ver, "wild"))
    return out


def _swsh_wander(text):
    """EncounterArea8.GetAreasCanWanderTo -> {location: [reachable locations]}."""
    m = re.search(r"GetAreasCanWanderTo\(byte location\)\s*=>\s*location switch\s*\{", text)
    body = text[m.end():_match(text, text.index("{", m.end() - 1), '{', '}') - 1]
    out = {}
    for src, dsts in re.findall(r"(\d+)\s*=>\s*\[([^\]]*)\]", body):
        out[int(src)] = [int(x) for x in re.findall(r"\d+", dsts)]
    return out


def _swsh_wild_area(loc):
    """EncounterArea8.IsWildArea -- levels there are boosted to 60 post-game."""
    return (122 <= loc <= 154) or (164 <= loc <= 194) or (204 <= loc <= 234 and loc != 206)


def wild_swsh():
    text = strip_comments(read_cs("Legality/Encounters/Templates/Gen8/EncounterArea8.cs"))
    wander = _swsh_wander(text)
    out = []
    for fname, ver, crossover in (("encounter_sw_symbol.pkl", 1, True),
                                  ("encounter_sh_symbol.pkl", 2, True),
                                  ("encounter_sw_hidden.pkl", 1, False),
                                  ("encounter_sh_hidden.pkl", 2, False)):
        for a in bin32(_load(WILD("Gen8", fname))):
            home = a[0]
            # Only visible ("symbol") encounters roam between areas.
            locs = [home] + (wander.get(home, []) if crossover else [])
            slot_count, ofs, ctr = a[1], 2, 0
            while ctr != slot_count:
                _weather, mn, mx, count, _type = struct.unpack_from("<HBBBB", a, ofs)
                ofs += 6
                for _ in range(count):
                    v = struct.unpack_from("<H", a, ofs)[0]
                    ofs += 2
                    ctr += 1
                    for loc in locs:
                        flags = F_BOOST60 if _swsh_wild_area(loc) else 0
                        out.append(row(v & 0x3FF, loc, v >> 11, mn, mx, ver, "wild",
                                       flags=flags))
    return out


# EncounterArea8b.CanCrossoverTo -- the only surf pairs whose slot lists differ.
BDSP_SURF_CROSS = {486: 167, 167: 486, 420: 489, 489: 420}
BDSP_SURF = 2  # SlotType8b.Surf


def wild_bdsp():
    out = []
    for fname, ver in (("encounter_bd.pkl", 1), ("encounter_sp.pkl", 2),
                       ("encounter_bd_underground.pkl", 1),
                       ("encounter_sp_underground.pkl", 2)):
        for a in bin32(_load(WILD("Gen8", fname))):
            home = struct.unpack_from("<H", a, 0)[0]
            locs = [home]
            if a[2] == BDSP_SURF and home in BDSP_SURF_CROSS:
                locs.append(BDSP_SURF_CROSS[home])
            body = a[4:]
            for i in range(0, len(body) - 3, 4):
                v, mn, mx = struct.unpack_from("<HBB", body, i)
                for loc in locs:
                    out.append(row(v & 0x3FF, loc, v >> 11, mn, mx, ver, "wild"))
    return out


def wild_pla():
    out = []
    for a in bin32(_load(WILD("Gen8", "encounter_la.pkl"))):
        count = a[0]
        locs = list(a[1:1 + count])
        align = count + 1
        align += align & 1          # the slot block is 2-byte aligned
        b = a[align:]
        slot_count, body = b[1], b[2:]
        for i in range(slot_count):
            sp, form, alpha, mn, mx, gender, flawless = struct.unpack_from("<HBBBBBB", body, i * 8)
            # PLA slots are the ONE source that spells "no gender restriction" as 2, because
            # they store PKHeX's `Gender` enum, where `Random = Genderless = 2` -- and
            # EncounterSlot8a checks `Gender is not Gender.Random`. Every other blob uses
            # FixedGenderUtil.GenderRandom (0xFF) and means genderless by 2. Reading 2 as
            # genderless here would put a gender lock on 7130 of the 7132 PLA slots.
            if gender == 2:
                gender = GENDER_ANY
            flags = F_ALPHA if alpha else 0
            for loc in locs:
                out.append(row(sp, loc, form, mn, mx, 1, "wild", gender=gender,
                               flawless=flawless, flags=flags))
    return out


def wild_sv():
    out = []
    for a in bin32(_load(WILD("Gen9", "encounter_wild_paldea.pkl"))):
        loc = a[0] if a[2] == 0 else a[2]   # CrossFrom overrides the nominal area
        body = a[4:]
        for i in range(0, len(body) - 7, 8):
            sp, form, gender, mn, mx = struct.unpack_from("<HBBBB", body, i)
            out.append(row(sp, loc, wild_form(form), mn, mx, BOTH, "wild", gender=gender))

    # Mass outbreaks: one record, a base location and a 120-bit mask of offsets from it.
    data = _load(WILD("Gen9", "encounter_outbreak_paldea.pkl"))
    size = 0x1C
    for i in range(0, len(data) - size + 1, size):
        e = data[i:i + size]
        sp, form, gender, mn, mx = struct.unpack_from("<HBBBB", e, 0)
        shiny = SHINY["always"] if e[0x0B] else SHINY["random"]
        base = e[0x0C]
        mask = int.from_bytes(e[0x0C:0x1C], "little") >> 8
        for bit in range(128):
            if (mask >> bit) & 1:
                out.append(row(sp, base + bit, wild_form(form), mn, mx, BOTH, "wild",
                               gender=gender, shiny=shiny))
    return out


def wild_za():
    out = []
    for fname in ("encounter_za.pkl", "encounter_hyperspace_za.pkl"):
        for a in bin16(_load(WILD("Gen9", fname))):
            loc = struct.unpack_from("<H", a, 0)[0]
            body = a[4:]
            for i in range(0, len(body) - 7, 8):
                sp, form, gender, mn, mx, alpha, shiny = struct.unpack_from("<HBBBBBB", body, i)
                out.append(row(sp, loc, wild_form(form), mn, mx, 1, "wild", gender=gender,
                               shiny=shiny, flawless=3 if alpha else 0,
                               flags=F_ALPHA if alpha else 0))
    return out


# ---------------------------------------------------------------------------
# Statics / gifts / trades, parsed from the C# tables
# ---------------------------------------------------------------------------
def named_static(entries, versions, kind, default_form=0, location_override=None):
    """Rows for a `new(Version) { Species = .., Level = .., Location = .. }` table."""
    out = []
    for args, p in entries:
        ver = versions(args)
        sp = num(p["Species"])
        lvl = num(p["Level"])
        lvmax = num(p.get("LevelMax", str(lvl)))
        loc = location_override if location_override is not None else num(p["Location"])
        egg_loc = num(p.get("EggLocation", "0"))
        k = "gift" if (kind == "static" and num(p.get("FixedBall", "0"))) else kind
        flags = flags_of(p, F_EGG if egg_loc not in (0, BDSP_NONE) else 0)
        if loc == BDSP_NONE and egg_loc not in (0, BDSP_NONE):
            loc = EGG_RULES["BDSP"][2]      # the egg gift is met where it hatches
        out.append(row(sp, loc, num(p.get("Form", str(default_form))), lvl, lvmax,
                       ver, k, shiny=shiny_of(p), gender=gender_of(p),
                       ball=num(p.get("FixedBall", "0")),
                       flawless=num(p.get("FlawlessIVCount", "0")),
                       nature=nature_of(p), flags=flags, egg_location=egg_loc))
    return out


def version_mask(names, group):
    """Map C# GameVersion tokens (SW / SH / SWSH / ...) to this group's bit mask."""
    v0, v1 = GROUP_VERSIONS[group]
    m = 0
    for nm in names:
        nm = nm.strip().split(".")[-1]
        if nm == v0:
            m |= 1
        elif v1 and nm == v1:
            m |= 2
        elif nm == GROUP_ENUM[group] or nm == group:
            m |= BOTH if v1 else 1
    return m or (BOTH if v1 else 1)


def static_frlg():
    src = "Legality/Encounters/Data/Gen3/Encounters3FRLG.cs"
    text = strip_comments(read_cs(src))
    assert_all_arrays_used(src, ["StaticFRLG", "StaticFR", "StaticLG",
                                 "TradeGift_FRLG", "TradeGift_FR", "TradeGift_LG"])
    out = []
    for name in ("StaticFRLG", "StaticFR", "StaticLG"):
        for args, p in parse_array(text, name):
            sp, lvl = num(args[0]), num(args[1])
            ver = version_mask([args[2]], "FRLG")
            is_egg = num(p.get("IsEgg", "0"))
            ball = num(p.get("FixedBall", "0"))
            out.append(row(sp, num(p["Location"]), num(p.get("Form", "0")), lvl, lvl,
                           ver, "gift" if ball else "static", ball=ball,
                           flags=flags_of(p, F_EGG if is_egg else 0)))
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
        for args, p in parse_array(text, name):
            ver = version_mask([args[2]], "FRLG")
            out.append(row(num(args[4]), LINK_TRADE_NPC_3, 0, num(args[5]), 100,
                           ver, "trade", gender=gender_of(p)))
    return out


def static_gg():
    src = "Legality/Encounters/Data/Gen7/Encounters7GG.cs"
    text = strip_comments(read_cs(src))
    assert_all_arrays_used(src, ["Encounter_GG", "StaticGP", "StaticGE",
                                 "TradeGift_GG", "TradeGift_GP", "TradeGift_GE"])
    out = []
    for name in ("Encounter_GG", "StaticGP", "StaticGE"):
        out += named_static(parse_array(text, name),
                            lambda a: version_mask([a[0]], "GG"), "static")
    for name in ("TradeGift_GG", "TradeGift_GP", "TradeGift_GE"):
        for args, p in parse_array(text, name):
            out.append(row(num(p["Species"]), LINK_TRADE_NPC_6, num(p.get("Form", "0")),
                           num(p["Level"]), num(p["Level"]), version_mask([args[0]], "GG"),
                           "trade", gender=gender_of(p)))
    return out


def static_swsh():
    src = "Legality/Encounters/Data/Gen8/Encounters8.cs"
    text = strip_comments(read_cs(src))
    assert_all_arrays_used(src, ["StaticSWSH", "StaticSW", "StaticSH",
                                 "TradeSWSH", "TradeSW", "TradeSH"])
    out = []
    for name, ver in (("StaticSWSH", BOTH), ("StaticSW", 1), ("StaticSH", 2)):
        out += named_static(parse_array(text, name), lambda a, v=ver: v, "static")
    # Trades: the two overloads differ only in whether arg 0 is the shared name table.
    for name in ("TradeSWSH", "TradeSW", "TradeSH"):
        for args, p in parse_array(text, name):
            off = 0 if args[0] == "TradeOT_R1" else 1
            ver = version_mask([args[1 + off]], "SWSH")
            sp, lvl = num(args[2 + off]), num(args[3 + off])
            out.append(row(sp, LINK_TRADE_NPC_6, num(p.get("Form", "0")), lvl, lvl,
                           ver, "trade", gender=gender_of(p), nature=nature_of(p),
                           flawless=num(p.get("FlawlessIVCount", "0"))))
    return out + raids_swsh()


# EncounterStatic8N.LevelCaps, indexed [rank*2] = min, [rank*2+1] = max.
NEST_LEVEL_CAPS = [15, 20, 25, 30, 35, 40, 45, 50, 55, 60]
SHARED_NEST_MIN_LEVEL = 20
DIST_INDEX_MIN_DLC1 = 25
DIST_INDEX_MIN_DLC2 = 40


def _nest_locations(text):
    m = re.search(r"GetNestLocations\(byte nestIndex\)\s*=>\s*nestIndex switch\s*\{", text)
    body = text[m.end():_match(text, text.index("{", m.end() - 1), '{', '}') - 1]
    out = {}
    for idx, locs in re.findall(r"(\d+)\s*=>\s*\[([^\]]*)\]", body):
        out[int(idx)] = [int(x) for x in re.findall(r"\d+", locs)]
    return out


def raids_swsh():
    src = "Legality/Encounters/Data/Gen8/Encounters8Nest.cs"
    text = strip_comments(read_cs(src))
    assert_all_arrays_used(src, ["Crystal_SWSH"])
    nests = _nest_locations(text)
    out = []

    # Regular raid dens: 10-byte records, level range from the den's star ranks.
    for fname, ver in (("encounter_sw_nest.pkl", 1), ("encounter_sh_nest.pkl", 2)):
        data = _load(WILD("Gen8", fname))
        for i in range(0, len(data) - 9, 10):
            e = data[i:i + 10]
            sp, form, gender = struct.unpack_from("<HBB", e, 0)
            nest, min_rank, max_rank, flawless = e[6], e[7], e[8], e[9]
            lo = NEST_LEVEL_CAPS[min_rank * 2]
            hi = NEST_LEVEL_CAPS[max_rank * 2 + 1]
            for loc in nests.get(nest, []):
                out.append(row(sp, loc, form, lo, hi, ver, "raid",
                               gender=gender, flawless=flawless))
            # A den entered from someone else's raid reports the shared-nest location
            # and may be down-levelled to 20.
            out.append(row(sp, SHARED_NEST_8, form, min(lo, SHARED_NEST_MIN_LEVEL), hi,
                           ver, "raid", gender=gender, flawless=flawless))

    # Distribution raids: the den is anonymous, so any Wild Area of the right tier.
    for fname, ver in (("encounter_sw_dist.pkl", 1), ("encounter_sh_dist.pkl", 2)):
        data = _load(WILD("Gen8", fname))
        for i in range(0, len(data) - 15, 16):
            e = data[i:i + 16]
            sp, form = struct.unpack_from("<HB", e, 0)[0], e[2]
            lvl, index = e[12], e[15]
            f = e[14]
            flawless = f & 0xF
            shiny = {1: SHINY["never"], 2: SHINY["always"]}.get(f >> 4, SHINY["random"])
            region = (LOC_SWSH_WA_ALL if index >= DIST_INDEX_MIN_DLC2 else
                      LOC_SWSH_WA_IOA if index >= DIST_INDEX_MIN_DLC1 else LOC_SWSH_WA_BASE)
            lo = min(lvl, SHARED_NEST_MIN_LEVEL)
            for loc in (region, SHARED_NEST_8):
                out.append(row(sp, loc, form, lo, lvl, ver, "raid",
                               shiny=shiny, flawless=flawless))

    # Dynamax Adventures (Max Lair): fixed location, fixed level 70.
    data = _load(WILD("Gen8", "encounter_swsh_underground.pkl"))
    for i in range(0, len(data) - 13, 14):
        e = data[i:i + 14]
        sp = struct.unpack_from("<H", e, 0)[0]
        out.append(row(sp, MAX_LAIR_8, e[2], 70, 70, BOTH, "raid",
                       shiny=SHINY["never"], flawless=4))

    # Event "crystal" raids are a plain C# list, all met at the shared nest.
    for args, p in parse_array(text, "Crystal_SWSH"):
        lvl = num(p["Level"])
        out.append(row(num(p["Species"]), SHARED_NEST_8, num(p.get("Form", "0")),
                       min(lvl, SHARED_NEST_MIN_LEVEL), lvl,
                       version_mask([args[0]], "SWSH"), "raid",
                       flawless=num(p.get("FlawlessIVCount", "0"))))
    return out


def static_bdsp():
    src = "Legality/Encounters/Data/Gen8/Encounters8b.cs"
    text = strip_comments(read_cs(src))
    assert_all_arrays_used(src, ["Encounter_BDSP", "StaticBD", "StaticSP", "TradeGift_BDSP"])
    out = []
    for name in ("Encounter_BDSP", "StaticBD", "StaticSP"):
        out += named_static(parse_array(text, name),
                            lambda a: version_mask([a[0]], "BDSP"), "static")
    for args, p in parse_array(text, "TradeGift_BDSP"):
        lvl = num(p["Level"])
        out.append(row(num(p["Species"]), LINK_TRADE_NPC_6, 0, lvl, lvl,
                       version_mask([args[2]], "BDSP"), "trade",
                       gender=gender_of(p), nature=nature_of(p)))
    return out


def static_pla():
    src = "Legality/Encounters/Data/Gen8/Encounters8a.cs"
    text = strip_comments(read_cs(src))
    assert_all_arrays_used(src, ["StaticLA"])
    out = []
    for args, p in parse_array(text, "StaticLA"):
        sp, form, lvl = num(args[0]), num(args[1]), num(args[2])
        lvmax = num(p.get("LevelMax", str(lvl)))
        ball = num(p.get("FixedBall", "0"))
        out.append(row(sp, num(p["Location"]), form, lvl, lvmax, 1,
                       "gift" if ball else "static", shiny=shiny_of(p),
                       gender=gender_of(p), ball=ball,
                       flawless=num(p.get("FlawlessIVCount", "0")),
                       flags=flags_of(p)))
    return out


def static_sv():
    src = "Legality/Encounters/Data/Gen9/Encounters9.cs"
    text = strip_comments(read_cs(src))
    assert_all_arrays_used(src, ["Encounter_SV", "StaticSL", "StaticVL", "TradeGift_SV"])
    out = []
    # Scarlet and Violet each add version-exclusive statics on top of the shared list.
    for name in ("Encounter_SV", "StaticSL", "StaticVL"):
        out += named_static(parse_array(text, name),
                            lambda a: version_mask([a[0]], "SV"), "static")
    for args, p in parse_array(text, "TradeGift_SV"):
        lvl = num(args[4])
        out.append(row(num(args[3]), LINK_TRADE_NPC_6, num(p.get("Form", "0")), lvl, lvl,
                       version_mask([args[2]], "SV"), "trade", shiny=shiny_of(p),
                       gender=gender_of(p), nature=nature_of(p),
                       ball=num(p.get("FixedBall", "0"))))
    return out + raids_sv()


def raids_sv():
    """Tera raids (base + both DLC maps), distribution, Mightiest, and fixed spawns."""
    out = []

    def gem_row(e, size):
        sp, form = struct.unpack_from("<H", e, 0)[0], e[2]
        gender = e[3] - 1                      # stored +1 so 0 can mean "any"
        flawless, lvl = e[5], e[7]
        shiny = {1: SHINY["never"], 2: SHINY["always"]}.get(e[6], SHINY["random"])
        return row(sp, TERA_CAVERN_9, wild_form(form), lvl, lvl, BOTH, "raid",
                   shiny=shiny, gender=gender, flawless=flawless)

    for fname, size in (("encounter_gem_paldea.pkl", 0x18),
                        ("encounter_gem_kitakami.pkl", 0x18),
                        ("encounter_gem_blueberry.pkl", 0x18),
                        ("encounter_dist_paldea.pkl", 0x14 + (2 * 2 * 4 * 2) + 10),
                        ("encounter_might_paldea.pkl", 0x14 + (2 * 2 * 4 * 2) + 10)):
        data = _load(WILD("Gen9", fname))
        for i in range(0, len(data) - size + 1, size):
            out.append(gem_row(data[i:i + size], size))

    # Fixed spawns: up to four met locations per record, 0 = slot unused.
    data = _load(WILD("Gen9", "encounter_fixed_paldea.pkl"))
    for i in range(0, len(data) - 0x13, 0x14):
        e = data[i:i + 0x14]
        sp, form, lvl, flawless = struct.unpack_from("<HBBB", e, 0)
        gender = e[6]
        for loc in (e[0x10], e[0x11], e[0x12], e[0x13]):
            if loc:
                out.append(row(sp, loc, form, lvl, lvl, BOTH, "static",
                               gender=GENDER_ANY if gender > 2 else gender,
                               flawless=flawless))
    return out


def static_za():
    src = "Legality/Encounters/Data/Gen9/Encounters9a.cs"
    text = strip_comments(read_cs(src))
    assert_all_arrays_used(src, ["Gifts", "Static", "Trades"])
    out = []
    for name, kind in (("Gifts", "gift"), ("Static", "static")):
        for args, p in parse_array(text, name):
            sp, form, lvl = num(args[0]), num(args[1]), num(args[2])
            # Z-A gifts are always handed over in a Poke Ball (EncounterGift9a.FixedBall).
            ball = BALL["Poke"] if kind == "gift" else 0
            # Both Z-A tables default to Shiny.Never unless the entry says otherwise.
            out.append(row(sp, num(p["Location"]), form, lvl, lvl, 1, kind,
                           shiny=shiny_of(p) if "Shiny" in p else SHINY["never"],
                           gender=gender_of(p), ball=ball,
                           flawless=num(p.get("FlawlessIVCount", "0")),
                           nature=nature_of(p), flags=flags_of(p)))
    for args, p in parse_array(text, "Trades"):
        sp, form, lvl = num(args[2]), num(args[3]), num(args[4])
        out.append(row(sp, LINK_TRADE_NPC_6, form, lvl, lvl, 1, "trade",
                       gender=gender_of(p), nature=nature_of(p),
                       flawless=num(p.get("FlawlessIVCount", "0"))))
    return out


WILD_SOURCES = {"FRLG": wild_frlg, "GG": wild_gg, "SWSH": wild_swsh, "BDSP": wild_bdsp,
                "PLA": wild_pla, "SV": wild_sv, "ZA": wild_za}
STATIC_SOURCES = {"FRLG": static_frlg, "GG": static_gg, "SWSH": static_swsh,
                  "BDSP": static_bdsp, "PLA": static_pla, "SV": static_sv,
                  "ZA": static_za}


# ---------------------------------------------------------------------------
# Breedable species (PKHeX Breeding.IsAbleToHatchFromEgg)
# ---------------------------------------------------------------------------
def unbreedable_species():
    text = strip_comments(read_cs("Legality/Breeding.cs"))
    m = re.search(r"IsAbleToHatchFromEgg\(ushort species\)\s*=>\s*species switch\s*\{", text)
    if not m:
        raise SystemExit("Breeding.IsAbleToHatchFromEgg not found")
    body = text[m.end():_match(text, text.index("{", m.end() - 1), '{', '}') - 1]
    out = set()
    for arm in body.split(","):
        if "=> false" not in arm:
            continue
        for nm in re.findall(r"\(int\)([A-Za-z0-9_]+)", arm):
            if nm not in SPECIES:
                raise SystemExit("unknown species token %r in Breeding.cs" % nm)
            out.add(SPECIES[nm])
    if len(out) < 80:
        raise SystemExit("Breeding.cs parse looks wrong (%d species)" % len(out))
    return out


def breedable_bitset(game, present):
    """Species bitset: can a Pokemon in this game have come from a daycare egg?"""
    blocked = unbreedable_species()
    bits = bytearray((MAX_SPECIES + 8) // 8)
    for sp in range(1, MAX_SPECIES + 1):
        if sp in blocked or sp not in present[game]:
            continue
        bits[sp >> 3] |= 1 << (sp & 7)
    return bytes(bits)


def _permitted_bytes(relpath, name):
    text = strip_comments(read_cs(relpath))
    m = re.search(r"ReadOnlySpan<byte>\s+" + name + r"\s*=>\s*\[(.*?)\];", text, re.S)
    if not m:
        raise SystemExit("%s not found in %s" % (name, relpath))
    return [int(x) for x in re.findall(r"\d+", m.group(1))]


def hatch_mask(game):
    """Per-location version mask: which games' eggs may hatch at this met location?

    An egg hatches wherever the player is walking, so "the" hatch location is a set, and
    PKHeX keeps one per game with three different encodings. They are normalised here into
    one byte per location id, bit 0 / bit 1 matching this table's version bits, so the
    matcher is a single indexed test instead of three special cases.
    """
    if game == "FRLG":
        # Fixed, not a set -- one entry, both versions.
        mask = bytearray(HATCH_LOCATION_FRLG + 1)
        mask[HATCH_LOCATION_FRLG] = BOTH
        return bytes(mask)
    if game == "SWSH":
        # EggHatchLocation8: indexed by location >> 1, and every ODD location is invalid.
        src = _permitted_bytes("Legality/Encounters/Verifiers/EggHatchLocation8.cs",
                               "LocationPermitted8")
        mask = bytearray(len(src) * 2)
        for i, v in enumerate(src):
            if v:
                mask[i * 2] = BOTH
        return bytes(mask)
    if game in ("BDSP", "SV"):
        # Already one byte per location with bit 0 / bit 1 = the two versions, in the same
        # order as this table's version bits (BD/SP, SL/VL).
        relpath, name = {
            "BDSP": ("Legality/Encounters/Verifiers/EggHatchLocation8b.cs", "LocationPermitted8b"),
            "SV":   ("Legality/Encounters/Verifiers/EggHatchLocation9.cs", "LocationPermitted9"),
        }[game]
        return bytes(_permitted_bytes(relpath, name))
    return b""


def present_species():
    """Species each game contains, from the personal tables gen_learnsets already reads."""
    from gen_learnsets import GAMES_TO_EMIT, Personal, Personal3
    pers = {g: Personal(g) for g in GAMES_TO_EMIT if g != "FRLG"}
    pers["FRLG"] = Personal3()
    return {g: {sp for sp in range(1, MAX_SPECIES + 1) if pers[g].present(sp, 0)}
            for g in GAMES}


# ---------------------------------------------------------------------------
# Emit
# ---------------------------------------------------------------------------
HEADER = '''/**
 * EncounterTable.h - Per-game encounter templates for Layer 3 legality.
 *
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
        ENC_KIND_WILD   = 0,
        ENC_KIND_STATIC = 1,
        ENC_KIND_GIFT   = 2,
        ENC_KIND_TRADE  = 3,
        ENC_KIND_RAID   = 4,
    };

    /// Shiny policy of a template -- Never is a shiny LOCK, Always a forced shiny.
    enum EncounterShiny : uint8_t {
        ENC_SHINY_RANDOM = 0,
        ENC_SHINY_NEVER  = 1,
        ENC_SHINY_ALWAYS = 2,
    };

    enum EncounterFlag : uint8_t {
        ENC_FLAG_FATEFUL = 1 << 0,  ///< sets the fateful-encounter (event) bit
        ENC_FLAG_ALPHA   = 1 << 1,  ///< Legends Alpha (PLA / Z-A)
        ENC_FLAG_BOOST60 = 1 << 2,  ///< SW/SH Wild Area: post-game boosts levels to 60
        ENC_FLAG_EGG     = 1 << 3,  ///< handed over as an egg
    };

    constexpr uint8_t ENC_FORM_ANY   = 0xFF;  ///< slot randomises the form
    constexpr uint8_t ENC_GENDER_ANY = 0xFF;
    constexpr uint8_t ENC_NATURE_ANY = 0xFF;

    /// Locations that stand for a whole region rather than one place. SW/SH
    /// distribution raids are the only user: the den is anonymous, so any Wild Area
    /// location of the right DLC tier is a legal met location.
    constexpr uint16_t ENC_LOC_SWSH_WILDAREA     = 0xFFF0;  ///< Galar Wild Area
    constexpr uint16_t ENC_LOC_SWSH_WILDAREA_IOA = 0xFFF1;  ///< + Isle of Armor
    constexpr uint16_t ENC_LOC_SWSH_WILDAREA_ALL = 0xFFF2;  ///< + Crown Tundra

    struct EncounterRow {
        uint16_t species;
        uint16_t location;     ///< met location, in the ORIGIN game's namespace
        uint16_t eggLocation;  ///< 0 unless the template hands over an egg
        uint8_t  form;         ///< ENC_FORM_ANY when the slot randomises it
        uint8_t  levelMin;
        uint8_t  levelMax;
        uint8_t  versions;     ///< bit 0 / bit 1 = the group's first / second game
        uint8_t  kind;         ///< EncounterKind
        uint8_t  shiny;        ///< EncounterShiny
        uint8_t  gender;       ///< 0 male, 1 female, 2 genderless, ENC_GENDER_ANY
        uint8_t  fixedBall;    ///< 0 = the encounter does not fix the ball
        uint8_t  flawlessIVs;  ///< guaranteed count of 31 IVs
        uint8_t  nature;       ///< ENC_NATURE_ANY unless the template forces one
        uint8_t  flags;        ///< EncounterFlag bits
    };

    /// Where a game's eggs come from and where they may hatch.
    struct EggRule {
        uint16_t eggLocation;     ///< nursery id stamped into the egg-location field.
                                  ///< 0 means the FORMAT has no such field (Gen 3 alone),
                                  ///< which is why an egg there has to be inferred instead.
        uint16_t eggLocationAlt;  ///< id a TRADED egg carries instead (0 = none)
        /// Per-location version mask: bit 0 / bit 1 set when an egg may hatch at that met
        /// location in the group's first / second game. An egg hatches wherever the player
        /// is walking, so this is a permitted SET, not one place -- Gen 3 is the exception
        /// and gets a one-entry mask, because its met location is stamped on receipt.
        /// nullptr when the game has no breeding.
        const uint8_t* hatchLocations;
        uint16_t hatchLocationCount;
        uint8_t  hatchLevel;      ///< met level of a hatched egg
        uint8_t  hasBreeding;     ///< 0 for the games with no daycare at all
    };

    struct EncounterTable {
        const EncounterRow* rows;
        uint32_t rowCount;
        /// speciesIndex[s] .. speciesIndex[s + 1] bounds the rows for species s.
        const uint16_t* speciesIndex;
        uint16_t speciesIndexLen;
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


def emit_bytes(lines, decl, blob):
    lines.append('        const uint8_t %s[%d] = {' % (decl, len(blob)))
    for i in range(0, len(blob), 16):
        lines.append('            ' + ' '.join('0x%02X,' % b for b in blob[i:i + 16]))
    lines.append('        };')


def emit_cpp(tables, indices, breedable, hatch):
    L = []
    L.append('/**')
    L.append(' * EncounterTable.cpp - Per-game encounter templates.')
    L.append(' *')
    L.append(' * Auto-generated by tools/gen_encounters.py. DO NOT EDIT BY HAND.')
    L.append(' */')
    L.append('#include "Legality/EncounterTable.h"')
    L.append('')
    L.append('namespace Legality {')
    L.append('')
    L.append('    namespace {')
    for game in GAMES:
        rows = tables[game]
        L.append('        const EncounterRow ROWS_%s[%d] = {' % (game, len(rows)))
        for r in rows:
            L.append('            {%4d,%6d,%6d,%4d,%4d,%4d,%2d,%2d,%2d,%4d,%3d,%2d,%4d,%2d},'
                     % r)
        L.append('        };')
        L.append('        const uint16_t IDX_%s[%d] = {' % (game, SPECIES_INDEX_LEN))
        idx = indices[game]
        for i in range(0, len(idx), 16):
            L.append('            ' + ' '.join('%5d,' % v for v in idx[i:i + 16]))
        L.append('        };')
        if game in breedable:
            emit_bytes(L, 'BREED_%s' % game, breedable[game])
        if hatch.get(game):
            emit_bytes(L, 'HATCH_%s' % game, hatch[game])
        L.append('')

    for game in GAMES:
        eggloc, eggalt, hatchlvl = EGG_RULES.get(game, (0, 0, 0))
        has = 0 if game in NO_BREEDING else 1
        hatchName = 'HATCH_%s' % game if hatch.get(game) else 'nullptr'
        hatchLen = len(hatch.get(game, b''))
        L.append('        const EncounterTable TABLE_%s = {' % game)
        L.append('            ROWS_%s, %d, IDX_%s, %d,' % (game, len(tables[game]), game,
                                                           SPECIES_INDEX_LEN))
        L.append('            %s,' % ('BREED_%s' % game if game in breedable else 'nullptr'))
        L.append('            { %d, %d, %s, %d, %d, %d },' % (eggloc, eggalt, hatchName,
                                                              hatchLen, hatchlvl, has))
        L.append('        };')
    L.append('    }')
    L.append('')
    L.append('    const EncounterTable* getEncounterTable(Enums::GameVersion versionOrGroup) {')
    L.append('        switch (versionOrGroup) {')
    for game in GAMES:
        v0, v1 = GROUP_VERSIONS[game]
        names = [GROUP_ENUM[game], v0] + ([v1] if v1 else [])
        for nm in dict.fromkeys(names):
            L.append('            case Enums::GameVersion::%s:' % nm)
        L.append('                return &TABLE_%s;' % game)
    L.append('            default:')
    L.append('                return nullptr;')
    L.append('        }')
    L.append('    }')
    L.append('')
    L.append('    uint8_t encounterVersionBit(Enums::GameVersion version) {')
    L.append('        switch (version) {')
    for game in GAMES:
        v0, v1 = GROUP_VERSIONS[game]
        L.append('            case Enums::GameVersion::%s: return 1;' % v0)
        if v1:
            L.append('            case Enums::GameVersion::%s: return 2;' % v1)
    L.append('            default: return 0;')
    L.append('        }')
    L.append('    }')
    L.append('')
    L.append('    bool encounterLocationMatches(uint16_t rowLocation, uint16_t metLocation) {')
    L.append('        switch (rowLocation) {')
    L.append('            // EncounterArea8.IsWildArea8 / Armor / Crown -- Freezington (206)')
    L.append('            // is a town inside the Crown Tundra range and holds no dens.')
    L.append('            case ENC_LOC_SWSH_WILDAREA:')
    L.append('                return metLocation >= 122 && metLocation <= 154;')
    L.append('            case ENC_LOC_SWSH_WILDAREA_IOA:')
    L.append('                return (metLocation >= 122 && metLocation <= 154)')
    L.append('                    || (metLocation >= 164 && metLocation <= 194);')
    L.append('            case ENC_LOC_SWSH_WILDAREA_ALL:')
    L.append('                return (metLocation >= 122 && metLocation <= 154)')
    L.append('                    || (metLocation >= 164 && metLocation <= 194)')
    L.append('                    || (metLocation >= 204 && metLocation <= 234 && metLocation != 206);')
    L.append('            default:')
    L.append('                return rowLocation == metLocation;')
    L.append('        }')
    L.append('    }')
    L.append('}')
    L.append('')
    return "\n".join(L)


def build_index(rows):
    """speciesIndex[s] = first row with species >= s (rows are sorted by species)."""
    idx = [0] * SPECIES_INDEX_LEN
    pos = 0
    for sp in range(SPECIES_INDEX_LEN):
        while pos < len(rows) and rows[pos][0] < sp:
            pos += 1
        idx[sp] = pos
    return idx


def main():
    present = present_species()
    tables, indices, breedable, hatch = {}, {}, {}, {}
    total = 0
    for game in GAMES:
        rows = WILD_SOURCES[game]() + STATIC_SOURCES[game]()
        bad = [r for r in rows if not (0 < r[0] <= MAX_SPECIES)]
        if bad:
            raise SystemExit("%s: %d rows with an out-of-range species (e.g. %r)"
                             % (game, len(bad), bad[0]))
        rows = merge_rows(rows)
        if len(rows) > 0xFFFF:
            raise SystemExit("%s: %d rows overflows the uint16 species index" % (game, len(rows)))
        tables[game] = rows
        indices[game] = build_index(rows)
        if game not in NO_BREEDING:
            breedable[game] = breedable_bitset(game, present)
            hatch[game] = hatch_mask(game)
        total += len(rows)
        kinds = {}
        for r in rows:
            kinds[r[FIELDS.index("kind")]] = kinds.get(r[FIELDS.index("kind")], 0) + 1
        summary = " ".join("%s=%d" % (k, kinds.get(v, 0)) for k, v in KIND.items())
        print("  %-5s rows %6d   %s" % (game, len(rows), summary))

    with open(OUT_H, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(HEADER)
    print("Wrote", OUT_H)
    with open(OUT_CPP, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(emit_cpp(tables, indices, breedable, hatch))
    print("Wrote", OUT_CPP)

    row_bytes = total * 16
    idx_bytes = len(GAMES) * SPECIES_INDEX_LEN * 2
    breed_bytes = sum(len(b) for b in breedable.values())
    hatch_bytes = sum(len(b) for b in hatch.values())
    tot = row_bytes + idx_bytes + breed_bytes + hatch_bytes
    print("  rows %d B  indices %d B  breedable %d B  hatch %d B"
          % (row_bytes, idx_bytes, breed_bytes, hatch_bytes))
    print("  TABLE TOTAL: %d B (%.1f KB)" % (tot, tot / 1024))


if __name__ == "__main__":
    main()
