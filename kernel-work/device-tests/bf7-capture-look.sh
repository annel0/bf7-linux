#!/bin/sh
# BF7: снять с телефона снимок панели и посмотреть на него.
#
# Снимок делает WDMA0 внутри ядра — это то, что тракт дисплея действительно
# вывел, а не то, что мы думаем, будто вывели.
set -e
SSH="ssh -o StrictHostKeyChecking=no -o ConnectTimeout=10 root@192.168.7.2"
OUT=${1:-/tmp/bf7-capture}
W=$($SSH 'cat /sys/class/graphics/fb0/virtual_size 2>/dev/null | cut -d, -f1' || echo 720)
H=720
H=$($SSH 'cat /sys/class/drm/card1-DSI-1/modes 2>/dev/null | head -1 | cut -dx -f2' || echo 1612)
W=$($SSH 'cat /sys/class/drm/card1-DSI-1/modes 2>/dev/null | head -1 | cut -dx -f1' || echo 720)

$SSH "dd if=/sys/kernel/debug/bf7disp/capture bs=1M 2>/dev/null" > "$OUT.raw"
printf 'снято %s байт при %sx%s\n' "$(stat -c %s "$OUT.raw")" "$W" "$H"

# Снимок в BGRA8888: синий в младшем байте.
convert -size "${W}x${H}" -depth 8 "bgra:$OUT.raw" -resize 30% "$OUT.png"
printf 'смотреть: %s.png\n' "$OUT"
