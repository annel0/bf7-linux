#!/bin/sh
# Интервалы настоящих кадров phoc во время жеста (kprobe на drm_mode_atomic_ioctl,
# пробные фиксации TEST_ONLY=0x100 отброшены). Вызов: sh bf7-kadry.sh <жест...>,
# например: sh bf7-kadry.sh bf7-palec swipe 360 1560 360 500 250
# LC_ALL=C обязателен: в русской локали awk читает «35239.247608» как 35239.
export LC_ALL=C
T=/sys/kernel/tracing
echo > $T/kprobe_events
echo 'p:bf7kadr drm_mode_atomic_ioctl flags=+0(%x1):u32' >> $T/kprobe_events
echo 1 > $T/events/kprobes/enable
: > $T/trace; echo 1 > $T/tracing_on
"$@" >/dev/null 2>&1
sleep 1.2
echo 0 > $T/tracing_on; echo 0 > $T/events/kprobes/enable
grep bf7kadr $T/trace > /tmp/kraw
echo > $T/kprobe_events
echo "сырых срабатываний: $(wc -l < /tmp/kraw)"
awk '{t=""; f=""; for(i=1;i<=NF;i++){ if($i ~ /^[0-9]+\.[0-9]+:$/){t=$i; sub(":","",t)} if($i ~ /^flags=/){f=$i; sub("flags=","",f)} } if(t!="" && (int(f)%512)<256) print t}' /tmp/kraw > /tmp/kt
echo "настоящих кадров: $(wc -l < /tmp/kt)"
awk 'NR==1{s=$1} {e=$1} END{printf "длительность: %.0f мс\n", (e-s)*1000}' /tmp/kt
awk 'NR>1{printf "%.1f ", ($1-p)*1000} {p=$1} END{print ""}' /tmp/kt
