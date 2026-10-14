#!/usr/bin/env bash
# Build and run the save round-trip harness on the HOST, not the Switch.
#
# The save layer is pure byte manipulation, so it compiles with an ordinary g++. Only the Switch
# SDK bits are excluded: FileUtilities (filesystem/accounts) and the title-lookup half of
# HelperUtilities are #ifdef'd out off-console, and this harness supplies its own logging stubs.
#
#   ./tools/harness/build.sh [save-root]
#
# save-root defaults to a directory of real saves; see roundtrip.cpp for the expected layout.
# Exits non-zero if any game fails to round-trip, so it can gate a release.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$ROOT/build-harness"
SAVES="${1:-$HOME/pkse-test-saves}"

mkdir -p "$OUT"

# Trainer67 signs Gen 7 saves, so pulling in src/Trainer drags in memecrypto. It is plain C and
# must be compiled with gcc rather than g++ -- it uses `xor` as an identifier, which is an
# alternative token for `^` in C++. See tools/harness/memecrypto.sh.
MEMECRYPTO_A=$(sh "$ROOT/tools/harness/memecrypto.sh" "$OUT")

# Every source the save layer needs, and nothing that touches the Switch SDK.
SRC=()
for d in src/Trainer src/Pokemon src/Encryption src/Names src/Enums; do
  # Bank.cpp and the Pokemon file import/export (which is built on it) are out of scope: SD-card storage rather than a save write path, and it uses POSIX
  # mkdir. The bank has its own in-app round-trip verification.
  while IFS= read -r f; do SRC+=("$f"); done < <(find "$ROOT/$d" -name '*.cpp' ! -name 'Bank.cpp' ! -name 'PokemonFile*.cpp')
done
SRC+=("$ROOT/src/Save/Block.cpp")          # SCBlock parse/serialize
SRC+=("$ROOT/src/Utils/StringHelpers.cpp")
SRC+=("$ROOT/src/Utils/HelperUtilities.cpp")
SRC+=("$ROOT/src/Utils/SHA256.cpp")        # SCBlock save hashing
SRC+=("$ROOT/src/Utils/MD5.cpp")           # BDSP whole-file hash
# Logger.cpp is deliberately absent -- roundtrip.cpp stubs the log entry points instead, so the
# harness stays identical between the g++ and MSVC builds.
SRC+=("$ROOT/tools/harness/roundtrip.cpp")

echo "Compiling ${#SRC[@]} sources for the host..."
g++ -std=c++20 -O1 -w \
    -I"$ROOT/include" -I"$ROOT/memecrypto" \
    -o "$OUT/roundtrip" "${SRC[@]}" "$MEMECRYPTO_A" || { echo "BUILD FAILED"; exit 1; }

echo
"$OUT/roundtrip" "$SAVES"
