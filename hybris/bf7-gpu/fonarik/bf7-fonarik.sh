#!/bin/sh
# BF7: фонарик одним нажатием — горит, так гасим; погашен, так зажигаем.
#
# Устройство white:torch даёт модуль bf7_torch (kernel-work/bf7-torch).
# Зажигаем на верхнюю ступень: это ток самого чипа после сброса, то есть
# значение, которое производитель считает разумным по умолчанию.
#
# Отдельная кнопка, а не переключатель в шторке Phosh: шторка включает
# фонарь через logind, а у нашей оболочки нет сеанса logind (почему и чем
# кончилась попытка его завести — docs/bf7-kernel/research-log.md, 2026-09-23).
set -u

t=/sys/class/leds/white:torch
if [ ! -d "$t" ]; then
	echo "bf7-fonarik: нет $t — не загружен модуль bf7_torch" >&2
	exit 1
fi

if [ "$(cat "$t/brightness")" -gt 0 ]; then
	echo 0 > "$t/brightness"
else
	cat "$t/max_brightness" > "$t/brightness"
fi
