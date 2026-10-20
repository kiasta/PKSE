#!/bin/sh
# Builds and runs the Gen 4/5/6/7 save-container harness on the HOST -- see oldgens.cpp.
# Usage: tools/harness/build-oldgens.sh [saves-root]      (default ~/pkse-test-saves)
set -e
cd "$(dirname "$0")/../.."
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

MEMECRYPTO_A=$(sh tools/harness/memecrypto.sh "$OUT")

g++ -std=c++20 -O1 -Wall -Wno-unused-parameter \
    -include cstdint -include cstddef \
    -I include \
    tools/harness/oldgens.cpp \
    src/Trainer/Trainer.cpp src/Trainer/Trainer2GSC.cpp \
    src/Trainer/Trainer4DP.cpp src/Trainer/Trainer4PT.cpp src/Trainer/Trainer4HGSS.cpp \
    src/Trainer/Trainer5BW.cpp src/Trainer/Trainer5B2W2.cpp \
    src/Trainer/Trainer6XY.cpp src/Trainer/Trainer6ORAS.cpp \
    src/Trainer/Trainer7SM.cpp src/Trainer/Trainer7USUM.cpp \
    src/Pokemon/Pokemon2GSC.cpp \
    src/Pokemon/Pokemon4DP.cpp src/Pokemon/Pokemon4PT.cpp src/Pokemon/Pokemon4HGSS.cpp \
    src/Pokemon/Pokemon5BW.cpp src/Pokemon/Pokemon5B2W2.cpp \
    src/Pokemon/Pokemon6XY.cpp src/Pokemon/Pokemon6ORAS.cpp \
    src/Pokemon/Pokemon7SM.cpp src/Pokemon/Pokemon7USUM.cpp \
    src/Pokemon/Experience.cpp src/Pokemon/PIDSearch.cpp src/Pokemon/Gen1Tables.cpp src/Pokemon/Gen2Tables.cpp \
    src/Pokemon/PersonalRecord.cpp $(find src/Pokemon -name 'PersonalInfo*.cpp') \
    $(find src/Names -name '*.cpp') \
    src/Encryption/Encryption.cpp \
    src/Encryption/Encryption4DP.cpp src/Encryption/Encryption4PT.cpp src/Encryption/Encryption4HGSS.cpp \
    src/Encryption/Encryption5BW.cpp src/Encryption/Encryption5B2W2.cpp \
    src/Encryption/Encryption6XY.cpp src/Encryption/Encryption6ORAS.cpp \
    src/Encryption/Encryption7SM.cpp src/Encryption/Encryption7USUM.cpp \
    src/Utils/HelperUtilities.cpp src/Utils/StringHelpers.cpp src/Utils/SHA256.cpp \
    src/Save/Block.cpp src/Save/SaveEnvelope.cpp src/Enums/SCTypeCode.cpp \
    -I memecrypto "$MEMECRYPTO_A" \
    -o "$OUT/oldgens"

"$OUT/oldgens" "$@"
