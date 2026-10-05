#!/bin/sh
# BF7: вид с камеры на мониторе ПК — и одновременно для Клауда.
#
# Камеру (/dev/video0) может держать только один процесс, поэтому её читает
# один ffmpeg и раздваивает поток: одна ветка — окно XVideo на мониторе,
# другая — свежий кадр в /tmp/bf7-glaz-potok/poslednij.jpg дважды в секунду.
# bf7-glaz.sh берёт этот кадр сам, если окно открыто (кадр моложе 3 с).
#
#   tools/bf7-glaz-okno.sh          — открыть (повторный вызов ничего не делает)
#   tools/bf7-glaz-okno.sh stop     — закрыть
set -u
potok=/tmp/bf7-glaz-potok
ustr=${BF7_KAMERA:-/dev/video0}

if [ "${1:-}" = stop ]; then
	pkill -f "[b]f7-glaz-potok/poslednij.jpg" && echo "окно закрыто"
	exit 0
fi
if pgrep -f "[b]f7-glaz-potok/poslednij.jpg" >/dev/null; then
	echo "окно уже открыто"
	exit 0
fi
mkdir -p "$potok"
v4l2-ctl -d "$ustr" -c brightness=30,contrast=57,gamma=3 2>/dev/null || true
DISPLAY=${DISPLAY:-:0} setsid ffmpeg -hide_banner -loglevel error \
	-f v4l2 -input_format yuyv422 -video_size 640x480 -framerate 30 -i "$ustr" \
	-filter_complex "[0:v]split=2[a][b];[a]scale=960:720,format=yuv420p[okno];[b]fps=2[kadr]" \
	-map "[okno]" -f xv -window_title "BF7 — вид с камеры" -window_size 960x720 display \
	-map "[kadr]" -update 1 -q:v 2 -y "$potok/poslednij.jpg" \
	> "$potok/ffmpeg.log" 2>&1 < /dev/null &
sleep 2
[ -s "$potok/poslednij.jpg" ] && echo "окно открыто, кадр: $potok/poslednij.jpg" ||
	{ echo "не завелось:"; cat "$potok/ffmpeg.log"; exit 1; }
