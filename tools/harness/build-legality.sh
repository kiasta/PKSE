#!/usr/bin/env bash
# Build and run the LEGALITY harness on the HOST.
#
# roundtrip.sh proves the write path does not corrupt a save; this proves the READ-side judgement is
# not slandering one. Layer 3 (encounter matching, relearn moves, ribbons/marks, OT/handler) is the
# only part of PKSE that can call a legitimately-obtained Pokemon illegal, so the measure that
# matters is what it complains about on saves from actual playthroughs -- where nearly everything IS
# legal and a wall of findings means the tables are wrong, not the save.
#
#   ./tools/harness/build-legality.sh <save-root> [--verbose]
#
# Saves are found by TITLE FOLDER (<root>/Shield/, <root>/Violet/, ...) -- see SaveTree.h.
#
# This replaces a tools/harness/legality.ps1 that the .sh migration left behind: the harness source
# outlived its runner, which is how it stopped being run at all.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$ROOT/build-harness"
mkdir -p "$OUT"

SRC=()
for d in src/Pokemon src/Names src/Enums src/Encryption src/Legality src/Conversion src/Trainer; do
  while IFS= read -r f; do SRC+=("$f"); done < <(find "$ROOT/$d" -name '*.cpp')
done
SRC+=("$ROOT/src/Save/Block.cpp")
SRC+=("$ROOT/src/Utils/SHA256.cpp")
SRC+=("$ROOT/src/Utils/MD5.cpp")
SRC+=("$ROOT/src/Utils/StringHelpers.cpp")
SRC+=("$ROOT/src/Utils/HelperUtilities.cpp")
SRC+=("$ROOT/src/Utils/EventLog.cpp")
SRC+=("$ROOT/tools/harness/legality.cpp")

MEMECRYPTO_A=$(sh "$ROOT/tools/harness/memecrypto.sh" "$OUT")

echo "Compiling ${#SRC[@]} sources for the host..."
g++ -std=c++20 -O1 -w \
    -include cstdint -include cstddef -include Globals.h \
    -I"$ROOT/include" -I"$ROOT/memecrypto" -I"$ROOT/tools/harness" \
    -o "$OUT/legality" "${SRC[@]}" "$MEMECRYPTO_A" || { echo "BUILD FAILED"; exit 1; }

echo
"$OUT/legality" "${@:-$HOME/pkse-test-saves}"
