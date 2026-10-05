#!/bin/bash
# BF7: самопроверка телефона.
#
# Проверяет разом всё, что мы подняли: слот и ядро, целостность ключевых
# файлов, службы, графику, звук, сеть, камеру, ввод. Выводит короткий отчёт с
# отметками. Задуман для запуска после каждой перезагрузки и после любой
# починки файловой системы — в ночь на 2026-09-22 четыре молча пропавших
# мелочи стоили часа поисков по симптомам.
#
# Имена переменных только латиницей: /bin/sh кириллические не принимает.
set -u

ok=0; bad=0
say() { printf '  %s %s\n' "$1" "$2"; }
good() { say '[+]' "$1"; ok=$((ok+1)); }
fail() { say '[-]' "$1"; bad=$((bad+1)); }
note() { say '   ' "$1"; }

echo "=== Основание ==="
kern=$(uname -r)
case "$kern" in
	*bf7-x657b*) good "ядро наше: $kern";;
	*) fail "ядро ЧУЖОЕ: $kern — проверь слот (fastboot set_active a)";;
esac
slot=$(tr ' ' '\n' < /proc/cmdline | sed -n 's/^androidboot.slot=//p')
[ "$slot" = a ] && good "слот a" || fail "слот $slot — должен быть a"
root_opts=$(findmnt -n -o OPTIONS / 2>/dev/null)
case "$root_opts" in
	rw,*) good "корень доступен на запись";;
	*) fail "корень только для чтения ($root_opts) — нужна проверка диска";;
esac
cma=$(awk '/CmaTotal/{print $2}' /proc/meminfo)
[ "${cma:-0}" -ge 100000 ] && good "непрерывная память: $cma кБ" || fail "мало CMA: $cma кБ"

# Корень должен работать С журналом ext4: до 2026-09-23 ramdisk монтировал его
# «ro,noload», noload переживал перемонтирование на запись, и любое нечистое
# выключение портило ФС.
case "$(findmnt -no OPTIONS /)" in
	*norecovery*|*noload*) fail "корень без журнала ext4 ($(findmnt -no OPTIONS /)) — ramdisk init?";;
	*) good "корень с журналом ext4";;
esac

echo "=== Ключевые файлы ==="
for f in /usr/share/glvnd/egl_vendor.d/10_libhybris.json \
         /etc/systemd/system/bf7-android-hal.service \
         /etc/modprobe.d/bf7-camera-modprobe.conf \
         /etc/asound.conf /etc/pulse/bf7.pa \
         /root/camera/bf7-camd /root/camera/bf7-seninf-up \
         /usr/local/lib/libbf7eglfilter.so /usr/local/lib/libbf7wlegl.so; do
	if [ -s "$f" ]; then
		sz=$(stat -c %s "$f")
		# Обрезанные файлы выглядят целыми в ls: проверяем и размер.
		if [ "$sz" -lt 60 ]; then
			fail "подозрительно мал ($sz байт): $f"
		else
			ok=$((ok+1))
		fi
	else
		fail "нет или пуст: $f"
	fi
done
[ "$bad" -eq 0 ] && good "все ключевые файлы на месте"

echo "=== Службы ==="
for s in bf7-android-hal bf7-audio bf7-pulse bf7-vibrad \
         bf7-camera bf7-camd NetworkManager bf7-phosh bf7-budilnik bf7-dosyp \
         bf7-thermalloadalgod; do
	st=$(systemctl is-active "$s" 2>/dev/null)
	[ "$st" = active ] && good "$s" || fail "$s: $st"
done
# Заводская политика температуры (bf7-thermal): без неё у платы нет порогов
# ограничения заряда и выключения, а процессор опрашивается каждые 40 мс.
case "$(sed -n 's/^interval=//p' /proc/driver/thermal/tzcpu 2>/dev/null)" in
	200) good "политика температуры MediaTek применена (процессор опрашивается раз в 200 мс)";;
	*) fail "политика температуры не применена — bf7-thermal.service?";;
esac
# Будильник зажигает экран через bf7-knopka (собирается из bf7-knopka.c).
[ -x /usr/local/bin/bf7-knopka ] && good "bf7-knopka есть (будильник зажигает им экран)" ||
	fail "нет /usr/local/bin/bf7-knopka — будильник не зажжёт экран"
# Экран при выходе из сна не должен загораться сам: это держат наш пакет
# phosh и наш gsd-power (hybris/bf7-patches). Обновление пакета молча
# вернуло бы вспышки экрана в кармане при каждом пробуждении.
case "$(dpkg-query -W -f='${Version}' phosh 2>/dev/null)" in
	*bf7*) good "phosh наш ($(dpkg-query -W -f='${Version}' phosh)) — экран при пробуждении не зажигает";;
	*) fail "phosh пакетный — экран будет вспыхивать при каждом пробуждении";;
