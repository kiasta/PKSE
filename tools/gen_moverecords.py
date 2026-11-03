#!/usr/bin/env python3
"""Generate include/Pokemon/MoveRecordTable.h + src/Pokemon/MoveRecordTable.cpp: the per-species
move permission tables PKHeX's MiscVerifier, LegendsArceusVerifier and LegendsZAVerifier read.

Every table is indexed by the PKHeX personal row (PersonalTable.GetFormIndex), which is the same
row PKSE's PersonalInfo<group> tables use, so the lookups resolve a (species, form) through the
generated PERSONAL_<group> arrays rather than keeping a second form index.

  * Sword/Shield Technical Record permissions (PersonalInfo8SWSH.GetIsLearnTR), and the move each
    record index teaches (MachineMovesRecord). PK8, PB8 and PA8 keep these flags.
  * Legends: Arceus move shop permissions (PersonalInfo8LA move shop bits) and the shop's moves.
  * Scarlet/Violet and Legends: Z-A Technical Machine permissions (GetIsLearnTM) and the moves.
  * Legends: Z-A Plus moves (PersonalInfo9ZA.PlusMoves), each row's Alpha move and Lumiose dex
    membership (IsLumioseNative).
  * Legends: Arceus base height and weight (PersonalInfo8LA.Height/Weight), which its absolute
    sizes are computed from.
  * Level-up learnsets WITH levels for Legends: Arceus and Legends: Z-A (lvlmove_la, lvlmove_za)
    and Z-A's Plus levels (plus_za): LearnsetTable.h keeps only a learnable-move bitset.
  * Legends: Arceus mastery levels (mastery_la): the level each move-shop move is mastered at, which
    PKHeX's IsForcedMasteryCorrect reads for an encounter's initial mastery flags.

Run:  PKHEX_LOCAL=~/repos/PKHeX python tools/gen_moverecords.py
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pkhex_source import pkhex_ref  # noqa: E402
from gen_learnsets import BYTE, MISC, _load, bin_entries, parse_move_list  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_H = os.path.join(ROOT, "include", "Pokemon", "MoveRecordTable.h")
OUT_CPP = os.path.join(ROOT, "src", "Pokemon", "MoveRecordTable.cpp")

INFO = "PersonalInfo/Info/"

# (group code, personal resource, row size)
PERSONAL = {
    "8SWSH": ("personal_swsh", 0xB0),
    "8LA": ("personal_la", 0xB0),
    "9SV": ("personal_sv", 0x50),
    "9LZA": ("personal_za", 0x50),
}

# Offsets from PKHeX.Core/PersonalInfo/Info/PersonalInfo*.cs.
SWSH_TR_OFFSET = 0x3C
SWSH_TR_BYTES = 13
LA_MOVE_SHOP_OFFSET = 0xA8
LA_HEIGHT_OFFSET = 0x24
LA_WEIGHT_OFFSET = 0x26
SV_TM_OFFSET = 0x2C
ZA_TM_OFFSET = 0x2C
TM_BYTES = 29
ZA_DEX_INDEX_OFFSET = 0x1E
ZA_ALPHA_MOVE_OFFSET = 0x4E
ZA_LUMIOSE_DEX_LAST = 232


def personal_rows(group):
    resource, size = PERSONAL[group]
    raw = _load(BYTE("personal", resource))
    if len(raw) % size != 0:
        raise SystemExit("%s: %d is not a multiple of %d" % (resource, len(raw), size))
    return [raw[index * size:(index + 1) * size] for index in range(len(raw) // size)]


def read_learnsets(path, row_count):
    """lvlmove / plus blob -> per-row list of (move, level), PKHeX LearnsetReader.ReadLearnset16."""
    entries = bin_entries(_load(path))
    if len(entries) != row_count:
        raise SystemExit("%s has %d entries, the personal table %d" % (path, len(entries), row_count))
    result = []
    for entry in entries:
        count = len(entry) // 3
        moves = struct.unpack_from("<%dH" % count, entry, 0) if count else ()
        levels = entry[count * 2:count * 3]
        result.append(list(zip(moves, levels)))
    result[0] = []
    return result


INNER = "            "


def wrap(values, per_line=16, indent="        "):
    lines = []
    for start in range(0, len(values), per_line):
        lines.append(indent + ", ".join(values[start:start + per_line]) + ",")
    return "\n".join(lines)


def emit_learnset(name, learnsets):
    flat = []
    offsets = [0]
    for moves in learnsets:
        flat.extend(moves)
        offsets.append(len(flat))
    if len(flat) > 0xFFFF:
        raise SystemExit("%s: %d moves overflow a uint16_t offset" % (name, len(flat)))
    text = []
    text.append("        const LevelUpMove %s_MOVES[] = {\n" % name)
    text.append(wrap(["{%d, %d}" % (move, level) for move, level in flat], 10, INNER) + "\n")
    text.append("        };\n\n")
    text.append("        const uint16_t %s_OFFSETS[] = {\n" % name)
    text.append(wrap([str(offset) for offset in offsets], 16, INNER) + "\n")
    text.append("        };\n\n")
    return "".join(text), len(flat) * 4 + len(offsets) * 2


def emit_bits(name, rows, offset, byte_count):
    text = ["        const uint8_t %s[][%d] = {\n" % (name, byte_count)]
    for row in rows:
        text.append("            {" + ", ".join("0x%02X" % value for value in row[offset:offset + byte_count]) + "},\n")
    text.append("        };\n\n")
    return "".join(text), len(rows) * byte_count


def emit_moves(name, moves):
    return "    const uint16_t %s[%d] = {\n%s\n    };\n\n" % (name, len(moves), wrap([str(move) for move in moves], 16))


def main():
    swsh = personal_rows("8SWSH")
    legends_arceus = personal_rows("8LA")
    scarlet_violet = personal_rows("9SV")
    legends_za = personal_rows("9LZA")

    tr_moves = parse_move_list(INFO + "PersonalInfo8SWSH.cs", "MachineMovesRecord", 100)
    move_shop_moves = parse_move_list(INFO + "PersonalInfo8LA.cs", "MoveShopMoves", 61)
    tm_moves_sv = parse_move_list(INFO + "PersonalInfo9SV.cs", "MachineMoves")
    tm_moves_za = parse_move_list(INFO + "PersonalInfo9ZA.cs", "MachineMoves")
    plus_moves = parse_move_list(INFO + "PersonalInfo9ZA.cs", "PlusMoves")

    level_up_la = read_learnsets(BYTE("levelup", "lvlmove_la.pkl"), len(legends_arceus))
    level_up_za = read_learnsets(BYTE("levelup", "lvlmove_za.pkl"), len(legends_za))
    plus_za = read_learnsets(MISC("plus_za.pkl"), len(legends_za))
    mastery_la = read_learnsets(MISC("mastery_la.pkl"), len(legends_arceus))

    header = """/**
 * Auto-generated by tools/gen_moverecords.py from PKHeX %s. DO NOT EDIT BY HAND -- rerun the
 * generator instead.
 *
 * The per-species move permissions PKHeX's legality checker reads for the Switch formats: Sword/
 * Shield Technical Records, the Legends: Arceus move shop, Scarlet/Violet and Legends: Z-A
 * Technical Machines and Plus moves, and the level-up learnsets of the two Legends games with
 * their levels. Every lookup takes the (species, form) through PKHeX's personal-table form index;
 * a species the game's table does not have answers "nothing permitted" and an empty learnset.
 */
