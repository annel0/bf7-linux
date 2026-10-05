#!/bin/sh
# BF7: отметить текущий слот успешно загрузившимся.
#
# Подробности — в описании службы bf7-boot-ok.service. Коротко: без этого
# счётчик попыток загрузчика тает с каждой перезагрузкой и телефон в конце
# концов уходит на слот со стоковым ядром.
#
# Ждём три минуты и проверяем, что графический сеанс жив: отметка должна
# означать «система действительно работает», а не «ядро дошло до init».
set -u

sleep 180

if ! systemctl is-active --quiet bf7-phosh.service &&
   ! systemctl is-active --quiet bf7-session.service; then
	echo "bf7-boot-ok: графического сеанса нет, отметку не ставлю" >&2
	exit 0
fi

helper=/usr/local/sbin/bf7-mark-boot-success
if [ ! -x "$helper" ]; then
	echo "bf7-boot-ok: помощника $helper нет — счётчик не тронут" >&2
	exit 0
fi

"$helper" && echo "bf7-boot-ok: слот отмечен успешным"