esac
# dpkg -i своего пакета снимает закрепление — первое же apt upgrade вернуло бы
# пакетный phosh. И после установки сеанс надо перезапустить: запущенная
# оболочка со старым файлом видна как «(deleted)» у /proc/PID/exe.
apt-mark showhold 2>/dev/null | grep -qx phosh && good "phosh закреплён (hold)" ||
	fail "phosh не закреплён — apt upgrade заменит его пакетным (apt-mark hold phosh)"
p=$(pgrep -f '^/usr/libexec/phosh' | head -1)
if [ -n "$p" ] && readlink "/proc/$p/exe" | grep -q deleted; then
	fail "оболочка запущена со старого файла — после установки phosh не перезапущен сеанс"
fi
# Вход без PIN (заплатка phosh-0.24-lock-enabled): состояние — для сведения.
case "$(gsettings get org.gnome.desktop.screensaver lock-enabled 2>/dev/null)" in
	false) good "экран блокировки открывается свайпом, без PIN (lock-enabled false)";;
	true) good "экран блокировки просит PIN (lock-enabled true)";;
esac
# Ночная тишина (bf7-tishina): беззвучный профиль лежит в dconf и переживает
# перезагрузку — без таймера возврата звонок остался бы беззвучным навсегда.
if [ -n "${p:-}" ]; then
	prof=$(tr '\0' '\n' < /proc/$p/environ | grep -E '^DBUS_SESSION_BUS_ADDRESS=' |
		{ read -r a && env "$a" gsettings get org.sigxcpu.feedbackd profile; } 2>/dev/null)
	case "$prof" in
	"'full'") good "звук уведомлений и звонка включён (feedbackd full)" ;;
	"'quiet'"|"'silent'")
		if systemctl is-enabled -q bf7-tishina-utro.timer 2>/dev/null; then
			good "ночная тишина до $(systemctl show bf7-tishina-utro.timer -p NextElapseUSecRealtime --value)"
		else
			fail "звонок беззвучный ($prof), а таймера возврата нет — bf7-tishina vykl"
		fi ;;
	esac
fi
# Удержание экрана (phoc-0.24-idle-inhibit.patch): без протокола экран гаснет
# посреди игры или фильма в RetroArch и mpv.
if XDG_RUNTIME_DIR=/run/bf7-phosh WAYLAND_DISPLAY=wayland-0 wayland-info 2>/dev/null |
	grep -q zwp_idle_inhibit_manager_v1; then
	good "композитор даёт программам держать экран (idle-inhibit)"
else
	fail "нет zwp_idle_inhibit_manager_v1 — экран будет гаснуть в играх и видео (phoc без заплатки?)"
fi
# RetroArch собран с Vulkan (retroarch-1.14-vulkan.patch) — пакетный без него.
case "$(dpkg-query -W -f='${Version}' retroarch 2>/dev/null)" in
	"") ;;
	*bf7*) apt-mark showhold 2>/dev/null | grep -qx retroarch &&
		good "retroarch наш, с Vulkan, закреплён" ||
		fail "retroarch наш, но не закреплён (apt-mark hold retroarch)";;
	*) fail "retroarch пакетный — без драйвера Vulkan";;
esac
dpkg-divert --list /usr/libexec/gsd-power 2>/dev/null | grep -q gsd-power.debian &&
	good "gsd-power наш (пакетный отведён в gsd-power.debian)" ||
	fail "gsd-power пакетный — экран будет вспыхивать при каждом пробуждении"
# На tty1 живёт phoc. Вход в систему там сбрасывал терминал в текстовый режим,
# и нажатия клавиш будили экран в обход композитора (см. bf7-getty-tty1.conf).
if pgrep -t tty1 -x agetty >/dev/null 2>&1; then
	fail "на tty1 запущен вход в систему — нет вставки getty@tty1.service.d/bf7-phosh.conf?"
else
	good "tty1 отдан телефонному сеансу"
fi

echo "=== Графика ==="
if [ -e /dev/dri/card1 ] && [ -e /dev/dri/renderD128 ]; then
	good "устройства DRM на месте"
else
	fail "нет /dev/dri/card1 или renderD128"
fi
st=$(grep -A2 '^crtc' /sys/kernel/debug/dri/1/state 2>/dev/null | grep -c 'active=1')
[ "${st:-0}" -ge 1 ] && good "панель включена" || note "панель погашена (это норма, если экран спит)"

