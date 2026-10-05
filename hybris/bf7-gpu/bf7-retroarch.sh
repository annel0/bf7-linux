#!/bin/sh
# BF7: RetroArch с Vulkan.
#
# Приложения сеанса получают только LD_PRELOAD для GL (eglfilter, wlegl), а
# Vulkan у нас работает лишь через свой загрузчик из /usr/local/lib
# (андроидный драйвер не ICD Khronos, загрузчик Debian его не видит) и с
# libbf7tlsguard. Без этого выбор «vulkan» в настройках RetroArch молча
# ничего не даёт. С обоими окружениями проверены и gl, и vulkan (2026-09-23).
# Ярлык /usr/local/share/applications/retroarch.desktop зовёт эту обёртку.
export LD_LIBRARY_PATH="/usr/local/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
case ":${LD_PRELOAD:-}:" in
*libbf7tlsguard.so*) ;;
*) export LD_PRELOAD="${LD_PRELOAD:+$LD_PRELOAD:}/usr/local/lib/libbf7tlsguard.so" ;;
esac
# Без рамки libdecor. Её модуль cairo рисует заголовок подповерхностью на
# (0,-24) и задаёт геометрию окна (0,-24,…); на весь экран phoc оставлял окно
# сдвинутым на 24 точки — картинка (прямой показ) на месте, а касания уходили
# на 48 пикселей ниже, и узкие START/SELECT экранных кнопок не нажимались.
# На телефоне рамка не нужна; libdecor без модулей работает «no decorations».
export LIBDECOR_PLUGIN_DIR=/nonexistent-bf7
exec /usr/bin/retroarch "$@"
