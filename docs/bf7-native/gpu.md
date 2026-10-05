# GPU: статус и обоснование решения

## Короткий вывод

Аппаратный 3D на native Debian **сознательно не используется**. Это решение, а
не тупик: путь к работающему GLES известен и описан ниже, но его цена
несопоставима с пользой для сервера без оконной системы.

Вывод картинки обеспечивает raw framebuffer `/dev/fb0` (см. [api.md](api.md)),
и он не зависит от состояния GPU.

## Железо и стек

| Компонент | Значение |
|---|---|
| GPU | Imagination PowerVR Rogue (**не** Mali) |
| Platform device | `/sys/devices/platform/13000000.mfg_clark`, `OF_COMPATIBLE_0=mediatek,clark` |
| Kernel driver | `pvrsrvkm`, встроен в OEM-ядро 4.19.191 |
| FW-процессор | **MIPS32r2** — `rgx.fw` является статическим ELF 32-bit MIPS |
| DRM ноды | `card0` (226:0), `controlD64` (226:64), `renderD128` (226:128) |

Прошивка GPU состоит из двух файлов заводского `vendor`-раздела:

```text
rgx.fw    118784 B   sha256 798a00b50941fe48ae3f39dd2331b88e1e51d3e21c68dbd5e3a2a6ed00010576
rgx.sh    350336 B   sha256 cf851a2cd2fa9687c242208cafa1221b32e6dcdfe025f7f7e454a5c2b7b915bb
```

`rgx.sh` — не shell-скрипт, а бинарный USC/shader-блоб для FW-процессора.

## Почему `renderD128` возвращает ENODEV

Инициализация GPU на этом DDK выполняется **целиком в ядре** (kernel-side
srvinit). Проверено по заводским образам: во всём `system` и `vendor` **нет**
`pvrsrvctl`, `srvinit`, `powervr.ini` и init-сервиса, запускающего GPU.
Userspace-утилиты для запуска GPU не существует и не требуется.

Ядро загружает прошивку обычным `request_firmware()` по именам буквально
`rgx.fw` и `rgx.sh` — **без BVNC-суффиксов**. Значит поиск подчиняется kernel
cmdline:

```text
firmware_class.path=/vendor/firmware
```

В native Debian `/vendor` — это каталог Debian rootfs, а не смонтированный
заводской vendor-раздел. Отсюда цепочка:

```text
probe pvrsrvkm -> регистрация DRM-нод и AppHint-хендлеров
  -> RGXInitFirmware -> request_firmware("rgx.fw") -> ENOENT
  -> "All RGX Firmware image loads failed"
  -> PVRSRVCommonDeviceInitialise fails -> устройство в bad state
  -> open(card0 | renderD128) -> "Driver already in bad state" -> ENODEV
```

Соответствующая диагностика присутствует в самом ядре:

```text
%s: request_firmware('%s') failed (%d) (ERROR_NOT_FOUND)
All RGX Firmware image loads failed for '%s' (%s)
%s: Driver already in bad state. Device open failed.
%s: Potential causes: firmware not initialised or the current Guest driver's
    OsConfig initialisation data was not accepted by the firmware
```

## Опасность: AppHint sysfs роняет ядро

**Не читать** параметры AppHint модуля `pvrsrvkm`, пока прошивка не установлена:

```text
/sys/module/pvrsrvkm/parameters/DisableClockGating   <- kernel NULL deref
```

Причина та же. AppHint-хендлеры регистрируются при probe, но структура
`psDevInfo->psFWIfOSConfig` выделяется только в `RGXSetupFirmware`, который не
отработал. `_ReadStateFlag` разыменовывает NULL:

```text
pc : _ReadStateFlag+0x18/0x3c
lr : get_apphint_value_from_action+0xa4/0x128
```

Это относится к любому процессу, рекурсивно обходящему `/sys`. Считать эту ноду
миной до установки прошивки.

## Как включить инициализацию GPU (если понадобится)

Достаточно двух файлов и перезагрузки. Userspace не нужен.

```sh
mkdir -p /vendor/firmware /lib/firmware
cp rgx.fw rgx.sh /vendor/firmware/
cp rgx.fw rgx.sh /lib/firmware/      # запасной стандартный путь
chmod 644 /vendor/firmware/rgx.{fw,sh} /lib/firmware/rgx.{fw,sh}
sha256sum /vendor/firmware/rgx.*     # сверить с хэшами выше
reboot
```

Имена менять нельзя: ядро запрашивает ровно `rgx.fw` и `rgx.sh`.

Не использовать `unbind`/`bind` драйвера вместо перезагрузки: устройство уже
находится в bad state, а этот драйвер один раз ронял ядро.

Критерий успеха в `dmesg`:

```text
RGX Firmware image 'rgx.fw' loaded
```

и отсутствие `All RGX Firmware image loads failed` / `RGXSetupFirmware failed`.
После этого `open("/dev/dri/renderD128")` перестаёт отдавать ENODEV, а AppHint
sysfs становится безопасным.

## Почему не делаем аппаратный GLES/EGL

Инициализация ядра — это только первая половина. Заводской userspace-стек
целиком Android-ABI (bionic, интерпретатор `/system/bin/linker64`), запустить
его на glibc напрямую нельзя.

Посчитанный транзитивный closure по `NEEDED`:

| Цель | Узлов | Состав |
|---|---|---|
| только `libsrv_um.so` | 45 | 4 vendor + 41 Android platform |
| полный EGL/GLES | 67 | 7 IMG + 15 MediaTek + 43 Android platform/VNDK + 2 egl |

Отсутствующих библиотек нет — всё есть в заводских образах. Проблема не в
наличии файлов, а в том, что тянется следом: `libPVRMtkutils.so` требует
`libhidlbase`, `libgralloctypes` и `android.hardware.graphics.mapper@4.0`, а
gralloc-mapper на Android 12 — это HIDL поверх **binder**. Понадобился бы
работающий `hwservicemanager` и `/dev/binder`, то есть Android-совместимый
userspace рядом с systemd (`/opt/bf7-android` с bionic, VNDK v31 и linker
namespaces; `LD_LIBRARY_PATH` для этого недостаточно, нужен `linkerconfig`).

Дополнительно: заводской `libIMGegl.so` — это **Android**-EGL. Он завязан на
`ANativeWindow`/gralloc и не имеет GBM-платформы, поэтому привычная для
десктопного Linux связка «GBM + EGL» с этими блобами не заработает. Это не Mesa.

Итог: для headless-сервера без композитинга и 3D-нагрузки выигрыш нулевой, а
объём работы — отдельный проект. Решено не делать.

Если аппаратное ускорение когда-нибудь понадобится, наиболее реалистичной
первой целью является **не** GLES, а `libPVROCL.so` (OpenCL): он значительно
меньше завязан на оконную систему.
