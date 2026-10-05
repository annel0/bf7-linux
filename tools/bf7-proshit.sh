#!/bin/sh
# BF7: прошить собранное ядро и все модули одним заходом и проверить.
#
# За ночь 2026-09-23 эта цепочка прошла руками пять раз, и однажды модули
# связности оказались собраны против другой сборки ядра (v2 при прошитом
# v3) — встали лишь потому, что совпала строка версии. Здесь это
# проверяется до прошивки, а не после.
#
# Что делает:
#   1. сверяет, что каждый модуль собран против той же сборки ядра;
#   2. раскладывает модули в /lib/modules/<версия>/extra и зовёт depmod;
#   3. собирает загрузочный образ;
#   4. проверяет счётчик слота A;
#   5. входит в fastboot, прошивает boot_a, выбирает слот a, перезагружает;
#   6. ждёт загрузку, Wi-Fi и Bluetooth — с пределами, без вечных ожиданий;
#   7. запускает самопроверку и печатает итог.
#
# Сборку не делает: сначала ядро и модули (см. порядок в
# bf7-kernel-flash-procedure), потом этот сценарий.
#
# Вызов: tools/bf7-proshit.sh
set -eu

KORNI=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
KW=$KORNI/kernel-work
KOUT=${KOUT:-$KW/build/bf7-x657b-clang11-v3}
CN=$KW/build/bf7-connectivity-x657b/src
TELEFON=${BF7_TELEFON:-bf7}
IP_USB=${BF7_IP_USB:-192.168.7.2}

obraz=$KORNI/native-linux-build/alps191-rootfs-v1/bf7-alps191-rootfs-v1.img
vhod_fastboot=$KORNI/native-linux-build/storage-diagnostic-v1/bf7-enter-fastboot.aarch64

MODULI="
$CN/common/wmt_drv.ko
$CN/adaptor/wmt_chrdev_wifi.ko
$CN/gen4m/wlan_drv_gen4m.ko
$KW/bf7-bluetooth/bt_drv.ko
$KW/bf7-gps/gps_drv.ko
$KW/bf7-backlight/bf7_backlight.ko
$KOUT/drivers/video/backlight/backlight.ko
$KW/v4l2loopback/v4l2loopback-0.12.7/v4l2loopback.ko
$KOUT/drivers/bluetooth/hci_vhci.ko
$KOUT/net/bluetooth/rfcomm/rfcomm.ko
$KOUT/net/bluetooth/hidp/hidp.ko
$KW/bf7-torch/bf7_torch.ko
"

skazat() { printf '\n=== %s\n' "$*"; }
provalit() { printf 'bf7-proshit: %s\n' "$*" >&2; exit 1; }

# Ждать условия не дольше предела. Предел обязателен: однажды вечное
# ожидание файла на перезагрузившемся телефоне провисело семь часов.
zhdat() {
	predel=$1; shift
	i=0
	while [ "$i" -lt "$predel" ]; do
		if "$@" >/dev/null 2>&1; then return 0; fi
		sleep 5
		i=$((i + 1))
	done
	return 1
}

