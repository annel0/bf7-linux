#!/bin/sh
# BF7: весь план после пробуждения телефона — одним заходом, с откатами.
#
# Ночью 2026-09-23 телефон уснул в первой пробе сна, и всё подготовленное
# за время его сна ждало одного нажатия кнопки. Этот сценарий делает по
# порядку то, что иначе делалось бы руками, и на каждом рискованном шаге
# сам откатывается при неудаче:
#
#   1. дождаться телефона (с пределом);
#   2. прошить ядро и модули (tools/bf7-proshit.sh, со сверкой CRC);
#   3. испытать гашение тракта экрана (tools/bf7-ekran-son-proverka.sh;
#      параметр usyplyat_panel оставляет как был);
#   4. показать режим энергосбережения Wi-Fi;
#   5. собрать на телефоне и испытать сон матрицы камеры
#      (tools/bf7-kamera-son-proverka.sh); неудача — прежний демон обратно;
#   6. замер «кто будит» по точкам трассировки (tools/bf7-kto-budit.sh).
#
# Пробу сна (bf7-son-proba) сюда НЕ включаю намеренно: её неудача стоит
# физического нажатия кнопки, и решать её надо отдельно, глядя на итоги
# шагов 3 и 6.
#
# Вызов: tools/bf7-posle-probuzhdeniya.sh [--bez-proshivki]
set -u

KORNI=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TELEFON=${BF7_TELEFON:-bf7}
IP_USB=${BF7_IP_USB:-192.168.7.2}

VREM=$(mktemp -d)
trap 'rm -rf "$VREM"' EXIT

skazat() { printf '\n##### %s\n' "$*"; }
na_telefone() { ssh -o ConnectTimeout=10 "$TELEFON" "$@"; }

skazat "1. жду телефон"
i=0
until ping -c 1 -W 2 "$IP_USB" >/dev/null 2>&1; do
	i=$((i + 1))
	[ "$i" -ge 120 ] && { echo "телефона нет десять минут — нужна кнопка питания"; exit 1; }
	sleep 3
done
echo "на связи; аптайм $(na_telefone 'cut -d. -f1 /proc/uptime') с"

if [ "${1:-}" != "--bez-proshivki" ]; then
	skazat "2. прошивка"
	"$KORNI/tools/bf7-proshit.sh" || { echo "прошивка не удалась — дальше не иду"; exit 1; }
fi

skazat "3. гашение тракта экрана"
# Прежняя проверка здесь (wlr-randr --off и последняя строка журнала) дала
# ложный провал: экран уже погас по простою, и гасить было нечего, а
# «усыплён» прятался за «разбужен». Теперь — отдельный сценарий, который
# гасит тем же путём, что и система по простою.
"$KORNI/tools/bf7-ekran-son-proverka.sh" ||
	echo "ИТОГ: сон тракта экрана с провалами — смотреть вывод выше"

skazat "4. энергосбережение Wi-Fi"
na_telefone 'iw dev wlan0 get power_save 2>&1; nmcli -t -f NAME,DEVICE connection show --active 2>/dev/null | grep wlan0 |
	while IFS=: read -r imya ustr; do echo "соединение $imya: wifi.powersave=$(nmcli -g 802-11-wireless.powersave connection show "$imya")"; done'

skazat "5. сон матрицы камеры"
scp -q "$KORNI/hybris/bf7-camera/bf7-camd.c" "$TELEFON:/tmp/"
if na_telefone 'cd /tmp && rm -f bf7-camd.son && gcc -O2 -o bf7-camd.son bf7-camd.c -lm -lpthread 2>&1 | grep -E "error" ; [ -x bf7-camd.son ]'; then
	na_telefone 'cp -a /root/camera/bf7-camd /root/camera/bf7-camd.do-sna; install -m755 /tmp/bf7-camd.son /root/camera/bf7-camd'
	if "$KORNI/tools/bf7-kamera-son-proverka.sh"; then
		echo "ИТОГ: сон матрицы работает — новый демон оставлен"
	else
		na_telefone 'install -m755 /root/camera/bf7-camd.do-sna /root/camera/bf7-camd; systemctl restart bf7-camd'
		echo "ИТОГ: не вышло — прежний демон возвращён"
	fi
else
	echo "демон не собрался — пропускаю"
fi

skazat "6. кто будит процессор"
"$KORNI/tools/bf7-kto-budit.sh" 20
