#!/bin/sh
# BF7: разложить все наши файлы по местам на телефоне.
#
# Зачем. В ночь на 2026-09-22 починка повреждённой файловой системы унесла
# четыре важные мелочи — описание поставщика EGL для glvnd, описание службы
# вендорского слоя, настройку модуля камеры и профиль сети. Каждая ломала своё,
# и каждую пришлось искать по симптому. Этот скрипт кладёт всё разом и говорит,
# что изменилось.
#
# Запускать с ПК: скопировать hybris/bf7-gpu, hybris/bf7-codec2 и
# native-linux-build/bf7-wifi-init.sh с сохранением раскладки каталогов и
# запустить hybris/bf7-gpu/bf7-install.sh (сперва с --proverka);
# либо прямо из каталога репозитория, смонтированного на телефон.
#
# Двоичные части (bf7-camd, bf7-seninf-up, прослойки) собираются отдельно: см.
# docs/bf7-native/camera.md и комментарии в самих исходниках. Здесь только то,
# что копируется как есть.
set -u

# Только на телефоне и только от root. 2026-09-23 я по ошибке запустил его на
# хозяйской машине: файлы не легли (нет прав), но systemctl daemon-reload
# вызвал окно пароля polkit на чужом рабочем столе.
# --proverka: ничего не менять, только показать, чем телефон расходится с
# репозиторием. Вслепую ставить опасно: файл на телефоне бывает новее
# (так однажды старый bf7-wifi-init сломал Wi-Fi).
SUHO=0
case "${1:-}" in
	-h|--help)
		sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'
		echo "  --proverka  только показать расхождения, ничего не менять"
		exit 0 ;;
	--proverka) SUHO=1 ;;
esac
if ! grep -qs "MT6761" /proc/device-tree/model /proc/device-tree/compatible 2>/dev/null; then
	echo "bf7-install: это не телефон BF7 (нет MT6761 в дереве устройств) — ничего не делаю" >&2
	exit 1
fi
[ "$(id -u)" = 0 ] || { echo "bf7-install: нужен root" >&2; exit 1; }

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
changed=0

put() {
	src=$1
	dst=$2
	mode=${3:-644}
	[ -f "$src" ] || { echo "  нет исходника: $src"; return; }
	if [ -f "$dst" ] && cmp -s "$src" "$dst"; then
		return
	fi
	if [ "$SUHO" = 1 ]; then
		[ -f "$dst" ] && echo "  отличается: $dst" || echo "  нет на телефоне: $dst"
		changed=$((changed + 1))
		return
	fi
	mkdir -p "$(dirname "$dst")"
	install -m "$mode" "$src" "$dst" && {
		echo "  положено: $dst"
		changed=$((changed + 1))
	}
}

