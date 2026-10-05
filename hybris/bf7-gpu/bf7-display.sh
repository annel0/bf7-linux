#!/bin/sh
# Масштаб и поворот панели BF7.
#
# И то и другое выполняет композитор на GPU: под Xwayland без -rootless
# размер экрана X задаёт вывод Wayland, поэтому настройки Xfce тут
# бессильны.
#
#   bf7-display              показать текущее
#   bf7-display 1.5          масштаб 1.5x (растягивание на GPU, мягче картинка)
#   bf7-display 1.5 90       масштаб и поворот
#   bf7-display - 270        только поворот
#   bf7-display 1 0 144      без растягивания, крупнее за счёт DPI (чётко)
#
# BF7_SCALE растягивает весь экран X силами GPU: действует на всё, включая
# декорации окон, но картинка становится мягче. BF7_DPI оставляет родное
# разрешение и укрупняет шрифты и элементы — чётко, но слушаются не все
# приложения. Их можно сочетать.
set -e
CONF=/etc/default/bf7-display

BF7_SCALE=1.0
BF7_ROTATION=0
BF7_KEYBOARD=onboard
BF7_DPI=
[ -r "$CONF" ] && . "$CONF"

if [ $# -eq 0 ]; then
	echo "масштаб $BF7_SCALE, поворот $BF7_ROTATION°, DPI ${BF7_DPI:-по умолчанию}, клавиатура ${BF7_KEYBOARD:-нет}"
	exit 0
fi

[ "$1" != "-" ] && BF7_SCALE=$1
[ -n "$2" ] && [ "$2" != "-" ] && BF7_ROTATION=$2
[ -n "$3" ] && { [ "$3" = "-" ] && BF7_DPI= || BF7_DPI=$3; }

case "$BF7_ROTATION" in
0|90|180|270) ;;
*) echo "поворот бывает 0, 90, 180 или 270" >&2; exit 1 ;;
esac

cat > "$CONF" <<EOC
# Меняется утилитой bf7-display.
BF7_SCALE=$BF7_SCALE
BF7_ROTATION=$BF7_ROTATION
BF7_KEYBOARD=$BF7_KEYBOARD
BF7_DPI=$BF7_DPI
EOC

echo "ставлю масштаб $BF7_SCALE, поворот $BF7_ROTATION°, DPI ${BF7_DPI:-по умолчанию}; перезапускаю сеанс"
systemctl restart bf7-session.service
