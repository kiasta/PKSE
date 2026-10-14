#!/usr/bin/env bash
# Build and run the PKSM bank import harness on the HOST, not the Switch.
#
# Same trick as build.sh: the save layer is pure byte manipulation, so an ordinary g++ compiles it.
# Bank.cpp IS included here (unlike the round-trip harness) because commit() is half of what is
# being tested; its one filesystem call, Utils::readAllBytes, is stubbed in pksmimport.cpp.
#
#   ./tools/harness/build-pksmimport.sh [path/to/pksm_1.bnk]
#
# Defaults to ~/pkse-test-saves/pksm_1.bnk, beside the test saves. Copy it from a 3DS SD card's
# 3ds/PKSM/extDataBackup/banks/. Exits non-zero on any failed check.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$ROOT/build-harness"
BNK="${1:-$HOME/pkse-test-saves/pksm_1.bnk}"

mkdir -p "$OUT"

SRC=()
for d in src/Trainer src/Pokemon src/Encryption src/Names src/Enums; do
  while IFS= read -r f; do SRC+=("$f"); done < <(find "$ROOT/$d" -name '*.cpp')
done
SRC+=("$ROOT/src/Save/Block.cpp")
SRC+=("$ROOT/src/Utils/StringHelpers.cpp")
SRC+=("$ROOT/src/Utils/HelperUtilities.cpp")
SRC+=("$ROOT/src/Utils/SHA256.cpp")
SRC+=("$ROOT/src/Utils/MD5.cpp")
SRC+=("$ROOT/tools/harness/pksmimport.cpp")

echo "Compiling ${#SRC[@]} sources for the host..."
MEMECRYPTO_A=$(sh "$ROOT/tools/harness/memecrypto.sh" "$OUT")

g++ -std=c++20 -O1 -w \
    -include cstdint -include cstddef \
    -I"$ROOT/include" -I"$ROOT/memecrypto" \
    -o "$OUT/pksmimport" "${SRC[@]}" "$MEMECRYPTO_A" || { echo "BUILD FAILED"; exit 1; }

echo
"$OUT/pksmimport" "$BNK"
