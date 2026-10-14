#!/usr/bin/env bash
# Build and run the Gen 1 entity harness on the HOST, not the Switch.
#
# Gen 1 is decidable from data: a PK1 party record stores the battle stats the game computed, so
# the tables, the big-endian reads, the DV unpack and the stat formula can all be checked against
# real records instead of eyeballed on hardware. Gen 1 has no checksum, so this is the only thing
# that would catch a wrong parse.
#
#   ./tools/harness/build-gen1.sh [path/to/pksm_1.bnk]
#
# Runs the table, charset and setter checks with or without a bank; the real-record checks need one.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$ROOT/build-harness"
mkdir -p "$OUT"

# Same source set as build-pksmimport.sh. The Gen 1 entity itself needs only a handful of
# these, but the legality analyzer reaches across Pokemon/, Names/, Enums/ and Trainer/, and
# naming files one at a time here means a link failure every time one is added.
SRC=()
for d in src/Trainer src/Pokemon src/Encryption src/Names src/Enums src/Legality; do
  while IFS= read -r f; do SRC+=("$f"); done < <(find "$ROOT/$d" -name '*.cpp')
done
SRC+=("$ROOT/src/Save/Block.cpp")
SRC+=("$ROOT/src/Utils/StringHelpers.cpp")
SRC+=("$ROOT/src/Utils/HelperUtilities.cpp")
SRC+=("$ROOT/src/Utils/SHA256.cpp")
SRC+=("$ROOT/src/Utils/MD5.cpp")
SRC+=("$ROOT/tools/harness/gen1.cpp")

echo "Compiling ${#SRC[@]} sources for the host..."
MEMECRYPTO_A=$(sh "$ROOT/tools/harness/memecrypto.sh" "$OUT")

g++ -std=c++20 -O1 -w \
    -include cstdint -include cstddef \
    -I"$ROOT/include" -I"$ROOT/memecrypto" \
    -o "$OUT/gen1" "${SRC[@]}" "$MEMECRYPTO_A" || { echo "BUILD FAILED"; exit 1; }

echo
"$OUT/gen1" "$@"
