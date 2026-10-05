#!/bin/sh
# BF7: обновить список правил блокировщика рекламы на телефоне.
#
# Зачем это отдельно. Epiphany по умолчанию сам качает список с
# easylist-downloads.adblockplus.org — но с телефона этот узел недоступен:
# TCP-соединение открывается за десятые доли секунды, а рукопожатие TLS
# истекает по сроку. MTU ни при чём, проверено (пакет в 1472 байта проходит
# без фрагментации), и остальной интернет работает.
#
# **Чем это грозило.** Пока список не готов, Epiphany откладывает КАЖДОЕ
# решение о переходе: `decide_policy_cb` в `src/ephy-window.c` кладёт запрос
# в очередь и возвращает TRUE, пока не придёт «notify::is-initialized».
# Список не приходит никогда — и браузер не показывает ни одной страницы,
# включая собственную внутреннюю. Внешне это выглядело как «браузер не
# рисует», и вся вина много часов возлагалась на ускорение и на WebKit.
#
# Поэтому список качает ХОЗЯЙСКАЯ машина и кладёт телефону рядом, а
# настройка `content-filters` указывает на местный файл. Качать больше не
# нужно, а раз в несколько месяцев стоит обновить — списки стареют.
#
# Вызов: bf7-adblock-obnovit.sh [имя-телефона-для-ssh]
set -eu

telefon=${1:-bf7}
adres=${BF7_SPISOK:-https://easylist-downloads.adblockplus.org/easylist_min_content_blocker.json}
imya=easylist_min_content_blocker.json
kuda=/usr/local/share/bf7

vrem=$(mktemp -d)
trap 'rm -rf "$vrem"' EXIT

echo "качаю список правил"
if ! python3 - "$adres" "$vrem/$imya" <<'PY'
import sys, urllib.request
adres, kuda = sys.argv[1], sys.argv[2]
with urllib.request.urlopen(adres, timeout=90) as r:
    dannye = r.read()
if len(dannye) < 100000:
    raise SystemExit("список подозрительно мал: %d байт" % len(dannye))
open(kuda, "wb").write(dannye)
print("получено %d байт" % len(dannye))
PY
then
    echo "не вышло скачать список; старый на телефоне остаётся нетронутым" >&2
    exit 1
fi

ssh "$telefon" "mkdir -p $kuda"
scp -q "$vrem/$imya" "$telefon:$kuda/"

# Скомпилированный вид кэшируется по содержимому списка, поэтому после смены
# файла первая загрузка браузера снова займёт около двух минут: WebKit
# перегоняет шесть с лишним мегабайт правил в двоичный вид (у нас вышло
# двадцать мегабайт). Дальше — мгновенно.
ssh "$telefon" 'rm -rf /root/.cache/epiphany/adblock'

echo "список положен в $kuda, кэш сброшен"
echo "первый запуск браузера после этого займёт около двух минут"
