#!/bin/sh
# Подставляем стоковые разделы Android под libhybris.
#
# Логические тома лежат внутри super (/dev/mmcblk0p42) сплошными кусками,
# смещения найдены поиском UUID суперблока ext4 в дампе super.raw.img и
# проверены по имени тома на живом железе.
set -e

SUPER=/dev/mmcblk0p42

mount_logical() {
	name=$1; off=$2; size=$3; mp=/android/$name
	mkdir -p "$mp"
	mountpoint -q "$mp" && return 0
	loop=$(losetup -f --show -r -o "$off" --sizelimit "$size" "$SUPER")
	mount -o ro "$loop" "$mp"
}

mount_logical product     1048576    2737209344
mount_logical system      2738880512 1195098112
mount_logical system_ext  3934257152 1728778240
mount_logical vendor      5663358976 496889856

# Наши распакованные файлы идут первым lowerdir и перекрывают стоковые:
# в стоке /system/lib64/libc.so — симлинк в /apex, а apex монтируется позже.
for part in system vendor; do
	[ -d "/$part.local" ] || mv "/$part" "/$part.local"
	mkdir -p "/$part"
	mountpoint -q "/$part" && continue
	if [ "$part" = system ]; then
		lower=/system.local:/android/system/system
	else
		lower=/vendor.local:/android/vendor
	fi
	mount -t overlay overlay -o ro,lowerdir=$lower "/$part"
done

python3 /usr/local/sbin/bf7-apex-mount.py || true

# HWC и часть библиотек ищут фреймбуфер по андроидному пути.
mkdir -p /dev/graphics
ln -sf /dev/fb0 /dev/graphics/fb0

exit 0
