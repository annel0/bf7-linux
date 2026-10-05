# Матрица BF7-порта

Состояние: планирование, без собственной прошивки. «Закрыто» не означает
готовность к тесту — только то, что известны необходимые исходные материалы.

| Подсистема | Работает на OEM native Debian | Код в X657B 4.19 | BF7-specific данные | Статус для своего kernel |
|---|---:|---:|---:|---|
| Boot / MT6761 / ARM64 | да | да | boot header, OEM kernel/DTB | закрыто для baseline-анализа |
| eMMC / ext4 / CPUfreq / thermal | да | да | OEM config, runtime evidence | высокий шанс |
| Display / framebuffer | да | MT6765 display core + recovered BF7 TD4160-HX LCM + local OCP2130 bias client compile | OEM Image command table/params/reset sequence; active LCM tag; factory DTBO pinctrl and I2C 0x3e | compile-only preflight closed; device boot test still required |
| USB gadget / RNDIS | да | да | OEM DTB + working init setup | высокий шанс |
| Wi-Fi / ConnSys | да | Motorola Android 12 MT6761 WMT + adaptor + gen4m Connac/6765 | exact firmware, WMT Bionic shim | три matching `.ko` собраны; vermagic и 585 symbol CRC закрыты; только manual hardware test после baseline |
| Audio / speaker | да | да | OEM mixer route, tinyalsa artifacts | средний: board route/amp надо сверить |
| Touch | да | GT1151 есть; найден neutral OVT 4.14 donor с 45 совпадающими символами, **не exact BF7 source** | OEM DTB, `hdl_firmware*`, runtime node, `kallsyms` | высокая важность, но реалистичный путь; BF7 DTB сохранить |
| Hardware keys | да | обычный input framework | OEM DTB/runtime evidence | высокий шанс |
| Charger AW32257 | да | официальный exact AW32257 V1.2 + MTK charger class | live merged DT: `awinic,aw32257@6a`, GPIO, 240000 uA iterm | `.ko` собран exact Clang/ABI, 37 CRC совпали; manual-only hardware test |
| Battery V/I/T/status | да | MTK power stack + AW32257 module | OEM DTB/runtime evidence | средний; charger module ещё hardware-untested |
| Battery SOC | нет (`capacity=-1`) | неизвестно | factory `fuelgauged*` | отдельная задача |
| PowerVR Rogue | не используется | **да, m1.15ED6070602** | exact `rgx.fw`, `rgx.sh`, userspace 1.15@6070602 | перспективно, но deferred/risky |
| Camera / ISP | нет | generic framework, не BF7 sensor set | vendor HAL, sensor IDs/glue | исключена из server baseline; отдельная задача |
| Bluetooth / GNSS | нет | частично | vendor modules/firmware/HAL | средняя–сложная |
| LTE / CCCI / CCMNI | не проверен | нужен отдельный audit | OEM modem artifacts/init chain | сложная задача |
| Sensors / fingerprint / TEE | нет | generic portions | custom ABI/HAL | низкий приоритет |

## Артефакты, уже сохранённые локально

| Артефакт | Где искать в проекте | Значение |
|---|---|---|
| Распакованный OEM boot | `native-linux-build/unpack-bootimg/` | kernel, ramdisk, DTB-container и boot metadata |
| OEM kernel config | доступен read-only как `/proc/config.gz` на BF7; SHA зафиксирован в journal | полный source of truth для будущего `olddefconfig` |
| Wi-Fi firmware/modules | `native-linux-build/vendor-wifi/` | доказанно рабочая native Wi-Fi цепочка |
| WMT runtime dependencies | `native-linux-build/wmt-bionic-root/` | минимальная Android/Bionic обвязка для ConnSys |
| Touch firmware | `native-linux-build/touch-firmware/` | работающий OEM touch runtime набор |
| GPU firmware/userland extracts | описаны в [../bf7-native/gpu.md](../bf7-native/gpu.md) | только будущий контролируемый тест |
| Current native boot image | `native-linux-build/bf7-native-debian-v20-mark-success.img` | known-good reference, не перезаписывать |

## Устранимые build dependencies на host

Для финальной основной ветки используется exact OEM toolchain Android
Clang/LLD 11.0.1 `r383902`; он сохранён в `kernel-work/toolchains/`.
`aarch64-linux-gnu-binutils`, `bc` и host build tools остаются вспомогательными.
Ядро и новые Wi-Fi modules собираются одним Clang и одним kernel
`Module.symvers`; современный host GCC больше не считается финальным
toolchain.

## Первые блокеры baseline-сборки

1. Source baseline уже зафиксирован: X657B 4.19.325,
   commit `75934cf7f57bc8ffbdda9e77b1072a32a9b74221`.
2. Его `k61v1_64_bsp_defconfig` успешно разворачивается в 5584-line full
   `.config` с `CONFIG_MTK_PLATFORM="mt6761"`; OEM `/proc/config.gz` ещё
   предстоит перенести отдельной контролируемой итерацией.
