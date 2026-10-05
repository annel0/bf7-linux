#!/bin/sh
# BF7: разбудить и разблокировать экран — ровно настолько, насколько нужно.
#
# Прежний вариант вслепую давил одну и ту же череду: касание, свайп вверх,
# четыре цифры кода, «Unlock». На экране блокировки это верно. Но если экран
# уже разблокирован, та же череда ложится на сетку приложений: свайп
# сворачивает её (виден голый фон Phosh, синий #023c88 — это стандартный
# цвет фона GNOME, а не чьё-то окно), а касания по «цифрам» попадают в
# значки и запускают программы. 2026-09-23 это выглядело как загадочное
# синее окно во весь экран.
#
# Теперь сперва спрашиваем оболочку: заблокирован ли экран
# (org.gnome.ScreenSaver.GetActive на шине её сеанса).
#
#   заблокирован        — полная череда, как раньше;
#   открыт, но погашен  — одно касание в пустое место, чтобы разбудить;
#   открыт и светится   — ничего не делаем.
#
# Всё одним вызовом bf7-touchinj: каждый новый узел uinput композитор
# принимает не сразу, и набранные цифры терялись.
set -u

export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/run/bf7-phosh}
export WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-0}

# Шина сеанса оболочки — у нас своя, от dbus-run-session.
obol=""
for q in $(ls /proc | grep -E '^[0-9]+$'); do
	case "$(readlink /proc/$q/exe 2>/dev/null)" in
		*/phosh) obol=$q; break;;
	esac
done
if [ -n "$obol" ]; then
	shina=$(tr '\0' '\n' < /proc/$obol/environ | sed -n 's/^DBUS_SESSION_BUS_ADDRESS=//p')
	[ -n "$shina" ] && export DBUS_SESSION_BUS_ADDRESS="$shina"
fi

zablokirovan() {
	# Не удалось спросить — считаем заблокированным: полная череда на
	# экране блокировки безвредна, а недоразблокировать хуже.
	otvet=$(busctl --user call org.gnome.ScreenSaver /org/gnome/ScreenSaver \
		org.gnome.ScreenSaver GetActive 2>/dev/null) || return 0
	case "$otvet" in
		*false*) return 1;;
		*) return 0;;
	esac
}

pogashen() {
	# По разъёму DRM, а не по подсветке: при спящем тракте экрана файл
	# lcd-backlight/brightness может показывать прежнюю яркость (класс
	# backlight пропускает первое гашение после загрузки), хотя панель тёмная.
	[ "$(cat /sys/class/drm/card1-DSI-1/enabled 2>/dev/null)" = disabled ]
}

if zablokirovan; then
	printf "360 800\nсвайп 360 1450 360 500 30\n188 688\n358 688\n530 688\n188 860\n358 1388\n" |
		bf7-touchinj поток >/dev/null 2>&1
	echo "bf7-otperet: был заблокирован — разблокирован"
elif pogashen; then
	# Пустое место под сеткой приложений: касание там будит экран и ничего
	# не запускает.
	printf "360 1450\n" | bf7-touchinj поток >/dev/null 2>&1
	echo "bf7-otperet: был открыт, но погашен — разбужен"
else
	echo "bf7-otperet: уже открыт и светится — ничего не делаю"
fi
