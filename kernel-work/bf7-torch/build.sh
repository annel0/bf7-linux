#!/bin/sh
# BF7: сборка фонарика — светодиод вспышки как устройство класса leds.
# Подробности — в комментарии к bf7_torch.c. Образ ядра не пересобирается.
#
# KOUT обязан указывать на ту же сборку ядра, что прошита (см. заметку в
# scripts/build-bf7-connectivity-x657b.sh — там это уже дважды устаревало).
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
KERNEL_WORK=$(CDPATH= cd -- "$HERE/.." && pwd)
TREE=$KERNEL_WORK/x657b-4.19
KOUT=${KOUT:-$KERNEL_WORK/build/bf7-x657b-clang11-v3}
TOOLCHAIN=$KERNEL_WORK/toolchains/clang-r383902/bin

KBUILD="ARCH=arm64 CC=clang LD=ld.lld AR=llvm-ar NM=llvm-nm OBJCOPY=llvm-objcopy OBJDUMP=llvm-objdump STRIP=llvm-strip CROSS_COMPILE=aarch64-linux-gnu- CLANG_TRIPLE=aarch64-linux-gnu-"

printf 'ядро: %s\n' "$(cat "$KOUT/include/config/kernel.release")"
export PATH="$TOOLCHAIN:$PATH"
make -C "$TREE" O="$KOUT" M="$HERE" $KBUILD modules
printf 'готово: %s\n' "$HERE/bf7_torch.ko"
