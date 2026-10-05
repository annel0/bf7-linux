#!/bin/sh
# BF7: время до первого кадра программы (первый attach в WAYLAND_DEBUG):
#   bf7-start-vremya.sh команда...
# время до первого кадра: start.sh команда...
P=$(pgrep -x phoc | head -1)
eval "$(tr '\0' '\n' < /proc/$P/environ | grep -E '^(WAYLAND_DISPLAY|XDG_RUNTIME_DIR|LD_PRELOAD|GDK_GL|LANG|HOME|XDG_DATA_DIRS|DBUS_SESSION_BUS_ADDRESS|XDG_CURRENT_DESKTOP|XDG_SESSION_TYPE|GST_GL_API|PULSE_SERVER|PULSE_RUNTIME_PATH)=' | sed "s/^\([A-Z_]*\)=\(.*\)/export \1='\2'/")"
[ -n "${WAYLAND_DISPLAY:-}" ] || export WAYLAND_DISPLAY=wayland-0
export WAYLAND_DEBUG=client
t0=$(date +%s.%N)
"$@" > /tmp/wd.log 2>&1 &
pid=$!
i=0
while [ $i -lt 300 ]; do
	if grep -q "wl_surface@[0-9]*\.attach(wl_buffer@[0-9]" /tmp/wd.log 2>/dev/null; then break; fi
	sleep 0.05; i=$((i+1))
done
t1=$(date +%s.%N)
awk -v a=$t0 -v b=$t1 -v n=$(basename $1) -v i=$i "BEGIN{printf \"%s: %.2f с до первого кадра%s\\n\", n, b-a, (i>=300?\" (не дождался)\":\"\")}"
sleep 1
kill $pid 2>/dev/null; sleep 0.5; kill -9 $pid 2>/dev/null
wait $pid 2>/dev/null
