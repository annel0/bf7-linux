#!/bin/sh
# Полный сеанс рабочего стола на панели телефона.
#
#   композитор Wayland (Qt, наш EGL -> PowerVR)
#     -> Xwayland (X-сервер как обычный клиент Wayland)
#        -> Xfce (xfwm4, панель, рабочий стол)
#
# Клиенты X рисуют в разделяемую память, композиция и вывод на панель идут
# через GPU. Своего ускорения GL у X-клиентов нет: GLX тут неоткуда взять.
set -e

RUNDIR=/run/bf7-gui
WL=wayland-1
XDISP=:1

# Масштаб и поворот панели. Меняются через /usr/local/bin/bf7-display,
# которая правит этот файл и перезапускает сеанс.
BF7_SCALE=1.0
BF7_ROTATION=0
BF7_KEYBOARD=onboard
BF7_DPI=
[ -r /etc/default/bf7-display ] && . /etc/default/bf7-display
export BF7_SCALE BF7_ROTATION

# systemd не задаёт HOME службам, а без него Xfce открывает терминал там, где
# оказался рабочий каталог, и не находит ни настроек, ни ~/.config.
: "${HOME:=/root}"
export HOME
export XDG_RUNTIME_DIR="$RUNDIR"
export XDG_CONFIG_HOME="$HOME/.config"
export XDG_DATA_HOME="$HOME/.local/share"
export XDG_CACHE_HOME="$HOME/.cache"
export USER="${USER:-root}"
export LOGNAME="${LOGNAME:-root}"
export SHELL="${SHELL:-/bin/bash}"
mkdir -p "$RUNDIR" "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_CACHE_HOME"
chmod 700 "$RUNDIR"

# --- проверка среды: без неё всё «работает», но впятеро медленнее ---
if [ ! -e /system/lib64/libEGL.so ] || [ ! -e /vendor/lib64/hw/gralloc.rogue.so ]; then
	echo "ВНИМАНИЕ: стоковые /system и /vendor не смонтированы, GPU не поднимется" >&2
fi
if ! grep -q "^ro.hardware.gralloc=rogue" /system/build.prop 2>/dev/null; then
	echo "ВНИМАНИЕ: в /system/build.prop нет ro.hardware.gralloc=rogue —" \
	     "libhybris возьмёт gralloc.default.so и EGL не заработает" >&2
fi
if [ "$(readelf -dW /system/lib64/libutilscallstack.so 2>/dev/null | grep -c NEEDED)" != "0" ]; then
	echo "ВНИМАНИЕ: libutilscallstack.so не подменена заглушкой —" \
	     "MTK будет печатать раскрутку стека на каждый кадр," \
	     "производительность упадёт примерно впятеро" >&2
fi

# Консоль перестаёт рисовать поверх нас; до этого места на экране обычный
# вывод загрузки.
[ -w /sys/class/vtconsole/vtcon1/bind ] && echo 0 > /sys/class/vtconsole/vtcon1/bind || true

cleanup() {
	kill $XWAYLAND_PID $COMPOSITOR_PID 2>/dev/null || true
}
trap cleanup EXIT INT TERM

# Осколки прошлого сеанса: если оставить сокет, следующий Xwayland успеет
# подключиться к мёртвому файлу раньше, чем композитор займёт новый.
rm -f "$RUNDIR/$WL" "$RUNDIR/$WL.lock"

# --- композитор: единственное, что работает через libhybris и наш EGL ---
env LD_LIBRARY_PATH=/usr/local/lib \
    LD_PRELOAD=/usr/local/lib/libbf7tlsguard.so \
    EGL_PLATFORM=mtkfb \
    QT_QPA_PLATFORM=eglfs \
    QT_QPA_EGLFS_INTEGRATION=none \
    QT_QPA_EGLFS_PHYSICAL_WIDTH=62 \
    QT_QPA_EGLFS_PHYSICAL_HEIGHT=139 \
    BF7_SCALE="$BF7_SCALE" BF7_ROTATION="$BF7_ROTATION" \
    /root/gui/bf7gui /root/gui/compositor.qml &
COMPOSITOR_PID=$!

# Проверяем не наличие файла, а то, что кто-то действительно принимает
# подключения: файл появляется раньше, чем композитор начинает слушать.
i=0
until python3 -c "import socket,sys
s=socket.socket(socket.AF_UNIX); s.settimeout(2)
s.connect(sys.argv[1]); s.close()" "$RUNDIR/$WL" 2>/dev/null; do
	i=$((i + 1))
	[ "$i" -gt 150 ] && { echo "композитор не принимает подключения" >&2; exit 1; }
	sleep 0.2
done
echo "композитор готов: $RUNDIR/$WL"

# --- Xwayland: единственный клиент Wayland, дальше всё живёт в X ---
# Без -rootless весь экран X — одно окно Wayland, которое композитор
# разворачивает на всю панель.
env WAYLAND_DISPLAY="$WL" Xwayland "$XDISP" -noreset &
XWAYLAND_PID=$!

export DISPLAY="$XDISP"
i=0
while ! xdpyinfo >/dev/null 2>&1; do
	i=$((i + 1))
	[ "$i" -gt 150 ] && { echo "Xwayland не поднялся" >&2; exit 1; }
	sleep 0.2
done
echo "X-сервер готов: $DISPLAY"

# --- рабочий стол: обычный сеанс X, без следов Wayland и libhybris ---
# WAYLAND_DISPLAY тут смертелен: GTK выбирает по нему бэкенд Wayland, а
# xfce4-session подставляет его значение в DISPLAY, и xfwm4 падает с
# "cannot open display: wayland-1".
unset WAYLAND_DISPLAY
unset LD_PRELOAD
unset EGL_PLATFORM
export GDK_BACKEND=x11
export QT_QPA_PLATFORM=xcb
export XDG_SESSION_TYPE=x11

# Своё композитирование xfwm4 тут только мешает: рисовать его нечем (GLX
# нет), а композитор у нас уже есть и он на GPU.
xfconf-query -c xfwm4 -p /general/use_compositing -s false --create -t bool >/dev/null 2>&1 || true

# Второй способ укрупнения, в отличие от BF7_SCALE — без растягивания, то есть
# чётко: X работает в родном разрешении, а крупнее становятся шрифты и
# элементы тулкитов. Работает только для того, что уважает DPI.
if [ -n "$BF7_DPI" ]; then
	printf "Xft.dpi: %s\n" "$BF7_DPI" | xrdb -merge 2>/dev/null || true
	xfconf-query -c xsettings -p /Xft/DPI -s "$BF7_DPI" --create -t int >/dev/null 2>&1 || true
	echo "Xft.dpi = $BF7_DPI"
fi

echo "панель $BF7_SCALE x, поворот $BF7_ROTATION°, клавиатура ${BF7_KEYBOARD:-нет}"

# Рабочий каталог сеанса — домашний, иначе терминал открывается там, откуда
# запускался композитор.
cd "$HOME"

exec startxfce4