#ifndef POKEMON_MOVERECORDTABLE_H
#define POKEMON_MOVERECORDTABLE_H

#include <cstdint>
#include <span>

namespace Pokemon
{
    struct LevelUpMove
    {
        uint16_t moveId;
        uint8_t level;
    };

    /// Sword/Shield Technical Records, in record-flag order (PKHeX PersonalInfo8SWSH.MachineMovesRecord).
    inline constexpr int TR_COUNT_8SWSH = %d;
    extern const uint16_t TR_MOVES_8SWSH[TR_COUNT_8SWSH];
    bool isTRPermitted8SWSH(uint16_t speciesId, uint8_t formId, int recordIndex);

    /// The Legends: Arceus move shop, in purchase-flag order (PKHeX PersonalInfo8LA.MoveShopMoves).
    inline constexpr int MOVE_SHOP_COUNT_8LA = %d;
    extern const uint16_t MOVE_SHOP_MOVES_8LA[MOVE_SHOP_COUNT_8LA];
    /// One bit per shop index the (species, form) may buy; 0 when it has no move shop at all.
    uint64_t getMoveShopPermissions8LA(uint16_t speciesId, uint8_t formId);

    /// The base height and weight Legends: Arceus computes absolute sizes from; false for a species
    /// the table does not have.
    bool getBaseSize8LA(uint16_t speciesId, uint8_t formId, uint16_t &baseHeight, uint16_t &baseWeight);

