#!/bin/sh
# Сборка драйвера VA-API. Ставится в системный каталог драйверов: туда же
# смотрит libva, и только туда песочница процесса декодирования у Firefox
# разрешает заглядывать.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
OUT=${OUT:-$HERE/bf7_drv_video.so}

g++ -std=c++17 -O2 -fPIC -shared -o "$OUT" \
	"$HERE/src/driver.cpp" "$HERE/src/decode.cpp" \
	"$HERE/src/daemon.cpp" "$HERE/src/h264.cpp" "$HERE/src/h265.cpp" \
	-I"$HERE/src" $(pkg-config --cflags libva) \
	-Wall -Wno-unused-parameter
echo "собрано: $OUT"
