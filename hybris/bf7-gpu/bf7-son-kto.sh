#!/bin/sh
# Запускать на телефоне через systemd-run (связь во сне рвётся): systemd-run /root/bf7-son-kto.sh 6 30
# Итог — /root/bf7-son-kto.log. Сон, прерванный через секунду с ccci_ipc_3+1, — ответ модема
# на сообщение Wi-Fi (см. research-log 2026-09-24, wmt_idc.c в оверлее).
# N снов по S с: длительность, причина и какие источники пробуждения сработали за время сна
L=/root/bf7-son-kto.log
W=/sys/kernel/debug/wakeup_sources
: > $L
snap(){ awk 'NR>1{print $1, $3}' $W | sort; }
for i in $(seq 1 ${1:-6}); do
  snap > /tmp/ws0; t0=$(cut -d. -f1 /proc/uptime)
  rtcwake -m freeze -s ${2:-30} >/dev/null 2>&1
  t1=$(cut -d. -f1 /proc/uptime); snap > /tmp/ws1
  echo "сон $i: $((t1-t0)) с, $(dmesg | grep 'Resume caused by' | tail -1 | sed 's/.*caused by //'); $(join /tmp/ws0 /tmp/ws1 | awk '$3>$2{printf "%s+%d ", $1, $3-$2}')" >> $L
  sleep 4
done
echo готово >> $L
