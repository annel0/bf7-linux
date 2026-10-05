#!/bin/sh
# BF7: сборка драйвера Bluetooth. Подробности — в Makefile и в bt.h.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
KERNEL_WORK=$(CDPATH= cd -- "$HERE/.." && pwd)
TREE=$KERNEL_WORK/x657b-4.19
KOUT=${KOUT:-$KERNEL_WORK/build/bf7-x657b-clang11-v3}
CONN=$KERNEL_WORK/donors/motorola-mt6761-connectivity-sta32
SYMS=$KERNEL_WORK/build/bf7-connectivity-x657b/src/common/Module.symvers
TOOLCHAIN=$KERNEL_WORK/toolchains/clang-r383902/bin

KBUILD="ARCH=arm64 CC=clang LD=ld.lld AR=llvm-ar NM=llvm-nm OBJCOPY=llvm-objcopy OBJDUMP=llvm-objdump STRIP=llvm-strip CROSS_COMPILE=aarch64-linux-gnu- CLANG_TRIPLE=aarch64-linux-gnu-"

printf 'ядро: %s\n' "$(cat "$KOUT/include/config/kernel.release")"
export PATH="$TOOLCHAIN:$PATH"

EXTRA=""
[ -f "$SYMS" ] && EXTRA="KBUILD_EXTRA_SYMBOLS=$SYMS"

make -C "$TREE" O="$KOUT" M="$HERE" BF7_CONN="$CONN" $KBUILD $EXTRA modules
echo "готово: $HERE/bt_drv.ko"
