#!/bin/bash
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
KERNEL_WORK=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
PROJECT_DIR=$(CDPATH= cd -- "$KERNEL_WORK/.." && pwd)
TREE=$KERNEL_WORK/mediatek-mt6761-alps188-gpu115
OUT=${KERNEL_OUT:-$KERNEL_WORK/build/bf7-alps191-gpu115-clang11-v1}
DONOR=$KERNEL_WORK/donors/motorola-mt6761-connectivity-sta32
COMMON=$DONOR/common
ADAPTOR=$DONOR/adaptor
GEN4M=$DONOR/gen4m/gen4m
TOOLCHAIN=$KERNEL_WORK/toolchains/clang-r383902/bin
STAGE=$PROJECT_DIR/native-linux-build/alps191-wifi-modules-v1

check_commit() {
    repo=$1
    expected=$2
    actual=$(/usr/bin/git -C "$repo" rev-parse HEAD)
    if [ "$actual" != "$expected" ]; then
        printf 'unexpected donor commit: %s\nexpected: %s\nactual:   %s\n' \
            "$repo" "$expected" "$actual" >&2
        exit 1
    fi
}

for required in "$TREE/Makefile" "$OUT/.config" "$OUT/Module.symvers" \
    "$TOOLCHAIN/clang" "$COMMON/Kbuild" "$ADAPTOR/Makefile" \
    "$GEN4M/Makefile"; do
    if [ ! -e "$required" ]; then
        printf 'missing required input: %s\n' "$required" >&2
        exit 1
    fi
done

check_commit "$COMMON" dd2d236cb46469f4102b8b83855de8bf6557996e
check_commit "$ADAPTOR" 4370ded09ce824cc21df16f9771bd74220446c22
check_commit "$DONOR/gen4m" e07ad500ebb4f963a80609a1698980bffc540e08

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
)

mkdir -p "$STAGE/logs"

set -o pipefail
make "${common_args[@]}" M="$COMMON" \
    MTK_CONSYS_ADIE= MTK_PLATFORM_WMT=MT6761 \
    TARGET_BOARD_PLATFORM_WMT=mt6761 clean >/dev/null
make "${common_args[@]}" M="$COMMON" \
    MTK_CONSYS_ADIE= MTK_PLATFORM_WMT=MT6761 \
    TARGET_BOARD_PLATFORM_WMT=mt6761 modules \
    2>&1 | tee "$STAGE/logs/wmt_drv.log"

adaptor_inc="-I$COMMON/common_main/include -I$COMMON/common_main/linux/include -I$COMMON/debug_utility"
make "${common_args[@]}" M="$ADAPTOR" \
    EXTRA_CFLAGS="$adaptor_inc" CONNAC_VER=1_0 \
    MODULE_NAME=wmt_chrdev_wifi clean >/dev/null
make "${common_args[@]}" M="$ADAPTOR" \
    KBUILD_EXTRA_SYMBOLS="$COMMON/Module.symvers" \
    EXTRA_CFLAGS="$adaptor_inc" CONNAC_VER=1_0 \
    MODULE_NAME=wmt_chrdev_wifi modules \
    2>&1 | tee "$STAGE/logs/wmt_chrdev_wifi.log"

gen4m_inc="-I$COMMON/common_main/include -I$COMMON/common_main/linux/include -I$COMMON/debug_utility -I$ADAPTOR/include"
make "${common_args[@]}" M="$GEN4M" \
    EXTRA_CFLAGS="$gen4m_inc" \
    DRIVER_BUILD_DATE=20241207225237 \
    CONFIG_MTK_COMBO_WIFI_HIF=axi MODULE_NAME=wlan_drv_gen4m \
    MTK_COMBO_CHIP=CONNAC WLAN_CHIP_ID=6765 MTK_ANDROID_WMT=y \
    WIFI_ENABLE_GCOV= WIFI_IP_SET=1 MTK_ANDROID_EMI=y \
    MTK_WLAN_SERVICE=yes clean >/dev/null
