#!/usr/bin/env python3
"""Generate include/Pokemon/LearnsetTable.h + src/Pokemon/LearnsetTable.cpp
from PKHeX's binary learnset resources (every supported game group).

PKSE needs a fast set-membership query to highlight legal moves in
the move picker and for legality Layer-2 (move learnability):
    isLearnable(species, form, group, moveId) -> bool

For each (species, form) present in a game, this table stores a *bitset* over move
ids: bit M is set iff that species+form can legally KNOW move M in that game. The
bit pool is the UNION of every learn method PKHeX models for that exact species+
form -- mirroring each LearnSource's GetAllMoves at max level:

    level-up  U  shared egg  U  TM/TR/HM  U  tutor  U  reminder  U  enhanced-tutor

That per-species pool is then UNIONED WITH ITS WHOLE PRE-EVOLUTION CHAIN, because a
Pokemon keeps the moves it knew before evolving: a Raichu may legally know Double
Kick even though only Pikachu learns it. The chain comes from PKHeX's evolution
binaries (evos_*.pkl, reverse lineage per EvolutionReversePersonal); ancestors that
are absent from the game are skipped, since you cannot have evolved from one there.

This fold happens HERE, at generation time, not at runtime -- the pool is not
deduplicated, so every row already owns its own bitset and the extra bits are free.
Getting it wrong is not merely cosmetic: isLearnable() also drives the cross-game
transfer sanitizer in Conversion::convert(), which CLEARS any move it reports as
unlearnable, so a missing pre-evolution move is silently deleted off a legitimate
Pokemon. (The reverse direction is a Bad Egg, so the sanitizer cannot just be
loosened -- the pool has to actually be right.)

--------------------------------------------------------------------------------
Per-game assembly (PKHeX.Core/Legality/LearnSource/Sources/LearnSource*.GetAllMoves
+ the matching PersonalInfo*.cs bitflag layout). Move-permit lists are parsed from
the C# source; TM/tutor flags are read from each game's personal binary.

  GG (7GG)   lvlmove_gg + TM(60 flags@0x28 x MachineMoves[LearnSource7GG])
             + partner Pikachu(25,f8)/Eevee(133,f1) tutor moves. No eggs.
  SWSH       lvlmove_swsh + eggmove_swsh(shared, via HatchSpecies@0x56)
             + TM(100@0x28 x MachineMovesTechnical) + TR(100@0x3C x MachineMovesRecord)
             + TypeTutor(8 bits@0x38 x TypeTutorMoves) + TutorSpecial(18@0xA8 x
             SpecialTutorMoves) + enhanced(Rotom/Necrozma) + Calyrex(898,f0)->Agility.
  BDSP       lvlmove_bdsp + eggmove_bdsp(shared, indexed by HatchSpecies@0x3E)
             + TM(100@0x28 x MachineMoves) + TypeTutor(4 bits@0x38 x TypeTutorMoves)
             + enhanced(Rotom).
  PLA (8LA)  lvlmove_la + mastery_la + MoveShop(61-bit ulong@0xA8 x MoveShopMoves)
             + enhanced(Rotom). No eggs. (mastery_la proven subset of level-up U
             move-shop, so it is a redundant no-op; included for fidelity.)
  SV (9SV)   lvlmove_sv + eggmove_sv(shared, via HatchSpecies@0x24)
             + TM(230@0x2C x MachineMoves) + reminder_sv + enhanced(Rotom/Necrozma).
  ZA (9ZA)   lvlmove_za + plus_za + TM(flags@0x2C x MachineMoves9ZA, 160 defined of a
             230-bit field). No eggs. (plus_za proven subset of level-up U TM per
             LegendsZAVerifier.CanLearnMovePlus, which validates Plus moves against
             TM+level-up; the plus_za learnset itself grants no new moves. Redundant
             no-op, included for fidelity.)

Binary formats (little-endian):
  * BinLinkerAccessor16 container: [2-byte ASCII magic][u16 count][u16 offset_i...],
    entry i = data[offset_i:offset_{i+1}]. Arrays indexed by that game's
    PersonalTable.GetFormIndex(species, form) -- the same FormIndex/HasForm
    redirection gen_personal.py replicates (verified vs PersonalInfo.FormIndex).
    Exception: eggmove_bdsp is indexed by raw species id (PKHeX optimization).
  * Level-up / mastery / plus entry: move[count] (u16) then level[count] (u8),
    count = len/3. We take ALL moves (ignore the level cap).
  * Egg / reminder entry: a flat u16[] move list.
  * TM/TR/tutor flags live in the personal entry (offsets above).

Enhanced tutor: Rotom(479) forms 1..5 -> Overheat/HydroPump/Blizzard/AirSlash/
LeafStorm; Necrozma(800) form 1 -> SunsteelStrike, form 2 -> MoongeistBeam.

Max move id per game (PKHeX Legal.cs): GG 742, SWSH/BDSP 826, PLA 850, SV 919
(MalignantChain), ZA 920 (NihilLight). Combined max = 920. All bitsets use a single
UNIFORM width sized to hold move id 920 (LEARN_BITSET_BYTES), so the move picker can
iterate the full move space against any game's bitset without a per-game bound.

--------------------------------------------------------------------------------
Row layout mirrors PersonalInfoTable exactly: rows 0..1025 are the form-0 entries
(indexed by National Dex id), rows 1026.. are alternate forms, addressed via a
formIndex redirection (form N -> LEARN_FORM_INDEX[species] + N - 1). The union form
count per species is the max FormCount across all six games, so a LearnsetTable row
index and a PersonalInfoTable row index are identical for any (species, form).

Storage (per-game bitsets, design (a)): one flat byte pool per game holds the
concatenated bitsets of the species+forms present in that game; a per-row u16 block
index (0xFFFF = absent) maps a row to its bitset (byte offset = block * width).

Regenerate with:  python tools/gen_learnsets.py
Pulls every PKHeX resource + source file it reads from GitHub on demand
(tools/pkhex_source.py); no local PKHeX checkout required.
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pkhex_source import pkhex_path  # noqa: E402


def BYTE(*parts):
    """PKHeX.Core/Resources/byte/<...> -> local (fetched) path."""
    return pkhex_path("Resources/byte/" + "/".join(parts))


def MISC(*parts):
    """PKHeX.Core/Resources/legality/misc/<...> -> local (fetched) path."""
    return pkhex_path("Resources/legality/misc/" + "/".join(parts))


def CORE(relpath):
    """PKHeX.Core/<relpath> -> local (fetched) path."""
    return pkhex_path(relpath)


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_H = os.path.join(ROOT, "include", "Pokemon", "LearnsetTable.h")
OUT_CPP = os.path.join(ROOT, "src", "Pokemon", "LearnsetTable.cpp")
# Species names for the generated comments come from PKHeX, like every other name this script
# reads -- NOT from PKSE's own SpeciesNames.cpp. A generator that reads another generator's
# OUTPUT breaks the moment that output is reshaped, and says nothing useful when it does.
SPECIES_NAMES_SRC = "Resources/text/other/en/text_Species_en.txt"

MAX_SPECIES = 1025            # National Dex #1025 Pecharunt (PKHeX MaxSpeciesID_9)
BASE_ROWS = MAX_SPECIES + 1   # indices 0..1025
MELTAN, MELMETAL, PIKACHU, EEVEE = 808, 809, 25, 133
ROTOM, NECROZMA, CALYREX = 479, 800, 898

# Uniform bitset width: hold move ids 0..MAX_MOVE_ID (920 = ZA's NihilLight).
MAX_MOVE_ID = 920
WIDTH = (MAX_MOVE_ID + 8) // 8  # 116 bytes

# Games in presence-bit order (matches PersonalInfoTable's PersonalGameBit).
GAMES = ["GG", "SWSH", "BDSP", "PLA", "SV", "ZA", "DP", "PT", "HGSS", "BW", "B2W2",
         "XY", "ORAS", "SM", "USUM"]
# Every supported game. GSC/FRLG/RBY are assembled without a Personal helper (their tables predate
# per-game presence flags entirely), so they sit outside GAMES.
GAMES_TO_EMIT = GAMES + ["GSC", "FRLG", "RSE", "RBY"]

# Personal binary format per game: (filename, SIZE, formIdx_off, formCnt_off,
# maxspecies, present_kind). Mirrors gen_personal.py's FMT.
PERSONAL_FMT = {
    "GG":   ("personal_gg",   0x54, 0x1C, 0x20, 809,  "gg"),
    "SWSH": ("personal_swsh", 0xB0, 0x1E, 0x20, 898,  "flag21"),
    "BDSP": ("personal_bdsp", 0x44, 0x1E, 0x20, 493,  "bdsp"),
    "PLA":  ("personal_la",   0xB0, 0x1E, 0x20, 905,  "flag21"),
    "SV":   ("personal_sv",   0x50, 0x18, 0x1A, 1025, "flag1C"),
    "ZA":   ("personal_za",   0x50, 0x18, 0x1A, 1000, "flag1C"),
    # Gens 4-7 predate the per-game presence flag: a species is in the game iff its id is within
    # that game's dex, which is what "dex" means below. Note SM stops at 802 and USUM at 807 --
    # collapsing those to one number quietly grants Sun/Moon six Pokemon it does not have.
    "DP":   ("personal_dp",   0x2C, 0x2A, 0x29, 493,  "dex"),
    "PT":   ("personal_pt",   0x2C, 0x2A, 0x29, 493,  "dex"),
    "HGSS": ("personal_hgss", 0x2C, 0x2A, 0x29, 493,  "dex"),
    "BW":   ("personal_bw",   0x3C, 0x1C, 0x20, 649,  "dex"),
    "B2W2": ("personal_b2w2", 0x4C, 0x1C, 0x20, 649,  "dex"),
    "XY":   ("personal_xy",   0x40, 0x1C, 0x20, 721,  "dex"),
    "ORAS": ("personal_ao",   0x50, 0x1C, 0x20, 721,  "dex"),
    "SM":   ("personal_sm",   0x54, 0x1C, 0x20, 802,  "dex"),
    "USUM": ("personal_uu",   0x54, 0x1C, 0x20, 807,  "dex"),
}

# Enums::GameVersion values that map to each game group (group + individual ids).
GAME_VERSION_IDS = {
    "GG":   ["GG", "GP", "GE"],
    "SWSH": ["SWSH", "SW", "SH"],
    "BDSP": ["BDSP", "BD", "SP"],
    "PLA":  ["PLA"],
    "SV":   ["SV", "SL", "VL"],
    "ZA":   ["ZA"],
    "FRLG": ["FRLG", "FR", "LG"],
    "RSE":  ["RSE", "RU", "SA", "EM"],
    "RBY": ["RBY", "RD", "GN", "BU", "YW"],
    "GSC":  ["GSC", "GD", "SI", "C"],
    "DP":   ["DP", "D", "P"],
    "PT":   ["PT", "Pt"],
    "HGSS": ["HGSS", "HG", "SS"],
    "BW":   ["BW", "B", "W"],
    "B2W2": ["B2W2", "B2", "W2"],
    "XY":   ["XY", "X", "Y"],
    "ORAS": ["ORAS", "OR", "AS"],
    "SM":   ["SM", "SN", "MN"],
    "USUM": ["USUM", "US", "UM"],
}


# ----------------------------------------------------------------------------
# C# source + binary parsing
# ----------------------------------------------------------------------------
def _load(path):
    with open(path, "rb") as fh:
        return fh.read()


def load_move_ids():
    """Parse PKHeX's Move enum (Game/Enums/Move.cs) into a name -> id dict."""
    path = CORE("Game/Enums/Move.cs")
    with open(path, encoding="utf-8") as fh:
        text = fh.read()
    body = text[text.index("{", text.index("enum Move")):]
    names = re.findall(r'^\s*([A-Za-z_][A-Za-z0-9_]*)\s*,', body, re.M)
    if "MalignantChain" not in names:
        raise SystemExit("Move.cs parse failed")
    return {n: i for i, n in enumerate(names)}


