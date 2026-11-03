#!/usr/bin/env python3
"""Generate include/Pokemon/LearnSourceTable.h + src/Pokemon/LearnSourceTable.cpp: the per-game move
data PKHeX's learn sources (PKHeX.Core/Legality/LearnSource/Sources/LearnSource*.cs) read, kept in
PKHeX's own shape so the C++ port can mirror each GetCanLearn line for line.

LearnsetTable.h keeps one learnable-move bitset per game for the move picker; it cannot say HOW or
AT WHAT LEVEL a move is learned, which is what PKHeX's per-move verdicts are made of. This table is
that missing half, per game PKHeX distinguishes (Red/Blue apart from Yellow, Gold/Silver apart from
Crystal, each Gen 3 game apart):

  * level-up learnsets WITH levels (lvlmove_*.pkl), indexed like PKHeX: by species for Gens 1-3, by
    the game's personal-table form index from Gen 4 on;
  * egg moves (eggmove_*.pkl), indexed as each PKHeX learn source indexes them;
  * Scarlet/Violet reminder moves (reminder_sv.pkl) and Stadium 2's move reminder
    (reminder_stad2.pkl, level / move / source triples);
  * the TM, HM and tutor permission bits of each game's personal row, copied byte for byte from the
    regions PKHeX's PersonalInfo* accessors read (offsets below), plus Gen 3's hmtm_g3/tutors_g3 and
    HeartGold/SoulSilver's tutors_g4, which PKHeX loads beside the personal tables;
  * each game's form-index redirection (FormStatsIndex / FormCount) and, for the three games with
    shared egg moves, the hatch species and form of every row;
  * the move lists those permission bits index into, parsed from PKHeX's C# source;
  * each personal row's gender ratio, two egg groups and IsPresentInGame flag, which PKHeX's
    ChainBreedLegality reads to pick compatible fathers (every game with breeding, Gen 2 on).

Sword/Shield Technical Records, the Legends: Arceus move shop and the Scarlet/Violet and Legends:
Z-A TMs are not repeated here: MoveRecordTable.h (tools/gen_moverecords.py) already has them.

Run:  PKHEX_LOCAL=~/repos/PKHeX python tools/gen_learnsources.py
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pkhex_source import pkhex_ref  # noqa: E402
from gen_learnsets import BYTE, _load, bin_entries, bin_entries32, parse_move_list  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_H = os.path.join(ROOT, "include", "Pokemon", "LearnSourceTable.h")
OUT_CPP = os.path.join(ROOT, "src", "Pokemon", "LearnSourceTable.cpp")

INFO = "PersonalInfo/Info/"
SOURCES = "Legality/LearnSource/Sources/"

# Game name (C++ enumerator), level-up resource, PKHeX personal resource, row size, form-index
# offsets (None for the species-indexed Gens 1-3), egg resource and how it is indexed.
#   egg index: "species" (MoveSource[species]) or "form" (MoveSource[GetFormIndex]).
GAMES = [
    # name,   levelup,              personal,         size, formIndex, formCount, egg,                 eggIndex
    ("RB",   "lvlmove_rb.pkl",   "personal_rb",   0x1C, None, None, None, None),
    ("YW",   "lvlmove_y.pkl",    "personal_y",    0x1C, None, None, None, None),
    ("GS",   "lvlmove_gs.pkl",   "personal_gs",   0x20, None, None, "eggmove_gs.pkl", "species"),
    ("C",    "lvlmove_c.pkl",    "personal_c",    0x20, None, None, "eggmove_c.pkl", "species"),
    ("RS",   "lvlmove_rs.pkl",   "personal_rs",   0x1C, None, None, "eggmove_rs.pkl", "species"),
    ("E",    "lvlmove_e.pkl",    "personal_e",    0x1C, None, None, "eggmove_rs.pkl", "species"),
    ("FR",   "lvlmove_fr.pkl",   "personal_fr",   0x1C, None, None, "eggmove_rs.pkl", "species"),
    ("LG",   "lvlmove_lg.pkl",   "personal_lg",   0x1C, None, None, "eggmove_rs.pkl", "species"),
    ("DP",   "lvlmove_dp.pkl",   "personal_dp",   0x2C, 0x2A, 0x29, "eggmove_dppt.pkl", "species"),
    ("PT",   "lvlmove_pt.pkl",   "personal_pt",   0x2C, 0x2A, 0x29, "eggmove_dppt.pkl", "species"),
    ("HGSS", "lvlmove_hgss.pkl", "personal_hgss", 0x2C, 0x2A, 0x29, "eggmove_hgss.pkl", "species"),
    ("BW",   "lvlmove_bw.pkl",   "personal_bw",   0x3C, 0x1C, 0x20, "eggmove_bw.pkl", "species"),
    ("B2W2", "lvlmove_b2w2.pkl", "personal_b2w2", 0x4C, 0x1C, 0x20, "eggmove_bw.pkl", "species"),
    ("XY",   "lvlmove_xy.pkl",   "personal_xy",   0x40, 0x1C, 0x20, "eggmove_xy.pkl", "species"),
    ("ORAS", "lvlmove_ao.pkl",   "personal_ao",   0x50, 0x1C, 0x20, "eggmove_ao.pkl", "species"),
    ("SM",   "lvlmove_sm.pkl",   "personal_sm",   0x54, 0x1C, 0x20, "eggmove_sm.pkl", "form"),
    ("USUM", "lvlmove_uu.pkl",   "personal_uu",   0x54, 0x1C, 0x20, "eggmove_uu.pkl", "form"),
    ("GG",   "lvlmove_gg.pkl",   "personal_gg",   0x54, 0x1C, 0x20, None, None),
    ("SWSH", "lvlmove_swsh.pkl", "personal_swsh", 0xB0, 0x1E, 0x20, "eggmove_swsh.pkl", "form"),
    ("BDSP", "lvlmove_bdsp.pkl", "personal_bdsp", 0x44, 0x1E, 0x20, "eggmove_bdsp.pkl", "species"),
    ("PLA",  "lvlmove_la.pkl",   "personal_la",   0xB0, 0x1E, 0x20, None, None),
    ("SV",   "lvlmove_sv.pkl",   "personal_sv",   0x50, 0x18, 0x1A, "eggmove_sv.pkl", "form"),
    ("ZA",   "lvlmove_za.pkl",   "personal_za",   0x50, 0x18, 0x1A, None, None),
]

# PKHeX Legal.MaxSpeciesID_* each learn source's TryGetPersonal compares against.
MAX_SPECIES = {"RB": 151, "YW": 151, "GS": 251, "C": 251, "RS": 386, "E": 386, "FR": 386, "LG": 386,
               "DP": 493, "PT": 493, "HGSS": 493, "BW": 649, "B2W2": 649, "XY": 721, "ORAS": 721,
               "SM": 802, "USUM": 807, "GG": 809, "SWSH": 898, "BDSP": 493, "PLA": 905, "SV": 1025,
               "ZA": 1025}

# The personal-row byte regions PKHeX's accessors read: (region name, offset, byte count).
# Offsets from PKHeX.Core/PersonalInfo/Info/PersonalInfo*.cs.
PERMISSION_REGIONS = {
    "RB": [("MACHINE", 0x14, 7), ("INITIAL_MOVES", 0x0F, 4)],
    "YW": [("MACHINE", 0x14, 7), ("INITIAL_MOVES", 0x0F, 4)],
    "GS": [("MACHINE", 0x18, 8)],
    "C": [("MACHINE", 0x18, 8)],
    "DP": [("MACHINE", 0x1C, 13)],
    "PT": [("MACHINE", 0x1C, 13)],
    "HGSS": [("MACHINE", 0x1C, 13)],
    "BW": [("MACHINE", 0x28, 13), ("TYPE_TUTOR", 0x38, 1)],
    "B2W2": [("MACHINE", 0x28, 13), ("TYPE_TUTOR", 0x38, 1), ("TUTOR1", 0x3C, 4), ("TUTOR2", 0x40, 4), ("TUTOR3", 0x44, 4), ("TUTOR4", 0x48, 4)],
    "XY": [("MACHINE", 0x28, 14), ("TYPE_TUTOR", 0x38, 1)],
    "ORAS": [("MACHINE", 0x28, 14), ("TYPE_TUTOR", 0x38, 1), ("TUTOR1", 0x40, 4), ("TUTOR2", 0x44, 4), ("TUTOR3", 0x48, 4), ("TUTOR4", 0x4C, 4)],
    "SM": [("MACHINE", 0x28, 14), ("TYPE_TUTOR", 0x38, 1), ("SPECIAL_TUTOR", 0x3C, 9)],
    "USUM": [("MACHINE", 0x28, 14), ("TYPE_TUTOR", 0x38, 1), ("SPECIAL_TUTOR", 0x3C, 9)],
    "GG": [("MACHINE", 0x28, 8)],
    "SWSH": [("MACHINE", 0x28, 13), ("TYPE_TUTOR", 0x38, 1), ("SPECIAL_TUTOR", 0xA8, 3)],
    "BDSP": [("MACHINE", 0x28, 14), ("TYPE_TUTOR", 0x38, 1)],
}

# Gen 3: PKHeX PersonalTable.PopulateGen3Tutors loads Emerald's machine and tutor flags from these and
# copies them into Ruby/Sapphire, FireRed and LeafGreen, so every Gen 3 game reads the same rows.
GEN3_MACHINE_RESOURCE = "hmtm_g3.pkl"
GEN3_TUTOR_RESOURCE = "tutors_g3.pkl"
# HeartGold/SoulSilver: PersonalTable.PopulateGen4Tutors, indexed by the HGSS form index.
HGSS_TUTOR_RESOURCE = "tutors_g4.pkl"

# (game, hatch species offset, hatch form source): "everstone" is HatchFormIndexEverstone
# (IsRegionalForm ? RegionalFormIndex : LocalFormIndex), "direct" a stored HatchFormIndex.
HATCH = {
    "SWSH": (0x56, ("everstone", 0x58, 0x5A, 0x5E)),
    "BDSP": (0x3E, ("direct", 0x40)),
    "SV": (0x24, ("everstone", 0x26, 0x28, 0x2A)),
}

# The breeding fields of a personal row, from PKHeX.Core/PersonalInfo/Info/PersonalInfo*.cs:
# (gender offset, egg group offset, egg group 2 offset or None when both share a byte as nibbles
# (Gen 2: EggGroup1 = low nibble, EggGroup2 = high nibble), presence rule). The presence rule is
# None for a table with no per-row flag (every row of the game's table counts), ("bit6", offset)
# for PersonalInfo8SWSH/8BDSP/8LA.IsPresentInGame, or ("nonzero", offset) for PersonalInfo9SV/9ZA.
BREEDING_FIELDS = {
    "GS": (0x0D, 0x17, None, None),
    "C": (0x0D, 0x17, None, None),
    "RS": (0x10, 0x14, 0x15, None),
    "E": (0x10, 0x14, 0x15, None),
    "FR": (0x10, 0x14, 0x15, None),
    "LG": (0x10, 0x14, 0x15, None),
    "DP": (0x10, 0x14, 0x15, None),
    "PT": (0x10, 0x14, 0x15, None),
    "HGSS": (0x10, 0x14, 0x15, None),
    "BW": (0x12, 0x16, 0x17, None),
    "B2W2": (0x12, 0x16, 0x17, None),
    "XY": (0x12, 0x16, 0x17, None),
    "ORAS": (0x12, 0x16, 0x17, None),
    "SM": (0x12, 0x16, 0x17, None),
    "USUM": (0x12, 0x16, 0x17, None),
    "GG": (0x12, 0x16, 0x17, None),
    "SWSH": (0x12, 0x16, 0x17, ("bit6", 0x21)),
    "BDSP": (0x12, 0x16, 0x17, ("bit6", 0x21)),
    "PLA": (0x12, 0x16, 0x17, ("bit6", 0x21)),
    "SV": (0x0C, 0x10, 0x11, ("nonzero", 0x1C)),
    "ZA": (0x0C, 0x10, 0x11, ("nonzero", 0x1C)),
}


def breeding_row(row, fields):
    gender_offset, egg_offset, egg2_offset, presence = fields
    if egg2_offset is None:
        egg_group1 = row[egg_offset] & 0x0F
        egg_group2 = row[egg_offset] >> 4
    else:
        egg_group1 = row[egg_offset]
        egg_group2 = row[egg2_offset]
    present = 1
    if presence is not None and presence[0] == "bit6":
        present = (row[presence[1]] >> 6) & 1
    elif presence is not None:
        present = 1 if row[presence[1]] != 0 else 0
    return [row[gender_offset], egg_group1, egg_group2, present]


# Move and species lists the permission bits index into: (C++ name, C# file, C# array name).
MOVE_LISTS = [
    ("MACHINE_MOVES_1RBY", INFO + "PersonalInfo1.cs", "MachineMoves"),
    ("MACHINE_MOVES_2GSC", INFO + "PersonalInfo2.cs", "MachineMoves"),
    ("TUTOR_MOVES_2GSC", INFO + "PersonalInfo2.cs", "TutorMoves"),
    ("TECHNICAL_MACHINE_MOVES_GENERATION3", INFO + "PersonalInfo3.cs", "MachineMovesTechnical"),
    ("HIDDEN_MACHINE_MOVES_GENERATION3", INFO + "PersonalInfo3.cs", "MachineMovesHidden"),
    ("TUTOR_MOVES_3RSE", SOURCES + "LearnSource3E.cs", "Tutor_E"),
    ("MEW_TUTOR_MOVES_3RSE", SOURCES + "LearnSource3RS.cs", "Tutor_3Mew"),
    ("SELF_DESTRUCT_TUTOR_SPECIES_3RSE", SOURCES + "LearnSource3RS.cs", "SpecialTutors_XD_SelfDestruct"),
    ("SKY_ATTACK_TUTOR_SPECIES_3RSE", SOURCES + "LearnSource3RS.cs", "SpecialTutors_XD_SkyAttack"),
    ("NIGHTMARE_TUTOR_SPECIES_3RSE", SOURCES + "LearnSource3RS.cs", "SpecialTutors_XD_Nightmare"),
    ("TUTOR_MOVES_GENERATION4", INFO + "PersonalInfo4.cs", "TutorMoves"),
    ("TECHNICAL_MACHINE_MOVES_GENERATION4", INFO + "PersonalInfo4.cs", "MachineMovesTechnical"),
    ("HIDDEN_MACHINE_MOVES_4DP", INFO + "PersonalInfo4.cs", "MachineMovesHiddenDPPt"),
    ("HIDDEN_MACHINE_MOVES_4HGSS", INFO + "PersonalInfo4.cs", "MachineMovesHiddenHGSS"),
    ("BLAST_BURN_TUTOR_SPECIES_GENERATION4", INFO + "PersonalInfo4.cs", "SpecialTutorBlastBurn"),
    ("HYDRO_CANNON_TUTOR_SPECIES_GENERATION4", INFO + "PersonalInfo4.cs", "SpecialTutorHydroCannon"),
    ("FRENZY_PLANT_TUTOR_SPECIES_GENERATION4", INFO + "PersonalInfo4.cs", "SpecialTutorFrenzyPlant"),
    ("DRACO_METEOR_TUTOR_SPECIES_GENERATION4", INFO + "PersonalInfo4.cs", "SpecialTutorDracoMeteor"),
    ("MACHINE_MOVES_5BW", INFO + "PersonalInfo5BW.cs", "MachineMoves"),
    ("TYPE_TUTOR_MOVES_5BW", INFO + "PersonalInfo5BW.cs", "TypeTutorMoves"),
    ("TUTOR1_MOVES_5B2W2", INFO + "PersonalInfo5B2W2.cs", "TutorMoves1"),
    ("TUTOR2_MOVES_5B2W2", INFO + "PersonalInfo5B2W2.cs", "TutorMoves2"),
    ("TUTOR3_MOVES_5B2W2", INFO + "PersonalInfo5B2W2.cs", "TutorMoves3"),
    ("TUTOR4_MOVES_5B2W2", INFO + "PersonalInfo5B2W2.cs", "TutorMoves4"),
    ("MACHINE_MOVES_6XY", SOURCES + "LearnSource6XY.cs", "MachineMoves"),
    ("MACHINE_MOVES_6ORAS", SOURCES + "LearnSource6AO.cs", "MachineMoves"),
    ("TUTOR1_MOVES_6ORAS", INFO + "PersonalInfo6AO.cs", "TutorMoves1"),
    ("TUTOR2_MOVES_6ORAS", INFO + "PersonalInfo6AO.cs", "TutorMoves2"),
    ("TUTOR3_MOVES_6ORAS", INFO + "PersonalInfo6AO.cs", "TutorMoves3"),
    ("TUTOR4_MOVES_6ORAS", INFO + "PersonalInfo6AO.cs", "TutorMoves4"),
    ("MACHINE_MOVES_7SM", INFO + "PersonalInfo7.cs", "MachineMoves"),
    ("BATTLE_POINT_TUTOR_MOVES_7SM", INFO + "PersonalInfo7.cs", "BattlePointTutorMoves"),
    ("MACHINE_MOVES_7LGPE", SOURCES + "LearnSource7GG.cs", "MachineMoves"),
    ("PARTNER_PIKACHU_TUTOR_MOVES_7LGPE", SOURCES + "LearnSource7GG.cs", "TutorStarterPikachu"),
    ("PARTNER_EEVEE_TUTOR_MOVES_7LGPE", SOURCES + "LearnSource7GG.cs", "TutorStarterEevee"),
    ("MACHINE_MOVES_8SWSH", INFO + "PersonalInfo8SWSH.cs", "MachineMovesTechnical"),
    ("TYPE_TUTOR_MOVES_8SWSH", INFO + "PersonalInfo8SWSH.cs", "TypeTutorMoves"),
    ("SPECIAL_TUTOR_MOVES_8SWSH", INFO + "PersonalInfo8SWSH.cs", "SpecialTutorMoves"),
    ("MACHINE_MOVES_8BDSP", INFO + "PersonalInfo8BDSP.cs", "MachineMoves"),
    ("TYPE_TUTOR_MOVES_8BDSP", INFO + "PersonalInfo8BDSP.cs", "TypeTutorMoves"),
]


def personal_rows(resource, size):
    raw = _load(BYTE("personal", resource))
    if len(raw) % size != 0:
        raise SystemExit("%s: %d is not a multiple of %d" % (resource, len(raw), size))
    return [raw[index * size:(index + 1) * size] for index in range(len(raw) // size)]


def read_learnsets(resource):
    """PKHeX LearnsetReader.ReadLearnset16: move[count] (u16) then level[count] (u8); entry 0 empty."""
    result = []
    for index, entry in enumerate(bin_entries(_load(BYTE("levelup", resource)))):
        count = len(entry) // 3
        if index == 0 or count == 0:
            result.append(([], []))
            continue
        moves = list(struct.unpack_from("<%dH" % count, entry, 0))
        levels = list(entry[count * 2:count * 3])
        result.append((moves, levels))
    return result


def read_move_sources(path):
    """PKHeX MoveSource.GetArray: a flat u16 list per entry; entry 0 empty."""
    result = []
    for index, entry in enumerate(bin_entries(_load(path))):
        count = len(entry) // 2
        result.append([] if index == 0 or count == 0 else list(struct.unpack_from("<%dH" % count, entry, 0)))
    return result


def read_stadium(resource):
    """PKHeX LearnsetStadium: (level, move, source) byte triples per species. MemoryMarshal.Cast drops
    the padding byte some entries end with, so a trailing partial triple is not data."""
    result = []
    for entry in bin_entries(_load(BYTE("levelup", resource))):
        result.append(bytes(entry[:len(entry) - len(entry) % 3]))
    return result


def wrap(values, per_line=16, indent="        "):
    lines = []
    for start in range(0, len(values), per_line):
        lines.append(indent + ", ".join(values[start:start + per_line]) + ",")
    return "\n".join(lines) if lines else indent + "0,"


class Emitter:
    def __init__(self):
        self.parts = []
        self.byte_total = 0

    def array(self, type_name, name, values, per_line=16, element_size=2):
        self.parts.append("        const %s %s[] = {\n%s\n        };\n\n" % (type_name, name, wrap([str(value) for value in values], per_line)))
        self.byte_total += max(1, len(values)) * element_size

    def offsets(self, name, lists):
        offsets = [0]
        for values in lists:
            offsets.append(offsets[-1] + len(values))
        if offsets[-1] > 0xFFFF:
            raise SystemExit("%s: %d entries overflow a uint16_t offset" % (name, offsets[-1]))
        self.array("uint16_t", name, offsets)
        return offsets


def everstone_form(row, local_offset, flags_offset, regional_offset):
    local_form = struct.unpack_from("<H", row, local_offset)[0] & 0xFF
    regional = struct.unpack_from("<H", row, flags_offset)[0] & 1
    regional_form = struct.unpack_from("<H", row, regional_offset)[0] & 0xFF
    return regional_form if regional else local_form


def main():
    emitter = Emitter()
    table_entries = []
    region_constants = []

    gen3_machine = bin_entries32(_load(BYTE("personal", GEN3_MACHINE_RESOURCE)))
    gen3_tutor = bin_entries32(_load(BYTE("personal", GEN3_TUTOR_RESOURCE)))
    hgss_tutor = bin_entries32(_load(BYTE("personal", HGSS_TUTOR_RESOURCE)))

    for name, levelup, personal, size, form_index_offset, form_count_offset, egg, egg_index in GAMES:
        rows = personal_rows(personal, size)
        learnsets = read_learnsets(levelup)
        prefix = "GAME_" + name

        moves_flat = [move for moves, _ in learnsets for move in moves]
        levels_flat = [level for _, levels in learnsets for level in levels]
        emitter.array("uint16_t", prefix + "_LEVEL_UP_MOVES", moves_flat)
        emitter.array("uint8_t", prefix + "_LEVEL_UP_LEVELS", levels_flat, 24, 1)
        emitter.offsets(prefix + "_LEVEL_UP_OFFSETS", [moves for moves, _ in learnsets])

        egg_fields = "nullptr, nullptr, 0"
        if egg:
            egg_moves = read_move_sources(BYTE("eggmove", egg))
            emitter.array("uint16_t", prefix + "_EGG_MOVES", [move for moves in egg_moves for move in moves])
            emitter.offsets(prefix + "_EGG_OFFSETS", egg_moves)
            egg_fields = "%s_EGG_MOVES, %s_EGG_OFFSETS, %d" % (prefix, prefix, len(egg_moves))

        reminder_fields = "nullptr, nullptr, 0"
        if name == "SV":
            reminder = read_move_sources(BYTE("personal", "reminder_sv.pkl"))
            emitter.array("uint16_t", prefix + "_REMINDER_MOVES", [move for moves in reminder for move in moves])
            emitter.offsets(prefix + "_REMINDER_OFFSETS", reminder)
            reminder_fields = "%s_REMINDER_MOVES, %s_REMINDER_OFFSETS, %d" % (prefix, prefix, len(reminder))

        # Permission rows: the personal regions, then any resource PKHeX loads beside the table.
        regions = list(PERMISSION_REGIONS.get(name, []))
        permission_rows = [bytearray() for _ in rows]
        region_offset = 0
        for region_name, offset, byte_count in regions:
            for row_index, row in enumerate(rows):
                permission_rows[row_index] += row[offset:offset + byte_count]
            region_constants.append("    inline constexpr int PERMISSION_%s_%s = %d;" % (region_name, name, region_offset))
            region_offset += byte_count
        if name in ("RS", "E", "FR", "LG"):
            if len(gen3_machine) != len(rows) or len(gen3_tutor) != len(rows):
                raise SystemExit("Gen 3 machine/tutor rows do not match %s" % personal)
            for row_index in range(len(rows)):
                permission_rows[row_index] += gen3_machine[row_index][:8].ljust(8, b"\0") + gen3_tutor[row_index][:4].ljust(4, b"\0")
            region_constants.append("    inline constexpr int PERMISSION_MACHINE_%s = 0;" % name)
            region_constants.append("    inline constexpr int PERMISSION_TUTOR_%s = 8;" % name)
            region_offset = 12
        if name == "HGSS":
            if len(hgss_tutor) != len(rows):
                raise SystemExit("tutors_g4 has %d rows, personal_hgss %d" % (len(hgss_tutor), len(rows)))
            for row_index in range(len(rows)):
                permission_rows[row_index] += hgss_tutor[row_index][:8].ljust(8, b"\0")
            region_constants.append("    inline constexpr int PERMISSION_TUTOR_%s = %d;" % (name, region_offset))
            region_offset += 8
        permission_fields = "nullptr, 0"
        if region_offset:
            emitter.array("uint8_t", prefix + "_PERMISSIONS", ["0x%02X" % value for row in permission_rows for value in row], 24, 1)
            permission_fields = "%s_PERMISSIONS, %d" % (prefix, region_offset)

        hatch_fields = "nullptr, nullptr"
        if name in HATCH:
            species_offset, form_source = HATCH[name]
            hatch_species = [struct.unpack_from("<H", row, species_offset)[0] for row in rows]
            if form_source[0] == "everstone":
                hatch_forms = [everstone_form(row, form_source[1], form_source[2], form_source[3]) for row in rows]
            else:
                hatch_forms = [struct.unpack_from("<H", row, form_source[1])[0] & 0xFF for row in rows]
            emitter.array("uint16_t", prefix + "_HATCH_SPECIES", hatch_species)
            emitter.array("uint8_t", prefix + "_HATCH_FORMS", hatch_forms, 24, 1)
            hatch_fields = "%s_HATCH_SPECIES, %s_HATCH_FORMS" % (prefix, prefix)

        breeding_fields = "nullptr"
        if name in BREEDING_FIELDS:
            breeding_values = [value for row in rows for value in breeding_row(row, BREEDING_FIELDS[name])]
            emitter.array("uint8_t", prefix + "_BREEDING", breeding_values, 24, 1)
            breeding_fields = prefix + "_BREEDING"

        form_fields = "nullptr, nullptr"
        if form_index_offset is not None:
            species_count = MAX_SPECIES[name] + 1
            form_indexes = [struct.unpack_from("<H", rows[species], form_index_offset)[0] for species in range(species_count)]
            form_counts = [rows[species][form_count_offset] for species in range(species_count)]
            emitter.array("uint16_t", prefix + "_FORM_STATS_INDEX", form_indexes)
            emitter.array("uint8_t", prefix + "_FORM_COUNT", form_counts, 24, 1)
            form_fields = "%s_FORM_STATS_INDEX, %s_FORM_COUNT" % (prefix, prefix)

        table_entries.append("        {%s_LEVEL_UP_MOVES, %s_LEVEL_UP_LEVELS, %s_LEVEL_UP_OFFSETS, %d, %s, %s, %s, %d, %s, %s, %s, %d}," % (prefix, prefix, prefix, len(learnsets), egg_fields, reminder_fields, permission_fields, len(rows), hatch_fields, breeding_fields, form_fields, MAX_SPECIES[name]))

    stadium = read_stadium("reminder_stad2.pkl")
    emitter.array("uint8_t", "STADIUM2_REMINDER_TUPLES", ["0x%02X" % value for entry in stadium for value in entry], 24, 1)
    stadium_offsets = [0]
    for entry in stadium:
        stadium_offsets.append(stadium_offsets[-1] + len(entry) // 3)
    emitter.array("uint16_t", "STADIUM2_REMINDER_OFFSETS", stadium_offsets)

    move_list_declarations = []
    move_list_definitions = []
    for cpp_name, source, array_name in MOVE_LISTS:
        values = parse_move_list(source, array_name)
        move_list_declarations.append("    inline constexpr int %s_COUNT = %d;\n    extern const uint16_t %s[%s_COUNT];" % (cpp_name, len(values), cpp_name, cpp_name))
        move_list_definitions.append("    const uint16_t %s[%s_COUNT] = {\n%s\n    };\n" % (cpp_name, cpp_name, wrap([str(value) for value in values], 16)))
        emitter.byte_total += len(values) * 2

    enumerators = "\n".join("        %s," % game[0] for game in GAMES)
    header = """/**
 * Auto-generated by tools/gen_learnsources.py from PKHeX %s. DO NOT EDIT BY HAND -- rerun the
 * generator instead.
 *
 * The per-game move data PKHeX's learn sources read (PKHeX.Core/Legality/LearnSource/Sources), in
 * PKHeX's shape: level-up learnsets with levels, egg moves, Scarlet/Violet reminder moves, Stadium
 * 2's reminder, the TM/HM/tutor permission bytes of each personal row and the move lists they index.
 * Legality::LearnSource reads it; LearnsetTable.h remains the move picker's union bitset.
 */
