#!/bin/sh
# BF7: кто будит процессор — с точностью до функции.
#
# Счёт переключений по /proc/*/status (см. заметку bf7-wakeup-audit) говорит
# «kworker/2:2-events_freezable — 33 раза в секунду», но не говорит, какая
# работа там крутится. Точки трассировки ядра говорят: имя функции работы,
# имя функции таймера, имя прерывания. Так отличается «опрос термозон» от
# «опроса карты памяти» без догадок по исходникам.
#
# Что включаем (все есть в нашем ядре: CONFIG_EVENT_TRACING=y):
#   workqueue/workqueue_execute_start — какая работа выполнилась;
#   timer/timer_expire_entry, timer/hrtimer_expire_entry — какой таймер сработал;
#   irq/irq_handler_entry — какое прерывание пришло;
#   cpuhp/cpuhp_enter — ядра включаются и выключаются (горячее подключение
#     MediaTek, поток hps_main): каждый переход — десятки шагов, дорого;
#   sched/sched_migrate_task — кого перекладывают между ядрами. Отсюда
#     частые пробуждения потоков migration/N.
#
# Лучше мерить при погашенном экране: сценарий ждёт, пока подсветка станет 0
# (не дольше трёх минут), и пишет, в каком состоянии мерил.
#
# Вызов: tools/bf7-kto-budit.sh [секунд=20]
set -eu

sekund=${1:-20}
telefon=${BF7_TELEFON:-bf7}

ssh "$telefon" "sekund=$sekund sh -s" <<'NA_TELEFONE'
set -u
T=/sys/kernel/tracing
[ -d "$T/events" ] || T=/sys/kernel/debug/tracing
[ -d "$T/events" ] || { echo "нет tracefs"; exit 1; }

i=0
# Погашен ли экран — по разъёму DRM: файл подсветки после загрузки может
# хранить прежнюю яркость при уже спящем тракте экрана.
while [ "$i" -lt 36 ] && [ "$(cat /sys/class/drm/card1-DSI-1/enabled)" != disabled ]; do
	sleep 5; i=$((i + 1))
done
echo "вывод при замере: $(cat /sys/class/drm/card1-DSI-1/enabled) (disabled — экран погашен)"

sobytiya="workqueue/workqueue_execute_start timer/timer_expire_entry timer/hrtimer_expire_entry irq/irq_handler_entry cpuhp/cpuhp_enter sched/sched_migrate_task"
bylo_on=$(cat "$T/tracing_on")
echo 0 > "$T/tracing_on"
echo > "$T/trace"
echo 4096 > "$T/buffer_size_kb"
for e in $sobytiya; do echo 1 > "$T/events/$e/enable"; done
echo 1 > "$T/tracing_on"
sleep "$sekund"
echo 0 > "$T/tracing_on"
for e in $sobytiya; do echo 0 > "$T/events/$e/enable"; done

# Сводка: сколько раз за замер каждая работа, таймер и прерывание.
awk -v s="$sekund" '
	/workqueue_execute_start:/ { if (match($0, /function [^ ]+/)) r["работа   " substr($0, RSTART + 9, RLENGTH - 9)]++ }
	/timer_expire_entry:/      { if (match($0, /function=[^ ]+/)) r["таймер   " substr($0, RSTART + 9, RLENGTH - 9)]++ }
	/irq_handler_entry:/       { if (match($0, /name=[^ ]+/)) r["прерыв.  " substr($0, RSTART + 5, RLENGTH - 5)]++ }
	/cpuhp_enter:/             { if (match($0, /cpu: [0-9]+/)) r["подключ. cpu" (substr($0, RSTART + 5, RLENGTH - 5) + 0) " (шагов)"]++ }
	/sched_migrate_task:/      { if (match($0, /comm=[^ ]+/)) r["миграц.  " substr($0, RSTART + 5, RLENGTH - 5)]++ }
	END {
		for (k in r) { printf "%8.1f/с  %s\n", r[k] / s, k }
	}' "$T/trace" | sort -rn | head -30

poteri=$(grep -c "LOST" "$T/trace" 2>/dev/null || true)
[ "${poteri:-0}" -gt 0 ] && echo "ВНИМАНИЕ: буфер переполнялся ($poteri раз) — числа занижены"

echo > "$T/trace"
echo "$bylo_on" > "$T/tracing_on"
NA_TELEFONE
