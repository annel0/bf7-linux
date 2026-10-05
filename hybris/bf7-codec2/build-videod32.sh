#!/bin/sh
# Сборка 32-битного демона аппаратного декодирования.
#
# Настоящий драйвер кодека на этом устройстве только 32-битный
# (/vendor/lib/libvcodecdrv.so), поэтому и помощник должен быть 32-битным.
# Это обычный андроидный бинарник: libhybris не нужен, границы между libc++
# нет, всё живёт в мире bionic.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
HYBRIS=$(CDPATH= cd -- "$HERE/.." && pwd)
NDK=${NDK:-$HYBRIS/ndk/android-ndk-r25c}
TC=$NDK/toolchains/llvm/prebuilt/linux-x86_64
CXX=$TC/bin/armv7a-linux-androideabi31-clang++
AOSP=$HYBRIS/android-src
C2=$AOSP/frameworks/av/media/codec2
LIBS=$HERE/sysroot-libs32
OUT=${OUT:-$HERE/bf7-videod}

[ -x "$CXX" ] || { echo "нет компилятора: $CXX" >&2; exit 1; }

INC="
-I$C2/core/include
-I$C2/vndk/include
-I$AOSP/system/core/libutils/include
-I$AOSP/system/core/libcutils/include
-I$AOSP/system/core/libsystem/include
-I$AOSP/system/logging/liblog/include
-I$AOSP/frameworks/native/libs/nativebase/include
-I$AOSP/frameworks/native/libs/nativewindow/include
-I$AOSP/hardware/libhardware/include
-I$HYBRIS/android-headers
"

"$CXX" -std=c++17 -O2 -Wall -Wno-unused-parameter \
	-D_LIBCPP_ABI_NAMESPACE=__1 -nostdlib++ \
	$INC -o "$OUT" "$HERE/bf7-videod.cpp" \
	-L"$LIBS" -lcodec2_vndk -lutils -lcutils -llog -lbase -lui -lc++ \
	-Wl,--no-undefined

echo "собрано: $OUT"
"$TC/bin/llvm-readelf" -h "$OUT" | awk '/Class:|Machine:/{print "  " $0}'
