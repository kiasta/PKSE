#!/usr/bin/env python3
"""Compare PKSE's MethodFinder port with PKHeX's, Pokemon by Pokemon.

    pidiv_diff.py <pkse.jsonl> <pkhex.jsonl> [--verbose]

<pkse.jsonl> comes from `legality --pidiv`, <pkhex.jsonl> from `legalitydump pidiv`. Both key a
Pokemon by (title, slot). A Pokemon agrees when the inputs (PID, EC, IVs) match and the outputs
(PID type, origin seed) match; an input mismatch means the two loaders read different data, which
is reported separately from an RNG disagreement. Exits non-zero on any disagreement.
"""
import json
import sys
from collections import Counter


def load(path):
    records = {}
    for line in open(path, encoding="utf-8"):
        line = line.strip()
        if not line:
            continue
        record = json.loads(line)
        if "error" in record:
            continue
        records[(record["title"], record["slot"])] = record
    return records


def main():
    arguments = [argument for argument in sys.argv[1:] if not argument.startswith("--")]
    verbose = "--verbose" in sys.argv
    if len(arguments) != 2:
        print(__doc__)
        return 2
    pkse = load(arguments[0])
    pkhex = load(arguments[1])

    agreed = 0
    input_mismatches = []
    output_mismatches = []
    only_pkse = sorted(set(pkse) - set(pkhex))
    only_pkhex = sorted(set(pkhex) - set(pkse))
    types = Counter()
    for key in sorted(set(pkse) & set(pkhex)):
        ours = pkse[key]
        theirs = pkhex[key]
        if (ours["pid"], ours["ec"], ours["ivs"]) != (theirs["pid"], theirs["ec"], theirs["ivs"]):
            input_mismatches.append((key, ours, theirs))
            continue
        if (ours["pidType"], ours["seed"]) != (theirs["pidType"], theirs["seed"]):
            output_mismatches.append((key, ours, theirs))
            continue
        agreed += 1
        types[theirs["pidType"]] += 1

    compared = agreed + len(output_mismatches)
    print(f"compared {compared}, agreed {agreed}, RNG disagreements {len(output_mismatches)}")
    print(f"input mismatches {len(input_mismatches)}, only in PKSE {len(only_pkse)}, only in PKHeX {len(only_pkhex)}")
    print("types agreed: " + ", ".join(f"{name} {count}" for name, count in types.most_common()))
    for key, ours, theirs in output_mismatches:
        print(f"  DISAGREE {key[0]} {key[1]}: PKSE {ours['pidType']} {ours['seed']:#010x}, PKHeX {theirs['pidType']} {theirs['seed']:#010x}")
    if verbose:
        for key, ours, theirs in input_mismatches:
            print(f"  INPUT {key[0]} {key[1]}: PKSE pid {ours['pid']:#010x} ec {ours['ec']:#010x} ivs {ours['ivs']}, PKHeX pid {theirs['pid']:#010x} ec {theirs['ec']:#010x} ivs {theirs['ivs']}")
        for key in only_pkse:
            print(f"  ONLY PKSE {key[0]} {key[1]}")
        for key in only_pkhex:
            print(f"  ONLY PKHEX {key[0]} {key[1]}")
    return 0 if not output_mismatches and not input_mismatches else 1


if __name__ == "__main__":
    sys.exit(main())