# Композитор должен быть с заплаткой про пробуждение касанием. Без неё phoc
# отбрасывает нажатие при погашенном экране раньше, чем сбрасывает простой, и
# телефон перестаёт будиться пальцем — снаружи это выглядит как «экран
# блокировки глотает касания». Заплатка в hybris/bf7-patches/, пакет собран на
# телефоне и закреплён через apt-mark hold.
phoc_ver=$(dpkg-query -W -f='${Version}' phoc 2>/dev/null)
case "$phoc_ver" in
*+bf7*) good "phoc с заплаткой пробуждения касанием ($phoc_ver)" ;;
"")     fail "phoc не установлен" ;;
*)      fail "phoc $phoc_ver без заплатки — касание не разбудит экран" ;;
esac

# Аппаратное видео: драйвер VA-API ходит к демону bf7-videod через сокет. Без
# демона «Failed to create decode context» — 2026-09-23 служба оказалась
# выключенной (её не было в списке включения установщика).
if systemctl is-active -q bf7-videod && [ -S /run/bf7-video.sock ]; then
	good "аппаратное видео: bf7-videod работает"
else
	fail "bf7-videod не работает — аппаратного декодирования нет"
fi

chastota=$(grep -o 'refresh: [0-9.]*' /dev/null 2>/dev/null)
chastota=$(XDG_RUNTIME_DIR=/run/bf7-phosh WAYLAND_DISPLAY=wayland-0 wayland-info 2>/dev/null |
	grep -o 'refresh: [0-9.]* Hz' | head -1 | grep -o '[0-9]*' | head -1)
case "${chastota:-0}" in
	5[0-9]|6[0-9]|7[0-9]|1[02][0-9]) good "экран сообщает ${chastota} Гц";;
	*) fail "экран сообщает ${chastota:-нисколько} Гц — GTK будет рисовать раз в тысячу секунд";;
esac

echo "=== Яркость ==="
if [ -e /sys/class/backlight/bf7-backlight/brightness ]; then
	good "устройство подсветки есть ($(cat /sys/class/backlight/bf7-backlight/brightness)/$(cat /sys/class/backlight/bf7-backlight/max_brightness))"
	if grep -q '\[bf7-backlight\]' /sys/class/leds/lcd-backlight/trigger 2>/dev/null; then
		good "светодиод привязан к подсветке"
	else
		fail "светодиод не привязан — яркость меняться не будет"
	fi
else
	fail "нет /sys/class/backlight/bf7-backlight — не загружены модули подсветки"
fi

echo "=== Звук ==="
amp=$(amixer -c 0 cget name=Audio_Amp_L_Switch 2>/dev/null | tail -1 | tr -dc '0-9')
[ "${amp:-0}" = 1 ] && good "усилитель ЦАП включён" || fail "усилитель ЦАП выключен — перезапусти bf7-audio"
gpio=$(grep -E '^151:' /sys/devices/platform/pinctrl@10005000/mt_gpio 2>/dev/null | awk '{print $2}')
case "$gpio" in 01*) good "вывод внешнего усилителя удерживается";; *) fail "вывод внешнего усилителя отпущен ($gpio)";; esac
PULSE_SERVER=unix:/run/bf7-pulse/native pactl list short sinks >/dev/null 2>&1 &&
	good "звуковой сервер отвечает" || fail "звуковой сервер молчит"
grep -q '^avoid-resampling = yes' /etc/pulse/daemon.conf.d/bf7.conf 2>/dev/null &&
	good "музыка 44,1 кГц идёт без пересчёта частоты" ||
	fail "avoid-resampling выключен — пересчёт 44,1→48 кГц ест ~5% ядра"
for t in bf7-rtc-sync.timer bf7-gps-epo.timer; do
	[ -f /etc/systemd/system/$t ] || continue
	systemctl is-active -q $t && good "таймер $t идёт" || fail "таймер $t не запущен"
done
# RTC не должен уходить от системного времени дальше чем на минуту
if [ "$(timedatectl show -p NTPSynchronized --value)" = yes ]; then
	rtc=$(date -d "$(hwclock -r --utc 2>/dev/null)" +%s 2>/dev/null)
	[ -n "$rtc" ] && [ $(( $(date +%s) - rtc )) -lt 60 ] && [ $(( rtc - $(date +%s) )) -lt 60 ] &&
		good "аппаратные часы сходятся с NTP" || fail "RTC ушёл от системного времени — bf7-rtc-sync"
fi
# Правила udev: ошибка в одном (например «$d» вместо «$$d» в RUN) — и udev
# молча выбрасывает его целиком. udevadm test разбирает все правила.
plohie=$(udevadm test --action=change /sys/class/power_supply/battery 2>&1 | grep -iE "invalid|error" | head -2)
[ -z "$plohie" ] && good "правила udev разбираются без ошибок" ||
	fail "udev отверг правило: $plohie"
