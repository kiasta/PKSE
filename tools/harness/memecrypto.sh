#!/bin/sh
# Builds memecrypto/ into a static archive for the host harnesses, and echoes its path.
#
# It has to be compiled AS C: memecrypto.c uses `xor` as an identifier, which is an alternative
# token for `^` in C++, so g++ rejects the file outright. The real build already does the right
# thing -- the Makefile routes .c through $(CC) -- and this is the same rule for the harnesses.
#
# Every harness that links any Trainer source needs it, because Trainer67 (Gen 6/7) signs Gen 7
# saves. Sourced rather than copy-pasted so the next harness cannot get it subtly wrong:
#
#     MEMECRYPTO_A=$(sh "$(dirname "$0")/memecrypto.sh" "$OUT")
#     ... g++ ... -I "$ROOT/memecrypto" "$MEMECRYPTO_A"
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${1:-$(mktemp -d)}
mkdir -p "$OUT"

if [ ! -f "$OUT/memecrypto.a" ]; then
    for f in memecrypto aes sha1 rsa mini-gmp; do
        gcc -std=gnu99 -O1 -w -I "$ROOT/memecrypto" \
            -c "$ROOT/memecrypto/$f.c" -o "$OUT/$f.o" >&2
    done
    ar rcs "$OUT/memecrypto.a" "$OUT"/memecrypto.o "$OUT"/aes.o "$OUT"/sha1.o \
                               "$OUT"/rsa.o "$OUT"/mini-gmp.o >&2
fi
echo "$OUT/memecrypto.a"
