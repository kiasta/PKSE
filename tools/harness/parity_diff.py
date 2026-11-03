#!/usr/bin/env python3
"""Compare PKSE's legality findings with PKHeX's, Pokemon by Pokemon and check by check.

Inputs are two JSON-lines files in tools/legalitydump's shape: PKSE's (tools/harness/legality.cpp
--json) and PKHeX's (tools/legalitydump saves ...). PKHeX is the reference: every Invalid or Fishy
finding PKHeX reports that PKSE does not is a GAP, and every one PKSE reports that PKHeX does not is
an EXTRA (PKSE calling a Pokemon worse than the reference does).

POKEMON ARE PAIRED BY IDENTITY, NOT BY SLOT KEY. The two programs number boxes differently for some
saves (PKHeX counts X/Y's Battle Box among its boxes), so a Pokemon is keyed by title, species, PID,
encryption constant and its occurrence count among identical keys.

A FINDING IS (identifier, code, severity). Valid results are ignored. PKHeX's per-move lines
(source "move") are compared by identifier and severity only, because PKHeX's code there is a learn
method rather than a result code.

    python tools/harness/parity_diff.py pkse.jsonl pkhex.jsonl [--title Violet] [--verbose] [--summary-only]

Exit status is 0 only when there are no gaps and no extras.
"""
import argparse
import collections
import json
import sys


def load(path):
    pokemon_by_key = {}
    occurrence = collections.Counter()
    errors = []
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if not line:
                continue
            record = json.loads(line)
            if "error" in record:
                errors.append(record)
                continue
            identity = (record["title"], record["species"], record["pid"], record["ec"])
            occurrence[identity] += 1
            pokemon_by_key[identity + (occurrence[identity],)] = record
    return pokemon_by_key, errors


def findings(record):
    found = collections.Counter()
    for result in record.get("results", []):
        if result["severity"] == "Valid":
            continue
        if result.get("source") == "move":
            found[(result["identifier"], "(move)", result["severity"])] += 1
        else:
            found[(result["identifier"], result["code"], result["severity"])] += 1
    return found


def describe(finding):
    identifier, code, severity = finding
    return "%s %s/%s" % (severity, identifier, code)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("pkse")
    parser.add_argument("pkhex")
    parser.add_argument("--title", help="only this save title")
    parser.add_argument("--verbose", action="store_true", help="list every differing Pokemon")
    parser.add_argument("--summary-only", action="store_true", help="totals only")
    args = parser.parse_args()

    pkse, pkse_errors = load(args.pkse)
    pkhex, pkhex_errors = load(args.pkhex)
    for error in pkse_errors + pkhex_errors:
        print("LOAD ERROR %s: %s" % (error.get("title"), error.get("error")))

    keys = sorted(set(pkse) | set(pkhex), key=lambda key: (key[0], key[1], key[2], key[3], key[4]))
    if args.title:
        keys = [key for key in keys if key[0] == args.title]

    gap_count_by_finding = collections.Counter()
    extra_count_by_finding = collections.Counter()
    unpaired = collections.Counter()
    pokemon_compared = 0
    pokemon_agreeing = 0
    differing = []
    for key in keys:
        if key not in pkse or key not in pkhex:
            unpaired["only in PKSE" if key in pkse else "only in PKHeX"] += 1
            continue
        pokemon_compared += 1
        reference = findings(pkhex[key])
        ported = findings(pkse[key])
        gaps = reference - ported
        extras = ported - reference
        gap_count_by_finding.update(gaps)
        extra_count_by_finding.update(extras)
        if not gaps and not extras:
            pokemon_agreeing += 1
        else:
            differing.append((key, gaps, extras))

    print("Pokemon compared: %d, agreeing: %d, differing: %d" % (pokemon_compared, pokemon_agreeing, len(differing)))
    for reason, count in sorted(unpaired.items()):
        print("Unpaired (%s): %d" % (reason, count))
    print("Gap findings (PKHeX reports, PKSE does not): %d" % sum(gap_count_by_finding.values()))
    print("Extra findings (PKSE reports, PKHeX does not): %d" % sum(extra_count_by_finding.values()))
    if args.summary_only:
        return 0 if not gap_count_by_finding and not extra_count_by_finding else 1

    print("\nGAPS by check:")
    for finding, count in gap_count_by_finding.most_common():
        print("  %5d  %s" % (count, describe(finding)))
    print("\nEXTRAS by check:")
    for finding, count in extra_count_by_finding.most_common():
        print("  %5d  %s" % (count, describe(finding)))

    if args.verbose:
        print("\nDIFFERING POKEMON:")
        for key, gaps, extras in differing:
            record = pkhex[key]
            print("  %s %s species %d form %d (%s)" % (key[0], record["slot"], key[1], record.get("form", 0), record.get("encounter")))
            for finding in sorted(gaps):
                messages = [result["message"] for result in record["results"] if (result["identifier"], result["code"] if result.get("source") != "move" else "(move)", result["severity"]) == finding]
                print("      GAP    %s  %s" % (describe(finding), messages[0] if messages else ""))
            for finding in sorted(extras):
                messages = [result["message"] for result in pkse[key]["results"] if (result["identifier"], result["code"], result["severity"]) == finding]
                print("      EXTRA  %s  %s" % (describe(finding), messages[0] if messages else ""))
    return 0 if not gap_count_by_finding and not extra_count_by_finding else 1


if __name__ == "__main__":
    sys.exit(main())