if systemctl is-enabled -q bf7-gps 2>/dev/null; then
	grep -q '^gps_drv ' /proc/modules && [ -c /dev/stpgps ] &&
		good "GPS: модуль gps_drv и /dev/stpgps на месте" ||
		fail "GPS: нет gps_drv — kernel-work/bf7-gps/build.sh под текущее ядро"
	systemctl is-active -q bf7-gps && systemctl is-active -q bf7-gps-nmea &&
		good "GPS: mnld и раздатчик NMEA работают" ||
		fail "GPS: bf7-gps или bf7-gps-nmea не запущены"
fi
if pgrep -x phoc >/dev/null; then
	pgrep -f '[b]f7-nochnoj-svet$' >/dev/null &&
		good "служба ночного света живёт в сеансе" ||
		fail "нет bf7-nochnoj-svet — ночной свет не дойдёт до экрана (запускается из bf7-phosh-session)"
fi
PULSE_SERVER=unix:/run/bf7-pulse/native pactl list short modules 2>/dev/null | grep -q bluez5-discover &&
	good "звук в Bluetooth-наушники (bluez5-discover загружен)" ||
	fail "нет module-bluez5-discover — пакет pulseaudio-module-bluetooth и строки в /etc/pulse/bf7.pa"
grep -q ' bf7_copy_from_user_io$' /proc/kallsyms 2>/dev/null &&
	good "защита от паники на выравнивании в ядре" ||
	fail "в ядре нет bf7_copy_from_user_io — звук способен уронить систему"

# Звонок и будильник у feedbackd — FF_PERIODIC с FF_SINE; без объявленной
# формы волны ядро отвергает эффект, и они молча не вибрируют.
vib=""
for d in /sys/class/input/event*; do
	[ "$(cat $d/device/name 2>/dev/null)" = bf7-vibrator ] && vib=$d
done
if [ -n "$vib" ] && python3 -c "
import sys
w=open('$vib/device/capabilities/ff').read().split()
n=0
for x in w: n=(n<<64)|int(x,16)
sys.exit(0 if (n>>0x5a)&1 and (n>>0x51)&1 else 1)" 2>/dev/null; then
	good "вибро: звонок и будильник принимаются (FF_PERIODIC + FF_SINE)"
else
	fail "вибро без FF_PERIODIC/FF_SINE — звонок и будильник не вибрируют (пересобери bf7-vibrad)"
fi

# Огонёк уведомлений включён наоборот — без инверсии он горит постоянно.
[ "$(cat /sys/module/leds_mt6357/parameters/bf7_invert_isink1 2>/dev/null)" = Y ] &&
	good "огонёк уведомлений: инверсия включена (в покое тёмный)" ||
	fail "огонёк уведомлений без инверсии — горит постоянно (tmpfiles/bf7-svetodiod.conf, ядро v9)"

echo "=== Сейсмограф ==="
# Пишет раз в секунду и сбрасывает на диск раз в 10 с; свежей строки старше
# минуты быть не должно (если телефон не спал только что).
# Сейсмограмма пишется только на зарядке; на батарее датчик на 10 Гц ради
# автоповорота (0x27), при погашенном экране выключен (0x07).
sf=/var/lib/bf7-seismograf/$(date +%F).csv
if systemctl is-active -q bf7-seismograf; then
	if [ "$(cat /sys/class/power_supply/usb/online 2>/dev/null)" = 1 ] ||
	   [ "$(cat /sys/class/power_supply/ac/online 2>/dev/null)" = 1 ]; then
		zhdu=0x47
		last=$(tail -1 "$sf" 2>/dev/null | cut -d, -f1)
		if [ -n "$last" ] && [ $(( $(date +%s) - last )) -lt 60 ]; then
			good "сейсмограф пишет ($(wc -l < "$sf") с за сегодня)"
		else
			fail "сейсмограф запущен, но свежих записей нет — journalctl -u bf7-seismograf"
		fi
	elif [ "$(cat /sys/class/drm/card1-DSI-1/enabled 2>/dev/null)" = disabled ]; then
		zhdu=0x07
	else
		zhdu=0x27
	fi
	sleep 2		# режим меняется не чаще раза в 2 с
	reg=$(i2cget -y -f 1 0x18 0x20 2>/dev/null)
	if [ "$reg" = "$zhdu" ]; then
		good "акселерометр в своём режиме по питанию ($reg)"
	else
		fail "акселерометр: CTRL_REG1=$reg, ждал $zhdu"
	fi
else
	fail "сейсмограф не запущен — systemctl start bf7-seismograf"
fi

# Автоповорот: сейсмограф пишет ориентацию, bf7-sensor-proxy отдаёт её phosh.
if busctl --system get-property net.hadess.SensorProxy /net/hadess/SensorProxy \
	net.hadess.SensorProxy HasAccelerometer 2>/dev/null | grep -q true; then
	good "автоповорот: ориентация $(cat /run/bf7-akselerometr/orientaciya 2>/dev/null) для оболочки"
