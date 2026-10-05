#!/bin/sh
# Андроидная часть камеры. На C, не на C++: библиотеки libc++_shared.so на
# разделе нет, а нам она и не нужна — вендорская часть камеры только
# 64-битная и живёт в мире bionic, как и bf7-videod.
#
# Демон bf7-camd здесь НЕ собирается: это обычная программа glibc, её
# собирают прямо на телефоне —
#     gcc -O2 -o bf7-camd bf7-camd.c -lm
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
NDK=${NDK:-$HERE/../ndk/android-ndk-r25c}
CC=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android29-clang
[ -x "$CC" ] || { echo "нет компилятора: $CC" >&2; exit 1; }
for prog in bf7-camshot bf7-seninf-up; do
	"$CC" -O2 -Wall -o "$HERE/$prog" "$HERE/$prog.c" -ldl -lm
	echo "собрано: $HERE/$prog"
done
