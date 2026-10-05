#!/bin/sh
# BF7: доводит Wi-Fi до подключения после загрузки.
#
# Зачем. При загрузке драйвер MTK не успевает подготовить интерфейс к тому
# моменту, когда NetworkManager просит посредника им заняться. Посредник
# отвечает «wpa_supplicant couldn't grab this interface», NetworkManager делает
# пять попыток и **сдаётся навсегда** — устройство остаётся в состоянии
# unavailable до ручного вмешательства.
#
# Что делает. Ждёт появления интерфейса, даёт ему время, и если через минуту
# соединения нет — опускает и поднимает интерфейс, чистит застрявшие сокеты
# управления и перезапускает посредника с NetworkManager. Проверено: после
# этого соединение встаёт.
#
# Имена переменных только латиницей: /bin/sh кириллические не принимает.
set -u

IFACE=wlan0
TRIES=3

state() {
	nmcli -t -f DEVICE,STATE device 2>/dev/null | sed -n "s/^${IFACE}://p"
}

# Ждём, пока интерфейс вообще появится.
n=0
while [ ! -d "/sys/class/net/$IFACE" ] && [ "$n" -lt 60 ]; do
	n=$((n + 1))
	sleep 2
done
[ -d "/sys/class/net/$IFACE" ] || { echo "bf7-wifi-watch: $IFACE так и не появился" >&2; exit 0; }

try=0
while [ "$try" -lt "$TRIES" ]; do
	# Даём минуту на самостоятельное подключение.
	n=0
	while [ "$n" -lt 30 ]; do
		[ "$(state)" = connected ] && {
			echo "bf7-wifi-watch: подключено"
			exit 0
		}
		n=$((n + 1))
		sleep 2
	done

	try=$((try + 1))
	echo "bf7-wifi-watch: попытка $try — состояние «$(state)», встряхиваю" >&2

	systemctl stop NetworkManager >/dev/null 2>&1
	sleep 2
	rm -f /run/wpa_supplicant/"$IFACE" /run/wpa_supplicant/wlan1 /run/wpa_supplicant/ap0 2>/dev/null
	ip link set "$IFACE" down 2>/dev/null
	sleep 1
	ip link set "$IFACE" up 2>/dev/null
	sleep 2
	systemctl restart wpa_supplicant >/dev/null 2>&1
	sleep 3
	systemctl start NetworkManager >/dev/null 2>&1
done

echo "bf7-wifi-watch: за $TRIES попытки соединение не поднялось" >&2