else
	fail "нет net.hadess.SensorProxy — автоповорот не работает (bf7-sensor-proxy)"
fi
# Свет и приближение (mn78911): приближение гасит экран у уха во время звонка.
if busctl --system get-property net.hadess.SensorProxy /net/hadess/SensorProxy \
	net.hadess.SensorProxy HasProximity 2>/dev/null | grep -q true &&
   [ "$(i2cget -y 1 0x41 0x06 2>/dev/null)" = 0x88 ]; then
	good "датчик света и приближения mn78911 отвечает"
else
	fail "нет датчика приближения — экран не будет гаснуть у уха (bf7-sensor-proxy, i2c-1:0x41)"
fi

echo "=== Питание ==="
cap=$(cat /sys/class/power_supply/battery/capacity 2>/dev/null)
if [ -n "$cap" ] && [ "$cap" -ge 0 ] 2>/dev/null; then
	good "батарея: ${cap}% ($(cat /sys/class/power_supply/battery/status 2>/dev/null))"
else
	fail "заряд не читается (${cap:-нет}) — не запущен bf7-fuelgauge"
fi

echo "=== Сеть ==="
w=$(nmcli -t -f DEVICE,STATE,CONNECTION device 2>/dev/null | grep '^wlan0:' | cut -d: -f2)
[ "$w" = connected ] && good "Wi-Fi подключён" || fail "Wi-Fi: ${w:-нет}"
ip -4 -br addr show rndis0 2>/dev/null | grep -q UP && good "канал с ПК жив" || note "канал с ПК не поднят"

echo "=== Связь ==="
# Модем MediaTek (см. hybris/bf7-modem): готов, ModemManager его видит,
# прерывание модема будит сон, звук разговора отдаёт bf7-zvonki.
case "$(cut -d" " -f1 /sys/kernel/ccci/boot 2>/dev/null)" in
	md1:4) good "модем запущен (md1:4)";;
	*) fail "модем не готов: $(cut -d' ' -f1 /sys/kernel/ccci/boot 2>/dev/null) — bf7-modem.service?";;
esac
mm=$(mmcli -m any 2>/dev/null)
if [ -z "$mm" ]; then
	fail "ModemManager модема не видит (мост bf7-modem-at, bf7-modem-sim?)"
else
	st=$(echo "$mm" | sed -n 's/.*  state: //p' | head -1 | sed 's/\x1b\[[0-9;]*m//g')
	case "$st" in
		*registered*|*connected*) good "в сети: $st, $(echo "$mm" | sed -n 's/.*access tech: //p' | head -1)";;
		*) fail "модем в состоянии «$st»";;
	esac
fi
[ "$(cat /sys/kernel/irq/37/wakeup 2>/dev/null)" = enabled ] &&
	good "прерывание модема будит сон (входящие дойдут до спящего)" ||
	fail "прерывание модема (irq 37) не будит — ядро без SKIP_SET_WAKE?"
[ -f /usr/local/share/dbus-1/services/org.mobian_project.CallAudio.service ] &&
	good "звук разговора: bf7-zvonki вместо callaudiod" ||
	fail "нет замены callaudiod — разговор будет без звука"
# Наш MM: данные через +CGACT/ccmni0 и входящие без +CLCC (hybris/bf7-patches).
# Пакетный MM без заплаток хоронит входящий через 2 с и умеет данные только PPP.
mmver=$(dpkg-query -W -f='${Version} ${Status}' modemmanager 2>/dev/null)
case "$mmver" in
	*+bf7*hold*) good "ModemManager с заплатками MediaTek (${mmver%% *}, закреплён)";;
	*+bf7*) fail "ModemManager ${mmver%% *} не закреплён — apt заменит (apt-mark hold)";;
	*) fail "ModemManager без заплаток MediaTek (${mmver%% *}) — входящие и данные сломаны";;
esac
# Голос в разговор (bf7-skazat): Festival с русским голосом, запасной espeak-ng.
if [ -x /usr/local/bin/bf7-skazat ] && [ -d /usr/share/festival/voices/russian ]; then
	good "голос в разговор: bf7-skazat, Festival (festvox-ru)"
elif [ -x /usr/local/bin/bf7-skazat ] && command -v espeak-ng >/dev/null 2>&1; then
	note "голос в разговор только espeak-ng — поставить festival festvox-ru"
else
	fail "нет bf7-skazat или синтезатора (festival festvox-ru / espeak-ng)"
fi
if udevadm info /sys/class/tty/ttyp0 2>/dev/null | grep -q ID_MM_BF7_MTK_NO_CLCC=1 &&
   udevadm info /sys/class/net/ccmni0 2>/dev/null | grep -q ID_MM_BF7_MTK_DATA=1; then
	good "метки udev для MM на ttyp0 и ccmni0"
