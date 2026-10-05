#!/bin/sh
# BF7: тишина — выключить и вернуть звук телефона.
#
#   bf7-tishina vkl      — звук выключить: выход PulseAudio bf7out без звука,
#                          feedbackd в профиль quiet (звонок и уведомления —
#                          только вибрация и огонёк)
#   bf7-tishina vykl     — вернуть: звук включён, профиль full
#   bf7-tishina do HH:MM — выключить сейчас и вернуть в HH:MM (таймер systemd)
#   bf7-tishina          — показать состояние
#
# Звук разговора идёт мимо PulseAudio (bf7-rech → звуковой HAL модема), так
# что автоответ хозяину и голос Клауда в трубке тишина не трогает.
#
# Адрес шины сеанса берётся у запущенной оболочки каждый раз заново: к утру
# сеанс мог перезапуститься, и старый адрес был бы уже неверен.
set -eu

sreda() {
    p=$(pgrep -f '^/usr/libexec/phosh' | head -1) || true
    if [ -z "$p" ]; then
        echo "bf7-tishina: оболочка не запущена — профиль feedbackd не меняю" >&2
        return 1
    fi
    export $(tr '\0' '\n' < /proc/$p/environ | grep -E '^(DBUS_SESSION_BUS_ADDRESS|XDG_RUNTIME_DIR)=')
}

export PULSE_SERVER=unix:/run/bf7-pulse/native

pokazat() {
    echo "выход bf7out: $(pactl get-sink-mute bf7out 2>/dev/null || echo '?')"
    if sreda; then
        echo "профиль feedbackd: $(gsettings get org.sigxcpu.feedbackd profile)"
    fi
    systemctl list-timers --no-pager 2>/dev/null | grep bf7-tishina || true
}

case "${1:-}" in
vkl)
    pactl set-sink-mute bf7out 1
    if sreda; then gsettings set org.sigxcpu.feedbackd profile quiet; fi
    echo "bf7-tishina: звук выключен"
    ;;
vykl)
    pactl set-sink-mute bf7out 0
    if sreda; then gsettings set org.sigxcpu.feedbackd profile full; fi
    # Таймер своё отработал (или звук вернули руками) — убрать, чтобы не висел.
    if [ -f /etc/systemd/system/bf7-tishina-utro.timer ]; then
        systemctl disable bf7-tishina-utro.timer >/dev/null 2>&1 || true
        rm -f /etc/systemd/system/bf7-tishina-utro.timer /etc/systemd/system/bf7-tishina-utro.service
        systemctl daemon-reload
    fi
    echo "bf7-tishina: звук включён"
    ;;
do)
    vremya=${2:?"нужно время: bf7-tishina do 09:00"}
    "$0" vkl
    # Ближайшее HH:MM (сегодня, если ещё не прошло, иначе завтра).
    cel=$(date -d "$vremya" +%s)
    [ "$cel" -gt "$(date +%s)" ] || cel=$((cel + 86400))
    kogda=$(date -d "@$cel" '+%Y-%m-%d %H:%M:%S')
    # Таймер — файлами в /etc, а не systemd-run: профиль feedbackd «quiet»
    # лежит в dconf и переживает перезагрузку, а временный таймер — нет, и
    # звонок остался бы беззвучным навсегда. Persistent=true: если телефон
    # проспал срок (выключен, перезагружался), возврат выполнится при запуске.
    cat > /etc/systemd/system/bf7-tishina-utro.service <<EOT
[Unit]
Description=BF7: вернуть звук после ночной тишины
After=bf7-phosh.service bf7-pulse.service

[Service]
Type=oneshot
# Сеанс оболочки после загрузки поднимается не сразу — даём ему время.
ExecStartPre=/bin/sh -c 'for i in \$\$(seq 1 60); do pgrep -f "^/usr/libexec/phosh" >/dev/null && exit 0; sleep 2; done'
ExecStart=/usr/local/bin/bf7-tishina vykl
EOT
    cat > /etc/systemd/system/bf7-tishina-utro.timer <<EOT
[Unit]
Description=BF7: конец ночной тишины ($kogda)

[Timer]
OnCalendar=$kogda
Persistent=true
AccuracySec=1s

[Install]
WantedBy=timers.target
EOT
    systemctl stop bf7-tishina-utro.timer 2>/dev/null || true
    systemctl reset-failed bf7-tishina-utro.service bf7-tishina-utro.timer 2>/dev/null || true
    systemctl daemon-reload
    systemctl enable --now bf7-tishina-utro.timer >/dev/null 2>&1
    echo "bf7-tishina: звук вернётся $kogda"
    ;;
"")
    pokazat
    ;;
*)
    sed -n '2,11p' "$0" >&2
    exit 2
    ;;
esac
