#!/usr/bin/env python3
"""Generate include/Legality/Evolutions/EvolutionTreeTable.h + src/Legality/Evolutions/EvolutionTreeTable.cpp
-- the data behind PKSE's port of PKHeX's evolution engine (PKHeX.Core/Legality/Evolutions).

One tree per PKHeX EntityContext, exactly as PKHeX's EvolutionTree builds them:

  context  evolution binary  tree's personal table  history/discard table
  Gen1     evos_g1           Y        (by species)  RB
  Gen2     evos_g2           C        (by species)  C
  Gen3     evos_g3           RS       (by species)  E
  Gen4     evos_g4           DP       (by species)  HGSS
  Gen5     evos_g5           BW       (by species)  B2W2
  Gen6     evos_g6           AO       (by species)  AO
  Gen7     evos_uu           USUM     (form index)  USUM
  Gen7b    evos_gg           GG       (form index)  GG
  Gen8     evos_ss           SWSH     (form index)  SWSH
  Gen8a    evos_la           LA       (form index)  LA      level-up evolutions trigger on demand
  Gen8b    evos_bs           BDSP     (form index)  BDSP
  Gen9     evos_sv           SV       (form index)  SV
  Gen9a    evos_za           ZA       (form index)  ZA      level-up evolutions trigger on demand

Per tree it emits:
  * every EvolutionMethod (PKHeX EvolutionSet.GetMethod; LevelUp = 1, or 0 for Legends: Arceus and
    Legends: Z-A, when the type requires a level up),
  * the forward entries GetForward reads (species-indexed trees by species; personal-indexed trees
    by (species, form), with an empty record wherever PKHeX's FormIndex lands on an empty entry
    while the species' form 0 has methods, so a lookup can fall back to form 0 exactly when
    PKHeX's HasForm does),
  * the reverse lineage EvolutionReverseLookup registers, in PKHeX's registration order (first
    link, second link),
  * a per-species form-presence mask: bit f = the history table's IsPresentInGame(species, f).

Regenerate:  python tools/gen_evolutiontree.py   (PKHEX_LOCAL=~/repos/PKHeX to read a checkout)
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_learnsets import BYTE, Personal, Personal3, _load, bin_entries  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_H = os.path.join(ROOT, "include", "Legality", "Evolutions", "EvolutionTreeTable.h")
OUT_CPP = os.path.join(ROOT, "src", "Legality", "Evolutions", "EvolutionTreeTable.cpp")

ANY_FORM = 0xFF
METHOD_SIZE = 8
PRESENCE_FORM_BITS = 32

# PKHeX EvolutionType.IsLevelUpRequired == false. Every other known type requires a level up.
NOT_LEVEL_UP_TYPES = {0, 5, 6, 7, 8, 17, 18, 35, 42, 43, 44, 45, 48, 49, 61, 62, 90, 91, 92}
KNOWN_TYPES = set(range(0, 63)) | {90, 91, 92}

UNOWN = 201


class PersonalGen1:
    """PersonalTable1 (RB or Y): no forms; present when form 0 and species <= 151."""
    maxsp = 151

    def form_count(self, sp):
        return 1

    def present(self, sp, form):
        return form == 0 and sp <= self.maxsp


class PersonalGen2:
    """PersonalTable2 (C): no form entries; Unown's 26 letters count as present."""
    maxsp = 251

    def form_count(self, sp):
        return 1

    def present(self, sp, form):
        if sp > self.maxsp:
            return False
        if form == 0:
            return True
        return sp == UNOWN and form < 26


class PersonalGen3:
    """PersonalTable3 (RS / E): FormCount is the base class default of 1."""
    maxsp = 386

    def __init__(self):
        self.reader = Personal3("personal_rs")

    def form_count(self, sp):
        return 1

    def present(self, sp, form):
        if sp == 0:
            return form == 0
        return self.reader.present(sp, form)


# (C++ name suffix, PKHeX context, resource, identifier, species-indexed, level-up amount,
#  tree personal key, history personal key)
TREES = [
    ("RBY", "Gen1", "g1", b"g1", True, 1, "gen1", "gen1"),
    ("GSC", "Gen2", "g2", b"g2", True, 1, "gen2", "gen2"),
    ("RSE", "Gen3", "g3", b"g3", True, 1, "gen3", "gen3"),
    ("DP", "Gen4", "g4", b"g4", True, 1, "DP", "HGSS"),
    ("BW", "Gen5", "g5", b"g5", True, 1, "BW", "B2W2"),
    ("XY", "Gen6", "g6", b"g6", True, 1, "ORAS", "ORAS"),
    ("SM", "Gen7", "uu", b"uu", False, 1, "USUM", "USUM"),
    ("GG", "Gen7b", "gg", b"gg", False, 1, "GG", "GG"),
    ("SWSH", "Gen8", "ss", b"ss", False, 1, "SWSH", "SWSH"),
    ("PLA", "Gen8a", "la", b"la", False, 0, "PLA", "PLA"),
    ("BDSP", "Gen8b", "bs", b"bs", False, 1, "BDSP", "BDSP"),
    ("SV", "Gen9", "sv", b"sv", False, 1, "SV", "SV"),
    ("ZA", "Gen9a", "za", b"za", False, 0, "ZA", "ZA"),
]


