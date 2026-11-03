#!/usr/bin/env python3
"""Compare PKSE's evolution engine port with PKHeX's, Pokemon by Pokemon.

    evolution_diff.py <pkse.jsonl> <pkhex.jsonl> [--verbose]

<pkse.jsonl> comes from `legality --evolution`, <pkhex.jsonl> from `legalitydump evolution`. Both key
a Pokemon by (title, slot). A Pokemon agrees when its species and form match, every context's chain
in the evolution history matches, and the encounter-origin chain matches. A species or form mismatch
means the two loaders read different data, which is reported separately from an engine
disagreement. Exits non-zero on any disagreement.
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
    disagreeing_parts = Counter()
    for key, expected in pkhex.items():
        actual = pkse.get(key)
        if actual is None:
            continue
        if (actual["species"], actual["form"]) != (expected["species"], expected["form"]):
            input_mismatches.append(key)
            continue
        parts = [context for context, chain in expected["history"].items() if actual["history"].get(context) != chain]
        if actual.get("origin") != expected.get("origin"):
            parts.append("origin")
        if parts:
            output_mismatches.append((key, parts, actual, expected))
            disagreeing_parts.update(parts)
            continue
        agreed += 1

    missing_from_pkse = [key for key in pkhex if key not in pkse]
    missing_from_pkhex = [key for key in pkse if key not in pkhex]
    compared = agreed + len(input_mismatches) + len(output_mismatches)
    print(f"compared {compared}: agreed {agreed}, input mismatches {len(input_mismatches)}, engine disagreements {len(output_mismatches)}")
    print(f"only in PKHeX {len(missing_from_pkse)}, only in PKSE {len(missing_from_pkhex)}")
    for part, count in disagreeing_parts.most_common():
        print(f"  {part}: {count}")
    for key in input_mismatches[: None if verbose else 10]:
        print(f"input mismatch {key}: PKSE {pkse[key]['species']}-{pkse[key]['form']} PKHeX {pkhex[key]['species']}-{pkhex[key]['form']}")
    for key, parts, actual, expected in output_mismatches[: None if verbose else 10]:
        print(f"disagreement {key} species {expected['species']}-{expected['form']} encounter {expected['encounter']}")
        for part in parts:
            if part == "origin":
                print(f"    origin PKSE  {actual.get('origin')}")
                print(f"    origin PKHeX {expected.get('origin')}")
            else:
                print(f"    {part} PKSE  {actual['history'].get(part)}")
                print(f"    {part} PKHeX {expected['history'][part]}")
    for key in (missing_from_pkse + missing_from_pkhex)[: None if verbose else 5]:
        print(f"unpaired {key}")
    has_disagreement = input_mismatches or output_mismatches or missing_from_pkse or missing_from_pkhex
    return 1 if has_disagreement else 0


if __name__ == "__main__":
    sys.exit(main())
