#!/bin/bash
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
KERNEL_WORK=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
PROJECT_DIR=$(CDPATH= cd -- "$KERNEL_WORK/.." && pwd)
TREE=$KERNEL_WORK/mediatek-mt6761-alps188-gpu115
OUT=${KERNEL_OUT:-$KERNEL_WORK/build/bf7-alps191-gpu115-clang11-v1}
SOURCE=$KERNEL_WORK/donors/awinic-aw322xx/AW32257_Driver_MTK_V1.2.0/aw32257
ARCHIVE=$KERNEL_WORK/donors/awinic-aw322xx/AW322XX_Driver_MTK_V1.0.0.zip
TOOLCHAIN=$KERNEL_WORK/toolchains/clang-r383902/bin
STAGE=$PROJECT_DIR/native-linux-build/alps191-aw32257-module-v1

for required in "$TREE/Makefile" "$OUT/.config" "$OUT/Module.symvers" \
    "$SOURCE/Makefile" "$SOURCE/aw32257.c" "$SOURCE/aw32257.h" \
    "$ARCHIVE" "$TOOLCHAIN/clang"; do
    if [ ! -e "$required" ]; then
        printf 'missing required input: %s\n' "$required" >&2
        exit 1
    fi
done

printf '%s  %s\n' \
    975e7fa29c73f1968737112786df7b348a49306675cb0eb1de3de04843e38f4f \
    "$ARCHIVE" | sha256sum -c -
printf '%s  %s\n' \
    cc45c9613a5fc8984da040e7d2f7912cbe2f9219e6b067060477be52146be031 \
    "$SOURCE/aw32257.c" | sha256sum -c -
printf '%s  %s\n' \
    8781cc5209d08fdeb9f5e5309d24f8ed915232883487852b920e0a90b0fce5aa \
    "$SOURCE/aw32257.h" | sha256sum -c -

export PATH="$TOOLCHAIN:$PATH"
export KBUILD_BUILD_TIMESTAMP='Sat Dec 7 22:52:37 CST 2024'
export KBUILD_BUILD_USER=bf7
export KBUILD_BUILD_HOST=native-linux
export KBUILD_BUILD_VERSION=1
export LOCALVERSION=

common_args=(
    -C "$TREE"
    O="$OUT"
    ARCH=arm64
    CC=clang
    LD=ld.lld
    AR=llvm-ar
    NM=llvm-nm
    OBJCOPY=llvm-objcopy
    OBJDUMP=llvm-objdump
    STRIP=llvm-strip
    CROSS_COMPILE=aarch64-linux-gnu-
    CLANG_TRIPLE=aarch64-linux-gnu-
    M="$SOURCE"
    CONFIG_CHARGER_AW32257=m
    EXTRA_CFLAGS=-I$TREE/drivers/power/supply
)

mkdir -p "$STAGE/logs"
make "${common_args[@]}" clean >/dev/null
set -o pipefail
make "${common_args[@]}" modules 2>&1 | tee "$STAGE/logs/build.log"
install -m 0644 "$SOURCE/aw32257.ko" "$STAGE/aw32257.ko"
"$TOOLCHAIN/llvm-strip" --strip-debug "$STAGE/aw32257.ko"

release=$(cat "$OUT/include/config/kernel.release")
actual=$(modinfo -F vermagic "$STAGE/aw32257.ko" | cut -d' ' -f1)
[ "$actual" = "$release" ] || {
    printf 'vermagic mismatch: %s != %s\n' "$actual" "$release" >&2
    exit 1
}

report=$STAGE/abi-closure.txt
: > "$report"
imports=0
while read -r expected_crc symbol; do
    imports=$((imports + 1))
    actual_crc=$(awk -v wanted="$symbol" \
        '$2 == wanted { print $1; exit }' "$OUT/Module.symvers")
    if [ -z "$actual_crc" ]; then
        printf 'unresolved %s %s\n' "$expected_crc" "$symbol" >> "$report"
        printf 'unresolved module symbol: %s\n' "$symbol" >&2
        exit 1
    fi
    if [ "$actual_crc" != "$expected_crc" ]; then
        printf 'crc-mismatch %s expected=%s available=%s\n' \
            "$symbol" "$expected_crc" "$actual_crc" >> "$report"
        printf 'module CRC mismatch: %s\n' "$symbol" >&2
        exit 1
    fi
done < <(modprobe --dump-modversions "$STAGE/aw32257.ko")
printf 'imports=%s unresolved=0 crc_mismatch=0\n' "$imports" >> "$report"

(
    cd "$STAGE"
    sha256sum aw32257.ko > SHA256SUMS
)

printf 'BF7 AW32257 manual-only module built for %s\n' "$release"
cat "$STAGE/SHA256SUMS"
cat "$report"
