#!/usr/bin/env python3
"""Compare the encounter engine's matched template with PKHeX's, Pokemon by Pokemon.

    encounter_diff.py <pkse-encounter.jsonl> <pkhex.jsonl> [--verbose]

<pkse-encounter.jsonl> comes from `legality --encounter`, <pkhex.jsonl> is legalitydump's saves or
files output (the parity cache in build-harness/pkhex-cache/). Both key a Pokemon by (title, slot).
PKHeX's `encounter` is LegalInfo.EncounterOriginal's LongName, which PKSE's template longName()
reproduces. Mismatches are grouped by origin version. Exits non-zero on any mismatch.
"""
import json
import sys
from collections import defaultdict


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
    mismatches_by_origin = defaultdict(list)
    for key in sorted(set(pkse) & set(pkhex)):
        ours = pkse[key]
        theirs = pkhex[key]
        origin = ours.get("originVersion", 0)
        expected = theirs.get("encounter") or ""
        if ours.get("encounter") == expected:
            agreed += 1
        else:
            mismatches_by_origin[origin].append((key, ours.get("encounter"), expected))

    mismatch_count = sum(len(entries) for entries in mismatches_by_origin.values())
    print(f"compared {agreed + mismatch_count}, agreed {agreed}, mismatched {mismatch_count}")
    print(f"only in PKSE {len(set(pkse) - set(pkhex))}, only in PKHeX {len(set(pkhex) - set(pkse))}")
    for origin in sorted(mismatches_by_origin):
        entries = mismatches_by_origin[origin]
        print(f"origin version {origin}: {len(entries)} mismatched")
        shown = entries if verbose else entries[:5]
        for key, ours, expected in shown:
            print(f"    {key[0]} {key[1]}: PKSE {ours!r}, PKHeX {expected!r}")
    return 1 if mismatch_count else 0


if __name__ == "__main__":
    sys.exit(main())