#ifndef POKEMON_LEARNSOURCETABLE_H
#define POKEMON_LEARNSOURCETABLE_H

#include <cstdint>

namespace Pokemon
{
    /// Every game PKHeX keeps a learn source for, in PKHeX LearnEnvironment order.
    enum class LearnSourceGame : uint8_t
    {
%s
        Count,
    };

    struct LearnSourceTables
    {
        /// Level-up learnsets: entry N's moves are levelUpMoves[levelUpOffsets[N] .. levelUpOffsets[N + 1]).
        const uint16_t *levelUpMoves;
        const uint8_t *levelUpLevels;
        const uint16_t *levelUpOffsets;
        uint16_t levelUpCount;
        const uint16_t *eggMoves;
        const uint16_t *eggOffsets;
        uint16_t eggCount;
        const uint16_t *reminderMoves;
        const uint16_t *reminderOffsets;
        uint16_t reminderCount;
        /// permissionRowSize bytes per personal row, laid out by the PERMISSION_* offsets below.
        const uint8_t *permissions;
        uint8_t permissionRowSize;
        uint16_t personalRowCount;
        /// Per personal row (Sword/Shield, BDSP, Scarlet/Violet only).
        const uint16_t *hatchSpecies;
        const uint8_t *hatchForms;
        /// BREEDING_ROW_SIZE bytes per personal row, laid out by the BREEDING_* offsets below; null
        /// for the games with no breeding (Red/Blue, Yellow).
        const uint8_t *breeding;
        /// Per species 0..maxSpecies (Gen 4 on): PKHeX FormStatsIndex and FormCount.
        const uint16_t *formStatsIndex;
        const uint8_t *formCounts;
        uint16_t maxSpecies;
    };

