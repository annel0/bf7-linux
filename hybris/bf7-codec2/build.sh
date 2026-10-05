#!/bin/sh
# Сборка прослойки к декодеру MediaTek.
#
# Собирается NDK, потому что Codec2 — это C++ поверх bionic-овской libc++, и
# прослойка обязана пользоваться той же. Компонуется не с libc++_shared.so из
# NDK, а с платформенной libc++.so, снятой с самого телефона: иначе в процессе
# окажутся две разные libc++ и std::shared_ptr между ними не переживёт границу.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
HYBRIS=$(CDPATH= cd -- "$HERE/.." && pwd)
NDK=${NDK:-$HYBRIS/ndk/android-ndk-r25c}
TC=$NDK/toolchains/llvm/prebuilt/linux-x86_64
CXX=$TC/bin/aarch64-linux-android31-clang++
AOSP=$HYBRIS/android-src
C2=$AOSP/frameworks/av/media/codec2
LIBS=$HERE/sysroot-libs
OUT=${OUT:-$HERE/libbf7c2dec.so}

[ -x "$CXX" ] || { echo "нет компилятора NDK: $CXX" >&2; exit 1; }

INC="
-I$HERE
-I$C2/core/include
-I$C2/vndk/include
-I$C2/vndk/internal
-I$AOSP/system/core/libutils/include
-I$AOSP/system/core/libcutils/include
-I$AOSP/system/core/libsystem/include
-I$AOSP/system/logging/liblog/include
-I$AOSP/frameworks/native/libs/nativebase/include
-I$AOSP/frameworks/native/libs/nativewindow/include
-I$AOSP/hardware/libhardware/include
-I$HYBRIS/android-headers
"

# libc++ из NDK живёт в пространстве имён __ndk1, платформенная — в __1.
# Из-за этого мангленные имена не сходятся: линковщик не находит ни
# std::__ndk1::basic_string::~basic_string(), ни
# android::GetCodec2BlockPool(..., std::__ndk1::shared_ptr<...>).
# В __config макрос объявлен под #ifndef, поэтому достаточно задать его снаружи.
"$CXX" -std=c++17 -fPIC -shared -O2 -Wall -Wno-unused-parameter \
	-D_LIBCPP_ABI_NAMESPACE=__1 \
	-nostdlib++ \
	$INC \
	-o "$OUT" "$HERE/bf7c2dec.cpp" \
	-L"$LIBS" \
	-lcodec2_vndk -lutils -lcutils -llog -lbase -lui -lc++ \
	-Wl,--no-undefined -Wl,-soname,libbf7c2dec.so

echo "собрано: $OUT"
"$TC/bin/llvm-readelf" -dW "$OUT" | grep -E "NEEDED|SONAME"
