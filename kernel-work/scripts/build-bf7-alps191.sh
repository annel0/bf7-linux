#!/bin/bash
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
KERNEL_WORK=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
PROJECT_DIR=$(CDPATH= cd -- "$KERNEL_WORK/.." && pwd)
TREE=$KERNEL_WORK/mediatek-mt6761-alps188-gpu115
CONFIG=$KERNEL_WORK/configs/bf7-alps191-v1.config
TOOLCHAIN=$KERNEL_WORK/toolchains/clang-r383902/bin
OUT=${OUT:-$KERNEL_WORK/build/bf7-alps191-gpu115-clang11-v1}
LOG=$OUT/build-clang11-final.log

for required in "$TREE/Makefile" "$CONFIG" "$TOOLCHAIN/clang" \
    "$TOOLCHAIN/ld.lld"; do
    if [ ! -e "$required" ]; then
        printf 'missing required input: %s\n' "$required" >&2
        exit 1
    fi
done

mkdir -p "$OUT"
# Always refresh .config from the tracked one: keeping a stale $OUT/.config
# silently builds a different kernel than the config the caller edited.
cp "$CONFIG" "$OUT/.config"

export PATH="$TOOLCHAIN:$PATH"
export KBUILD_BUILD_TIMESTAMP='Sat Dec 7 22:52:37 CST 2024'
export KBUILD_BUILD_USER=bf7
export KBUILD_BUILD_HOST=native-linux
export KBUILD_BUILD_VERSION=1
export LOCALVERSION=

# CONFIG_DEBUG_INFO records the absolute O= directory in DWARF.  LLD then
# hashes those debug sections into the kernel and embedded vDSO build IDs,
# making otherwise identical Image files differ by 16 bytes.  Normalize only
# that build-directory prefix; this does not suppress build IDs or debug data.
export KCFLAGS="-fdebug-prefix-map=$OUT=/bf7-build -fmacro-prefix-map=$OUT=/bf7-build"
export KAFLAGS="-fdebug-prefix-map=$OUT=/bf7-build -fmacro-prefix-map=$OUT=/bf7-build"

common_args="O=$OUT ARCH=arm64 CC=clang LD=ld.lld AR=llvm-ar NM=llvm-nm OBJCOPY=llvm-objcopy OBJDUMP=llvm-objdump STRIP=llvm-strip CROSS_COMPILE=aarch64-linux-gnu- CLANG_TRIPLE=aarch64-linux-gnu-"

# shellcheck disable=SC2086
make -C "$TREE" $common_args olddefconfig

jobs=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '8')}
case "$jobs" in
    ''|*[!0-9]*) jobs=8 ;;
esac

set -o pipefail
# shellcheck disable=SC2086
make -C "$TREE" $common_args -j"$jobs" Image modules 2>&1 | tee "$LOG"

printf 'kernel release: %s\n' "$(cat "$OUT/include/config/kernel.release")"
sha256sum "$OUT/arch/arm64/boot/Image" "$OUT/vmlinux" "$OUT/.config"