    const LearnSourceTables &getLearnSourceTables(LearnSourceGame game) noexcept;

    /// Byte offset of each permission region within a game's permission row.
%s

    /// A breeding row: PKHeX PersonalInfo.Gender (0 male only, 254 female only, 255 genderless),
    /// EggGroup1, EggGroup2, and 1 when the row is in the game (PKHeX PersonalInfo8SWSH/8BDSP/8LA/
    /// 9SV/9ZA.IsPresentInGame; 1 for every row of the other games, which have no such flag).
    inline constexpr int BREEDING_ROW_SIZE = 4;
    inline constexpr int BREEDING_GENDER = 0;
    inline constexpr int BREEDING_EGG_GROUP1 = 1;
    inline constexpr int BREEDING_EGG_GROUP2 = 2;
    inline constexpr int BREEDING_PRESENT = 3;

    /// Stadium 2's move reminder: species N's (level, move, source) triples are
    /// STADIUM2_REMINDER_TUPLES[3 * STADIUM2_REMINDER_OFFSETS[N] .. 3 * STADIUM2_REMINDER_OFFSETS[N + 1]).
    inline constexpr int STADIUM2_REMINDER_SPECIES_COUNT = %d;
    extern const uint8_t STADIUM2_REMINDER_TUPLES[];
    extern const uint16_t STADIUM2_REMINDER_OFFSETS[];

%s
}

