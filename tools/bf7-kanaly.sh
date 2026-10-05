#!/bin/sh
# BF7: жив ли телефон — по трём каналам, как просил хозяин: USB, а если его
# нет — камера (что на экране) и ssh по Wi-Fi. Одна строка итога.
#
# Вызов: tools/bf7-kanaly.sh [файл-снимка]
# Выход: 0 — отвечает хотя бы один ssh, 1 — ни USB, ни Wi-Fi.
cd "$(dirname "$0")/.." || exit 2
snimok=${1:-/tmp/bf7-kanaly.png}

if usb=$(ssh -o ConnectTimeout=4 -o BatchMode=yes bf7 'echo "аптайм $(cut -d. -f1 /proc/uptime) с, $(systemctl is-system-running)"' 2>/dev/null); then
	echo "USB: $usb"
	exit 0
fi
if [ -e /dev/ttyACM0 ]; then usb_ustr="USB-устройство есть, ssh нет"; else usb_ustr="USB-устройства нет"; fi
wifi=$(ssh -o ConnectTimeout=5 -o BatchMode=yes bf7w 'echo "аптайм $(cut -d. -f1 /proc/uptime) с, $(systemctl is-system-running)"' 2>/dev/null) ||
	wifi="нет ответа"
timeout 40 tools/bf7-glaz.sh --polnyj >/dev/null 2>&1 && cp glaz-polnyj.png "$snimok" &&
	kamera="снимок $snimok" || kamera="камера не сняла"
echo "USB: $usb_ustr; Wi-Fi: $wifi; $kamera"
case "$wifi" in "нет ответа") exit 1;; esac
exit 0
