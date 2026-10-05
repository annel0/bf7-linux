#!/bin/sh
# Start the stock BF7 MediaTek Wi-Fi stack under native Debian.
set -u

log() { printf '%s bf7-wifi: %s\n' "$(date -u +%FT%TZ)" "$*" >> /var/log/bf7-wifi.log; }
load_module() {
    name=$1
    file=$2
    grep -q "^${name} " /proc/modules || /usr/sbin/insmod "$file"
}

# Откуда брать модули связности.
#
# В /opt/bf7/wifi лежат СТОКОВЫЕ вендорские модули — они собраны под чужое
# ядро (vermagic 4.19.191-g216400aba14e-dirty) и на нашем не встанут никогда:
# insmod отвечает «Invalid module format». Наши собственные, пересобранные
# под текущее ядро, живут в /lib/modules/<версия>/extra.
#
# Эта строка однажды уже стоила вечера: я поставил на телефон устаревшую
# копию этого скрипта из дерева, где путь был жёстко прописан на
# /opt/bf7/wifi, — и Wi-Fi с Bluetooth не поднялись после прошивки. Выглядело
# как «пересобранные модули не работают», а на деле их никто не грузил.
# Поэтому путь один, в переменной, и берётся от текущего ядра.
WIFI_MOD_DIR=${WIFI_MOD_DIR:-/lib/modules/$(uname -r)/extra}

mkdir -p /run/bf7-wifi /tmp/bf7-wifi /var/log
[ -c /dev/wmtdetect ] || mknod -m 660 /dev/wmtdetect c 154 0

# connfem у нас своей сборки нет, и он не нужен: Wi-Fi поднимается без него.
# Грузим, только если он вдруг появится, и молча.
[ -f "$WIFI_MOD_DIR/connfem.ko" ] && load_module connfem "$WIFI_MOD_DIR/connfem.ko"

load_module wmt_drv "$WIFI_MOD_DIR/wmt_drv.ko"

export LD_LIBRARY_PATH=/system/lib64:/apex/com.android.runtime/lib64/bionic
unset LD_PRELOAD
/vendor/bin/wmt_loader >/var/log/wmt-loader.log 2>&1 || log 'wmt_loader completed with its expected non-zero Android-property status'

[ -c /dev/stpwmt ] || mknod -m 660 /dev/stpwmt c 190 0
export LD_PRELOAD=/opt/bf7/fake-android-props.so
/vendor/bin/wmt_launcher -p /vendor/firmware/ -o 1 >/var/log/wmt-launcher.log 2>&1 &
sleep 2
unset LD_PRELOAD

load_module wmt_chrdev_wifi "$WIFI_MOD_DIR/wmt_chrdev_wifi.ko"
load_module wlan_drv_gen4m "$WIFI_MOD_DIR/wlan_drv_gen4m.ko"

[ -e /dev/wmtWifi ] && [ ! -c /dev/wmtWifi ] && rm -f /dev/wmtWifi
if [ ! -c /dev/wmtWifi ]; then
    devno=$(cat /sys/class/wmtWifi/wmtWifi/dev)
    mknod -m 660 /dev/wmtWifi c "${devno%:*}" "${devno#*:}"
fi

# /vendor — оверлей только для чтения (нижний слой /vendor.local), запись в
# него давала «Read-only file system» на каждой загрузке. Файлы лежат в
# /vendor.local/firmware; обновляем их там, только если источник изменился.
for pair in /vendor/firmware/wifi.cfg:wifi_sigma.cfg /opt/bf7/txpowerctrl.cfg:txpowerctrl.cfg; do
    src=${pair%%:*}
    dst=/vendor.local/firmware/${pair#*:}
    [ -f "$src" ] || continue
    cmp -s "$src" "$dst" || cp "$src" "$dst"
done
printf '1' > /dev/wmtWifi
ip link set wlan0 up

# Убавить болтливость драйвера.
#
# Заводской уровень 0x2f — со всеми сообщениями уровня INFO. Это около
# тысячи строк в минуту: опрос таймеров, выгрузка счётчиков, разбор каждого
# ARP-запроса. Четверть всего журнала ядра и заметный износ флеша, а
# полезного в них нет ничего.
#
# С 2026-09-23 ставим 0x01 — только ошибки. На 0x03 драйвер предупреждал о
# каждом отброшенном пакете («qmHandleRxPackets:(QM WARN) Mark NULL the Packet
# for Dropped Packet»), это около 15 строк в минуту в журнал; связь на 0x01
# работает так же (проверено: ping шлюза 20 мс).
#
# Раньше ставили 0x03 — только ошибки и предупреждения. Внимание: 0x07 (добавить
# «состояние») **не помогает**, строки INFO при нём продолжают идти, хотя по
# разрядам не должны: DBG_CLASS_INFO это BIT(5). Объяснения у меня нет,
# значение подобрано опытом. Если однажды понадобится подробность —
# 0x2f возвращает всё как было.
if [ -w /proc/net/wlan/dbgLevel ]; then
    i=0
    while [ "$i" -lt 32 ]; do
        printf '0x%02x:0x01\n' "$i" > /proc/net/wlan/dbgLevel 2>/dev/null
        i=$((i + 1))
    done
    log 'wlan debug level lowered to 0x01 (errors only)'
fi

# Ядро связности (WMT) и транспорт STP тоже пишут на уровне INFO — при
# трафике каждые три секунды строку про опрос температуры чипа. Уровень
# меняется только через /proc/driver/wmt_dbg, и только после «ключа»
# 0xDB9DB9. Команда 9 — уровень WMT, a — STP; 1 = предупреждения и ошибки,
# 2 — как было. Файл пишет только root.
if [ -w /proc/driver/wmt_dbg ]; then
    printf '0xDB9DB9' > /proc/driver/wmt_dbg
    printf '9 1' > /proc/driver/wmt_dbg
    printf 'a 1' > /proc/driver/wmt_dbg
    log 'WMT/STP debug level lowered to 1 (warnings and errors)'
fi

log 'wlan0 driver stack ready; bf7-wpa.service will start the supplicant'