#endif
""" % (pkhex_ref(), enumerators, "\n".join(region_constants), len(stadium), "\n\n".join(move_list_declarations))

    source = """/**
 * Auto-generated by tools/gen_learnsources.py from PKHeX %s. DO NOT EDIT BY HAND -- rerun the
 * generator instead. See include/Pokemon/LearnSourceTable.h.
 */
#include "Pokemon/LearnSourceTable.h"

namespace Pokemon
{
    namespace
    {
%s    }

    const uint8_t STADIUM2_REMINDER_TUPLES[] = {
%s
    };

    const uint16_t STADIUM2_REMINDER_OFFSETS[] = {
%s
    };

%s
    const LearnSourceTables &getLearnSourceTables(LearnSourceGame game) noexcept
    {
        static const LearnSourceTables tables[static_cast<int>(LearnSourceGame::Count)] = {
%s
        };
        return tables[static_cast<int>(game) < static_cast<int>(LearnSourceGame::Count) ? static_cast<int>(game) : 0];
    }
}
""" % (pkhex_ref(), "".join(part for part in emitter.parts if "STADIUM2" not in part), wrap(["0x%02X" % value for entry in stadium for value in entry], 24), wrap([str(value) for value in stadium_offsets], 16), "\n".join(move_list_definitions), "\n".join(table_entries))

    with open(OUT_H, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(header)
    with open(OUT_CPP, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(source)
    print("wrote %s and %s (~%d KB of data)" % (OUT_H, OUT_CPP, emitter.byte_total // 1024))


if __name__ == "__main__":
    main()
