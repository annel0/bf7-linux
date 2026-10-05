# Внешние исходники и репозитории

Статус на 2026-08-23. Ссылки проверены read-only. «База» означает кандидата
для кода будущего BF7-порта, а не готовый образ для прошивки.

| Приоритет | Репозиторий | Версия / ветка | Что подтверждённо есть | Оценка |
|---|---|---|---|---|
| **Основная база** | [bengris32/android_kernel_mediatek_4.19](https://github.com/bengris32/android_kernel_mediatek_4.19) | ALPS commit `6f3a19332a49`, локально merged с Android stable `4.19.191` | MT6761 BSP, ComboA eMMC, display/audio/USB/WDT/CCCI и точный Rogue DDK `m1.15ED6070602` | Лучшее найденное основание: vendor snapshot и exact GPU DDK. Не BF7 board source; Transsion touch/sensors/camera glue отсутствуют. Локальная ветка: `codex/bf7-alps191-base`. |
| Старший donor / display reference | [MostafaAshry513/x657b_kernel_4.19](https://github.com/MostafaAshry513/x657b_kernel_4.19) | Linux `4.19.325`, `main` | MT6761, MediaTek display/audio/storage/USB, WMT/ConnSys adapter, GED и Rogue DDK `m1.15ED6070602` | Больше не baseline: история самого проекта фиксирует неуспешную загрузку на исходном устройстве, а версия заметно дальше OEM. Сохраняется как donor более нового layout/исправлений. |
| **Основной Wi-Fi donor** | [Motorola connectivity common](https://github.com/MotorolaMobilityLLC/vendor-mediatek-kernel_modules-connectivity-common), [adaptor](https://github.com/MotorolaMobilityLLC/vendor-mediatek-kernel_modules-connectivity-wlan-adaptor), [gen4m](https://github.com/MotorolaMobilityLLC/vendor-mediatek-kernel_modules-connectivity-wlan-core-gen4m) | Android 12 tag `MMI-STA32.79-77-28` | точные MT6761 WMT platform files, adaptor и gen4m Connac/6765 AXI path | Все три модуля чисто собираются exact Clang 11 против нового kernel; 585 imported symbols суммарно, `unresolved=0`, CRC mismatch=0. Основной source для первого manual-only Wi-Fi test. |
| **Exact charger donor** | [официальный Awinic AW322XX MTK driver](https://www.awinic.com/en/driver/AW32207FCR/249) | archive `AW322XX_Driver_MTK_V1.0.0.zip`, legacy exact subarchive `AW32257_Driver_MTK_V1.2.0.zip` | `CONFIG_CHARGER_AW32257`, `awinic,aw32257`, MTK charger class | Exact V1.2 source адаптирован только к реально наблюдаемым BF7 DT properties и старому power-supply enum. Собран как manual-only `.ko`; 37 imports, unresolved=0, CRC mismatch=0. |
| Поздний Wi-Fi reference | [StasGr12/kernel_ubports_mt6781](https://github.com/StasGr12/kernel_ubports_mt6781) | Linux `4.19.325`, `main` | полный современный `wlan_drv_gen4m`, WMT и adaptor | Полезен для diff, но tip не содержит точного MT6761 platform file. Не использовать вместо найденного Motorola MT6761 snapshot. |
| Сравнительный кандидат | [JamiKettunen/android_kernel_duoqin_mt6761](https://github.com/JamiKettunen/android_kernel_duoqin_mt6761) | Linux `4.19.127`, `android-11.0` | MT6761, `k61v1_64_bsp`, документированный реальный reverse-engineering порт | Полезен как второй независимый 4.19 reference. Другая плата; автор отмечает проблемы с тачем, динамиком, BT и камерами. |
| Сравнительный кандидат | [nokia-mt6761-devs/android_kernel_nokia_mt6761](https://github.com/nokia-mt6761-devs/android_kernel_nokia_mt6761) | Linux `4.19.127`, `alps-wasp` | Полное MT6761 4.19 дерево; база Duoqin-форка | Полезно для diff и отсутствующих компонентов, не BF7. |
| BF7 board reference | [twrpdtgen/android_device_tecno_TECNO-BF7](https://github.com/twrpdtgen/android_device_tecno_TECNO-BF7) | ветка `vnd_bf7_h6127-user-12-SP1A.210812.016-651674-release-keys` | точные boot offsets, header v2, MT6761, A/B, partition layout; prebuilt kernel и DTB | Не kernel source. Ценен как независимая фиксация параметров BF7. |
| BF7 board reference | [Charles078728/android_device_tecno_TECNO-BF7](https://github.com/Charles078728/android_device_tecno_TECNO-BF7) | `test` | `CY-BF7-H6127`, boot parameters, recovery observations | Не kernel source; часть значений/комментариев надо проверять по нашему boot image. |
| Touch driver donor | [Nhan918/a13-kernel-source](https://github.com/Nhan918/a13-kernel-source) | `test10`, Android 13-era source | `drivers/input/touchscreen/ovt/td4150_spi/` с `ovt_tcm_*` | Не MT6761/BF7 kernel и не готов к сборке у нас, но это первый публичный source с очень сильным совпадением символов BF7 touchscreen driver. |
| **Основной touch donor** | [zany415/android_kernel_unisoc_qogirl6](https://github.com/zany415/android_kernel_unisoc_qogirl6) | Linux `4.14.303`, `nosec` | `drivers/input/touchscreen/omnivision_tcm/`, neutral Kconfig и `ovt_tcm_*` | Не MT6761, но значительно ближе к BF7 по коду и API: 45 точных совпадений символов, без Samsung-specific hooks. Кандидат для будущего port в X657B 4.19. |
| Старый MTK donor | [mediatek-android-development/android_kernel_mediatek_mt6761-62-4.9](https://github.com/mediatek-android-development/android_kernel_mediatek_mt6761-62-4.9) | Linux `4.9.117`, `master` | Открытый ALPS BSP для MT6761/63/65, `k61v1_64_bsp_defconfig` | Годится лишь как donor/историческая справка: ABI и ядро существенно старее OEM BF7 4.19. |
| Не использовать как базу | [LCM-MTK/android_kernel_mediatek_mt6761](https://github.com/LCM-MTK/android_kernel_mediatek_mt6761) | Linux `4.4.146`, `lineage-16.0` | Generic ALPS дерево | Слишком старое; содержит не нашу k61v1 конфигурацию. |
| Старый device-specific donor | [huawei-mediatek-devs/android_kernel_huawei_mt6761](https://github.com/huawei-mediatek-devs/android_kernel_huawei_mt6761) | Linux `4.9.117`, `lineage-21` | k61v1 MT6761 вариант (`mexico`) | Только точечный источник старого кода, не база. |

## Почему ALPS snapshot + stable 4.19.191 теперь основной путь

В истории `bengris32/android_kernel_mediatek_4.19` найден commit
`6f3a19332a49efbc3e3e960468b198818addc137` от 2021-08-25 с полным портом
PowerVR `m1.15ED6070602`. Это точная версия DDK из OEM BF7 userspace и
firmware. Snapshot уже содержит MT6761 display, ComboA eMMC, USB gadget,
MediaTek watchdog, CCCI и остальные основные BSP-блоки.

Локальный worktree
`kernel-work/mediatek-mt6761-alps188-gpu115` создан от этого commit. В него
merged общий Android stable commit
`011b73c995f35959b39ccde045addbc1862fa3e6`, соответствующий Linux 4.19.191;
merge commit — `9c2539cb9aef6f2fc85cad43fe76eab90916820b`. Это не делает дерево
побайтной копией OEM `g216400aba14e`, но закрывает upstream stable level без
перехода на чужой 4.19.325 donor.

Рабочее OEM-ядро сообщает точный compiler:
Android Clang `11.0.1`, build `6443078 based on r383902`, LLD 11.0.1. Его
официальный prebuilt сохранён локально в
`kernel-work/toolchains/clang-r383902`; финальная сборка должна использовать
именно его, а GCC 16 остаётся только диагностическим компилятором.

В старом ALPS header не хватало 48 байт более нового `LCM_PARAMS`, а enum
DSI format кодировал RGB888 как 2 вместо OEM-значения 3. В базу точечно
перенесены поля `ap_data_rate`, bridge flags и `vsync_after_te`, а enum
синхронизирован с более новым MediaTek layout. Действующие guards теперь
снова доказывают `sizeof(LCM_PARAMS) == 0x518` и offset RGB format = word 127.

## Connectivity sources для MT6761

В официальных Motorola-published MediaTek repositories tag
`MMI-STA32.79-77-28` содержит именно нужную старую архитектуру ConnSys:
`platform/mt6761.c`, `platform/mt6761_dbg.c`, WMT common, WLAN adaptor и
gen4m Connac driver с AXI path для `WLAN_CHIP_ID=6765`. Локально зафиксированы
commits `dd2d236cb464...` (common), `4370ded09ce8...` (adaptor) и
`e07ad500ebb4...` (gen4m).

Они собраны не как чужие готовые `.ko`, а заново против exact
`4.19.191-bf7-native-alps191-v1` и его `Module.symvers`. Поэтому совпадают
vermagic и CRC всех импортов. Это закрывает compile/ABI часть, но не доказывает
runtime совместимость firmware, EMI map и реального ConnSys блока BF7.

## Exact AW32257 charger source

С официальной страницы Awinic сохранён архив
`kernel-work/donors/awinic-aw322xx/AW322XX_Driver_MTK_V1.0.0.zip`, SHA-256
`975e7fa29c73f1968737112786df7b348a49306675cb0eb1de3de04843e38f4f`.
Внутри производителя лежит предыдущая точная версия
`AW32257_Driver_MTK_V1.2.0.zip`. Она совпадает с OEM по Kconfig,
`compatible = "awinic,aw32257"` и 46 именам функций из 53 видимых в OEM
`kallsyms`.

Живой merged DT BF7 подтверждает I2C node `aw32257@6a`, property
`awinic,chg-en-gpio` и `awinic,iterm = <240000>`. В donor внесена минимальная
адаптация: fallback GPIO property, чтение OEM termination current, C90 fix и
замена отсутствующего в этой 4.19 базе
`POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT` на семантически ближайший
`POWER_SUPPLY_PROP_INPUT_VOLTAGE_REGULATION`. Модуль не auto-load: probe
записывает реальные регистры зарядного IC.

Дополнительно OEM probe восстановлен по `bf7-kallsyms.txt` и заводскому
Image: `06:ff`, `00:d0`, `01:38`, `08:8e`, затем ITERM. Эта последовательность
перенесена вместо defaults старого donor. Обнаруженный в donor и OEM
off-by-one при формировании I2C message исправлен без изменения wire payload.

## Почему X657B 4.19 остаётся сильным donor

X657B — Infinix Smart 5 на MT6761D, не клон BF7: у него иные корпус,
разрешение 720x1600, micro-USB, фронтальная камера и своя аппаратная плата.
Однако его исходное дерево уже использует MediaTek 4.19 и базовый профиль
`k61v1_64_bsp`, так что совпадает архитектура software BSP, а не только SoC.

Дополнительная локальная проверка 2026-08-13 показала, что каркас
`arch/arm64/boot/dts/mediatek/mt6761.dts` X657B очень близок к извлечённому
BF7 DTB: в обоих присутствуют одноимённые узлы `touch`, `wifi`, `consys`,
`usb`, `msdc0/1`, `audio`, `gpufreq`, `ged`, `CCCI` и camera framework.
Это повышает ценность дерева именно как *SoC/BSP-базы*. Но это не повод
подменять BF7 DTB: в X657B включён `CONFIG_TOUCHSCREEN_MTK_GT1151=y`, тогда
как у BF7 фактически работает `omnivision_tcm_spi`. Даже строка
`compatible = "goodix,touch"` у обоих оказывается общим MTK placeholder, а
не идентификатором реального контроллера.

Особенно важен путь:

```text
drivers/gpu/mediatek/gpu_rgx/m1.15ED6070602
```

Он соответствует нашему OEM PowerVR userspace `1.15@6070602` и заводской
паре firmware `rgx.fw` / `rgx.sh`. Это сильное основание для будущей
контролируемой работы с GPU, но **не разрешение** открывать опасные текущие
PowerVR sysfs/AppHint nodes: известный риск panic остаётся.

## Исходник OVT TCM: найден близкий donor, но не точная версия BF7

У BF7 сохранён `kallsyms` показал static driver с префиксом `ovt_tcm_*`
(`ovt_tcm_spi_probe`, `ovt_tcm_wait_hdl`, `ovt_tcm_run_application_firmware`).
В `Nhan918/a13-kernel-source` найден публичный источник
`drivers/input/touchscreen/ovt/td4150_spi`. Из проверенных символов 39
совпадают буквально, включая SPI transport, Host Download Firmware (HDL),
reset, identify, IRQ и application firmware запуск. Следовательно,
`hdl_firmware.img` и наш `bf7-touch-init.service` соответствуют известной
архитектуре драйвера, а не случайному vendor workaround.

Это пока **донор для переноса**, не готовая BF7-реализация: дерево рассчитано
на Android 13-era Samsung kernel, содержит Samsung-specific conditional code,
и цель `td4150_spi` может отличаться от панели BF7. Перед переносом нужны
version diff, минимизация platform hooks и compile-only проверка в выбранной
4.19 базе.

Более подходящий source найден затем в Linux 4.14.303 дереве
`zany415/android_kernel_unisoc_qogirl6`: его neutral
`drivers/input/touchscreen/omnivision_tcm/` даёт 45 совпадающих BF7
`ovt_tcm_*`-символов, использует ordinary SPI/input/firmware API и имеет
буквально `FW_IMAGE_NAME "hdl_firmware.img"`. Это совпадает с работающим
BF7 runtime. Донор всё ещё не является exact BF7 source, но он становится
основой будущей compile-only адаптации вместо Samsung-варианта.

## Что пока не найдено

- исходники, опубликованные именно TECNO/Transsion для `BF7`, `BF7S`, `BF7H`
  или `bf7_h6127`;
- исходники, опубликованные именно Transsion для BF7-варианта OVT TCM и его
  точные Kconfig/DT bindings (найден только близкий публичный donor);
- исходники точного sensor/fingerprint/camera board glue;
- подтверждение, что какая-либо внешняя kernel tree собирается с точным BF7
  DTB без изменений.

## Смежная аппаратная зацепка: Infinix X6515

В опубликованном логе прошивки Infinix Smart 7 X6515 (тоже MT6761, Android
12) одновременно перечислены `omnivision_tcm_spi`, `mxc4005xc`, `mn78911_l`
и тот же ConnSys MT6761. Это делает X6515 полезным **направлением поиска
исходников**, но не donor kernel: совпадение логов не доказывает ни ту же
плату, ни тот же тач-панельный firmware. Открытый X6515 kernel source пока
не найден; GitHub repository search по `X6515`, `Infinix Smart 7` и
`omnivision_tcm_spi` результатов не дал.

## Недопустимые выводы

- Нельзя прошивать X657B, Duoqin, Nokia, BF7H или BF7S kernel/DTB в BF7.
- Нельзя переносить `.ko` между сборками: kernel ABI зависит от точного дерева
  и config.
- Наличие исходников PowerVR не означает, что заводские `libsrv_um.so` будут
  автоматически ABI-совместимы с произвольной сборкой.
- Успешная локальная линковка не доказывает загрузку на BF7. Текущий LK
  ломает `fastboot boot` даже для точного известного рабочего image, поэтому
  runtime validation пока заблокирован без записи в A/B slot.
