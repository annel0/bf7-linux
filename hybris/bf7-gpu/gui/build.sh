#!/bin/sh
# Сборка композитора. Собирается на самом телефоне: Qt здесь системный.
#
# Кроме собственно композитора в двоичный файл входят две серверные половины
# протоколов, которых Qt не предоставляет:
#   wldrm.cpp  — wl_drm, без него Xwayland не включает glamor;
#   wlegl.cpp  — android_wlegl, без него libhybris не отдаёт клиентам EGL,
#                и браузеры остаются на программной графике.
# Разбор буферов для второго уже написан в libhybris и лежит в
# libhybris-platformcommon; отсюда и связывание с /usr/local/lib.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
OUT=${OUT:-$HERE/bf7gui}
SCANNER=${SCANNER:-wayland-scanner}

PKGS="Qt6Core Qt6Gui Qt6Qml Qt6WaylandCompositor wayland-server"
CFLAGS=$(pkg-config --cflags $PKGS)
LIBS=$(pkg-config --libs $PKGS)

# Описание wl_drm в Qt не входит, разбор генерируем сами.
[ -f "$HERE/wayland-drm-server-protocol.h" ] ||
	"$SCANNER" server-header "$HERE/drm.xml" "$HERE/wayland-drm-server-protocol.h"
[ -f "$HERE/wayland-drm-protocol.c" ] ||
	"$SCANNER" private-code "$HERE/drm.xml" "$HERE/wayland-drm-protocol.c"

cc -c -O2 -fPIC -o "$HERE/wayland-drm-protocol.o" "$HERE/wayland-drm-protocol.c"

# Приватные заголовки Qt нужны только компоновке; сам код пользуется
# открытым QWaylandCompositor.
g++ -std=c++17 -O2 -fPIC -Wall \
	-I"$HERE" $CFLAGS \
	-o "$OUT" \
	"$HERE/main.cpp" "$HERE/wldrm.cpp" "$HERE/wlegl.cpp" \
	"$HERE/wayland-drm-protocol.o" \
	$LIBS \
	-L/usr/local/lib -Wl,-rpath,/usr/local/lib \
	-lhybris-platformcommon -lgralloc

echo "собрано: $OUT"