else
	fail "нет меток ID_MM_BF7_MTK_* (77-bf7-modem.rules, udevadm trigger)"
fi

echo "=== Камера ==="
[ -e /dev/video0 ] && good "/dev/video0 есть" || fail "нет /dev/video0"
[ -e /run/bf7-camd.grabbed ] && good "кадры собираются" || note "кадры пока не собирались"
# Съёмка фотографий идёт через ffmpeg: он сжимает сложенный кадр в JPEG.
# Без него снимок остаётся PPM, а до исправления 2026-09-22 — пропадал вовсе.
if command -v ffmpeg >/dev/null 2>&1 && ffmpeg -version >/dev/null 2>&1; then
	good "ffmpeg для снимков работает"
else
	fail "ffmpeg не работает — фотографии не сожмутся"
fi

echo "=== Bluetooth ==="
# Адаптер поднимается связкой: драйвер MediaTek -> /dev/stpbt -> мост
# bf7-btbridge -> виртуальный HCI. Подробности в units/bf7-bluetooth.service.
if [ -c /dev/stpbt ]; then
	good "узел /dev/stpbt есть"
else
	fail "нет /dev/stpbt — драйвер bt_drv не загружен"
fi
# Загрузка стала быстрой (22 с): система «running» раньше, чем поднимается
# мост Bluetooth (~33 с). В первые полторы минуты подождём его, не дольше 30 с.
if [ "$(cut -d. -f1 /proc/uptime)" -lt 90 ]; then
	i=0
	while [ "$i" -lt 30 ] && ! hciconfig hci0 2>/dev/null | grep -q "UP RUNNING"; do
		sleep 1; i=$((i + 1))
	done
fi
if hciconfig hci0 2>/dev/null | grep -q "UP RUNNING"; then
	good "адаптер поднят ($(hciconfig hci0 2>/dev/null | sed -n 's/.*BD Address: \([0-9A-F:]*\).*/\1/p' | head -1))"
else
	fail "hci0 не поднят"
fi
# Адрес. Прошивка сама отдаёт заглушку 00:00:46:76:63:01; мост ставит
# заводской из nvdata. Заглушка значит, что установка адреса сломалась —
# а она долго падала из-за ложного пробуждения в BT_read.
bt_adres=$(hciconfig hci0 2>/dev/null | sed -n 's/.*BD Address: \([0-9A-F:]*\).*/\1/p' | head -1)
case "$bt_adres" in
	"") ;;
	00:00:46:76:63:01) fail "адрес Bluetooth — заглушка прошивки, заводской не встал" ;;
	*) good "адрес Bluetooth не заглушка" ;;
esac

echo "=== Браузер ==="
# Без списка правил Epiphany не покажет ни одной страницы: пока блокировщик
# не готов, он держит каждое решение о переходе в очереди, а заводской адрес
# списка с телефона недоступен. Это выглядит как «браузер не рисует» и стоило
# многих часов поисков не в той стороне.
spisok=/usr/local/share/bf7/easylist_min_content_blocker.json
if [ -s "$spisok" ]; then
	good "список блокировщика на месте ($(( $(stat -c%s "$spisok") / 1024 / 1024 )) МБ)"
else
	fail "нет $spisok — браузер не покажет ни одной страницы"
fi
if [ -n "$(ls /root/.cache/epiphany/adblock/compiled/* 2>/dev/null)" ]; then
	good "список уже скомпилирован — запуск браузера быстрый"
else
	good "список ещё не скомпилирован — первый запуск займёт около двух минут"
fi
# Без функций синхронизации EGL 1.5 DMABuf-рендерер WebKit встаёт после
# первого кадра: страница грузится, а поле серое (2026-09-25).
grep -q "eglClientWaitSync" /usr/local/lib/libEGL_libhybris.so.0.0.0 2>/dev/null &&
	BF7_EGL_TRACE=1 timeout 20 /root/src/gbmprobe 2>&1 | grep -q 'getProcAddress("eglCreateSync") -> есть' &&
	good "EGL отдаёт eglCreateSync через glvnd — браузер рисует без копий" ||
	note "не проверено: eglCreateSync через glvnd (нужен /root/src/gbmprobe)"
grep -q "DISABLE_DMABUF_RENDERER=1\|RENDERER_DISABLE_GBM=1\|FORCE_SHM=1" /usr/local/bin/bf7-web 2>/dev/null &&
	fail "bf7-web включает подпорку рендерера — страницы идут через общую память" ||
	good "bf7-web без подпорок рендерера"
