#!/bin/sh
# BF7: проверка будильника bf7-budilnik — наяву и во сне, с камерой.
#
# Заводит в GNOME Clocks проверочный будильник на ближайшую минуту (не ближе
# 70 с), ждёт звонка, смотрит журнал службы и звуковой поток feedbackd,
# снимает панель КАМЕРОЙ (tools/bf7-glaz.sh: снимок буфера показывает, что
# собрал композитор, а не что горит на панели), снимает звонок так, как это
# сделал бы хозяин (закрывает уведомление), и возвращает список будильников.
#
# Режим «son»: телефон гасит экран и засыпает (systemctl suspend) со
# страховочным будильником RTC и должен проснуться сам ровно к звонку.
# Первая версия запускала всё одним ssh — сон рвал связь, и уборка не
# выполнялась. Теперь телефон только заводит и засыпает, а ждёт, смотрит и
# убирает эта машина; прежний список будильников лежит на телефоне в файле.
#
# Вызов: tools/bf7-budilnik-proverka.sh [najavu|son]
set -u
rezhim=${1:-najavu}
telefon=${BF7_TELEFON:-bf7}
KORNI=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
kadr=${TMPDIR:-/tmp}/bf7-budilnik-kamera.png

SEANS='for q in $(ls /proc | grep -E "^[0-9]+$"); do
	case "$(readlink /proc/$q/exe 2>/dev/null)" in */phosh) obol=$q; break;; esac
done
export DBUS_SESSION_BUS_ADDRESS=$(tr "\0" "\n" < /proc/$obol/environ | sed -n "s/^DBUS_SESSION_BUS_ADDRESS=//p")
export XDG_RUNTIME_DIR=/run/bf7-phosh WAYLAND_DISPLAY=wayland-0'

# 1. Завести (и уснуть).
cel=$(ssh "$telefon" "$SEANS
	[ -f /root/bf7-budilnik-proverka.bylo ] || gsettings get org.gnome.clocks alarms > /root/bf7-budilnik-proverka.bylo
	cel=\$(( (\$(date +%s) + 70 + 59) / 60 * 60 ))
	gsettings set org.gnome.clocks alarms \"[{'name': <'bf7-проверка'>, 'id': <'bf7proverka'>, 'active': <true>, 'hour': <\$(date -d @\$cel +%-H)>, 'minute': <\$(date -d @\$cel +%-M)>, 'days': <@ai []>, 'snooze_minutes': <10>}]\"
	echo \$cel") || { echo "не завёлся"; exit 1; }
t0=$(( cel - 80 ))
echo "1. заведено на $(date -d "@$cel" +%H:%M:%S)"
sleep 2
ssh "$telefon" "journalctl -u bf7-budilnik -n 1 -o cat --no-pager; echo \"   таймер: \$(systemctl list-timers --all --no-legend bf7-budilnik-zvonok.timer | awk '{print \$1, \$2, \$3}')\""

if [ "$rezhim" = son ]; then
	ssh "$telefon" "$SEANS
		busctl --user set-property org.gnome.Mutter.DisplayConfig /org/gnome/Mutter/DisplayConfig \
			org.gnome.Mutter.DisplayConfig PowerSaveMode i 3
		sleep 3
		echo 0 > /sys/class/rtc/rtc0/wakealarm
		echo $(( cel + 120 )) > /sys/class/rtc/rtc0/wakealarm
		setsid -f systemctl suspend </dev/null >/dev/null 2>&1
		echo \"   усыпляю в \$(date +%T), страховка RTC на +2 мин после звонка\""
	sleep 10
	ping -c 1 -W 2 192.168.7.2 >/dev/null 2>&1 && echo "   ВНИМАНИЕ: телефон всё ещё на связи — уснул ли?" ||
		echo "   телефон спит (связи нет)"
fi

# 2. Дождаться звонка и посмотреть камерой.
while [ "$(date +%s)" -lt $(( cel + 8 )) ]; do sleep 2; done
i=0
until ssh -o ConnectTimeout=4 -o BatchMode=yes "$telefon" true 2>/dev/null; do
	i=$((i + 1)); [ "$i" -ge 40 ] && { echo "нет связи с телефоном 3 мин после звонка"; exit 1; }
	sleep 3
done
"$KORNI/tools/bf7-glaz.sh" "$kadr" >/dev/null 2>&1 && echo "2. кадр камерой: $kadr" || echo "2. камера не сняла кадр"

# 3. Проверить, снять звонок, убрать.
ssh "$telefon" "$SEANS
	zh=\$(journalctl -u bf7-budilnik --since @$t0 -o cat --no-pager)
	echo \"3. \$(echo \"\$zh\" | grep -E 'звонок «|звоню сам|Clocks запущен' | tr '\n' '|')\"
	echo \"   в журнале звонок в \$(journalctl -u bf7-budilnik --since @$t0 -o short-iso --no-pager | grep 'звонок «' | tail -1 | cut -c12-19) (назначено $(date -d "@$cel" +%H:%M:%S))\"
	[ $rezhim = son ] && echo \"   пробуждение: \$(journalctl -b -o cat --no-pager --since @$t0 | grep -E 'Resume caused' | tail -1)\"
	echo \"   экран: вывод \$(wlr-randr | sed -n 's/ *Enabled: //p'), подсветка \$(cat /sys/class/leds/lcd-backlight/brightness); звуковых потоков: \$(PULSE_SERVER=unix:/run/bf7-pulse/native pactl list short sink-inputs | wc -l)\"
	id=\$(echo \"\$zh\" | sed -n 's/.*уведомление \([0-9]*\).*/\1/p' | tail -1)
	[ -n \"\$id\" ] && gdbus call --session --dest org.freedesktop.Notifications --object-path /org/freedesktop/Notifications \
		--method org.freedesktop.Notifications.CloseNotification \"\$id\" >/dev/null
	sleep 2
	echo \"4. \$(journalctl -u bf7-budilnik --since @$t0 -o cat --no-pager | grep 'звонок снят' | tail -1)\"
	gsettings set org.gnome.clocks alarms \"\$(cat /root/bf7-budilnik-proverka.bylo)\" && rm -f /root/bf7-budilnik-proverka.bylo
	echo 0 > /sys/class/rtc/rtc0/wakealarm
	sleep 3   # feedbackd гасит звук не мгновенно
	echo \"5. будильники возвращены: \$(gsettings get org.gnome.clocks alarms | cut -c1-60); таймер: \$(systemctl is-active bf7-budilnik-zvonok.timer); звуковых потоков: \$(PULSE_SERVER=unix:/run/bf7-pulse/native pactl list short sink-inputs | wc -l)\""
