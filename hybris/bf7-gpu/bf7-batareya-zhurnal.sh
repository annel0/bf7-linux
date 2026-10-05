#!/bin/sh
# BF7: журнал батареи для замера расхода без кабеля.
#
# Раз в минуту (пока процессор бодрствует — во сне цикл спит вместе со всеми
# и телефон не будит) пишет строку: время, кабель, кулонометр (0,1 мА·ч),
# заряд %, число снов. Расход считается по участку, где usb=0:
#   разность кулонометра / длительность.
# На кабеле цифра не показательна: USB держит такты и не пускает в глубокий
# простой (см. research-log 2026-09-24).
#
# Запуск: systemd-run --unit=bf7-batareya /usr/local/bin/bf7-batareya-zhurnal
# Журнал: /root/bf7-batareya.log
L=${1:-/root/bf7-batareya.log}
G=/sys/class/power_supply/mtk-gauge
while :; do
	echo "$(date +%s) usb=$(cat /sys/class/power_supply/usb/online) car=$(cat $G/coulomb_get) cap=$(cat /sys/class/power_supply/battery/capacity) snov=$(cat /sys/power/suspend_stats/success)" >> "$L"
	sleep 60
done