skazat "1. сверка модулей с ядром"
versiya=$(cat "$KOUT/include/config/kernel.release")
echo "ядро сборки: $versiya ($KOUT)"
for m in $MODULI; do
	[ -f "$m" ] || provalit "нет модуля $m — сначала собрать"
	vm=$(modinfo -F vermagic "$m" | cut -d' ' -f1)
	[ "$vm" = "$versiya" ] || provalit "$(basename "$m") собран под $vm, а ядро $versiya"
	# Строка версии у разных сборок одинакова — она не спасла, когда модули
	# собрали под v2 при прошитом v3. Сверяем то, что ядро проверит само:
	# контрольные суммы символов, которые модуль берёт у ядра.
	chuzhie=$(modprobe --dump-modversions "$m" 2>/dev/null | awk -v sv="$KOUT/Module.symvers" '
		BEGIN { while ((getline l < sv) > 0) { split(l, f, "\t"); crc[f[2]] = f[1] } }
		($2 in crc) && (tolower(crc[$2]) != tolower($1)) { print $2 }' | head -3)
	[ -z "$chuzhie" ] || provalit "$(basename "$m") собран под другое ядро: не сходятся суммы $chuzhie"
done
echo "все модули под $versiya, контрольные суммы символов сходятся"

skazat "2. модули на телефон"
ping -c 1 -W 3 "$IP_USB" >/dev/null 2>&1 || provalit "телефон не отвечает по USB"
[ "$(ssh -o ConnectTimeout=10 "$TELEFON" uname -r)" = "$versiya" ] ||
	echo "  внимание: на телефоне сейчас другое ядро — модули лягут под $versiya"
for m in $MODULI; do
	scp -q "$m" "$TELEFON:/lib/modules/$versiya/extra/" || provalit "не лёг $(basename "$m")"
done
# Без списка встроенного modprobe не знает, что ledtrig-pattern и прочие
# уже в ядре, и шумит «Failed to find module» на каждый запрос udev.
scp -q "$KOUT/modules.builtin" "$KOUT/modules.order" \
	"$TELEFON:/lib/modules/$versiya/" || provalit "не легли списки модулей"
ssh "$TELEFON" "depmod -a $versiya 2>/dev/null; sync"
echo "разложено модулей: $(echo "$MODULI" | grep -c .)"

skazat "3. загрузочный образ"
KERNEL_IMAGE=$KOUT/arch/arm64/boot/Image \
	sh "$KORNI/native-linux-build/alps191-rootfs-v1/build.sh" 2>&1 |
	grep -E "reverse verification" || provalit "образ не собрался"

skazat "4. слот A"
bajt=$(ssh "$TELEFON" 'dd if=/dev/mmcblk0p3 bs=1 skip=2060 count=1 2>/dev/null | od -A n -t x1' | tr -d ' ')
echo "байт слота A: $bajt (младшие 4 бита — приоритет, следующие 3 — попытки)"

skazat "5. прошивка"
scp -q "$vhod_fastboot" "$TELEFON:/tmp/ef"
ssh "$TELEFON" 'chmod +x /tmp/ef; sync; setsid /tmp/ef >/dev/null 2>&1 &' || true
zhdat 20 sh -c 'sudo fastboot devices | grep -q .' || provalit "телефон не вошёл в fastboot"
sudo fastboot flash boot_a "$obraz" 2>&1 | grep -E "OKAY|FAIL" | tail -1
sudo fastboot set_active a 2>&1 | grep -E "OKAY|FAIL"
sudo fastboot reboot 2>&1 | grep -E "OKAY|FAIL"

skazat "6. загрузка"
sleep 20
zhdat 60 ssh -o ConnectTimeout=4 -o BatchMode=yes "$TELEFON" true ||
	provalit "телефон не вернулся за пять минут — смотреть камерой (tools/bf7-glaz.sh)"
ssh "$TELEFON" 'i=0; while [ $i -lt 30 ] && [ "$(systemctl is-system-running)" = starting ]; do sleep 4; i=$((i+1)); done
	i=0; while [ $i -lt 30 ] && ! hciconfig hci0 2>/dev/null | grep -q "UP RUNNING"; do sleep 2; i=$((i+1)); done
	echo "ядро: $(uname -r), аптайм $(cut -d. -f1 /proc/uptime) с, система: $(systemctl is-system-running)"
	echo "wlan0: $(cat /sys/class/net/wlan0/operstate 2>/dev/null), hci0: $(hciconfig hci0 2>/dev/null | grep -oE "UP RUNNING|DOWN" | head -1)"'

skazat "7. самопроверка"
ssh "$TELEFON" '/usr/local/bin/bf7-selftest 2>&1 | grep -E "\[-\]|Итог"'
