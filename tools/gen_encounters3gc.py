#!/usr/bin/env python3
"""Generate include/Legality/Encounters/EncounterData3GC.h + src/Legality/Encounters/EncounterData3GC.cpp:
PKHeX's Colosseum and XD encounter data, one array per PKHeX array, in PKHeX's order.

Sources (all under PKHeX.Core/Legality/Encounters/Data/Gen3):
  Encounters3ColoShadow.cs / Encounters3XDShadow.cs -- the locked teams (TeamLock / NPCLock)
  Encounters3ColoTeams.cs  / Encounters3XDTeams.cs  -- the permutations a shadow's PartyPrior names
  Encounters3Colo.cs       / Encounters3XD.cs       -- starters, gifts, shadows, e-Reader, trades, Poke Spots

The team-lock detail (PKHeX NPCLockDetail) is read from Legality/RNG/CXD/TeamLock.cs.

    PKHEX_LOCAL=~/repos/PKHeX python tools/gen_encounters3gc.py
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_encounters import read_cs, strip_comments, parse_array, split_top, load_enum  # noqa: E402
from gen_encounters2 import cpp_text, number, moveset  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_H = os.path.join(ROOT, "include", "Legality", "Encounters", "EncounterData3GC.h")
OUT_CPP = os.path.join(ROOT, "src", "Legality", "Encounters", "EncounterData3GC.cpp")
DATA = "Legality/Encounters/Data/Gen3/"
LOCK_DETAIL = load_enum("Legality/RNG/CXD/TeamLock.cs", "NPCLockDetail")


def constant_name(identifier):
    """PKHeX's PascalCase identifier as an UPPER_SNAKE C++ name (XSphealCipherLab -> X_SPHEAL_CIPHER_LAB)."""
    spaced = re.sub(r"(?<=[a-z0-9])(?=[A-Z])|(?<=[A-Z])(?=[A-Z][a-z])", "_", identifier)
    return spaced.upper()


def read_team_locks(file_name):
    """{identifier: (species, detail, [lock C++ expression])} in declaration order."""
    text = strip_comments(read_cs(DATA + file_name))
    teams = {}
    for match in re.finditer(r"TeamLock\s+(\w+)\s*=\s*new\s*\(", text):
        start = match.end() - 1
        depth, index = 0, start
        while True:
            if text[index] == "(":
                depth += 1
            elif text[index] == ")":
                depth -= 1
                if depth == 0:
                    break
            index += 1
        arguments = split_top(text[start + 1:index])
        species = number(arguments[0])
        detail = 0
        if len(arguments) == 3:
            detail_name = arguments[1].strip().split(".")[-1]
            if detail_name not in LOCK_DETAIL:
                raise SystemExit("unknown NPCLockDetail %r" % detail_name)
            detail = LOCK_DETAIL[detail_name]
        locks = []
        for lock in re.finditer(r"new\s+NPCLock\s*\(([^)]*)\)", arguments[-1]):
            parts = [part.strip() for part in lock.group(1).split(",")]
            if len(parts) == 4:
                locks.append("RNG::NPCLock::regular(%d, %d, %d, %d)" % tuple(number(part) for part in parts))
            elif len(parts) == 1:
                locks.append("RNG::NPCLock::shadow(%d)" % number(parts[0]))
            elif len(parts) == 2 and parts[1] in ("true", "false"):
                locks.append("RNG::NPCLock::shadow(%d, %s)" % (number(parts[0]), parts[1]))
            else:
                raise SystemExit("cannot parse NPCLock(%s)" % lock.group(1))
        if not locks:
            raise SystemExit("TeamLock %s has no locks" % match.group(1))
        teams[match.group(1)] = (species, detail, locks)
    return teams


def read_team_arrays(file_name):
    """{identifier: [TeamLock identifier]} for the `TeamLock[] Name = [a, b];` permutations."""
    text = strip_comments(read_cs(DATA + file_name))
    arrays = {}
    for match in re.finditer(r"TeamLock\[\]\s+(\w+)\s*=\s*\[([^\]]*)\]", text):
        arrays[match.group(1)] = [part.strip() for part in match.group(2).split(",") if part.strip()]
    return arrays


def emit_team_locks(teams):
    lines = []
    for identifier, (species, detail, locks) in teams.items():
        name = constant_name(identifier)
        lines.append("            constexpr RNG::NPCLock LOCKS_%s[] = {%s};" % (name, ", ".join(locks)))
        lines.append("            constexpr RNG::TeamLock %s = {%d, %d, LOCKS_%s};" % (name, species, detail, name))
    return "\n".join(lines) + "\n"


