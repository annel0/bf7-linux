#!/bin/sh
# BF7: поправки к русским переводам — то, что не влезает в экран 360 точек
# или не переведено вовсе.
#
#   phosh         «Показать приложения только для мобильных устройств»
#                 (51 знак) выходила обрезанной посередине —
#                 «Показать прилож… льных устройств»; «Портретная» и
#                 «Ландшафтная» на плитке поворота — «Пор… тная»;
#   purism-chatty «Start Chatting» на пустом экране «Чатов» в переводе
#                 chatty 0.7.0 отсутствует — висело по-английски;
#   phosh         заголовок «Notifications» в шторке — в переводе phosh
#                 0.24 его нет (2026-09-25);
#   purism-chatty метки времени в переписке («7 minutes ago», «Just Now»,
#                 «Today/Yesterday %H∶%M») — в переводе 0.7.0 их нет.
#
# Пакетный .mo не правим на месте — его вернёт переустановка пакета. Он
# отводится dpkg-divert в *.debian, а сюда кладётся собранный из него же с
# нашими заменами. После обновления пакета запустить снова: соберёт заново из
# нового *.debian.
#
# Вызов: bf7-perevody [--proverka]
set -eu

proverka=0
[ "${1:-}" = --proverka ] && proverka=1
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# Замены: домен<TAB>msgid<TAB>наш перевод. Строки без msgid в переводе
# дописываются. Множественное число — шесть полей: домен, msgid, форма для 1,
# msgid_plural, форма для 2–4, форма для 5+ (так их и ждёт Plural-Forms ru).
# Двоеточие в метках времени chatty — «∶» (U+2236), как в исходнике.
cat > "$tmp/zameny" <<'EOF'
phosh	Show Only Mobile Friendly Apps	Только мобильные приложения
phosh	Portrait	Портрет
phosh	Landscape	Альбом
purism-chatty	Start Chatting	Начните переписку
phosh	Notifications	Уведомления
purism-chatty	Just Now	Только что
purism-chatty	Today %H∶%M	Сегодня %H∶%M
purism-chatty	Yesterday %H∶%M	Вчера %H∶%M
purism-chatty	%lu minute ago	%lu минуту назад	%lu minutes ago	%lu минуты назад	%lu минут назад
EOF

for domen in $(cut -f1 "$tmp/zameny" | sort -u); do
	MO=/usr/share/locale/ru/LC_MESSAGES/$domen.mo
	[ -f "$MO" ] || [ -f "$MO.debian" ] || continue
	if ! dpkg-divert --list "$MO" | grep -q .; then
		if [ "$proverka" = 1 ]; then
			echo "  не отведён: $MO"
			continue
		fi
		dpkg-divert --add --rename --divert "$MO.debian" "$MO" >/dev/null
		echo "  отведён пакетный $MO"
	fi
	msgunfmt "$MO.debian" -o "$tmp/ru.po"
	# Таблица — файлом, не через конвейер: «python3 -» читает сам скрипт из
	# heredoc, и конвейер на stdin он бы молча потерял (так и было).
	grep "^$domen	" "$tmp/zameny" > "$tmp/moi"
	python3 - "$tmp/ru.po" "$tmp/moi" <<'PY'
import re, sys
put = sys.argv[1]
s = open(put, encoding="utf-8").read()
for stroka in open(sys.argv[2], encoding="utf-8").read().splitlines():
    polya = stroka.split("\t")
    if len(polya) == 6:
        _, msgid, f0, plural, f1, f2 = polya
        if 'msgid "%s"\nmsgid_plural' % msgid not in s:
            s += ('\nmsgid "%s"\nmsgid_plural "%s"\nmsgstr[0] "%s"\nmsgstr[1] "%s"\n'
                  'msgstr[2] "%s"\n' % (msgid, plural, f0, f1, f2))
        continue
    _, msgid, perevod = polya
    kl = re.escape(msgid)
    shablon = re.compile(r'(msgid "%s"\nmsgstr )(?:"[^\n]*"\n)+' % kl)
    s, n = shablon.subn(lambda m: m.group(1) + '"%s"\n' % perevod, s)
    if n == 0:
        s += '\nmsgid "%s"\nmsgstr "%s"\n' % (msgid, perevod)
open(put, "w", encoding="utf-8").write(s)
PY
	msgfmt -o "$tmp/$domen.mo" "$tmp/ru.po"
	if [ -f "$MO" ] && cmp -s "$tmp/$domen.mo" "$MO"; then
		continue
	fi
	if [ "$proverka" = 1 ]; then
		echo "  отличается: $MO"
	else
		install -m 644 "$tmp/$domen.mo" "$MO"
		echo "  перевод $domen поправлен (перезапуск программы подхватит)"
	fi
done
