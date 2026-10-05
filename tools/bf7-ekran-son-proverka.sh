#!/bin/sh
# BF7: проверка сна тракта экрана (параметр bf7_drm usyplyat_panel).
#
# Первое испытание (tools/bf7-posle-probuzhdeniya.sh, шаг 3) дало ложный
# провал по двум причинам:
#   - экран к тому времени уже погас по простою, и `wlr-randr --off` ничего
#     не выключал — конвейер уже стоял;
#   - из журнала бралась только последняя строка, а тракт уснул и через
#     0,3 с проснулся, так что «усыплён» прятался за «разбужен».
# Здесь гасим тем же путём, что и сама система по простою: gsd-power
# ставит PowerSaveMode у org.gnome.Mutter.DisplayConfig, Phosh передаёт это
# композитору, тот выключает конвейер. И считаем строки журнала с момента
# начала, а не последнюю.
#
# Что проверяем:
#   1. при гашении тракт засыпает и держатель pri_disp_wakelock отпущен;
#   2. за десять секунд никто не будит его обратно;
#   3. при включении тракт просыпается и снимок экрана не пустой;
#   4. тачскрин после этого снова в рабочей прошивке — у TD4160 касание и
#      дисплей одна микросхема, и сон панели перезапускает контроллер
#      касаний;
#   5. о гашении узнали слушатели оповещений fb (вендорские правила «экран
#      погашен» и сам тачскрин): сторож касаний во сне молчит.
#
# Параметр по итогам оставляется таким, каким был до проверки, — решение о
# включении принимается отдельно.
#
# Вызов: tools/bf7-ekran-son-proverka.sh
set -eu

telefon=${BF7_TELEFON:-bf7}

ssh "$telefon" 'sh -s' <<'NA_TELEFONE'
set -u
export XDG_RUNTIME_DIR=/run/bf7-phosh WAYLAND_DISPLAY=wayland-0
param=/sys/module/bf7_drm/parameters/usyplyat_panel
tach=/sys/devices/platform/11012000.spi2/spi_master/spi2/spi2.0/synaptics_tcm.0/synaptics_tcm/info
[ -w "$param" ] || { echo "нет параметра $param — ядро старое"; exit 1; }

obol=""
for q in $(ls /proc | grep -E "^[0-9]+$"); do
	case "$(readlink /proc/$q/exe 2>/dev/null)" in */phosh) obol=$q; break;; esac
done
[ -n "$obol" ] || { echo "оболочка не запущена"; exit 1; }
export DBUS_SESSION_BUS_ADDRESS=$(tr "\0" "\n" < /proc/$obol/environ | sed -n "s/^DBUS_SESSION_BUS_ADDRESS=//p")

rezhim() {
	busctl --user set-property org.gnome.Mutter.DisplayConfig /org/gnome/Mutter/DisplayConfig \
		org.gnome.Mutter.DisplayConfig PowerSaveMode i "$1"
}
derzhatel() {
	awk '$1 == "pri_disp_wakelock" { print $6 }' /sys/kernel/debug/wakeup_sources
}
novye() {
	dmesg | tail -n +"$((n0 + 1))" | grep -E "тракт экрана" | sed "s/.*bf7: //" | tr "\n" "|"
}
zhdat_stroku() {
	i=0
	while [ "$i" -lt "$2" ] && ! novye | grep -q "$1"; do sleep 1; i=$((i + 1)); done
}

bylo=$(cat "$param")
proval=0

# Экран должен гореть до начала — иначе гасить нечего.
rezhim 0; sleep 1
/usr/local/bin/bf7-otperet >/dev/null 2>&1; sleep 3
echo 1 > "$param"
n0=$(dmesg | wc -l)

rezhim 3
zhdat_stroku "усыплён" 10
d=$(derzhatel)
echo "1. после гашения: $(novye)  держатель экрана активен с: $d"
novye | grep -q "усыплён" || { echo "   ПРОВАЛ: тракт не уснул"; proval=1; }
[ "$d" = 0 ] || { echo "   ПРОВАЛ: pri_disp_wakelock всё ещё держится"; proval=1; }

sleep 10
echo "2. через 10 с: $(novye)"
novye | grep -q "разбужен" && { echo "   ПРОВАЛ: кто-то разбудил тракт без нас"; proval=1; }
# Оповещение о гашении дошло до слушателей: вендорский диспетчер памяти
# пишет «SCREEN OFF», а сторож тачскрина молчит (иначе он раз в секунду
# читал бы спящую микросхему).
sosed=$(dmesg | tail -n +"$((n0 + 1))")
echo "   оповещение «экран погашен»: $(echo "$sosed" | grep -c "SCREEN OFF"), ошибок сторожа касаний во сне: $(echo "$sosed" | grep -c "watchdog_work.*Failed")"
echo "$sosed" | grep -q "SCREEN OFF" || { echo "   ПРОВАЛ: слушатели гашения не оповещены"; proval=1; }
echo "$sosed" | grep -q "watchdog_work.*Failed" && { echo "   ПРОВАЛ: сторож касаний читает спящую микросхему"; proval=1; }

rezhim 0
zhdat_stroku "разбужен" 10
/usr/local/bin/bf7-otperet >/dev/null 2>&1; sleep 3
echo "3. после включения: $(novye)"
novye | grep -q "разбужен" || { echo "   ПРОВАЛ: тракт не проснулся"; proval=1; }
if grim /tmp/bf7-ekran-son.png 2>/dev/null; then
	razmer=$(stat -c %s /tmp/bf7-ekran-son.png)
	echo "   снимок экрана: $razmer байт"
else
	echo "   ПРОВАЛ: снимок не снят"; proval=1
fi

sleep 2
rezhim_tacha=$(sed -n "s/^Firmware mode: *//p" "$tach")
echo "4. тачскрин: $rezhim_tacha, ошибок контроллера в журнале: $(dmesg | tail -n +"$((n0 + 1))" | grep -c "synaptics.*Failed")"
[ "$rezhim_tacha" = "Host Download" ] || { echo "   ПРОВАЛ: прошивка касаний не запущена"; proval=1; }

echo "$bylo" | sed "s/Y/1/;s/N/0/" > "$param"
echo "параметр возвращён: $(cat "$param")"
[ "$proval" = 0 ] && echo "ИТОГ: сон тракта экрана работает" || echo "ИТОГ: есть провалы"
exit "$proval"
NA_TELEFONE
