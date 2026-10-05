#!/bin/sh
# BF7: подготовить Bluetooth к запуску моста.
#
# Грузит модули и создаёт узел. Отдельным скриптом, а не строками в описании
# службы, потому что тут нужна проверка «уже загружено»: rmmod на этом ядре
# роняет систему, так что перезагружать модули нельзя ни при каких условиях.
#
# Имена переменных только латиницей: /bin/sh кириллические не принимает.
set -u

extra=/lib/modules/$(uname -r)/extra

load() {
	name=$1
	grep -q "^${name} " /proc/modules && return 0
	[ -f "$extra/$name.ko" ] || { echo "нет модуля $name" >&2; return 1; }
	insmod "$extra/$name.ko"
}

# Ждём связность. bt_drv опирается на символы wmt_drv, а тот грузит подъём
# Wi-Fi — служба Type=simple, то есть «запущена» сразу, а модули появляются
# позже. Без ожидания insmod отвечает «Unknown symbol in module».
i=0
while [ "$i" -lt 120 ]; do
	grep -q "^wmt_drv " /proc/modules && break
	sleep 0.5
	i=$((i + 1))
done
if ! grep -q "^wmt_drv " /proc/modules; then
	echo "связность не поднялась за минуту, Bluetooth пропускаю" >&2
	exit 1
fi

# Мало дождаться модуля: подъём Wi-Fi ещё какое-то время работает с чипом
# через WMT, и если влезть в этот промежуток, открытие /dev/stpbt отвечает
# EIO, а мост умирает. Признак завершения — появление wlan0, им и пользуемся,
# плюс небольшая выдержка на то, чтобы чип успокоился.
i=0
while [ "$i" -lt 120 ]; do
	[ -e /sys/class/net/wlan0 ] && break
	sleep 0.5
	i=$((i + 1))
done
[ -e /sys/class/net/wlan0 ] || echo "wlan0 не появился, пробую всё равно" >&2
sleep 3

load bt_drv || exit 1
load hci_vhci || exit 1
load rfcomm || true
load hidp || true

# Узел символьного устройства BT. Номер берём из /proc/devices, а не
# закрепляем: драйвер регистрируется динамически.
if [ ! -c /dev/stpbt ]; then
	major=$(awk '/mtk_stp_bt_chrdev/ {print $1}' /proc/devices | head -1)
	[ -n "$major" ] || { echo "драйвер не зарегистрировал устройство" >&2; exit 1; }
	mknod -m 660 /dev/stpbt c "$major" 0
fi

# Готовность чипа пробой НЕ проверяем. Пробовал — и наступил на собственные
# грабли: чтение /dev/stpbt у этого драйвера непрерываемо, и когда чип готов,
# read просто не возвращается. `timeout 2 dd` не помог: процесс пережил даже
# SIGKILL, systemd написал «Processes still around after final SIGKILL» и
# оставил висеть dd, державший устройство. Снимается это только перезагрузкой.
#
# Поэтому ждём по косвенным признакам и полагаемся на перезапуск службы:
# сперва wlan0 должен не только появиться, но и связаться (operstate не down),
# потом выдержка. Если чип всё равно не готов, мост упадёт с EIO, а systemd
# попробует снова через пятнадцать секунд — так и получается надёжно, без
# опасных проб.
i=0
while [ "$i" -lt 60 ]; do
	sostoyanie=$(cat /sys/class/net/wlan0/operstate 2>/dev/null || echo нет)
	[ "$sostoyanie" = "up" ] && break
	sleep 0.5
	i=$((i + 1))
done
sleep 8

# Заводской адрес Bluetooth. Лежит в разделе nvdata первыми шестью байтами
# файла APCFG/APRDEB/BT_Addr — там же, где заводской MAC для Wi-Fi (в файле
# WIFI, со смещения 4). У этого телефона: BT e4:f8:be:xx:xx:xx,
# Wi-Fi e4:f8:be:xx:xx:xx — настоящий OUI, а не заглушка.
#
# Читаем один раз и складываем в кэш: при обычной загрузке чужой раздел
# трогать незачем. Монтируем строго для чтения и без журнала, чтобы не
# записать в него ни байта.
kesh=/etc/bf7-bt-addr
if [ ! -s "$kesh" ]; then
	tochka=$(mktemp -d)
	if mount -o ro,noload -t ext4 /dev/mmcblk0p7 "$tochka" 2>/dev/null; then
		if [ -s "$tochka/APCFG/APRDEB/BT_Addr" ]; then
			dd if="$tochka/APCFG/APRDEB/BT_Addr" of="$kesh" bs=1 count=6 \
				2>/dev/null && chmod 600 "$kesh"
			echo "bf7-bt-prepare: заводской адрес взят из nvdata"
		fi
		umount "$tochka" 2>/dev/null
	else
		echo "bf7-bt-prepare: nvdata не прочитать, адрес будет свой" >&2
	fi
	rmdir "$tochka" 2>/dev/null
fi

echo "bf7-bt-prepare: модули на месте, /dev/stpbt готов"
