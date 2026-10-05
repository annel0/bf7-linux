#!/bin/sh
# Запуск экранной клавиатуры в сеансе X, если она не отключена.
BF7_KEYBOARD=onboard
[ -r /etc/default/bf7-display ] && . /etc/default/bf7-display
[ -z "$BF7_KEYBOARD" ] && exit 0
command -v "$BF7_KEYBOARD" >/dev/null || exit 0
exec "$BF7_KEYBOARD"
