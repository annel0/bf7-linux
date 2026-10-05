#!/bin/sh
# BF7: спрятать из сетки Phosh служебное и нерабочее, не трогая Xfce.
#
# «Показать все приложения» в Phosh выдавал демонстрации GTK, XTerm, xvkbd,
# Onboard, служебные окна Thunar и Xfce, настройки печати — и три
# программы, которые на этом телефоне не работают (Cheese падает на старом
# clutter-gst, guvcview спотыкается на EGL в SDL2, Megapixels ждёт описания
# сенсора, которого для MT6761 нет; снимает «Фотоаппарат»). Chromium под
# root без --no-sandbox не стартует вовсе, а снимать песочницу у браузера
# под root нельзя; браузеров и так два (bf7-web и Firefox).
#
# Пакетные файлы в /usr/share не правим — их вернёт первое же обновление.
# Кладём подмену с тем же именем в /usr/local/share/applications (он в
# XDG_DATA_DIRS раньше /usr/share) и дописываем Phosh в NotShowIn: GLib
# сверяет это с XDG_CURRENT_DESKTOP (Phosh:GNOME), а в сеансе Xfce всё
# остаётся на месте. Подмена — копия пакетной записи на момент запуска;
# после обновления пакета запустить заново.
#
# Второе дело — русские имена тем, у кого их нет (IMENA):
#   «Mobile Settings» стояла в основной сетке по-английски и обрезанной
#   (других настроек в сетке нет, поэтому просто «Настройки»; «Доп.
#   настройки» обрезалось под значком);
#   Foliate — в основной записи нет русского имени («Библиотека» у пакета
#   относится к его дополнительному действию), теперь «Книги»;
#   Portfolio — файловый менеджер, «Файлы»;
#   Текстовый редактор — «Блокнот»: полное имя под значком обрезалось.
#
# Вызов: bf7-phosh-skryt [--proverka]
set -eu

SPISOK="
org.gtk.Demo4 org.gtk.IconBrowser4 org.gtk.gtk4.NodeEditor
org.gtk.PrintEditor4 org.gtk.WidgetFactory4
display-im6.q16 debian-uxterm debian-xterm xvkbd
onboard onboard-settings
thunar-bulk-rename thunar-settings xfce4-terminal xfce4-appfinder
system-config-printer
org.gnome.Cheese guvcview org.postmarketos.Megapixels chromium
"
# «имя записи|русское имя»
IMENA="
org.sigxcpu.MobileSettings|Настройки
com.github.johnfactotum.Foliate|Книги
dev.tchx84.Portfolio|Файлы
org.gnome.TextEditor|Блокнот
"
# «имя записи|переменная=значение» — окружение для запуска из сетки.
# Калькулятор: при запуске фокус в поле формулы, GTK4 включал text-input, и
# экранная клавиатура закрывала собственные кнопки калькулятора. С модулем
# ввода gtk-im-context-simple GTK4 text-input не включает (проверено
# WAYLAND_DEBUG и камерой 2026-09-30); «simple» — имя из GTK3, GTK4 его не знает.
OKRUZHENIE="
org.gnome.Calculator|GTK_IM_MODULE=gtk-im-context-simple
"
METKA="# BF7: подмена из bf7-phosh-skryt"
SRC=/usr/share/applications
DST=/usr/local/share/applications
proverka=0
[ "${1:-}" = --proverka ] && proverka=1

mkdir -p "$DST"

# Можно ли класть подмену: пакетная запись есть, а в /usr/local нет чужой.
mozhno() {
	[ -f "$SRC/$1.desktop" ] || return 1
	if [ -f "$DST/$1.desktop" ] && ! grep -q "^$METKA" "$DST/$1.desktop"; then
		echo "  пропускаю $1: в $DST лежит чужая подмена"
		return 1
	fi
}

# Положить готовую подмену $2 для записи $1, если она изменилась.
polozhit() {
	if [ -f "$DST/$1.desktop" ] && cmp -s "$2" "$DST/$1.desktop"; then
		rm -f "$2"
	elif [ "$proverka" = 1 ]; then
		echo "  поменял бы: $1 ($3)"
		rm -f "$2"
	else
		install -m 644 "$2" "$DST/$1.desktop"
		rm -f "$2"
		echo "  $3: $1"
	fi
}

for id in $SPISOK; do
	mozhno "$id" || continue
	src=$SRC/$id.desktop
	if grep -q '^OnlyShowIn=' "$src"; then
		echo "  пропускаю $id: у него OnlyShowIn, NotShowIn рядом нельзя"
		continue
	fi
	tmp=$(mktemp)
	{
		echo "$METKA ($src)."
		echo "# Спрятано только из Phosh; править список там, не здесь."
		# NotShowIn дописываем в первую группу [Desktop Entry]: к уже
		# имеющемуся списку либо отдельной строкой в конце группы.
		awk '
			/^\[/ { if (vgruppe && !est) print "NotShowIn=Phosh;"; vgruppe = ($0 == "[Desktop Entry]"); if (!vgruppe) est = 1 }
			vgruppe && /^NotShowIn=/ { est = 1; if ($0 !~ /(=|;)Phosh;/) { sub(/;?$/, ";"); $0 = $0 "Phosh;" } }
			{ print }
			END { if (vgruppe && !est) print "NotShowIn=Phosh;" }
		' "$src"
	} > "$tmp"
	polozhit "$id" "$tmp" "спрятано из Phosh"
done

echo "$IMENA" | while IFS='|' read -r id imya; do
	[ -n "$id" ] || continue
	mozhno "$id" || continue
	src=$SRC/$id.desktop
	tmp=$(mktemp)
	{
		echo "$METKA ($src)."
		echo "# Русское имя для сетки; править список там, не здесь."
		# Name[ru] встаёт сразу за Name= в группе [Desktop Entry],
		# прежний Name[ru] (если был) убирается.
		awk -v imya="$imya" '
			/^\[/ { vgruppe = ($0 == "[Desktop Entry]") }
			vgruppe && /^Name\[ru\]=/ { next }
			{ print }
			vgruppe && /^Name=/ && !gotovo { print "Name[ru]=" imya; gotovo = 1 }
		' "$src"
	} > "$tmp"
	polozhit "$id" "$tmp" "русское имя"
done
echo "$OKRUZHENIE" | while IFS='|' read -r id per; do
	[ -n "$id" ] || continue
	mozhno "$id" || continue
	src=$SRC/$id.desktop
	tmp=$(mktemp)
	{
		echo "$METKA ($src)."
		echo "# Окружение для запуска; править список там, не здесь."
		awk -v per="$per" '
			/^\[/ { vgruppe = ($0 == "[Desktop Entry]") }
			vgruppe && /^Exec=/ && $0 !~ /^Exec=env / { sub(/^Exec=/, "Exec=env " per " ") }
			{ print }
		' "$src"
	} > "$tmp"
	polozhit "$id" "$tmp" "окружение запуска"
done
[ "$proverka" = 1 ] || update-desktop-database "$DST" 2>/dev/null || true