def emit_team_arrays(prefix, arrays, used):
    lines = []
    for identifier, members in arrays.items():
        if not members:
            continue
        used.add(identifier)
        lines.append("            constexpr RNG::TeamLock TEAMS_%s_%s[] = {%s};" % (prefix, constant_name(identifier), ", ".join(constant_name(member) for member in members)))
    return "\n".join(lines) + "\n"


def team_span(prefix, identifier, arrays):
    if not arrays[identifier]:
        return "std::span<const RNG::TeamLock>{}"
    return "std::span<const RNG::TeamLock>(TEAMS_%s_%s)" % (prefix, constant_name(identifier))


def read_name_arrays(text):
    """{identifier: [8 names]} for `string[] Name = [..];`, following `=> Other.Name` aliases."""
    names = {}
    for match in re.finditer(r"string\[\]\s+(\w+)\s*=\s*\[([^\]]*)\]", text):
        entries = []
        for part in split_top(match.group(2)):
            part = part.strip()
            entries.append("" if part == "string.Empty" else part.strip('"'))
        if len(entries) != 8:
            raise SystemExit("%s has %d names, not 8" % (match.group(1), len(entries)))
        names[match.group(1)] = entries
    for match in re.finditer(r"string\[\]\s+(\w+)\s*=>\s*\w+\.(\w+)\s*;", text):
        names[match.group(1)] = match.group(2)
    return names


def emit_names(name_arrays):
    lines = []
    for identifier, entries in name_arrays.items():
        if isinstance(entries, str):
            continue
        lines.append("            constexpr LanguageNames NAMES_%s = {%s};" % (constant_name(identifier), ", ".join(cpp_text(entry) for entry in entries)))
    return "\n".join(lines) + "\n"


def names_reference(identifier, name_arrays):
    target = name_arrays[identifier]
    if isinstance(target, str):
        identifier = target
    return "&NAMES_%s" % constant_name(identifier)


def shadow_line(class_name, args, props, prefix, arrays):
    fields = [
        ".shadowIndex = %d" % number(args[0]),
        ".heartGauge = %d" % number(args[1]),
        ".partyPrior = %s" % team_span(prefix, args[2].strip(), arrays),
        ".speciesId = %d" % number(props["Species"]),
        ".level = %d" % number(props["Level"]),
        ".location = %d" % number(props["Location"]),
        ".moves = %s" % moveset(props["Moves"]),
    ]
    if "FixedBall" in props:
        fields.append(".fixedBall = Enums::Ball::%s" % props["FixedBall"].split(".")[-1])
    return "                %s({%s})," % (class_name, ", ".join(fields))


