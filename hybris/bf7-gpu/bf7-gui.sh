#!/bin/sh
# Запуск GUI на панели телефона: Qt Quick поверх PowerVR через libhybris.
set -e

# Консоль перестаёт рисовать поверх нас; до этого момента на экране виден
# обычный вывод загрузки.
[ -w /sys/class/vtconsole/vtcon1/bind ] && echo 0 > /sys/class/vtconsole/vtcon1/bind || true

export LD_LIBRARY_PATH=/usr/local/lib
export LD_PRELOAD=/usr/local/lib/libbf7tlsguard.so
export EGL_PLATFORM=mtkfb
export QT_QPA_PLATFORM=eglfs
export QT_QPA_EGLFS_INTEGRATION=none
export QT_QPA_EGLFS_PHYSICAL_WIDTH=62
export QT_QPA_EGLFS_PHYSICAL_HEIGHT=139
export QT_IM_MODULE=qtvirtualkeyboard
# Без этого плагин пытается сделать клавиатуру отдельным окном,
# а eglfs больше одного окна не умеет.
export QT_VIRTUALKEYBOARD_DESKTOP_DISABLE=1
export XDG_RUNTIME_DIR=/run/bf7-gui
mkdir -p "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"

cd /root/gui
exec ./bf7gui "${1:-/root/gui/demo6.qml}"
