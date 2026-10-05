#!/bin/sh
# BF7: страховка после сна — сеть и модем не должны остаться «спящими».
#
# Ставится в /usr/lib/systemd/system-sleep/ (стадия «post»).
# 2026-09-24 после автосна на батарее NetworkManager остался в состоянии
# «asleep» (сигнал пробуждения до него не дошёл): Wi-Fi и мобильные данные
# пропали до ручного вмешательства, а ModemManager потерял AT-порт и модем.
#
# 1) NetworkManager «спит» — разбудить (Sleep false; для бодрствующего
#    ответ «уже бодрствует», вреда нет).
# 2) Через 30 с, когда ModemManager уже должен был заново найти модем:
#    модем готов (md1:4), а модема в ModemManager нет — перезапустить его.
# 3) Поднять усилитель динамика и камеру: перед сном их останавливает systemd (Conflicts=sleep.target
#    в bf7-camera.service / bf7-camd.service) — см. там, зачем.
[ "$1" = post ] || exit 0

# Вывод подсветки (GPIO43) здесь НЕ трогаем: после глубокого сна экран ещё
# погашен, дисплейный блок обесточен, и в режиме ШИМ подсветка светила бы на
# полную. Функцию ШИМ вернёт правило 79-bf7-podsvetka.rules, когда подсветку
# включат (событие модуля bf7_backlight).
rm -f /run/bf7-gpio43-shim

if nmcli -t -f STATE general 2>/dev/null | grep -qx asleep; then
	busctl call org.freedesktop.NetworkManager /org/freedesktop/NetworkManager \
		org.freedesktop.NetworkManager Sleep b false >/dev/null 2>&1
	echo "bf7-posle-sna: NetworkManager спал после пробуждения — разбужен" > /dev/kmsg
fi

# Усилитель динамика (перед сном его гасит Conflicts=sleep.target) — первым и
# быстро: при пробуждении звонком мелодия не должна играть в тишину.
systemd-run --quiet --on-active=1 --unit=bf7-posle-sna-usilitel-$$ \
	systemctl start bf7-extamp.service >/dev/null 2>&1

# Камеру поднимаем, только если она включена (хозяин мог её выключить). Не
# сразу: крюк работает, пока systemd-suspend ещё выполняется, и запуск службы с
# Conflicts=sleep.target systemd отвергает («transaction is destructive»).
if systemctl is-enabled -q bf7-camd.service 2>/dev/null; then
	systemd-run --quiet --on-active=3 --unit=bf7-posle-sna-kamera-$$ \
		systemctl start bf7-camd.service >/dev/null 2>&1
fi

systemd-run --quiet --on-active=30 --unit=bf7-posle-sna-modem-$$ /bin/sh -c '
	grep -q "md1:4" /sys/kernel/ccci/boot 2>/dev/null || exit 0
	mmcli -L 2>/dev/null | grep -q "/Modem/" && exit 0
	echo "bf7-posle-sna: модем готов, а ModemManager его не видит — перезапуск" > /dev/kmsg
	systemctl restart ModemManager' >/dev/null 2>&1
exit 0