MOVE = load_move_ids()


def load_species_ids():
    """Parse PKHeX's Species enum (Game/Enums/Species.cs) into a name -> id dict.

    The identifier pattern is Unicode-aware because the enum contains Flabébé: an ASCII-only
    pattern skips that member and numbers every species above #669 one too low, with nothing to
    say so. Anchored at both ends of the range for the same reason.
    """
    path = CORE("Game/Enums/Species.cs")
    with open(path, encoding="utf-8") as fh:
        text = fh.read()
    body = text[text.index("{", text.index("enum Species")):]
    names = re.findall(r'^\s*([^\W\d]\w*)\s*,', body, re.M)
    species_ids = {name: index for index, name in enumerate(names)}
    if species_ids.get("Flabébé") != 669 or species_ids.get("Pecharunt") != 1025:
        raise SystemExit("Species.cs parse failed")
    return species_ids


SPECIES = load_species_ids()

# THE TABLE WHOSE IsPresentInGame EACH GEN 4-7 GAME ASKS. One class serves every Gen 4 game and
# both pairs of Gen 7 games, which is PKHeX's own arrangement.
COSMETIC_FORM_TABLE = {"DP": "PersonalTable4", "PT": "PersonalTable4", "HGSS": "PersonalTable4",
                       "BW": "PersonalTable5BW", "B2W2": "PersonalTable5B2W2",
                       "XY": "PersonalTable6XY", "ORAS": "PersonalTable6AO",
                       "SM": "PersonalTable7", "USUM": "PersonalTable7"}


def load_cosmetic_forms(table_name):
    """{species: {form, ...}} that a Gen 4-7 table counts as present without an entry of its own.

    A form that shares its species' stats entry has no HasForm, so each of these tables lists them
    by hand in IsPresentInGame's species switch -- Unown's letters, Burmy's cloaks, Shellos' seas,
    Deerling's seasons, the Flabébé line's flowers. Without the list they read as absent, and every
    evolution edge between two of them was dropped: a Blue Flower Florges walked down to a RED
    Flabébé and was "never" met on Route 7, where X and Y both have blue ones.

    Parsed rather than transcribed. An arm in a shape this does not know raises, because reading it
    as "no forms" is the silent failure this exists to remove.
    """
    path = CORE("PersonalInfo/Table/%s.cs" % table_name)
    with open(path, encoding="utf-8") as fh:
        text = fh.read()
    switch_start = text.index("return species switch", text.index("public bool IsPresentInGame"))
    body = text[text.index("{", switch_start) + 1:text.index("};", switch_start)]
    cosmetic_forms = {}
    for line in body.splitlines():
        line = re.sub(r"//.*", "", line).strip().rstrip(",").strip()
        if not line or line == "_ => false":
            continue
        arm = re.fullmatch(r"(\(int\)[^\W\d]\w*(?:\s+or\s+\(int\)[^\W\d]\w*)*)\s*=>\s*form\s+(.+)", line)
        if not arm:
            raise SystemExit("%s: unrecognised IsPresentInGame arm %r" % (table_name, line))
        condition = arm.group(2).strip()
        below = re.fullmatch(r"<\s*(\d+)", condition)
        at_most = re.fullmatch(r"<=\s*(\d+)", condition)
        exactly = re.fullmatch(r"==\s*(\d+)", condition)
        below_except = re.fullmatch(r"is\s*<\s*(\d+)\s+and\s+not\s+(\d+)", condition)
        if below:
            forms = set(range(1, int(below.group(1))))
        elif at_most:
            forms = set(range(1, int(at_most.group(1)) + 1))
        elif exactly:
            forms = {int(exactly.group(1))}
        elif below_except:
            forms = set(range(1, int(below_except.group(1)))) - {int(below_except.group(2))}
        else:
            raise SystemExit("%s: unrecognised IsPresentInGame condition %r" % (table_name, condition))
        for species_name in re.findall(r"\(int\)([^\W\d]\w*)", arm.group(1)):
            if species_name not in SPECIES:
                raise SystemExit("%s: unknown species %s" % (table_name, species_name))
            cosmetic_forms[SPECIES[species_name]] = forms
    if not cosmetic_forms:
        raise SystemExit("%s: IsPresentInGame listed no forms" % table_name)
    return cosmetic_forms


def parse_move_list(cs_relpath, array_name, expect=None):
    """Parse a `<array_name> => [ ... ];` list of move ids from a C# source file.
    Accepts both numeric literals and `(int)Move.Name` tokens (comments stripped)."""
    with open(CORE(cs_relpath), encoding="utf-8") as fh:
        text = fh.read()
    m = re.search(re.escape(array_name) + r"\s*=>\s*\[(.*?)\];", text, re.S)
    if not m:
        raise SystemExit(f"{array_name} not found in {cs_relpath}")
    seg = re.sub(r"//[^\n]*", "", m.group(1))
    out = []
    for name, num in re.findall(r"\(int\)Move\.([A-Za-z0-9_]+)|(\d+)", seg):
        out.append(MOVE[name] if name else int(num))
    if expect is not None and len(out) != expect:
        raise SystemExit(f"{cs_relpath}:{array_name} has {len(out)}, expected {expect}")
    return out


def bin_entries(data):
    """Unpack a BinLinkerAccessor16 container into a list of byte spans."""
    length = struct.unpack_from("<H", data, 2)[0]
    return [data[s:e] for (s, e) in
            (struct.unpack_from("<HH", data, 4 + i * 2) for i in range(length))]


def read_levelup(path):
    """Level-up / mastery / plus blob -> per-index list of move ids (levels ignored)."""
    res = []
    for e in bin_entries(_load(path)):
        if len(e) == 0:
            res.append([])
            continue
        count = len(e) // 3
        res.append(list(struct.unpack_from("<%dH" % count, e, 0)))
    return res


def read_movesource(path):
    """Egg / reminder blob -> per-index list of move ids."""
    res = []
    for e in bin_entries(_load(path)):
        n = len(e) // 2
        res.append(list(struct.unpack_from("<%dH" % n, e, 0)) if n else [])
    return res


# ----------------------------------------------------------------------------
# Personal table (form-index redirection + per-game presence), mirrors gen_personal
# ----------------------------------------------------------------------------
class Personal:
    def __init__(self, key):
        fn, size, fi, fc, maxsp, pk = PERSONAL_FMT[key]
        self.key = key
        self.raw = _load(BYTE("personal", fn))
        self.SIZE, self.fi, self.fc, self.maxsp, self.pk = size, fi, fc, maxsp, pk
        if len(self.raw) % size != 0:
            raise SystemExit(f"{key}: {len(self.raw)} not a multiple of SIZE {size}")
        self.count = len(self.raw) // size
        self.cosmetic_forms = load_cosmetic_forms(COSMETIC_FORM_TABLE[key]) if pk == "dex" else {}

    def ent(self, idx):
        return self.raw[idx * self.SIZE:(idx + 1) * self.SIZE]

    def u16(self, e, off):
        return struct.unpack_from("<H", e, off)[0]

    def form_stats_index(self, sp):
        return self.u16(self.ent(sp), self.fi)

    def form_count(self, sp):
        return self.ent(sp)[self.fc]

    def has_form(self, sp, form):
        return form != 0 and self.form_stats_index(sp) > 0 and form < self.form_count(sp)

    def form_index(self, sp, form):
        if sp > self.maxsp or sp >= self.count:
            return 0
        if not self.has_form(sp, form):
            return sp
        return self.form_stats_index(sp) + form - 1

    def present(self, sp, form):
        if sp > self.maxsp:
            return False
        if self.pk == "gg":
            if not ((1 <= sp <= 151) or sp in (MELTAN, MELMETAL)):
                return False
            if form == 0:
                return True
            if sp == PIKACHU:
                return form == 8
            return self.has_form(sp, form)
        if self.pk in ("bdsp", "dex"):
            if form == 0 or self.has_form(sp, form):
                return True
            return form in self.cosmetic_forms.get(sp, ())
        if form != 0 and not self.has_form(sp, form):
            return False
        e = self.ent(self.form_index(sp, form))
        if self.pk == "flag1C":
            return e[0x1C] != 0
        return ((e[0x21] >> 6) & 1) == 1


# ----------------------------------------------------------------------------
# Gen 3 (FireRed/LeafGreen) personal table -- shaped unlike every other game's
# ----------------------------------------------------------------------------
# Three things differ from Gen 5+ and each one is a trap:
#   1. The table is FLAT and indexed by NATIONAL species. Gen 3 saves store an INTERNAL
#      species id, but PKHeX pre-converts to national ordering when building the binary,
#      so no SpeciesConverter is needed here (only when reading a save).
#   2. There are NO per-form entries -- PersonalTable3.GetFormIndex(species, form) is just
#      `species`, so every form of a species shares one move pool.
#   3. TM/HM compatibility is NOT in the personal entry. It lives in a separate blob
#      (hmtm_g3.pkl) that uses the **u32-offset** BinLinkerAccessor, not the u16 one every
#      other resource uses. 58 bits per species: TM01-50 at bits 0-49, HM01-08 at 50-57.
# ----------------------------------------------------------------------------
# Gen 1 (Red/Blue/Yellow)
# ----------------------------------------------------------------------------
G1_MAX_SPECIES = 151
G1_PERSONAL_SIZE = 0x1C
G1_TMHM_OFF = 0x14          # PersonalInfo1.TMHM
G1_COUNT_TMHM = 55          # 50 TMs + 5 HMs, one bit each
G1_MOVES_OFF = 0x0F         # PersonalInfo1.Move1..Move4

# PersonalInfo1.MachineMoves -- the move each TM/HM bit stands for, in bit order.
G1_MACHINE_MOVES = [
    5, 13, 14, 18, 25, 92, 32, 34, 36, 38,
    61, 55, 58, 59, 63, 6, 66, 68, 69, 99,
    72, 76, 82, 85, 87, 89, 90, 91, 94, 100,
    102, 104, 115, 117, 118, 120, 121, 126, 129, 130,
    135, 138, 143, 156, 86, 149, 153, 157, 161, 164,
    15, 19, 57, 70, 148,     # the five HMs
]

