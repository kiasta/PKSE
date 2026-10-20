#!/usr/bin/env python3
"""Generate include/Pokemon/DexTable8LA.h + src/Pokemon/DexTable8LA.cpp from PKHeX.

Legends: Arceus does not have a Pokedex in the shape every other game uses. Its block is a research
LOG, and it is split in two:

  * a per-SPECIES research entry (0x58 bytes each, 981 of them) holding the task counters, and
  * a per-SPECIES+FORM statistics entry (0x18 bytes each, 1480 of them) holding the seen/obtained
    flags and the size records.

The statistics entries are NOT indexed by species. They are reached through a lookup table keyed by
`species | (form << 11)` -- an arbitrary, hand-authored ordering with no derivable structure, which is
why it has to be copied rather than computed. PKHeX keeps it as two parallel arrays in
PokedexConstants8a (FormStorageIndexIds -> FormStorageIndexValues), sorted so the key can be found by
binary search; that is reproduced verbatim here.

PokemonInfoIds / PokemonInfoGenders come along too: the gender flag decides whether an entry tracks
male and female separately, which the "selected variant" logic needs.

The size research tasks ("catch a large / small / heavy / light one") come from researchtask_la.pkl:
one list of 0x28-byte task records per Hisui dex entry, reached from a species through personal_la's
DexIndexHisui, exactly as PKHeX's PokedexSave8a.TryGetResearchTasks does. Only the FIRST task of each
of the four types counts (PKHeX TryGetTriggeredTask), and only its threshold byte is kept.

Run by hand when the PKHeX-derived data changes; the build only compiles the committed output.
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pkhex_source import pkhex_path  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = "Saves/Substructures/Gen8/LA/Pokedex/PokedexConstants8a.cs"
RESEARCH_TASKS = "Resources/byte/zukan_research/researchtask_la.pkl"
PERSONAL = "Resources/byte/personal/personal_la"

# PersonalInfo8LA field offsets (PKHeX PersonalInfo8LA.cs).
PERSONAL_SIZE = 0xB0
PERSONAL_FORM_STATS_INDEX = 0x1E
PERSONAL_FORM_COUNT = 0x20
PERSONAL_DEX_INDEX_HISUI = 0x60

# PokedexResearchTask8a: 0x28 bytes, task type at 0x00, threshold at 0x03.
RESEARCH_TASK_SIZE = 0x28
# PokedexResearchTaskType8a, in the order the emitted SizeResearchTask8LA enum lists them.
SIZE_TASK_TYPES = (6, 7, 8, 9)  # CatchLarge, CatchSmall, CatchHeavy, CatchLight


def grab(text, name):
    """Extract the numeric literals of a `ReadOnlySpan<T> Name => [ ... ];` array."""
    start = text.index(name + " =>")
    body = text[text.index("[", start) + 1: text.index("];", start)]
    return [int(x, 0) for x in re.findall(r"0x[0-9A-Fa-f]+|\d+", body)]


def read_size_research(max_species):
    """species -> (task mask, [threshold per SIZE_TASK_TYPES]) for every species with a Hisui entry."""
    with open(pkhex_path(PERSONAL), "rb") as fh:
        personal = fh.read()
    with open(pkhex_path(RESEARCH_TASKS), "rb") as fh:
        linker = fh.read()
    assert linker[:2] == b"la", "researchtask_la.pkl magic"
    entry_count = struct.unpack_from("<H", linker, 2)[0]
    task_lists = []
    for entry_index in range(entry_count):
        start, end = struct.unpack_from("<II", linker, 4 + entry_index * 4)
        task_lists.append(linker[start:end])

    def personal_field(index, offset, fmt):
        return struct.unpack_from(fmt, personal, index * PERSONAL_SIZE + offset)[0]

    def form_index(species, form):
        # PKHeX PersonalInfo.FormIndex / HasForm.
        form_stats_index = personal_field(species, PERSONAL_FORM_STATS_INDEX, "<H")
        form_count = personal_field(species, PERSONAL_FORM_COUNT, "<B")
        if form == 0 or form_stats_index <= 0 or form >= form_count:
            return species
        return form_stats_index + form - 1

    result = {}
    for species in range(1, max_species):
        form_count = personal_field(species, PERSONAL_FORM_COUNT, "<B")
        dex_index = 0
        for form in range(form_count):
            dex_index = personal_field(form_index(species, form), PERSONAL_DEX_INDEX_HISUI, "<H")
            if dex_index:
                break
        if not dex_index:
            continue
        tasks = task_lists[dex_index - 1]
        assert len(tasks) % RESEARCH_TASK_SIZE == 0, f"species {species}: ragged task list"
        mask = 0
        thresholds = [0, 0, 0, 0]
        for task_offset in range(0, len(tasks), RESEARCH_TASK_SIZE):
            task_type = tasks[task_offset]
            if task_type not in SIZE_TASK_TYPES:
                continue
            slot = SIZE_TASK_TYPES.index(task_type)
            if mask & (1 << slot):
                continue  # PKHeX takes the first task of a type
            mask |= 1 << slot
            thresholds[slot] = tasks[task_offset + 3]
        if mask:
            result[species] = (mask, thresholds)
    return result


def main():
    with open(pkhex_path(SRC), encoding="utf-8-sig") as fh:
        text = fh.read()

    ids = grab(text, "FormStorageIndexIds")
    vals = grab(text, "FormStorageIndexValues")
    info_ids = grab(text, "PokemonInfoIds")
    info_gender = grab(text, "PokemonInfoGenders")

    print(f"FormStorageIndexIds   {len(ids)}")
    print(f"FormStorageIndexValues {len(vals)}")
    print(f"PokemonInfoIds        {len(info_ids)}")
    print(f"PokemonInfoGenders    {len(info_gender)}")

    assert len(ids) == len(vals), "the two FormStorageIndex arrays must be parallel"
    assert len(info_ids) == len(info_gender), "the two PokemonInfo arrays must be parallel"
    assert ids == sorted(ids), "ids must be sorted -- the lookup binary-searches them"
    assert info_ids == sorted(info_ids), "info ids must be sorted"
    assert len(set(ids)) == len(ids), "duplicate key in FormStorageIndexIds"
    assert max(vals) < 1480, f"statistics index {max(vals)} is outside the 1480-entry region"

    # A key is species | form<<11, so decoding it back must give a sane species/form pair.
    for k in ids:
        assert (k & 0x7FF) <= 981 and (k >> 11) < 120, f"key {k} decodes out of range"
    print(f"highest statistics index used: {max(vals)}  (region holds 1480)")
    print(f"keys with a non-zero form: {sum(1 for k in ids if k >> 11)}")

    size_research = read_size_research(981)
    size_species = sorted(size_research)
    print(f"species with a size research task: {len(size_species)}")
    for slot, label in enumerate(("large", "small", "heavy", "light")):
        print(f"  {label:<6} tasks: {sum(1 for s in size_species if size_research[s][0] & (1 << slot))}")

    def rows(vs, per, width, hexfmt=False):
        out = ""
        for n in range(0, len(vs), per):
            out += "        " + " ".join(
                (f"0x{v:02X}," if hexfmt else f"{v:>{width}},") for v in vs[n:n + per]) + "\n"
        return out

    hdr = """/**
 * GENERATED by tools/gen_dextable8la.py.
 *
 * PLA's dex block is a research LOG, split into a per-SPECIES research entry (0x58 bytes) and a
 * per-SPECIES+FORM statistics entry (0x18 bytes) that holds the seen/obtained flags. The statistics
 * entries are not indexed by species: they are reached through a lookup keyed by
 * `species | (form << 11)`, an arbitrary ordering with no derivable structure. Copied verbatim from
 * PKHeX PokedexConstants8a; do not hand-edit, rerun the generator.
 */
