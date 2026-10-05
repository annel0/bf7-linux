#!/bin/sh
# Сборка 32-битного аппаратного декодера. Пояснения — в build32.sh.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
OUT=${OUT:-$HERE/c2dec32} SRC=$HERE/c2dec32.cpp
export OUT
sed 's|c2probe32.cpp|c2dec32.cpp|' "$HERE/build32.sh" > "$HERE/.build-dec32.tmp"
OUT="$OUT" sh "$HERE/.build-dec32.tmp"
rm -f "$HERE/.build-dec32.tmp"