# Видео в браузере: без ранга vah264dec playbin берёт программный avdec, без
# GST_GL_API — GstGL заводит настольный GL, которого у нас нет.
systemctl show bf7-phosh -p Environment 2>/dev/null | grep -q "vah264dec:MAX" &&
	systemctl show bf7-phosh -p Environment 2>/dev/null | grep -q "GST_GL_API=gles2" &&
	good "сеанс: аппаратный H.264 и GLES для GStreamer" ||
	fail "в bf7-phosh.service нет GST_PLUGIN_FEATURE_RANK/GST_GL_API — видео в браузере программное"
# GTK4 рисует процессором (Cairo): через GPU без ранней фиксации кадра —
# 44–46 к/с с рывками, а ранняя фиксация показала на панели недорисованный
# кадр (2026-09-25). Кэш шейдеров драйвера нужен и так (phoc, WebKit, Vulkan).
if systemctl show bf7-phosh -p Environment 2>/dev/null | grep -q "GDK_DEBUG=gl-gles"; then
	fail "в bf7-phosh.service снова GDK_DEBUG=gl-gles — GTK4 на GPU даёт 45 к/с с рывками"
else
	good "GTK4 рисует процессором — ровные 60 к/с"
fi
grep -q "obshchij.blob" /usr/local/lib/libEGL_libhybris.so.0.0.0 2>/dev/null &&
	good "кэш шейдеров драйвера (общий журнал) в libhybris" ||
	fail "libhybris без общего кэша шейдеров — пересобери"
vainfo 2>/dev/null | grep -q VAProfileHEVCMain &&
	good "аппаратный H.265 (HEVC) виден через VA-API" ||
	fail "VA-драйвер без HEVC — ролики H.265 декодируются процессором (sh vaapi-shim/build.sh)"
grep -q "не копирую" /usr/lib/aarch64-linux-gnu/dri/bf7_drv_video.so 2>/dev/null &&
	good "VA-драйвер кладёт кадры в вывезенный буфер (GStreamer без зелёного экрана)" ||
	fail "VA-драйвер старый — у vah264dec через dma-buf будет зелёный кадр (sh vaapi-shim/build.sh)"

echo "=== Фонарик ==="
# Светодиод вспышки как устройство leds — модуль bf7_torch. Чип на i2c-5:0x63,
# клон LM3644; в переписи железа он когда-то числился за MT6370, которого
# на плате нет.
if [ -d /sys/class/leds/white:torch ]; then
	good "фонарик есть (white:torch, ступеней $(cat /sys/class/leds/white:torch/max_brightness))"
	[ "$(cat /sys/class/leds/white:torch/brightness)" = 0 ] ||
		note "фонарик сейчас горит"
else
	fail "нет /sys/class/leds/white:torch — не загружен модуль bf7_torch"
fi

echo "=== Экономия ==="
# Три настройки, каждая из которых молча пропадает при переустановке или
# новом ядре, и заметно это только по батарее.
case "$(cat /sys/module/bf7_drm/parameters/usyplyat_panel 2>/dev/null)" in
	Y) good "тракт экрана засыпает вместе с выводом (usyplyat_panel)";;
	N) fail "тракт экрана не засыпает — нет /etc/tmpfiles.d/bf7-ekran-son.conf?";;
	*) fail "нет параметра usyplyat_panel — ядро без bf7_drm нового образца";;
esac
if [ -e /sys/class/net/wlan0 ]; then
	iw dev wlan0 get power_save 2>/dev/null | grep -q ": on" &&
		good "энергосбережение Wi-Fi включено" ||
		fail "энергосбережение Wi-Fi выключено — нет bf7-nm-powersave.conf?"
fi
# Без этого перед сном ядро переключает терминал и навсегда ждёт seatd
# (у того SIGUSR1 заблокирован) — rtcwake виснет в vt_waitactive.
[ "$(cat /sys/module/printk/parameters/console_suspend 2>/dev/null)" = Y ] &&
	good "консоли засыпают вместе с системой (сон без переключения терминала)" ||
	fail "console_suspend=N — сон зависнет на переключении терминала; нет bf7-son.conf?"
[ "$(cat /sys/class/graphics/fbcon/cursor_blink 2>/dev/null)" = 0 ] &&
	good "курсор консоли не мигает (не будит процессор)" ||
	fail "курсор консоли мигает — нет строки в bf7-tiho.conf?"
# Модули связности наши (оверлей kernel-work/bf7-connectivity-overlay): сон не
# рвётся ответом модема на сообщение Wi-Fi, а при тёмном экране Wi-Fi спит.
# Пересборка из голого донора молча вернула бы оба бага.
ext=/lib/modules/$(uname -r)/extra
grep -aq "BF7: LTE coex answer" "$ext/wmt_drv.ko" 2>/dev/null &&
	good "wmt_drv ждёт ответ модема перед сном (сон не рвётся через секунду)" ||
	fail "wmt_drv без правки сна — телефон проснётся через секунду после засыпания"