make "${common_args[@]}" M="$GEN4M" \
    KBUILD_EXTRA_SYMBOLS="$ADAPTOR/Module.symvers $COMMON/Module.symvers" \
    EXTRA_CFLAGS="$gen4m_inc" \
    DRIVER_BUILD_DATE=20241207225237 \
    CONFIG_MTK_COMBO_WIFI_HIF=axi MODULE_NAME=wlan_drv_gen4m \
    MTK_COMBO_CHIP=CONNAC WLAN_CHIP_ID=6765 MTK_ANDROID_WMT=y \
    WIFI_ENABLE_GCOV= WIFI_IP_SET=1 MTK_ANDROID_EMI=y \
    MTK_WLAN_SERVICE=yes modules \
    2>&1 | tee "$STAGE/logs/wlan_drv_gen4m.log"

install -m 0644 "$COMMON/wmt_drv.ko" "$STAGE/wmt_drv.ko"
install -m 0644 "$ADAPTOR/wmt_chrdev_wifi.ko" \
    "$STAGE/wmt_chrdev_wifi.ko"
install -m 0644 "$GEN4M/wlan_drv_gen4m.ko" \
    "$STAGE/wlan_drv_gen4m.ko"
"$TOOLCHAIN/llvm-strip" --strip-debug "$STAGE"/*.ko

expected_release=$(cat "$OUT/include/config/kernel.release")
for module in "$STAGE"/*.ko; do
    actual_release=$(modinfo -F vermagic "$module" | cut -d' ' -f1)
    if [ "$actual_release" != "$expected_release" ]; then
        printf 'vermagic mismatch: %s: %s != %s\n' \
            "$module" "$actual_release" "$expected_release" >&2
        exit 1
    fi
done

ABI_REPORT=$STAGE/abi-closure.txt
: > "$ABI_REPORT"
for module in "$STAGE"/*.ko; do
    imported=0
    printf '[%s]\n' "$(basename "$module")" >> "$ABI_REPORT"
    while read -r expected_crc symbol; do
        imported=$((imported + 1))
        actual_crc=$(awk -v wanted="$symbol" \
            '$2 == wanted { print $1; exit }' \
            "$OUT/Module.symvers" "$COMMON/Module.symvers" \
            "$ADAPTOR/Module.symvers" "$GEN4M/Module.symvers")
        if [ -z "$actual_crc" ]; then
            printf 'unresolved %s %s\n' "$expected_crc" "$symbol" \
                >> "$ABI_REPORT"
            printf 'unresolved module symbol: %s: %s\n' \
                "$module" "$symbol" >&2
            exit 1
        fi
        if [ "$actual_crc" != "$expected_crc" ]; then
            printf 'crc-mismatch %s expected=%s available=%s\n' \
                "$symbol" "$expected_crc" "$actual_crc" >> "$ABI_REPORT"
            printf 'module CRC mismatch: %s: %s\n' "$module" "$symbol" >&2
            exit 1
        fi
    done < <(modprobe --dump-modversions "$module")
    printf 'imports=%s unresolved=0 crc_mismatch=0\n\n' "$imported" \
        >> "$ABI_REPORT"
done

printf '%s\n' \
    'wmt_drv.ko:' \
    'wmt_chrdev_wifi.ko: wmt_drv.ko' \
    'wlan_drv_gen4m.ko: wmt_drv.ko wmt_chrdev_wifi.ko' \
    > "$STAGE/modules.dep"

(
    cd "$STAGE"
    sha256sum wmt_drv.ko wmt_chrdev_wifi.ko wlan_drv_gen4m.ko \
        > SHA256SUMS
)

printf 'BF7 Wi-Fi modules built for %s\n' "$expected_release"
cat "$STAGE/SHA256SUMS"
cat "$ABI_REPORT"
