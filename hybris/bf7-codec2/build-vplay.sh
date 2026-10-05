#!/bin/sh
# Сборка проигрывателя. Код протокола linux-dmabuf генерируется на месте:
# готовой библиотеки для него нет, только описание.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
XML=/usr/share/wayland-protocols/unstable/linux-dmabuf/linux-dmabuf-unstable-v1.xml

wayland-scanner client-header "$XML" "$HERE/linux-dmabuf-unstable-v1-client-protocol.h"
wayland-scanner public-code   "$XML" "$HERE/linux-dmabuf-unstable-v1-protocol.c"

gcc -O2 -o "$HERE/bf7-vplay" \
	"$HERE/bf7-vplay.c" "$HERE/linux-dmabuf-unstable-v1-protocol.c" \
	-I"$HERE" -lwayland-client
echo "собрано: $HERE/bf7-vplay"
