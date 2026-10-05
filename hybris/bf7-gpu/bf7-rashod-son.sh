#!/bin/sh
# Запускать через systemd-run: systemd-run /root/bf7-rashod-son.sh 600; итог в /root/bf7-rashod-son.log.
# coulomb_get — 0,1 мА·ч; разность за сон / длительность = средний ток во сне.
# Расход во сне: кулонометр до и после rtcwake freeze; число успешных снов и причины пробуждений
G=/sys/class/power_supply/mtk-gauge
L=/root/bf7-rashod-son.log
S=${1:-600}
{
echo "до: $(date +%s) car=$(cat $G/coulomb_get) ok=$(cat /sys/power/suspend_stats/success) stat=$(i2cget -f -y 3 0x6a 0)"
cat /sys/kernel/debug/wakeup_sources 2>/dev/null | awk 'NR>1{print $1,$6}' | sort > /root/ws-do.txt
rtcwake -m freeze -s $S
echo "после: $(date +%s) car=$(cat $G/coulomb_get) ok=$(cat /sys/power/suspend_stats/success) fail=$(cat /sys/power/suspend_stats/fail) stat=$(i2cget -f -y 3 0x6a 0)"
cat /sys/kernel/debug/wakeup_sources 2>/dev/null | awk 'NR>1{print $1,$6}' | sort > /root/ws-posle.txt
join /root/ws-do.txt /root/ws-posle.txt | awk '$3-$2>0{print "проб.", $1, $3-$2}'
dmesg | grep -iE "PM: (suspend|resume)|wakeup|Freezing|s2idle" | tail -12
} > $L 2>&1
