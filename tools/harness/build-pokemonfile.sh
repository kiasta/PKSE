#!/usr/bin/env bash
# Build and run the Pokemon file harness on the HOST: PKSE's .pkX reader and writer against every
# entity file in PKHeX's test tree (see pokemonfile.cpp for what each file is checked for).
#
#   ./tools/harness/build-pokemonfile.sh [PKHeX tests directory]     (default ~/repos/PKHeX/Tests)
#
# Exits non-zero on any failed check.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$ROOT/build-harness"
TESTS="${1:-$HOME/repos/PKHeX/Tests}"

mkdir -p "$OUT"

SRC=()
for d in src/Trainer src/Pokemon src/Encryption src/Names src/Enums; do
  while IFS= read -r f; do SRC+=("$f"); done < <(find "$ROOT/$d" -name '*.cpp')
done
SRC+=("$ROOT/src/Save/Block.cpp")
SRC+=("$ROOT/src/Utils/StringHelpers.cpp")
SRC+=("$ROOT/src/Utils/HelperUtilities.cpp")
SRC+=("$ROOT/src/Utils/EventLog.cpp")
SRC+=("$ROOT/src/Utils/SHA256.cpp")
SRC+=("$ROOT/src/Utils/MD5.cpp")
SRC+=("$ROOT/tools/harness/pokemonfile.cpp")

echo "Compiling ${#SRC[@]} sources for the host..."
MEMECRYPTO_A=$(sh "$ROOT/tools/harness/memecrypto.sh" "$OUT")

g++ -std=c++20 -O1 -w \
    -include cstdint -include cstddef -include Globals.h \
    -I"$ROOT/include" -I"$ROOT/memecrypto" \
    -o "$OUT/pokemonfile" "${SRC[@]}" "$MEMECRYPTO_A" || { echo "BUILD FAILED"; exit 1; }

echo
"$OUT/pokemonfile" "$TESTS"