#ifndef POKEMON_DEXTABLE8LA_H
#define POKEMON_DEXTABLE8LA_H

#include <cstdint>

namespace Pokemon {

    /// Returned when the species+form has no statistics entry (it cannot appear in PLA's dex).
    inline constexpr uint16_t DEX8LA_NO_STAT_ENTRY = 0xFFFF;

    /// Number of statistics entries the block allocates.
    inline constexpr uint16_t DEX8LA_STATISTICS_ENTRIES = 1480;

    /// Highest species with a research entry (PKHeX PokedexSave8a.MAX_SPECIES).
    inline constexpr uint16_t DEX8LA_MAX_SPECIES = %d;

    /// Highest form the dex tables address (PKHeX PokedexSave8a.MAX_FORM).
    inline constexpr uint8_t  DEX8LA_MAX_FORM = %d;

    /// Statistics-entry index for a (species, form), or DEX8LA_NO_STAT_ENTRY when it has none.
    uint16_t getStatisticsIndex8LA(uint16_t species, uint8_t form) noexcept;

    /// True when the dex tracks this species+form's genders SEPARATELY (PokemonInfoGenders bit 3).
    /// Where false, the male and female slots are one shared record.
    bool tracksGenderSeparately8LA(uint16_t species, uint8_t form) noexcept;

    /// The four size research tasks. Large and small compare HeightAbsolute with the threshold;
    /// heavy and light compare WeightAbsolute with the threshold in tenths.
    enum class SizeResearchTask8LA : uint8_t
    {
        CatchLarge = 0,
        CatchSmall,
        CatchHeavy,
        CatchLight,
    };

    /// True when the species has `task` among its research tasks, with its threshold in
    /// `taskThreshold`. False, leaving `taskThreshold` alone, when it has none.
    bool getSizeResearchThreshold8LA(uint16_t speciesId, SizeResearchTask8LA task, uint8_t &taskThreshold) noexcept;
}

#endif
""" % (981, 120)

    cpp = """/**
 * GENERATED by tools/gen_dextable8la.py from PKHeX PokedexConstants8a. Do not hand-edit.
 */
#include "Pokemon/DexTable8LA.h"

namespace Pokemon {