    /// Technical Machines, in TM-flag order (PKHeX PersonalInfo9SV/PersonalInfo9ZA.MachineMoves).
    inline constexpr int TM_COUNT_9SV = %d;
    extern const uint16_t TM_MOVES_9SV[TM_COUNT_9SV];
    bool isTMPermitted9SV(uint16_t speciesId, uint8_t formId, int machineIndex);
    inline constexpr int TM_COUNT_9LZA = %d;
    extern const uint16_t TM_MOVES_9LZA[TM_COUNT_9LZA];
    bool isTMPermitted9LZA(uint16_t speciesId, uint8_t formId, int machineIndex);

    /// Legends: Z-A Plus moves, in Plus-flag order (PKHeX PersonalInfo9ZA.PlusMoves).
    inline constexpr int PLUS_MOVE_COUNT_9LZA = %d;
    extern const uint16_t PLUS_MOVES_9LZA[PLUS_MOVE_COUNT_9LZA];
    /// The move a wild Alpha of this (species, form) comes with (PKHeX PersonalInfo9ZA.AlphaMove).
    uint16_t getAlphaMove9LZA(uint16_t speciesId, uint8_t formId);
    /// In the base game's Lumiose dex (PKHeX PersonalInfo9ZA.IsLumioseNative).
    bool isLumioseNative9LZA(uint16_t speciesId, uint8_t formId);

    /// Level-up learnsets with levels, in learn order (PKHeX lvlmove_la / lvlmove_za).
    std::span<const LevelUpMove> getLevelUpMoves8LA(uint16_t speciesId, uint8_t formId);
    std::span<const LevelUpMove> getLevelUpMoves9LZA(uint16_t speciesId, uint8_t formId);
    /// The levels at which Legends: Z-A marks a move as a Plus move (PKHeX plus_za).
    std::span<const LevelUpMove> getPlusLevels9LZA(uint16_t speciesId, uint8_t formId);
    /// The levels at which Legends: Arceus masters a move (PKHeX mastery_la).
    std::span<const LevelUpMove> getMasteryLevels8LA(uint16_t speciesId, uint8_t formId);
}