G3_MAX_SPECIES = 386
G3_PERSONAL_SIZE = 0x1C
G3_COUNT_TM = 50

# PersonalInfo3.MachineMovesTechnical / MachineMovesHidden, in TM/HM bit order.
G3_TM_MOVES = [
    264, 337, 352, 347,  46,  92, 258, 339, 331, 237,
    241, 269,  58,  59,  63, 113, 182, 240, 202, 219,
    218,  76, 231,  85,  87,  89, 216,  91,  94, 247,
    280, 104, 115, 351,  53, 188, 201, 126, 317, 332,
    259, 263, 290, 156, 213, 168, 211, 285, 289, 315,
]
G3_HM_MOVES = [15, 19, 57, 70, 148, 249, 127, 291]


def bin_entries32(data):
    """Unpack a u32-offset BinLinkerAccessor container (hmtm_g3.pkl only)."""
    length = struct.unpack_from("<H", data, 2)[0]
    return [data[s:e] for (s, e) in
            (struct.unpack_from("<II", data, 4 + i * 4) for i in range(length))]


class Personal2:
    """Gen 2 personal table. Same surface as Personal1/Personal3 so the evolution fold works
    unchanged. Gen 2 has no alternate forms, so the form index is the identity.

    personal_c is used rather than personal_gs: PKSE has ONE Gen 2 group, Crystal is the superset
    (its table carries the three Move Tutor bits Gold/Silver has nothing at), and the two are
    otherwise identical for everything read here.
    """

    SIZE = 0x20
    MAX_SPECIES = 251

    def __init__(self):
        self.raw = _load(BYTE("personal", "personal_c"))
        self.maxsp = self.MAX_SPECIES
        self.count = len(self.raw) // self.SIZE

    def form_count(self, sp):
        return 1

    def form_index(self, sp, form):
        return sp

    def present(self, sp, form):
        return form == 0 and 1 <= sp <= self.MAX_SPECIES