3. Не заменять DTB: файл `unpack-bootimg/dtb` — MTK-контейнер с FDT с offset
   64, а не одиночный DTB. Для baseline использовать exact BF7 FDT из него и
   тот же boot header.
4. Разобрать отсутствие `CONFIG_DEVTMPFS` в OEM и заранее решить, включать ли
   его только после успешной baseline-загрузки.
5. Подготовить serial/RNDIS/mass-storage наблюдаемость до первого boot test.

## Результат первого compile-only baseline (2026-08-13)

Сборка `Image.gz` завершилась локально и без unresolved-symbol/link errors.
Получены `arch/arm64/boot/Image.gz` (12,461,518 B, SHA-256
`b0373154ad1da9fe7c6abdc0df81674a69eb279fb9f79e2aae1c7050b27d7bac`),
`vmlinux` (AArch64 ELF64) и `Module.symvers`. В effective config сохранены
`CONFIG_MTK_PLATFORM="mt6761"`, USB MTK HDRC, Wi-Fi, Rogue GPU, framebuffer
и ASoC. Это **доказательство воспроизводимой host-side сборки**, а не
доказательство загрузки на BF7. Image не паковался и не передавался на
устройство.

## BF7 OEM config audit (2026-08-13)

Exact OEM `/proc/config.gz` (5764 lines, known SHA-256) прошёл через
`olddefconfig` выбранного tree: effective config получился 5575 lines. В нём
сохранились MT6761, USB HDRC, Wi-Fi, GPU, ASoC, power supply, CCCI и BT.
Подтверждены два board-specific source gaps: camera sensor stack (`hi1336`,
`gc05a2`, `fake_serial_yuv` отсутствуют) и LCM (`td4160_*_bf7`,
`nt36528_hdp_dsi_vdo_djn_boe_bf7` отсутствуют). Для headless profile camera,
LCM и MTK_FB исключены; generic FB, USB, Wi-Fi, audio, power, modem and GPU
remain enabled. Это local compile-only configuration, не boot image.

### Headless BF7 server profile — compile-only success

`bf7-headless-server-nofb` собрался до `vmlinux` и `Image.gz` без
unresolved-symbol/link errors. `Image.gz` имеет 11,374,787 B и SHA-256
`88fe5e0f8d800785fada5a6d162896ea62c482b5ac414cdca7aee81f4bb379f6`.
В нём подтверждены MT6761, USB HDRC, cfg80211/mac80211, ConnSys, audio,
power supply, CCCI/CCMNI, BTIF и Rogue GPU; camera, LCM и MTK_FB отключены.
Это наиболее близкий к BF7 **локально собранный headless baseline**, но он
не упакован как Android boot image и не тестировался на устройстве.

### Экранный BF7 TD4160-HX profile — полный compile-only успех (2026-08-22)

Активный драйвер панели BF7 `td4160_hdp_dsi_vdo_hx_inx_bf7` восстановлен из
зафиксированного OEM kernel Image и слинкован в полный ARM64 `Image` с
`CONFIG_CUSTOM_KERNEL_LCM="bf7_lcm_recovered"`, `CONFIG_MTK_FB=y` и
`CONFIG_TOUCHSCREEN_MTK=y`. Итоговый
`arch/arm64/boot/Image`: 31,641,608 B, SHA-256
`31c8ae4033a214c07752c1236a0662bfc2f9b479a0e9eaf55ef9e589d218662a`.

В финальном `vmlinux` присутствуют descriptor панели, `lcm_driver_list`,
необходимые BF7 `tpd_gpio_output` и `disp_dts_gpio_select_state`; прошли
финальная линковка и kallsyms. Makefile тачскрина донора потребовал малого
условного исправления для включения общего TPD core на MT6761. Это только
host-side доказательство сборки: Android boot container, замена DTB,
перенос на устройство и boot test не выполнялись. Остаётся один старый
section-mismatch warning от modpost для последующего разбора. Последующий
независимый audit обнаружил до-hardware блокеры: RGB888 был записан не в то
поле `LCM_PARAMS`, отсутствовал panel power path, а LCM init небезопасно
зависел от позднего TPD pinctrl. Поэтому этот compile-only успех нельзя
трактовать как готовность к boot-test до исправления и повторной проверки.

### Результат compile-only feasibility audit для OVT (2026-08-13)

Samsung OVT donor — это примерно 7.5k строк и требует MTK SPI API
(`CONFIG_SPI_MT65XX`), но несёт много Samsung hooks. Он остаётся лишь
справочным.

Предпочтительный neutral OVT 4.14 donor использует стандартные
SPI/input/firmware/PM/GPIO API, не имеет Samsung/Unisoc-specific вызовов и
ждёт firmware под точным именем `hdl_firmware.img`. Его Kconfig уже задаёт
нужный набор: `TOUCHSCREEN_OMNIVISION_TCM`, `..._SPI`, `..._CORE`,
`..._TOUCH`, `..._ZEROFLASH`. Поэтому оценка: **port реалистичен, но это
отдельная подзадача, не первый baseline boot**.