    namespace {
        // Sorted keys (species | form<<11) and the statistics index each maps to.
        constexpr uint16_t STORAGE_KEYS[] = {
%s        };
        constexpr uint16_t STORAGE_VALUES[] = {
%s        };
        constexpr int STORAGE_COUNT = static_cast<int>(sizeof(STORAGE_KEYS) / sizeof(STORAGE_KEYS[0]));

        // Sorted keys again, with the per-entry gender byte. Bit 3 = the two genders are tracked apart.
        constexpr uint16_t INFO_KEYS[] = {
%s        };
        constexpr uint8_t INFO_GENDERS[] = {
%s        };
        constexpr int INFO_COUNT = static_cast<int>(sizeof(INFO_KEYS) / sizeof(INFO_KEYS[0]));

        // Species with a size research task, sorted; a bit per SizeResearchTask8LA it has, and the
        // threshold of each (0 where the bit is clear).
        constexpr uint16_t SIZE_RESEARCH_SPECIES[] = {
%s        };
        constexpr uint8_t SIZE_RESEARCH_TASK_MASKS[] = {
%s        };
        constexpr uint8_t SIZE_RESEARCH_THRESHOLDS[][4] = {
%s        };
        constexpr int SIZE_RESEARCH_COUNT = static_cast<int>(sizeof(SIZE_RESEARCH_SPECIES) / sizeof(SIZE_RESEARCH_SPECIES[0]));

        // Same binary search PKHeX uses; -1 when absent.
        int findKey(const uint16_t* keys, int count, uint16_t key) noexcept {
            int lo = 0, hi = count - 1;
            while (lo <= hi) {
                const int mid = lo + ((hi - lo) >> 1);
                if      (keys[mid] == key) return mid;
                else if (keys[mid] <  key) lo = mid + 1;
                else                       hi = mid - 1;
            }
            return -1;
        }

        uint16_t packKey(uint16_t species, uint8_t form) noexcept {
            return static_cast<uint16_t>(species | (static_cast<uint16_t>(form) << 11));
        }
    }

    uint16_t getStatisticsIndex8LA(uint16_t species, uint8_t form) noexcept {
        if (species == 0 || species > DEX8LA_MAX_SPECIES || form >= DEX8LA_MAX_FORM) return DEX8LA_NO_STAT_ENTRY;
        const int i = findKey(STORAGE_KEYS, STORAGE_COUNT, packKey(species, form));
        return (i < 0) ? DEX8LA_NO_STAT_ENTRY : STORAGE_VALUES[i];
    }

    bool tracksGenderSeparately8LA(uint16_t species, uint8_t form) noexcept {
        if (species == 0 || species > DEX8LA_MAX_SPECIES || form >= DEX8LA_MAX_FORM) return false;
        const int i = findKey(INFO_KEYS, INFO_COUNT, packKey(species, form));
        return (i >= 0) && (INFO_GENDERS[i] & 0x08) != 0;
    }

    bool getSizeResearchThreshold8LA(uint16_t speciesId, SizeResearchTask8LA task, uint8_t &taskThreshold) noexcept
    {
        const int speciesIndex = findKey(SIZE_RESEARCH_SPECIES, SIZE_RESEARCH_COUNT, speciesId);
        const unsigned taskSlot = static_cast<unsigned>(task);
        if (speciesIndex < 0 || taskSlot >= 4 || (SIZE_RESEARCH_TASK_MASKS[speciesIndex] & (1u << taskSlot)) == 0) return false;
        taskThreshold = SIZE_RESEARCH_THRESHOLDS[speciesIndex][taskSlot];
        return true;
    }
}
""" % (rows(ids, 12, 6), rows(vals, 12, 5), rows(info_ids, 12, 6),
       rows(info_gender, 12, 4, hexfmt=True),
       rows(size_species, 12, 4), rows([size_research[s][0] for s in size_species], 12, 4, hexfmt=True),
       "".join("        { " + ", ".join(f"{v:>3}" for v in size_research[s][1]) + " },\n" for s in size_species))

    hp = os.path.join(ROOT, "include", "Pokemon", "DexTable8LA.h")
    cp = os.path.join(ROOT, "src", "Pokemon", "DexTable8LA.cpp")
    with open(hp, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(hdr)
    with open(cp, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(cpp)
    print(f"\nwrote {hp}\nwrote {cp}")

    # Spot checks against the raw arrays, so a mangled emit is caught here rather than on hardware.
    lookup = dict(zip(ids, vals))
    for sp, fm, label in ((25, 0, "Pikachu"), (493, 0, "Arceus"), (899, 0, "Wyrdeer"),
                          (58, 1, "Growlithe-Hisui"), (59, 1, "Arcanine-Hisui"), (100, 1, "Voltorb-Hisui")):
        k = sp | (fm << 11)
        print(f"  spot check: {label:<18} key={k:<6} -> "
              f"{lookup.get(k, 'no statistics entry')}")
    for sp, label in ((399, "Bidoof"), (25, "Pikachu"), (493, "Arceus"), (900, "Kleavor"), (58, "Growlithe")):
        print(f"  size research: {label:<10} {size_research.get(sp, 'none')}")


if __name__ == "__main__":
    main()