def load_personal(key, cache):
    if key not in cache:
        if key == "gen1":
            cache[key] = PersonalGen1()
        elif key == "gen2":
            cache[key] = PersonalGen2()
        elif key == "gen3":
            cache[key] = PersonalGen3()
        else:
            cache[key] = Personal(key)
    return cache[key]


def read_methods(entry, level_up_amount):
    methods = []
    for offset in range(0, len(entry) - (METHOD_SIZE - 1), METHOD_SIZE):
        evolution_type = entry[offset]
        if evolution_type not in KNOWN_TYPES:
            raise SystemExit("unknown evolution type %d" % evolution_type)
        argument = struct.unpack_from("<H", entry, offset + 2)[0]
        species = struct.unpack_from("<H", entry, offset + 4)[0]
        form = entry[offset + 6]
        level = entry[offset + 7]
        level_up = 0 if evolution_type in NOT_LEVEL_UP_TYPES else level_up_amount
        methods.append((species, argument, form, evolution_type, level, level_up))
    return methods


def build_tree(spec, cache):
    suffix, context, resource, identifier, species_indexed, level_up_amount, tree_key, history_key = spec
    data = _load(BYTE("evolve", "evos_%s.pkl" % resource))
    if data[0:2] != identifier:
        raise SystemExit("%s: identifier %r, expected %r" % (resource, data[0:2], identifier))
    entries = [read_methods(entry, level_up_amount) for entry in bin_entries(data)]
    tree_personal = load_personal(tree_key, cache)
    history_personal = load_personal(history_key, cache)
    max_species = tree_personal.maxsp

    methods = []
    method_start = {}

    def method_index(entry_index, position):
        if entry_index not in method_start:
            method_start[entry_index] = len(methods)
            methods.extend(entries[entry_index])
        return method_start[entry_index] + position

    def form_index(species, form):
        # PKHeX IPersonalTable.GetFormIndex for the personal-indexed trees.
        return tree_personal.form_index(species, form)

    forward = []
    if species_indexed:
        for species, entry in enumerate(entries):
            if entry:
                forward.append((species, 0, method_index(species, 0), len(entry)))
    else:
        if entries[0]:
            raise SystemExit("%s: entry 0 is not empty" % resource)
        for species in range(1, max_species + 1):
            base_index = form_index(species, 0)
            base_methods = entries[base_index] if base_index < len(entries) else []
            for form in range(tree_personal.form_count(species)):
                if form != 0 and not tree_personal.has_form(species, form):
                    continue
                index = form_index(species, form)
                entry = entries[index] if index < len(entries) else []
                if entry:
                    forward.append((species, form, method_index(index, 0), len(entry)))
                elif form != 0 and base_methods:
                    forward.append((species, form, 0, 0))

    # PKHeX EvolutionReverseSpecies / EvolutionReversePersonal.GetLineage.
    lineage = {}
    for source_species in range(1, max_species + 1):
        for source_form in range(tree_personal.form_count(source_species)):
            index = source_species if species_indexed else form_index(source_species, source_form)
            if index >= len(entries):
                continue
            for position, method in enumerate(entries[index]):
                destination_species = method[0]
                if destination_species == 0:
                    break
                destination_form = source_form if method[2] == ANY_FORM else method[2]
                links = lineage.setdefault((destination_species, destination_form), [])
                if len(links) == 2:
                    raise SystemExit("%s: more than two links into %r" % (resource, (destination_species, destination_form)))
                links.append((method_index(index, position), source_species, source_form))

    presence = []
    for species in range(0, history_personal.maxsp + 1):
        mask = 0
        for form in range(0, 64):
            if history_personal.present(species, form):
                if form >= PRESENCE_FORM_BITS:
                    raise SystemExit("%s: species %d form %d present beyond the mask" % (history_key, species, form))
                mask |= 1 << form
        presence.append(mask)

    forward.sort()
    reverse = sorted(lineage.items())
    if len(methods) > 0xFFFF:
        raise SystemExit("%s: too many methods" % resource)
    return {
        "suffix": suffix, "context": context, "species_indexed": species_indexed,
        "max_species": max_species, "methods": methods, "forward": forward, "reverse": reverse,
        "presence": presence,
    }


