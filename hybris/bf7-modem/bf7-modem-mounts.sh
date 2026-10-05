#!/bin/sh
# BF7: разделы NVRAM модема на путях Android (/mnt/vendor/*).
#
# Модем при запуске и в работе читает и пишет свои файлы через fsd
# (в ccci_mdinit) и HAL NVRAM, а те знают только андроидные пути. Опции —
# как в /vendor/etc/fstab.mt6761. Перед монтированием e2fsck -p, как
# «check» в том же fstab: IMEI и калибровка радио лежат здесь, и монтировать
# на запись битую ФС нельзя. Если e2fsck не справился — раздел не трогаем,
# модем не поднимется, а данные целы. Копии всех разделов, снятые до первого
# запуска: firmware-backup/radio-nv-20260923 на ПК.
set -u
OPC=noatime,nosuid,nodev,noauto_da_alloc,commit=1,nodelalloc
oshibka=0
for m in nvdata:nvdata nvcfg:nvcfg protect_f:protect1 protect_s:protect2 persist:persist; do
	kuda=/mnt/vendor/${m%%:*}
	otkuda=/dev/disk/by-partlabel/${m#*:}
	mountpoint -q "$kuda" && continue
	mkdir -p "$kuda"
	e2fsck -p "$otkuda" >/dev/null 2>&1
	kod=$?
	if [ "$kod" -ge 4 ]; then
		echo "bf7-modem-mounts: $otkuda: e2fsck $kod, не монтирую" >&2
		oshibka=1
		continue
	fi
	mount -t ext4 -o "$OPC" "$otkuda" "$kuda" || oshibka=1
done
exit $oshibka
