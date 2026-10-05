#!/bin/sh
# BF7: снимок экрана телефона веб-камерой, обрезанный строго по панели.
#
# Зачем, если есть grim. grim показывает, что композитор СОБРАЛ, а камера —
# что панель ПОКАЗАЛА. Между ними слой показа, MVA и сама матрица.
# Расхождение этих двух снимков — улика, которую нечем больше добыть.
# Первая же проверка так нашла подсветку, стоявшую на 12 из 255.
#
# Вызов:
#   bf7-glaz.sh [выход.png]        — снимок экрана, разогнутый в 360x806
#   bf7-glaz.sh --nastroyka        — калибровка: сам покажет белое поле
#   bf7-glaz.sh --syroj [выход]    — весь кадр целиком, без обрезки
#   bf7-glaz.sh --polnyj [выход]   — весь кадр, уровни по каналам, верхом вверх
#                                    (ТАК И СМОТРЕТЬ — просьба хозяина)
#   BF7_DELITEL=1 bf7-glaz.sh ...  — полное разрешение 720x1612
#
# Углы снимаются заново каждый раз, когда телефон подвинули: старая
# настройка тогда врёт молча, и снимок выходит перекошенным.
set -eu

zdes=$(cd "$(dirname "$0")" && pwd)
ugly=${BF7_UGLY:-$zdes/bf7-glaz-ugly.json}
ustr=${BF7_KAMERA:-/dev/video0}
delitel=${BF7_DELITEL:-2}
telefon=${BF7_TELEFON:-bf7}

# Камера отдаёт первые кадры почти чёрными, пока подбирает выдержку: на
# двадцать пятом средняя яркость 48, на сто двадцатом — 120. Меньше ста
# двадцати брать нельзя, снимок выйдет тёмным. Это четыре секунды.
progrev=${BF7_PROGREV:-120}

vrem=$(mktemp -d)
trap 'rm -rf "$vrem"' EXIT

snyat() {
    # Настройки камеры выставляем каждый раз: их сбрасывает переподключение.
    # 30/57/3 оставлены после опыта с таблицей цветов (2026-09-23): 60/40/4
    # лучше для яркой таблицы, но на тёмном интерфейсе высветляют фон, а
    # пересвет ярких значков идёт от автоэкспозиции в датчике — его эти
    # настройки не лечат (см. bf7-glaz-cvet.py).
    # Окно на мониторе (bf7-glaz-okno.sh) держит камеру само — тогда берём
    # его свежий кадр: прогрев выдержки там уже пройден.
    potok=/tmp/bf7-glaz-potok/poslednij.jpg
    if [ -s "$potok" ] && [ $(( $(date +%s) - $(stat -c %Y "$potok") )) -lt 3 ]; then
        sleep 0.6
        ffmpeg -hide_banner -loglevel error -i "$potok" -y "$1"
        return
    fi

    v4l2-ctl -d "$ustr" -c brightness=30,contrast=57,gamma=3 2>/dev/null || true

    ffmpeg -hide_banner -loglevel error \
        -f v4l2 -input_format yuyv422 -video_size 640x480 -i "$ustr" \
        -vf "select=gte(n\,$progrev)" \
        -frames:v 1 -update 1 -y "$1"
}

pokazat_beloe() {
    # Белое поле на телефоне — мишень для поиска границ экрана. Держим его
    # заведомо дольше прогрева камеры.
    ssh "$telefon" "XDG_RUNTIME_DIR=/run/bf7-phosh WAYLAND_DISPLAY=wayland-0 \
        setsid /usr/local/bin/bf7-zalivka ffffff 25 \
        > /tmp/bf7-zalivka.log 2>&1 < /dev/null &" || {
        echo "bf7-glaz: не смог показать белое поле на телефоне" >&2
        exit 1
    }
    sleep 2
}