def emit_header():
    return """/**
 * GENERATED by tools/gen_evolutiontree.py from PKHeX's evos_*.pkl and personal tables -- do not
 * hand-edit; rerun the generator.
 *
 * The data behind Legality::Evolutions::EvolutionTree, one table per PKHeX EntityContext. See the
 * generator's docstring for which personal table each context reads.
 */
#ifndef LEGALITY_EVOLUTIONS_EVOLUTIONTREETABLE_H
#define LEGALITY_EVOLUTIONS_EVOLUTIONTREETABLE_H

#include <cstdint>
#include <span>

#include "Legality/Evolutions/EvolutionTree.h"

namespace Legality
{
    namespace Evolutions
    {
        /// The methods of one forward entry: methods[methodIndex .. methodIndex + methodCount).
        struct EvolutionForwardEntry
        {
            uint16_t speciesId;
            uint8_t formId;
            uint8_t methodCount;
            uint16_t methodIndex;
        };

        /// PKHeX EvolutionNode for one destination species-form; a source species of 0 is no link.
        struct EvolutionReverseEntry
        {
            uint16_t speciesId;
            uint8_t formId;
            uint8_t firstSourceFormId;
            uint16_t firstSourceSpeciesId;
            uint16_t firstMethodIndex;
            uint16_t secondSourceSpeciesId;
            uint16_t secondMethodIndex;
            uint8_t secondSourceFormId;
        };

        struct EvolutionTreeData
        {
            /// Gen 1-6: the binary is read by species and every form shares the entry.
            bool isSpeciesIndexed;
            /// The tree personal table's MaxSpeciesID.
            uint16_t maxSpeciesId;
            std::span<const EvolutionMethod> methods;
            /// Sorted by (speciesId, formId).
            std::span<const EvolutionForwardEntry> forwardEntries;
            /// Sorted by (speciesId, formId).
            std::span<const EvolutionReverseEntry> reverseEntries;
            /// Per species, bit f set when the history table has form f.
            std::span<const uint32_t> presenceMasks;
        };

%s    }
}

#endif
""" % "".join("        extern const EvolutionTreeData EVOLUTION_TREE_%s;\n" % spec[0] for spec in TREES)


def emit_tree(tree):
    suffix = tree["suffix"]
    lines = []
    lines.append("        // PKHeX EntityContext.%s" % tree["context"])
    lines.append("        constexpr EvolutionMethod METHODS_%s[] = {" % suffix)
    for species, argument, form, evolution_type, level, level_up in tree["methods"]:
        lines.append("            {%d, %d, %d, static_cast<EvolutionType>(%d), %d, %d}," % (species, argument, form, evolution_type, level, level_up))
    lines.append("        };")
    lines.append("        constexpr EvolutionForwardEntry FORWARD_%s[] = {" % suffix)
    for species, form, index, count in tree["forward"]:
        lines.append("            {%d, %d, %d, %d}," % (species, form, count, index))
    lines.append("        };")
    lines.append("        constexpr EvolutionReverseEntry REVERSE_%s[] = {" % suffix)
    for (species, form), links in tree["reverse"]:
        first = links[0]
        second = links[1] if len(links) > 1 else (0, 0, 0)
        lines.append("            {%d, %d, %d, %d, %d, %d, %d, %d}," % (species, form, first[2], first[1], first[0], second[1], second[0], second[2]))
    lines.append("        };")
    lines.append("        // clang-format off")
    lines.append("        constexpr uint32_t PRESENCE_%s[] = {" % suffix)
    presence = tree["presence"]
    for start in range(0, len(presence), 12):
        lines.append("            " + " ".join("0x%X," % mask for mask in presence[start:start + 12]))
    lines.append("        };")
    lines.append("        // clang-format on")
    lines.append("")
    return "\n".join(lines)


def emit_cpp(trees):
    out = []
    out.append("// GENERATED by tools/gen_evolutiontree.py from PKHeX's evos_*.pkl and personal tables -- do not hand-edit.")
    out.append('#include "Legality/Evolutions/EvolutionTreeTable.h"')
    out.append("")
    out.append("namespace Legality")
    out.append("{")
    out.append("    namespace Evolutions")
    out.append("    {")
    out.append("        namespace")
    out.append("        {")
    for tree in trees:
        out.append("    " + emit_tree(tree).replace("\n", "\n    "))
    out.append("        }")
    out.append("")
    for tree in trees:
        suffix = tree["suffix"]
        out.append("        const EvolutionTreeData EVOLUTION_TREE_%s = {%s, %d, METHODS_%s, FORWARD_%s, REVERSE_%s, PRESENCE_%s};" % (suffix, "true" if tree["species_indexed"] else "false", tree["max_species"], suffix, suffix, suffix, suffix))
    out.append("    }")
    out.append("}")
    out.append("")
    return "\n".join(out)


def main():
    cache = {}
    trees = [build_tree(spec, cache) for spec in TREES]
    for tree in trees:
        if not tree["forward"] or not tree["reverse"]:
            raise SystemExit("%s: empty tree" % tree["suffix"])
        print("  %-5s methods %4d  forward %4d  reverse %4d  presence %4d" % (tree["suffix"], len(tree["methods"]), len(tree["forward"]), len(tree["reverse"]), len(tree["presence"])))
    with open(OUT_H, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(emit_header())
    print("Wrote", OUT_H)
    with open(OUT_CPP, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(emit_cpp(trees))
    print("Wrote", OUT_CPP)


if __name__ == "__main__":
    main()
