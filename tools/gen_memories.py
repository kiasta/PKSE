#!/usr/bin/env python3
"""Generate include/Legality/Verifiers/MemoryTable.h from PKHeX's trainer-memory rules, for the
legality checker's memory verifier (PKHeX MemoryVerifier).

PKHeX keeps these as C# spans next to the logic that reads them:

    Legality/Restrictions/Memories/Memories.cs           ArgTypes
    Legality/Restrictions/Memories/MemoryContext6Data.cs X/Y and Omega Ruby/Alpha Sapphire tables
    Legality/Restrictions/Memories/MemoryContext8Data.cs Sword/Shield tables
    Legality/Restrictions/Memories/MemoryPermissions.cs  fishing and Dynamax species lists
    Items/ItemStorage6XY.cs                              the berries a memory can name as planted

Species lists are written in PKHeX as `(int)Name`; they are resolved through Game/Enums/Species.cs.
Every table is emitted header-only as an `inline constexpr` array.

Regenerate with:  python tools/gen_memories.py
Pulls the PKHeX sources it reads from GitHub on demand (tools/pkhex_source.py); PKHEX_LOCAL points it at
a checkout instead.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pkhex_source import pkhex_path  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_H = os.path.join(ROOT, "include", "Legality", "Verifiers", "MemoryTable.h")

MEMORIES = "Legality/Restrictions/Memories/"

# (emitted name, element type, source file, span name, comment)
TABLES = [
    ("MEMORY_ARGUMENT_TYPES", "uint8_t", MEMORIES + "Memories.cs", "ArgTypes", "Per memory id, what its variable names (PKHeX MemoryArgType)."),
    ("MEMORIES_NOT_IN_XY", "uint8_t", MEMORIES + "MemoryContext6Data.cs", "Memory_NotXY", "Memory ids X/Y cannot record."),
    ("MEMORIES_NOT_IN_ORAS", "uint8_t", MEMORIES + "MemoryContext6Data.cs", "Memory_NotAO", "Memory ids Omega Ruby/Alpha Sapphire cannot record."),
    ("HIDDEN_MACHINE_MEMORIES", "uint8_t", MEMORIES + "MemoryContext6Data.cs", "MoveSpecificMemoryHM", "Memory ids that record a field move, in HM order (Cut, Fly, Surf, Strength, Waterfall, Rock Smash, Dive)."),
    ("POKEMON_CENTER_LOCATIONS_XY", "uint8_t", MEMORIES + "MemoryContext6Data.cs", "LocationsWithPokeCenter_XY", "Kalos locations with a Pokemon Center."),
    ("POKEMON_CENTER_LOCATIONS_ORAS", "uint8_t", MEMORIES + "MemoryContext6Data.cs", "LocationsWithPokeCenter_AO", "Hoenn locations with a Pokemon Center."),
    ("MEMORY_MINIMUM_INTENSITY_GENERATION_6", "uint8_t", MEMORIES + "MemoryContext6Data.cs", "MemoryMinIntensity", "Lowest intensity each memory id can have in Generation 6."),
    ("MEMORY_FEELINGS_GENERATION_6", "uint32_t", MEMORIES + "MemoryContext6Data.cs", "MemoryFeelings", "Per memory id, one bit per feeling it can have in Generation 6."),
    ("PURCHASABLE_ITEMS_XY", "uint16_t", MEMORIES + "MemoryContext6Data.cs", "PurchaseableItemXY", "Items an X/Y shop sells."),
    ("PURCHASABLE_ITEMS_ORAS", "uint16_t", MEMORIES + "MemoryContext6Data.cs", "PurchaseableItemAO", "Items an Omega Ruby/Alpha Sapphire shop sells."),
    ("LOTO_ID_PRIZES_GENERATION_6", "uint16_t", MEMORIES + "MemoryContext6Data.cs", "LotoPrizeXYAO", "Loto-ID prizes in Generation 6."),
    ("CAPTURE_FLAGS_X", "uint8_t", MEMORIES + "MemoryContext6Data.cs", "CaptureFlagsX", "One bit per species that can be caught in X."),
    ("CAPTURE_FLAGS_Y", "uint8_t", MEMORIES + "MemoryContext6Data.cs", "CaptureFlagsY", "One bit per species that can be caught in Y."),
    ("CAPTURE_FLAGS_AS", "uint8_t", MEMORIES + "MemoryContext6Data.cs", "CaptureFlagsAS", "One bit per species that can be caught in Alpha Sapphire."),
    ("CAPTURE_FLAGS_OR", "uint8_t", MEMORIES + "MemoryContext6Data.cs", "CaptureFlagsOR", "One bit per species that can be caught in Omega Ruby."),
    ("MEMORIES_NOT_IN_SWSH", "uint8_t", MEMORIES + "MemoryContext8Data.cs", "Memory_NotSWSH", "Memory ids Sword/Shield cannot record."),
    ("PURCHASABLE_ITEMS_SWSH", "uint16_t", MEMORIES + "MemoryContext8Data.cs", "PurchaseItemsNoTR", "Items a Sword/Shield shop sells, Technical Records aside (sorted)."),
    ("LOTO_ID_PRIZES_SWSH", "uint16_t", MEMORIES + "MemoryContext8Data.cs", "LotoPrizeSWSH", "Loto-ID prizes in Sword/Shield."),
    ("GENERAL_LOCATION_BY_MET_LOCATION_SWSH", "uint8_t", MEMORIES + "MemoryContext8Data.cs", "SingleGenLocAreas", "The one general location a Sword/Shield met location (index = met location / 2) records; 0 for none."),
    ("GENERAL_LOCATIONS_SWSH", "uint8_t", MEMORIES + "MemoryContext8Data.cs", "PossibleGeneralLocations8", "Every general location Sword/Shield can record (sorted)."),
    ("MEMORY_MINIMUM_INTENSITY_GENERATION_8", "uint8_t", MEMORIES + "MemoryContext8Data.cs", "MemoryMinIntensity", "Lowest intensity each memory id can have in Sword/Shield."),
    ("MEMORY_FEELINGS_GENERATION_8", "uint32_t", MEMORIES + "MemoryContext8Data.cs", "MemoryFeelings", "Per memory id, one bit per feeling (stored value minus one) it can have in Sword/Shield."),
    ("CAPTURE_FLAGS_SW", "uint8_t", MEMORIES + "MemoryContext8Data.cs", "CaptureFlagsSW", "One bit per species that can be caught in Sword."),
    ("CAPTURE_FLAGS_SH", "uint8_t", MEMORIES + "MemoryContext8Data.cs", "CaptureFlagsSH", "One bit per species that can be caught in Shield."),
    ("BERRIES_XY", "uint16_t", "Items/ItemStorage6XY.cs", "Berry", "Berries, which a Generation 6 memory can name as planted."),
]

# (emitted name, MemoryPermissions.cs method, comment)
SPECIES_LISTS = [
    ("FISHING_SPECIES_XY", "IsFishingSpeciesXY", "Species that can be fished in both X and Y."),
    ("FISHING_SPECIES_X", "IsFishingSpeciesX", "Species that can be fished in X only."),
    ("FISHING_SPECIES_Y", "IsFishingSpeciesY", "Species that can be fished in Y only."),
    ("FISHING_SPECIES_ORAS", "IsFishingSpeciesAO", "Species that can be fished in Omega Ruby/Alpha Sapphire."),
    ("FISHING_SPECIES_SWSH", "IsFishingSpeciesSWSH", "Species that can be fished in Sword/Shield."),
    ("DYNAMAX_TRAINER_SPECIES_SWSH", "IsDynamaxSWSH", "Species a trainer in both Sword and Shield Dynamaxes."),
    ("DYNAMAX_TRAINER_SPECIES_SW", "IsDynamaxSW", "Species a trainer only in Sword Dynamaxes."),
    ("DYNAMAX_TRAINER_SPECIES_SH", "IsDynamaxSH", "Species a trainer only in Shield Dynamaxes."),
]


def read(relpath):
    with open(pkhex_path(relpath), encoding="utf-8-sig") as handle:
        return handle.read()


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def span_values(relpath, span_name):
    text = strip_comments(read(relpath))
    match = re.search(r"ReadOnlySpan<\w+>\s+" + re.escape(span_name) + r"\s*=>\s*\[(.*?)\];", text, re.S)
    if not match:
        raise SystemExit(f"{relpath}: span {span_name} not found")
    return [int(token, 0) if token.lower().startswith("0x") else int(token) for token in re.findall(r"0x[0-9A-Fa-f]+|\d+", match.group(1))]


def species_ids():
    text = strip_comments(read("Game/Enums/Species.cs"))
    body = text[text.index("{") + 1:text.index("}")]
    names = [entry.split("=")[0].strip() for entry in body.split(",")]
    return {name: index for index, name in enumerate(names) if name}


def species_list(method, ids):
    text = strip_comments(read(MEMORIES + "MemoryPermissions.cs"))
    match = re.search(r"bool\s+" + re.escape(method) + r"\(ushort species\)\s*=>\s*species is(.*?);", text, re.S)
    if not match:
        raise SystemExit(f"MemoryPermissions.cs: {method} not found")
    return sorted(ids[name] for name in re.findall(r"\(int\)(\w+)", match.group(1)))


def multiple_general_locations():
    """MultiLocNNN spans: a met location whose memory may name any of several general locations."""
    text = strip_comments(read(MEMORIES + "MemoryContext8Data.cs"))
    rows = []
    for location, body in re.findall(r"ReadOnlySpan<byte>\s+MultiLoc(\d+)\s*=>\s*\[(.*?)\];", text, re.S):
        rows.append((int(location), [int(token) for token in re.findall(r"\d+", body)]))
    switch = re.search(r"GetMultiGenLocAreaList\(ushort location\)\s*=>\s*location switch\s*\{(.*?)\};", text, re.S).group(1)
    mapped = sorted((int(location), int(name)) for location, name in re.findall(r"(\d+)\s*=>\s*MultiLoc(\d+)", switch))
    if any(location != name for location, name in mapped) or sorted(location for location, _ in rows) != [location for location, _ in mapped]:
        raise SystemExit("MemoryContext8Data.cs: MultiLoc spans no longer match GetMultiGenLocAreaList")
    return sorted(rows)


def format_values(values, per_line, width):
    lines = []
    for start in range(0, len(values), per_line):
        chunk = values[start:start + per_line]
        lines.append("        " + ", ".join(f"0x{value:0{width}X}" if width else str(value) for value in chunk) + ",")
    return "\n".join(lines)


def main():
    out = []
    for name, element_type, relpath, span_name, comment in TABLES:
        values = span_values(relpath, span_name)
        width = 6 if element_type == "uint32_t" else (2 if name.startswith("CAPTURE_FLAGS") else 0)
        out.append(f"    /// {comment}")
        out.append(f"    inline constexpr {element_type} {name}[] = {{")
        out.append(format_values(values, 16 if width == 2 else 10, width))
        out.append("    };")
        out.append("")
    ids = species_ids()
    for name, method, comment in SPECIES_LISTS:
        values = species_list(method, ids)
        out.append(f"    /// {comment} (sorted)")
        out.append(f"    inline constexpr uint16_t {name}[] = {{")
        out.append(format_values(values, 10, 0))
        out.append("    };")
        out.append("")
    rows = multiple_general_locations()
    flat = []
    out.append("    /// A Sword/Shield met location whose memory may record any of several general locations: the")
    out.append("    /// locations are GENERAL_LOCATIONS_BY_MET_LOCATION_SWSH[firstIndex .. firstIndex + locationCount).")
    out.append("    struct MetLocationGeneralLocations")
    out.append("    {")
    out.append("        uint16_t metLocation;")
    out.append("        uint8_t firstIndex;")
    out.append("        uint8_t locationCount;")
    out.append("    };")
    out.append("")
    out.append("    inline constexpr MetLocationGeneralLocations MET_LOCATION_GENERAL_LOCATIONS_SWSH[] = {")
    for location, values in rows:
        out.append(f"        {{{location}, {len(flat)}, {len(values)}}},")
        flat.extend(values)
    out.append("    };")
    out.append("")
    out.append("    inline constexpr uint8_t GENERAL_LOCATIONS_BY_MET_LOCATION_SWSH[] = {")
    out.append(format_values(flat, 10, 0))
    out.append("    };")

    header = '''/**
 * Auto-generated by tools/gen_memories.py from PKHeX's memory rules (PKHeX.Core/Legality/Restrictions/
 * Memories and MemoryPermissions.cs). DO NOT EDIT BY HAND -- rerun the generator instead.
 *
 * The tables PKHeX's MemoryVerifier reads to decide whether a trainer memory, its variable, intensity
 * and feeling could have been recorded by Generation 6 (X/Y, Omega Ruby/Alpha Sapphire) or Sword/Shield.
 */
#ifndef LEGALITY_VERIFIERS_MEMORYTABLE_H
#define LEGALITY_VERIFIERS_MEMORYTABLE_H

#include <cstdint>

namespace Legality
{
    // clang-format off
'''
    footer = '''
    // clang-format on
}

#endif
'''
    with open(OUT_H, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(header + "\n".join(out) + footer)
    print(f"wrote {os.path.relpath(OUT_H, ROOT)}")


if __name__ == "__main__":
    main()
