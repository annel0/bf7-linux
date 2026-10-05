#!/bin/sh
# BF7: пробный сон с пробуждением по будильнику RTC.
#
# Во время сна связь по USB рвётся, поэтому всё пишется в файл
# /root/bf7-son-proba.log — читать после.
#
# ГЛАВНОЕ ПРАВИЛО, оплаченное 2026-09-23: rtcwake на полпути НЕ ПРЕРЫВАТЬ.
# Первая проба застряла на переключении терминала, я снял rtcwake сигналом —
# и сон пошёл дальше без будильника (тот уже отзвенел впустую), телефон
# уснул насовсем, будила только кнопка питания. Поэтому здесь нет никаких
# kill: если сон завис, сценарий честно пишет об этом и ждёт.
#
# Перед сном проверяем держателей пробуждения: если какой-то активен, ядро
# всё равно сон отменит («early wake event») — пробовать незачем, лучше
# узнать, кто держит.
#
# Вызов: bf7-son-proba [freeze|mem] [секунд]
set -u

rezhim=${1:-freeze}
sekund=${2:-20}
log=/root/bf7-son-proba.log
istochniki=/sys/kernel/debug/wakeup_sources

{
	echo "=== $(date '+%F %T') сон $rezhim на $sekund с, аптайм $(cut -d. -f1 /proc/uptime)"
	echo "вывод: $(cat /sys/class/drm/card1-DSI-1/enabled), подсветка: $(cat /sys/class/leds/lcd-backlight/brightness)"

	aktivnye=$(awk 'NR>1 && $6>0 {print $1}' "$istochniki" 2>/dev/null)
	if [ -n "$aktivnye" ]; then
		echo "активные держатели пробуждения — ядро отменит сон:"
		echo "$aktivnye" | sed 's/^/  /'
	fi

	cat /sys/power/suspend_stats/success /sys/power/suspend_stats/fail \
		2>/dev/null | tr '\n' ' ' | sed 's/^/до: успехов и провалов сна: /'
	echo

	sync
	t0=$(date +%s)
	rtcwake -m "$rezhim" -s "$sekund" 2>&1
	kod=$?
	echo "rtcwake вернул $kod через $(( $(date +%s) - t0 )) с по часам"

	cat /sys/power/suspend_stats/success /sys/power/suspend_stats/fail \
		2>/dev/null | tr '\n' ' ' | sed 's/^/после: успехов и провалов сна: /'
	echo
	echo "последний провал: $(cat /sys/power/suspend_stats/last_failed_dev 2>/dev/null) $(cat /sys/power/suspend_stats/last_failed_step 2>/dev/null)"
	echo "--- ядро о сне:"
	dmesg | grep -iE 'PM: suspend|PM: resume|suspend entry|suspend exit|Freezing|Restarting tasks|wakeup|abort|failed to suspend' | tail -20
	echo "=== конец $(date '+%F %T')"
} >> "$log" 2>&1
