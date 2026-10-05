#!/bin/sh
# BF7: создать недостающие узлы устройств.
#
# Зачем. Подстраховка, а не основной путь. Узлы в /dev заводит само ядро:
# в нашей сборке CONFIG_DEVTMPFS=y и CONFIG_DEVTMPFS_MOUNT=y (проверено по
# /proc/config.gz 2026-09-22), и /dev смонтирован именно как devtmpfs. Прежний
# заголовок этого файла утверждал обратное — это была моя ошибка в разборе
# ночи на 2026-09-22, когда после череды паник недосчитались 232 узлов. Узлов
# не было не потому, что /dev — tmpfs, а потому, что не поднялись модули: нет
# устройства — нет и записи в /sys/dev/char, из которой ядро делает узел.
#
# Держим на случай, если /dev однажды окажется подменён обычным tmpfs (так
# бывает, когда initramfs монтирует его сам и systemd оставляет как есть) или
# если узел пропадёт иначе. На здоровой загрузке служба создаёт ноль узлов и
# стоит меньше секунды — за это её и держим.
#
# Что делает. Обходит /sys/dev/char и /sys/dev/block, берёт из uevent каждого
# устройства имя и номера и создаёт узел, если его нет. Права ставит скромные
# (root, 0600): настоящие раздаст udev своим правилом, когда до него дойдёт.
# Уже существующие узлы не трогает.
#
# Имена переменных только латиницей: /bin/sh кириллические не принимает.
set -u

made=0

scan() {
	kind=$1          # c или b
	base=$2
	for dev in "$base"/*; do
		[ -e "$dev/uevent" ] || continue
		name=$(sed -n 's/^DEVNAME=//p' "$dev/uevent" 2>/dev/null)
		[ -n "$name" ] || continue
		path="/dev/$name"
		[ -e "$path" ] && continue
		mm=$(basename "$dev")
		maj=${mm%%:*}
		min=${mm##*:}
		dir=$(dirname "$path")
		[ -d "$dir" ] || mkdir -p "$dir"
		if mknod "$path" "$kind" "$maj" "$min" 2>/dev/null; then
			chmod 600 "$path"
			made=$((made + 1))
		fi
	done
}

# Полный обход нужен только если /dev не devtmpfs. На здоровой загрузке узлы
# заводит ядро, обход находит ноль пропаж — а стоит он пяти с половиной секунд
# на критическом пути (замер 2026-09-22): сотни каталогов в /sys, и на каждый
# отдельный sed. Поэтому сперва смотрим, чем смонтирован /dev, и если это
# devtmpfs — пропускаем обход, оставляя только дешёвую заготовку петель ниже.
devtmpfs=0
while read -r _ mnt_point mnt_type _; do
	[ "$mnt_point" = /dev ] && [ "$mnt_type" = devtmpfs ] && devtmpfs=1
done < /proc/mounts

if [ "$devtmpfs" = 1 ]; then
	echo "bf7-devnodes-all: /dev на devtmpfs, обход не нужен"
else
	scan c /sys/dev/char
	scan b /sys/dev/block
fi

# Петлевые устройства ядро заводит по требованию через /dev/loop-control, и на
# devtmpfs узел появляется сам (losetup -f выдаёт /dev/loop12 без нашей помощи).
# Заготовка на восемь остаётся на тот же случай подменённого /dev.
if [ -c /dev/loop-control ]; then
	i=0
	while [ "$i" -lt 8 ]; do
		[ -e "/dev/loop$i" ] || {
			mknod "/dev/loop$i" b 7 "$i" 2>/dev/null && {
				chmod 600 "/dev/loop$i"
				made=$((made + 1))
			}
		}
		i=$((i + 1))
	done
fi

echo "bf7-devnodes-all: создано узлов: $made"

# Пусть udev расставит настоящие права по своим правилам.
udevadm trigger --action=change >/dev/null 2>&1 || true