#endif
""" % (pkhex_ref(), len(tr_moves), len(move_shop_moves), len(tm_moves_sv), len(tm_moves_za), len(plus_moves))

    source = []
    source.append("// Auto-generated by tools/gen_moverecords.py from PKHeX %s. DO NOT EDIT BY HAND.\n" % pkhex_ref())
    source.append('#include "Pokemon/MoveRecordTable.h"\n\n')
    source.append("#include <cstddef>\n\n")
    source.append('#include "Pokemon/PersonalInfo8LA.h"\n')
    source.append('#include "Pokemon/PersonalInfo8SWSH.h"\n')
    source.append('#include "Pokemon/PersonalInfo9LZA.h"\n')
    source.append('#include "Pokemon/PersonalInfo9SV.h"\n\n')
    source.append("namespace Pokemon\n{\n")
    source.append(emit_moves("TR_MOVES_8SWSH", tr_moves))
    source.append(emit_moves("MOVE_SHOP_MOVES_8LA", move_shop_moves))
    source.append(emit_moves("TM_MOVES_9SV", tm_moves_sv))
    source.append(emit_moves("TM_MOVES_9LZA", tm_moves_za))
    source.append(emit_moves("PLUS_MOVES_9LZA", plus_moves))
    source.append("    namespace\n    {\n")
    total = 0
    for name, rows, offset, byte_count in [
        ("TR_PERMISSIONS_8SWSH", swsh, SWSH_TR_OFFSET, SWSH_TR_BYTES),
        ("MOVE_SHOP_PERMISSIONS_8LA", legends_arceus, LA_MOVE_SHOP_OFFSET, 8),
        ("TM_PERMISSIONS_9SV", scarlet_violet, SV_TM_OFFSET, TM_BYTES),
        ("TM_PERMISSIONS_9LZA", legends_za, ZA_TM_OFFSET, TM_BYTES),
    ]:
        text, size = emit_bits(name, rows, offset, byte_count)
        source.append(text)
        total += size
    base_sizes = ["{%d, %d}" % (struct.unpack_from("<H", row, LA_HEIGHT_OFFSET)[0], struct.unpack_from("<H", row, LA_WEIGHT_OFFSET)[0]) for row in legends_arceus]
    source.append("        const uint16_t BASE_SIZES_8LA[][2] = {\n%s\n        };\n\n" % wrap(base_sizes, 10, INNER))
    total += len(legends_arceus) * 4
    alpha_moves = [str(struct.unpack_from("<H", row, ZA_ALPHA_MOVE_OFFSET)[0]) for row in legends_za]
    source.append("        const uint16_t ALPHA_MOVES_9LZA[] = {\n%s\n        };\n\n" % wrap(alpha_moves, 16, INNER))
    dex_indexes = [struct.unpack_from("<H", row, ZA_DEX_INDEX_OFFSET)[0] for row in legends_za]
    lumiose = ["1" if 0 < dex_index <= ZA_LUMIOSE_DEX_LAST else "0" for dex_index in dex_indexes]
    source.append("        const uint8_t LUMIOSE_NATIVE_9LZA[] = {\n%s\n        };\n\n" % wrap(lumiose, 32, INNER))
    total += len(legends_za) * 3
    for name, learnsets in [("LEVEL_UP_8LA", level_up_la), ("LEVEL_UP_9LZA", level_up_za), ("PLUS_LEVELS_9LZA", plus_za), ("MASTERY_LEVELS_8LA", mastery_la)]:
        text, size = emit_learnset(name, learnsets)
        source.append(text)
        total += size
    source.append("""        /// PKHeX PersonalTable.GetFormIndex; -1 for a species past the table.
        int getPersonalRowIndex(const PersonalRecord *rows, size_t rowCount, uint16_t maxSpeciesId, uint16_t speciesId, uint8_t formId)
        {
            if (speciesId > maxSpeciesId)
            {
                return -1;
            }
            const PersonalRecord &base = rows[speciesId];
            if (formId == 0 || base.formIndex == 0 || formId >= base.formCount)
            {
                return speciesId;
            }
            const size_t formRowIndex = static_cast<size_t>(base.formIndex) + formId - 1;
            return formRowIndex < rowCount ? static_cast<int>(formRowIndex) : -1;
        }

        int getRowIndex8SWSH(uint16_t speciesId, uint8_t formId) { return getPersonalRowIndex(PERSONAL_8SWSH, PERSONAL_COUNT_8SWSH, PERSONAL_MAX_SPECIES_8SWSH, speciesId, formId); }
        int getRowIndex8LA(uint16_t speciesId, uint8_t formId) { return getPersonalRowIndex(PERSONAL_8LA, PERSONAL_COUNT_8LA, PERSONAL_MAX_SPECIES_8LA, speciesId, formId); }
        int getRowIndex9SV(uint16_t speciesId, uint8_t formId) { return getPersonalRowIndex(PERSONAL_9SV, PERSONAL_COUNT_9SV, PERSONAL_MAX_SPECIES_9SV, speciesId, formId); }
        int getRowIndex9LZA(uint16_t speciesId, uint8_t formId) { return getPersonalRowIndex(PERSONAL_9LZA, PERSONAL_COUNT_9LZA, PERSONAL_MAX_SPECIES_9LZA, speciesId, formId); }

        bool isBitSet(const uint8_t *bits, int bitIndex)
        {
            return ((bits[bitIndex >> 3] >> (bitIndex & 7)) & 1) != 0;
        }

        std::span<const LevelUpMove> getLearnset(const LevelUpMove *moves, const uint16_t *offsets, int rowIndex)
        {
            if (rowIndex < 0)
            {
                return {};
            }
            return std::span<const LevelUpMove>(moves + offsets[rowIndex], moves + offsets[rowIndex + 1]);
        }
    }

    bool isTRPermitted8SWSH(uint16_t speciesId, uint8_t formId, int recordIndex)
    {
        const int rowIndex = getRowIndex8SWSH(speciesId, formId);
        return rowIndex >= 0 && recordIndex >= 0 && recordIndex < TR_COUNT_8SWSH && isBitSet(TR_PERMISSIONS_8SWSH[rowIndex], recordIndex);
    }

    uint64_t getMoveShopPermissions8LA(uint16_t speciesId, uint8_t formId)
    {
        const int rowIndex = getRowIndex8LA(speciesId, formId);
        if (rowIndex < 0)
        {
            return 0;
        }
        uint64_t permissions = 0;
        for (int byteIndex = 7; byteIndex >= 0; --byteIndex)
        {
            permissions = (permissions << 8) | MOVE_SHOP_PERMISSIONS_8LA[rowIndex][byteIndex];
        }
        return permissions;
    }

    bool getBaseSize8LA(uint16_t speciesId, uint8_t formId, uint16_t &baseHeight, uint16_t &baseWeight)
    {
        const int rowIndex = getRowIndex8LA(speciesId, formId);
        if (rowIndex < 0)
        {
            return false;
        }
        baseHeight = BASE_SIZES_8LA[rowIndex][0];
        baseWeight = BASE_SIZES_8LA[rowIndex][1];
        return true;
    }

    bool isTMPermitted9SV(uint16_t speciesId, uint8_t formId, int machineIndex)
    {
        const int rowIndex = getRowIndex9SV(speciesId, formId);
        return rowIndex >= 0 && machineIndex >= 0 && machineIndex < TM_COUNT_9SV && isBitSet(TM_PERMISSIONS_9SV[rowIndex], machineIndex);
    }

    bool isTMPermitted9LZA(uint16_t speciesId, uint8_t formId, int machineIndex)
    {
        const int rowIndex = getRowIndex9LZA(speciesId, formId);
        return rowIndex >= 0 && machineIndex >= 0 && machineIndex < TM_COUNT_9LZA && isBitSet(TM_PERMISSIONS_9LZA[rowIndex], machineIndex);
    }

    uint16_t getAlphaMove9LZA(uint16_t speciesId, uint8_t formId)
    {
        const int rowIndex = getRowIndex9LZA(speciesId, formId);
        return rowIndex >= 0 ? ALPHA_MOVES_9LZA[rowIndex] : 0;
    }

    bool isLumioseNative9LZA(uint16_t speciesId, uint8_t formId)
    {
        const int rowIndex = getRowIndex9LZA(speciesId, formId);
        return rowIndex >= 0 && LUMIOSE_NATIVE_9LZA[rowIndex] != 0;
    }

    std::span<const LevelUpMove> getLevelUpMoves8LA(uint16_t speciesId, uint8_t formId) { return getLearnset(LEVEL_UP_8LA_MOVES, LEVEL_UP_8LA_OFFSETS, getRowIndex8LA(speciesId, formId)); }
    std::span<const LevelUpMove> getLevelUpMoves9LZA(uint16_t speciesId, uint8_t formId) { return getLearnset(LEVEL_UP_9LZA_MOVES, LEVEL_UP_9LZA_OFFSETS, getRowIndex9LZA(speciesId, formId)); }
    std::span<const LevelUpMove> getPlusLevels9LZA(uint16_t speciesId, uint8_t formId) { return getLearnset(PLUS_LEVELS_9LZA_MOVES, PLUS_LEVELS_9LZA_OFFSETS, getRowIndex9LZA(speciesId, formId)); }
    std::span<const LevelUpMove> getMasteryLevels8LA(uint16_t speciesId, uint8_t formId) { return getLearnset(MASTERY_LEVELS_8LA_MOVES, MASTERY_LEVELS_8LA_OFFSETS, getRowIndex8LA(speciesId, formId)); }
}
""")
    body = "".join(source)
    with open(OUT_H, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(header)
    with open(OUT_CPP, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(body)
    print("Wrote", OUT_H)
    print("Wrote", OUT_CPP)
    print("  table data: %.1f KB" % (total / 1024.0))


if __name__ == "__main__":
    main()