class Personal1:
    """Gen 1 personal table. Same surface as Personal3 so the evolution fold works unchanged.

    Two Gen 1 quirks matter here. The TM/HM flags live INSIDE the personal entry (0x14, 55
    bits) rather than in a separate blob as in Gen 3. And the four INITIAL moves are in the
    personal entry too (0x0F), not in the level-up learnset -- lvlmove_rb.pkl for Bulbasaur
    starts at level 7, so Tackle and Growl appear nowhere else. Leaving them out would make
    every Gen 1 Pokemon's starting moves read as unlearnable, and the transfer sanitizer
    DELETES anything isLearnable() denies.
    """

    def __init__(self, fn="personal_rb"):
        self.raw = _load(BYTE("personal", fn))
        self.maxsp = G1_MAX_SPECIES
        self.count = len(self.raw) // G1_PERSONAL_SIZE

    def _entry(self, sp):
        if sp >= self.count:
            return b""
        return self.raw[sp * G1_PERSONAL_SIZE:(sp + 1) * G1_PERSONAL_SIZE]

    # Gen 1 has no forms at all, so the form index is the identity and there is exactly one
    # "form" to walk -- the same shape Personal3 uses for its species-indexed evolution data.
    def form_count(self, sp):
        return 1

    def form_index(self, sp, form):
        return sp

    def present(self, sp, form):
        return form == 0 and 1 <= sp <= G1_MAX_SPECIES

    def tm_bit(self, sp, i):
        e = self._entry(sp)
        if i >= G1_COUNT_TMHM or len(e) < G1_TMHM_OFF + ((G1_COUNT_TMHM + 7) // 8):
            return False
        return bool((e[G1_TMHM_OFF + (i >> 3)] >> (i & 7)) & 1)

    def initial_moves(self, sp):
        e = self._entry(sp)
        if len(e) < G1_MOVES_OFF + 4:
            return []
        return [m for m in e[G1_MOVES_OFF:G1_MOVES_OFF + 4] if m]


class Personal3:
    """Gen 3 personal table + TM/HM flags. Exposes the same surface the rest of this
    script expects from `Personal`, so read_evolutions() and the fold work unchanged."""

    def __init__(self, fn="personal_fr"):
        self.raw = _load(BYTE("personal", fn))
        self.maxsp = G3_MAX_SPECIES
        self.count = len(self.raw) // G3_PERSONAL_SIZE
        self.tmhm = bin_entries32(_load(BYTE("personal", "hmtm_g3.pkl")))

    # Gen 3 evolution data is species-indexed too (PKHeX EvolutionTree.Evolves3 uses
    # GetViaSpecies, not GetViaPersonal), so the form index is the identity and there is
    # exactly one "form" to walk when building the reverse lineage.
    def form_count(self, sp):
        return 1

    def form_index(self, sp, form):
        return sp

    def present(self, sp, form):
        if sp == 0 or sp > G3_MAX_SPECIES:
            return False
        if form == 0:
            return True
        # PersonalTable3.IsPresentInGame -- only these three have forms in Gen 3.
        return (sp == 201 and form < 28) or (sp == 351 and form < 4) or (sp == 386 and form < 4)

    def tm_bit(self, sp, i):
        e = self.tmhm[sp] if sp < len(self.tmhm) else b""
        return bool((e[i >> 3] >> (i & 7)) & 1) if (i >> 3) < len(e) else False


# ----------------------------------------------------------------------------
# Evolution lineage (reverse map), for folding pre-evolution moves into the pool
# ----------------------------------------------------------------------------
# evos_<key>.pkl is the same BinLinkerAccessor16 container as the learnset blobs,
# indexed by the game's PersonalTable.GetFormIndex(species, form). Each entry is an
# array of 8-byte EvolutionMethod records (PKHeX EvolutionSet.GetMethod):
#     [0] type   [2:4] arg   [4:6] DESTINATION species   [6] dest form   [7] level
# Form 0xFF is PKHeX's AnyForm sentinel = "evolves into the same form it already is".
EVO_RESOURCE = {"GG": "gg", "SWSH": "ss", "BDSP": "bs", "PLA": "la", "SV": "sv", "ZA": "za",
                "FRLG": "g3", "RSE": "g3", "RBY": "g1", "GSC": "g2",
                # One evolution binary per GENERATION for Gens 4-7, shared by the games in it.
                # evos_uu covers both Sun/Moon and Ultra Sun/Moon.
                "DP": "g4", "PT": "g4", "HGSS": "g4",
                "BW": "g5", "B2W2": "g5",
                "XY": "g6", "ORAS": "g6",
                "SM": "uu", "USUM": "uu"}
# THE PERSONAL TABLE AN EVOLUTION BINARY IS ADDRESSED BY IS NOT ALWAYS THE GAME'S OWN.
# evos_uu.pkl serves Sun/Moon as well as Ultra Sun/Moon, and PKHeX pairs it with
# PersonalTable.USUM for BOTH (EvolutionTree.Evolves7 = GetViaPersonal(PersonalTable.USUM, ...)).
# Sun/Moon's own table stops at species 802 where Ultra's stops at 807, so its alternate-form
# region sits at different indices -- form 0 still lands on the species (index == species id) and
# is fine, but every OTHER form reads a row belonging to a different Pokemon. That shipped as
# twelve wrong edges in the SM lineage, one per Alolan form: "Alolan Ninetales evolved from Alolan
# Golem", "Alolan Golem evolved from Greninja". Layer 3 then walked to the wrong ancestor and
# reported a legitimately evolved Alolan form as having no encounter at all.
# A game absent from this map is addressed by its own table, which is the common case.
EVO_INDEX_GAME = {"SM": "USUM"}

# PKHeX reads the Gen 1-6 evolution binaries BY SPECIES (EvolutionTree.GetViaSpecies) and the
# Gen 7+ ones by the personal table's FORM INDEX (GetViaPersonal). Reading one with the other does
# not fail -- it lands on a different species' entry for every alternate form, which is how a row
# saying "Alolan Golem evolved from Greninja" gets into a table and stays there.
EVO_SPECIES_INDEXED = {"g1", "g2", "g3", "g4", "g5", "g6"}

EVO_ANY_FORM = 0xFF
EVO_SIZE = 8
MAX_EVO_DEPTH = 4   # longest real chain is 3 (e.g. Bulbasaur->Ivysaur->Venusaur); +1 slack


class ReverseLineage(dict):
    """{(dest_sp, dest_form): (src_sp, src_form)}, plus a level floor per destination.

    A plain dict everywhere it is read as a lineage, so every existing caller is unchanged.
    `levels[(dest_sp, dest_form)]` is the lowest CURRENT level at which that destination can
    exist having evolved from the source recorded here -- 0 when the evolution is not gated on
    a level at all (a stone, a trade, friendship).
    """

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.levels = {}


def read_evolutions(key, personal, index_personal=None):
    """evos_<key>.pkl -> ReverseLineage {(dest_sp, dest_form): (src_sp, src_form)}.

    Mirrors PKHeX EvolutionReversePersonal.GetLineage: walk every source species+form,
    read the evolutions it leads TO, and register the reverse edge. Where a destination
    has more than one registered source we keep the FIRST, matching PKHeX's
    GetPreEvolutions ("no convergent evolutions; first method is enough").

    An 8-byte EvolutionMethod is `method u16, argument u16, species u16, form u8, level u8`
    (PKHeX EvolutionSet.GetMethod). THE LEVEL IS THE MINIMUM ACROSS EVERY METHOD reaching the
    same destination from the kept source, never the first: a species with both a level-up and
    a stone route to one destination is not level-gated at all, and taking the first method
    would invent a floor the games do not impose.

    `personal` is the GAME's table: which species and forms exist, and which forms to walk.
    `index_personal` is the table the BINARY is addressed by, which is not always the same one --
    see EVO_INDEX_GAME. It defaults to `personal`, so every game but Sun/Moon is unaffected.
    """
    if index_personal is None:
        index_personal = personal
    path = BYTE("evolve", "evos_%s.pkl" % key)
    entries = bin_entries(_load(path))
    rev = ReverseLineage()
    last = min(personal.maxsp, personal.count - 1)
    # A GEN 1-6 BINARY IS ADDRESSED BY SPECIES, AND EVERY FORM READS THAT ONE ENTRY -- PKHeX's
    # EvolutionReverseSpecies walks each source form against entries[species], resolving AnyForm to
    # the form being walked. Addressing it by form index sent every alternate form past the end of
    # the file, so only form 0 of a form-keeping line had an ancestor: a Blue Flower Florges walked
    # down to a RED Flabebe, and a Florges met on Route 7 -- where Y has blue ones -- read as a
    # species that is never there.
    speciesIndexed = key in EVO_SPECIES_INDEXED
    for sp in range(1, last + 1):
        for form in range(personal.form_count(sp)):
            idx = sp if speciesIndexed else index_personal.form_index(sp, form)
            if idx >= len(entries):
                continue
            e = entries[idx]
            for off in range(0, len(e) - (EVO_SIZE - 1), EVO_SIZE):
                dsp = struct.unpack_from("<H", e, off + 4)[0]
                if dsp == 0:
                    break
                dform = e[off + 6]
                if dform == EVO_ANY_FORM:
                    dform = form
                destination = (dsp, dform)
                rev.setdefault(destination, (sp, form))
                if rev[destination] != (sp, form):
                    continue  # a different source won this destination; its level is the one that counts
                level = e[off + 7]
                previous = rev.levels.get(destination)
                rev.levels[destination] = level if previous is None else min(previous, level)
    return rev


def preevo_chain(rev, sp, form):
    """Full pre-evolution chain for (sp, form), nearest ancestor first."""
    out = []
    cur = (sp, form)
    seen = {cur}
    while len(out) < MAX_EVO_DEPTH:
        prev = rev.get(cur)
        if prev is None or prev in seen:   # `seen` guards against a cyclic data error
            break
        out.append(prev)
        seen.add(prev)
        cur = prev
    return out


# ----------------------------------------------------------------------------
# Per-game learnable-pool assemblers (each == that game's LearnSource.GetAllMoves)
# ----------------------------------------------------------------------------
ROTOM_FORM_MOVE = {1: MOVE["Overheat"], 2: MOVE["HydroPump"], 3: MOVE["Blizzard"],
                   4: MOVE["AirSlash"], 5: MOVE["LeafStorm"]}
SUNSTEEL, MOONGEIST, AGILITY = MOVE["SunsteelStrike"], MOVE["MoongeistBeam"], MOVE["Agility"]


def _tmbit(e, base, i):
    return (e[base + (i >> 3)] >> (i & 7)) & 1


KELDEO = 647
MELOETTA = 648


def _add_rotom(pool, sp, form):
    if sp == ROTOM and form in ROTOM_FORM_MOVE:
        pool.add(ROTOM_FORM_MOVE[form])


def _add_necrozma(pool, sp, form):
    if sp == NECROZMA and form == 1:
        pool.add(SUNSTEEL)
    elif sp == NECROZMA and form == 2:
        pool.add(MOONGEIST)


def _hatch_form_everstone(p, e, local_off, regflag_off, regidx_off):
    local = p.u16(e, local_off) & 0xFF
    regf = p.u16(e, regflag_off)
    regidx = p.u16(e, regidx_off) & 0xFF
    return regidx if (regf & 1) else local


def load_max_move_ids():
    """PKHeX Legality/Legal.cs MaxMoveID_* constants, resolving the ones defined as another
    constant or as a (int)Move.X cast."""
    with open(CORE("Legality/Legal.cs"), encoding="utf-8") as fh:
        text = fh.read()
    raw = dict(re.findall(r"const\s+\w+\s+(MaxMoveID_\w+)\s*=\s*([^;]+);", text))

    def resolve(name, depth=0):
        expr = raw[name].strip()
        if expr.isdigit():
            return int(expr)
        cast = re.fullmatch(r"\((?:int|ushort)\)Move\.(\w+)", expr)
        if cast:
            return MOVE[cast.group(1)]
        if expr in raw and depth < 8:
            return resolve(expr, depth + 1)
        raise SystemExit("Legal.cs: cannot resolve %s = %s" % (name, expr))

    return {name: resolve(name) for name in raw}


def parse_cs_move_names(relpath, array_name):
    """The `(ushort)Move` entries of a `<array_name> => [...]` span in a C# file, skipping
    commented-out entries."""
    with open(CORE(relpath), encoding="utf-8") as fh:
        text = fh.read()
    match = re.search(array_name + r"\s*=>\s*\[(.*?)\];", text, re.DOTALL)
    if not match:
        raise SystemExit("Could not find %s in %s" % (array_name, relpath))
    names = []
    for line in match.group(1).splitlines():
        names.extend(re.findall(r"\(\w+\)(\w+)", line.split("//", 1)[0]))
    if not names:
        raise SystemExit("%s in %s parsed empty" % (array_name, relpath))
    return names


# Sketch. Smeargle's move pool is not a learnset: it can know any move Sketch can copy in the game,
# and modelling only its level-up list reported every sketched move as unlearnable (and let the
# transfer sanitizer delete them). PKHeX MoveInfo.IsSketchValid: never Struggle or Chatter, never a
# Z-move, Max move or torque move (IsMoveKnowable), and the per-context exclusions of
# IsSketchPossible(move, context). Two deliberate departures, where the game wins over PKHeX's
# context-wide bound (GetMaxMoveID): the bound is the GAME's own max move id -- X/Y has no move past
# 617, Sun/Moon none past 719 and Sword/Shield none past 826, so nothing there can be sketched -- and
# Sword/Shield's dummied moves are excluded as BD/SP's and S/V's are, since a move no battle can use
# is not one Sketch can copy.
SMEARGLE = 235
SKETCH_CONTEXT = {
    "RBY": ("MaxMoveID_1", "Gen1"), "GSC": ("MaxMoveID_2", "Gen2"),
    "RSE": ("MaxMoveID_3", "Gen3"), "FRLG": ("MaxMoveID_3", "Gen3"),
    "DP": ("MaxMoveID_4", "Gen4"), "PT": ("MaxMoveID_4", "Gen4"), "HGSS": ("MaxMoveID_4", "Gen4"),
    "BW": ("MaxMoveID_5", "Gen5"), "B2W2": ("MaxMoveID_5", "Gen5"),
    "XY": ("MaxMoveID_6_XY", "Gen6"), "ORAS": ("MaxMoveID_6_AO", "Gen6"),
    "SM": ("MaxMoveID_7", "Gen7"), "USUM": ("MaxMoveID_7_USUM", "Gen7"),
    "GG": ("MaxMoveID_7b", "Gen7b"), "SWSH": ("MaxMoveID_8", "Gen8"),
    "PLA": ("MaxMoveID_8a", "Gen8a"), "BDSP": ("MaxMoveID_8b", "Gen8b"),
    "SV": ("MaxMoveID_9", "Gen9"), "ZA": ("MaxMoveID_9a", "Gen9a"),
}


def _is_move_knowable(move):
    """PKHeX MoveInfo.IsMoveKnowable: not a Z-move, Max move or torque move."""
    unknowable_ranges = (("BreakneckBlitzP", "Catastropika"), ("SinisterArrowRaid", "GenesisSupernova"),
                         ("TenMVoltThunderbolt", "TenMVoltThunderbolt"),
                         ("LightThatBurnstheSky", "ClangorousSoulblaze"), ("MaxFlare", "MaxSteelspike"),
                         ("BlazingTorque", "MagicalTorque"))
    return not any(MOVE[first] <= move <= MOVE[last] for first, last in unknowable_ranges)


_SKETCH_CACHE = {}


def sketch_pool(key):
    if key in _SKETCH_CACHE:
        return _SKETCH_CACHE[key]
    from gen_movepresence import dummied_set  # noqa: E402 -- same DummiedMoves parser
    max_name, context = SKETCH_CONTEXT[key]
    max_move = load_max_move_ids()[max_name]
    excluded = {MOVE["Struggle"], MOVE["Chatter"]}
    if context == "Gen2":
        excluded |= {MOVE[name] for name in ("SelfDestruct", "Explosion", "Mimic", "Metronome",
                                             "MirrorMove", "Transform", "SleepTalk")}
    elif context == "Gen6":
        excluded |= {MOVE["ThousandArrows"], MOVE["ThousandWaves"], MOVE["LightofRuin"]}
    elif context == "Gen7":
        excluded.add(MOVE["LightofRuin"])
    elif context == "Gen8":
        excluded.add(MOVE["LightofRuin"])
        excluded |= dummied_set("MoveInfo8.cs")
    elif context == "Gen8b":
        excluded |= dummied_set("MoveInfo8b.cs")
    elif context == "Gen9":
        excluded |= dummied_set("MoveInfo9.cs")
        excluded |= {MOVE[name] for name in parse_cs_move_names("Moves/MoveInfo.cs", "DisallowSketch9")}
    pool = {move for move in range(1, max_move + 1) if move not in excluded and _is_move_knowable(move)}
    _SKETCH_CACHE[key] = pool
    return pool


class Assembler:
    def __init__(self, key):
        self.p = Personal(key)

    def _levelup(self, path):
        lvl = read_levelup(path)
        if self.p.count != len(lvl):
            raise SystemExit(f"{self.p.key}: personal count {self.p.count} != levelup {len(lvl)}")
        return lvl


class GGAssembler(Assembler):
    def __init__(self):
        super().__init__("GG")
        self.lvl = self._levelup(BYTE("levelup", "lvlmove_gg.pkl"))
        cs = "Legality/LearnSource/Sources/LearnSource7GG.cs"
        self.machine = parse_move_list(cs, "MachineMoves", 60)
        self.pika = parse_move_list(cs, "TutorStarterPikachu", 3)
        self.eevee = parse_move_list(cs, "TutorStarterEevee", 8)

    def assemble(self, sp, form):
        idx = self.p.form_index(sp, form)
        e = self.p.ent(idx)
        pool = set(self.lvl[idx])
        for i in range(60):
            if _tmbit(e, 0x28, i):
                pool.add(self.machine[i])
        if sp == PIKACHU and form == 8:
            pool.update(self.pika)
        elif sp == EEVEE and form == 1:
            pool.update(self.eevee)
        return pool


class SWSHAssembler(Assembler):
    def __init__(self):
        super().__init__("SWSH")
        self.lvl = self._levelup(BYTE("levelup", "lvlmove_swsh.pkl"))
        self.egg = read_movesource(BYTE("eggmove", "eggmove_swsh.pkl"))
        cs = "PersonalInfo/Info/PersonalInfo8SWSH.cs"
        self.tech = parse_move_list(cs, "MachineMovesTechnical", 100)
        self.record = parse_move_list(cs, "MachineMovesRecord", 100)
        self.type_tutor = parse_move_list(cs, "TypeTutorMoves", 8)
        self.special_tutor = parse_move_list(cs, "SpecialTutorMoves", 18)

    def assemble(self, sp, form):
        p = self.p
        idx = p.form_index(sp, form)
        e = p.ent(idx)
        pool = set(self.lvl[idx])
        # Shared egg (HatchSpecies@0x56, HatchFormIndexEverstone from 0x58/0x5A/0x5E)
        hatch_sp = p.u16(e, 0x56)
        hatch_form = _hatch_form_everstone(p, e, 0x58, 0x5A, 0x5E)
        eidx = p.form_index(hatch_sp, hatch_form)
        if eidx < len(self.egg):
            pool.update(self.egg[eidx])
        for i in range(100):                    # TM @0x28
            if _tmbit(e, 0x28, i):
                pool.add(self.tech[i])
        for i in range(100):                    # TR @0x3C
            if _tmbit(e, 0x3C, i):
                pool.add(self.record[i])
        for i in range(8):                      # TypeTutor: 8 bits in byte 0x38
            if (e[0x38] >> i) & 1:
                pool.add(self.type_tutor[i])
        for i in range(18):                     # TutorSpecial (Armor) @0xA8, 3 bytes
            if _tmbit(e, 0xA8, i):
                pool.add(self.special_tutor[i])
        _add_rotom(pool, sp, form)
        _add_necrozma(pool, sp, form)
        if sp == CALYREX and form == 0:         # Agility Calyrex without TR glitch
            pool.add(AGILITY)
        return pool


class BDSPAssembler(Assembler):
    def __init__(self):
        super().__init__("BDSP")
        self.lvl = self._levelup(BYTE("levelup", "lvlmove_bdsp.pkl"))
        # eggmove_bdsp is indexed by raw species id (PKHeX optimization).
        self.egg = read_movesource(BYTE("eggmove", "eggmove_bdsp.pkl"))
        cs = "PersonalInfo/Info/PersonalInfo8BDSP.cs"
        self.machine = parse_move_list(cs, "MachineMoves", 100)
        self.type_tutor = parse_move_list(cs, "TypeTutorMoves", 4)

    def assemble(self, sp, form):
        p = self.p
        idx = p.form_index(sp, form)
        e = p.ent(idx)
        pool = set(self.lvl[idx])
        hatch_sp = p.u16(e, 0x3E)               # shared egg indexed by HatchSpecies
        if hatch_sp < len(self.egg):
            pool.update(self.egg[hatch_sp])
        for i in range(100):                    # TM @0x28
            if _tmbit(e, 0x28, i):
                pool.add(self.machine[i])
        for i in range(4):                      # TypeTutor: 4 bits in byte 0x38
            if (e[0x38] >> i) & 1:
                pool.add(self.type_tutor[i])
        _add_rotom(pool, sp, form)
        return pool


class PLAAssembler(Assembler):
    def __init__(self):
        super().__init__("PLA")
        self.lvl = self._levelup(BYTE("levelup", "lvlmove_la.pkl"))
        self.mastery = read_levelup(MISC("mastery_la.pkl"))
        self.moveshop = parse_move_list("PersonalInfo/Info/PersonalInfo8LA.cs",
                                        "MoveShopMoves", 61)

    def assemble(self, sp, form):
        p = self.p
        idx = p.form_index(sp, form)
        e = p.ent(idx)
        pool = set(self.lvl[idx])
        if idx < len(self.mastery):             # redundant subset, included for fidelity
            pool.update(self.mastery[idx])
        bits = struct.unpack_from("<Q", e, 0xA8)[0]  # MoveShop: 61-bit ulong
        for i in range(61):
            if (bits >> i) & 1:
                pool.add(self.moveshop[i])
        _add_rotom(pool, sp, form)
        return pool


class ZAAssembler(Assembler):
    def __init__(self):
        super().__init__("ZA")
        self.lvl = self._levelup(BYTE("levelup", "lvlmove_za.pkl"))
        self.plus = read_levelup(MISC("plus_za.pkl"))
        self.machine = parse_move_list("PersonalInfo/Info/PersonalInfo9ZA.cs",
                                       "MachineMoves", 160)  # 160 defined of a 230-bit field

    def assemble(self, sp, form):
        p = self.p
        idx = p.form_index(sp, form)
        e = p.ent(idx)
        pool = set(self.lvl[idx])
        if idx < len(self.plus):                # redundant subset, included for fidelity
            pool.update(self.plus[idx])
        for i in range(230):                    # TM @0x2C (only 0..159 ever set)
            if _tmbit(e, 0x2C, i):
                if i >= len(self.machine):
                    raise SystemExit(f"ZA TM bit {i} set but MachineMoves has {len(self.machine)}")
                pool.add(self.machine[i])
        return pool


class SVAssembler(Assembler):
    def __init__(self):
        super().__init__("SV")
        self.lvl = self._levelup(BYTE("levelup", "lvlmove_sv.pkl"))
        self.egg = read_movesource(BYTE("eggmove", "eggmove_sv.pkl"))
        self.rem = read_movesource(BYTE("personal", "reminder_sv.pkl"))
        self.machine = parse_move_list("PersonalInfo/Info/PersonalInfo9SV.cs",
                                       "MachineMoves", 230)

    def assemble(self, sp, form):
        p = self.p
        idx = p.form_index(sp, form)
        e = p.ent(idx)
        pool = set(self.lvl[idx])
        # Shared egg (HatchSpecies@0x24, HatchFormIndexEverstone from 0x26/0x28/0x2A)
        hatch_sp = p.u16(e, 0x24)
        hatch_form = _hatch_form_everstone(p, e, 0x26, 0x28, 0x2A)
        eidx = p.form_index(hatch_sp, hatch_form)
        if eidx < len(self.egg):
            pool.update(self.egg[eidx])
        for i in range(230):                    # TM @0x2C
            if _tmbit(e, 0x2C, i):
                pool.add(self.machine[i])
        if idx < len(self.rem):                 # move reminder
            pool.update(self.rem[idx])
        _add_rotom(pool, sp, form)
        _add_necrozma(pool, sp, form)
        return pool


class FRLGAssembler:
    """LearnSource3FR.GetAllMoves: level-up U TM U HM U shared egg U the 3 starter tutors.

    FR and LG share this pool -- PKHeX keeps separate lvlmove_fr/lvlmove_lg blobs, but the
    only differences are version-exclusive *encounters*, not learnsets, and PKSE treats
    FR/LG as one GameVersion group. Uses lvlmove_fr as the representative.
    """

    # LearnSource3FR.GetIsTutor -- the three starter-exclusive tutor moves, and the only
    # tutor moves the Gen 3 learn source models at all.
    TUTOR = {6: MOVE["BlastBurn"], 9: MOVE["HydroCannon"], 3: MOVE["FrenzyPlant"]}

    def __init__(self):
        self.p = Personal3()
        self.lvl = read_levelup(BYTE("levelup", "lvlmove_fr.pkl"))
        self.egg = read_movesource(BYTE("eggmove", "eggmove_rs.pkl"))

    def assemble(self, sp, form):
        pool = set()
        if sp < len(self.lvl):
            pool.update(self.lvl[sp])
        if sp < len(self.egg):
            pool.update(self.egg[sp])
        for i, mv in enumerate(G3_TM_MOVES):
            if self.p.tm_bit(sp, i):
                pool.add(mv)
        for i, mv in enumerate(G3_HM_MOVES):
            if self.p.tm_bit(sp, G3_COUNT_TM + i):
                pool.add(mv)
        tutor = self.TUTOR.get(sp)
        if tutor:
            pool.add(tutor)
        pool.discard(0)
        return pool


class RSEAssembler:
    """LearnSource3RS + LearnSource3E GetAllMoves: level-up U shared egg U TM/HM U tutors.

    RUBY/SAPPHIRE AND EMERALD ARE UNIONED, the same call made for Red/Blue/Yellow and for
    Gold/Silver/Crystal, and for the same reason: PKSE has ONE Gen 3 Hoenn storage group. A
    learn pool is looked up by GROUP, so the group's pool has to answer for every game in it --
    narrowing to Ruby would report a legitimate Emerald Pokemon's Battle Frontier tutor move as
    illegal, and the transfer sanitizer deletes what isLearnable() denies.

    RS and Emerald genuinely differ in level-up tables (Emerald retunes a number of lines), which
    is why both blobs are read rather than one being taken as representative.

    THE TUTORS ARE TWO DIFFERENT MECHANISMS and both belong in the union:

      * Emerald's Battle Frontier tutors are a 30-move list gated per species by the TypeTutors
        bitfield in tutors_g3.pkl -- the same shape as Gen 4's separate tutor blob, and NOT part
        of the personal entry. The first 15 of those 30 are the ones FR/LG also teach.
      * Ruby/Sapphire's are the Pokemon XD ones PKHeX models on LearnSource3RS: Mew's six, plus
        flat species lists for Self-Destruct, Sky Attack and Nightmare. They are not bitfields
        and have no personal-table representation at all.

    Note this pool is deliberately WIDER than FRLGAssembler's, which carries only the three
    starter tutors -- that is PKHeX's LearnSource3FR, not an omission here.
    """

    # LearnSource3E.Tutor_E -- the move each TypeTutors bit stands for, in bit order.
    TUTOR_E = [
        # Introduced in FR/LG
        5, 14, 25, 34, 38, 68, 69, 102, 118, 135, 138, 86, 153, 157, 164,
        # Introduced in Emerald
        223, 205, 244, 173, 196, 203, 189, 8, 207, 214, 129, 111, 9, 7, 210,
    ]

    # LearnSource3RS.Tutor_3Mew -- Mew alone, from Pokemon XD.
    TUTOR_MEW = [185, 252, 95, 101, 272, 192]

    # LearnSource3RS.SpecialTutors_XD_* -- flat species lists, one per move.
    XD_SELF_DESTRUCT = {
        74, 75, 76, 88, 89, 90, 91, 92, 93, 94, 95,
        100, 101, 102, 103, 109, 110, 143, 150, 151, 185, 204,
        205, 208, 211, 218, 219, 222, 273, 274, 275, 299, 316,
        317, 320, 321, 323, 324, 337, 338, 343, 344, 362, 375,
        376, 377, 378, 379,
    }
    XD_SKY_ATTACK = {
        16, 17, 18, 21, 22, 84, 85, 142, 144, 145, 146,
        151, 163, 164, 176, 177, 178, 198, 225, 227, 250, 276,
        277, 278, 279, 333, 334,
    }
    XD_NIGHTMARE = {
        12, 35, 36, 39, 40, 52, 53, 63, 64, 65, 79,
        80, 92, 93, 94, 96, 97, 102, 103, 108, 121, 122,
        124, 131, 137, 150, 151, 163, 164, 173, 174, 177, 178,
        190, 196, 197, 198, 199, 200, 203, 206, 215, 228, 229,
        233, 234, 238, 248, 249, 250, 251, 280, 281, 282, 284,
        292, 302, 315, 316, 317, 327, 353, 354, 355, 356, 358,
        359, 385, 386,
    }
    MOVE_SELF_DESTRUCT = 120
    MOVE_SKY_ATTACK = 143
    MOVE_NIGHTMARE = 171
    SPECIES_MEW = 151

    def __init__(self):
        self.p = Personal3("personal_rs")
        self.lvl_rs = read_levelup(BYTE("levelup", "lvlmove_rs.pkl"))
        self.lvl_e = read_levelup(BYTE("levelup", "lvlmove_e.pkl"))
        self.egg = read_movesource(BYTE("eggmove", "eggmove_rs.pkl"))
        # tutors_g3.pkl is the u32-offset container hmtm_g3.pkl uses, not the u16 one -- PKHeX
        # hands both to PersonalTable3.LoadTables through the same BinLinkerAccessor.
        self.tutors = bin_entries32(_load(BYTE("personal", "tutors_g3.pkl")))

    def tutor_bit(self, sp, index):
        entry = self.tutors[sp] if sp < len(self.tutors) else b""
        return bool((entry[index >> 3] >> (index & 7)) & 1) if (index >> 3) < len(entry) else False

    def assemble(self, sp, form):
        pool = set()
        for table in (self.lvl_rs, self.lvl_e):
            if sp < len(table):
                pool.update(table[sp])
        if sp < len(self.egg):
            pool.update(self.egg[sp])
        for i, mv in enumerate(G3_TM_MOVES):
            if self.p.tm_bit(sp, i):
                pool.add(mv)
        for i, mv in enumerate(G3_HM_MOVES):
            if self.p.tm_bit(sp, G3_COUNT_TM + i):
                pool.add(mv)
        for i, mv in enumerate(self.TUTOR_E):
            if self.tutor_bit(sp, i):
                pool.add(mv)
        if sp == self.SPECIES_MEW:
            pool.update(self.TUTOR_MEW)
        if sp in self.XD_SELF_DESTRUCT:
            pool.add(self.MOVE_SELF_DESTRUCT)
        if sp in self.XD_SKY_ATTACK:
            pool.add(self.MOVE_SKY_ATTACK)
        if sp in self.XD_NIGHTMARE:
            pool.add(self.MOVE_NIGHTMARE)
        pool.discard(0)
        return pool


class RBYAssembler:
    """LearnSource1RB / LearnSource1YW GetAllMoves: level-up U TM/HM U the Surf tutor,
    PLUS the personal table's four initial moves (see Personal1 -- PKHeX gets those from
    PersonalInfo1.GetMoves during encounter generation, not from the learnset).

    RED/BLUE AND YELLOW ARE UNIONED. PKHeX keeps them as two separate learn sources because
    their level-up tables genuinely differ (Yellow retunes several lines, the Pikachu line
    most of all). PKSE has ONE Gen 1 storage group, and a PK1 records nowhere which of the
    three games it came from -- the format has no version field -- so "could this Pokemon
    legally know this move" has to be answered across all of them. Narrowing to Red/Blue
    would report a legitimate Yellow Pokemon's level-up move as illegal, and worse, the
    transfer sanitizer would delete it.

    Surf on Pikachu/Raichu is included. It is Stadium-only, which PKHeX gates behind its
    AllowGBStadium2 setting, but it IS legitimately obtainable and this pool is "what may be
    known", not "what this cartridge alone teaches".
    """

    SURF = 57
    SURF_SPECIES = (25, 26)   # Pikachu, Raichu

    def __init__(self):
        self.p = Personal1()
        self.lvl_rb = read_levelup(BYTE("levelup", "lvlmove_rb.pkl"))
        self.lvl_yw = read_levelup(BYTE("levelup", "lvlmove_y.pkl"))

    def assemble(self, sp, form):
        if form != 0:
            return set()
        pool = set(self.p.initial_moves(sp))
        for table in (self.lvl_rb, self.lvl_yw):
            if sp < len(table):
                pool.update(table[sp])
        for i, mv in enumerate(G1_MACHINE_MOVES):
            if self.p.tm_bit(sp, i):
                pool.add(mv)
        if sp in self.SURF_SPECIES:
            pool.add(self.SURF)
        pool.discard(0)
        return pool


class Gen2Assembler:
    """LearnSource2GS + LearnSource2C GetAllMoves: level-up U shared egg U TM/HM U the Crystal
    tutors.

    GOLD/SILVER AND CRYSTAL ARE UNIONED, for the same reason Red/Blue and Yellow are: PKSE has one
    Gen 2 storage group and a PK2 records nowhere which of the three games it came from, so "could
    this Pokemon legally know this move" has to be answered across all of them. Crystal's three
    Move Tutor moves (Flamethrower/Thunderbolt/Ice Beam) are therefore in the pool for a GS
    Pokemon too -- narrowing would report a legitimate Crystal mon's tutor move as illegal.

    The personal table has NO form entries in Gen 2, so every lookup is by species alone.
    """

    def __init__(self):
        self.raw = _load(BYTE("personal", "personal_c"))
        self.SIZE = 0x20
        self.maxsp = 251
        self.lvl_gs = read_levelup(BYTE("levelup", "lvlmove_gs.pkl"))
        self.lvl_c  = read_levelup(BYTE("levelup", "lvlmove_c.pkl"))
        self.egg_gs = read_movesource(BYTE("eggmove", "eggmove_gs.pkl"))
        self.egg_c  = read_movesource(BYTE("eggmove", "eggmove_c.pkl"))
        cs = "PersonalInfo/Info/PersonalInfo2.cs"
        self.machine = parse_move_list(cs, "MachineMoves", 57)   # 50 TMs + 7 HMs
        self.tutor   = parse_move_list(cs, "TutorMoves", 3)

    # TMHM @0x18; the three Crystal tutor bits continue in the same field, at bit 57+.
    TMHM = 0x18
    COUNT_TMHM = 57

    def ent(self, sp):
        return self.raw[sp * self.SIZE:(sp + 1) * self.SIZE]

    def assemble(self, sp, form):
        if form != 0 or sp > self.maxsp:
            return set()
        pool = set()
        for table in (self.lvl_gs, self.lvl_c):
            if sp < len(table):
                pool.update(table[sp])
        for table in (self.egg_gs, self.egg_c):
            if sp < len(table):
                pool.update(table[sp])
        e = self.ent(sp)
        for i in range(self.COUNT_TMHM):
            if _tmbit(e, self.TMHM, i):
                pool.add(self.machine[i])
        for i in range(3):
            if _tmbit(e, self.TMHM, self.COUNT_TMHM + i):
                pool.add(self.tutor[i])
        pool.discard(0)
        return pool


class Gen4Assembler(Assembler):
    """LearnSource4DP / 4Pt / 4HGSS GetAllMoves.

    Level-up U shared egg U TM(92) U HM(8) U the type tutors U the four elemental-beam tutors.

    THE TYPE TUTORS ARE NOT IN THE PERSONAL TABLE. Gen 4 keeps them in a separate blob,
    tutors_g4.pkl, indexed by the same form index -- PKHeX loads it into the table at runtime
    (PersonalTable4.LoadTables). Reading bits out of the personal entry instead finds whatever
    happens to be at that offset.

    The HM list differs between DP/Pt and HGSS, so each game passes its own.
    """

    HM_LIST = {"DP": "MachineMovesHiddenDPPt", "PT": "MachineMovesHiddenDPPt",
               "HGSS": "MachineMovesHiddenHGSS"}
    # LearnSource4*.SpecialTutor*: the four elemental-beam tutors, by species.
    BEAMS = {
        "BlastBurn":   (6, 157, 257, 392),
        "HydroCannon": (9, 160, 260, 395),
        "FrenzyPlant": (3, 154, 254, 389),
        "DracoMeteor": (147, 148, 149, 230, 329, 330, 334, 371, 372, 373, 380, 381, 384,
                        443, 444, 445, 483, 484, 487),
    }

    TMHM = 0x1C
    COUNT_TM = 92
    COUNT_HM = 8
    COUNT_TUTOR = 0x34

    def __init__(self, key, levelup, eggmove):
        super().__init__(key)
        self.lvl = self._levelup(BYTE("levelup", levelup))
        self.egg = read_movesource(BYTE("eggmove", eggmove))
        cs = "PersonalInfo/Info/PersonalInfo4.cs"
        self.tm = parse_move_list(cs, "MachineMovesTechnical", self.COUNT_TM)
        self.hm = parse_move_list(cs, self.HM_LIST[key], self.COUNT_HM)
        self.tutor = parse_move_list(cs, "TutorMoves", self.COUNT_TUTOR)
        self.tutor_bits = bin_entries(_load(BYTE("personal", "tutors_g4.pkl")))

    def assemble(self, sp, form):
        p = self.p
        idx = p.form_index(sp, form)
        e = p.ent(idx)
        pool = set(self.lvl[idx])
        if sp < len(self.egg):          # eggmove_dppt/hgss are indexed by species
            pool.update(self.egg[sp])
        for i in range(self.COUNT_TM):
            if _tmbit(e, self.TMHM, i):
                pool.add(self.tm[i])
        for i in range(self.COUNT_HM):
            if _tmbit(e, self.TMHM, self.COUNT_TM + i):
                pool.add(self.hm[i])
        if idx < len(self.tutor_bits):
            bits = self.tutor_bits[idx]
            for i in range(min(self.COUNT_TUTOR, len(bits) * 8)):
                if _tmbit(bits, 0, i):
                    pool.add(self.tutor[i])
        for move, species in self.BEAMS.items():
            if sp in species:
                pool.add(MOVE[move])
        pool.discard(0)
        return pool


class Gen5Assembler(Assembler):
    """LearnSource5BW / 5B2W2 GetAllMoves: level-up U shared egg U TM(101) U type tutors(8)
    U the four Black-2/White-2 tutor shops U the enhanced tutors.

    THE FOUR TUTOR SHOPS ARE B2W2 ONLY. Black/White has no such tutors and PersonalInfo5BW has no
    Tutor1..4 fields at all -- its entries are 0x3C bytes, which is where B2W2's Tutor1 starts.
    Reading them from a BW table walks off the end of every entry.
    """

    TMHM = 0x28
    COUNT_TMHM = 101
    TYPE_TUTOR = 0x38
    TYPE_TUTOR_COUNT = 8
    # (offset, count, span name) -- Driftveil, Lentimas, Humilau, Nacrene.
    SHOPS = ((0x3C, 15, "TutorMoves1"), (0x40, 17, "TutorMoves2"),
             (0x44, 13, "TutorMoves3"), (0x48, 15, "TutorMoves4"))

    def __init__(self, key):
        super().__init__(key)
        self.lvl = self._levelup(BYTE("levelup",
                                      "lvlmove_bw.pkl" if key == "BW" else "lvlmove_b2w2.pkl"))
        self.egg = read_movesource(BYTE("eggmove", "eggmove_bw.pkl"))
        self.type_tutor = parse_move_list("PersonalInfo/Info/PersonalInfo5BW.cs",
                                          "TypeTutorMoves", self.TYPE_TUTOR_COUNT)
        # ONE list for both games: LearnSource5B2W2 does `using static PersonalInfo5BW` and reads
        # its MachineMoves, so B2W2 has no list of its own to read.
        self._machine = parse_move_list("PersonalInfo/Info/PersonalInfo5BW.cs",
                                        "MachineMoves", self.COUNT_TMHM)
        self.shops = []
        if key == "B2W2":
            cs = "PersonalInfo/Info/PersonalInfo5B2W2.cs"
            for ofs, count, name in self.SHOPS:
                self.shops.append((ofs, count, parse_move_list(cs, name, count)))

    def assemble(self, sp, form):
        p = self.p
        idx = p.form_index(sp, form)
        e = p.ent(idx)
        pool = set(self.lvl[idx])
        if sp < len(self.egg):
            pool.update(self.egg[sp])
        for i in range(self.COUNT_TMHM):
            if _tmbit(e, self.TMHM, i):
                pool.add(self.machine_move(i))
        for i in range(self.TYPE_TUTOR_COUNT):
            if _tmbit(e, self.TYPE_TUTOR, i):
                pool.add(self.type_tutor[i])
        for ofs, count, moves in self.shops:
            for i in range(count):
                if _tmbit(e, ofs, i):
                    pool.add(moves[i])
        _add_rotom(pool, sp, form)
        if sp == KELDEO:
            pool.add(MOVE["SecretSword"])
        if sp == MELOETTA:
            pool.add(MOVE["RelicSong"])
        pool.discard(0)
        return pool

    def machine_move(self, i):
        return self._machine[i]


class Gen6Assembler(Assembler):
    """LearnSource6XY / 6AO GetAllMoves.

    Two differences between the games and both matter: ORAS has SEVEN HMs to XY's five (so the
    TM/HM bit field is 107 bits, not 105, and the machine list is its own), and ORAS has the four
    tutor shops while XY has none at all.
    """

    TMHM = 0x28
    TYPE_TUTOR = 0x38
    SHOPS = ((0x40, 15, "TutorMoves1"), (0x44, 17, "TutorMoves2"),
             (0x48, 16, "TutorMoves3"), (0x4C, 15, "TutorMoves4"))

    def __init__(self, key):
        super().__init__(key)
        xy = key == "XY"
        self.lvl = self._levelup(BYTE("levelup", "lvlmove_xy.pkl" if xy else "lvlmove_ao.pkl"))
        self.egg = read_movesource(BYTE("eggmove", "eggmove_xy.pkl" if xy else "eggmove_ao.pkl"))
        cs = "PersonalInfo/Info/PersonalInfo6%s.cs" % ("XY" if xy else "AO")
        self.count_tmhm = 105 if xy else 107
        # Gen 6 keeps its TM/HM list in the LEARN SOURCE, not the personal info -- unlike Gen 5
        # and Gen 7, which both keep theirs in PersonalInfo. XY and ORAS have separate lists
        # because ORAS added two HMs.
        self.machine = parse_move_list(
            "Legality/LearnSource/Sources/LearnSource6%s.cs" % ("XY" if xy else "AO"),
            "MachineMoves", self.count_tmhm)
        # LearnSource6XY/6AO both pass PersonalInfo5BW.TypeTutorMoves; XY reads 7 bits, ORAS 8.
        self.type_tutor = parse_move_list("PersonalInfo/Info/PersonalInfo5BW.cs",
                                          "TypeTutorMoves", 8)
        self.type_tutor_count = 7 if xy else 8
        self.shops = []
        if not xy:
            for ofs, count, name in self.SHOPS:
                self.shops.append((ofs, count, parse_move_list(cs, name, count)))

    def assemble(self, sp, form):
        p = self.p
        idx = p.form_index(sp, form)
        e = p.ent(idx)
        pool = set(self.lvl[idx])
        if sp < len(self.egg):
            pool.update(self.egg[sp])
        for i in range(self.count_tmhm):
            if _tmbit(e, self.TMHM, i):
                pool.add(self.machine[i])
        for i in range(self.type_tutor_count):
            if _tmbit(e, self.TYPE_TUTOR, i):
                pool.add(self.type_tutor[i])
        for ofs, count, moves in self.shops:
            for i in range(count):
                if _tmbit(e, ofs, i):
                    pool.add(moves[i])
        _add_rotom(pool, sp, form)
        if sp == KELDEO:
            pool.add(MOVE["SecretSword"])
        if sp == MELOETTA:
            pool.add(MOVE["RelicSong"])
        pool.discard(0)
        return pool


class Gen7Assembler(Assembler):
    """LearnSource7SM / 7USUM GetAllMoves: level-up U shared egg U TM(100) U type tutors(8)
    U the Battle Point tutors(67) U the enhanced tutors.

    Gen 7's enhanced tutors are a longer list than Gen 5/6's: Rotom's form moves, Necrozma's fusion
    moves (Dusk Mane form 1, Dawn Wings form 2), Keldeo, Meloetta, Zygarde's five signature moves,
    and Volt Tackle for the Pikachu line.
    """

    TMHM = 0x28
    COUNT_TM = 100
    TYPE_TUTOR = 0x38
    TYPE_TUTOR_COUNT = 8
    TUTOR1 = 0x3C
    TUTOR1_COUNT = 67

    ZYGARDE = 718
    PIKACHU_LINE = (25, 26)

    def __init__(self, key):
        super().__init__(key)
        sm = key == "SM"
        self.lvl = self._levelup(BYTE("levelup", "lvlmove_sm.pkl" if sm else "lvlmove_uu.pkl"))
        self.egg = read_movesource(BYTE("eggmove", "eggmove_sm.pkl" if sm else "eggmove_uu.pkl"))
        cs = "PersonalInfo/Info/PersonalInfo7.cs"
        self.machine = parse_move_list(cs, "MachineMoves", self.COUNT_TM)
        self.bp_tutor = parse_move_list(cs, "BattlePointTutorMoves", self.TUTOR1_COUNT)
        self.type_tutor = parse_move_list("PersonalInfo/Info/PersonalInfo5BW.cs",
                                          "TypeTutorMoves", self.TYPE_TUTOR_COUNT)

    def assemble(self, sp, form):
        p = self.p
        idx = p.form_index(sp, form)
        e = p.ent(idx)
        pool = set(self.lvl[idx])
        # Gen 7 is the one generation whose egg-move binary is indexed by FORM index, not species
        # (LearnSource7SM/7USUM.GetEggMoves go through Personal.GetFormIndex), so Alolan Vulpix
        # has its own list. Reading it by species handed every regional form the base form's list.
        if idx < len(self.egg):
            pool.update(self.egg[idx])
        for i in range(self.COUNT_TM):
            if _tmbit(e, self.TMHM, i):
                pool.add(self.machine[i])
        for i in range(self.TYPE_TUTOR_COUNT):
            if _tmbit(e, self.TYPE_TUTOR, i):
                pool.add(self.type_tutor[i])
        for i in range(self.TUTOR1_COUNT):
            if _tmbit(e, self.TUTOR1, i):
                pool.add(self.bp_tutor[i])
        _add_rotom(pool, sp, form)
        _add_necrozma(pool, sp, form)
        if sp == KELDEO:
            pool.add(MOVE["SecretSword"])
        if sp == MELOETTA:
            pool.add(MOVE["RelicSong"])
        if sp == self.ZYGARDE:
            for m in ("CoreEnforcer", "ExtremeSpeed", "DragonDance",
                      "ThousandArrows", "ThousandWaves"):
                pool.add(MOVE[m])
        if sp in self.PIKACHU_LINE:
            pool.add(MOVE["VoltTackle"])
        pool.discard(0)
        return pool


ASSEMBLERS = {"GG": GGAssembler, "SWSH": SWSHAssembler, "BDSP": BDSPAssembler,
              "RBY": RBYAssembler,
              "GSC": Gen2Assembler,
              "FRLG": FRLGAssembler,
              "RSE": RSEAssembler,
              "DP":   lambda: Gen4Assembler("DP",   "lvlmove_dp.pkl",   "eggmove_dppt.pkl"),
              "PT":   lambda: Gen4Assembler("PT",   "lvlmove_pt.pkl",   "eggmove_dppt.pkl"),
              "HGSS": lambda: Gen4Assembler("HGSS", "lvlmove_hgss.pkl", "eggmove_hgss.pkl"),
              "BW":   lambda: Gen5Assembler("BW"),
              "B2W2": lambda: Gen5Assembler("B2W2"),
              "XY":   lambda: Gen6Assembler("XY"),
              "ORAS": lambda: Gen6Assembler("ORAS"),
              "SM":   lambda: Gen7Assembler("SM"),
              "USUM": lambda: Gen7Assembler("USUM"),
              "PLA": PLAAssembler, "SV": SVAssembler, "ZA": ZAAssembler}


# ----------------------------------------------------------------------------
# Row enumeration (identical to PersonalInfoTable)
# ----------------------------------------------------------------------------
def load_species_names():
    """Species names, index-aligned so entry N is species N. Comments only."""
    with open(pkhex_path(SPECIES_NAMES_SRC), "rb") as fh:
        raw = fh.read()
    # Decoded by its BOM: PKHeX's text resources are not uniformly encoded, and almost any
    # even-length byte sequence is valid UTF-16, so trying encodings in order decodes garbage
    # instead of raising.
    text = raw.decode("utf-16") if raw[:2] in (b"\xff\xfe", b"\xfe\xff") else raw.decode("utf-8-sig")
    names = text.splitlines()
    # PKHeX calls entry 0 "Egg"; PKSE's own species table substitutes "None" there
    # (gen_speciesnames.py does the same). Keep PKSE's spelling so regenerating this file does not
    # churn nineteen comment lines that have nothing to do with the move data.
    if names:
        names[0] = "None"
    if len(names) <= MAX_SPECIES:
        raise SystemExit(f"{SPECIES_NAMES_SRC} only has {len(names)} entries")
    return names


def build_rows(personals):
    union_fc = [1] * BASE_ROWS
    for sp in range(BASE_ROWS):
        fc = 1
        for t in personals.values():
            if sp <= t.maxsp and sp < t.count:
                fc = max(fc, t.form_count(sp))
        union_fc[sp] = fc

    rows = [(sp, 0) for sp in range(BASE_ROWS)]
    form_index = [0] * BASE_ROWS
    next_index = BASE_ROWS
    for sp in range(BASE_ROWS):
        if union_fc[sp] > 1:
            form_index[sp] = next_index
            for form in range(1, union_fc[sp]):
                rows.append((sp, form))
            next_index += union_fc[sp] - 1
    return rows, form_index, union_fc


# ----------------------------------------------------------------------------
# Emission
# ----------------------------------------------------------------------------
HDR_TMPL = '''/**
 *
 * Auto-generated by tools/gen_learnsets.py from PKHeX's binary learnset resources.
 * DO NOT EDIT BY HAND -- rerun the generator instead.
 *
 * For a (species, form) present in a game, that game's bitset holds bit M set =>
 * the species+form can legally KNOW move M in that game (union of level-up / egg /
 * TM / TR / tutor / reminder, then unioned with its whole pre-evolution chain --
 * an evolved Pokemon keeps what it learned earlier). Smeargle's row is every move
 * Sketch can copy in that game. Rows mirror PersonalInfoTable (base rows 0..{MAX},
 * then alt forms via formIndex). Every save-format group PKSE supports, Red/Blue/Yellow
 * through Legends: Z-A, has its own bitsets. Bitsets share one uniform width sized to
 * hold move id {MAXMOVE} (ZA's NihilLight).
 */
#ifndef PKM_LEARNSET_TABLE_H
#define PKM_LEARNSET_TABLE_H

#include <cstdint>
#include <cstddef>

#include "Enums/GameVersion.h"

namespace Pokemon {{
    constexpr uint16_t LEARN_MAX_SPECIES = {MAX};
    constexpr uint16_t LEARN_MAX_MOVE_ID = {MAXMOVE};   // ZA NihilLight; move space 0..{MAXMOVE}
    constexpr size_t   LEARN_BITSET_BYTES = {BYTES};    // uniform width for every game

    // Pointer to the {BYTES}-byte learnable bitset for (species, form) in the given
    // game group, or nullptr if the species+form is absent from that game. Accepts a
    // GameVersion group (GG/SWSH/BDSP/PLA/SV/ZA) or an individual version (e.g.
    // GP/GE, SW/SH, BD/SP, SL/VL) -- both resolve to the right game.
    const uint8_t* getLearnableBits(uint16_t species, uint8_t form, Enums::GameVersion group);

    // True iff moveId is in the (species, form) learnable pool for the game group.
    bool isLearnable(uint16_t species, uint8_t form, Enums::GameVersion group, uint16_t moveId);
}}

#endif  // PKM_LEARNSET_TABLE_H
'''


def emit_hex_pool(parts, name, blocks, width, names):
    parts.append("    const uint8_t %s[%d * %d] = {\n" % (name, max(len(blocks), 1), width))
    if not blocks:
        parts.append("        0,  // (no present species -- placeholder)\n")
    for (sp, form, bits, count) in blocks:
        nm = names[sp] if sp < len(names) else "?"
        label = f"#{sp} {nm}" + (f" form {form}" if form else "")
        parts.append(f"        // {label}  ({count} moves)\n")
        for r in range(0, width, 16):
            chunk = bits[r:r + 16]
            parts.append("        " + "".join("0x%02X, " % b for b in chunk).rstrip() + "\n")
    parts.append("    };\n\n")


def emit_u16_array(parts, name, values, per_line, decl_count):
    parts.append("    const uint16_t %s[%s] = {\n" % (name, decl_count))
    line = "        "
    for i, v in enumerate(values):
        line += "0x%04X," % (v & 0xFFFF)
        if (i + 1) % per_line == 0:
            parts.append(line + "\n")
            line = "        "
        else:
            line += " "
    if line.strip():
        parts.append(line.rstrip() + "\n")
    parts.append("    };\n\n")


def main():
    names = load_species_names()
    personals = {g: Personal(g) for g in GAMES}
    personals["FRLG"] = Personal3()   # Gen 3 is shaped differently -- see Personal3
    # personal_rs rather than personal_fr: same shape, same 386 species, but it is the table the
    # Hoenn games actually ship. Nothing in the move pool reads a stat out of it (TM/HM and tutor
    # bits come from the shared hmtm_g3/tutors_g3 blobs), so this is for honesty, not behaviour.
    personals["RSE"] = Personal3("personal_rs")
    personals["RBY"] = Personal1()    # Gen 1 more so -- see Personal1
    personals["GSC"] = Personal2()    # Gen 2: no forms, species-indexed -- see Personal2
    rows, form_index, form_count = build_rows(personals)
    print("union rows:", len(rows), "(base", BASE_ROWS, "+ alt", len(rows) - BASE_ROWS, ")")

    # Build every game's bitsets for its present species+forms.
    game_blocks = {}   # game -> [(sp, form, bytes, movecount)]
    game_rowblk = {}   # game -> [block index per row, 0xFFFF = absent]
    for g in GAMES_TO_EMIT:
        asm = ASSEMBLERS[g]()
        pers = personals[g]
        # The lineage the move pool is folded along is the same one EvolutionTable.cpp emits,
        # read through the same table -- see EVO_INDEX_GAME.
        evo_rev = read_evolutions(EVO_RESOURCE[g], pers, personals[EVO_INDEX_GAME.get(g, g)])
        cache = {}

        def own_pool(sp, form, _asm=asm, _cache=cache):
            key = (sp, form)
            if key not in _cache:
                _cache[key] = frozenset(_asm.assemble(sp, form))
            return _cache[key]

        row_block = [0xFFFF] * len(rows)
        blocks = []
        folded_rows = folded_moves = 0
        for ri, (sp, form) in enumerate(rows):
            if not pers.present(sp, form):
                continue
            pool = set(own_pool(sp, form))
            if sp == SMEARGLE:
                pool |= sketch_pool(g)
            own = len(pool)
            # Fold in every ancestor's pool. A Pokemon keeps the moves it knew before
            # evolving, so its legally-knowable set is the UNION over its evolution
            # chain -- e.g. a Raichu may know Double Kick, which only Pikachu learns.
            # Ancestors absent from this game are skipped: you cannot have evolved
            # from one here (Pichu is not in Let's Go), so its moves aren't reachable.
            for (psp, pform) in preevo_chain(evo_rev, sp, form):
                if pers.present(psp, pform):
                    pool |= own_pool(psp, pform)
            if len(pool) > own:
                folded_rows += 1
                folded_moves += len(pool) - own
            bits = bytearray(WIDTH)
            for mv in pool:
                if mv > MAX_MOVE_ID:
                    raise SystemExit(f"{g}: move id {mv} exceeds width (sp {sp} form {form})")
                bits[mv >> 3] |= 1 << (mv & 7)
            row_block[ri] = len(blocks)
            blocks.append((sp, form, bits, len(pool)))
        game_blocks[g] = blocks
        game_rowblk[g] = row_block
        print(f"  {g:5} pre-evo fold: {folded_rows:4} rows gained {folded_moves:5} moves"
              f"  (lineage edges: {len(evo_rev)})")

    # ---- Header ----
    with open(OUT_H, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(HDR_TMPL.format(MAX=MAX_SPECIES, MAXMOVE=MAX_MOVE_ID, BYTES=WIDTH))

    # ---- Source ----
    p = []
    p.append('/**\n'
             ' * Auto-generated by tools/gen_learnsets.py from PKHeX\'s binary learnset\n'
             ' * resources. DO NOT EDIT BY HAND -- rerun the generator instead.\n'
             ' *\n'
             ' * Layout: for each game G, LEARN_<G>_BITS is a flat pool of %d-byte bitsets\n'
             ' * (one per G-present species+form, in row order) and LEARN_<G>_ROW_BLOCK[row]\n'
             ' * gives a row\'s block index (0xFFFF = absent); byte offset = block * %d.\n'
             ' * LEARN_FORM_INDEX redirects alternate forms exactly like PersonalInfoTable\n'
             ' * (form N -> LEARN_FORM_INDEX[species] + N - 1).\n'
             ' *\n'
             ' * Each pool is the species\' own learnable set UNIONED with its whole\n'
             ' * pre-evolution chain (a Raichu keeps Pikachu\'s Double Kick), folded in at\n'
             ' * generation time from PKHeX\'s evos_*.pkl lineage.\n'
             ' */\n\n' % (WIDTH, WIDTH))
    p.append('#include "Pokemon/LearnsetTable.h"\n\n')
    p.append("namespace Pokemon {\n\n")
    p.append("    constexpr uint16_t LEARN_ABSENT = 0xFFFF;\n\n")
    p.append("    // Row -> National Dex form redirection (0 = species has no alternate forms).\n")
    emit_u16_array(p, "LEARN_FORM_INDEX", form_index, 12, "LEARN_MAX_SPECIES + 1")
    p.append("    // Forms per species (the most any game has). A form past it reads the base row, as\n")
    p.append("    // PersonalInfoTable does -- without the bound it read the NEXT species' rows.\n")
    emit_u16_array(p, "LEARN_FORM_COUNT", form_count, 12, "LEARN_MAX_SPECIES + 1")

    for g in GAMES_TO_EMIT:
        blocks = game_blocks[g]
        p.append("    // ==== %s (%d present species+forms) ====\n" % (g, len(blocks)))
        p.append("    // Row -> %s bitset block index (0xFFFF = not present in %s).\n" % (g, g))
        emit_u16_array(p, "LEARN_%s_ROW_BLOCK" % g, game_rowblk[g], 12, str(len(rows)))
        emit_hex_pool(p, "LEARN_%s_BITS" % g, blocks, WIDTH, names)

    # accessors
    p.append("    static inline size_t learnRowIndex(uint16_t species, uint8_t form) {\n")
    p.append("        if (species > LEARN_MAX_SPECIES)\n")
    p.append("            return 0;\n")
    p.append("        const uint16_t formIndex = LEARN_FORM_INDEX[species];\n")
    p.append("        if (form == 0 || formIndex == 0 || form >= LEARN_FORM_COUNT[species])\n")
    p.append("            return species;\n")
    p.append("        return static_cast<size_t>(formIndex) + form - 1;\n")
    p.append("    }\n\n")

    p.append("    static inline const uint8_t* learnLookup(const uint16_t* rowBlock, "
             "const uint8_t* pool, size_t row) {\n")
    p.append("        if (row >= %d)\n" % len(rows))
    p.append("            return nullptr;\n")
    p.append("        uint16_t block = rowBlock[row];\n")
    p.append("        if (block == LEARN_ABSENT)\n")
    p.append("            return nullptr;\n")
    p.append("        return &pool[static_cast<size_t>(block) * LEARN_BITSET_BYTES];\n")
    p.append("    }\n\n")

    p.append("    const uint8_t* getLearnableBits(uint16_t species, uint8_t form, Enums::GameVersion group) {\n")
    p.append("        size_t row = learnRowIndex(species, form);\n")
    p.append("        switch (group) {\n")
    for g in GAMES_TO_EMIT:
        cases = "".join("            case Enums::GameVersion::%s:\n" % v for v in GAME_VERSION_IDS[g])
        p.append(cases)
        p.append("                return learnLookup(LEARN_%s_ROW_BLOCK, LEARN_%s_BITS, row);\n" % (g, g))
    p.append("            default:\n")
    p.append("                return nullptr;\n")
    p.append("        }\n")
    p.append("    }\n\n")

    p.append("    bool isLearnable(uint16_t species, uint8_t form, Enums::GameVersion group, uint16_t moveId) {\n")
    p.append("        if (moveId > LEARN_MAX_MOVE_ID)\n")
    p.append("            return false;\n")
    p.append("        const uint8_t* bits = getLearnableBits(species, form, group);\n")
    p.append("        if (bits == nullptr)\n")
    p.append("            return false;\n")
    p.append("        return (bits[moveId >> 3] >> (moveId & 7)) & 1;\n")
    p.append("    }\n")
    p.append("}\n")

    with open(OUT_CPP, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("".join(p))

    # ---- Report ----
    print("\nWrote", OUT_H)
    print("Wrote", OUT_CPP)
    idx_bytes = BASE_ROWS * 2  # LEARN_FORM_INDEX
    pool_total = 0
    print(f"\n  uniform bitset width : {WIDTH} bytes (move ids 0..{MAX_MOVE_ID})")
    print("  per-game:")
    for g in GAMES_TO_EMIT:
        n = len(game_blocks[g])
        pool = n * WIDTH
        rb = len(rows) * 2
        pool_total += pool
        idx_bytes += rb
        print(f"    {g:5} present={n:4}  bits={pool:7}B ({pool/1024:5.1f}KB)  rowblock={rb}B")
    total = pool_total + idx_bytes
    print(f"  bitset pools total   : {pool_total} B ({pool_total/1024:.1f} KB)")
    print(f"  indices (rowblock*6 + formIndex): {idx_bytes} B ({idx_bytes/1024:.1f} KB)")
    print(f"  TABLE TOTAL          : {total} B ({total/1024:.1f} KB, {total/1024/1024:.3f} MB)")


if __name__ == "__main__":
    main()
