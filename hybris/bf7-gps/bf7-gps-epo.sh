#!/bin/sh
# BF7: эфемериды GPS наперёд (EPO) для mnld — быстрый первый фикс.
#
# mnld качает EPO сам, но у bionic DNS идёт через netd (/dev/socket/dnsproxyd),
# которого у нас нет, а серверы из его настроек (sfepodownload/sgepodownload)
# без подписанной ссылки отвечают 403. Публичный wepodownload.mediatek.com
# отдаёт тот же EPO_GR_3_1 (GPS+ГЛОНАСС на 3 суток, 48 КБ) простым GET — с
# телефона уходит только IP, как при apt. Проверено 2026-09-30: mnld принял
# («EPO start time … 00:00:00», «EPO file is valid») и загрузил в чип
# ($PMTK843).
#
# SUPL (A-GPS через supl.google.com) НЕ включаем: он шлёт соту и SET ID —
# решение хозяина.
#
# Файл обновляется, если ему больше 20 часов. Запуск — bf7-gps-epo.timer.
set -eu

KAT=/data/vendor/gps
URL=http://wepodownload.mediatek.com/EPO_GR_3_1
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

if [ -f "$KAT/EPO.DAT" ] && [ -z "$(find "$KAT/EPO.DAT" -mmin +1200)" ]; then
	echo "bf7-gps-epo: EPO свежий, не качаю"
	exit 0
fi
wget -q -T 30 -t 2 -O "$tmp/EPO.DAT" "$URL.DAT"
wget -q -T 30 -t 2 -O "$tmp/EPO.MD5" "$URL.MD5"
ozh=$(awk '{print $1}' "$tmp/EPO.MD5")
est=$(md5sum "$tmp/EPO.DAT" | awk '{print $1}')
[ -n "$ozh" ] && [ "$ozh" = "$est" ] || { echo "bf7-gps-epo: MD5 не сошёлся ($est ≠ $ozh)" >&2; exit 1; }
mkdir -p "$KAT"
cp "$tmp/EPO.DAT" "$KAT/.EPO.DAT.new" && mv "$KAT/.EPO.DAT.new" "$KAT/EPO.DAT"
cp "$tmp/EPO.MD5" "$KAT/.EPO.MD5.new" && mv "$KAT/.EPO.MD5.new" "$KAT/EPO_GR_3_1.MD5"
echo "bf7-gps-epo: EPO обновлён ($(wc -c < "$KAT/EPO.DAT") байт)"