echo "Описания служб:"
for f in "$HERE"/units/*.service; do
	[ -f "$f" ] || continue
	put "$f" "/etc/systemd/system/$(basename "$f")"
done
# Старшие службы лежат в корне каталога, а не в units/. Долго их клали
# руками, и установщик, включая их, не умел вернуть сами файлы — ровно то, от
# чего он написан. Сверено 2026-09-23: на телефоне все совпадают с этими.
# bf7-gui.service — прежний сеанс, давно не ставится; bf7-zram отставлена
# (своп поднимает генератор systemd, см. units/отставлены).
for n in bf7-android-mounts bf7-binderfs bf7-firewall bf7-fuelgauge \
         bf7-minilogd bf7-session bf7-vnc; do
	put "$HERE/$n.service" "/etc/systemd/system/$n.service"
done

echo "Настройки:"
put "$HERE/units/bf7-camera-modprobe.conf" /etc/modprobe.d/bf7-camera-modprobe.conf
put "$HERE/units/bf7-camera-modules.conf"  /etc/modules-load.d/bf7-camera.conf
put "$HERE/units/bf7-backlight-modules.conf" /etc/modules-load.d/bf7-backlight.conf
put "$HERE/units/bf7-torch-modules.conf"    /etc/modules-load.d/bf7-torch.conf
put "$HERE/units/bf7-power-key.conf"       /etc/systemd/logind.conf.d/bf7-power-key.conf
put "$HERE/units/bf7-sleep.conf"           /etc/systemd/sleep.conf.d/bf7-sleep.conf
put "$HERE/units/mimeapps.list"            /etc/xdg/mimeapps.list
# Русские переводы: докерный корень выбрасывает все (см. сам файл).
put "$HERE/dpkg/zz-bf7-ru-locale"          /etc/dpkg/dpkg.cfg.d/zz-bf7-ru-locale
put "$HERE/units/asound.conf"              /etc/asound.conf
put "$HERE/units/mpv.conf"                 /etc/mpv/mpv.conf
put "$HERE/units/bf7-pulse.pa"             /etc/pulse/bf7.pa
put "$HERE/units/bf7-pulse-daemon.conf"    /etc/pulse/daemon.conf.d/bf7.conf
put "$HERE/units/phoc.ini"                 /etc/phosh/phoc.ini
put "$HERE/units/sm.puri.Phosh.desktop"    /usr/local/share/applications/sm.puri.Phosh.desktop
put "$HERE/units/51-bf7-alsa-no-mmap.lua"  /etc/wireplumber/main.lua.d/51-bf7-alsa-no-mmap.lua
put "$HERE/units/bf7-nm-unmanaged.conf"    /etc/NetworkManager/conf.d/bf7-nm-unmanaged.conf
put "$HERE/units/bf7-nm-powersave.conf"    /etc/NetworkManager/conf.d/bf7-nm-powersave.conf
# autovt@ — синоним getty@, его запускает logind; вставка нужна под обоими именами.
put "$HERE/units/bf7-getty-tty1.conf"      /etc/systemd/system/getty@tty1.service.d/bf7-phosh.conf
put "$HERE/units/bf7-getty-tty1.conf"      /etc/systemd/system/autovt@tty1.service.d/bf7-phosh.conf
put "$HERE/units/bf7-nm-order.conf"        /etc/systemd/system/NetworkManager.service.d/bf7-nm-order.conf
put "$HERE/units/bf7-bluetoothd.conf"     /etc/systemd/system/bluetooth.service.d/bf7-bluetoothd.conf
put "$HERE/units/bf7-backlight.rules"      /etc/udev/rules.d/71-bf7-backlight.rules

# Правила udev. Имя подсветки с числом впереди не случайно: порядок чтения
# правил определяется именно им, а привязка светодиода должна случиться после
# того, как правила ядра создадут узел.
echo "Правила udev:"
for f in "$HERE"/units/*.rules "$HERE"/99-bf7-touch.rules; do
	[ -f "$f" ] || continue
	name=$(basename "$f")
	case "$name" in
	bf7-backlight.rules) name=71-bf7-backlight.rules;;
	esac
	put "$f" "/etc/udev/rules.d/$name"
done

echo "Запускалки:"
for f in "$HERE"/bf7-phosh-session.sh "$HERE"/bf7-audio-up.sh "$HERE"/bf7-audio-amp.sh \
         "$HERE"/bf7-web.sh; do
	[ -f "$f" ] || continue
	put "$f" "/usr/local/bin/$(basename "$f" .sh)" 755
done

# Служебные скрипты. Сюда же помощник отметки слота: без него bf7-boot-ok —
# пустышка, счётчик попыток загрузчика тает, и телефон в конце концов уходит на
# слот со стоковым ядром.
# Фонарик: приложение, ярлык, значок и переключатель для сценариев.
echo "Фонарик:"
put "$HERE/fonarik/bf7-fonarik.py"      /usr/local/bin/bf7-fonarik-app 755
put "$HERE/fonarik/bf7-fonarik.sh"      /usr/local/bin/bf7-fonarik 755
put "$HERE/fonarik/bf7-fonarik.desktop" /usr/local/share/applications/bf7-fonarik.desktop
put "$HERE/fonarik/bf7-fonarik.svg"     /usr/local/share/icons/hicolor/scalable/apps/bf7-fonarik.svg
# Камера: приложение с видоискателем; прежнее окно серий скрыто из сетки.
put "$HERE/../bf7-camera/bf7-kamera.py"          /usr/local/bin/bf7-kamera 755
put "$HERE/../bf7-camera/ru.bf7.Kamera.desktop"  /usr/local/share/applications/ru.bf7.Kamera.desktop
put "$HERE/../bf7-camera/bf7-camera.desktop"     /usr/local/share/applications/bf7-camera.desktop
# Вибромотор: мост к feedbackd; звонок играет ритм из /etc/bf7/vibro-melodiya.
echo "Вибро:"
if command -v gcc >/dev/null 2>&1; then
	tmp=$(mktemp)
	gcc -O2 -o "$tmp" "$HERE/bf7-vibrad.c" -lpthread && put "$tmp" /usr/local/bin/bf7-vibrad 755
	rm -f "$tmp"
fi
[ -f /etc/bf7/vibro-melodiya ] || { mkdir -p /etc/bf7; echo betkhoven > /etc/bf7/vibro-melodiya; }

# Сейсмограф: служба пишет тряску по акселерометру, ярлык рисует ночь.
echo "Сейсмограф:"
if command -v gcc >/dev/null 2>&1; then
	tmp=$(mktemp)
	gcc -O2 -o "$tmp" "$HERE/bf7-seismograf.c" -lm && put "$tmp" /usr/local/bin/bf7-seismograf 755
	rm -f "$tmp"
fi
put "$HERE/bf7-seismograf-grafik.py"      /usr/local/bin/bf7-seismograf-grafik 755
put "$HERE/units/bf7-seismograf.service"  /etc/systemd/system/bf7-seismograf.service
# Автоповорот: ориентацию считает сейсмограф, оболочке её отдаёт наш
# net.hadess.SensorProxy; iio-sensor-proxy датчиков не находит и мешал бы.
put "$HERE/bf7-sensor-proxy.py"           /usr/local/bin/bf7-sensor-proxy 755
put "$HERE/units/bf7-sensor-proxy.service" /etc/systemd/system/bf7-sensor-proxy.service
# RTC: в ядре нет RTC_SYSTOHC — время из NTP пишет в часы таймер.
put "$HERE/units/bf7-rtc-sync.service"    /etc/systemd/system/bf7-rtc-sync.service
put "$HERE/units/bf7-rtc-sync.timer"      /etc/systemd/system/bf7-rtc-sync.timer
systemctl mask iio-sensor-proxy.service >/dev/null 2>&1 || true
put "$HERE/bf7-seismograf.desktop"        /usr/local/share/applications/bf7-seismograf.desktop
# GPS: модуль gps_drv (kernel-work/bf7-gps, собирается против ядра — как
# модули Wi-Fi, при каждом новом ядре заново), mnld из /vendor под подложкой
# свойств, раздатчик NMEA для geoclue. Подробности — в hybris/bf7-gps.
echo "GPS:"
GPS=$HERE/../bf7-gps
KO=$HERE/../../kernel-work/bf7-gps/gps_drv.ko
if [ -f "$KO" ] && modinfo -F vermagic "$KO" 2>/dev/null | grep -q "^$(uname -r) "; then
	put "$KO" "/lib/modules/$(uname -r)/extra/gps_drv.ko"
else
	echo "  gps_drv.ko нет или собран не под $(uname -r) — kernel-work/bf7-gps/build.sh"
fi
[ "$SUHO" = 1 ] || mkdir -p /opt/bf7/gps
put "$GPS/bf7-gps-props.so"               /opt/bf7/gps/bf7-gps-props.so
if command -v gcc >/dev/null 2>&1 && [ -d /root/hybris/android-headers ]; then
	tmp=$(mktemp)
	gcc -O2 -I/root/hybris/android-headers -I/root/hybris/libhybris/hybris/include \
		-o "$tmp" "$GPS/bf7-gps-nmea.c" -L/usr/local/lib -lhardware -lhybris-common -pthread &&
		put "$tmp" /usr/local/bin/bf7-gps-nmea 755
	rm -f "$tmp"
fi
put "$GPS/bf7-gps.service"                /etc/systemd/system/bf7-gps.service
put "$GPS/bf7-gps-nmea.service"           /etc/systemd/system/bf7-gps-nmea.service
put "$GPS/bf7-gps-epo.sh"                 /usr/local/sbin/bf7-gps-epo 755
put "$GPS/bf7-gps-epo.service"            /etc/systemd/system/bf7-gps-epo.service
put "$GPS/bf7-gps-epo.timer"              /etc/systemd/system/bf7-gps-epo.timer
# geoclue 2.6 из Debian не читает conf.d — строка правится в самом файле
GC=/etc/geoclue/geoclue.conf
if [ -f "$GC" ] && ! grep -q '^nmea-socket=/run/bf7-gps/nmea.sock' "$GC"; then
	if [ "$SUHO" = 1 ]; then
		echo "  в $GC нет nmea-socket"
		changed=$((changed + 1))
	else
		sed -i '/^\[network-nmea\]/a nmea-socket=/run/bf7-gps/nmea.sock' "$GC" &&
			echo "  $GC: nmea-socket"
	fi
fi

# Снимок экрана: запись в сетке, grim через 3 с (клавиши Print у телефона нет).
put "$HERE/bf7-snimok.py"                 /usr/local/bin/bf7-snimok 755
put "$HERE/bf7-snimok.desktop"            /usr/local/share/applications/bf7-snimok.desktop

# «Фото»: eog сразу с папкой «Изображения».
put "$HERE/bf7-foto.sh"                   /usr/local/bin/bf7-foto 755
put "$HERE/bf7-foto.desktop"              /usr/local/share/applications/bf7-foto.desktop

# Точка доступа: раздача интернета через ap0 (переключателя в phosh 0.24 нет).
put "$HERE/bf7-tochka.py"                 /usr/local/bin/bf7-tochka 755
put "$HERE/bf7-tochka.desktop"            /usr/local/share/applications/bf7-tochka.desktop

# Ночной свет: gsd-color считает температуру, эта служба пишет её в блок
# гаммы MediaTek (у bf7_drm нет GAMMA_LUT). Запускается из bf7-phosh-session.
put "$HERE/bf7-nochnoj-svet.py"           /usr/local/bin/bf7-nochnoj-svet 755
put "$HERE/bf7-nochnoj-svet.desktop"      /usr/local/share/applications/bf7-nochnoj-svet.desktop
put "$HERE/bf7-seismograf.svg"            /usr/local/share/icons/hicolor/scalable/apps/bf7-seismograf.svg
command -v gtk-update-icon-cache >/dev/null 2>&1 &&
	gtk-update-icon-cache -q -f -t /usr/local/share/icons/hicolor 2>/dev/null

echo "Обои:"
put "$HERE/fon/adwaita-l-720x1612.jpg" /usr/local/share/backgrounds/bf7/adwaita-l-720x1612.jpg
put "$HERE/fon/adwaita-d-720x1612.jpg" /usr/local/share/backgrounds/bf7/adwaita-d-720x1612.jpg

echo "Служебные скрипты:"
put "$HERE/bf7-devnodes-all.sh"        /usr/local/sbin/bf7-devnodes-all 755
put "$HERE/bf7-wait-seninf.sh"         /usr/local/sbin/bf7-wait-seninf 755
put "$HERE/bf7-mark-boot-success.py"   /usr/local/sbin/bf7-mark-boot-success 755
put "$HERE/bf7-trim-autostart.sh"      /usr/local/sbin/bf7-trim-autostart 755
put "$HERE/bf7-selftest.sh"            /usr/local/bin/bf7-selftest 755
put "$HERE/bf7-boot-ok.sh"             /usr/local/sbin/bf7-boot-ok 755
put "$HERE/bf7-wifi-watch.sh"          /usr/local/sbin/bf7-wifi-watch 755
put "$HERE/bf7-otperet.sh"             /usr/local/bin/bf7-otperet 755
put "$HERE/bf7-son-proba.sh"           /usr/local/bin/bf7-son-proba 755
put "$HERE/bf7-budilnik.py"            /usr/local/bin/bf7-budilnik 755
put "$HERE/bf7-phosh-skryt.sh"         /usr/local/bin/bf7-phosh-skryt 755
put "$HERE/bf7-perevody.sh"            /usr/local/bin/bf7-perevody 755
put "$HERE/perevody/mobile-settings.mo" /usr/share/locale/ru/LC_MESSAGES/mobile-settings.mo
put "$HERE/perevody/portfolio.mo" /usr/share/locale/ru/LC_MESSAGES/portfolio.mo
# Шрифты: спрятать 1540 начертаний fonts-noto-extra (см. сам файл); нужен fonts-cantarell.
put "$HERE/fonts/70-bf7-noto-extra.conf" /etc/fonts/conf.d/70-bf7-noto-extra.conf
put "$HERE/bf7-dosyp.py"               /usr/local/bin/bf7-dosyp 755
# RetroArch: обёртка с нашим загрузчиком Vulkan, ярлык вместо пакетного и свои
# экранные кнопки (bf7-gamepad.cfg). Настройки игрока (video_driver = vulkan,
# input_overlay = …) — в /root/.config/retroarch/retroarch.cfg, не здесь.
put "$HERE/bf7-retroarch.sh"           /usr/local/bin/bf7-retroarch 755
put "$HERE/retroarch.desktop"          /usr/local/share/applications/retroarch.desktop
for f in "$HERE"/retroarch-overlay/*; do
	put "$f" "/usr/local/share/bf7/retroarch-overlay/$(basename "$f")"
done
# bf7-tishina — ночная тишина: звук выключить/вернуть, «do 09:00» — с таймером.
put "$HERE/bf7-tishina.sh"             /usr/local/bin/bf7-tishina 755
# Перед сном гасить экран: сон при горящем тракте экрана ломал CMDQ и вешал ядро (RCU).
put "$HERE/bf7-ekran-pered-snom.sh"     /usr/lib/systemd/system-sleep/bf7-ekran-pered-snom 755
# После сна: разбудить NetworkManager, если остался «asleep»; вернуть модем ModemManager.
put "$HERE/bf7-posle-sna.sh"            /usr/lib/systemd/system-sleep/bf7-posle-sna 755
# Кормилец детектора зависаний MediaTek: зависла система — ядро перезагрузит телефон.
put "$HERE/bf7-hang-kick.py"            /usr/local/sbin/bf7-hang-kick 755
# bf7-palec — касание и свайп по экрану без рук (запись в узел тача).
put "$HERE/bf7-palec"                  /usr/local/bin/bf7-palec 755
# Сценарии, которые запускают службы, — долго клались руками (сверено
# 2026-09-23: на телефоне совпадают с этими). Два лежат вне этого каталога:
# если установщик запущен из одной копии bf7-gpu, он скажет «нет исходника».
put "$HERE/bf7-firewall.sh"            /usr/local/sbin/bf7-firewall 755
put "$HERE/bf7vnc.py"                  /usr/local/sbin/bf7vnc.py 755
put "$HERE/minilogd.py"                /usr/local/sbin/minilogd.py 755
put "$HERE/bf7-android-mounts.sh"      /usr/local/sbin/bf7-android-mounts.sh 755
put "$HERE/bf7-bt-prepare.sh"          /usr/local/sbin/bf7-bt-prepare 755
put "$HERE/../bf7-codec2/hal-start.sh" /usr/local/bin/hal-start.sh 755
put "$HERE/../../native-linux-build/bf7-wifi-init.sh" /usr/local/sbin/bf7-wifi-init 755

# Ярлык браузера и свой состав сеанса.
echo "Оболочка:"
put "$HERE/units/bf7-web.desktop"   /usr/local/share/applications/bf7-web.desktop
put "$HERE/units/bf7-phosh.session" /usr/local/share/gnome-session/sessions/phosh.session

# Предел ожидания для oneshot-служб. У Type=oneshot systemd таймаут по
# умолчанию отключает, и общий DefaultTimeoutStartSec на них не действует —
# любая может повиснуть навсегда вместе с загрузкой. Разделам Android даём
# больше: монтирование двух десятков образов по петлям на eMMC небыстрое.
# При выключении службы с буферами GPU/ION ждут выхода phoc (см. сам файл).
echo "Порядок остановки после phoc:"
for u in bf7-android-hal bf7-videod bf7-modem; do
	put "$HERE/units/bf7-posle-phoc.conf" "/etc/systemd/system/$u.service.d/bf7-posle-phoc.conf"
done
echo "Пределы ожидания:"
for u in bf7-wifi bf7-binderfs bf7-firewall bf7-audio \
         bf7-console-quiet bf7-touch-init bf7-devnodes-all; do
	put "$HERE/units/bf7-timeout.conf" "/etc/systemd/system/$u.service.d/bf7-timeout.conf"
done
if [ -f "$HERE/units/bf7-timeout.conf" ]; then
	vrem=$(mktemp)
	sed 's/TimeoutStartSec=90/TimeoutStartSec=180/' "$HERE/units/bf7-timeout.conf" > "$vrem"
	put "$vrem" /etc/systemd/system/bf7-android-mounts.service.d/bf7-timeout.conf
	rm -f "$vrem"
fi

# Камера ждёт, пока приёмник настроен: bf7-camera.service это Type=simple, и
# systemd считает её запущенной раньше, чем поднят поток с матрицы.
if [ -f "$HERE/units/bf7-camd-wait.conf" ]; then
	put "$HERE/units/bf7-camd-wait.conf" /etc/systemd/system/bf7-camd.service.d/bf7-wait.conf
fi

# Поставщик EGL для glvnd. Файл маленький, но без него оболочка не стартует:
# glvnd уходит к Mesa и та отвечает «DRI2: gbm device using incorrect/
# incompatible backend».
echo "Поставщик EGL:"
vend=/usr/share/glvnd/egl_vendor.d/10_libhybris.json
if [ ! -s "$vend" ] || ! grep -q libEGL_libhybris "$vend" 2>/dev/null; then
	[ "$SUHO" = 1 ] && { echo "  нет или испорчен: $vend"; changed=$((changed + 1)); }
fi
if [ "$SUHO" = 0 ] && { [ ! -s "$vend" ] || ! grep -q libEGL_libhybris "$vend" 2>/dev/null; }; then
	mkdir -p "$(dirname "$vend")"
	cat > "$vend" <<'JSON'
{
    "file_format_version" : "1.0.0",
    "ICD" : {
        "library_path" : "libEGL_libhybris.so.0"
    }
}
JSON
	chmod 644 "$vend"
	echo "  восстановлен: $vend"
	changed=$((changed + 1))
fi

# Гасим автозапуск того, что телефону не нужно. Скрипт кладёт в
# ~/.config/autostart свои файлы с Hidden=true, пакетные не трогает.
if [ "$SUHO" = 0 ] && [ -x /usr/local/sbin/bf7-trim-autostart ]; then
	echo "Автозапуск:"
	/usr/local/sbin/bf7-trim-autostart | sed 's/^/  /'
fi

# --- Модем -----------------------------------------------------------------
# Модем MediaTek без Android: NVRAM на /mnt/vendor, HAL NVRAM, ccci_rpcd,
# ccci_mdinit с подпорками, мост AT-порта к /dev/ttyp0 для ModemManager и
# перевод в CFUN=4 до него. Подробности — в описаниях служб в bf7-modem/.
echo "Модем:"
M=$HERE/../bf7-modem
put "$M/bf7-modem-mounts.sh"   /usr/local/sbin/bf7-modem-mounts 755
put "$M/bf7-modem-sim.py"      /usr/local/sbin/bf7-modem-sim 755
for u in bf7-modem-mounts bf7-nvram-hal bf7-ccci-rpcd bf7-modem bf7-modem-at bf7-modem-sim; do
	put "$M/$u.service" "/etc/systemd/system/$u.service"
done
put "$M/77-bf7-modem.rules"         /etc/udev/rules.d/77-bf7-modem.rules
put "$M/mm-dropin/bf7-modem.conf"   /etc/systemd/system/ModemManager.service.d/bf7-modem.conf
put "$M/libbf7wakelock64.so"        /vendor.local/lib64/libbf7wakelock.so
# Звук разговора: bf7-rech (бионик, собран NDK, см. bf7-rech.c) и
# bf7-zvonki — замена callaudiod через активацию D-Bus. Сеанс видит новый
# файл службы после перезапуска или org.freedesktop.DBus.ReloadConfig.
put "$M/bf7-rech"                   /usr/local/bin/bf7-rech 755
put "$M/bf7-zvonki.py"              /usr/local/bin/bf7-zvonki 755
# bf7-skazat — сказать в разговор синтезом речи (нужен пакет espeak-ng).
put "$M/bf7-skazat"                 /usr/local/bin/bf7-skazat 755
# bf7-sluh — распознать речь собеседника в разговоре. Распознаёт Parakeet TDT
# 0.6B v3 int8 (sherpa-onnx), если есть /opt/bf7-golos/parakeet-tdt-0.6b-v3-int8
# и /opt/bf7-golos/venv с pip-пакетом sherpa-onnx (их установщик не ставит:
# нужна сеть — см. research-log 2026-09-30); иначе — whisper.cpp.
put "$M/bf7-sluh"                   /usr/local/bin/bf7-sluh 755
# bf7-hozyain — взять трубку, когда звонит хозяин (номер — в /etc/bf7/hozyain-nomer,
# вручную, не в git); разговор ведёт golos-pc/dialog-storozh на ПК.
put "$M/bf7-hozyain.py"             /usr/local/bin/bf7-hozyain 755
put "$M/bf7-hozyain.service"        /etc/systemd/system/bf7-hozyain.service
# bf7-pozvonit — позвонить и отдать разговор Клауду, когда ответят (+ECPI …,6),
# а не когда MM скажет «active» (у MTK это сразу после ATD, ещё до гудков).
put "$M/bf7-pozvonit.py"            /usr/local/bin/bf7-pozvonit 755
# bf7-mobil-avto — мобильный интернет сам, только на SIM из /etc/bf7/mobil-avto
# (у сотовых профилей NM автоподключение выключено: гонка с оператором SIM).
put "$M/bf7-mobil-avto.py"          /usr/local/bin/bf7-mobil-avto 755
put "$M/bf7-mobil-avto.service"     /etc/systemd/system/bf7-mobil-avto.service
put "$M/org.mobian_project.CallAudio.service" /usr/local/share/dbus-1/services/org.mobian_project.CallAudio.service
if command -v gcc >/dev/null 2>&1; then
	tmp=$(mktemp)
	gcc -O2 -o "$tmp" "$M/bf7-at-most.c" && put "$tmp" /usr/local/sbin/bf7-at-most 755
	rm -f "$tmp"
fi
# ccci_mdinit ждёт готовности NVRAM, которую на Android выставляет
# nvram_daemon в своём процессе, — заглушка свойств у нас на процесс своя.
# Звуковой HAL при маршрутизации опрашивает Bluetooth-чип через
# libbluetooth_mtk_pure, а та ждёт готовности connsys и номера чипа — без
# них каждый вызов висит минутами.
for l in vendor.service.nvram_init=Ready vendor.service.nvram_restore=Ready \
         vendor.connsys.driver.ready=yes persist.vendor.connsys.chipid=0x6761; do
	grep -qx "$l" /vendor.local/etc/bf7.prop 2>/dev/null && continue
	if [ "$SUHO" = 1 ]; then
		echo "  нет в bf7.prop: $l"
		changed=$((changed + 1))
	else
		echo "$l" >> /vendor.local/etc/bf7.prop && echo "  в bf7.prop: $l"
	fi
done

# --- Сетка приложений Phosh и Firefox ---------------------------------------
# Служебное и нерабочее прячется из Phosh подменами в
# /usr/local/share/applications (см. bf7-phosh-skryt: Xfce не затронут).
echo "Сетка Phosh:"
if [ "$SUHO" = 1 ]; then
	/usr/local/bin/bf7-phosh-skryt --proverka 2>/dev/null || echo "  bf7-phosh-skryt ещё не на телефоне"
else
	/usr/local/bin/bf7-phosh-skryt
fi
echo "Переводы:"
if [ "$SUHO" = 1 ]; then
	/usr/local/bin/bf7-perevody --proverka 2>/dev/null || true
else
	/usr/local/bin/bf7-perevody
fi

# Мобильная настройка Firefox (пакет firefox-esr-mobile-config 3.2.0) писана
# под модули .jsm, которых в Firefox 140 нет, — без правки Firefox при
# каждом старте показывал «Configuration Error». Пакетный файл отведён.
MCFA=/usr/lib/firefox-esr/mobile-config-autoconfig.js
if dpkg -s firefox-esr-mobile-config >/dev/null 2>&1; then
	if ! dpkg-divert --list "$MCFA" | grep -q .; then
		if [ "$SUHO" = 1 ]; then
			echo "  не отведён: $MCFA"
			changed=$((changed + 1))
		else
			dpkg-divert --add --rename --divert "$MCFA.debian" "$MCFA" >/dev/null &&
				echo "  отведён пакетный $MCFA"
		fi
	fi
	[ "$SUHO" = 1 ] && [ ! -f "$MCFA.debian" ] || put "$HERE/firefox/mobile-config-autoconfig.js" "$MCFA"
fi

echo
if [ "$SUHO" = 1 ]; then
	echo "Расхождений в файлах: $changed (ничего не менял)."
elif [ "$changed" -gt 0 ]; then
	echo "Изменено файлов: $changed. Перечитываю описания служб и правила."
	systemctl daemon-reload
	udevadm control --reload-rules 2>/dev/null
else
	echo "Всё уже на месте."
fi

# --- Настройки сеанса по умолчанию ---------------------------------------
# Системная база dconf. Действует на любой сеанс и не требует шины, в отличие
# от «gsettings set» руками; пользовательская база всё равно главнее.
# (Прежде здесь была postavit(), которая перезаписывала молча и без сверки;
# теперь всё идёт через put() — со сверкой и в отчёт.)
changed=0
echo "Настройки сеанса:"
# Подмена родной записи Epiphany: прячет второй «Веб-браузер» из сетки и
# уводит открытие ссылок на bf7-web, где снята песочница WebKit.
put "$HERE/org.gnome.Epiphany.desktop" \
	/usr/local/share/applications/org.gnome.Epiphany.desktop
if [ "$SUHO" = 0 ] && command -v update-desktop-database >/dev/null 2>&1; then
	update-desktop-database /usr/local/share/applications 2>/dev/null
fi

# Утихомирить болтливые драйверы на каждой загрузке — см. сам файл.
put "$HERE/tmpfiles/bf7-tiho.conf" /etc/tmpfiles.d/bf7-tiho.conf
put "$HERE/tmpfiles/bf7-ekran-son.conf" /etc/tmpfiles.d/bf7-ekran-son.conf
put "$HERE/tmpfiles/bf7-son.conf" /etc/tmpfiles.d/bf7-son.conf
put "$HERE/tmpfiles/bf7-svetodiod.conf" /etc/tmpfiles.d/bf7-svetodiod.conf

put "$HERE/dconf/00-bf7" /etc/dconf/db/local.d/00-bf7
put "$HERE/dconf/user"   /etc/dconf/profile/user
if [ "$SUHO" = 1 ]; then
	[ "$changed" -gt 0 ] && echo "  (dconf update не делаю — режим проверки)"
elif command -v dconf >/dev/null 2>&1; then
	dconf update
else
	echo "  нет dconf-cli — поставь его, иначе база не соберётся и GTK будет ругаться"
fi

# Список правил блокировщика рекламы. Кладётся не отсюда, а с хозяйской
# машины (bf7-adblock-obnovit.sh): заводской адрес с телефона недоступен.
# Без файла браузер не покажет НИ ОДНОЙ страницы — Epiphany держит каждое
# решение о переходе в очереди, пока блокировщик не готов, а без списка он
# не будет готов никогда. Поэтому здесь не молчаливая проверка, а крик.
if [ ! -s /usr/local/share/bf7/easylist_min_content_blocker.json ]; then
	echo "  ВНИМАНИЕ: нет списка блокировщика рекламы."
	echo "  Браузер не покажет ни одной страницы, пока он не появится."
	echo "  Выполни на хозяйской машине: hybris/bf7-gpu/bf7-adblock-obnovit.sh"
fi

# --- Включение служб ------------------------------------------------------
# Установщик долго только раскладывал файлы, а включение оставалось ручным.
# Это вышло боком 2026-09-22: проверка файловой системы после паник убрала
# ссылку включения у bf7-bluetooth, и Bluetooth молча не поднялся. Файл на
# месте, служба «disabled» — по одному взгляду не заметишь.
#
# Перечисляем только то, что обязано подниматься само. Ничего не выключаем и
# не маскируем: это осознанные решения хозяина, им тут не место.
vklyuchit() {
	for u in "$@"; do
		[ -f "/etc/systemd/system/$u" ] || continue
		case "$(systemctl is-enabled "$u" 2>/dev/null)" in
			enabled|static|masked|indirect) continue ;;
		esac
		[ "$SUHO" = 1 ] && { echo "  не включена: $u"; continue; }
		systemctl enable "$u" >/dev/null 2>&1 &&
			echo "  включена $u"
	done
}
echo "Включение служб:"
vklyuchit \
	bf7-audio.service bf7-binderfs.service \
	bf7-bluetooth.service bf7-boot-ok.service bf7-camd.service \
	bf7-camera.service bf7-console-quiet.service bf7-devnodes-all.service \
	bf7-firewall.service bf7-fuelgauge.service bf7-minilogd.service \
	bf7-phosh.service bf7-pulse.service bf7-recovery-telnet.service \
	bf7-touch-init.service bf7-vibrad.service bf7-vnc.service \
	bf7-wifi.service bf7-wifi-watch.service \
	bf7-extamp.service bf7-budilnik.service bf7-dosyp.service \
	bf7-thermal.service bf7-thermalloadalgod.service bf7-videod.service \
	bf7-modem-mounts.service bf7-nvram-hal.service bf7-ccci-rpcd.service \
	bf7-modem.service bf7-modem-at.service bf7-modem-sim.service \
	bf7-hozyain.service bf7-mobil-avto.service bf7-straza-vykl.service \
	bf7-hang-kick.service bf7-seismograf.service bf7-sensor-proxy.service \
	bf7-gps.service bf7-gps-nmea.service bf7-gps-epo.timer bf7-rtc-sync.timer
