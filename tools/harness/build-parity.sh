#!/usr/bin/env bash
# Legality PARITY: PKSE's findings against PKHeX's, on the same saves.
#
# PKHeX is the source of truth for legality and PKSE ports its checks. This runs PKSE's analyzer
# over every test save (build-legality.sh --json), runs PKHeX's own LegalityAnalysis over exactly
# the files PKSE opened (tools/legalitydump), and diffs them per Pokemon and per check
# (parity_diff.py). Every gap is a PKHeX check PKSE has not ported or got wrong.
#
#   ./tools/harness/build-parity.sh <save-root> [parity_diff.py options...]
#
# Needs the .NET SDK and a PKHeX checkout: PKHEX_LOCAL (default ~/repos/PKHeX).
#
# PKHeX's side depends only on the saves and the PKHeX checkout, never on PKSE's code, so it is
# computed once and cached in build-harness/pkhex-cache/; PARITY_REFRESH_PKHEX=1 recomputes it
# (after a PKHeX pull or a change of test saves). PKSE_HARNESS_OUT moves PKSE's side elsewhere, so
# several runs can share one tree.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${PKSE_HARNESS_OUT:-$ROOT/build-harness}"
SAVE_ROOT="${1:?usage: build-parity.sh <save-root> [parity_diff.py options...]}"
shift
PKHEX_LOCAL="${PKHEX_LOCAL:-$HOME/repos/PKHeX}"
PKHEX_CORE="$PKHEX_LOCAL/PKHeX.Core/PKHeX.Core.csproj"
[ -f "$PKHEX_CORE" ] || { echo "PKHeX.Core not found at $PKHEX_CORE (set PKHEX_LOCAL)"; exit 2; }
mkdir -p "$OUT"

"$ROOT/tools/harness/build-legality.sh" "$SAVE_ROOT" --json "$OUT/parity-pkse.jsonl" > "$OUT/parity-legality.log" 2>&1 || { echo "legality harness failed; see $OUT/parity-legality.log"; tail -20 "$OUT/parity-legality.log"; exit 1; }

MANIFEST=()
while IFS= read -r entry; do
  [ -n "$entry" ] && MANIFEST+=("$entry")
done < "$OUT/parity-pkse.jsonl.manifest"

# PKHeX's own test files: its Legal/ and Illegal/ trees reach checks real catches never do.
TESTS="$PKHEX_LOCAL/Tests"
"$OUT/legality" --files "$TESTS" --json "$OUT/parity-files-pkse.jsonl" > /dev/null 2>&1 || { echo "legality --files failed"; exit 1; }

CACHE="$ROOT/build-harness/pkhex-cache"
mkdir -p "$CACHE"
if [ "${PARITY_REFRESH_PKHEX:-0}" = "1" ] || [ ! -s "$CACHE/saves.jsonl" ] || [ ! -s "$CACHE/files.jsonl" ]; then
  dotnet run -c Release --project "$ROOT/tools/legalitydump" -p:PKHexCore="$PKHEX_CORE" -- saves "${MANIFEST[@]}" --out "$CACHE/saves.jsonl" > "$CACHE/saves.log" 2>&1 || { echo "legalitydump failed; see $CACHE/saves.log"; tail -20 "$CACHE/saves.log"; exit 1; }
  dotnet run -c Release --project "$ROOT/tools/legalitydump" -p:PKHexCore="$PKHEX_CORE" -- files "$TESTS" --out "$CACHE/files.jsonl" > "$CACHE/files.log" 2>&1 || { echo "legalitydump files failed; see $CACHE/files.log"; exit 1; }
fi

echo "=== Real saves ==="
python3 "$ROOT/tools/harness/parity_diff.py" "$OUT/parity-pkse.jsonl" "$CACHE/saves.jsonl" "$@"
SAVES_STATUS=$?
echo
echo "=== PKHeX test files ==="
python3 "$ROOT/tools/harness/parity_diff.py" "$OUT/parity-files-pkse.jsonl" "$CACHE/files.jsonl" "$@"
FILES_STATUS=$?
[ $SAVES_STATUS -eq 0 ] && [ $FILES_STATUS -eq 0 ]
