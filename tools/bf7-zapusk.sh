#!/bin/sh
# BF7: запустить программу в живом сеансе phosh как транзиентную службу:
#   bf7-zapusk.sh ИМЯ-ЮНИТА команда...   (убирать: systemctl stop ИМЯ-ЮНИТА)
# zapusk.sh ИМЯ-юнита команда... — запуск в сеансе phosh
U=$1; shift
P=$(pgrep -x phoc | head -1)
ENVS=$(tr '\0' '\n' < /proc/$P/environ | grep -E '^(WAYLAND_DISPLAY|XDG_RUNTIME_DIR|LD_PRELOAD|GDK_GL|LANG|HOME|XDG_DATA_DIRS|DBUS_SESSION_BUS_ADDRESS|XDG_CURRENT_DESKTOP|XDG_SESSION_TYPE|GST_GL_API|GST_PLUGIN_FEATURE_RANK|PULSE_SERVER|PULSE_RUNTIME_PATH)=' | sed 's/^/-E /' | tr '\n' ' ')
tr '\0' '\n' < /proc/$P/environ | grep -q ^WAYLAND_DISPLAY || ENVS="$ENVS -E WAYLAND_DISPLAY=wayland-0"
ENVS="$ENVS -E DISPLAY=:0"
exec systemd-run -q --unit=$U $ENVS "$@"
