#!/bin/sh
# Build the BF7 ALPS191 "rootfs trial v1" boot image: candidate kernel +
# known-good switch_root initramfs (mark-success removed, root ro by default).
# Output is a normal header-v2 boot image for slot A; nothing here touches the phone.
set -eu
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
NATIVE_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
PROJECT_DIR=$(CDPATH= cd -- "$NATIVE_DIR/.." && pwd)
ROOTFS=$NATIVE_DIR/initramfs-root
INIT=$SCRIPT_DIR/init
KERNEL_IMAGE=${KERNEL_IMAGE:-$PROJECT_DIR/kernel-work/build/bf7-alps191-gpu115-clang11-v1/arch/arm64/boot/Image}
DTB=$NATIVE_DIR/kernel-boot-test-preflight-20260822/original-native/dtb
OUTPUT=$SCRIPT_DIR/bf7-alps191-rootfs-v1.img
KERNEL_GZIP=$SCRIPT_DIR/Image-bf7-alps191-v1.gz
RAMDISK=$SCRIPT_DIR/ramdisk-bf7-alps191-rootfs-v1.cpio.gz
VERIFY=$SCRIPT_DIR/verified-unpack
HEADER=$SCRIPT_DIR/header.txt
MANIFEST=$SCRIPT_DIR/SHA256SUMS
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

for f in "$ROOTFS/init" "$INIT" "$KERNEL_IMAGE" "$DTB"; do
    [ -e "$f" ] || { printf 'missing: %s\n' "$f" >&2; exit 1; }
done
sh -n "$INIT"

mkdir -p "$WORK/root"
cp -a "$ROOTFS/." "$WORK/root/"
rm -f "$WORK/root/status.bin"
cp "$INIT" "$WORK/root/init"
chmod 0755 "$WORK/root/init"
# Extra tools for kernel debugging (Alpine aarch64/musl packages): dropbear
# (real SSH instead of telnet), strace, i2c-tools, ethtool, tcpdump, kmod.
# Everything lives in RAM; the overlay never touches eMMC.
[ -d "$SCRIPT_DIR/overlay" ] && cp -a "$SCRIPT_DIR/overlay/." "$WORK/root/"

# raw reboot(RESTART2,"bootloader") helper, useful in the bf7.noroot=1 mode
install -m 0755 "$NATIVE_DIR/storage-diagnostic-v1/bf7-enter-fastboot.aarch64" \
    "$WORK/root/bin/bf7-enter-fastboot"
# Belt and braces: the mark-success helper must not exist in this ramdisk.
rm -f "$WORK/root/bin/bf7-mark-boot-success"
grep -vE "^\s*#" "$WORK/root/init" | grep -qE "bf7-mark-boot-success" && { echo "init still references mark-success" >&2; exit 1; }

gzip -9n -c "$KERNEL_IMAGE" > "$KERNEL_GZIP"
(
    cd "$WORK/root"
    find . -print0 | LC_ALL=C sort -z |
        cpio --null -o -H newc --owner=0:0 --reproducible 2> "$WORK/cpio.log"
) | gzip -9n > "$RAMDISK"