def main():
    colo_teams = read_team_locks("Encounters3ColoShadow.cs")
    xd_teams = read_team_locks("Encounters3XDShadow.cs")
    colo_arrays = read_team_arrays("Encounters3ColoTeams.cs")
    xd_arrays = read_team_arrays("Encounters3XDTeams.cs")
    colo_text = strip_comments(read_cs(DATA + "Encounters3Colo.cs"))
    xd_text = strip_comments(read_cs(DATA + "Encounters3XD.cs"))
    colo_names = read_name_arrays(colo_text)
    xd_names = read_name_arrays(xd_text)
    # XD's Duking is an alias (`=> Encounters3Colo.TrainerNameDuking`), so one map resolves both games.
    all_names = dict(colo_names)
    all_names.update(xd_names)

    blocks, accessors = [], []

    def accessor(function, element, array):
        accessors.append((function, element, array))

    blocks.append(emit_team_locks(colo_teams))
    blocks.append(emit_team_locks(xd_teams))
    used = set()
    blocks.append(emit_team_arrays("COLO", colo_arrays, used))
    blocks.append(emit_team_arrays("XD", xd_arrays, used))
    blocks.append(emit_names(colo_names))
    blocks.append(emit_names(xd_names))

    # Colosseum
    lines = ["                EncounterStarter3Colo(%d, %d, %s)," % (number(args[0]), number(args[1]), moveset(props["Moves"])) for args, props in parse_array(colo_text, "Starters")]
    blocks.append("            const EncounterStarter3Colo STARTERS_COLO[] = {\n%s\n            };\n" % "\n".join(lines))
    accessor("getStarters3Colo", "EncounterStarter3Colo", "STARTERS_COLO")

    lines = []
    for args, props in parse_array(colo_text, "Gifts"):
        if args[3].strip() != "GameVersion.CXD":
            raise SystemExit("unexpected Colosseum gift version %s" % args[3])
        lines.append("                EncounterGift3Colo({.speciesId = %d, .level = %d, .trainerNames = %s, .version = GAME_VERSION_COLOSSEUM_XD, .location = %d, .TID16 = %d, .trainerGender = %d, .moves = %s}),"
                     % (number(args[0]), number(args[1]), names_reference(args[2].strip(), colo_names), number(props["Location"]), number(props["TID16"]), number(props["OriginalTrainerGender"]), moveset(props["Moves"])))
    blocks.append("            const EncounterGift3Colo GIFTS_COLO[] = {\n%s\n            };\n" % "\n".join(lines))
    accessor("getGifts3Colo", "EncounterGift3Colo", "GIFTS_COLO")

    for array_name, name, function in (("Shadow", "SHADOW_COLO", "getShadow3Colo"), ("EReader", "SHADOW_COLO_E_READER", "getShadow3ColoEReader")):
        lines = [shadow_line("EncounterShadow3Colo", args, props, "COLO", colo_arrays) for args, props in parse_array(colo_text, array_name)]
        blocks.append("            const EncounterShadow3Colo %s[] = {\n%s\n            };\n" % (name, "\n".join(lines)))
        accessor(function, "EncounterShadow3Colo", name)

    # XD
    lines = ["                EncounterStatic3XD({.speciesId = %d, .level = %d, .location = %d, .moves = %s})," % (number(args[0]), number(args[1]), number(props["Location"]), moveset(props["Moves"])) for args, props in parse_array(xd_text, "Gifts")]
    blocks.append("            const EncounterStatic3XD GIFTS_XD[] = {\n%s\n            };\n" % "\n".join(lines))
    accessor("getGifts3XD", "EncounterStatic3XD", "GIFTS_XD")

    lines = []
    for args, props in parse_array(xd_text, "Trades"):
        nicknames = names_reference(args[3].strip(), all_names) if len(args) > 3 else "nullptr"
        trainer_reference = names_reference(args[2].strip(), all_names)
        lines.append("                EncounterTrade3XD({.speciesId = %d, .level = %d, .trainerNames = %s, .nicknames = %s, .location = %d, .TID16 = %d, .moves = %s}),"
                     % (number(args[0]), number(args[1]), trainer_reference, nicknames, number(props["Location"]), number(props["TID16"]), moveset(props["Moves"])))
    blocks.append("            const EncounterTrade3XD TRADES_XD[] = {\n%s\n            };\n" % "\n".join(lines))
    accessor("getTrades3XD", "EncounterTrade3XD", "TRADES_XD")

    lines = [shadow_line("EncounterShadow3XD", args, props, "XD", xd_arrays) for args, props in parse_array(xd_text, "Shadow")]
    blocks.append("            const EncounterShadow3XD SHADOW_XD[] = {\n%s\n            };\n" % "\n".join(lines))
    accessor("getShadow3XD", "EncounterShadow3XD", "SHADOW_XD")

    lines = ["                EncounterArea3XD(%s)," % ", ".join(str(number(part)) for part in args) for args, _ in parse_array(xd_text, "Slots")]
    blocks.append("            const EncounterArea3XD AREAS_XD[] = {\n%s\n            };\n" % "\n".join(lines))
    accessor("getAreas3XD", "EncounterArea3XD", "AREAS_XD")

    declarations = "\n".join("        std::span<const %s> %s() noexcept;" % (element, function) for function, element, _ in accessors)
    definitions = "\n\n".join("        std::span<const %s> %s() noexcept\n        {\n            return %s;\n        }" % (element, function, array) for function, element, array in accessors)

    header = """/**
 * Auto-generated by tools/gen_encounters3gc.py from PKHeX's Colosseum and XD encounter data. DO NOT EDIT BY
 * HAND -- rerun the generator instead.
 *
 * One accessor per PKHeX array (Encounters3Colo, Encounters3XD), unmerged and in PKHeX's order. The shadow
 * templates carry their locked teams (Encounters3ColoShadow/XDShadow, as permuted by Encounters3ColoTeams/XDTeams).
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERDATA3GC_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERDATA3GC_H

#include <span>

#include "Legality/Encounters/EncounterArea3XD.h"
#include "Legality/Encounters/EncounterGift3Colo.h"
#include "Legality/Encounters/EncounterShadow3Colo.h"
#include "Legality/Encounters/EncounterShadow3XD.h"
#include "Legality/Encounters/EncounterStarter3Colo.h"
#include "Legality/Encounters/EncounterStatic3XD.h"
#include "Legality/Encounters/EncounterTrade3XD.h"

namespace Legality
{
    namespace Encounters
    {
%s
    }
}

#endif
""" % declarations
    source = """// Auto-generated by tools/gen_encounters3gc.py. DO NOT EDIT BY HAND.
#include "Legality/Encounters/EncounterData3GC.h"

#include "Legality/RNG/LockFinder.h"

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
    print("wrote %s (%d arrays, %d Colosseum and %d XD locked teams)" % (os.path.relpath(OUT_CPP, ROOT), len(accessors), len(colo_teams), len(xd_teams)))


if __name__ == "__main__":
    main()
