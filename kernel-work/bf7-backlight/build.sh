#!/bin/sh
# BF7: сборка устройства подсветки поверх светодиода.
#
# Два модуля: сам класс подсветки из дерева ядра (backlight.ko) и наш драйвер
# (bf7_backlight.ko). Образ ядра НЕ пересобирается и не перепрошивается —
# меняется только настройка сборочного каталога, чтобы класс собрался модулем.
#
# Подробности, зачем это всё, — в комментарии к bf7_backlight.c.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
KERNEL_WORK=$(CDPATH= cd -- "$HERE/.." && pwd)
TREE=$KERNEL_WORK/x657b-4.19
KOUT=${KOUT:-$KERNEL_WORK/build/bf7-x657b-clang11-v3}
TOOLCHAIN=$KERNEL_WORK/toolchains/clang-r383902/bin

KBUILD="ARCH=arm64 CC=clang LD=ld.lld AR=llvm-ar NM=llvm-nm OBJCOPY=llvm-objcopy OBJDUMP=llvm-objdump STRIP=llvm-strip CROSS_COMPILE=aarch64-linux-gnu- CLANG_TRIPLE=aarch64-linux-gnu-"

printf 'ядро: %s\n' "$(cat "$KOUT/include/config/kernel.release")"
export PATH="$TOOLCHAIN:$PATH"

# Перечитать настройку, чтобы появился CONFIG_BACKLIGHT_CLASS_DEVICE в
# autoconf.h. Образ при этом не собирается.
make -C "$TREE" O="$KOUT" $KBUILD olddefconfig >/dev/null
make -C "$TREE" O="$KOUT" $KBUILD modules_prepare >/dev/null

echo "собираю класс подсветки"
make -C "$TREE" O="$KOUT" $KBUILD M=drivers/video/backlight modules

echo "собираю наш драйвер"
make -C "$TREE" O="$KOUT" M="$HERE" $KBUILD modules

echo "готово:"
ls -la "$KOUT/drivers/video/backlight/backlight.ko" "$HERE/bf7_backlight.ko" 2>/dev/null