grep -aq "BF7: suspend mode" "$ext/wlan_drv_gen4m.ko" 2>/dev/null &&
	good "драйвер Wi-Fi засыпает при тёмном экране" ||
	fail "драйвер Wi-Fi без режима сна при тёмном экране — прерываний в 30 раз больше"
# Сон: глубокий (s2idle почти не экономит), с крюками экрана/подсветки и
# сети; UPower должен видеть отключение кабеля, иначе автосна нет вовсе.
grep -q '^SuspendState=mem' /etc/systemd/sleep.conf.d/bf7-sleep.conf 2>/dev/null &&
	good "сон глубокий (deep)" ||
	fail "сон не глубокий — SuspendState в bf7-sleep.conf (s2idle ~55 мА)"
[ -x /usr/lib/systemd/system-sleep/bf7-ekran-pered-snom ] && [ -x /usr/lib/systemd/system-sleep/bf7-posle-sna ] &&
	good "крюки сна на месте (экран и подсветка перед сном, сеть и модем после)" ||
	fail "нет крюков сна bf7-ekran-pered-snom/bf7-posle-sna — подсветка светит во сне, сеть может не проснуться"
[ -f /etc/udev/rules.d/79-bf7-pitanie.rules ] &&
	good "UPower видит отключение кабеля (79-bf7-pitanie.rules)" ||
	fail "нет 79-bf7-pitanie.rules — без кабеля телефон не уснёт"
# Детектор зависаний MediaTek: зависнет система — ядро перезагрузит телефон
# через ~5 минут (проверено 2026-09-24: bootreason=hang_detect).
systemctl is-active -q bf7-hang-kick 2>/dev/null &&
	good "детектор зависаний кормится (зависание → перезагрузка через ~5 мин)" ||
	fail "bf7-hang-kick не работает — зависший телефон сам не перезагрузится"
# Камера, пережившая сон, гасила при закрытии такты второй раз и роняла
# общую с дисплеем шину SMI: перезагрузка после сна висела до сторожа.
systemctl show bf7-camera -p Conflicts 2>/dev/null | grep -q sleep.target &&
	good "камера останавливается перед сном и поднимается после" ||
	fail "bf7-camera без Conflicts=sleep.target — перезагрузка после сна зависнет на 60 с"
systemctl is-enabled -q bf7-straza-vykl 2>/dev/null &&
	good "сторож выключения на месте (зависшая перезагрузка — сброс через 60 с)" ||
	fail "нет сторожа выключения — зависшая перезагрузка будет ждать 15-25 минут"

echo "=== Вид ==="
# Эмодзи в SMS и уведомлениях без шрифта — пустые квадраты (хозяин пишет 🤔😁).
fc-list 2>/dev/null | grep -qi "Noto Color Emoji" &&
	good "цветные эмодзи есть (fonts-noto-color-emoji)" ||
	fail "нет шрифта эмодзи — в SMS квадраты (apt install fonts-noto-color-emoji)"
# Шрифт интерфейса — Cantarell; без него подставлялся сжатый Noto Sans
# SemiCondensed, а 1500+ шрифтов замедляли запуск каждого приложения.
fc-match Cantarell 2>/dev/null | grep -q "Cantarell" &&
	good "шрифт интерфейса Cantarell, шрифтов в fontconfig: $(fc-list 2>/dev/null | wc -l)" ||
	fail "нет Cantarell — интерфейс сжатым Noto (apt install fonts-cantarell)"
# Сеть второго оператора имени не шлёт — MM +bf7.8 берёт его с SIM, иначе под значком «250xx».
opname=$(mmcli -m any 2>/dev/null | sed -n "s/.*operator name: //p" | tr -d " ")
case "$opname" in
	"") note "имя оператора неизвестно (модема нет?)";;
	*[!0-9]*) good "под значком связи имя оператора: $opname";;
	*) fail "вместо имени оператора цифры ($opname) — ModemManager без заплатки sim-operator-name";;
esac

echo "=== Ввод ==="
grep -q mtk-tpd2 /proc/bus/input/devices 2>/dev/null && good "тачскрин виден" || fail "тачскрина нет"
[ -n "$(ls /dev/input/event* 2>/dev/null)" ] && good "устройства ввода есть" || fail "устройств ввода нет"

echo
printf 'Итог: %d в порядке, %d с изъяном\n' "$ok" "$bad"
[ "$bad" -eq 0 ] && echo 'Телефон в полном порядке.' || \
	echo 'Есть чем заняться — см. строки со знаком минус.'
exit $(( bad > 0 ? 1 : 0 ))