# Same cmdline as trial-v3 plus rootwait; root itself is mounted by our init.
# cma=128M: wlroots (phoc/phosh) выделяет буферы показа через DRM dumb, а это
# CMA; по умолчанию его 16 МБ и на второй буфер 720x1612 уже не хватает.
#
# quiet вместо «loglevel=8 ignore_loglevel» (2026-09-23). Загрузчик добавляет
# ещё две консоли, console=tty0 и console=ttyS0,921600, и с ignore_loglevel
# КАЖДОЕ сообщение ядра, отладочные тоже, синхронно печаталось в три места:
# отрисовкой текста на панели, в отладочный UART на 921600 (к нему ничего не
# подключено) и в USB-последовательный порт. Каждая строка состояния systemd
# стоила 180–240 мс: драйвер USB_ACM на каждую запись печатает свою
# отладочную строку, и она шла тем же путём. PID 1 стоял, службы запускались
# пачками с опозданием на секунды. С quiet на консоли идут только
# предупреждения и ошибки, systemd показывает состояние лишь у медленных
# служб; dmesg и журнал хранят всё, как прежде (log_buf_len=16M).
#
# ramoops.record_size=0x20000: посмертная запись паники (dmesg-ramoops)
# берёт 128 КБ хвоста журнала ядра со ВСЕМИ уровнями, а не 4 КБ. Раньше
# предысторию паники давал console-ramoops, но при quiet в нём только
# предупреждения — сама паника туда всё равно попадает полностью (ядро
# переключает консоль в подробный режим в oops_enter/panic).
#
# Подробный вывод на консоли, если он нужен для разбора незагрузки:
#   BF7_PODROBNO=1 sh build.sh
# Образ «только ядро» ниже всегда подробный — он для отладки и есть.
#
# psi=0 (2026-09-23): учёт «давления» ресурсов никто не читает (systemd-oomd
# не стоит, /proc/pressure никем не открыт), а он будил процессор ~5,6 раза в
# секунду (psi_avgs_work по каждой из ~50 cgroup) и добавлял работы
# планировщику на каждой смене состояния задачи.
CMDLINE_BASE='bootopt=64S3,32N2,64N2 buildvariant=user security=none log_buf_len=16M printk.time=1 hung_task_timeout_secs=30 panic=20 panic_on_warn=0 console=ttyGS0,115200 fsck.mode=skip cma=128M ramoops.record_size=0x20000 psi=0'
CMDLINE_PODROBNO="$CMDLINE_BASE loglevel=8 ignore_loglevel"
if [ "${BF7_PODROBNO:-0}" = 1 ]; then
    CMDLINE=$CMDLINE_PODROBNO
else
    # loglevel=3 (2026-09-23): при одном quiet на экран загрузки шли ошибки
    # драйверов — камерные «Driver 'MAINAF' is already registered», extcon,
    # «autofs4» от systemd; хозяин сфотографировал их, приняв за поломку.
    # Теперь на экран только критическое (паника и так выводится вся), а
    # dmesg и журнал хранят всё.
    CMDLINE="$CMDLINE_BASE quiet loglevel=3"
fi

mkbootimg --header_version 2 --os_version 12.0.0 \
    --os_patch_level 2024-12 --kernel "$KERNEL_GZIP" --ramdisk "$RAMDISK" \
    --dtb "$DTB" --pagesize 2048 --base 0x00000000 \
    --kernel_offset 0x40080000 --ramdisk_offset 0x51b00000 \
    --second_offset 0x00000000 --tags_offset 0x47880000 \
    --dtb_offset 0x47880000 --board CY-BF7-H6127 --cmdline "$CMDLINE" \
    --output "$OUTPUT"

rm -rf -- "$VERIFY"; mkdir -p "$VERIFY"
unpack_bootimg --boot_img "$OUTPUT" --out "$VERIFY" --format=mkbootimg > "$HEADER"
cmp "$VERIFY/kernel" "$KERNEL_GZIP"
cmp "$VERIFY/ramdisk" "$RAMDISK"
cmp "$VERIFY/dtb" "$DTB"
echo "reverse verification: kernel, ramdisk and dtb match inputs"
size=$(stat -c %s "$OUTPUT")
[ "$size" -le 33554432 ] || { echo "image exceeds 32 MiB boot partition" >&2; exit 1; }
(
    cd "$SCRIPT_DIR"
    sha256sum "$(basename "$OUTPUT")" "$(basename "$KERNEL_GZIP")" "$(basename "$RAMDISK")" \
        "$(realpath --relative-to=. "$KERNEL_IMAGE")" "$(realpath --relative-to=. "$DTB")" > "$MANIFEST"
)
printf 'size: %s bytes\n' "$size"
cat "$MANIFEST"

# --- kernel-only variant: same kernel+ramdisk, cmdline keeps us in the initramfs ---
KONLY_OUT=$SCRIPT_DIR/bf7-alps191-kernelonly.img
mkbootimg --header_version 2 --os_version 12.0.0 \
    --os_patch_level 2024-12 --kernel "$KERNEL_GZIP" --ramdisk "$RAMDISK" \
    --dtb "$DTB" --pagesize 2048 --base 0x00000000 \
    --kernel_offset 0x40080000 --ramdisk_offset 0x51b00000 \
    --second_offset 0x00000000 --tags_offset 0x47880000 \
    --dtb_offset 0x47880000 --board CY-BF7-H6127 \
    --cmdline "$CMDLINE_PODROBNO bf7.noroot=1" --output "$KONLY_OUT"
printf 'kernel-only image: %s\n' "$(sha256sum "$KONLY_OUT")"
