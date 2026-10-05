#!/bin/sh
# BF7: погасить автозапуск того, что телефону не нужно.
#
# Зачем. Оболочка тянет за собой полный набор служб настроек GNOME — их
# восемнадцать, — плюс остатки прежних заходов: демон настроек Xfce, хранитель
# экрана из иксов, апплет печати. Каждая штука это отдельный процесс на два
# десятка мегабайт и своя доля процессора при старте сеанса, а старт сеанса у
# нас упирается именно в процессор.
#
# Как гасим. Не трогая пакетные файлы в /etc/xdg/autostart, кладём рядом свои
# в ~/.config/autostart с Hidden=true — по правилам XDG это отменяет запуск.
# Вернуть что-либо можно, убрав соответствующий файл.
#
# Правкой состава сеанса (phosh.session) это не решается: службы поднимаются
# именно автозапуском, а не списком RequiredComponents — проверено.
#
# Имена переменных только латиницей: /bin/sh кириллические не принимает.
set -u

dir=${HOME:-/root}/.config/autostart
mkdir -p "$dir"

# Планшеты, смарт-карты, принтеры, обмен файлами, защита USB (у нас по USB
# идёт единственный надёжный канал к телефону). pulseaudio — это
# start-pulseaudio-x11 для пользовательского сервера; у нас звуковой сервер
# системный (bf7-pulse), и тот при каждом старте сеанса выходил с кодом 1.
off="org.gnome.SettingsDaemon.Wacom
org.gnome.SettingsDaemon.Smartcard
org.gnome.SettingsDaemon.PrintNotifications
org.gnome.SettingsDaemon.Sharing
org.gnome.SettingsDaemon.UsbProtection
print-applet
xscreensaver
xfsettingsd
geoclue-demo-agent
onboard-autostart
pulseaudio"

made=0
for name in $off; do
	[ -f "/etc/xdg/autostart/$name.desktop" ] || continue
	cat > "$dir/$name.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=$name
Exec=/bin/true
Hidden=true
EOF
	made=$((made + 1))
done

# Когда-то погашенное, а теперь нужное: снимаем свои же заглушки (чужие
# файлы с тем же именем не трогаем). Wwan гасили, пока модема не было; с
# 2026-09-23 модем работает, а gsd-wwan спрашивает PIN SIM-карты.
on="org.gnome.SettingsDaemon.Wwan"
for name in $on; do
	f=$dir/$name.desktop
	[ -f "$f" ] && grep -qx "Exec=/bin/true" "$f" && grep -qx "Hidden=true" "$f" &&
		rm -f "$f" && echo "bf7-trim-autostart: снова запускается $name"
done

echo "bf7-trim-autostart: погашено записей: $made"
