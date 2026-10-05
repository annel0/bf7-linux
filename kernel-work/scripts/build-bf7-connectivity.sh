#!/bin/bash
# Собрать внеядерные модули Wi-Fi (ConnSys) под ядро-кандидат BF7.
#
# Прошивка телефона поставляет их готовыми под OEM-ядро
# (vermagic=4.19.191-g216400aba14e-dirty), загрузить их на наше ядро нельзя.
# Исходники берём из донорского дерева Motorola MT6761 (см. donors/), которое
# содержит и платформенный слой mt6761, и backend chips/connac, строящий имена
# прошивок WIFI_RAM_CODE_soc1_0_1_1.bin — ровно те, что лежат в /vendor/firmware.
#
# Порядок сборки важен: wmt_drv экспортирует символы, нужные остальным двум,
# поэтому его Module.symvers передаётся дальше через KBUILD_EXTRA_SYMBOLS.
set -u

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
KERNEL_WORK=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
TREE=$KERNEL_WORK/mediatek-mt6761-alps188-gpu115
KOUT=${KOUT:-$KERNEL_WORK/build/bf7-alps191-gpu115-clang11-v1}
DONOR=$KERNEL_WORK/donors/motorola-mt6761-connectivity-sta32
OUT=${OUT:-$KERNEL_WORK/build/bf7-connectivity-v1}
TOOLCHAIN=$KERNEL_WORK/toolchains/clang-r383902/bin
PLATFORM=mt6761

for required in "$TREE/Makefile" "$KOUT/Module.symvers" "$TOOLCHAIN/clang" \
    "$DONOR/common/Kbuild" "$DONOR/adaptor" "$DONOR/gen4m/gen4m"; do
    [ -e "$required" ] || { printf 'нет: %s\n' "$required" >&2; exit 1; }
done

export PATH="$TOOLCHAIN:$PATH"
KBUILD="ARCH=arm64 CC=clang LD=ld.lld AR=llvm-ar NM=llvm-nm OBJCOPY=llvm-objcopy OBJDUMP=llvm-objdump STRIP=llvm-strip CROSS_COMPILE=aarch64-linux-gnu- CLANG_TRIPLE=aarch64-linux-gnu-"

# Копируем исходники: в донорском дереве лежат объектники и .ko от чужой
# сборки, собирать поверх них нельзя (и не хочется пачкать donors/).
sync_src() {
    src=$1 dst=$2
    rm -rf "$dst"
    mkdir -p "$dst"
    tar -C "$src" --exclude=.git --exclude=.tmp_versions --exclude='*.o' \
        --exclude='*.ko' --exclude='*.mod.c' --exclude='*.cmd' \
        --exclude=modules.order --exclude=Module.symvers -cf - . | tar -C "$dst" -xf -
}

mkdir -p "$OUT/src"
echo "копирую исходники..."
sync_src "$DONOR/common"      "$OUT/src/common"
sync_src "$DONOR/adaptor"     "$OUT/src/adaptor"
sync_src "$DONOR/gen4m/gen4m" "$OUT/src/gen4m"

# Наши добавления поверх донорских исходников (см. каждый файл: там написано,
# чего именно в доноре не хватает).  Держим отдельно, чтобы donors/ оставался
# нетронутой копией чужого дерева.
OVERLAY=$KERNEL_WORK/bf7-connectivity-overlay
if [ -d "$OVERLAY" ]; then
    echo "накладываю оверлей..."
    tar -C "$OVERLAY" -cf - . | tar -C "$OUT/src" -xf -
    find "$OVERLAY" -type f -printf '  %P\n'
fi

# Донорские Makefile'ы адресуют соседей через $(TOP)/vendor/mediatek/
# kernel_modules/connectivity/... - раскладка AOSP-дерева.  Делаем такую
# раскладку символическими ссылками, чтобы не переписывать чужие Makefile.
TOPDIR=$OUT/top/vendor/mediatek/kernel_modules/connectivity
rm -rf "$OUT/top"
mkdir -p "$TOPDIR/wlan/core"
ln -s "$OUT/src/common"  "$TOPDIR/common"
ln -s "$OUT/src/adaptor" "$TOPDIR/adaptor"
ln -s "$OUT/src/gen4m"   "$TOPDIR/wlan/core/gen4m"

build_one() {
    name=$1; dir=$2; shift 2
    echo "=== $name ==="
    # shellcheck disable=SC2086
    make -C "$TREE" O="$KOUT" M="$dir" $KBUILD \
        MTK_PLATFORM="$PLATFORM" TARGET_BOARD_PLATFORM="$PLATFORM" \
        KERNEL_SRC="$TREE" KERNEL_OUT="$KOUT" TOP="$OUT/top" \
        "$@" modules > "$OUT/build-$name.log" 2>&1 || {
            echo "--- ошибка, последние строки $OUT/build-$name.log ---"
            tail -25 "$OUT/build-$name.log"
            return 1
        }
    echo "ок: $(grep -c . "$OUT/build-$name.log") строк в $OUT/build-$name.log"
}

EXTRA=""
build_one wmt_drv "$OUT/src/common" ${EXTRA:+KBUILD_EXTRA_SYMBOLS="$EXTRA"}
EXTRA="$OUT/src/common/Module.symvers"
build_one wmt_chrdev_wifi "$OUT/src/adaptor" KBUILD_EXTRA_SYMBOLS="$EXTRA" \
    MODULE_NAME=wmt_chrdev_wifi
# MT6761 использует backend CONNAC поверх AXI; без явного задания Makefile
# падает на умолчании MT6632 и пустом HIF.  MODULE_NAME фиксируем таким же,
# как у стоковых модулей, чтобы bf7-wifi-init нашёл его в /proc/modules.
build_one wlan_drv_gen4m "$OUT/src/gen4m" KBUILD_EXTRA_SYMBOLS="$EXTRA" \
    MTK_COMBO_CHIP=CONNAC WLAN_CHIP_ID=6761 \
    CONFIG_MTK_COMBO_WIFI_HIF=axi MTK_ANDROID_WMT=y \
    MODULE_NAME=wlan_drv_gen4m MTK_WLAN_SERVICE_PATH=wlan_service/ \
    MTK_ANDROID_EMI=y

echo "=== результат ==="
find "$OUT/src" -name '*.ko' -printf '%p  %s байт\n'
