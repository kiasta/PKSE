#!/usr/bin/env bash
# Build and run the DATA-TABLE half of PKSE's self test on the HOST.
#
# The self test proper runs inside the .nro on a console, because most of what it checks -- real
# saves, the SD card, ROMFS -- exists only there. The suites that check pure data do not, and this
# runs those against exactly the code the .nro compiles: species and move names, the personal table,
# the experience curves, per-generation item pouches and learnsets, and the Gen 1 tables, for every
# save-format group at once.
#
#   ./tools/harness/build-selftest.sh [report-path]
#
# Exits non-zero if any check fails, so it can gate a release.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$ROOT/build-harness"
mkdir -p "$OUT"

# The tables themselves plus the two suites that read them. Deliberately a NARROW source list: the
# per-game Trainer and Encryption classes are not needed here (nothing opens a save), and leaving
# them out is what keeps this build a few seconds rather than a minute. The suites that DO need the
# Switch SDK are excluded by construction -- the harness calls the two host-safe ones directly rather
# than through the registry, so nothing ever pulls them in.
# Bank.cpp IS built, even though the bank suite is not run here: it owns the one mapping from a save
# format group to its entity class, and the tables suite's capability sweep constructs an entity per
# group through it. The alternative -- a second copy of an eighteen-way switch in the test -- is a
# table that has to be kept in step with the real one, which is the failure this whole suite exists
# to catch. Its SD-card paths are never reached from here. The suites that need the Switch SDK --
# "save", which writes through Save::GetSaveFileContents -- are excluded by construction: the
# harness calls the host-safe suites DIRECTLY rather than through the registry in TestSuite.cpp, so
# nothing ever pulls them in.
SRC=()
for d in src/Pokemon src/Names src/Enums src/Encryption src/Legality src/Conversion; do
  while IFS= read -r f; do SRC+=("$f"); done < <(find "$ROOT/$d" -name '*.cpp')
done
while IFS= read -r f; do SRC+=("$f"); done < <(find "$ROOT/src/Trainer" -name '*.cpp')
SRC+=("$ROOT/src/Save/Block.cpp")          # SCBlock parse/serialize
SRC+=("$ROOT/src/Utils/SHA256.cpp")
SRC+=("$ROOT/src/Utils/MD5.cpp")
SRC+=("$ROOT/src/Utils/StringHelpers.cpp")
SRC+=("$ROOT/src/Utils/HelperUtilities.cpp")
SRC+=("$ROOT/src/Utils/EventLog.cpp")      # Test::monLabel spells a pokemon with Utils::briefMon
SRC+=("$ROOT/src/Test/TestReport.cpp")
SRC+=("$ROOT/src/Test/SuiteTables.cpp")
SRC+=("$ROOT/src/Test/SuiteGenTables.cpp")
SRC+=("$ROOT/src/Test/SuiteEntity.cpp")
SRC+=("$ROOT/src/Test/SuiteSetters.cpp")
SRC+=("$ROOT/src/Test/SuiteLegality.cpp")
SRC+=("$ROOT/src/Test/SuiteConversion.cpp")
SRC+=("$ROOT/src/Test/SuitePokemonFile.cpp")
SRC+=("$ROOT/tools/harness/selftest.cpp")

MEMECRYPTO_A=$(sh "$ROOT/tools/harness/memecrypto.sh" "$OUT")

# -include Globals.h / cstdint / cstddef mirror what the other harnesses do: a few headers reach
# these transitively through <switch.h> on console and have to be handed them off it.
echo "Compiling ${#SRC[@]} sources for the host..."
g++ -std=c++20 -O1 -w \
    -include cstdint -include cstddef -include Globals.h \
    -I"$ROOT/include" -I"$ROOT/memecrypto" \
    -o "$OUT/selftest" "${SRC[@]}" "$MEMECRYPTO_A" || { echo "BUILD FAILED"; exit 1; }

echo
PKSE_SELFTEST_WORK="$OUT/selftest-work" "$OUT/selftest" "$@"