case "${1:-}" in
--nastroyka)
    echo "bf7-glaz: шаг 1 — белое поле, ищу границы экрана"
    pokazat_beloe
    snyat "$vrem/kadr.png"
    python3 "$zdes/bf7-glaz.py" nastroit "$vrem/kadr.png" "$ugly"

    # Шаг 2. Границы найдены, но какая сторона верхняя — по белому полю не
    # понять. Снимаем обычный экран сразу двумя способами и сверяем: грим
    # знает правду, камера — нет.
    # Белое поле держится дольше прогрева камеры — к началу второго шага оно
    # ещё на экране. Если его не убрать, оба снимка будут белыми, корреляция
    # выйдет нулевой, и верх определится подбрасыванием монетки.
    echo "bf7-glaz: убираю белое поле"
    ssh "$telefon" 'pkill -x bf7-zalivka' >/dev/null 2>&1 || true
    sleep 2

    echo "bf7-glaz: шаг 2 — сверяю верх с эталоном grim"
    ssh "$telefon" 'XDG_RUNTIME_DIR=/run/bf7-phosh WAYLAND_DISPLAY=wayland-0 \
        /usr/local/bin/bf7-otperet >/dev/null 2>&1; sleep 1; \
        XDG_RUNTIME_DIR=/run/bf7-phosh WAYLAND_DISPLAY=wayland-0 \
        grim /tmp/bf7-ehtalon.png' >/dev/null 2>&1
    scp -q "$telefon:/tmp/bf7-ehtalon.png" "$vrem/ehtalon.png"
    snyat "$vrem/obychnyj.png"
    python3 "$zdes/bf7-glaz.py" verh "$vrem/obychnyj.png" \
        "$vrem/ehtalon.png" "$ugly"
    ;;
--polnyj)
    vyhod=${2:-glaz-polnyj.png}
    snyat "$vrem/kadr.png"
    # В тёмной комнате яркое на экране пересвечено в датчике камеры — снимаем
    # второй раз при приглушённой подсветке и сразу возвращаем её. Выключить:
    # BF7_PRIGLUSHIT=0 (например, когда проверяется сама подсветка).
    cel=$(python3 "$zdes/bf7-glaz-cvet.py" podsvetka "$vrem/kadr.png")
    if [ -n "$cel" ] && [ "${BF7_PRIGLUSHIT:-1}" != 0 ]; then
        podsv=/sys/class/leds/lcd-backlight/brightness
        bylo=$(ssh "$telefon" "cat $podsv")
        if [ "$bylo" -gt "$cel" ]; then
            trap 'ssh "$telefon" "echo $bylo > $podsv"; rm -rf "$vrem"' EXIT
            ssh "$telefon" "echo $cel > $podsv"
            snyat "$vrem/kadr.png"
            ssh "$telefon" "echo $bylo > $podsv"
            trap 'rm -rf "$vrem"' EXIT
            echo "bf7-glaz: темно — снимал при подсветке $cel (вернул $bylo)" >&2
        fi
    fi
    python3 "$zdes/bf7-glaz-cvet.py" ispravit "$vrem/kadr.png" "$vyhod" --stoya
    echo "bf7-glaz: полный кадр в $vyhod"
    ;;
--syroj)
    vyhod=${2:-glaz-syroj.png}
    snyat "$vyhod"
    echo "bf7-glaz: кадр целиком в $vyhod"
    ;;
--sverka)
    # Ради этого всё и затевалось: два независимых свидетеля одного экрана.
    # Низкое сходство при исправном снимке grim означает, что композитор
    # собрал кадр, а до матрицы он дошёл не таким — ошибка в пути показа.
    osnova=${2:-sverka}
    ssh "$telefon" 'XDG_RUNTIME_DIR=/run/bf7-phosh WAYLAND_DISPLAY=wayland-0 \
        grim /tmp/bf7-ehtalon.png' >/dev/null 2>&1 || {
        echo "bf7-glaz: grim отказал — панель, скорее всего, погашена" >&2
    }
    scp -q "$telefon:/tmp/bf7-ehtalon.png" "$osnova-grim.png"
    snyat "$vrem/kadr.png"
    python3 "$zdes/bf7-glaz.py" obrezat "$vrem/kadr.png" "$ugly" \
        "$osnova-kamera.png" "$delitel"
    python3 "$zdes/bf7-glaz.py" sverit "$osnova-kamera.png" "$osnova-grim.png"
    ;;
*)
    vyhod=${1:-glaz.png}
    snyat "$vrem/kadr.png"
    python3 "$zdes/bf7-glaz.py" obrezat "$vrem/kadr.png" "$ugly" \
        "$vyhod" "$delitel"
    ;;
esac
