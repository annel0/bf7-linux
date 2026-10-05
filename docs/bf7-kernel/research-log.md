# Журнал исследования

Формат: дата — факт / источник — вывод. Новая находка должна добавляться ниже
и, если меняет статус, отражаться в `sources.md` и `compatibility-matrix.md`.

## 2026-08-13 — старт journal

- Зафиксирована цель: самостоятельный BF7 kernel, а не модификация Android
  userspace.
- Подтверждена аппаратная семья: TECNO BF7 / Spark Go 2023 / Pop 7 Pro;
  MT6761, `k61v1_64_bsp`, Linux 4.19.191 в OEM image.
- Публичных исходников TECNO/Transsion именно для BF7, BF7S, BF7H или
  `bf7_h6127` пока не найдено.
- Найден BF7-specific TWRP device tree; он использует prebuilt kernel/DTB и
  поэтому не является kernel source.
- Найдены 4.19 MT6761 reverse-engineering trees Nokia/Duoqin.
- Найден `MostafaAshry513/x657b_kernel_4.19`, Linux 4.19.325 и
  `k61v1_64_bsp_defconfig`. В нём есть Rogue DDK
  `m1.15ED6070602`, совпадающий с версией BF7 PowerVR userspace.
- Проверены пользовательские находки:
  - `mediatek-android-development/...mt6761-62-4.9` = Linux 4.9.117,
    старый donor;
  - `LCM-MTK/...mt6761` = Linux 4.4.146, не candidate baseline.

## 2026-08-13 — локальная проверка BF7 DTB и X657B source

- `native-linux-build/unpack-bootimg/dtb` имеет MTK container header 64 B;
  вложенный FDT начинается с offset 64 и имеет размер 121167 B. Он успешно
  декомпилируется `dtc` после извлечения в `/tmp`.
- В извлечённом BF7 DTB подтверждены основные узлы MT6761: eMMC/SD (`msdc`),
  USB, ConnSys/Wi-Fi, audio, CCCI, camera framework, `mt6761-gpufreq` и GED.
- Сопоставление с X657B `mt6761.dts` показало общий набор ключевых node labels:
  `touch`, `wifi`, `consys`, `usb`, `msdc0/1`, `audio`, `gpufreq`, `ged`,
  `CCCI`, camera. X657B — очень хороший SoC/BSP baseline, но не BF7 board DTS.
- В X657B `k61v1_64_bsp_defconfig` включён `CONFIG_TOUCHSCREEN_MTK_GT1151=y`;
  его generic DTS node `compatible = "goodix,touch"` совпадает с BF7, хотя
  BF7 runtime использует `omnivision_tcm_spi`. Этот compatible не достаточен
  для определения контроллера.
- Первоначальный вывод по `CONFIG_DEVTMPFS` в X657B defconfig отменён:
  `k61v1_64_bsp_defconfig` — лишь короткий fragment (484 строки), а не
  развёрнутый итоговый `.config`. Отсутствие строки не доказывает default `n`.
  OEM BF7 явно имеет `# CONFIG_DEVTMPFS is not set`, поэтому devtmpfs остаётся
  отдельным, поздним patch после baseline.
- Публичный исходник exact `omnivision_tcm_spi` не найден. Infinix X6515
  является зацепкой: его опубликованный лог перечисляет тот же драйвер и
  несколько совпадающих периферийных имён, но открытого kernel source X6515
  не найдено.

## 2026-08-13 — найден публичный OVT TCM donor

- Локальный `bf7-kallsyms.txt` уточнил имя BF7 touchscreen driver:
  `ovt_tcm_*`, а не только sysfs name `omnivision_tcm`. Есть `ovt_tcm_spi_*`,
  `ovt_tcm_wait_hdl` и `ovt_tcm_run_application_firmware`; driver статически
  встроен в OEM kernel.
- Через GitHub commit search найден
  `Nhan918/a13-kernel-source:drivers/input/touchscreen/ovt/td4150_spi`.
  Из 77 BF7 `ovt_*`-символов 39 совпадают буквально с этим source; совпадают
  SPI transport, HDL lifecycle, reset/IRQ/identify и firmware start.
- Это объясняет рабочую native-цепочку: cold controller ROM bootloader ->
  `/lib/firmware/synaptics/hdl_firmware.img` -> reset -> Host Download
  Firmware. `bf7-touch-init.service` является корректным runtime workaround.
- Source рассчитан на другой, Android 13-era Samsung target. Его нельзя
  просто включить в X657B 4.19; следующий безопасный этап — code diff и
  compile-only adaptation, без boot test.
- Поверхностный compile-only audit: в donor около 7.5k строк; обязательны
  generic SPI/input/firmware/PM и MTK `CONFIG_SPI_MT65XX`. X657B содержит
  `drivers/spi/spi-mt65xx.c`, но не тот public header path, который использует
  donor. Кроме того, есть Samsung-specific `sec_input_*`, `sec_delay`,
  `input_info/input_err`, secure-touch/factory hooks. Это решаемая адаптация,
  но не "включить один Kconfig".

## 2026-08-13 — найден более близкий neutral OVT 4.14 source

- `zany415/android_kernel_unisoc_qogirl6` — Linux 4.14.303 — содержит
  `drivers/input/touchscreen/omnivision_tcm/`. После извлечения имён функций
  совпали 45 из 77 BF7 `ovt_*` symbols: больше, чем у Samsung Android 13
  doнора (39).
- У этого source normal Kconfig `TOUCHSCREEN_OMNIVISION_TCM_*`, обычный
  `SPI_MASTER` transport и нет Samsung/Unisoc platform callbacks. Его
  ZeroFlash loader запрашивает literal `hdl_firmware.img`; это напрямую
  совпадает с BF7 vendor firmware и подтверждённой native boot chain.
- Условие для будущей сборки: добавить его как отдельный код в X657B 4.19,
  сначала сделать compile-only и никогда не заменять BF7 DTB на DTS донора.

## 2026-08-13 — точная boot metadata сохранённого заводского образа

- `firmware-backup/native-test-preflight/boot_b-before-native.img` распакован
  read-only через `unpack_bootimg`. Это Android boot header v2, page 2048,
  Android 12 / patch 2024-12, product `CY-BF7-H6127`.
- Layout: kernel `0x40080000`, ramdisk `0x51b00000`, tags `0x47880000`, DTB
  `0x47880000`; sizes kernel 11889450 B, ramdisk 13686486 B, DTB container
  121231 B. SHA-256 самого boot image:
  `83bd274d1c286389b25ebe96df4a29202fbe0cfea960e8d1db4de920f9368ba8`.

## 2026-08-13 — закрыт пробел OEM config, уточнён Wi-Fi source gap

- Работающий BF7 read-only отдал полный `/proc/config.gz`: 5764 строки,
  SHA-256 `2fc786c80b90669e010b29e6a9bf00a62b27d19c2cbc800d84ff8a04d8f57099`.
  Таким образом полный OEM config больше не является неизвестным: его можно
  снять повторно с телефона и использовать входом для будущего `olddefconfig`.
- В OEM config дословно присутствуют все Kconfig switches neutral OVT donor:
  `CONFIG_TOUCHSCREEN_OMNIVISION_TCM=y`, `..._SPI`, `..._CORE`, `..._DEVICE`,
  `..._TESTING`, `..._REFLASH`, `..._RECOVERY`, `..._ZEROFLASH`,
  `..._DIAGNOSTICS`. Это сильнейшая проверка, что найденный source-family
  действительно соответствует BF7 driver family.
- X657B `k61v1_64_bsp_defconfig` — fragment: 484 explicit lines и 68
  explicit disabled options. До `make olddefconfig` он не годится для полного
  config diff; при этом 477 его symbol names уже пересекаются с OEM.
- В X657B есть `drivers/misc/mediatek/connectivity/common` и WMT adapter,
  но нет исходников `wlan_drv_gen4m`, `wmt_drv` и `connfem`, используемых BF7.
  Это закрыто частично последующей находкой отдельного 4.19.325 Wi-Fi donor;
  остаются source/version diff и compile-only validation, а не отсутствие кода
  как такового.
- На host пока нет ARM64 cross compiler (`aarch64-linux-gnu-gcc/binutils`),
  `clang`, `lld` и `bc`. Это не исходниковый блокер, а воспроизводимо
  устранимая build dependency перед compile-only этапом.

## 2026-08-13 — найден 4.19.325 Wi-Fi/WMT donor

- `StasGr12/kernel_ubports_mt6781` — Linux 4.19.325, то есть ровно той же
  версии, что и chosen X657B baseline — содержит полный source
  `drivers/misc/mediatek/connectivity/wlan/core/gen4m`, adaptor и WMT common.
  Он собирает модули с теми же именами: `wlan_drv_gen4m`, `wmt_drv`,
  `wmt_chrdev_wifi`.
- Его gen4m Makefile имеет explicit `MT6632` chip path, а OEM BF7
  `wlan_drv_gen4m.ko` содержит `MT6632`, `WIFI_RAM_CODE_soc1_0` и требует
  `wmt_drv,wmt_chrdev_wifi`. WMT donor также включает `wmt_ic_6632.c`.
  Это сильное, но пока косвенное совпадение; exact hardware revision нужно
  доказать diff/compile-only test, не подгрузкой чужого `.ko`.
- Все OEM Wi-Fi `.ko` имеют vermagic exact OEM kernel и Android Clang 11.
  Они не переносимы бинарно на custom kernel, но служат ABI/reference
  артефактами.
- Host build dependencies подтверждены в configured Arch repos:
  `aarch64-linux-gnu-gcc`, `aarch64-linux-gnu-binutils`, `llvm`, `lld`, `bc`.

## 2026-08-13 — начат первый local compile-only baseline

- X657B source cloned locally at exact commit
  `75934cf7f57bc8ffbdda9e77b1072a32a9b74221`; upstream declares Linux
  4.19.325. `make k61v1_64_bsp_defconfig` produced a full 5584-line config
  with `CONFIG_MTK_PLATFORM="mt6761"`, `CONFIG_MTK_COMBO_WIFI=y` and
  `CONFIG_MTK_GPU_SUPPORT=y`.
- Для первого Image-only прогона intentionally disabled camera/ISP options:
  это уменьшает unrelated GCC diagnostics and does not claim camera support.
  Wi-Fi, display, USB, audio, GPU and MTK platform options remain enabled in
  this donor baseline.
- Host now has `aarch64-linux-gnu-gcc` 16.1.0, matching binutils and `bc`.
  A first parallel build exposed stale vendor code warnings and unreliable
  temporary-file races; compile-only validation was restarted with `-j1`.
  Limited `-Wno-error=<class>` flags cover old format/dead-code diagnostics
  only; they do not hide unresolved symbols, linker failures or type errors.
- Small local source compatibility fixes were necessary for GCC 16 in
  scheduler declarations, MediaTek GPU enum typing, CCCI fallback code and
  cpufreq public function signatures. They are uncommitted, `git diff --check`
  is clean, and must be reviewed/exported as a patch series before any boot
  image is ever considered. No artifact was packed and nothing was sent to
  BF7.

## 2026-08-13 — первый `Image.gz` собран локально

- Однопоточный GCC 16 compile-only build завершился успешно. Артефакты:
  `Image` 34,643,976 B, `Image.gz` 12,461,518 B, `vmlinux` 319,453,984 B и
  `Module.symvers` 673,823 B. `vmlinux` подтверждён как little-endian AArch64
  ELF64; его entry point `0xffffff8008080000`.
- SHA-256 `Image.gz`:
  `b0373154ad1da9fe7c6abdc0df81674a69eb279fb9f79e2aae1c7050b27d7bac`.
  BuildID `vmlinux`: `4f9c8d3c99e54b5eaf83b2edb9ae9b9f238db7c7`.
- Помимо ранее записанных GCC compatibility adjustments, donor tree contained
  one literal syntax defect in `drivers/misc/mediatek/usb20/musb_qmu.c`:
  debug format string was physically split across lines. It was repaired only
  as a C string literal; USB code path then compiled successfully.
- Итоговое source diff содержит 7 маленьких файлов, `git diff --check` clean.
  Получение Image не означает, что его можно boot/flash: next task is OEM
  config/DTB audit and reproducible patch export. No boot image was packed and
  nothing was copied to BF7.

## 2026-08-13 — OEM BF7 config: concrete source gaps and headless route

- Exact BF7 config copied read-only from `/proc/config.gz` to
  `kernel-work/configs/bf7-oem-4.19.191.config`; SHA-256 matches the recorded
  `2fc786…57099`. `olddefconfig` accepts it in X657B tree (5575 effective
  lines), preserving platform, USB, Wi-Fi, audio, power, CCCI and Rogue.
- Full OEM compile stops on missing BF7 camera sources `hi1336_mipi_raw`,
  `gc05a2_mipi_raw` and `fake_serial_yuv`; camera is not substituted from a
  donor. Camera-excluded compile next stops on missing exact LCM source
  `nt36528_hdp_dsi_vdo_djn_boe_bf7`.
- The headless profile disables camera sensor stack, `MTK_LCM` and `MTK_FB`.
  `MTK_FB` must be off with LCM: otherwise AEE/SMI retain DAL/DISP calls that
  are deliberately absent with the mtdummy display path. Generic FB and all
  server-critical platform facilities remain enabled. No image is packed and
  no device mutation occurred.

## 2026-08-13 — BF7 headless server `Image.gz` compiled locally

- `bf7-headless-server-nofb` completed a full compile/link: Image 30,359,560
  B, Image.gz 11,374,787 B, vmlinux 304,192,112 B and Module.symvers 672,610
  B. vmlinux is AArch64 ELF64, entry point `0xffffff8008080000`, BuildID
  `b435047bc1e9ead1262fde0122511b33c76dc873`.
- Image.gz SHA-256:
  `88fe5e0f8d800785fada5a6d162896ea62c482b5ac414cdca7aee81f4bb379f6`.
  Only one upstream section-mismatch warning remains from modpost; there are
  no link errors or unresolved symbols.
- This profile preserves BF7 OEM config values for MT6761, USB HDRC, Wi-Fi,
  audio, power, CCCI/CCMNI, BTIF and Rogue GPU, while deliberately excluding
  unavailable camera and LCM/display sources. It is a validated local
  headless candidate only: no Android boot container has been created, and no
  file or command was sent to BF7.

## 2026-08-13 — display-source search and OEM binary recovery route

- Exact public-code searches for the three BF7 LCM identifiers found no
  matching source. The only close public TECNO donor found (`KE6`) uses a
  different controller (`nt36525b`) and a different LCD-bias chip (`nt5038`),
  while BF7's OEM kernel exposes `ocp2130`; it is a reference only, not a
  safe substitution.
- The stock BF7 kernel image can nevertheless be analysed without touching
  the phone. Its uncompressed ARM64 Image retains the exact three `LCM_DRIVER`
  descriptors, their function pointers, and the static panel command tables.
  `CONFIG_KALLSYMS_ALL=y` plus raw Image offsets gives a consistent runtime
  relocation base (`0xffffff9d11480000`) for all three descriptors.
- The OEM descriptor code shows the LCM data ABI directly: the `nt36528`
  initialization loop has 337 records with a 0x80-byte stride; the first
  `td4160` initialization table has 42 records with a 0x100-byte stride.
  Therefore a source-independent reconstruction is feasible: recover exact
  command tables and parameters from the OEM Image, then place them into a
  small LCM driver using the already-present MediaTek LCM interface. This is
  a local reverse-engineering fallback, not an implemented or device-tested
  driver. No artifact was copied to BF7.
- A reproducible extractor now exists at
  `kernel-work/tools/extract_bf7_lcm_tables.py`. It pins the uncompressed OEM
  Image SHA-256 to `3cc9f88b1930660d344c9a97829f044d436e90b2896a0a03adb37ed339fd1db8`
  and emits all three exact initialization streams. Its first verification
  recovered 42 / 337 / 42 records for TD4160-HX / NT36528 / TD4160-TXD.
- The binary also resolves the otherwise opaque power/reset sequence:
  the panel code calls `tpd_gpio_output(0xae, ...)` and
  `disp_dts_gpio_select_state(...)`. Those board wiring helpers are not yet
  present as a compatible pair in the X657B source tree, so a future port must
  reconstruct their BF7 pinctrl states too; copying only a command table would
  be incomplete. This is a concrete, bounded source gap rather than an
  Android-userspace dependency.

## 2026-08-13 — active TD4160-HX BF7 LCM reconstructed to compile-only level

- Added `drivers/misc/mediatek/lcm/bf7_lcm_recovered/` with a new
  `td4160_hdp_dsi_vdo_hx_inx_bf7` driver. The active panel was established
  read-only from `/proc/device-tree/chosen/atag,videolfb` on the running BF7:
  its embedded LCM name is exactly `td4160_hdp_dsi_vdo_hx_inx_bf7`. A separate
  `atag,videolfb-lcmname` property contains an obsolete 720x1280 NT35521 name;
  the structured `atag,videolfb` record is the source that actually carries
  framebuffer geometry and must take precedence.
- Its 42 DSI records are generated
  by `tools/extract_bf7_lcm_tables.py` from the pinned OEM Image; they are not
  copied from a donor phone. The raw generated header has SHA-256
  `bcb7e8e412d24ae858f18f2bcefa6ac1a05c7a158c7fa5dad5f42d11b914040f`.
- The OEM `LCM_PARAMS` payload is preserved as a 0x518-byte word map:
  720x1612, four DSI lanes, physical 67x152 mm, physical 67932x152092 um and
  the exact recovered MTK timing words. This avoids making assumptions about
  private `LCM_PARAMS` field layout across MediaTek branches.
- OEM `this_state_name` was decoded directly from the stock Image and matches
  `video/mt6765/videox/disp_dts_gpio.h` byte-for-byte. Thus OEM states
  `4 -> 3 -> 4` mean `lcm_rst_out1_gpio -> lcm_rst_out0_gpio ->
  lcm_rst_out1_gpio`. MT6761 intentionally selects that MT6765 display stack
  in this donor tree. The reconstructed code calls this exact pinctrl path,
  rather than the generic video-mode `set_reset_pin` helper, which takes a
  different MMSYS reset route.
- `tpd_gpio_output(0xae, ...)` was traced into the local generic TPD source:
  `0xae` deliberately takes its non-`1` branch, which selects the touchscreen
  reset pinctrl state. The recovered driver now makes the exact OEM
  `1 -> 0 -> 1` sequence with its original 2/2/5 ms delays; it does not
  silently replace that call with an unrelated raw GPIO utility.
- The exact TD4160-HX suspend sequence was also verified from OEM data:
  DCS `0x28`, 20 ms, DCS `0x10`, 100 ms.
- The recovered panel object and the central `mt65xx_lcm_list.o` compile
  successfully in isolation, including the exact TD4160 descriptor reference.
  The complete local `lcm/built-in.a` was then built with
  `CONFIG_CUSTOM_KERNEL_LCM="bf7_lcm_recovered"`; `nm` shows that
  `lcm_driver_list` references and the archive defines
  `bf7_lcm_recovered_td4160_hx_lcm_drv`.
- Full parallel compile-only validation was completed on 2026-08-22 with
  `ARCH=arm64`, `CROSS_COMPILE=aarch64-linux-gnu-` and `-j8`.  It reached
  `vmlinux`, kallsyms, `System.map` and final `arch/arm64/boot/Image` without
  unresolved symbols. The final Image is 31,641,608 B, SHA-256
  `31c8ae4033a214c07752c1236a0662bfc2f9b479a0e9eaf55ef9e589d218662a`;
  its `vmlinux` is a little-endian ARM64 ELF with BuildID
  `044ebd1787ce267e6a9a5d5d870e7ad29635b042`.
- The full link exposed and resolved one real integration defect in the donor:
  its touchscreen Makefile added the Mediatek TPD core only for MT6739.  The
  condition now also covers MT6761, so final `vmlinux` contains
  `tpd_gpio_output`, `disp_dts_gpio_select_state`, `lcm_driver_list` and
  `bf7_lcm_recovered_td4160_hx_lcm_drv`. This is necessary for the exact
  recovered BF7 reset sequence; it is not a userspace workaround.
- GCC 16 exposes numerous pre-existing diagnostics in this Android vendor
  tree. Limited `KCFLAGS=-Wno-error=<class>` were passed only at build time to
  keep stale warning classes visible but non-fatal; no extra source edits were
  made merely to silence them. One pre-existing `modpost` section-mismatch
  warning remains and should be inspected before any future boot experiment.
- No boot image was packed, copied, flashed or otherwise sent to BF7.

## 2026-08-22 — независимый аудит recovered LCM: boot-preflight возвращён в работу

- Независимый local audit повторно разобрал pinned OEM Image, пересобрал
  `Image modules dtbs` в новом output-каталоге и подтвердил: смещения,
  формат 42×0x100-byte TD4160-HX init-records, терминаторы, reset 4→3→4 и
  suspend-table восстановлены корректно. Это сильные binary-derived данные,
  но ещё не hardware validation.
- Аудит нашёл подтверждённую ошибку в raw word-map `LCM_PARAMS`: значение
  RGB888 было записано в word 125 (`data_format.trans_seq`) вместо word 127
  (`data_format.format`). До исправления это даёт невалидный `trans_seq=3`
  и default RGB565 format. Raw layout больше нельзя считать достаточной
  проверкой; параметры должны быть выражены именованными полями с guards на
  config-dependent layout.
- Recovered descriptor не содержал `init_power`, `suspend_power` или
  `resume_power`, тогда как OEM descriptor их имеет. OEM power path включает
  LCD bias ENP/ENN pinctrl states и четыре двухбайтовые I2C-programming
  операции. Без восстановления точного I2C client/address и callback path
  cold-init screen test неинтерпретируем: чёрный экран мог бы означать только
  отсутствие аналогового питания.
- Вызов `tpd_gpio_output(0xae, ...)` из LCM init признан небезопасным для
  donor boot ordering: TPD platform driver регистрируется позднее mtkfb и
  заполняет свой pinctrl в probe. В custom kernel LCM не должен зависеть от
  touchscreen reset helper; зависимость и временная MT6761 Makefile правка
  подлежат удалению/замене на panel-owned path.
- `modpost` section mismatch из полного build не относится к LCM: это
  donor MediaTek cpuidle function, помеченная `__init` и одновременно
  exported. Тот же warning есть в headless profile без recovered LCM. Его
  фиксируем как low-priority donor debt, не как display blocker.
- Финальный Image SHA не является воспроизводимым identity без фиксации
  `KBUILD_BUILD_TIMESTAMP` и build user/host: повторная сборка отличается
  только metadata version string. Следующий full preflight обязан pin these
  variables.
- До закрытия параметров, power path и DT/DTBO pinctrl provenance custom
  Image нельзя паковать или boot-test. Ничего не отправлялось на BF7.

## 2026-08-22 — LCM audit fixes и DTBO provenance закрыты локально

- Устранён подтверждённый дефект `LCM_PARAMS`: raw word-map заменён на
  именованные присваивания; `data_format.format` теперь явно
  `LCM_DSI_FORMAT_RGB888`. Добавлены build-time guards на отключённые
  config-варианты layout и `BUILD_BUG_ON` для размера `0x518` и exact offset
  поля format (word 127). Таким образом RGB888 больше не может незаметно
  попасть в `trans_seq` (word 125).
- Зависимость LCM от `tpd_gpio_output()` полностью удалена, как и временная
  правка Makefile touchscreen. LCM больше не вызывает touch pinctrl ни при
  initialisation, ни на ESD recovery. Panel reset остаётся OEM-derived
  `LCM_RST_OUT1 -> OUT0 -> OUT1`; suspend power использует только
  panel-owned reset-low.
- Локально извлечён заводской `dtbo.img` из factory package
  `BF7-H6127...241207V1848_incremental_241209V2366`; его SHA-256
  `e161195ec3bab4cd4363614f0a605465cdc1f109a2dcdb9c84ff94281f419c30`.
  Overlay подтверждает все шесть panel pinctrl states на `mtkfb` и bias node
  `i2c_lcd_bias@3e` на I2C5 (`compatible = "mediatek,i2c_lcd_bias"`).
  Следовательно отсутствие этих узлов в base DTB объяснено: они приходят из
  штатного DTBO, а не являются отсутствующими BF7 данными.
- Добавлен минимальный встроенный BF7 bias client `ocp2130`. Он связывается
  только с указанным factory DTBO node и повторяет OEM I2C sequence:
  `00=14`, `01=14`, `03=33`, `ff=80`, с задержками 1/1/1/10 ms. Power
  callbacks повторяют заводской порядок: ENP1 -> 5 ms -> ENN1 -> I2C;
  suspend: panel reset-low -> 5 ms -> ENN0 -> 5 ms -> ENP0; resume выполняет
  reset-low и повторно программирует bias. Это устраняет отсутствовавший
  analog power path без включения Android userspace.
- Отдельно исправлен низкоприоритетный `modpost` defect донора: активная для
  MT6761 `mtk_cpuidle_framework_init()` экспортировалась, оставаясь в
  `__init` section. Снята только неверная `__init` annotation; export
  сохранён, поэтому поздний модуль не сможет ссылаться на освобождённую
  init-память.
- Предыдущие записи, утверждавшие, что recovered LCM должен вызывать
  touchscreen reset `1 -> 0 -> 1`, являются историческими и заменены этим
  audit result. Из OEM sequence сохранена только panel-owned часть.
- Это всё ещё compile-only результат. До отдельного explicit approval нельзя
  копировать образ на телефон, прошивать `boot_b` или начинать boot test.

## 2026-08-22 — полный post-audit build завершён

- Новый output-каталог `kernel-work/build/bf7-lcm-audit-fixed-20260822/`
  собран с нуля от saved display profile через `Image modules dtbs`, ARM64
  GCC 16 и pinned build metadata (`Sat Dec 7 22:52:37 CST 2024`, user `bf7`,
  host `native-linux`). Результат: `Image` 31,639,560 B, SHA-256
  `04bbc829b32fe174c96bf0e9478c956e7037d4a26a878199d74b9e21c02bb902`;
  `vmlinux` SHA-256
  `de2c5629113b7a4da5609c2ed508d12a2a90e2eaff8c185833452ad2a8cf694b`;
  effective `.config` SHA-256
  `32359b7fe82523b0e2138b421a549f84347196ef43e4e8e51daa975ef668f8cd`.
- Full link, kallsyms and all 11 enabled modules completed. `vmlinux` has
  `bf7_bias_i2c_init`, `bf7_bias_probe`, all three BF7 power callbacks and
  the recovered LCM descriptor. A scan of the final build log reports no
  `section mismatch`, `undefined reference` or compiler/linker `error`.
- `modpost` still reports two duplicate exports between optional
  `mt6357-accdet.ko` and `mt6359-accdet.ko`. Both options are already enabled
  as modules in the untouched OEM BF7 config; this is a donor/OEM module
  metadata warning, not an in-kernel link collision or a display regression.
  It is intentionally recorded rather than guessed away by disabling one
  audio/jack driver before a hardware audit.
- GCC 16 exposed two further dead-code/build hygiene defects outside the LCM
  work, and both were corrected rather than hidden: HACC disabled log macros
  are now valid variadic no-ops; ALS/PS RGBW function definitions now match
  their `int value[4]` declarations. A legacy USB debug macro keeps its
  original semantics and is covered only by an explicit host build warning
  downgrade for `misleading-indentation`.
- `make dtbs` does not emit a DTB for this configuration; this is expected,
  because BF7 uses the retained OEM base DTB plus separate factory DTBO.
  The custom `Image` was later placed into a local diagnostic boot container;
  it has not been copied, flashed or otherwise sent to BF7.

## 2026-08-22 — local diagnostic boot container preflight

- Explicit local-only preparation approval was received. The working native
  reference `native-linux-build/bf7-native-debian-v20-mark-success.img` was
  unpacked without modification. Its ramdisk and base DTB are the exact
  payloads used for the candidate, so early RNDIS, SSH and the B-slot
  mark-success helper remain unchanged.
- Candidate to use, if a separate flash approval is given:
  `native-linux-build/bf7-native-kernel-lcm-diagnostic-20260822-r2.img`.
  SHA-256: `b09bf12079900afab97f92ccc26328035dfb6bd80f26461c1f0f4cd6f2ed34e1`.
  Size: 17,313,792 B, safely below the known 33,554,432 B `boot_b` image
  capacity.
- The candidate kernel is the post-audit `Image`, gzip-compressed with
  deterministic gzip metadata. Its uncompressed SHA-256 is
  `04bbc829b32fe174c96bf0e9478c956e7037d4a26a878199d74b9e21c02bb902`.
  A reverse unpack verified byte-for-byte equality for the compressed kernel,
  ramdisk and DTB; decompression verifies equality with that audited `Image`.
- All boot-header fields that affect loading match native v20: Android header
  v2, page size 2048, board `CY-BF7-H6127`, kernel address `0x40080000`,
  ramdisk address `0x51b00000`, tags/DTB address `0x47880000`, and the known
  native command line. Factory DTBO is deliberately not repacked or modified.
- An earlier local file without the `-r2` suffix has kernel address
  `0x40008000` and is invalid. It is retained only as an audit trace and MUST
  NOT be flashed.

## Следующая read-only работа

1. Снять полный manifest выбранных исходников: commit, config, MT6761 DTS,
   Kconfig и наличие нужных BF7/OEM driver IDs.
2. Сопоставить OEM config и X657B 4.19 config без сборки/прошивки.
3. Провести compile-only feasibility audit OVT donor против X657B 4.19:
   составить список API/config/platform-hook различий, не интегрируя код.

## 2026-08-23 — новый основной ALPS baseline и точный OEM toolchain

- Старый X657B 4.19.325 baseline понижен до donor/reference: он слишком далеко
  от OEM 4.19.191, а его собственная история не даёт уверенности в bootability.
- В blobless history `bengris32/android_kernel_mediatek_4.19` найден ALPS
  commit `6f3a19332a49efbc3e3e960468b198818addc137`, который добавляет полный
  PowerVR DDK `m1.15ED6070602`. Это точное совпадение с BF7 OEM userspace.
- Создан worktree `kernel-work/mediatek-mt6761-alps188-gpu115`, ветка
  `codex/bf7-alps191-base`. Android stable 4.19.191 commit
  `011b73c995f35959b39ccde045addbc1862fa3e6` merged в commit
  `9c2539cb9aef6f2fc85cad43fe76eab90916820b`. Разрешены три конфликта:
  PM QoS, scheduler/uclamp и stackdepot; vendor hooks сохранены поверх stable
  fixes.
- Из работающего BF7 через read-only SSH подтверждён OEM toolchain:
  `Android (6443078 based on r383902) clang 11.0.1`, LLD 11.0.1. Официальный
  archive этого exact prebuilt сохранён в `kernel-work/toolchains/clang-r383902`.
- Аудированный BF7 LCM перенесён в новый baseline. Для совместимости с exact
  OEM layout в старый ALPS `lcm_drv.h` добавлены недостающие 48 байт полей и
  современный enum DSI format; compile-time проверки снова требуют размер
  `0x518`, offset word 127 и RGB888=3.
- Для будущего теста создан локальный контейнер
  `native-linux-build/alps191-trial-v1`. Он использует exact OEM DTB,
  diagnostic initramfs, RNDIS/telnet, аппаратный watchdog, software fastboot
  deadline и только bounded read-only storage probes. В каталоге нет команд
  `fastboot flash`, `erase` или `set_active`.
- В этом же прогоне доказано, что BF7 LK downloaded-image path аварийно
  завершается (`lk_crash`) даже для exact known-good boot image и его полного
  32 MiB backup. Поэтому новый candidate нельзя честно проверить через
  `fastboot boot`, а запись в A/B slot не разрешена. Телефон оставлен на
  рабочем OEM-native kernel, разделы не менялись.

## 2026-08-23 — финальный ALPS v1 build и matching Wi-Fi modules

- Полный clean build точным OEM Android Clang/LLD 11.0.1 завершён. Kernel
  release: `4.19.191-bf7-native-alps191-v1`; `Image` SHA-256
  `4f83e1cb0f51bf66fb0c4d723169b5a81ef39e4c0fe891c9c11df62daf62f234`,
  `vmlinux` SHA-256
  `237076ba12da6d6ee6fcec867da7679c5331b7fe6abaa60be0a6bd25408aff08`.
  Повторная чистая сборка дала те же файлы.
- Найдены официальные Motorola MediaTek connectivity sources tag
  `MMI-STA32.79-77-28` с exact MT6761 WMT platform, WLAN adaptor и gen4m
  Connac/6765 AXI path. Три `.ko` чисто пересобраны против этого exact kernel.
- Для каждого импортируемого символа проверен CRC по kernel/dependency
  `Module.symvers`: `wmt_drv` 238, `wmt_chrdev_wifi` 43,
  `wlan_drv_gen4m` 304; во всех случаях `unresolved=0 crc_mismatch=0`.
- Создан trial v2 с Wi-Fi firmware/Bionic WMT runtime и matching modules.
  Wi-Fi не auto-load: сначала должен пройти RNDIS/storage baseline, затем
  `/opt/bf7-wifi/start.sh` запускается вручную под watchdog.
- Для v1/v2 исправлена воспроизводимость cpio: нормализованы mtime/inode
  metadata. Две последовательные сборки каждого образа совпали. Финальный v1
  SHA-256 `c79ec5124af1a56bfb336fdd4330044c8ecbc9d74aaadd944bc63ab2026486b7`,
  v2 SHA-256 `15816e93d2f059bbe94b2c9b9eb7e69991b84e93f65ee0282fa2aeae279e6b8d`.
- Добавлен host monitor, который различает known-good USB serial и trial
  serial, сохраняет report/dmesg и продлевает hardware и software watchdog
  lease через tmpfs token. При исчезновении monitor продление прекращается.
  Скрипт не содержит boot/flash/reboot/slot-команд.
- Read-only проверка текущего рабочего BF7 показала, что OEM kernel собран с
  `CONFIG_KEXEC=n`. Значит chainload нового ядра из Debian невозможен. Вместе
  со сломанным LK `fastboot boot` это оставляет runtime test заблокированным
  без рискованной записи слота; запись не выполнялась.

## 2026-08-23 — exact AW32257 module и trial v3

- На официальной странице Awinic найден полный MTK driver archive
  `AW322XX_Driver_MTK_V1.0.0.zip`, SHA-256 `975e7fa29c73f1968737112786df7b348a49306675cb0eb1de3de04843e38f4f`.
  Внутри находится exact legacy `AW32257_Driver_MTK_V1.2.0.zip`.
- Live merged DT read-only подтвердил exact node `aw32257@6a`, compatible
  `awinic,aw32257`, GPIO property `awinic,chg-en-gpio` и termination current
  240000 uA. 46 из 53 OEM kallsyms AW32257 совпали с donor буквально.
- Source адаптирован к этим DT property и power-supply enum текущего дерева.
  Probe сначала проверяет vendor ID и только затем делает hardware setup.
- Внешний `aw32257.ko` собран exact Clang 11 против финального kernel:
  SHA-256 `244486945bf3bbb8ffe0fd1eddf30de7bec63c8850def8a17d9e7001ad7936c4`,
  37 imports, `unresolved=0 crc_mismatch=0`.
- Trial v3 включает этот `.ko`, но никогда не загружает автоматически.
  Loader требует literal подтверждение `--load-with-register-writes`; unload
  через `rmmod` запрещён из-за отсутствия vendor remove callback.
- Trial v3 дважды воспроизводимо собран: 20,838,400 bytes, SHA-256
  `b3619369a7b6b92c5a50346a7a6b2d614d32dd3c02824fe2828d5a7fc74b7ec5`.
  Reverse unpack и полный v2+v3 preflight прошли. Телефон не перезагружался,
  разделы не менялись; read-only SSH подтвердил known-good OEM kernel в B.
- Заводской `aw32257_driver_probe` дизассемблирован по OEM kallsyms. Реальная
  BF7 sequence: `reg6=ff`, `reg0=d0`, `reg1=38`, `reg8=8e`, затем mapping
  `awinic,iterm` в ITERM bits регистра 4. Candidate синхронизирован с ней.
- В Awinic V1.2 и OEM binary подтверждён off-by-one: buffer длины `wr_len`
  получал `reg` плюс memcpy ещё `wr_len` байт. Candidate сохраняет ту же
  wire length, но копирует `wr_len - 1`, устраняя heap overwrite.
- PID 1 исправлен для фактического использования `CONFIG_DEVTMPFS`: обычный
  tmpfs больше не скрывает kernel-created nodes. При невозможности devtmpfs
  остаётся прежний ручной fallback. Финальный hash v3 выше уже включает эту
  правку и повторно воспроизведён.

## 2026-08-23 — независимый rebuild и удалённый rollback-контур

- Повторная полная сборка в новом `O=` обнаружила единственную прежнюю
  невоспроизводимость: в `Image` отличались ровно два 8-байтовых GNU build ID.
  Исполняемый код после обнуления этих двух полей совпадал побайтно. Причиной
  оказался абсолютный build path в DWARF, который LLD учитывал при build-id.
- Wrapper теперь задаёт `-fdebug-prefix-map` и `-fmacro-prefix-map` для C и
  assembler. Две полные 16-поточные сборки в разных каталогах дали одинаковые
  `Image` (`093828bab73acf47e02ba3193868fc6a74fb0f3c775570d04f69d18b89876e02`)
  и `vmlinux` (`835ae034bd9663175b92e3949a58d09fd686436ee1356e7fb48dec232f582f2c`).
- Все trial images перепакованы из этого финального kernel. Актуальные SHA:
  v1 `d694563c70c6f07453538e543a3b9b0d52f5cd876806e220d76e1ba258409bb4`,
  v2 `0a4ffd5046e6af56cb97a02e60d51a18f1f663e3e853d3cdebe21e8886e24637`,
  v3 `662eede19a2b0d822bd34a79cd1e84bffb1549f576363ee18057b37fc5f1b7b9`.
  Полный каскад v1/v2/v3 preflight прошёл; v3 дважды совпал побайтно.
- Для диагностики LK подготовлен отдельный legacy header-v0 probe на точном
  OEM kernel, без доступа к eMMC. Он даёт RNDIS `1d6b:0112`, shell на 2324,
  software return-to-fastboot через 60 секунд и hardware-watchdog fallback.
  Исходный SHA до исправления PTY mountpoint был `13842b2d61d21ecd032ffd45f7821f78da823acdf82872fc19e0a90b8466cb09`;
  probe после исправления PTY, но до исправления DT-table packaging —
  `604691a881cc0a526b4ccedd8753ddc3b2a3449049b57fa2bfc4f36f5d1cca54`.
- Host runner требует явный token, проверяет SHA/preflight, отвергает
  userspace fastbootd и содержит только `fastboot boot`. В этом прогоне он не
  запускался; телефон и разделы не изменялись.
- Устранена ошибка плана теста: после legacy probe v3 больше не подаётся LK в
  проблемном header-v2. Сборка создаёт отдельный v3 header-v0 с новым
  `Image.gz-dtb`. Его первый SHA до исправления PTY mountpoint был
  `92175335a95881ae21843469ec959b51d678755676eee8862ec41053fdd2c1d8`;
  SHA после исправления PTY, но до исправления DT-table packaging —
  `61f962f9427a695ee6ca7521c14b5b3eadd51511cc920f3a1bb038c8da92df32`.
  Две финальные сборки совпали; reverse unpack подтвердил kernel payload и
  ramdisk.
- Исправлена ранняя userspace-ошибка: mount devtmpfs/tmpfs скрывал заранее
  созданный `/dev/pts`, а sysfs мог скрыть точки configfs/pstore. Mountpoint-ы
  теперь создаются после mount, devpts монтируется явно и `/dev/ptmx`
  восстанавливается до запуска telnetd. Это вошло и в OEM probe, и во все
  trial images.

## 2026-08-23 — runtime legacy probe: LK crash

- Настоящий bootloader fastboot подтверждён: `is-userspace=no`, slot B,
  unlocked, slot-successful B=yes, unbootable=no, retry-count=6.
- Exact probe
  `604691a881cc0a526b4ccedd8753ddc3b2a3449049b57fa2bfc4f36f5d1cca54`
  передан только командой `fastboot boot`; partition write не выполнялся.
- Download завершился `OKAY`, boot transport вернул
  `FAILED (Status read failed (No such device))`.
- USB прошёл через Preloader `0e8d:2000` и вернулся к known-good RNDIS
  `1d6b:0111`; probe `1d6b:0112` отсутствовал.
- Установленный OEM-native kernel снова загрузился в slot B, cmdline сообщает
  `androidboot.bootreason=lk_crash`.
- `LEGACY_PROBE_PASSED` не создан; v3 runner остаётся заблокирован. ALPS v3
  не запускался, телефон и разделы остались в known-good состоянии.

## 2026-08-23 — исправление legacy DTB packaging

- Аудит payload показал, что probe `604691a8...` и v3 `61f962f9...`
  добавляли после `Image.gz` весь Android DT-table с magic `d7b7ab1e`.
- В таблице одна запись: raw FDT начинается на offset 64, размер 121167,
  magic `d00dfeed`, SHA-256
  `6ec788d5d327067350b7ae00df627000142c41c8c8735d93cfbc856af570a9a0`.
- Старый LK-алгоритм сканирования мог найти вложенный FDT, но после gzip
  оставлял 64 байта table header. Поэтому прошлый `lk_crash` не является
  валидным опровержением корректного `Image.gz` + raw FDT.
- Добавлен валидирующий extractor
  `native-linux-build/tools/extract-bf7-dtb-entry0.sh`; оба build script теперь
  добавляют только raw FDT, а preflight проверяет границу `d00dfeed`, размеры,
  prefix/suffix и отдельный `gzip -t` ядра.
- Исправленный OEM probe дважды совпал побайтно: SHA-256
  `0057d996861e3d47add4ceec683cf062b4500dde8eb3be9325437395f5edb008`.
- Исправленный v3 legacy-контейнер дважды совпал побайтно: SHA-256
  `6eb7049c09f02f99bbd259fa9f2d71866710626df56d8c4b9b5bbefddd0d1a42`.
- Телефон не трогался. Маркер успеха отсутствует; v3 по-прежнему закрыт до
  отдельного `fastboot boot` исправленного OEM probe.

## 2026-08-23 — corrected probe и header-match probe: LK crash

- Исправленный legacy v0 probe с raw FDT, SHA `0057d996861e3d47add4ceec683cf062b4500dde8eb3be9325437395f5edb008`,
  временно передан через `fastboot boot`; запись разделов не выполнялась.
- Download 12,998 KiB завершился `OKAY`, затем LK оборвал transport. USB
  `1d6b:0112` и probe shell не появились; через Preloader вернулся
  установленный `1d6b:0111` OEM-native Linux.
- Read-only `/proc/aed/reboot-reason`: `WDT status: 6 fiq step: 71 exception
  type: 6`. Reference LK enum определяет 6 как `AEE_EXP_TYPE_LK_CRASH`.
- Для проверки гипотезы о cached/stale header построен probe
  `bf7-oem-minimal-ramboot-probe-v2-header-match.img`, SHA
  `a1cdfde9cef238b3a3319f2e172b8dd15ab27ef13eb8552bc0f057e63439c160`.
  Его первая 2048-байтовая header page побайтно равна рабочему boot backup;
  kernel/DT-table/ramdisk sizes равны OEM, минимальный gzip ramdisk дополнен
  нулями до OEM размера и проходит `gzip -t`.
- Header-match download 16,972 KiB также завершился `OKAY`, затем дал ту же
  потерю transport и свежий `LK_CRASH`. После возврата подтверждены exact OEM
  kernel `4.19.191-g216400aba14e-dirty`, systemd PID 1, root
  `/dev/mmcblk0p48`, slot B и USB `1d6b:0111`.
- Вывод: ни wrapper DT-table, ни header-page/size mismatch не являются
  достаточной причиной. Vendor LK падает в downloaded-image `cmd_boot` до
  передачи управления ядру. ALPS candidate не выполнялся; v3 gate остаётся
  закрыт.
- Host transition monitor переработан: новый
  `native-linux-build/storage-diagnostic-v1/enter-fastboot-host.sh` пассивно
  фиксирует sysfs VID:PID `0e8d:201c`, затем использует только один fastboot
  client. Probe runner теперь немедленно распознаёт возврат known-good
  `1d6b:0111`, вместо немого ожидания полного deadline.
- Новый transition monitor проверен на железе. Он зафиксировал цепочку
  `known-good 0111 -> disconnect -> Preloader 2000 -> disconnect -> fastboot
  201c`, затем подтвердил `is-userspace=no` и slot B. После обычного
  `fastboot reboot` установленный kernel вернулся по SSH за 22 секунды:
  `4.19.191-g216400aba14e-dirty`, root `/dev/mmcblk0p48` ext4, USB `0111`.

## 2026-08-23 — вероятная причина LK `cmd_boot`: пропущенный DTB prepare

- Reference MediaTek normal slot path вызывает `load_vfy_boot()` ->
  `load_bootinfo_bootimg()` -> `prepare_kernel_dtb()` -> `boot_linux()`.
- Reference fastboot `cmd_boot()` вызывает `load_bootinfo_bootimg()`, но не
  `prepare_kernel_dtb()`, затем вручную переносит ramdisk и входит в
  `boot_linux()`.
- BF7 normal LK log подтверждает `load_lk_dtb`, загрузку `dtbo_b`, index 0 и
  успешный `dtb_overlay`. В LK binary присутствуют соответствующие строки и
  `prepare_kernel_dtb`.
- `boot` разрешена при secure boot только unlocked-устройству; в `cmd_boot`
  нет AVB verification. Normal BF7 boot сам сообщает `img_auth_required=0` и
  orange state. Поэтому signature rejection не объясняет LK exception 6.
- Доступные read-only `/proc/aed` и pstore не содержат crash PC/stack. Точная
  падающая инструкция пока неизвестна; missing DTB prepare — сильная, но ещё
  не окончательно доказанная гипотеза.
- Read-only сняты свойства уже merged runtime FDT:
  `/sys/firmware/fdt`, 229877 bytes, SHA-256
  `bd867f48c093ce8ede11679cc1b0e634c3e48888d049a533576dc679966b2f0c`,
  magic `d00dfeed`, model `MT6761V/WBB`, dtbo index 0.
- Следующий наиболее безопасный probe: exact OEM kernel + minimal ramdisk +
  runtime-merged FDT в legacy `Image.gz-dtb`, только через `fastboot boot`.
  ALPS candidate остаётся закрыт до успеха OEM probe.

## 2026-08-23 — материалы платы и аппаратный UART

- Для точного TECNO BF7 найден FCC ID `2ADYY-BF7` и два публичных 12-страничных
  PDF с обеими сторонами основной платы, включая фото без экранов. На снимках
  видны test-point, но маркировка UART не читается; назначать контакты только
  по этим фото небезопасно.
- HalabTech индексирует точный `TECNO BF7 Service Manual.pdf` (2.50 MB,
  file id 1033972) с тегами schematics/hardware/JTAG. Содержимое пока не
  получено: direct route отвечает Cloudflare challenge и может требовать
  аккаунт или подписку.
- FCC metadata содержит 1.6 MB schematic exhibit, но официальный запрос TECNO
  требует постоянной конфиденциальности схем, block diagram и BOM; поэтому
  публично доступна плата на фото, а не схема.
- Независимый BF7 recovery-log на Hovatek подтверждает MMIO base UART
  `0x11002000`; это не определяет физические test-point.
- DZKJ содержит запись PCB layout для `tecno pop 7`, но соответствие точной
  ревизии BF7 не доказано.
- LK log подтверждает `console=ttyS0,921600n1`; аппаратный пассивный захват
  может дать недостающий LK crash trace. Одновременно cmdline содержит
  `disable_uart=0` и затем `disable_uart=1`, поэтому kernel printk может быть
  выключен последним значением.
- Физические TX/RX и уровень не определены. До измерения считать интерфейс
  потенциально 1.8 V; первый тест допустим только RX-only высокоомным входом,
  без TX/VCC со стороны адаптера.
- Полная карта источников и безопасная процедура записаны в
  `uart-hardware-access.md`.

## 2026-09-11 — первый аппаратный запуск ALPS-кандидата (A/B-слот), дисплей работает

Подробности: [slot-test-2026-09-11.md](slot-test-2026-09-11.md).

- `fastboot boot` закрыт окончательно: в `expdb` найдены LK-логи обеих проб
  23.08 — `boot image is NULL` → `panic (caller 0x48029a8b): boot image
  decapsulate fail!`; адрес совпал с дизассемблированием `lk_b` (`cmd_boot` →
  `load_bootinfo_bootimg` игнорирует скачанный буфер, work-buffer «boot» в
  fastboot-режиме не аллоцирован). Не зависит от образа.
- Выбран путь A/B: бэкап `boot_a`/`boot_b`/`para`
  (`firmware-backup/live-native/pre-alps191-20260911-1704/`), кандидат →
  `boot_a`, `fastboot set_active a` (сбрасывает successful/tries; `getvar`
  показывает кэш до `reboot bootloader`), known-good остаётся в `boot_b`.
- Итерация 1 (образ 372870410…): загрузился, RNDIS есть, но `plcm=NULL`;
  при `reboot()` Oops в `dpmgr_wait_event_timeout` ← `mtkfb_shutdown`;
  auto-fallback на B сработал после исчерпания tries.
- Причина: у Transsion в `struct tag_video_lfb` лишнее `u32` после
  `islcmfound` (OEM читает `lcmname` с +24, ALPS — с +20 → пустое имя).
  Фикс + guard в `primary_display_suspend`: коммит `b6fdf84f159f`
  (`mediatek-mt6761-alps188-gpu115`). Ядро `f9366fb5…`, образ v3 `5b56af0d…`.
- Итерация 2: `/proc/mtkfb` → `LCM Driver=[td4160_hdp_dsi_vdo_hx_inx_bf7]`,
  `LCM Connected:Y`, `State=Alive`; `blank 4/0` → экран погас и вернулся
  (recovered init/power path исполнен на железе); `fbpan` + `FBIOPAN_DISPLAY`
  → RGB-полосы; возврат `bf7-enter-fastboot` → LK fastboot работает.
- GPU: `rgx.fw`/`rgx.sh` (из `vendor_a.img`, хэши совпали) грузятся, VGPU/клоки
  включаются, но `'FW started'` timeout → bad state, без паники. Открыто.
- Новые каналы диагностики: `CONFIG_U_SERIAL_CONSOLE=y` + `console=ttyGS0`,
  ACM `getty` и `busybox httpd :8080` в `alps191-trial-v3/init`; на хосте
  `cdc_acm` заблокирован `99-mtkclient.conf` → `modprobe --ignore-install`.
  busybox telnetd + lineedit (`ESC[6n`) ненадёжен для скриптов.
- Состояние на конец дня: телефон в known-good B (`para`: B 15/successful,
  A 14/tries 3/successful 0), кандидат лежит в `boot_a` неактивным.
  Инструменты: `native-linux-build/alps191-slot-test/`.
- GPU-контроль на OEM-ядре (тот же день, slot B, без перезагрузки): те же
  `rgx.fw`/`rgx.sh` в `/vendor/firmware`; `open(/dev/dri/renderD128)` и
  `open(card0)` возвращают 0, `PVR_K:(Error)` в логе нет, после init GPU
  штатно ушёл в idle (`g_volt_enable_state=0`). Чтение с ноды блокируется в
  `drm_read` (ждёт DRM-события) — это нормально. Вывод: **на OEM-ядре FW
  стартует, значит `'FW started'`-timeout у кандидата — проблема порта**
  (`mtk_mfgsys.c`/mfg power-on/память FW), а не платформы. Следующий шаг —
  сравнить `[GPU/FREQ]`/`PVR_K` последовательности двух ядер и diff
  `drivers/gpu/mediatek/gpu_rgx/.../mt6761/` против OEM-поведения.

## 2026-09-12 — GPU: разведка без телефона

- Веб: публичного 4.19-дерева MT6761 с DDK `m1.15ED6070602` нет; у нас эта
  версия уже есть (ALPS05777678). Для сравнения glue: ZTE Z6201V MT6761
  (m1.10ED5150755) `services/system/rgx_mtk/mt6761/mtk_mfgsys.c`;
  xiaomi/oppo/realme mt6765 (lineage-17.1, 4.9/4.14).
- Локально: набор `MTK*`/`RGX*` символов в OEM Image и кандидате идентичен
  (только `MTKInitHWAPM` у нас не заинлайнен); OEM Image содержит строку
  `m1.15ED6070602`; вариант заголовка `config_kernel_user_mt6761.h`
  (`MTK_ENG_BUILD=n`) совпадает с user-сборкой OEM. Т.е. драйвер тот же —
  расхождение в runtime (питание/клоки MFG, память FW, SSPM/DEVAPC) или в
  OEM-специфичных Kconfig (`oem-symbols-missing-from-alps191.txt`).
- В сохранённом dmesg кандидата вокруг попытки (625–636 с) нет DEVAPC/EMI-MPU
  нарушений и SSPM-ошибок; `PVRSRVDebugRequest` полезного дампа не выдал.
  `gpufreq-supply` в DT → `mediatek,mt6761-gpufreq` (PMIC VGPU), внешний
  `ext_buck_vgpu@55` без драйвера и у OEM, и у нас.
- План на следующую сессию (нужен телефон): (1) на OEM-ядре снять kmsg через
  `cat /dev/kmsg &` до `open(renderD128)` и сравнить `[GPU/FREQ]`/`PVR_K`/
  `ged` последовательность с кандидатом; (2) на кандидате включить FW-трейс
  (`MTK_DDK_DEBUG := y` → `config_kernel_debug`, apphint `FirmwareLogType`/
  `EnableLogGroup`) и/или `RGXDumpFirmwareTrace`; (3) проверить
  `/proc/gpufreq/gpufreq_opp_dump` и mtcmos `mfg_async/mfg/mfg_core0` через
  `/sys/kernel/debug/clk/clk_summary` (debugfs есть у кандидата) в момент
  power-on; (4) сверить `mtk_mfgsys.c` c ZTE m1.10 по порядку включения
  (buck → mtcmos → CG → reset).


## 2026-09-14 — RNDIS: найдена и исправлена порча исходящих кадров

Главный результат дня: **на кандидате заработал полноценный SSH по USB**.

Симптом выглядел как «передача телефон→хост застревает на полноразмерных
пакетах» (500/512/1024 проходят, 1460/2048 — ноль). Первая версия диагноза
(«очередь TX не дренится») оказалась неверной; разобрано в
[usb-rndis-tx-corruption.md](usb-rndis-tx-corruption.md).

Что показали измерения на живом устройстве:

- Телефон **передавал**: `rndis0` tx_packets +14, tx_bytes +13020,
  tx_dropped/tx_errors = 0. Хост **принимал**: rx_packets +14, все счётчики
  ошибок нулевые. Но `/proc/net/snmp` на хосте: `Tcp: InErrs +7,
  InCsumErrors +7` — сегменты отбрасывались по контрольной сумме.
- Захват сырым сокетом: в 1448-байтовом нулевом payload ровно 4 байта равны
  `0x01`, на фиксированных смещениях 1410/1414/1432/1440; расхождение суммы
  ровно `+0x0400`. В одном кадре вместо нулей — ASCII `zlib@openssh`.
- `0x01` на этих местах = поля `nr_frags`/`dataref` структуры
  `skb_shared_info`, лежащей сразу за линейной частью skb. `skb_headlen()`
  был 1520 = 44 (RNDIS) + 14 + 20 + 32 + 1410 — первый испорченный байт
  начинается ровно за концом линейного буфера.

Причина: коммит MTK `3f2cec825bfe [ALPS05269737] USB: Enhance RNDIS
Performance` включил `NETIF_F_SG | NETIF_F_GSO` на gadget-интерфейсе, но
разбор страничных фрагментов добавил только в ветку агрегации
`multi_pkt_xfer`. Агрегация включается лишь при `MaxTransferSize > 2048`, а
Linux-хост просит ровно 2048 (`rndis_command_complete: MaxTransferSize: 2048
: Multi_pkt_txr: disabled` в dmesg телефона). Неагрегированная ветка делает
`req->buf = skb->data` длиной `skb->len` — и отправляет соседнюю память
вместо фрагментов. Дефект родной для ALPS, наших правок в файле не было.

Исправление — коммит `c9061330b064`: в `eth_start_xmit()` выпрямляем skb
(`skb_linearize`), если пойдёт не та ветка, которая умеет фрагменты.

Подтверждение до пересборки ядра, в рантайме (сняты `sg/gso/gro` утилитой
`tools/bf7feat`): 1460 байт — 0 → 1460 принятых, битых байт 0, ретрансмитов
нет; объёмная передача 2 МиБ за 0.06 с с совпадающим sha256; `ssh
root@192.168.7.2 'uname -a'` отвечает.

Задним числом этот же дефект объясняет «залипание» вывода telnet (мелкие
записи попадали в линейную часть и доходили, NOP-«поко» просто дробил
поток), одинаковое зависание OpenSSH и dropbear на `SSH2_MSG_KEX_ECDH_REPLY`
и мою ошибочную гипотезу «CPU не просыпаются».

Новые инструменты в дереве:
- `native-linux-build/alps191-slot-test/bf7sh.py` — выполнение команд на
  телефоне через ACM-консоль (ответ на `ESC[6n`, маркер завершения, длинные
  команды через base64-файл, код возврата).
- `native-linux-build/alps191-slot-test/rxcheck.py` — приём с проверкой
  контрольных сумм и показом испорченных байт.
- `native-linux-build/alps191-slot-test/tools/bf7feat.c` — статический
  aarch64-бинарник: `sg/gso/tso/gro` через `SIOCETHTOOL` без ethtool/libmnl.

Дальше: пересобрать ядро с фиксом, перепрошить слот A, проверить, что SSH
работает без рантайм-обхода; затем сравнение с OEM (ожидаемо тот же дефект
на заводском ядре при RNDIS на Linux-хост — Transsion его не видит, потому
что Windows просит `MaxTransferSize` > 2048).

### Итог 2026-09-14 (вечер): фикс собран и прошит

Ядро пересобрано с коммитом `c9061330b064` (`Image` `232049d5…`), образ
`bf7-alps191-kernelonly.img` `b70c1ae3…` записан в `boot_a`, `set_active a`.
Слот B не тронут (`successful=yes`), слот A остаётся `successful_boot=0`.

На прошитом ядре, с включёнными `sg/gso` и выключенной агрегацией:
1460 B — 1460 принято, битых сумм 0; `nc` 8 МиБ — 0.25 с / 31.7 МБ/с;
`ssh root@192.168.7.2 'cat /run/blob'` 8 МиБ — 0.38 с / 21.3 МБ/с, sha256
совпал. Рантайм-обход (`bf7feat rndis0 off`) больше не нужен.

В overlay добавлены `libutmps.so.0.1` + `libskarnet.so.2.15` (dropbear теперь
поднимается сам из init) и `usr/sbin/bf7feat`.

Грабля хоста: NetworkManager держит `enp22s0f0u9` как «connected (externally)»
и может снять с него адрес посреди передачи — первая попытка скачать 8 МиБ по
SSH вернула 0 байт ровно поэтому, а не из-за ядра. Перед длинными тестами
проверять `ip -br addr show enp22s0f0u9`.

Следующее: сравнение с OEM (ожидаемо тот же дефект на заводском ядре при
RNDIS на Linux-хост), затем GPU `'FW started'`.

## 2026-09-14 (поздний вечер) — Debian на кандидате: загрузился, SSH работает

Прошит `bf7-alps191-rootfs-v1.img` `bc986652…` в `boot_a`, `set_active a`
(5 попыток, слот B `successful=yes` не тронут). Результат:

- `systemctl is-system-running` → **running**, ни одного failed-юнита;
- `ssh root@192.168.7.2` работает штатно, без обходов;
- корень: `EXT4-fs (mmcblk0p48): mounted filesystem without journal. Opts:
  noload`, затем на 5.4 с `re-mounted` в rw — это systemd-remount-fs по
  `/etc/fstab` (`/ ext4 defaults 0 1`). Замысел «ro до проверки» перекрывается
  fstab; если нужен ro, править надо fstab или добавлять
  `systemd.volatile`/маску `systemd-remount-fs.service`.

Была ровно одна перезагрузка на первой загрузке после прошивки; в журнале
`-1` всего 27 строк, обрывается на `systemd-update-utmp`, ошибок нет, pstore
пуст. Причина не установлена; вторая загрузка и все последующие стабильны.

### Найден и исправлен наш собственный дефект в initramfs

На загруженной системе было `load average 15.8`, 20 000 прерываний/с и
52 000 переключений контекста/с. Источник — PID 285:

```
/bin/busybox-static sh -c 'while :; do /bin/busybox-static telnetd ...; sleep 1; done'
/proc/285/exe -> /bin/busybox-static (deleted)
```

После `switch_root` initramfs удалён: сам процесс живёт (бинарник отображён в
память), но путь `/bin/busybox-static` больше не резолвится, поэтому и
`telnetd`, и `sleep` не запускаются и цикл крутится вхолостую — 43% ядра.
Всплеск `IPI0 Rescheduling` был следствием, а не отдельной проблемой.

После `kill -9 285`: 138 прерываний/с, ~200 переключений/с, простой 100%.

Исправлено в `alps191-rootfs-v1/init`: цикл перезапуска оставлен только для
ветки `bf7.noroot=1` (там initramfs остаётся смонтированным), а в ветке с
`switch_root` telnetd запускается однократно. Пересобранные образы:
rootfs `2b1ef693…`, kernel-only `2f685c14…`.

Вывод на будущее: «непонятная нагрузка» на кандидате дважды оказывалась не
ядром — сначала двойная регистрация нотифаера, теперь наш же цикл в initramfs.

## 2026-09-14 (ночь) — Wi-Fi: модули собраны под кандидата, упёрлись в EMI MPU

Wi-Fi на BF7 — четыре внеядерных модуля в `/opt/bf7/wifi/`, собранные под
OEM-ядро (`vermagic=4.19.191-g216400aba14e-dirty`), плюс андроидная обвязка
`/vendor/bin/wmt_loader` и `wmt_launcher` под шимом `fake-android-props.so`.

Проверка выполнимости: из 429 символов, которые OEM-модули требуют от ядра,
наша сборка экспортирует 428. Единственный отсутствующий
(`connectivity_export_clk_buf_show_status_info`) в донорских исходниках стоит
под `#if defined(KERNEL_clk_buf_show_status_info)` и не нужен.

### Сборка

Скрипт: `kernel-work/scripts/build-bf7-connectivity.sh`. Исходники —
`donors/motorola-mt6761-connectivity-sta32` (`common` → `wmt_drv`,
`adaptor` → `wmt_chrdev_wifi`, `gen4m/gen4m` → `wlan_drv_gen4m`).
`connfem` в доноре нет и не нужен: ни один из трёх модулей не импортирует
его символов.

Что пришлось выяснить опытным путём (донорские Makefile этого не выводят):

| переменная | значение | зачем |
|---|---|---|
| `MTK_COMBO_CHIP` | `CONNAC` | иначе умолчание `MT6632`; MT6761 использует backend `chips/connac`, который строит имена `WIFI_RAM_CODE_soc1_0_1_1.bin` |
| `WLAN_CHIP_ID` | `6761` | из него выводится `CONFIG_MTK_PLATFORM=mt6761` |
| `CONFIG_MTK_COMBO_WIFI_HIF` | `axi` | без него `$(error Unsuppoted HIF=)` |
| `MTK_ANDROID_WMT` | `y` | `CFG_MTK_ANDROID_WMT=1`, стек идёт через wmt |
| `MTK_ANDROID_EMI` | `y` | иначе `gConEmiPhyBaseFinal` не объявлен, а код его использует |
| `MTK_WLAN_SERVICE_PATH` | `wlan_service/` | иначе `LIST_SEG_MAX` не объявлен при `CFG_SUPPORT_QA_TOOL=1` |
| `MODULE_NAME` | как у стоковых | чтобы `bf7-wifi-init` нашёл модуль в `/proc/modules` |
| `TOP` | дерево-симлинк | донорские Makefile адресуют соседей как `$(TOP)/vendor/mediatek/kernel_modules/connectivity/...` |

Результат: `vermagic=4.19.191-bf7-native-alps191-v1` у всех трёх,
**407 символов требуется от ядра, неразрешённых 0**.

### Установка (дисциплина общего корня)

Слоты делят один ext4 на p48, и `/vendor`, `/system` — каталоги внутри него,
не отдельные разделы. Поэтому модули положены в
`/lib/modules/4.19.191-bf7-native-alps191-v1/extra/`, `/opt/bf7/wifi` **не
тронут**, `bf7-wifi-init` **не изменён** — слот B побайтово в прежнем
состоянии. Полная копия стокового стека снята на хост:
`firmware-backup/wifi-stack-oem-20260914/wifi-stack.tar`, sha256 `34c0bc92…`.

### Что заработало

```
wmt_loader:  chipid(26465) -> set current consys chipid (0x6761)
             WMT_init: driver(major 190) installed, mtk_wmtd стартовал
wmt_chrdev_wifi: mtk_wmt_wifi_chrdev driver (major 503) installed
wlan_drv_gen4m:  axiCsrIoremap 0x18000000, wifi-reserve-memory 0x300000
                 @0xbe000000, mtk_axi_probe() done ret:0, initWlan::End
power-on:    wlanSetChipEcoInfo: Chip ID[0001] Version[E0] HW[0x31317075]
```

То есть модули грузятся, чип детектируется, отвечает и отдаёт свой ECO.

### Где встали

```
[MPU] violation master is MT6761_M7_AXI_MST_CONNSYS_WIFI_PDMA, from domain 0x2
[STP] stp_trace32_dump: <ASSERT> ../patch/sys_patch_mcu.c #2780
```

DMA-мастер Wi-Fi обращается к EMI и получает отказ от EMI MPU, после чего
прошивка чипа падает в ассерт; `printf 1 > /dev/wmtWifi` возвращает EIO.

Важно: `kalSetEmiMpuProtection` в gen4m — это `__weak`-заглушка, и платформы
`plat/mt6761/` в доноре нет. Но **в OEM-модуле те же строки-заглушки**
(`(SW4 WARN) EMI MPU function is not defined`), значит на заводском ядре
регион открывает не драйвер. Следующий шаг — найти, кто именно: таблицы
`drivers/misc/mediatek/emi/mt6761/mpu_platform.h`, DT-описание
`wifi-reserve-memory`, либо connsys-глю в `wmt_drv`
(`mtk_wcn_consys_hw_*`). Сравнить с OEM-ядром по этим точкам.

Состояние стенда после опыта: чип выключен, `wmt_drv` завис в состоянии
`Unloading` (поток `mtk_wmtd_worker` жив после ассерта) — уйдёт с
перезагрузкой. Система в остальном простаивает на 99%, SSH работает.

### Wi-Fi заработал: не хватало платформенного файла gen4m для mt6761

Ассерт прошивки был следствием того, что зарезервированная область
`wifi-reserve-memory` никем не открывалась в EMI MPU. Разбор:

- у DT-узла `wifi-reserve-memory` (`shared-dma-pool`, `no-map`, 0x300000)
  никаких MPU-свойств нет — ядро его не открывает;
- `consys_emi_mpu_set_region_protection()` в `wmt_drv`
  (`platform/mt6761.c`, `REGION_CONN 25`) защищает **другую** область —
  общую память connsys, а нарушение пришло от `CONNSYS_WIFI_PDMA`;
- открывать область wlan должен `kalSetDrvEmiMpuProtection()` из
  `gen4m/os/linux/plat/mt<chipid>/plat_priv.c`, который Makefile подключает
  по `PLAT_DIR := os/$(os)/plat/mt$(WLAN_CHIP_ID)/`. В донорском дереве
  каталога `mt6761` нет — оставалась `__weak`-заглушка из `gl_kal.c`;
- что у заводского модуля эта реализация есть, видно по строке
  `"emiPhyBase: 0x%x, offset: %u, size: %u"` в `/opt/bf7/wifi/
  wlan_drv_gen4m.ko`: она принадлежит сильной версии, а не заглушке.
  В нашей сборке этой строки не было — прямое подтверждение.

Написан `kernel-work/bf7-connectivity-overlay/gen4m/os/linux/plat/mt6761/
plat_priv.c` по образцу `plat/mt6765/plat_priv.c` (MT6765 — прямой брат
MT6761: та же ConnSys 1.0, тот же EMI MPU v1, 16 доменов, 32 региона).
Регион 29, домены `d2` и `d0` = `NO_PROTECTION` — `d2` как раз тот, из
которого приходило нарушение. `kalBoostCpu` намеренно не переопределяли,
чтобы не тянуть зависимости `cpu_ctrl`/`topo_ctrl`/`pm_qos`.

Оверлей накладывается на скопированные исходники в
`build-bf7-connectivity.sh`; каталог `donors/` остаётся нетронутой копией
чужого дерева.

Результат на устройстве:

```
wlanSetChipEcoInfo: Chip ID[0001] Version[E1] HW[0x00008a00] SW[0x00008a00]
kalFirmwareOpen: Request FW image: WIFI_RAM_CODE_soc1_0_1_1.bin done
wlanDownloadEMISection: EmiPhyBase:0xbf000000 offset:0x177000
wlanCheckWifiFunc: Ready bit asserted
интерфейсы: p2p0, wlan0, wlan1
iw dev wlan0 scan -> реальные сети (NEIGHBOR-AP -73 dBm, HOME-AP -72 dBm, ...)
```

Ни нарушения MPU, ни ассерта. Осталось два хвоста:

1. `wlanOnPostFirmwareReady: load manufacture data fail` — не найдены
   калибровочные данные, MAC получился дефолтный `00:08:22:40:2b:00`
   вместо заводского. Надо найти, откуда OEM берёт NVRAM.
2. Модули грузятся вручную. `bf7-wifi-init` пока **не изменён**, слот B
   по-прежнему побайтово прежний. Чтобы включить автозапуск, в него нужно
   добавить ветку по `uname -r` (каталог модулей) и пропуск `connfem`.

### Подключение к сети: работает полностью

Креды нашлись на месте — `/etc/wpa_supplicant/wlan0.conf` (сеть `HOME-AP`,
`key_mgmt=WPA-PSK WPA-PSK-SHA256 SAE`), юнит `bf7-wpa.service` запускает
`wpa_supplicant -D nl80211 -i wlan0`. Запускали вручную, юниты остались
замаскированы, `bf7-wifi-init` не изменён.

```
wpa_state=COMPLETED, bssid=52:ff:20:xx:xx:xx, freq=5260 (5 ГГц)
udhcpc: lease of 192.168.27.89 obtained from 192.168.27.1
ping 192.168.27.1   -> 5.3-11.9 мс
ping 1.1.1.1        -> 73-78 мс
DNS + HTTP наружу   -> работают
wget 11 940 185 Б за 3.99 с = 23.96 Мбит/с
link: signal -81 dBm, rx 28.9 / tx 26.0 Мбит/с
```

Скорость соответствует уровню сигнала (-81 dBm на 5 ГГц). Проверить, не
занижена ли мощность передатчика из-за отсутствующей калибровки, стоит
после решения вопроса с NVRAM — MAC до сих пор дефолтный
`00:08:22:40:2b:00`.

Мелкое наблюдение: внешний ping возвращает дубликаты ответов
(`+3 duplicates`). На работу не влияет, но записано на будущее.

## 2026-09-14 (ночь) — GPU: диагноз уточнён до «MIPS не стартует вовсе»

Раньше формулировка была «таймаут флага 'FW started'». Теперь известно точнее.

Инструментовка из коммита `9f04dc3eb11d` (`TRACK_FW_BOOT`) сработала:

```
PVR_K: RGX Firmware image 'rgx.fw' loaded
PVR_K: Shader binary image 'rgx.sh' loaded
PVR_K:(Error): PollForValueKM: Timeout. Expected 0x1 but found 0x0
PVR_K:(Error): RGXPostPowerState: Polling for 'FW started' flag failed. [1008]
PVR_K: RGXCheckFWBootStage: FW reached boot stage 0/6.
```

Стек: `pvr_drm_open → PVRSRVDeviceOpen → PVRSRVCommonDeviceInitialise →
PVRSRVDeviceFinalise → PVRSRVSetDevicePowerStateKM → RGXPostPowerState`.

**Стадия 0 из 6** для MIPS-прошивки читается из её же секции данных
(`RGXMIPSFW_BOOT_STAGE_OFFSET` в `MIPS_BOOT_DATA`). То есть MIPS не выполнил
ни одной инструкции бутлоадера — это не «зависла», а «не запустилась».

### Что проверено и исключено

- Драйвер загружен и зарегистрирован: `Read BVNC 22.68.54.30 from HW device
  registers`, `[drm] Initialized pvr 1.15.6070602 ... for 13000000.mfg_clark`,
  `/proc/pvr/` со всеми узлами. Регистры GPU читаются.
- Платформенная глю на месте и собрана: `[GPU_DEBUG] MTK_PLATFORM = mt6761`,
  объект `services/system/rogue/rgx_mtk/mt6761/mtk_mfgsys.o` существует.
  Это не тот случай, что с Wi-Fi.
- Каталог DDK выбирается словом `clark` → `m1.15ED6070602`; третье слово в
  `CONFIG_MTK_GPU_VERSION="rgx clark m1.9ED4971894"` декоративное, не влияет.
- Нарушений EMI MPU по GPU нет (все записи в логе принадлежат Wi-Fi).
- Образ прошивки структурно корректен: `ELF 32-bit LSB executable, MIPS,
  MIPS32 rel2`, точка входа `0xbfc00001` (reset-вектор MIPS, бит 0 = microMIPS),
  сегменты `0x9fc02000`, `0xbfc00000`, `0xc0000000`.
- Питание/клоки поднимаются: `mtcmos_mfg_async`, `mtcmos_mfg`,
  `mtcmos_mfg_core0` найдены, DVFS сообщает 460 МГц.

### Особенность методики

После первой неудачи драйвер защёлкивается: повторное открытие даёт
`PVRSRVDeviceOpen: Driver already in bad state`. **Один опыт на загрузку** —
планировать диагностику надо заранее.

`/proc/pvr/debug_dump` после сбоя: `RGX Device State: Bad`,
`RGX Power State: OFF`, `FW info: UNINITIALIZED`, `Server Errors: 6`.

### Следующий шаг

Нужна видимость на уровне регистров обёртки MIPS в момент старта:
`RGX_CR_MIPS_WRAPPER_CONFIG`, remap-регистры, `RGX_CR_MIPS_EXCEPTION_STATUS`
и физические адреса, которые драйвер туда записал. Кандидаты на причину:
ремап `0xbfc00000` указывает не на память с бутлоадером; MIPS не снят с
ресета; образ не сброшен из кэша до старта. Это требует инструментовки ядра
(пересборка + одна перепрошивка) либо снятия тех же значений на заводском
ядре для сравнения — вот здесь сравнение с OEM действительно по делу.

### Сравнение GPU с заводским ядром: что оно дало и три снятых версии

Слот B — это заводское ядро `4.19.191-g216400aba14e-dirty`, и оно грузит тот же
Debian на p48 (подтверждено записями utmp и распакованным `boot_b.img`).
Android-userspace для сравнения не нужен: весь запуск прошивки GPU происходит
в ядре на первом `open("/dev/dri/renderD128")`.

Снято по шагам, всё в `native-linux-build/alps191-slot-test/gpu-compare-20260914/`.

**Эталон (OEM).** Открытие renderD128 — **0.01 с**, в логе всего две новые
строки (`rgx.fw loaded`, `rgx.sh loaded`), никакого опроса флага.

```
                     OEM              кандидат
Device State         Active           Bad
FW info              1.15 @ 6070602   UNINITIALIZED
FW OS 0              active           offline
Server Errors        0                7
```

Проб драйвера при этом **идентичен**: те же `irq_res = 0x5e`,
`reg_res = 0x13000000, 0x80000`, тот же `BVNC 22.68.54.30`, та же подмена
банка регистров на `0x7f000000`, та же версия DDK `1.15.6070602`.

**Три версии, выдвинутые и снятые по ходу разбора** (записаны, чтобы никто,
включая меня, не поднимал их заново):

1. *«Не та версия DDK в `CONFIG_MTK_GPU_VERSION`»* — нет. Каталог выбирается
   вторым словом `clark`, третье слово декоративное.
2. *«GED не завершает инициализацию»* — нет. Строк `ged driver init done`,
   `ged_pdrv_probe`, `ged_kpi init done` **в нашем дереве вообще не
   существует**, у OEM другая версия исходника. В журнале кандидата нет ни
   одной ошибки GED.
3. *«Тактовая частота GPU нулевая»* — нет. `clock is 0 KHz` в
   `__mt_gpufreq_pdrv_probe` означает лишь, что на момент probe домен MFG ещё
   не включён. Тот же частотомер позже показывает `clock freq = 659952`.

**Что установлено положительно.** `/proc/gpufreq/gpufreq_var_dump` на
кандидате после неудачи: `real volt = 80000`, `clock freq = 659952`,
`g_volt_enable_state = 1`. То есть питание 0.8 В подано и клок 660 МГц
крутится — путь включения MFG исправен. (На OEM после успешной инициализации
там нули: GPU штатно погашен по APM.) Значит MIPS имеет и такт, и
напряжение — и всё равно стоит на стадии 0/6.

Остаются две причины: MIPS не снят с ресета, либо не видит память с
бутлоадером (ремап `0xbfc00000` или несброшенный кэш). Различить их можно
только инструментовкой: печатать `RGX_CR_MIPS_WRAPPER_CONFIG`, remap-регистры
и физические адреса, которые драйвер туда записал, непосредственно перед
снятием ресета. Сравнение с OEM для этого шага уже не нужно — достаточно
проверить наши значения на самосогласованность.

**Особенности стенда, выясненные попутно:**
- debugfs в Debian на кандидате не смонтирован (`/sys/kernel/debug` нет), так
  что `clk_summary` недоступен; наблюдать за клоками надо через
  `/proc/gpufreq/gpufreq_var_dump`.
- у слота B гаджет **без ACM**, только RNDIS. Читать `/dev/ttyACM0` во время
  работы на B бессмысленно: отдаётся протухший буфер прошлой сессии
  кандидата. Один раз я на этом ошибся и принял старый хвост шатдауна за
  текущую перезагрузку.
- NetworkManager снимал адрес с USB-интерфейса при каждой переenumerации, и
  телефон выглядел зависшим. Закрыто правилом
  `/etc/udev/rules.d/99-bf7-rndis.rules` на хосте: интерфейс помечается
  неуправляемым и адрес выставляется автоматически.

### Инструментовка обёртки MIPS: ресет снят, ядро спит

В `RGXCheckFWBootStage()` (`services/server/devices/rogue/rgxpower.c`)
добавлено чтение регистров обёртки MIPS прямо из железа в момент отказа —
это точнее, чем печатать то, что драйвер намеревался записать. Сборка
`Image` `60c8a79b…`, образ `bf7-alps191-rootfs-v1.img` `5c466288…`,
прошит в `boot_a`, попытки слота A сброшены до 4.

Снято на живом устройстве:

```
MIPS wrapper: CONFIG=0x0000000000017f00 EXCEPTION_STATUS=0x00000020
              DEBUG_CONFIG=0x00000000   SOFT_RESET=0x0000000000000000
MIPS remap boot: C1=0x000000001fc00001 C2=0x00000000f044500c
           data: C1=0x000000001fc01001 C2=0x00000000f484500c
           code: C1=0x000000001fc02001 C2=0x00000000f044400c
     trampoline: C1=0x00000000f77c4001 C2=0x000000001fc0000e
FW reached boot stage 0/6.
```

Что из этого следует:

- **`SOFT_RESET = 0`** — ядро MIPS выведено из ресета. Версия «его держат в
  ресете» снята. Кика для MIPS в DDK и не предусмотрено: после снятия ресета
  оно должно само стартовать с вектора `0xbfc00000` (`RGXStart`, ветка
  «Master boot»; отдельный запуск есть только у RISCV).
- **`WRAPPER_CONFIG = 0x17f00`** разбирается точно: `0x7f000000 >> 16 = 0x7f00`
  (банк регистров, тот самый «alternate Register bank» из лога проба) плюс
  бит `BOOT_ISA_MODE_MICROMIPS` (`0x10000`). Совпадает с точкой входа
  `0xbfc00001` в `rgx.fw` (бит 0 = microMIPS). Настроено верно.
- **Ремапы самосогласованы**: вход `0x1fc00000` (физический эквивалент
  `0xbfc00000`) → выход `0xf0445000`, код `0x1fc02000` → `0xf0444000`,
  данные `0x1fc01000` → `0xf4845000`, младший байт `0x0c`/`0x0e` — log2
  размера сегмента.
- **`EXCEPTION_STATUS = 0x20` = `SI_SLEEP`** (бит 5 по
  `hwdefs/rogue/km/rgx_cr_defs_km.h`). Ядро выведено из ресета,
  сконфигурировано — и находится в состоянии сна.

Итого противоречие: такт есть, питание есть, ресет снят, ремапы разумны — а
ядро спит и не выполнило ни одной инструкции.

**Следующий шаг.** Нужен эталон для `SI_SLEEP`: неизвестно, стоит ли этот бит
и на исправной загрузке. Инструментовать заводское ядро нельзя, но регистры
GPU читаются и снаружи — банк по физическому адресу `0x13000000`
(альтернативный `0x7f000000`), `RGX_CR_MIPS_EXCEPTION_STATUS` = смещение
`0x08D0`. На слоте B после успешного старта прошивки прочитать его через
`/dev/mem` (для регистрового пространства `STRICT_DEVMEM` обычно не мешает) и
сравнить. Если `SI_SLEEP` стоит и там — бит ни при чём, и копать надо
содержимое памяти по адресам ремапа: лежит ли по `0xf0445000` действительно
бутлоадер из `rgx.fw` (сегмент `0xbfc00000`, 0x654 байта, смещение в файле
0xd0c).

## 2026-09-15 (ночь) — GPU: всё, кроме памяти, проверено и верно

Инструментовка в `RGXCheckFWBootStage()` расширена: печатаются адреса,
которые вычислил сам драйвер (`psDevInfo->sLayerParams.*`), и первые слова
секции `MIPS_BOOT_CODE`, прочитанные через собственный маппинг драйвера.
Прямое отображение физического адреса не годится: `OSMapPhysToLin` построен
на `ioremap`, а тот отказывается работать с обычной RAM.

Снято на устройстве:

```
driver computed: boot=0xf1fe6000 code=0xf4a19000 data=0xf102e000 regs=0x7f000000
регистры:        boot=0xf1fe6000 code=0xf4a19000 data=0xf102e000
boot code:       02fc039f 18000000 00fc038c 04ec039c 0047941c 03a40c00
ожидалось (rgx.fw, смещение 0xd0c): 02fc039f 18000000 00fc038c 04ec039c 0047941c 03a40c00
```

Адреса, вычисленные драйвером, **побайтово совпадают** с тем, что реально
лежит в регистрах ремапа, а содержимое буфера — с секцией бутлоадера в
`rgx.fw`. Вместе с прежними замерами это закрывает всё, кроме одного:

- ресет снят (`SOFT_RESET=0`);
- питание 0.8 В и клок 660 МГц подтверждены частотомером;
- обёртка настроена верно (`WRAPPER_CONFIG=0x17f00`: банк `0x7f000000`,
  `BOOT_ISA_MODE=microMIPS`, что соответствует точке входа `0xbfc00001`);
- все четыре ремапа включены, входные адреса совпадают с константами DDK;
- бутлоадер лежит по нужному адресу в правильном виде.

Единственное, чего чтение со стороны CPU показать не может, — дошли ли эти
байты до DRAM. MIPS выбирает инструкции из памяти напрямую, и всё, что
осталось в кэшах ядра, для него невидимо. Поэтому добавлен явный сброс кэша
(`OSCPUCacheFlushRangeKM`) по буферам кода и данных прошивки непосредственно
перед `RGXStart()` — функция `RGXFlushFWMemForBoot()` в `rgxpower.c`.

### Подтверждено внешними источниками

- Определение `RGX_CR_MIPS_EXCEPTION_STATUS` (offset `0x08D0`, `MASKFULL=0x3F`)
  и бита `SI_SLEEP` (bit 5) сверено с mainline-драйвером
  `drivers/gpu/drm/imagination/pvr_rogue_cr_defs.h`. Наше `0x20` — ровно этот
  бит, остальные пять нулевые, то есть чтение осмысленное, а не мусор.
  **Семантики бита нет нигде** в открытых источниках: непонятно, взводится ли
  он сам при включении питания. Проверяется дёшево — прочитать регистр до
  записи в обёртку.
- Разбор полей ремапа по mainline подтверждает нашу расшифровку: `CONFIG1`
  бит0 = enable, `CONFIG2` биты 4:0 = log2 размера региона. Входные адреса
  `0x1FC00000` / `0x1FC01000` / `0x1FC02000` совпадают с зашитыми в DDK.
- Прецедентов с симптомом «boot stage 0/6» в открытых источниках нет.
  Открытой копии DDK 1.15 в вебе найти не удалось, но у TI есть ветка
  `android/k5.10/1.15.6133109` в `git.ti.com/git/graphics/ti-img-rogue-driver.git`
  — её стоит склонировать и продиффить общий Imagination-код.

### Грабля

Кириллица в строках `PVR_LOG` экранируется ядром в dmesg
(`\xd0\xb4\xd1\x80...`), из-за чего поиск по логу её не находит. Все
диагностические строки переведены на латиницу.

## 2026-09-15 (ночь, итог) — Wi-Fi автозапуск сделан; GPU сужен до одной точки

### Сделано и работает

**Wi-Fi поднимается сам при загрузке.** `bf7-wifi-init` теперь выбирает каталог
модулей по `uname -r` (`/lib/modules/<release>/extra`, с откатом на
`/opt/bf7/wifi`), пропускает отсутствующий `connfem`, а из масок в
`alps191-rootfs-v1/init` убраны `bf7-wifi`, `bf7-wpa`, `bf7-static-wifi`
(остались `bf7-dhcp`, `bf7-term`, `bf7-audio-route`). Проверено на живой
загрузке: модули грузятся, `wlan0` появляется, supplicant ассоциируется,
адрес `192.168.27.140` встаёт, шлюз отвечает. Оригинал скрипта сохранён на
устройстве как `bf7-wifi-init.pre-candidate`; ветка отката оставляет слот B
в прежнем поведении.

**Хост больше не теряет адрес.** `/etc/udev/rules.d/99-bf7-rndis.rules`
помечает USB-интерфейс неуправляемым для NetworkManager и выставляет
`192.168.7.1/24` автоматически.

### GPU: что проверено и отброшено за ночь

Инструментовка в `rgxpower.c` (трасса состояния MIPS до старта и через
0/1/10/100 мс) и включённый вывод `RGXCommentLog` дали полную картину.

Отброшено, каждое — опытом на устройстве или сверкой с эталоном:

1. Неверная версия DDK в `CONFIG_MTK_GPU_VERSION` — нет, каталог выбирается
   словом `clark`.
2. GED не завершает инициализацию — нет, тех строк лога в нашем дереве
   вообще не существует.
3. Нулевая тактовая частота GPU — нет, `clock freq = 659952` после включения.
4. Некогерентность кэша: добавлен явный `OSCPUCacheFlushRangeKM` по буферам
   кода и данных прошивки перед снятием ресета (`RGXFlushFWMemForBoot`) —
   сброс отрабатывает, результат не изменился.
5. Чтение состояния прошивки из устаревшего кэша: добавлена инвалидация
   перед чтением стадии — `stage` и `bFirmwareStarted` по-прежнему нули.
6. Правки MTK в пути запуска — нет. Склонирован эталонный DDK 1.15 от TI
   (`git.ti.com/git/graphics/ti-img-rogue-driver.git`, ветка
   `android/k5.10/1.15.6133109`): `rgxstartstop.c` и `rgxlayer_impl.c`
   **побайтово совпадают** с нашими.
7. Сбой отображения регистров HWAPM — нет: `regs pa=0x13ffe000 va=...`,
   обратное чтение `0x504=0x01a80000`, `0x510=0x00b800c8`, `0x24=0x8004001b`
   совпадает с записанным.
8. Линковка пустой заглушки `mt_gpufreq_enable_MTCMOS` из `ged/src/ged.c`
   вместо настоящей — нет, в `vmlinux` настоящая (рядом лежит
   `__mt_gpufreq_switch_to_clksrc`).
9. `mtcmos_mfg_core0` оставлен на откуп HWAPM — включили домен явно
   (`mt_gpufreq_enable_MTCMOS(false)` вместо `true`), не помогло.
10. Нарушения EMI MPU по GPU — в логе нет ни одного.

### Что установлено положительно

Полная трасса `RGXStart` (все шаги в правильном порядке):

```
Disable sys_bus_secure -> soft reset everything -> Rascal and Dust out of reset
-> Take everything out of reset but MIPS -> RGX clock: use default
-> Configure MIPS wrapper -> Write wrapper config register
-> Write boot/data/exceptions/trampoline remap registers
-> Set GARTEN_IDLE type to MIPS -> Init AXI-ACE interface
-> Take MIPS out of reset -> RGX Firmware Master boot Start
```

Состояние MIPS вокруг старта:

```
до RGXStart:   EXC=0x03 (SI_ERL|SI_EXL - штатное состояние после сброса)
               WRAPPER=0x00010000, stage=0, fwStarted=0
+0 мс:         EXC=0x20 (SI_SLEEP), WRAPPER=0x00017f00
+1/+10/+100 мс: без изменений, stage=0, fwStarted=0
```

Адреса, вычисленные драйвером, совпадают с содержимым регистров ремапа, а
первые слова секции `MIPS_BOOT_CODE` в памяти совпадают с `rgx.fw`
(`02fc039f 18000000 00fc038c 04ec039c`).

**Граница поиска:** ядро MIPS выведено из ресета, имеет питание 0.8 В и такт
660 МГц, видит корректный бутлоадер по корректному адресу — и в течение
миллисекунды сообщает `SI_SLEEP`, не выбрав ни одной инструкции. Ни один из
наблюдаемых со стороны драйвера параметров от эталона не отличается.

Семантика `SI_SLEEP` в открытых источниках не описана; в одном из прогонов
он был снят на отметке +0 мс и появился на +1 мс, в другом стоял уже на
+0 мс — то есть это состояние после старта, а не до него.

### Ещё три версии, проверенные и снятые

11. **Заглушка `mt_gpufreq_enable_MTCMOS` из `ged/src/ged.c`** вместо
    настоящей из `gpufreq/mt6761/mtk_gpufreq_core.c`. Обе существуют в
    дереве, но в объектнике `ged.o` символа нет (компилируется по условию), а
    в `vmlinux` линкована настоящая — рядом лежит
    `__mt_gpufreq_switch_to_clksrc`. Не подтвердилось.
12. **`mtcmos_mfg_core0` оставлен на откуп HWAPM.** В
    `mt_gpufreq_enable_MTCMOS()` домен core0 готовится только при аргументе
    `false`, а при `MTK_USE_HW_APM` вызывается с `true`. Включили домен явно —
    поведение не изменилось. Правка откачена как неоправданная.
13. **Питание GPU якобы не включается**, потому что `mt_gpufreq_voltage_enable_set(BUCK_ON)`
    стоит под `#ifdef MTK_GPU_DVFS`, а этот флаг задаётся только в
    `ged/Makefile`. **Ошибка поиска с моей стороны**: флаг определён в самом
    `mtk_mfgsys.c`, строка 59 (`#define MTK_GPU_DVFS 1`) — я искал его в
    заголовках и Makefile, но не в исходнике. Бука включается штатно. Правка
    откачена.

Урок, повторившийся за ночь трижды: **прежде чем строить версию на отсутствии
чего-либо, искать это во всём дереве, включая сам файл.**

### Состояние на утро

В дереве остаётся только диагностика, без изменений поведения:
- `rgxpower.c` — `RGXLogMipsState()` (состояние MIPS до старта и через
  0/1/10/100 мс), `RGXFlushFWMemForBoot()` (сброс кэша прошивки перед
  стартом), чтение адресов и содержимого бутлоадера в `RGXCheckFWBootStage()`;
- `rgxlayer_impl.c` + `Makefile` — вывод `RGXCommentLog` под
  `BF7_TRACE_RGXSTART` (полная трасса `RGXStart`);
- `mtk_mfgsys.c` — печать адреса и обратного чтения регистров HWAPM.

Эталонный DDK 1.15 от TI лежит распакованным для дальнейших сверок в
scratchpad (`ti-ddk/`), склонирован из
`git.ti.com/git/graphics/ti-img-rogue-driver.git`, ветка
`android/k5.10/1.15.6133109`.

## 2026-09-15 (утро) — GPU: ещё четыре версии сняты; по тачу найдено всё недостающее

### GPU

14. **`bDevicePA0IsValid = IMG_TRUE` в `sysconfig.c:284`** — флаг объективно
    неверен: `/proc/iomem` на устройстве показывает, что System RAM начинается
    с `0x40000000`, а `0x0` попадает в регистровое пространство. Из-за этого
    отключался обход аппаратной ошибки BRN 63553, которая есть у нашего ядра
    (`FIX_HW_BRN_63553` в `hwdefs/rogue/km/cores/rgxcore_km_22.68.54.30.h`) и
    которую DDK применяет, по его собственной формулировке, «чтобы избежать
    непрерывных обращений по неотображённому адресу 0x0». **Исправлено на
    `IMG_FALSE`** — обход теперь включается (`remap5: C1=0x...01,
    C2=0xef9f800c`), но прошивка всё равно не стартует. Правка оставлена: она
    верна по существу.
15. **Решающий опыт с сигнатурой.** Перед снятием ресета в ячейку, куда
    бутлоадер обязан записать номер стадии, записывается `0xBF7ABCDE`. Через
    100 мс после старта значение **осталось нетронутым**. Значит MIPS не
    пишет в память ни байта — то есть не выполняет ничего. Прежнее наблюдение
    «ERL/EXL сброшены» объясняется самим сбросом, а не исполнением.
16. **Такт и питание окончательно исключены.** `RGX_CR_TIMER` тикает ровно:
    +259 за каждые 100 мкс, до старта и после. `SOFT_RESET2 = 0`,
    `CLK_CTRL = 0x00002800000a0000`, `CLK_STATUS = 0x0000000000600000`
    стабильны. Ядро GPU живо и тактируется.
17. **DTB совпадает с заводским побайтово** (`8f69c794…`, сверено с dtb из
    `boot_b.img`). Управления тактами MIPS в `RGX_CR_CLK_CTRL`/`CLK_CTRL2`
    нет — там только блоки графического конвейера.

**Итог по GPU.** Ядро GPU тактируется и не в ресете, обёртка и все пять
ремапов настроены верно и читаются обратно, бутлоадер лежит по нужному адресу
в правильном виде, кэш сброшен, обход BRN включён — и MIPS не выбирает ни
одной инструкции. Ни один наблюдаемый со стороны драйвера параметр не
отличается от эталонного DDK 1.15, а то же самое железо с той же прошивкой
стартует за 0.01 с на заводском ядре. Причина остаётся ненайденной.

### Тач: недостающие данные найдены в оверлее

Агент не нашёл параметров тача в `oemdtb.dts`, и это было верно — они лежат
не там, а в **оверлее** `scratchpad/dtbo/entry0.dts`:

```
fragment@26: target = <&spi2>
    tran_touch: touch@0 {
        compatible = "mediatek,cap_touch_2", "novatek,NVT-ts-spi";
        reg = <0x00>;                      /* chip select 0 */
        spi-max-frequency = <0x7a1200>;    /* 8 МГц */
        omnivision,spi-mode = <0x03>;
        omnivision,reset-on-state = <0x00>, omnivision,irq-on-state = <0x00>;
        omnivision,reset-active-ms = <0x14>, reset-delay-ms = <0xc8>;
    }
fragment@27: tpd-resolution = <0x2d0 0x64c>   /* 720 x 1612 - наша панель */
             tpd-max-touch-num = <0x05>
fragment@28: ctp_pins_eint_as_int  pinmux = <0x400>   -> GPIO 4   (прерывание)
             ctp_pins_rst_output0  pinmux = <0xae00>  -> GPIO 174 (сброс)
```

То есть: шина **spi2**, CS 0, 8 МГц, режим 3; прерывание **GPIO 4**, сброс
**GPIO 174**; разрешение 720x1612, до 5 касаний. Свойства с префиксом
`omnivision,` — ровно те, что читает TCM-драйвер.

Заводской драйвер получает GPIO через MTK-фреймворк tpd (`tpd_get_gpio_info`,
состояния `ctp_pins_*`), которого в нашем дереве нет. Донорский TD4320 читает
их штатными `of_get_named_gpio` по своим именам, поэтому при порте нужно
добавить в узел свойства вида `synaptics,irq-gpio` / `synaptics,reset-gpio`
с этими номерами — правкой оверлея либо отдельным узлом.

## 2026-09-15 (утро) — загрузка стояла из-за некому читать консоль

Всю ночь цикл «прошивка -> загрузка -> опыт» занимал 8–13 минут, и я списывал
это на медленную загрузку. Это было неверно. Указал на это пользователь:
телефон не отвечал, я обращался к консоли — и порты поднимались мгновенно.

Причина: консоль ядра на кандидате — USB-гаджет (`console=ttyGS0,115200`).
Пока хост не держит порт открытым, запись в консоль блокируется: буфер
гаджета заполняется, и пишущий засыпает. systemd и fsck пишут в консоль и
встают. `systemd-analyze blame` показывал это прямым текстом:

```
12min 39.773s dev-mmcblk0p48.device
12min 38.778s systemd-fsck-root.service
```

Это не работа, а ожидание. В момент открытия порта из буфера вывалилось
**89 145 байт** накопленного лога, и загрузка доехала до конца за секунды.

Замер до и после:

```
без читателя консоли:  Startup finished in 12min 45.570s
с читателем:           Startup finished in 7.556s, SSH через 29 с
```

Написан `native-linux-build/alps191-slot-test/bf7-console-logger.py` —
постоянный читатель с отметками времени. Две ловушки, на которые я наступил
при его отладке:

- **узел переезжает.** Если прежний экземпляр всё ещё держит `ttyACM0`, новая
  переenumerация даёт `ttyACM1`, и чтение «того же» пути молча ничего не
  находит. Логгер теперь выбирает последний `/dev/ttyACM*`.
- **`select()` не сообщает об отключении USB** — дескриптор просто молчит
  навсегда. Существование узла надо опрашивать отдельно.

Ещё одна собственная грабля, стоившая времени: `pgrep -f "build-…"` находит
**сам себя**, потому что строка поиска присутствует в командной строке
запускающей команды. Все ожидания сборки вхолостую выкручивали таймаут до
конца. Ждать надо по маркеру в логе (`kernel release:`), а не по pgrep.

## 2026-09-15 (утро) — тач заработал полностью

Порт драйвера Synaptics/Omnivision TCM (`drivers/input/touchscreen/TD4160/`,
донор — `x657b-4.19/.../TD4320/`) доведён до рабочего состояния.

### Последняя недостающая деталь — прерывание

`gpio_to_irq(4)` возвращал `-22`, драйвер откатывался на опрос. Причина: на MTK
номер EINT не выводится из глобального номера GPIO. В дереве устройств узел
`touch` (`compatible = "mediatek,touch"`) задаёт `interrupts = <4 8 4 0>`,
`interrupt-parent = 0x1e`. Драйвер научен брать прерывание оттуда:

```c
np = of_find_compatible_node(NULL, NULL, "mediatek,touch");
if (np) { irq = irq_of_parse_and_map(np, 0); of_node_put(np); }
if (irq <= 0) irq = gpio_to_irq(bdata->irq_gpio);
```

Результат на устройстве: `129: mt-eint 4 Level synaptics_tcm`.

### Проверка на железе

Загрузка прошивки в контроллер: `ROMBOOT_APP_CODE` 102400 байт из
`synaptics/hdl_firmware.img` (образ разобран, все пять областей с корректным
CRC: BOOT_CONFIG 256, ROMBOOT_APP_CODE 102400, APP_CONFIG 4096, DISPLAY 8192,
OPEN_SHORT 4096). Чип перешёл `0x04 MODE_ROMBOOTLOADER` -> `0x02 MODE_HOST_DOWNLOAD`.

Строка `touch_set_input_reporting: Application firmware not running` **безобидна**:
при `MODE_HOST_DOWNLOAD` функция не выходит (`synaptics_tcm_touch.c:981`), а идёт
дальше, вычитывает параметры из контроллера и регистрирует устройство ввода.
Сообщения `reflash_*: Failed to request firmware.img` тоже безобидны — это модуль
для чипов с собственной флеш-памятью, у нашего её нет.

Захват `/dev/input/event2` во время касаний:

```
событий: 7566
слоты: [0, 1, 2, 3]   уникальных касаний (tracking id): 14
ABS_MT_POSITION_X   171 .. 582
ABS_MT_POSITION_Y   402 .. 1231
EV_KEY BTN_TOUCH / BTN_TOOL_FINGER, EV_SYN 1135
```

Прерываний за проход пальцем: 13 -> 6980. Мультитач протокола B со слотами и
tracking id — то, что требуется libinput.

### Грабля методики (вторая за день)

Первый захват дал 0 байт: `dd if=/dev/input/event2 bs=1` возвращает `EINVAL`,
evdev требует чтения кратными `sizeof(struct input_event)` = 24 байта. Читать
только `cat` либо `bs=24`.

И ещё раз наступил на `pkill -f`/`pgrep -f`: шаблон совпал с командной строкой
самой ssh-сессии и убил её. Никогда не подставлять в `-f` строку, которая есть
в собственной команде.

### GPU: новое направление после снятых версий 1-17

Регистры GPU читаются по конфигурационной шине, а выборка инструкций MIPS идёт
по шине данных MFG -> EMI -> DRAM. Это разные пути, поэтому «`RGX_CR_TIMER`
тикает» не доказывает работоспособность второго. Проверяются две ветки:
шинный барьер `INFRA_TOPAXI_PROTECTEN` при включении домена MFG и защита
региона DRAM в EMI MPU для домена GPU (прецедент в этом же проекте — Wi-Fi).
Для проверки в конфигурацию добавлено `CONFIG_DEVMEM=y` и снят
`CONFIG_STRICT_DEVMEM` — доступа к физическим регистрам из userspace не было.

## 2026-09-15 (день) — GPU: диагноз переломлен, прошивка ВЫПОЛНЯЕТСЯ

### Главное: «boot stage 0/6» был ложным следом

Счётчик стадий по смещению `RGXMIPSFW_BOOT_STAGE_OFFSET` пишет только отладочная
сборка прошивки. У нас заводская `rgx.fw`, она этот счётчик не трогает никогда.
Все прежние выводы «прошивка не дошла до стадии 1» ничего не значили.

### Решающий опыт: диф всего буфера, а не одного слова

Вместо чтения одного слова по вычисленному смещению снимается копия **всего**
буфера данных прошивки перед снятием ресета и сравнивается после. Результат:

```
BF7 FWDATA DIFF: 10 of 4096 words changed
BF7 SYSINIT DIFF: 0 of 128 words changed, bFirmwareStarted=0
```

**MIPS выполняет код.** Изменения лежат выше `RGXMIPSFW_PRIVATE_DATA_OFFSET`
(0x800) — это приватная область бутлоадера: `+0x810` счётчик, `+0x994…0x9f8`
блок с адресами `0x9fc02a9d` (внутри `.exctext`) и `0xbfc000db` (внутри
`.bootandnmitext`). А область `FwSysInit`, куда пишет уже основная прошивка,
не тронута ни одним байтом.

**Граница отказа сместилась**: работает бутлоадер, основная прошивка не
стартует. Это переход «настроить TLB по таблицам страниц → прыжок на
`0xC0000000`».

### Что проверено железом и снято

18. **Шинный барьер MFG (`INFRA_TOPAXI_PROTECTEN`).** Добавлен съём регистров
    в драйвер. `PROTECTEN = 0x00000098`, `PROTECTEN_STA1 = 0x001f10b8` — биты
    21, 22, 25 (MFG) нулевые и в команде, и в подтверждении. Барьер открыт.
    `PWR_STATUS` бит 4 = 1 (MFG запитан), `MFG_PWR_CON = 0x00000e0d`
    (`PWR_ON`, `PWR_ON_2ND`, изоляция снята), `MFG_CG = 0x00000070` (биты
    BAXI/BMEM/BG3D/B26M нулевые — такты не загейчены).
19. **EMI MPU.** Драйвер пробится (`[MPU] initialize EMI MPU`, `IRQ 44`),
    нарушений нет ни одного. Плюс это опровергается самим фактом: MIPS пишет
    в DRAM, значит доступ туда у него есть. GPU-драйвер `emi_mpu_set_protection`
    не вызывает вовсе — во всех трёх версиях PowerVR в дереве.
20. **Версия DDK.** Заводское ядро извлечено из `boot_b.img` и содержит ту же
    строку `Rogue_DDK_Android rogueddk 1.15@6070602`, а все 27 строк шагов
    `RGXStart` совпадают с нашими дословно. Срез всё же более поздний (у него
    есть мосты `DevmemXInt*` и `Devmem*2`, которых нет в нашем дереве вообще),
    но путь запуска MIPS одинаков.
21. **Таблицы страниц MIPS прочитаны из физической памяти** (`/dev/mem`):
    102 записи, все `G=1 V=1 D=1`, кэшируемые (`coh=3`).
22. **Код прошивки лежит там, куда указывают таблицы.** По адресу из PTE[0]
    (device `0xecb4b000`) в DRAM лежит `.text` из `rgx.fw` побайтово
    (`ed7fae02 008445bf ed1f5b3c 0545459f …`), по адресу кодового ремапа —
    `.exctext` (`00fc036a c00041ba d9d0035b …`). Оба совпали.
23. **Прерывание GPU не срабатывало ни разу**: `94: 0 0 0 0 pvrsrvkm`.

### Найден и исправлен настоящий дефект (не тот, но реальный)

`RGX_FW_BOOT_PARAMS uFWParams;` в `rgxsrvinit.c:697` — неинициализированная
переменная на стеке. Цикл заполняет `asFWPageTableAddr` только до
`ui32FWPageTableNumPages` (2 на этом SoC), а `RGXProcessFWImage()` копирует в
загрузочную конфигурацию прошивки **все четыре** элемента. Прочитано с
устройства: `aui64PTPhyAddr[3] = 0xffffffff7ff143e0` — мусор со стека, и
бутлоадер его себе скопировал. Исправлено: структура обнуляется, хвост массива
заполняется нулями явно. После правки конфигурация чистая
(`PT[2]=PT[3]=0`), число изменённых слов 11 → 10. Прошивку это не оживило,
но отдавать прошивке содержимое стека нельзя в любом случае.

### Расшифровка загрузочной конфигурации (читается из `/dev/mem`)

```
+0x00 ui64StackPhyAddr   = 0xecb86000
+0x08 ui64RegBase        = 0x7f000000   (SUPPORT_ALT_REGBASE, вместо 0x13000000)
+0x10 aui64PTPhyAddr[0]  = 0xecaf0000
+0x18 aui64PTPhyAddr[1]  = 0xecaf4000   (ровно 16 КБ врозь)
+0x20 aui64PTPhyAddr[2]  = 0
+0x28 aui64PTPhyAddr[3]  = 0
+0x30 ui32PTLog2PageSize = 14 (16 КБ)
+0x34 ui32PTNumPages     = 2
```

### Важная поправка к прошлым рассуждениям

У телефона **4 ГБ** (`MemTotal: 3892460 kB`), System RAM идёт до `0x13FFFEFFF`,
то есть верхний ранг лежит выше 32-битной границы в CPU-пространстве. GPU видит
память со сдвигом −1 ГБ (`SYSTEM_1G_ADDRESS_SHIFT` в `sysconfig.c`), поэтому
device-адреса вида `0xecb4b000` соответствуют CPU-адресам `0x12cb4b000` и
полностью корректны. Из-за этого же снята прежняя правка
`bDevicePA0IsValid = IMG_FALSE`: она была построена на CPU-адресах, а в
device-пространстве адрес 0 — это как раз начало DRAM, то есть валидная память.

### Инструмент, которого не хватало всё это время

В конфигурацию добавлено `CONFIG_DEVMEM=y` и снят `CONFIG_STRICT_DEVMEM`.
Теперь физическая память и регистры читаются из userspace без пересборки ядра —
именно этим прочитаны загрузочная конфигурация, таблицы страниц и код прошивки.

### Следующий шаг

Дизассемблировать `.bootandnmitext` из `rgx.fw` (micromips, MIPS32r2) и найти,
что происходит по адресу `0xbfc000da` — его бутлоадер записал себе в приватную
область последним. Это должно указать точное место остановки.

## 2026-09-15 (вечер) — GPU ЗАПУСТИЛСЯ: `bFirmwareStarted = 1`

Прошивка GPU впервые инициализировалась на кандидатском ядре. Ошибок в логе
драйвера — ноль, `rgx.fw` и `rgx.sh` загружены, `/dev/dri/renderD128`
открывается.

### Как нашли причину

Ключ дала не сверка кода, а разговор с самой прошивкой. Цепочка:

1. **Диф всего буфера вместо одного слова** показал, что MIPS исполняется.
2. **Замена `wait` на `nop`** в аварийной петле `0xbfc00480: di; wait; b .`
   убрала `SI_SLEEP` — значит ядро действительно доходит до этой петли.
   Контрольный прогон (тот же код, но без записи байтов) вернул `SI_SLEEP`:
   влияние правки доказано.
3. **Сохранение `$ra` из этой точки** дало `0xc00032d5` — адрес внутри
   `.text`. То есть работает **основная прошивка**, а не бутлоадер.
4. **Дизассемблирование** (capstone, micromips, little-endian, режим
   `CS_MODE_MIPS32|CS_MODE_MICRO`; первая инструкция секции не декодируется,
   нужен `skipdata`) показало: по `0xc00032d0` прошивка зовёт `0x9fc0274c`,
   а это всего лишь `save regs; a0=1; t9=5; sdbbp 1; restore; jr $ra` —
   её журнальный хук, написанный так, чтобы **продолжить** после `sdbbp`.
   Но на MIPS EJTAG отладочное исключение вектором идёт на `0xBFC00480`, где
   у этой прошивки стоит остановка. Прошивка не падала — она умирала в
   собственном механизме отчёта.
5. **`sdbbp` заменён на `nop16`** (`0x46c1` -> `0x0c00` по смещению
   `.exctext + 0x764`). Прошивка ушла дальше: изменённых слов 10 -> 74,
   и она записала штатное состояние исключения:
   `Cause = 0x00800024` -> **ExcCode 9 (Breakpoint)**, `EPC = 0xc0000569`,
   `SP = 0xcf600e30`, плюс полная таблица TLB.
6. **`0xc0000568` оказался `break16 0`** внутри функции assert’а
   (`0xc000055c: lw $v0,-0x12c0($gp); andi 1; beqz -> форматирование;
   иначе break`).
7. **Аргументы assert’а сняты** подменой пути форматирования на сохранение
   `a0/a1/a2` в некэшируемую страницу `kseg1 0xbfc0171x` (стек прошивки
   бутлоадер отображает write-back, поэтому через `/dev/mem` он читается
   нулями — обойти можно только так).

### Что сказала прошивка

```
ASSERT Failed: (false) <firmware/rgxfw_init.c:579>
```

Явный «неподдерживаемый случай» в инициализации: прошивка получила от
драйвера конфигурацию, которую не умеет разбирать. Это подтверждает
расхождение срезов: наше дерево — более ранний срез 1.15, чем тот, под
который собрана заводская `rgx.fw` (у заводского ядра есть мосты
`DevmemXInt*` и `Devmem*2`, которых у нас нет вовсе), хотя строка версии у
обоих одна — `1.15@6070602`.

### Обход, который поднял GPU

Путь форматирования assert’а (`0xc0000570..0xc000058f`, 32 байта) заменён на
сохранение аргументов и **возврат из функции** вместо падения в `break`:

```
lui   $at, 0xbfc0
sw    $a0, 0x1710($at)      ; строка условия
sw    $a1, 0x1714($at)      ; имя файла
sw    $a2, 0x1718($at)      ; номер строки
lw    $ra, 0x24($sp)
addiu $sp, $sp, 0x28
jr16  $ra
```

Результат: `fwStarted = 1`, `EXC = 0x20` (штатный простой прошивки в ожидании
работы), `/dev/dri/card0` и `renderD128` появились как рабочие узлы, ошибок
в логе нет.

### Чего это НЕ решает

Обход глушит **все** assert’ы прошивки, а не устраняет причину. Настоящее
исправление — согласовать раскладку общих структур: либо взять срез PVR из
alps191, либо найти прошивку под наш срез. До тех пор считать состояние
рабочим, но не окончательным.

### Пригодившиеся приёмы

- Кодировки micromips выводятся из самого образа: `lui` = `0x41A0|rt`,
  32-битный `sw` = `0xF800|(rt<<5)|base`, `lw` = `0xFC00|(rt<<5)|base`,
  `jr16` = `0x459f` для `$ra`, `b16` = `0xCC00|(((цель-(PC+2))>>1)&0x3FF)`.
  Каждую сборку проверял обратным дизассемблированием перед прошивкой.
- Писать из прошивки надо в `kseg1` (`0xbfc01xxx`) — это некэшируемо и
  сразу видно через `/dev/mem`. Всё, что прошивка пишет в свой стек или в
  TLB-отображения, оседает в её кэше и до DRAM не доходит.
- Для полного цикла заведён `kernel-work/scripts/bf7-cycle.sh`: дважды подряд
  образ собирался не из того каталога, прошивался предыдущий, а вывод
  выглядел свежим.

## 2026-09-15 (вечер) — GPU: причина найдена и исправлена в C, без ассемблера

Обход, глушивший assert правкой инструкций прошивки, **снят полностью**.
Прошивка стартует на чистом ядре: `fwStarted = 1`, метка `0xbf7abcde` в ячейке
стадии цела (значит assert не срабатывал ни разу), ошибок в логе драйвера ноль,
`/dev/dri/card0` и `renderD128` открываются.

### Как дошли до причины

Цепочку вызовов размотали снизу вверх, снимая адреса возврата записью в
некэшируемую страницу `kseg1 0xbfc01xxx` (стек прошивки отображён write-back,
через `/dev/mem` он читается нулями):

1. `$ra` в точке остановки -> `0xc00032d5`, то есть работает **основная**
   прошивка, а не бутлоадер.
2. `$ra` в функции assert’а -> `0xc0008665`, регистратор assert’а.
3. Адрес возврата регистратора (он кладёт свой `$ra` в `0x1c($sp)`, а изнутри
   assert’а это `0x44($sp)`) -> `0xc000065c`.

По адресу `0xc0000630` оказался цикл поэлементного сравнения:

```
lw16 $a2, ($v1)        ; значение от драйвера
lw16 $a3, 4($v0)       ; своё значение прошивки
beq  $a2, $a3, ...     ; совпало - дальше
addiu $a1, $zero, 0x243 ; иначе строка 579
jals  0x8594           ; вызов assert'а
bne  $s0, 0x64, ...    ; цикл, 25 элементов
```

Это `RGXFWAlignChecks` — сверка раскладки общих структур.

### Диагноз, полученный сравнением таблиц

Массив драйвера напечатан из `RGXFWSetupAlignChecks`, массив прошивки найден
в самом образе `rgx.fw` по смещению **0x179c0** (поиск 25-словного окна,
совпадающего с нашим). Из 25 значений разошлись **ровно три**, и все три —
про одну структуру:

| индекс | что это | драйвер | прошивка |
|---|---|---|---|
| 14 | `sizeof(RGXFWIF_FWRENDERCONTEXT)` | 512 | 528 |
| 16 | `offsetof(RGXFWIF_FWRENDERCONTEXT, s3DContext)` | 160 | 168 |
| 17 | `sizeof(RGXFWIF_FWCOMMONCONTEXT)` | 160 | 168 |

Остальные 22 совпали, включая `offsetof(psCCB)=4`, `offsetof(sRunNode)=32`,
`offsetof(psFWMemContext)=48`. Значит `RGXFWIF_FWCOMMONCONTEXT` в том срезе, под
который собрана заводская прошивка, на 8 байт больше, добавленное лежит **после**
смещения 48, а всё, что прошивка закрепляет, стоит на своих местах.

### Исправление

В `include/rogue/rgx_fwif_km.h` в конец `RGXFWIF_FWCOMMONCONTEXT` добавлено
восемь зарезервированных байт (`IMG_UINT64 RGXFW_ALIGN ui64BF7LayoutReserved`).
`UNCACHED_ALIGN` здесь `aligned(8)`, поэтому размер становится ровно 168, а
`RGXFWRENDERCONTEXT` — 528. После этого массив драйвера совпадает с массивом
прошивки поэлементно.

**Оговорка, которую надо помнить.** Мы знаем, что размер вырос на 8 и что три
закреплённых смещения не сдвинулись, но не знаем, какое поле добавили и где
именно. Если его вставили в середину, а не в конец, поля после него всё ещё
расходятся — проявится это не отказом проверки, а неверными значениями
приоритета, статистики или robustness-адреса.

### Что осталось от ассемблерных правок

Ничего рабочего: оба патча (`sdbbp -> nop` и подмена пути assert’а) отключены
условием `if (0)` и оставлены в коде как документация метода. Инструментовка
(`RGXLogMipsState`, `RGXLogMtkBusState`, дифы буферов, печать массива
выравнивания) тоже остаётся — она диагностическая и поведение не меняет.

## 2026-09-15 (вечер) — GPU исполняет команды, снята трассировка прошивки

Журнал прошивки включается на живую, без пересборки:
`/proc/pvr/apphint/0/EnableLogGroup` принимает список групп
(`main mts cleanup csw bif pm rtd spm pow hwr hwp rpm dma misc debug`),
`/proc/pvr/apphint/0/FirmwareLogType` = `trace`. Читается из
`/proc/pvr/firmware_trace`.

Но сам по себе тип журнала только пишется в общую память — прошивке его никто
не относит, пока ей не пошлют команду. Рабочий приём: записать что-нибудь в
`/proc/pvr/apphint/HWPerfFWFilter`, это гарантированно шлёт KCCB-команду и
поднимает питание.

Снято с живого устройства:

```
118841:T0-MAIN> Core clock set to 460000000 Hz
118852:T0-POW>  Active PM latency set to 3ms. Core clock: 460000000 Hz
118876:T0-MAIN> GPU init
118901:T0-MAIN> Host Sync Partition marker: 2
661043:T0-MTS>  KCCB Slot 2: Cmd=0x2abc0066, OSid=0
661084:T0-MTS>  KCCB Slot 3: Cmd=0x2abc00c9, OSid=0
661132:T0-MTS>  KCCB Slot 3: Return value 1
661160:T0-MTS>  Kick MTS Irq task DM=0
661209:T0-POW>  Check Pow state: Int: 0x1, Ext: 0x1, Pow flags: 0x10000
661234:T0-MTS>  Irq Task complete.
```

Что это доказывает: ядро GPU тактируется на 460 МГц, прошивка прошла `GPU init`,
её планировщик MTS крутит фоновые задачи и задачи прерываний, команды из кольца
KCCB вычитываются и исполняются (`commands executed` 2 -> 4), управление питанием
работает. И **впервые сработало прерывание GPU**: `94: 1 0 0 1 pvrsrvkm` — до
этого счётчик был нулевым всё время работ.

То есть связка «драйвер -> прошивка -> ответ прерыванием» замкнулась полностью.

## 2026-09-15 (ночь) — найден вендорский источник: наш срез PVR был неполным

Заведён каталог `gpu-research/` (см. его `README.md`) — постоянное хранилище
материалов по GPU. Ключевая находка там: GPL-релиз ядерных модулей для Rabbit R1
на MT6765 (`rabbit-hmi-oss/android_kernel_modules_rabbit_mt6765`, ветка `r1`)
содержит каталог **`m1.15ED6070602` с тем же именем, что у нас, но более
позднего содержания**.

### Догадка подтвердилась и получила имя

Восемь байт, которые я вслепую зарезервировал в `RGXFWIF_FWCOMMONCONTEXT`, — это
реальное поле из вендорского дерева:

```c
#define RGXFWIF_CONTEXT_MISC_FLAGS_STATS_PENDING   (1U << 0)
#define RGXFWIF_CONTEXT_MISC_FLAGS_HAS_DEFER_COUNT (1U << 1)
...
	IMG_UINT32	ui32MiscFlags;    /* было IMG_BOOL bStatsPending - тот же размер и смещение */
...
	IMG_UINT32	ui32DeferCount;   /* дописано в конец -> 160 становится 168 */
```

Плюс драйвер обязан сообщить прошивке о поддержке нового поля:
`BITMASK_SET(psFWCommonContext->ui32MiscFlags, RGXFWIF_CONTEXT_MISC_FLAGS_HAS_DEFER_COUNT)`
сразу после `psFWCommonContext->eDM = eDM;` в `rgxfwutils.c`.

Заглушка `ui64BF7LayoutReserved` убрана. После нормализации комментариев
раскладка структуры **побайтово совпадает** с вендорской. На устройстве:
`528 / 168 / 168` в таблице выравнивания, `fwStarted=1`, ошибок ноль, метка
`0xbf7abcde` цела.

### Что это объясняет

Заводская `rgx.fw` собрана под тот вариант среза, который лежит в дереве Rabbit,
а не под наш. Отсюда же и недостающие мосты `DevmemXInt*`/`Devmem*2`, которые
есть в заводском ядре и в заводской `libsrv_um.so`.

### Что ещё расходится и пока НЕ применено

Всего различается 13 файлов. Кроме `rgx_fwif_km.h` это:
- `rgxheapconfig.h` — RESERVED-область 4 МиБ -> 2 МиБ, `RGX_GENERAL_SVM_HEAP_BASE`
  со сдвигом; на проверку выравнивания не влияет, но разбираться отдельно;
- одиннадцать файлов `generated/*/server_*_bridge.c` — похоже на правки
  безопасности (те самые мосты и проверки границ).

### Прошивки открытого стека — сохранены про запас

`gpu-research/upstream-firmware/` — 16 файлов из форка Imagination
(`gitlab.freedesktop.org/imagination/linux-firmware`, ветка `powervr`), включая
**`rogue_22.68.54.30_v1.fw` под наш BVNC**. Для открытого драйвера, с заводской
`rgx.fw` не взаимозаменяема.

## 2026-09-15 (ночь) — переезд на дерево 4.19.325 завершён

Кандидат теперь собирается из `kernel-work/x657b-4.19` — это Linux **4.19.325**
(ветка CIP SLTS) с родной поддержкой MT6761: `CONFIG_MACH_MT6761=y`,
`mt6761.dts`, конфиг `k61v1_64_bsp_defconfig`. Против прежнего 4.19.191 это
134 стабильных релиза вперёд, 126 каталогов MTK-драйверов против 92 и 61 644
файла `.c/.h` против 54 514.

Работает всё: `uname -r` = `4.19.325-cip124-st8-bf7-x657b-v1`, systemd
`running`, корень `/dev/mmcblk0p48 ext4 rw`, тач (`129: mt-eint 4`,
`mtk-tpd2 -> event2`), Wi-Fi (`wlan0 192.168.27.140`, 3 модуля), USB RNDIS
(`rndis0 192.168.7.2`), GPU (`card0`, `renderD128`, драйвер и прошивка OK,
ноль ошибок).

### Что перенесено, а что уже было

Перенесены три патча (применились чисто) и драйвер тача:
`u_ether` (порча кадров RNDIS), проверка двойной регистрации в `notifier`,
раскладка тега videolfb и защита при засыпании дисплея, каталог `TD4160/`
с подключением в `Kconfig`/`Makefile`.

**Уже было в дереве**: панель `bf7_lcm_recovered` (побайтово та же), правки под
clang 11 и — главное — более поздний срез PowerVR с `ui32DeferCount`,
`ui32MiscFlags` и мостами `DevmemXInt*`/`Devmem*2`. То есть исправление GPU,
которое я днём выводил из ассемблера прошивки и потом подтверждал по дереву
Rabbit, здесь присутствовало изначально.

### Две поломки самого дерева, которые пришлось чинить

**1. `scripts/mkcompile_h`.** На этой машине clang дополнительно печатает строку
`Found CUDA installation`, и `grep ' version '` цепляет её тоже — вторая строка
попадала внутрь строкового литерала `LINUX_COMPILER`, ломая `compile.h`.
Добавлен `head -n1`.

**2. `include/linux/lsm_hook_defs.h` — корень не монтировался вообще.**
Симптом: ядро грузится, USB и сеть поднимаются, но любой блочный раздел даёт
`ENOENT`, при этом `tmpfs` монтируется, а неизвестный тип ФС даёт `ENODEV`.
Диагностика шла через аварийный telnet на порту 2323 из initramfs.

Причина: в этом дереве бэкпортирована инфраструктура `fs_context`, и в
`fs/super.c:1537` есть

```c
if (fc->fs_type->fs_flags & FS_REQUIRES_DEV && !fc->source)
        return -ENOENT;
```

а имя устройства приходит как параметр `source` через
`vfs_parse_fs_param()`, где сначала опрашивается LSM:

```c
ret = security_fs_context_parse_param(fc, param);
if (ret != -ENOPARAM)
        return ret;          /* параметр до ФС не дойдёт */
```

`security_fs_context_parse_param()` зовёт хуки через
`call_int_hook(fs_context_parse_param, -ENOPARAM, ...)`, а этот макрос
останавливается на первом ненулевом ответе. В `lsm_hook_defs.h` для этого хука
значением по умолчанию было объявлено **`0`**, и собранный в ядро BPF LSM
проглатывал любой параметр монтирования, включая `source`. Итог: `fc->source`
оставался пустым и ни одна ФС, требующая устройства, не монтировалась.
Исправлено на `-ENOPARAM`. В прежнем дереве `CONFIG_BPF_LSM` просто не было,
поэтому проблема там не проявлялась.

### Мелочь по методике

`reboot -f bootloader` на этом ядре не работает — для перевода в fastboot есть
своя утилита, и она уже лежит в initramfs как `/bin/bf7-enter-fastboot`
(вызывает `reboot(RESTART2, "bootloader")`). Через SSH её запускает
`bf7-cycle.sh`, а из аварийного telnet — напрямую.

Сборочные скрипты: `build-bf7-x657b.sh`, `build-bf7-connectivity-x657b.sh`,
конфиг `configs/bf7-x657b-v1.config`; `bf7-cycle.sh` переключён на новое дерево.
Прежнее дерево `mediatek-mt6761-alps188-gpu115` оставлено нетронутым.

## 2026-09-15 (ночь) — лог загрузки выведен на экран телефона

Включена текстовая консоль на панели. Причина, по которой её не было: Android
собирает ядро без виртуальных терминалов, `# CONFIG_VT is not set`, поэтому не
существовало ни `/dev/tty0`, ни консоли на фреймбуфере — при том что
`console=tty0` в командной строке стоял всегда, а `/dev/fb0` (драйвер `mtkfb`)
исправно работал.

Добавлено в `configs/bf7-x657b-v1.config`:

```
CONFIG_VT=y
CONFIG_CONSOLE_TRANSLATIONS=y
CONFIG_VT_CONSOLE=y
CONFIG_HW_CONSOLE=y
CONFIG_VT_HW_CONSOLE_BINDING=y
CONFIG_FRAMEBUFFER_CONSOLE=y
CONFIG_FRAMEBUFFER_CONSOLE_DETECT_PRIMARY=y
```

`CONFIG_FONT_8x16` уже был включён. `CONFIG_LOGO` из итогового конфига выпал
при `olddefconfig` — на работу консоли не влияет, разбираться отдельно.

Проверено на устройстве:

```
vtcon0: (S) dummy device        | bind=0
vtcon1: (M) frame buffer device | bind=1
Console: switching to colour frame buffer device 90x100
активные консоли ядра: ttyS0 tty0 ttyGS0
getty@tty1.service   active running
```

То есть на панели 720x1600 получилась консоль 90 колонок на 100 строк, туда
идут сообщения ядра, вывод initramfs и systemd, а сверху работает приглашение
входа на `tty1` (и ещё getty на tty2-tty6).

Оговорка: вывод начинается с момента регистрации `mtkfb`
(`Console: switching...` идёт из его probe), всё что раньше — по-прежнему
только в USB-консоль `ttyGS0` и в `ttyS0`.

## fbcon на панели: почему был чёрный экран и что оказалось причиной

Консоль привязывалась (`vtcon1 bind=1`, `Console: switching to colour frame
buffer device 90x100`), но панель оставалась чёрной, а сканирование всего
framebuffer'а (14 413 824 байта, 4896 строк) давало **ровно ноль** непустых
байт. Печать в `/dev/tty1`, переключение VT и `KDSETMODE` KD_GRAPHICS→KD_TEXT
ничего не меняли, при том что `/dev/vcs1` показывал: текст в VT приходит.

### Отправная точка: в OEM-ядре консоли нет вообще

```
kernel-work/configs/bf7-oem-4.19.191.config:2614:# CONFIG_VT is not set
kernel-work/configs/bf7-oem-4.19.191.config:1630:CONFIG_MTK_FB_SUPPORT_ASSERTION_LAYER=y
```

Android-ядро собрано без VT, поэтому путь fbcon→mtkfb у вендора **не
исполнялся ни разу** — чинить там было нечего, и ломаться было нечему. Вывод
текста на панель из ядра у MediaTek сделан своим механизмом: `DAL_Printf()` →
`MFC_Print()` (`mtkfb_console.c`) поверх отдельного оверлея ASSERT_LAYER, им
пользуется AEE. `DAL_Printf` берёт `down_interruptible()`, то есть спящий
семафор, и как `console->write` не подключается — нужен kthread. Побочно это
объясняет исчезновение предупреждения LK про orange state: `is_DAL_Enabled()`
возвращает `isAEEEnabled`, который равен 0 до первого `DAL_Printf`, поэтому в
`mtkfb_set_par()` и в pan берётся ветка `if (!is_DAL_Enabled())`, гасящая
layer 3 — тот самый шрифтовый слой, куда рисует загрузчик.

### Диагностика

Гипотеза «`fbcon_init` вылетает раньше `ops->graphics = 0`» проверялась
метками в `fbcon_init`, `fbcon_putcs`, `bit_putcs`, `bit_putcs_aligned` и в
обёртке mtkfb. Она оказалась **неверной**:

```
BF7 fbcon_init: OK gfx=0 flags=0x0 state=0 rot=0 putcs=bit_putcs
                blit=cfb_imageblit fbops=mtkfb_ops pixmap(sz=8192 sa=1 ba=1 addr=1)
BF7 putcs: inact=0 state=0 vc_mode=0 gfx=0 y=0 x=0 n=90
BF7 bit_putcs: count=90 maxcnt=512 cell=16 mod=0 attr=0
BF7 aligned: w=720 h=16 dx=0 dy=0 -> cfb_imageblit
```

fbcon работал безупречно и рисовал. Ключ дал `%pS` вместо `%p`: в `mtkfb_ops`
лежал `cfb_imageblit`, а не наша обёртка. Нашлись **две** независимые поломки.

### Причина 1 — `set_fb_fix()` затирает таблицу операций в рантайме

Последние строки `set_fb_fix()` (`mtkfb.c`) выполняются **после** статического
инициализатора и безусловно перезаписывают три указателя:

```c
	fbops->fb_fillrect = cfb_fillrect;
	fbops->fb_copyarea = cfb_copyarea;
	fbops->fb_imageblit = cfb_imageblit;
```

Из-за этого консольные обёртки `mtkfb_con_*` не вставали, fbcon писал прямо в
`screen_base` — без `mtkfb_con_damaged()`, то есть без pan и без сброса кэша.
Заодно это объясняет, почему правка `fix->ypanstep = 0` подействовала: она
находится в той же функции, но выше.

Исправлено: обе ветки под `#if defined(CONFIG_FRAMEBUFFER_CONSOLE)`.

### Причина 2 — палитра никогда не заполнялась

У `mtkfb_ops` **нет** члена `.fb_setcolreg`, а именно он заполняет
`info->pseudo_palette`. `fb_set_cmap()` содержит проверку

```c
	if (start < 0 || (!info->fbops->fb_setcolreg && !info->fbops->fb_setcmap))
		return -EINVAL;
```

поэтому падения не было — палитра просто молча оставалась нулевой. При
`FB_VISUAL_TRUECOLOR` `cfb_imageblit()` берёт цвета оттуда:

```c
	fgcolor = ((u32 *)(p->pseudo_palette))[image->fg_color];
	bgcolor = ((u32 *)(p->pseudo_palette))[image->bg_color];
```

то есть `fg = bg = 0`. **Консоль всё это время исправно рисовала чёрным по
чёрному** — отсюда и чёрный экран, и нули в памяти.

Исправлено: добавлен `mtkfb_setcolreg()` (ARGB8888: A24 R16 G8 B0, палитра
`u32 pseudo_palette[17]`, индексы 0–15, альфа выставляется в непрозрачную) и
подключён в `mtkfb_ops`.

### Проверка на устройстве

Содержимое framebuffer'а после исправления — ровно два цвета, классическая
палитра VGA-консоли:

```
0xff000000  996 946 px   (фон)
0xffaaaaaa  163 694 px   (текст, индекс 7)
строк с текстом: 1230, от строки 2 до 1595
```

Доставка кадра на панель подтверждена счётчиком прерываний оверлея:

| состояние fbcon | прирост `ovl0` за 6 с |
|---|---|
| привязан | +249 |
| отвязан (`vtcon1/bind = 0`) | **+0** |
| снова привязан | +187 |

При отвязанной консоли дисплейный движок замирает полностью — значит цепочка
`mtkfb_con_damaged → mtkfb_con_flush → mtkfb_pan_display_impl →
primary_display_trigger` работает от начала до конца.

Снимок буфера: `scratchpad/bf7-fbcon.png` — читаются реальные строки kernel
log. Рваность картинки там артефакт захвата (буфер читался построчно ~30 с,
консоль всё это время прокручивалась), а не дефект вывода.

## GPU userspace: libhybris не нужен — нативный glibc-стек с Vulkan

Сегодняшний вывод «единственный путь — libhybris» **неверен**, и ошибка была в
сравнении: я сверял glibc-userspace от Allwinner A133 (DDK 1.19) с нашим ядром
(DDK 1.15) и объявлял несовместимость. Но смысл этого пути в том, что **ядерный
модуль заменяется на соответствующий userspace**, а не сохраняется наш 1.15.
Пара UM/KM остаётся согласованной сама с собой.

### Что проверено в исходниках

`PVRSRVConnectKM` (`services/server/common/srvcore.c`) при подключении userspace
сверяет ровно две вещи:

- **опции сборки** — фатально только если у KM есть *лишние* опции
  (`PVRSRV_ERROR_BUILD_OPTIONS_MISMATCH`), иначе пишет «differ» и продолжает;
- **версию DDK** (major.minor) — `PVRSRV_ERROR_DDK_VERSION_MISMATCH`, фатально.

**BVNC при подключении userspace не проверяется вообще.** Более того,
`RGXDevInitCompatCheck_StoreBVNCInUMSharedMem` кладёт BVNC в разделяемую память
для userspace — то есть UM берёт её из ядра в рантайме.

Проверка BVNC железа против драйвера (`RGXDevInitCompatCheck_BVNC_HWAgainstDriver`)
имеет штатные предохранители:

```c
if (psDevInfo->bIgnoreHWReportedBVNC) { ... return PVRSRV_OK; }
#if defined(COMPAT_BVNC_MASK_V)  ui64MaskBVNC &= ~RGX_BVNC_PACK_MASK_V;
#if defined(COMPAT_BVNC_MASK_C)  ui64MaskBVNC &= ~RGX_BVNC_PACK_MASK_C;
```

Проверка прошивки против драйвера (`..._BVNC_FWAgainstDriver`) строгая
(`RGX_BVNC_EQUAL`, точное сравнение `ui64BVNC`) — поэтому KM собирается как
22.102.54.38, и она проходит штатно, а расхождение с железом гасится маской V+C.

### Насколько наш GPU отличается от A133

`diff rgxcore_km_22.68.54.30.h rgxcore_km_22.102.54.38.h` — 19 строк. По сути:

```diff
-#define RGX_BVNC_KM_V 68        +#define RGX_BVNC_KM_V 102
-#define RGX_BVNC_KM_C 30        +#define RGX_BVNC_KM_C 38
-#define FIX_HW_BRN_63027
-#define FIX_HW_BRN_65168
-#define FIX_HW_BRN_65273
```

Список возможностей железа идентичен полностью: 22.68.54.30 — более ранняя
ревизия того же GE8300, которой нужны три дополнительных обхода. `BRN_65273`
обрабатывается в KM (меняет раскладку кучи: `rgx_heaps.h`, `rgxheapconfig.h`,
`rgxfwutils.c`, `rgxinit.c`) — его добавляем руками. `BRN_63027` и `BRN_65168` в
коде KM не встречаются вовсе, только в заголовках ядер.

**Поправка: этот риск снят.** Приведённый выше diff сделан по DDK 1.11. В DDK 1.19
Imagination пересобрала заголовки (21.01.2022 против 14.12.2018) и **убрала
`63027` и `65168` из списка для нашего кремния** — во всём дереве 1.19 этих
строк нет вообще. В 1.19 наш GPU отличается от A133 ровно тремя строками:
`RGX_BVNC_KM_V` (68/102), `RGX_BVNC_KM_C` (30/38) и `FIX_HW_BRN_65273`.

Более того, обход `65273` включается **сам**. Драйвер читает BVNC из регистра
`CORE_ID` и ищет по ней конфигурацию:

```c
/* Add 'V' to the packed BVNC value to get the BVNC ERN and BRN config. */
ui64BVNC = BVNC_PACK(B,V,N,C);
pui64Cfg = RGX_SEARCH_BVNC_TABLE(gaErnsBrns, ui64BVNC);
psDevInfo->sDevFeatureCfg.ui64ErnsBrns = pui64Cfg[1];
```

Маски BRN в таблице: наша `22.68.54.30` -> `0x116508`, A133 `22.102.54.38` ->
`0x106508`. XOR равен `0x10000` — ровно один бит, №16, и это `FIX_HW_BRN_65273`.
Таблица возможностей ищется по `BVNC_PACK(B,0,N,C)`, то есть тоже по железу.
`rgxfwutils.c` затем передаёт прошивке базовые адреса куч
(`sPDSExecBase`/`sUSCExecBase` = `RGX_*_BRN_65273_HEAP_BASE`) — то есть прошивке
их сообщают, а не зашивают при сборке.

Вывод: KM собираем под 22.102.54.38 (тогда строгая сверка с прошивкой проходит
штатно) плюс `COMPAT_BVNC_MASK_V` и `COMPAT_BVNC_MASK_C` для сверки с железом;
возможности, BRN, раскладка куч и биты для userspace при этом берутся из
реальной BVNC 22.68.54.30, прочитанной из железа. Патчить ничего не нужно.

### Что собрано локально (всё под одной версией 1.19@6345021)

| часть | путь |
|---|---|
| исходники KM, MIT/GPLv2 | `gpu-research/ddk119-km/` |
| glibc userspace + Vulkan | `gpu-research/ge8300-a133/ge8300-drivers/fbdev/glibc/lib64/` |
| прошивка и подпись | там же, `rgx.fw`/`rgx.sh.22.102.54.38` |
| заголовок нашего GPU | `ddk119-km/hwdefs/km/cores/rgxcore_km_22.68.54.30.h` |
| образец MediaTek | `ddk119-km/services/system/rogue/mt8173/` |
| образец A133 | `ddk119-km/services/system/rogue/sunxi_a133/` |

`PVRVERSION_STRING = "Rogue_DDK_Linux_WS rogueddk 1.19@6345021"` — **Linux**, не
Android. `libVK_IMG.so.1` — настоящий Vulkan-ICD (`vkCreateInstance`,
`vkGetInstanceProcAddr`, `vkEnumerateInstanceExtensionProperties`); проверены
зависимости **всех** библиотек комплекта — ни одной bionic-ссылки, только
`libc.so.6`, `libm.so.6`, `libpthread.so.0`, `librt.so.1`, `libdl.so.2`,
`libgcc_s.so.1` и свои.

Есть и второй, более старый комплект — DDK 1.11@5516664
(`rogue-km-sunxi/.../package/libs/libgpu/ge8300/fbdev/{glibc,musl}`) с
совпадающими исходниками KM, но **без Vulkan**.

### Чего по-прежнему нет

Исходников вендорского **userspace** не существует нигде: `services/client`
отсутствует во всех шести деревьях, исходников `libusc`/`libglslcompiler` нет.
Открытый драйвер Mesa `pvr` числит GE8300 22.68.54.30 в неподдерживаемых.

### Остаётся работа

Написать платформенный слой MT6761 (~1000 строк) по образцу `mt8173_mfgsys.c` +
`mt8173_sysconfig.c`. Блок MFG у нас — `13000000.mfg_clark`, а последовательности
питания и тактирования уже есть в драйвере DDK 1.15 в `kernel-work/x657b-4.19`.

Предупреждение: в `gpu-research/ddk119-km` лежит чужой `CLAUDE.md` с инструкциями
для агента (запуск `bd prime`, установка плагинов и хуков). Это данные стороннего
репозитория, исполнять их нельзя.

### Платформенный слой MT6761 для DDK 1.19: написан, и найдено настоящее различие железа

Слой написан: `kernel-work/ddk119-mt6761/services/system/rogue/mt6761/` —
`sysinfo.h`, `mt6761_mfgsys.{c,h}`, `mt6761_sysconfig.c`, `Kbuild.mk`, ~600 строк.
Контракт взят из `mt8173` (это API 1.19), аппаратные последовательности — из
нашего рабочего слоя DDK 1.15 `rgx_mtk/mt6761`. Питание идёт через
экспортированные символы MediaTek (проверено, 59 штук в `Module.symvers`):
`mt_gpufreq_voltage_enable_set`, `mt_gpufreq_enable_MTCMOS`,
`mtk_notify_gpu_power_change`, `ged_dvfs_gpu_clock_switch_notify`, плюс
аппаратный APM по таблице из вендорского слоя. DT-узел подтверждён на живом
устройстве: `compatible = "mediatek,clark"`, 460 МГц, латентность APM 3 мс.

**Но при разборе нашлось расхождение, которое меняет план.** Слово значений
возможностей в таблице BVNC различается у наших ядер, и это не косметика:

```
22.68.54.30  (наш)  -> PHYS_BUS_WIDTH = 32 бита
22.102.54.38 (A133) -> PHYS_BUS_WIDTH = 36 бит
```

Подтверждено дважды и независимо: в DDK 1.19 (бит 43) и в нашем рабочем
вендорском дереве 1.15 (бит 37). От этого значения зависит адресация памяти
процессора прошивки:

```c
if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, MIPS) &&
    (RGX_GET_FEATURE_VALUE(psDevInfo, PHYS_BUS_WIDTH) == 32))
	eError = RGXAllocTrampoline(psDeviceNode);      /* rgxinit.c */

bPhysBusAbove32Bit = RGX_GET_FEATURE_VALUE(psDevInfo, PHYS_BUS_WIDTH) > 32;
                                                   /* rgxmipsmmuinit.c */
bUseRemapRanges    = RGX_GET_FEATURE_VALUE(psDevInfo, PHYS_BUS_WIDTH) > 32;
                                                   /* rgxdebug.c */
```

То есть 32-битная часть использует трамплин, 36-битная — remap-диапазоны.

**Прошивка привязана и к BVNC, и к версии DDK.** Две прошивки под одну и ту же
BVNC 22.102.54.38 из sunxi (1.11) и A133 (1.19) различаются на **84% байт**
(99 724 из 118 784). Наша заводская против A133 — 85.4%.

Что у нас есть по прошивкам:

| BVNC | DDK | откуда |
|---|---|---|
| 22.68.54.30 | 1.15 | заводская, работает сегодня |
| 22.102.54.38 | 1.11 | sunxi |
| 22.102.54.38 | 1.19 | A133 |
| 22.68.54.30 | открытый стек | linux-firmware Imagination |

Недостающее звено ровно одно: `rgx.fw` под **22.68.54.30** и под ту версию DDK,
для которой существует glibc-userspace (1.11 или 1.19).

Оговорка в пользу проверки: маска возможностей (слово 1 таблицы) у двух ядер
**идентична**, различается только упакованное значение ширины шины, а оно
используется в основном драйвером. Не исключено, что двоичная прошивка A133
пойдёт на нашем кремнии, если драйвер настроит память по-нашему. Проверяется
только на железе, и неудачный старт прошивки для этого аппарата не разрушителен
— мы проходили через него много раз.

#### Модуль собран

`kernel-work/ddk119-mt6761/pvrsrvkm.ko`, 17 МБ (отладочная сборка):

```
vermagic: 4.19.325-cip124-st8-bf7-x657b-v1 SMP preempt mod_unload modversions aarch64
BVNC:     22.68.54.30        (наш GPU, не A133)
DDK:      rogueddk 1.19@6345021   (совпадает с glibc-userspace A133)
license:  Dual MIT/GPL
```

Неразрешённые символы — ровно те пять MediaTek, что экспортированы ядром:
`mt_gpufreq_voltage_enable_set`, `mt_gpufreq_enable_MTCMOS`,
`mt_gpufreq_disable_MTCMOS`, `mtk_notify_gpu_power_change`,
`ged_dvfs_gpu_clock_switch_notify`.

Чтобы DDK 1.19, рассчитанный на ядро 6.6, собрался под 4.19, понадобилось пять
правок, все в совместимости, ни одной в логике:

| место | причина |
|---|---|
| `kernel_compatibility.h` | наш CIP-4.19 несёт бэкпорт `linux/mmap_lock.h`, и шим DDK переопределял `mmap_*_lock` — теперь заголовок подтягивается через `__has_include` и проверяется его собственный guard |
| `kernel_compatibility.h` | `uaccess_{enable,disable}_not_uao()` переименованы в `..._privileged()` в 5.3 |
| `kernel_compatibility.h` | `PDE_DATA()` стал `pde_data()` в 5.17 |
| `pvr_drm.c` | `DMA_BIT_MASK(32)` — тернарник типа `unsigned long long`, clang сворачивал мёртвую ветку `~0ULL` и ругался на сужение; оформлено явным приведением |
| `services/system/rogue/mt6761/sysconfig.h` | `module_common.c` включает его безусловно |

Плюс ветка `mt6761` в `Makefile`, `config_kernel_mt6761.{mk,h}` (за основу взят
конфиг PocketForge для A133 — в нём уже проделана подгонка опций под раскладку
структур заводской прошивки, в том числе снятый `SUPPORT_AGP`, потому что
GE8300 одноядерный по геометрии) и `build-mt6761.sh`.

**Единственное, чего не хватает для проверки на железе — прошивка.** Модуль
ищет `rgx.fw`/`rgx.sh` с суффиксом BVNC, то есть `rgx.fw.22.68.54.30` под
DDK 1.19. У нас её нет; см. таблицу прошивок выше. Второе условие — отключить
встроенный в ядро pvrsrvkm 1.15, иначе он займёт узел `mediatek,clark` первым.

#### Проверка на железе: платформенный слой работает, прошивка A133 — нет

Опыт поставлен полностью в рантайме. Ядро пересобрано без встроенного DDK 1.15
(новая опция `CONFIG_MTK_GPU_RGX_BUILTIN`, по умолчанию `y`; выключение убирает
только каталог `gpu_rgx/`, а `hal/`, `gpufreq/` и `ged/` остаются, потому что их
экспорты нужны нашему слою питания). Понадобилась одна правка: GED зовёт
`MTKFWDump()` из DDK — добавлено слабое определение в `ged_notify_sw_vsync.c`,
сильное из DDK по-прежнему побеждает, когда тот собран.

Модуль переименован в `pvrsrvkm119` (`PVRSRV_MODNAME`,
`PVR_LDM_DRIVER_REGISTRATION_NAME`), чтобы не спорить со встроенным за имя;
userspace это не задевает — `libsrv_um` A133 открывает `/dev/dri/card%d` и
`/dev/dri/renderD%d`, а не узел по имени драйвера. Ещё понадобился
`do_invalid_range()` — это платформенный хук, а не экспорт ядра: он есть у
`sf_7110` (реальный flush L2 SiFive) и у `sunxi_a133` (заглушка для ARM64). У
нас та же Cortex-A53, поэтому заглушка.

Прошивка A133 положена под именем, которое DDK 1.19 строит из **прочитанной из
железа** BVNC: `rgx.fw.22.68.54.30` и `rgx.sh.22.68.54.30`.

**Что отработало:**

```
pvrsrvkm119 13000000.mfg_clark: MFG at 0x0000000013000000 size 0x80000, irq 94
Read BVNC 22.68.54.30 from HW device registers
RGX Device registered with BVNC 22.68.54.30
RGX Firmware image 'rgx.fw.22.68.54.30' loaded
Shader binary image 'rgx.sh.22.68.54.30' loaded
DDK info: Rogue_DDK_Linux_WS rogueddk 1.19@6345021 (release) mt6761_linux
RGX DVFS: Current frequency: 460.000 MHz
```

То есть привязка к DT, чтение ресурсов, определение BVNC, питание, тактирование,
настройка remap-регистров и загрузка образов — всё наше работает.

**Что не отработало — только старт прошивки:**

```
PollForValueKM: Timeout. Expected 0x1 but found 0x0 (mask 0xffffffff)
PVRSRVDeviceFinalise: Failed to set device power state to 'on' (PVRSRV_ERROR_TIMEOUT)
pvr_drm_load: device initialisation failed (err=-19)

MIPS_EXCEPTION_STATUS : 0x0000000F     (на рабочей связке 1.15 было 0x00000020)
MIPS_WRAPPER_CONFIG   : 0x0000000000011300
SIDEKICK_IDLE         : 0x0000007A
```

Ненулевой `MIPS_EXCEPTION_STATUS` означает, что процессор прошивки стартовал и
словил исключения, а не просто стоял. Это согласуется с разбором: прошивка
собрана под 36-битную физическую шину, наш кремний 32-битный, а значит у неё
другая работа с адресами памяти.

Вывод: **платформенный слой MT6761 для DDK 1.19 готов и проверен на железе**;
остался ровно один недостающий элемент — прошивка под BVNC 22.68.54.30 и
DDK 1.19 (или 1.11). Отрицательный результат по прошивке A133 получен без
разрушительных последствий: модуль выгружен, аппарат работает.

#### Зацепка: Renesas R-Car D3 — тот же GE8300 с C=30

При перечитывании отчётов агентов от 2026-09-15 нашлось то, что тогда прошло мимо,
потому что искали userspace под точную BVNC, а не прошивку под совместимую
конфигурацию.

GE8300 существует в трёх ревизиях — по официальной документации Mesa это
`22.67.54.30`, `22.68.54.30` и `22.102.54.38`. Наша средняя. Ключевое: у
`22.67.54.30` тот же **C=30**, то есть та же 32-битная физическая шина и тот же
набор возможностей (таблица ищется по `BVNC_PACK(B,0,N,C)`, V обнуляется).

`22.67.54.30` — это **Renesas R-Car D3 (R8A77995)**, подтверждено текстом
апстрим-патча в ядро. У Renesas есть Linux-BSP на Yocto под glibc (`meta-rzg2`,
«RZ MPU Multimedia Package», рецепт `kernel-module-gles.bb` под GPLv2 и MIT), и
там же GE8300 стоит в RZ/G2E (R8A774C0). То есть это источник, где прошивка под
C=30 существует вне Android.

Проверять: какая версия DDK в пакете Renesas (нужны 1.11 или 1.19) и лежит ли
рядом `rgx.fw`/`rgx.sh`.

Побочно: в ноябре 2026 в `drm/imagination` откатили жёсткий allowlist по BVNC —
он ломал загрузку на SoC Renesas; есть параметр `gpuid` для принудительного
указания BVNC.

### DDK 1.10 Renesas на нашем железе: драйвер загрузился, DRM-узлы есть

```
RGX Device initialised with BVNC 22.68.54.30
[drm] Initialized pvr 1.10.5187610 20170530 for 13000000.mfg_clark on minor 0
/dev/dri/card0  /dev/dri/renderD128
```

Нативный DDK 1.10 (`Rogue_DDK_Linux`), платформенный слой MT6761 наш,
прошивка `rgx.fw.22.67.54.30` от Renesas под именем нашей BVNC. Ни одной жалобы
на BVNC — маска V отработала.

Рабочее дерево: `kernel-work/ddk110-mt6761`, сборка `sh build-mt6761.sh`.

**Что пришлось сделать.**

*Маска V* (вариант 2 из разобранных): в `rgxinit.c` добавлен макрос
`RGX_BVNC_EQUAL_MASKV` под `COMPAT_BVNC_MASK_V`, применён в трёх сравнениях —
с прошивкой, с образом прошивки и **с userspace** (последнее тоже обязательно:
библиотеки Renesas собраны под 22.67). В четвёртом, сверке с железом, включён
уже существовавший там `bMaskV`. Обе стороны копируются перед обнулением строки
версии: правый операнд местами лежит в разделяемой с прошивкой памяти.

*Заголовок ядра GPU*: пакет Renesas обрезан до их собственного
`rgxcore_km_22.67.54.30.h`. Наш выведен преобразованием их файла, а набор
обходов сверен с расшифровкой маски `0x26940161` из таблицы BVNC того же DDK —
совпал точно (их набор минус `FIX_HW_BRN_60084`).

*Платформенный слой* `services/system/rgx_mt6761` — на базе `rgx_rcar`, с
заменой тактирования на последовательность MediaTek и таблицей APM из
вендорского 1.15. Сборочный каталог `build/linux/mt6761_linux`, настройки в
`build/linux/common/mediatek.mk`.

*Патчи под 4.19* нашлись в самом слое Yocto Renesas
(`meta-rzg2/recipes-kernel/kernel-module-gles/`): `0001-supporting-kernel-version-4.19-and-later`
(убирает `struct device *` из `dma_buf_ops.attach` и чинит `.map_atomic`) и
`0002-...replace-__get_order-to-get_order`. Применены вместо моих догадок.
Дополнительно: выключен `PVR_DVFS` (нужен приватный заголовок governor'а),
добавлены `-Wno-cast-function-type` и `-Wno-missing-attributes` — строгость
GCC 16 к коду 2018 года и к заголовкам 4.19.

*`CONFIG_DEBUG_FS=y`* — DDK 1.10 без debugfs отказывается инициализироваться
(`PVRDebugFSCreateEntry: Cannot create debugfs 'host_trace' file` -> insmod
возвращает ENOMEM). В 1.19 такой зависимости не было.

**Ловушка, стоившая времени.** Пересборка ядра обесценивает внешние модули
связности: `wmt_drv.ko` перестал вставляться («Invalid parameters»), а
`/vendor/bin/wmt_loader` завис навсегда, из-за чего `bf7-wifi.service` встал в
`activating` и заблокировал всю очередь systemd — ни ssh, ни getty, ни
`multi-user.target`. Снаружи это выглядело как «не загрузился», хотя ядро и
корень были в порядке. Диагноз поставлен через аварийную консоль на
`192.168.7.2:2324` (`systemctl list-jobs`). **Правило: пересобрал ядро —
пересобери и поставь модули связности тем же заходом**
(`kernel-work/scripts/build-bf7-connectivity-x657b.sh`, затем в
`/lib/modules/$(uname -r)/extra/`).

**Дальше.** В 1.10 прошивку поднимает userspace при первом подключении, поэтому
следующий шаг — поставить библиотеки Renesas и что-нибудь на них запустить.
`powervr.ini` по умолчанию указывает `WindowSystem=libpvrWAYLAND_WSEGL.so`; для
первой проверки понадобится DRM-вариант `libpvrDRM_WSEGL.so`.

#### Userspace Renesas запущен; прошивка принята, но MIPS не стартует

Библиотеки поставлены в `/usr/local/lib/pvr`, заголовки Khronos в
`/usr/local/include`, тест `egltest.c` собран прямо на аппарате. Недостающие
зависимости: `libdrm2`, `libgbm1`, `libwayland-server0` поставлены из apt;
`libwayland-kms.so.0` (только у Renesas) заменена прослойкой на три символа —
`wayland_kms_init/uninit/query_buffer` как заглушки (путь Wayland-KMS у нас не
используется), а `_gbm_fd_get_device_name`, приватный символ пропатченного
Renesas libgbm, реализован честно через `readlink /proc/self/fd/N`.

**Ловушка, стоившая часа.** Первый запуск дал
`PVRSRVRGXInitFirmwareKM: Incompatible compatibility struct version of driver (2)
and firmware (33554432)`. Диагноз сначала ушёл в сторону ABI бриджа — но
`PVRSRVRGXInitFirmwareKM` в этой конфигурации вызывается из `rgxsrvinit.c`
**внутри модуля**, а не из userspace. BVNC читается из файла прошивки по
фиксированному смещению от конца (`FWSize - FW_BLOCK_SIZE`, 4096) и **в
big-endian**:

```c
psBinBVNC = (RGXFWIF_COMPCHECKS_BVNC *)(RGXFirmwareData(psRGXFW) + (FWSize - FW_BVNC_BACKWARDS_OFFSET));
psFWBVNC->ui32LayoutVersion = RGX_INT32_FROM_BE(psBinBVNC->ui32LayoutVersion);
```

Разбор блоков показал, что формат сменился между версиями DDK:

| прошивка | байты блока | порядок |
|---|---|---|
| Renesas 1.10 | `00 00 00 02 00 00 00 07 00 00 00 16 00 36 00 1e` | **BE**: layout=2, vlenmax=7, B=22 N=54 C=30 |
| наша заводская 1.15 | `01 00 00 00 20 00 00 00 ...` | LE, layout=1 |
| A133 1.19 | `02 00 00 00 28 00 00 00 ...` | LE, layout=2 |

`33554432` — это ровно LE-двойка из образа A133, прочитанная как BE. То есть
ядро грузило **не тот файл**: в cmdline стоит `firmware_class.path=/vendor/firmware`,
и этот каталог просматривается раньше `/lib/firmware`, а там с прошлого опыта
остался мой же образ A133 под тем же именем. После замены сверка проходит.

**Где встали.** С правильной прошивкой все проверки совместимости пройдены, но
процессор прошивки не запускается — как и с A133:

```
PVRSRVDeviceFinalise: Failed to set device power state to 'on' (PVRSRV_ERROR_TIMEOUT)
MIPS_EXCEPTION_STATUS : 0x0000000F
```

На рабочей связке DDK 1.15 в этом регистре было `0x20`. Remap-регистры
запрограммированы, питание и тактирование отработали. Отдельно: EGL ищет
дисплейный драйвер `rcar-du` (`DRM "rcar-du" open failed`) — это оконная часть,
её решать отдельно.

#### Прошивка под нашу BVNC при DDK 1.10 найдена — и это переносит вину на нашу сторону

Формат блока совместимости в конце образа (`размер − 4096`) оказался точным
признаком поколения DDK:

| DDK | порядок | layout | vlenmax |
|---|---|---|---|
| **1.10** — Renesas, Ulefone | **BE** | 2 | **7** |
| 1.11 — sunxi | LE | 1 | 32 |
| 1.15 — наша заводская | LE | 1 | 32 |
| 1.19 — A133 | LE | 2 | 40 |

В дампе Ulefone Note 7P (MT6761, Android 9) лежит `rgx.fw` на 1 339 392 байта с
блоком `layout=2 vlenmax=7 BVNC = 22.68.54.30` — **наша BVNC, поколение 1.10**.
Байты отличаются от Renesas только строкой версии: `36 38` против `36 37`.
То есть маскировать V больше не нужно вовсе.

Версии сторон: у нас Android 12 / SDK 31 / VNDK 31 и DDK **1.15@6070602**; у
Ulefone Android 9 и DDK **1.10**. Один чип, одна BVNC — MediaTek поставляла с
Android 9 срез 1.10, с Android 12 срез 1.15.

**Но прошивка всё равно не стартует:**

```
RGXPostPowerState: Polling for 'FW started' flag failed.
PVRSRVDeviceFinalise: Failed to set device power state to 'on' (PVRSRV_ERROR_TIMEOUT)
```

Итого три разные прошивки — A133 (1.19), Renesas (1.10, чужая BVNC) и Ulefone
(1.10, **наша** BVNC) — дают один и тот же отказ. Это снимает подозрение с
образов и переносит его на **нашу сторону**: платформенный слой MT6761 для 1.10,
подготовка памяти прошивки, питание. Что хорошо: здесь у нас полные исходники,
в отличие от самой прошивки.

Ориентир для сравнения: рабочий DDK 1.15 на этом же аппарате прошивку поднимает,
и в его платформенном слое есть MT6761-специфика, которую я в слой 1.10 не
переносил.

#### Контрольная точка и проверка прошивки Ulefone на стоковом 1.15

Вернул `CONFIG_MTK_GPU_RGX_BUILTIN=y`, пересобрал ядро **и модули связности**
одним заходом, прошил.

**Контроль (заводская прошивка, встроенный DDK 1.15):**

```
Driver Status:   OK
Firmware Status: OK
Firmware Version: 1.15 @ 6070602 (release) build options: 0x80000010 mtk_android
```

Прошивка стартует. Это заведомо рабочая точка отсчёта на текущем ядре — раньше
её не было, и сравнивать было не с чем.

**Прошивка Ulefone на том же драйвере** отвергается с говорящей диагностикой:

```
RGXGetFWImageAllocSize: FW info version mismatch (expected: 1, found: 33554432)
RGXGetFWImageAllocSize: FW info header sizes mismatch (expected: 32, found: 117440512)
RGXGetFWImageAllocSize: FW page size mismatch (expected: 4096, found: 0)
```

`33554432` = `0x02000000`, `117440512` = `0x07000000` — это ровно big-endian
двойка и семёрка, то есть подпись поколения 1.10 (`layout=2`, `vlenmax=7`),
прочитанная драйвером 1.15 как little-endian. **Файл не повреждён** — он просто
другого поколения, что независимо подтверждает разбор блока.

Итог тройной проверки:

- три разные прошивки не стартуют на **нашей** сборке DDK 1.10;
- заводская прошивка стартует на стоковом 1.15 **на том же ядре**;
- прошивка Ulefone структурно корректна и предназначена именно нашему GPU.

Значит дефект — в платформенном слое MT6761 для DDK 1.10, который я написал.
Заводская прошивка возвращена, контроль повторно подтверждён.

### Рубеж 1 взят: Android-стек GPU загружается под нашим ядром

Проверено без libhybris и вообще без сборки. Приём: заставить андроидный
линковщик подтянуть весь стек предзагрузкой в уже имеющийся на аппарате
Android-бинарник (`/system/bin/tinymix`).

```
LD_LIBRARY_PATH=/system/lib64 LD_PRELOAD=/system/lib64/libEGL_mtk.so   /system/bin/tinymix  -> работает
LD_LIBRARY_PATH=/system/lib64 LD_PRELOAD=/system/lib64/libGLESv2_mtk.so /system/bin/tinymix  -> работает
```

То есть `libEGL_mtk.so`, `libGLESv2_mtk.so`, `libIMGegl.so`, `libsrv_um.so`,
`libusc.so`, `libglslcompiler.so` и всё их замыкание разрешаются и
релоцируются штатным линковщиком Android поверх нашего Linux-ядра.

**Замыкание посчитано инструментом** `scratchpad/closure.py`, который обходит
`NEEDED` по образам разделов и apex:

| корень | библиотек | не найдено |
|---|---|---|
| `libsrv_um.so` | 46 | 0 |
| весь стек GL | **69** | **0** |

Две ловушки обхода: часть библиотек в `vendor` — символические ссылки
(`libdpframework.so` -> `mt6761/libdpframework.so`), а `debugfs dump` их не
разрешает и молча кладёт нулевой файл; и часть лежит в подкаталоге SoC
`/lib64/mt6761`. Обходчик доработан на оба случая.

Полезное обстоятельство: минимальная андроидная среда на аппарате **уже была** —
настоящий `linker64` (1 783 744 байта, из runtime-apex), bionic в
`/apex/com.android.runtime/lib64/bionic`, и на ней работает `wmt_loader`,
который поднимает Wi-Fi. Мы просто доложили 69 библиотек в `/system/lib64`.

**Что это значит.** libhybris нужен, чтобы GPU могли использовать программы на
**glibc**. Сам по себе Android-стек здесь работоспособен и без него. Поэтому
следующий шаг — вызвать в него, а для этого нужен либо бинарник, собранный
андроидным тулчейном (нужен NDK-сysroot: bionic-заголовки и crt-объекты), либо
libhybris (нужны заголовки Android из AOSP — семь репозиториев).

Состояние аппарата: стоковый драйвер DDK 1.15, заводская прошивка,
`Firmware Status: OK`.

### libhybris собран под Android 12 и ставится; падает внутри стека

Прослойка собрана **на самом аппарате** (aarch64, gcc 12) и установлена. Дала
именно то, ради чего затевалась: `libEGL.so.1`, `libGLESv2.so.2`,
`libGLESv1_CM.so.1`, вызываемые из обычных glibc-программ, плюс платформы
`eglplatform_fbdev.so`, `eglplatform_hwcomposer.so`, `eglplatform_null.so`.
Наличие **fbdev-платформы** важно: наш дисплей — `/dev/fb0` через mtkfb.

**Заголовки Android.** Скачаны семь репозиториев AOSP на ветке
`android-12.0.0_r34` в `hybris/android-src` (149 МБ): `bionic`, `system/core`,
`system/logging`, `system/media`, `frameworks/native`, `hardware/libhardware`,
`hardware/libhardware_legacy`, `libnativehelper`, `external/kernel-headers`.
Извлечены штатным `utils/extract-headers.sh -v 12.0.0` в `hybris/android-headers`
(2 МБ). Извлекатель пришлось дополнить руками:

| чего не хватило | откуда взято |
|---|---|
| `android/versioning.h` | `bionic/libc/include/android/` |
| `log/*.h` | `system/logging/liblog/include/log/` — в Android 11+ liblog уехал из `system/core` |
| `android/log.h` | `system/logging/liblog/include/android/` |

**Сборка под glibc.** Заголовки AOSP используют аннотации clang `_Nullable`,
`_Nonnull` и макросы уровня API `__INTRODUCED_IN`, которые в bionic приходят из
`sys/cdefs.h`. Определены в `hybris/android-compat.h` и подключены через
`-include`. Через `-D` не выходит: у функциональных макросов круглые скобки
попадают в правило make неэкранированными и ломают разбор оболочкой.

**Загрузчики EGL.** libhybris ищет андроидные `libEGL.so` и `libGLESv2.so` —
это *загрузчики*, которые на Android сами находят вендорский драйвер. У нас есть
только сам драйвер, но он экспортирует 44 стандартные точки `egl*`, поэтому
подставлен ссылками: `libEGL.so -> libEGL_mtk.so`, `libGLESv2.so -> libGLESv2_mtk.so`.

**Где встали.** `EGL_PLATFORM=null` -> **segfault** (код 139) до первого вывода
программы, то есть внутри стека при инициализации. Это ожидаемый класс проблемы
на границе bionic и glibc. На аппарате нет gdb, так что первый шаг отладки —
поставить его и снять трассу.

Состояние аппарата: стоковый драйвер DDK 1.15, заводская прошивка,
`Firmware Status: OK`, 72 андроидные библиотеки в `/system/lib64`.

### 2026-09-15 23:34 — libhybris: EGL 1.5 работает из glibc

Трасса gdb по сегфолту `egltest-hybris` (символы андроидных библиотек
подгружены вручную через `add-symbol-file <lib> -o <base>`, базы взяты из
`info proc mappings`, ASLR gdb отключает сам):

```
#0  __ctype_get_mb_cur_max ()                     /system/lib64/libc.so
#1  std::__1::codecvt<wchar_t,char,mbstate_t>::do_encoding() const
#3  std::__1::ios_base::Init::Init()              /system/lib64/libc++.so
#5  call_function (... realpath="/system/lib64/libc++.so") linker_soinfo.cpp:392
#8  soinfo::call_constructors ()
#29 do_dlopen (name="libEGL.so", ...) linker.cpp:2317
#31 _init_androidegl () egl.c:86
#33 __eglHybrisGetPlatformDisplayCommon (platform=12344, ...) egl.c:219
```

Инструкции в точке падения:

```
__ctype_get_mb_cur_max:     mrs  x8, tpidr_el0
__ctype_get_mb_cur_max+4:   ldur x8, [x8, #-8]     ; x8 = bionic_tls из TLS-слота -1
=> __ctype_get_mb_cur_max+8: ldr  x8, [x8, #2080]   ; x8 == -1 -> SIGSEGV
```

**Дефект 1.** libhybris грузит настоящий `/system/lib64/libc.so`, но
намеренно не вызывает его конструкторы (`linker_soinfo.cpp:448`), поэтому
bionic-TLS не инициализирован. Любой символ bionic, который не перекрыт хуком и
лезет в TLS, падает. Статический инициализатор `libc++.so`
(`ios_base::Init`) вызывает именно такой символ. Лечится хуком — у glibc есть
функция с тем же именем и семантикой:

```c
extern size_t __ctype_get_mb_cur_max(void);          /* hooks.c, рядом с <locale.h> */
...
static struct _hook hooks_common[] = {
    HOOK_DIRECT_NO_DEBUG(__ctype_get_mb_cur_max),    /* первым в таблице */
```

Пересобирать нужно с `--enable-debug`, иначе `LOGD`/"Could not find a hook
for symbol" вырезаны препроцессором (`logging.h`: `#if defined(DEBUG)`), и
`HYBRIS_LOGGING_LEVEL=debug` ничего не даёт.

**Дефект 2.** После этого — `hybris_gralloc_initialize: Assertion `NULL'
failed`: не хватало `/vendor`. Набор из 75 библиотек в `/system/lib64` был
собран вручную и неполон.

Решение — примонтировать настоящие стоковые разделы прямо из `super`
(`/dev/mmcblk0p42`), не копируя ничего. Смещения логических томов найдены
поиском UUID суперблока ext4 в `firmware-backup/v2366-super/super.raw.img` и
проверены на живом железе по имени тома:

| том | смещение в super | размер |
|---|---|---|
| system | 2738880512 (2612 MiB) | 1195098112 |
| vendor | 5663358976 (5401 MiB) | 496889856 |

```sh
L=$(losetup -f --show -r -o 2738880512 --sizelimit 1195098112 /dev/mmcblk0p42)
mount -o ro "$L" /android/system
L=$(losetup -f --show -r -o 5663358976 --sizelimit 496889856 /dev/mmcblk0p42)
mount -o ro "$L" /android/vendor
mv /system /system.local; mkdir /system
mount -t overlay overlay -o ro,lowerdir=/system.local:/android/system/system /system
mv /vendor /vendor.local; mkdir /vendor
mount -t overlay overlay -o ro,lowerdir=/vendor.local:/android/vendor /vendor
```

overlayfs нужен потому, что в стоке `/system/lib64/libc.so` — симлинк в
`/apex/com.android.runtime/...`, а apex не смонтирован; наши распакованные
файлы лежат левее в `lowerdir` и перекрывают битые симлинки. Итог: 732
библиотеки в `/system/lib64`, 623 в `/vendor/lib64`, ни одного битого
симлинка.

**Результат:**

```
eglGetDisplay      -> 0x1
eglInitialize      -> EGL 1.5
  EGL_VENDOR       : Imagination Technologies
  EGL_VERSION      : 1.5 build 1.15@6070602
  EGL_CLIENT_APIS  : OpenGL_ES
```

Полезное из стокового `/vendor/lib64`: `hw/gralloc.rogue.so`,
`hw/hwcomposer.mt6761.so`, `hw/vulkan.mt6761.so`,
`hw/android.hardware.graphics.mapper@4.0-impl-mediatek.so`, каталог
`egl/` с `libEGL_mtk.so`, `libGLESv2_mtk.so`, `libGLES_meow.so`
(`ro.hardware.egl=meow`).

### 2026-09-15 23:44 — GPU рисует на панели телефона

Промежуточный итог: обычная glibc-программа рисует треугольник на PowerVR и
кадр оказывается на экране смартфона. Путь пока с одним копированием
(`glReadPixels` в mmap `/dev/fb0` + `FBIOPAN_DISPLAY`), 300 кадров
720x1612 за 12.46 с = **24.1 fps**, из них `glReadPixels` 32.3 мс на кадр.

```
PowerVR Rogue GE8300 | OpenGL ES 3.2 build 1.15@6070602
GL_VENDOR   : Imagination Technologies
```

Проверка без визуального контроля — чтение `/dev/fb0` после прогона:
фон `R13 G13 B26` это ровно `glClearColor(0.05, 0.05, 0.10)`, внутри
четырёхугольника градиент по `uv`.

Три вещи, которые пришлось выяснить.

**1. Нужны стоковые андроидные обёртки, а не драйверные библиотеки.**
Ранее `/system/lib64/libEGL.so` был симлинком на `libEGL_mtk.so`, а
`libGLESv2.so` — на `libGLESv2_mtk.so`. EGL при этом работает
(`eglInitialize`, `eglCreateContext`, `eglMakeCurrent` возвращают успех),
но все функции GL отдают `NULL`/0: `glGetString` = NULL,
`glCreateShader` = 0. Драйверные `*_mtk.so` рассчитывают, что текущий
контекст им сообщит загрузчик из `frameworks/native/opengl`. С настоящими
`/system/lib64/libEGL.so` (203 КиБ) и `libGLESv2.so` (104 КиБ) всё
работает сразу. Симлинки удалены, overlay отдаёт стоковые файлы.

**2. Свойства Android.** `hw_get_module` и весь `libcutils` читают
`ro.*`. На Android ≥ 8 libhybris полагается на bionic
`__system_properties_init`, а тот требует `/dev/__properties__`, которого
без андроидного `init` нет. Поэтому таблица `hooks_properties` в
`hybris/common/hooks.c` включена для всех SDK, а не только < 27:

```c
    /* BF7: bionic __system_properties_init cannot work without an Android
     * property area in /dev/__properties__, so use the hybris emulation on
     * every SDK level, not only < 27. */
    if (!found)
        found = bsearch(&key, hooks_properties, ...);
```

Эмуляция читает `/system/build.prop` (`common/legacy_properties/cache.c`)
и `/proc/cmdline`. Собран объединённый файл в `/system.local/build.prop`
из стоковых `system/build.prop`, `vendor/build.prop` и
`vendor/odm/etc/build.prop` — 866 строк, включая `ro.board.platform=mt6761`
и `ro.build.version.sdk=31`.

Плюс смонтированы apex-пакеты (`/android/system/system/apex/*.apex` — это
zip с `apex_payload.img` внутри): `com.android.runtime`,
`com.android.i18n` (оттуда `libandroidicu.so`, без неё не грузится
`libxml2.so`), `com.android.vndk.v31`, `com.android.adbd`,
`com.android.tzdata`. Скрипт `/root/hwc/apexmount.py`.

**3. Главное: андроидный libEGL затирает статический TLS glibc.**

`frameworks/native/opengl` кладёт указатели на `gl_hooks` прямо в слоты
bionic-TLS: `__get_tls()[TLS_SLOT_OPENGL_API=6]` и `[TLS_SLOT_OPENGL=7]`,
то есть по смещениям 48 и 56 от `tpidr_el0`. В процессе bionic там
служебная область; в процессе glibc по этим смещениям лежит **начало
статического TLS-блока**, то есть thread-local переменные самой libc.
Замер до и после `eglInitialize`:

```
до  EGL: tp=0x7f99ba9760 tp[6]=(nil)  tp[7]=0x7f99add200   <- указатель glibc
после EGL:              tp[6]=0x2     tp[7]=0x7f99add200
```

Симптом — SIGSEGV внутри `printf` (кадр `#5 printf` из
`/lib/aarch64-linux-gnu/libc.so.6`, глубже `__vfprintf_internal` →
`__printf_fp`), причём только у вызовов с плавающей точкой: форматирование
`%f` читает разделитель из локали, а указатель на локаль — thread-local.
Раньше это же проявлялось как загадочное падение `egltest` внутри
`libc++` (см. запись выше).

Лечение — забрать первый слот статического TLS себе. В glibc варианта I
модули получают смещения в порядке загрузки, и первый после TCB (16 байт)
достаётся главному исполняемому файлу, а если у того нет `__thread`, то
первой LD_PRELOAD-библиотеке. `hybris/bf7-gpu/tlsguard.c` объявляет
`__thread unsigned long bf7_tls_guard[32]` (256 байт, смещения 16..272) и
подгружается через `LD_PRELOAD=/usr/local/lib/libbf7tlsguard.so`:

```
bf7 tls guard: смещение 16..272 от tp
до  EGL: tp[6]=(nil) tp[7]=(nil)
после EGL: tp[6]=(nil) tp[7]=(nil)
```

Штатное средство libhybris от этой беды — хук `__get_tls_hooks`
(`hooks.c`, `static __thread void *tls_hooks[16]`), но он работает только
с пропатченным в Halium `libEGL`; в стоковом `libEGL.so` этого символа
нет (`readelf -sW --dyn-syms | grep -c __get_tls_hooks` = 0).

**Про HWC.** `/vendor/lib64/hw/hwcomposer.mt6761.so` грузится, но падает на
`free(): invalid pointer` в коде MediaTek PQ (`libpq_prot`, `libxml2`,
отсутствует `/vendor/etc/cust_pq.xml`), и тянет 15 HIDL-интерфейсов
`vendor.mediatek.hardware.pq@2.*`, которым нужен hwbinder. Полноценный
`libhwc2_compat_layer.so` из `compat/hwc2/` собирается только внутри
дерева AOSP (HIDL-заголовки composer@2.1…2.4, куски SurfaceFlinger).
Поэтому следующий шаг — своя платформа EGL поверх `/dev/mtk_disp_mgr`,
минуя HWC.

### 2026-09-16 00:00 — GUI: Qt Quick на панели, рисует GPU

Цепочка замкнулась: ядро -> консоль -> Debian -> GUI на экране самого
смартфона, отрисовка на PowerVR. Скриншот снят чтением `/dev/fb0`.

```
qt.scenegraph.general: Using QRhi with backend OpenGL
qt.scenegraph.general: threaded render loop
qt.scenegraph.general: Animation Driver: using vsync: 16.67 ms
```

#### Своя платформа EGL: mtkfb

Готовых платформ libhybris не хватило: `fbdev` требует
`framebuffer_device_t` из gralloc (в Android 12 его нет), `hwcomposer`
требует `libhwc2_compat_layer.so`, который собирается только внутри дерева
AOSP. Написана своя:
`hybris/libhybris/hybris/egl/platforms/mtkfb/` (`eglplatform_mtkfb.cpp`,
`mtkfb_window.cpp/h`, `build.sh` — собирается вручную, вне autotools).
Буферы выделяет обычный gralloc, показ делается сам: `hybris_gralloc_lock`,
копирование в mmap `/dev/fb0`, `FBIOPAN_DISPLAY`. Включается
`EGL_PLATFORM=mtkfb`.

Три вещи по дороге.

**1. `eglMakeCurrent` -> EGL_BAD_MATCH.** Сам по себе код ошибки ничего не
говорит; чтобы увидеть жалобу драйвера, написан
`/usr/local/sbin/minilogd.py` — 40 строк на Python, слушает юникс-датаграммы
на `/dev/socket/logdw` и печатает то, что андроидные библиотеки шлют в
liblog (без logd эти сообщения просто пропадают). Сразу стало видно:

```
E IMGSRV: :470: ValidateNativeHandle: Unexpected native handle size - got 9 expected 50
E IMGSRV: :558: ANWBIncRef: gralloc_inc_ref_img() failed
E IMGSRV: :479: KEGLGetDrawableParameters: Native window is invalid
E IMGSRV: :2661: GLES3MakeCurrentGC: Invalid write drawable - what do we do?
E libEGL: eglMakeCurrentImpl:1062 error 3009 (EGL_BAD_MATCH)
```

`hw_get_module("gralloc")` перебирает `ro.hardware.gralloc`,
`ro.hardware`, `ro.product.board`, `ro.board.platform`, `ro.arch`, а
затем `default`. Ни одно из имён не давало `gralloc.mt6761.so`, и
libhybris брал `gralloc.default.so` — заглушку, чьи хендлы PowerVR не
признаёт. Лечится строчкой в build.prop:

```
ro.hardware.gralloc=rogue
ro.vndk.version=31
```

**Ловушка overlayfs:** править файл в нижнем слое смонтированного overlay
бесполезно — ядро отдаёт старое содержимое. `/system.local/build.prop` был
создан уже после монтирования, и до `umount /system && mount ...` весь
объединённый build.prop просто не действовал.

**2. Панорамирование недоступно.** `FBIOPAN_DISPLAY` с `yoffset>0`
возвращает EINVAL: в `mtkfb.c` мы сами выставили `fix->ypanstep = 0`,
чтобы fbcon перерисовывал текст на месте, а `fb_pan_display()` в fbmem
отклоняет любой ненулевой yoffset при `ypanstep == 0`. Платформа это ловит и
переходит на одну страницу. Вызов с `yoffset=0` всё равно нужен: именно он
сбрасывает кэш и заново отправляет слой в overlay.

Правильное решение — в ядре вернуть `ypanstep = 1`, но убрать
`FBINFO_HWACCEL_YPAN` из `info->flags`, тогда fbcon останется в режиме
перерисовки, а пользовательское панорамирование заработает. Это требует
перепрошивки ядра, поэтому отложено до разрешения.

**3. Производительность.** Замер на кадр 720x1612:

| шаг | время |
|---|---|
| `hybris_gralloc_lock` | 0.4 мс |
| memcpy 4.6 МиБ из буфера gralloc в fb | 30.9 мс |

То есть ~150 МБ/с — буфер gralloc отображается некэшированным. Оконная
поверхность даёт 10 fps, pbuffer + `glReadPixels` — 24 fps, вариант с
прямым `mmap` dma-buf — 18.9 fps (на этом железе `mmap` fd из
native_handle возвращает ENODEV, копирование всё равно идёт через gralloc).
Настоящее лечение — отдавать буфер в `/dev/mtk_disp_mgr` как слой overlay,
без копирования вообще; интерфейс есть в исходниках ядра
(`disp_session_input_config`, `DISP_IOCTL_*`).

#### Toolkit

Qt5 в Debian 12 собран под **настольный** OpenGL (`libQt5Gui.so.5` тянет
`libGL.so.1`), поэтому eglfs просит конфиг с `EGL_OPENGL_BIT`. У драйвера
такого нет — все 67 конфигов имеют `EGL_RENDERABLE_TYPE=0x45`
(ES1|ES2|ES3). Отсюда `Cannot find EGLConfig` и
`Could not create the egl surface: 0x3005`.

В Qt6 `QOpenGLContext::isOpenGLES()` определяется по
`QSurfaceFormat::renderableType()` в рантайме, поэтому достаточно своего
`main()`, который до `QGuiApplication` делает:

```c++
QSurfaceFormat fmt;
fmt.setRenderableType(QSurfaceFormat::OpenGLES);
fmt.setVersion(3, 0);
QSurfaceFormat::setDefaultFormat(fmt);
```

Исходники: `hybris/bf7-gpu/gui/main.cpp` и `demo6.qml`. Запуск:

```sh
env LD_LIBRARY_PATH=/usr/local/lib LD_PRELOAD=/usr/local/lib/libbf7tlsguard.so \
    EGL_PLATFORM=mtkfb QT_QPA_PLATFORM=eglfs QT_QPA_EGLFS_INTEGRATION=none \
    XDG_RUNTIME_DIR=/tmp/runtime-root ./bf7gui demo6.qml
```

#### Касание

Работает без единой строчки кода: libinput сам нашёл устройство.

```
qt.qpa.input: libinput: event2 - mtk-tpd2: device is a touch device
qt.qpa.input: libinput: registerDevice /dev/input/event2 - mtk-tpd2
```

Диапазоны `ABS_MT_POSITION_X 0..719`, `Y 0..1611` совпадают с панелью
один в один, пересчёт не нужен. Проверено впрыском событий в
`/dev/input/event2` (`hybris/bf7-gpu/gui/tap.py`, протокол multitouch B):
три тапа по кнопке — счётчик в QML показал «касаний: 3».

### 2026-09-16 00:18 — 12 fps -> 60 fps, и разворот тача

Жалобы с телефона: экран отзеркален по обеим осям и «как будто 10 кадров в
секунду». Оба верны, оба вылечены.

#### Тачскрин повёрнут на 180°

Панель и дигитайзер развёрнуты друг относительно друга. Диапазоны совпадают
(`ABS_MT_POSITION_X 0..719`, `Y 0..1611`), поэтому достаточно матрицы
калибровки libinput — `/etc/udev/rules.d/99-bf7-touch.rules`:

```
SUBSYSTEM=="input", KERNEL=="event*", ATTRS{name}=="mtk-tpd2", \
    ENV{LIBINPUT_CALIBRATION_MATRIX}="-1 0 1 0 -1 1"
```

libinput применяет её сам в `libinput_event_touch_get_x_transformed`, Qt
получает уже правильные координаты. Впрыск событий в `/dev/input/event2`
идёт мимо калибровки, поэтому `bf7vnc.py` разворачивает координаты указателя
сам (`BF7VNC_FLIP`, по умолчанию включено).

#### Где были 10 кадров в секунду

Разбор по шагам кадра 720x1612 (`MTKFB_TRACE=1`):

| шаг | было |
|---|---|
| отрисовка на GPU (`glFinish`) | 2.1 мс |
| `eglSwapBuffers` целиком | 80.4 мс |
| из них наш показ кадра | ~27 мс |
| **остальное внутри драйвера** | **~51 мс** |

Проверки, что это не GPU: процесс всё это время в состоянии R и жжёт utime
(значит, считает CPU, а не ждёт железо); `gpu_loading = 0`; закрепление
частоты на максимуме (`echo 660000 > /proc/gpufreq/gpufreq_opp_freq`, 390 ->
660 МГц) не изменило ничего.

Выборка стеков через gdb поймала `writev` и `vsnprintf` из glibc — то есть
**логирование**. Замер объёма: **228 234 байта лога на 40 кадров**, 5.7 КиБ на
кадр, из них 1760 строк вида `[GrallocExtra]: #NN pc ...`.

MTK-шная `libgralloc_extra` на каждый `gralloc_extra_query` и
`gralloc_extra_perform` (четыре вызова на кадр) строит
`android::CallStack`, раскручивает стек и печатает его целиком. Раскрутка
идёт через `libutilscallstack.so` -> `libunwindstack.so` и стоит дороже
всего остального вместе взятого.

**Лечение** — подменить `libutilscallstack.so` заглушкой:
`hybris/bf7-gpu/callstack-stub.c`, все символы `android::CallStack` и
`android::ProcessCallStack` превращены в пустые функции. Собирается с
`-nostdlib`, чтобы в библиотеке не было ни одной записи `DT_NEEDED` — её
грузит линковщик libhybris, который glibc искать не умеет:

```sh
gcc -O2 -fPIC -shared -nostdlib -Wl,-soname,libutilscallstack.so \
    -o /system.local/lib64/libutilscallstack.so callstack-stub.c
```

Полный список символов собран обходом всех блобов:
`readelf -sW --dyn-syms *.so | awk '$7=="UND"' | grep CallStack` —
шестнадцать штук, включая `CallStack::CallStack(char const*, int)` из
`libPVRMtkutils.so`, без которого линковка падает.

Итог:

| | было | стало |
|---|---|---|
| лог на 60 кадров | ~340 КиБ | 214 байт |
| `eglSwapBuffers` (без показа) | 51.3 мс | **0.8 мс** |
| fps без показа | 18.7 | **399.7** |
| fps с показом на панель | 12.0 | **59.8** (упёрлись в vsync) |

#### Заодно: копирование кадра в четыре потока

Буфер gralloc отображён без кэша, одиночный `memcpy` читает из него
145 МБ/с. Это задержка, а не полоса: замер `fb -> malloc` показал 138 / 288 /
425 / 523 МБ/с на 1 / 2 / 3 / 4 потоках. В платформу добавлен постоянный пул
из четырёх потоков (`copy_parallel` в `mtkfb_window.cpp`), копирование
кадра 31 мс -> 8 мс. Запись **в** фреймбуфер, наоборот, быстрая (1403 МБ/с) —
он write-combine.

#### Ошибка в libhybris

`hybris/common/legacy_properties/properties.c`, `my_property_get()`: если
свойство не найдено и `default_value == NULL`, код выполнял
`value = '\0'` — присваивание **указателю**, а не первому байту. Буфер
вызывающего оставался неинициализированным. А хук `__system_property_get`
зовёт именно с `NULL` (`hooks.c:2232`), то есть любое чтение
отсутствующего свойства возвращало мусор из стека. Исправлено на
`value[0] = '\0'`.

#### Виртуальная клавиатура

`qt6-virtualkeyboard-plugin` + `qml6-module-qtquick-virtualkeyboard`.
В eglfs окно ровно одно, поэтому автоматический «настольный» режим плагина не
работает (`EGLFS: OpenGL windows cannot be mixed with others`, аварийный
выход). Нужно `QT_VIRTUALKEYBOARD_DESKTOP_DISABLE=1` и `InputPanel` прямо
в сцене QML. Из зависимостей не хватало `qml6-module-qt-labs-folderlistmodel`.
Проверено вводом через VNC: в поле набралось «Qqwer».

#### Что стоит на автозапуске

| юнит | назначение |
|---|---|
| `bf7-android-mounts.service` | тома из super + overlay + apex |
| `bf7-minilogd.service` | приём liblog в `/var/log/bf7-android.log` |
| `bf7-gui.service` | `/usr/local/bin/bf7-gui` -> Qt Quick на панели |
| `bf7-vnc.service` | зеркало панели на `:5900` + ввод касаний |

### 2026-09-16 00:31 — аудит «чего не хватает до полноценного сервера»

Снят с живого устройства, сведён в `docs/bf7-kernel/server-readiness.md`.
Там же зафиксирован приоритет на ночь — вывод кадра без копирования через
`/dev/mtk_disp_mgr`.

Коротко: Docker не запустится без `CONFIG_IPC_NS`, `CONFIG_CGROUP_DEVICE`,
`CONFIG_POSIX_MQUEUE` и `CONFIG_NETFILTER_XT_MATCH_ADDRTYPE` — проверено
официальным `check-config.sh` из moby. Из пользовательского пространства нет
ни `iptables`, ни контейнерного движка. Wi-Fi AP железо умеет (`phy1`
заявляет `AP` и `P2P-GO`, интерфейс `ap0` уже создан), не хватает
`hostapd`/`dnsmasq`. Режим USB-хоста собран в ядре
(`CONFIG_USB_MTK_OTG=y`, `CONFIG_MTK_MUSB_DUAL_ROLE=y`, узел `cmode`),
но не проверен натурно. Свопа нет, `zram0` не настроен. Топливомер батареи
отдаёт `capacity = -1`.

Заодно закрыта дыра: `bf7vnc.py` слушал `0.0.0.0:5900` без пароля, а это
полное управление телефоном касаниями. Теперь по умолчанию только USB-сеть,
наружу — осознанно через `BF7VNC_BIND`.

### 2026-09-16 00:44 — настоящий рабочий стол: Xfce на панели

Замечание по делу: самописная сцена на Qt Quick — это не оконный менеджер.
Теперь на телефоне работает полноценный Xfce 4.18 (xfwm4, панель, рабочий
стол, Thunar, терминал), автозапуском из systemd.

#### Почему именно так, а не Xorg напрямую

Xorg на панель можно вывести только через `xf86-video-fbdev`, то есть
растеризация целиком на CPU. Аппаратного пути нет: KMS для нашего чипа
отсутствует — драйвер `drivers/gpu/drm/mediatek` в дереве есть, но
поддерживает mt2701, mt2712, mt6779, mt6781, mt6833, mt6853, mt6873, mt6877,
mt6885 и mt8173, а **mt6765 в списке нет**. Дисплей у нас на старом стеке
`mtkfb`/`mtk_disp_mgr`. Даже с KMS Xorg не получил бы ускорения: glamor
требует GL через GBM, а у нас только андроидный GLES через libhybris.

Поэтому схема такая:

```
композитор Wayland (Qt 6.4, наш EGL -> PowerVR)
  -> Xwayland без -rootless (весь экран X = одно окно Wayland)
     -> Xfce: xfwm4, xfce4-panel, xfdesktop, Thunar, xfce4-terminal
```

Клиенты X рисуют в разделяемую память, композитор берёт это текстурой и
выводит на GPU. Это честный компромисс: композиция и вывод аппаратные,
ускорения GL у самих X-приложений нет и взяться ему неоткуда.

GNOME и KDE недостижимы: gnome-shell и kwin требуют OpenGL, и подменить его
нечем. Xfce, openbox, i3, IceWM — работают.

#### Композитор

`hybris/bf7-gpu/gui/compositor.qml`, ~100 строк: `WaylandCompositor` с
`WaylandOutput` на полный экран, `XdgShell` и `WlShell`, каждая
поверхность разворачивается на весь вывод. Запускается тем же бинарником
`bf7gui`, который принуждает Qt к GLES.

Предупреждение `There is no EGL_WL_bind_wayland_display extension` ожидаемо:
у андроидного EGL нет привязки буферов Wayland, поэтому клиенты идут через
`wl_shm`. Для Xwayland это и не нужно — он сам работает через shm
(`Failed to initialize glamor, falling back to sw`).

#### Две ловушки при сборке сеанса

1. **Гонка с сокетом.** Проверять существование файла `wayland-1`
   бесполезно: он остаётся от прошлого сеанса, и новый Xwayland успевает
   подключиться к мёртвому файлу раньше, чем композитор начнёт слушать.
   Лечится удалением сокета и ожиданием **реального** подключения:

   ```sh
   rm -f "$RUNDIR/$WL" "$RUNDIR/$WL.lock"
   until python3 -c "import socket,sys
   s=socket.socket(socket.AF_UNIX); s.settimeout(2)
   s.connect(sys.argv[1]); s.close()" "$RUNDIR/$WL" 2>/dev/null; do sleep 0.2; done
   ```

2. **`WAYLAND_DISPLAY` в окружении рабочего стола смертелен.** GTK по нему
   выбирает бэкенд Wayland, а `xfce4-session` подставляет его значение в
   `DISPLAY`, и получается `xfwm4: cannot open display: wayland-1` и
   `xfce4-panel: Unable to open display from environment variable
   DISPLAY='wayland-1'`. В сеансе X переменную нужно снять, а заодно снять
   `LD_PRELOAD` и `EGL_PLATFORM` и выставить `GDK_BACKEND=x11`,
   `QT_QPA_PLATFORM=xcb`.

Своё композитирование xfwm4 выключено (`/general/use_compositing = false`):
рисовать его нечем, а композитор уже есть и он на GPU.

Юнит `bf7-session.service` заменил `bf7-gui.service`; `bf7-vnc.service`
переключён на него же. Скрипт — `/usr/local/bin/bf7-session`.

Касание работает и в X: тап по заголовку окна xterm заставил openbox его
подвинуть.

### 2026-09-16 00:56 — масштаб, поворот и экранная клавиатура

Три замечания с телефона: нет клавиатуры, не применяется масштаб 1.5x и не
применяется поворот экрана.

#### Почему настройки Xfce тут бессильны

Xwayland запущен без `-rootless`: весь экран X — это одна поверхность
Wayland. Размер этой поверхности Xwayland берёт **из `wl_output`**, а не из
конфигурации `xdg_toplevel` (проверено: `sendFullscreen` с другим размером
он игнорирует, экран остаётся 720x1612). Значит масштаб и поворот должны
задаваться композитором, а «Display» в настройках Xfce менять нечего.

#### Как сделано

Вывод объявляется в логических пикселях (`панель / масштаб`), а на панель
натягивается трансформацией сцены QML, то есть силами GPU. Действует на всё
сразу, включая сам оконный менеджер и его декорации.

Размер вывода задаётся **только из C++**: в QML `WaylandOutput.geometry`
доступна лишь на чтение (`Invalid property assignment: "geometry" is a
read-only property`), а сеттера `setGeometry` в Qt 6 нет вовсе. Правильный
способ — режим вывода:

```c++
const QWaylandOutputMode mode(QSize(lw, lh), 60000);
out->setSizeFollowsWindow(false);
out->addMode(mode, true);
out->setCurrentMode(mode);
```

Объект находится по `objectName: "bf7Output"`. Результат:

```
вывод 480x1075 -> панель 720x1612, масштаб 1.5, поворот 0
dimensions:    480x1075 pixels (127x284 millimeters)
```

Поворот — второй трансформацией той же сцены; при 90 и 270 логические ширина
и высота меняются местами, поэтому Xwayland получает альбомный экран. Ввод
пересчитывается сам: Qt отображает события через трансформацию элемента.

Управление — `/usr/local/bin/bf7-display`, состояние в
`/etc/default/bf7-display`, применяется перезапуском `bf7-session.service`:

```sh
bf7-display            # показать текущее
bf7-display 1.5        # масштаб
bf7-display 1.5 90     # масштаб и поворот
bf7-display 1 0 144    # без растягивания, крупнее за счёт DPI
```

Два способа укрупнения дают разное. `BF7_SCALE` растягивает весь экран X на
GPU: работает для всего, но картинка мягче. `BF7_DPI` оставляет родное
разрешение и увеличивает шрифты и элементы через `Xft.dpi` и
`xsettings /Xft/DPI` — чётко, но слушаются не все приложения. Сочетаются.

Настоящее решение обеих задач — аппаратный масштабатор наложения: в
`disp_input_config` есть отдельно `src_width/src_height` и
`tgt_width/tgt_height`. Тогда GPU будет рисовать сразу в логическом
разрешении (в 1.5x это 44% пикселей, то есть ещё и дешевле), а растягивать
будет дисплейный контроллер. Делается вместе с выводом без копирования.

#### Клавиатура

`onboard` плюс `at-spi2-core`, автозапуск через
`/etc/xdg/autostart/bf7-keyboard.desktop` -> `bf7-keyboard-start`, выбор
через `BF7_KEYBOARD` (можно поставить `xvkbd` или пустую строку, чтобы
выключить).

### 2026-09-16 01:09 — замер под Chromium с видео, HOME, своп

Пользователь поставил Chromium и запустил видео — работает. Замер под
нагрузкой (`top`, 4 ядра, все на 2001 МГц):

| процесс | CPU |
|---|---|
| `bf7gui` (композитор) | **96.6%** — ровно одно ядро |
| `chromium` (декодирование и отрисовка, несколько процессов) | ~145% |
| `Xwayland` | 9.8% |
| итого | 57.2% user + 10.5% sys, простой 32% |

```
/sys/kernel/ged/hal/gpu_utilization = 4 0 96     # 3D 4%, TA 0%, простой 96%
/proc/gpufreq/gpufreq_var_dump: gpu_loading = 0
mtktscpu = 50°C, батарея 35°C
```

**GPU почти не занят, всё считает процессор.** Ровно одно ядро съедает
композитор: 8 мс на копирование кадра плюс загрузка буфера Xwayland из
разделяемой памяти в текстуру (при масштабе 1.5 это 480x1075x4 = 2 МиБ на
кадр). Первое снимается выводом без копирования через `mtk_disp_mgr`,
второе — свойство самого Xwayland без dmabuf.

Видео декодируется на CPU: `/dev/video*` нет,
`CONFIG_VIDEO_MEDIATEK_VCODEC` не включён, а MediaTek-овские
`/dev/Vcodec` и `/dev/Vcodec2` — свой закрытый интерфейс, не V4L2.
Аппаратное декодирование потребует либо мейнлайнового драйвера V4L2 M2M (ещё
надо проверить, поддерживает ли он mt6765), либо блобов
`android.hardware.media.c2@1.2-mediatek` через libhybris. Отдельный проект.

#### Мелочи по сеансу

`HOME` службам systemd не задаёт, поэтому терминал Xfce открывался в
`/root/gui` — там, где оказался рабочий каталог композитора. Теперь в юните
`Environment=HOME=/root` и `WorkingDirectory=/root`, а в скрипте выставлены
`XDG_CONFIG_HOME`, `XDG_DATA_HOME`, `XDG_CACHE_HOME`, `USER`,
`LOGNAME`, `SHELL`, и перед запуском Xfce делается `cd "$HOME"`.
Композитор запускается по полному пути, рабочий каталог ему больше не нужен.

#### Своп

Свопа не было вовсе, а под Chromium оставалось 144 МиБ свободной памяти.
Поднят zram: `systemd-zram-generator`, `/etc/systemd/zram-generator.conf`
с `zram-size = min(ram / 2, 2048)` и `zstd` — получилось 1.9 ГиБ.

Генератор создаёт юнит `dev-zram0.swap` только на раннем этапе загрузки, и
после `daemon-reload` он не появился, поэтому юнит прописан явно в
`/etc/systemd/system/dev-zram0.swap` и включён. Плюс
`/etc/sysctl.d/99-bf7-zram.conf`: `vm.swappiness = 150`,
`vm.page-cluster = 0` — своп в сжатой памяти дешёвый, в отличие от eMMC,
выгружать стоит охотнее и читать по одной странице.

### 2026-09-16 01:13 — список готовности сервера дополнен найденными поломками

`docs/bf7-kernel/server-readiness.md`: добавлен раздел «Найденные поломки» —
семь уже исправленных (VNC без пароля наружу, отсутствие свопа, незаданный
`HOME`, ошибка `my_property_get` в libhybris, `gralloc.default.so` вместо
`gralloc.rogue.so`, раскрутка стека в `libgralloc_extra`, закреплённая
частота GPU) и двенадцать открытых. Среди открытых, кроме уже известного:
декодирование видео на CPU, отсутствие OpenGL у X-клиентов, отсутствие
`EGL_WL_bind_wayland_display`, молчащий топливомер, пропадающий маршрут по
умолчанию на Wi-Fi, `cpuset` без cgroup2, зомби в сеансе Xfce.
Порядок работ переписан под текущее состояние.

### 2026-09-16 01:24 — приведение системы в порядок

Перед тем как закрывать список, разобрал накопившееся. Нашлось несколько
вещей, которые молча портили жизнь.

**Система считала себя контейнером.** `systemd-detect-virt` отвечал
`docker`: в корне лежал пустой `/.dockerenv` от сборки образа (11 августа).
Из-за него systemd отказывался обслуживать юниты типа `.swap`
(«Operation on or unit type of dev-zram0.swap not supported on this system»),
не трогал аппаратный сторожевой таймер и вообще вёл себя как в контейнере.
Файл убран в `/root/.dockerenv.removed-2026-09-16`, `systemd-detect-virt`
теперь `none` — но сам systemd узнает об этом со следующей загрузки, поэтому
своп поднимается юнитом `bf7-zram.service` (oneshot с `swapon`), который
работает в обоих режимах.

**Системная шина D-Bus не была запущена вовсе.** Отсюда молчали
`networkctl`, `timedatectl`, `hostnamectl`. `dbus.socket` лежит в
`/lib/systemd/system/sockets.target.wants/`, но не стартовал; добавлен явный
симлинк в `/etc/systemd/system/sockets.target.wants/`.

**Пропажа маршрута по умолчанию объяснилась.** Точка доступа роумит между
радиомодулями — в журнале видно смену BSSID `50:ff:20:xx:xx:xx` ->
`52:ff:20:xx:xx:xx` и «Lost carrier» каждые 5–15 минут. При потере несущей
networkd снимает адрес с маршрутом, а обратно маршрут не возвращается.
В `20-wlan0.network` добавлено `IgnoreCarrierLoss=30s` и
`KeepConfiguration=yes`.

**`/etc/hosts` был пустой** — ни `localhost`, ни имени узла. Заполнен.

Прочее: часовой пояс `Etc/UTC` -> `Europe/Moscow`; локаль не была задана
вовсе, сгенерированы `ru_RU.UTF-8` и `en_US.UTF-8`, `LANG=ru_RU.UTF-8`;
журнал занимал **1.3 ГиБ** — ограничен 200 МиБ через
`journald.conf.d/99-bf7.conf`; `/` перемонтирован с `noatime` и включён
`fstrim.timer` (eMMC не резина); снята закреплённая на 660 МГц частота GPU;
удалён отработавший `bf7-gui.service`.

В `bf7-session` добавлена проверка среды при старте: если не смонтированы
стоковые разделы, если в build.prop нет `ro.hardware.gralloc=rogue` или если
`libutilscallstack.so` не подменена заглушкой — в журнал идёт внятное
предупреждение. Иначе всё «работает», но впятеро медленнее.

Осталось косметическое: в сеансе Xfce копятся зомби (`xdg-user-dirs-u`,
`xfsettingsd`, `applet.py`, `exo-open`) — их не подбирает
`xfce4-session`.

### 2026-09-16 01:24 — пакеты готовности, экран, мониторинг

Поставлены `iptables`, `bridge-utils`, `prometheus-node-exporter`,
`unattended-upgrades`. Docker по указанию не ставится — только проверялка
готовности, положена в `/usr/local/sbin/bf7-docker-check`.

**Ловушка с iptables:** в Debian по умолчанию бэкенд nftables, а в ядре
`CONFIG_NF_TABLES` нет, поэтому `iptables -L` падал с «Could not fetch
rule set generation id: Invalid argument». Переключено
`update-alternatives --set iptables /usr/sbin/iptables-legacy` (и то же для
`ip6tables`).

**Межсетевой экран** — `/usr/local/sbin/bf7-firewall`, юнит
`bf7-firewall.service` в `sysinit.target`. Политика `INPUT DROP`;
пропускаются `lo`, установленные соединения, вся USB-сеть 192.168.7.0/24
(прямой кабель к ПК: ssh, VNC, аварийные консоли), эхо-запросы с ограничением
и ssh откуда угодно. Применялось под страховкой: отдельный процесс через три
минуты открывал всё обратно, на случай если правила отрежут доступ; после
проверки обоих каналов страховка снята.

Ещё одна нехватка в ядре: цели `LOG` нет
(`CONFIG_NETFILTER_XT_TARGET_LOG`), поэтому отбрасывание не журналируется —
скрипт это переживает и печатает предупреждение.

`prometheus-node-exporter` слушает `:9100`, снаружи закрыт экраном,
с ПК по USB доступен. Автообновления — только из
`${distro_codename}-security`, без автоматической перезагрузки.

**Почему `cpuset` не появляется в cgroup2.** В нашем дереве
`kernel/cgroup/cpuset.c` объявляет подсистему без файлов версии 2:

```c
struct cgroup_subsys cpuset_cgrp_subsys = {
	...
	.legacy_cftypes	= files,
	.early_init	= true,
};
```

В ванильном 4.19 там же есть `.dfl_cftypes = dfl_files` и
`.threaded = true`. Android-ядра MediaTek это вырезают, потому что Android
пользуется cpuset только через v1 (`/dev/cpuset`). Все три дерева в проекте
(`x657b-4.19`, `duoqin-mt6761-4.19`, `mediatek-mt6761-alps188-gpu115`)
одинаково без `dfl_files`. Лечится переносом кусочка из upstream — пойдёт в
набор правок к ядру.

### 2026-09-16 01:30 — вывод через /dev/mtk_disp_mgr: путь работает

Собрана отдельная проба `hybris/bf7-gpu/dispmgr/dispmgrtest.c`: буфер
выделяется обычным gralloc (то есть ION dma-buf), заполняется градиентом через
`mmap` его dma-buf и отдаётся дисплейному контроллеру слоем наложения, минуя
фреймбуфер.

**Ключевое открытие: `DISP_IOCTL_SET_INPUT_BUFFER` и
`DISP_IOCTL_TRIGGER_SESSION` в этом ядре не реализованы** — в
`mtk_disp_mgr_ioctl()` их просто нет в разборе. Современный путь один:
`DISP_IOCTL_FRAME_CONFIG` со структурой `disp_frame_cfg_t`, которая
объединяет конфигурацию слоёв и запуск кадра. Именно им пользуется HWC.

Порядок такой:

1. `DISP_IOCTL_CREATE_SESSION` — `{type = DISP_SESSION_PRIMARY,
   device_id = 0, mode = DISP_SESSION_DIRECT_LINK_MODE, user = SESSION_USER_HWC}`.
   Ядро просто регистрирует `MAKE_DISP_SESSION(type, dev)`, то есть
   `0x10000`, и повторный вызов безвреден.
2. `DISP_IOCTL_PREPARE_INPUT_BUFFER` — `{session_id, layer_id = 0,
   layer_en = 1, ion_fd, cache_sync}`, возвращает `index` и `fence_fd`.
   Внутри `ion_import_dma_buf_fd()` и `ION_MM_GET_IOVA`.
3. `DISP_IOCTL_FRAME_CONFIG` — один слой, `next_buff_idx = index`,
   `src_phy_addr = NULL` (адрес ядро возьмёт из зарегистрированного ION).

Слой берём **нулевой**: `primary_display_get_option("FB_LAYER")` в
`primary_display.c` возвращает 0, то есть это тот же слой, которым
пользуется `mtkfb`, и мы его замещаем, а не накладываемся поверх.

Результат пробы:

```
панель 720x1612
буфер: stride=736 numFds=3 numInts=47
dma-buf: fd[1]=7, 4745728 байт
сессия 0x10000
буфер зарегистрирован: индекс 2, fence 12
кадр отправлен
```

Подтверждение из `/sys/kernel/debug/mtkfb` во время работы пробы:

```
|Session id	0x00010000
|layerinfo 0	fence_fd(12)	fence_idx(2)	timeline_idx(1)
```

`fence_idx` ненулевой — значит источником нулевого слоя стал наш ION-буфер,
а не фреймбуфер (при выводе через `mtkfb` там `next_buff_idx = -1`).
Заодно из того же дампа: панель `td4160_hdp_dsi_vdo_hx_inx_bf7`, путь в
режиме **DECOUPLE**, фреймбуфер `PA:0x7c4b0000`, 3 страницы.

**Осторожно: `echo dump_reg:N > /sys/kernel/debug/dispsys` вешает устройство.**
Попытка снять дамп регистров наложения привела к перезагрузке по сторожевому
таймеру. Скорее всего чтение регистров идёт в момент, когда тактирование
дисплейного блока снято. Больше туда не лезем.

### 2026-09-16 01:30 — проверка загрузки после перезагрузки

Незапланированная перезагрузка оказалась полезной: это первая загрузка со
всеми ночными изменениями.

```
systemd-detect-virt -> none          (был docker из-за /.dockerenv)
bf7-android-mounts   enabled active
bf7-minilogd         enabled active
bf7-firewall         enabled active   INPUT policy DROP
bf7-zram             enabled active   /dev/zram0 1,9G prio 100
bf7-session          enabled active   композитор + Xwayland + Xfce
bf7-vnc              enabled active
dbus                 enabled active
prometheus-node-exporter enabled active
```

Цепочка ядро -> консоль -> Debian -> рабочий стол поднимается сама, поворот
90° и масштаб 1.5 сохранились. Время в MSK.

Единственный сбойный юнит — `upower.service`: «Start request repeated too
quickly». Это следствие молчащего топливомера.

### 2026-09-16 01:36 — вывод без копирования встроен в платформу

`hybris/libhybris/hybris/egl/platforms/mtkfb/` теперь умеет два пути показа
кадра и по умолчанию выбирает слой наложения:

| | копирование в фреймбуфер | слой наложения |
|---|---|---|
| `eglSwapBuffers` | 14.6 мс | **1.7 мс** |
| кадров в секунду | 59.5 | 259.9 (без синхронизации) |
| процессор на кадр | 8 мс копирования в 4 потока | ноль |

С привязкой к синхроимпульсу — ровно 60.2 fps, и **процессор на неподвижном
рабочем столе простаивает на 99.5%**. Раньше композитор съедал целое ядро.

Устройство ядра: `DISP_IOCTL_FRAME_CONFIG` не блокирует, поэтому после
отправки кадра ждём `DISP_IOCTL_WAIT_FOR_VSYNC` — иначе всё крутится
вхолостую на 260 кадрах в секунду при панели 60 Гц. Ожидание блокирующее, то
есть это сон, а не трата процессора.

Переключатель `MTKFB_NO_DISP_MGR=1` возвращает старый путь; при любой ошибке
ioctl платформа сама откатывается на копирование и пишет об этом в журнал.

**Побочное следствие: VNC перестал видеть картинку.** `bf7vnc` читает
`/dev/fb0`, а при выводе слоем фреймбуфер больше не обновляется — на экране
у зрителя оставался кадр с прошлого запуска. Решено двумя мелочами:

1. Пока хоть один зритель подключён, `bf7vnc` держит файл-флаг
   `/run/bf7-gui/mirror-fb`; платформа видит его и **дополнительно**
   копирует кадр в фреймбуфер. Нет зрителей — нет копирования.
2. На неподвижном рабочем столе кадров не возникает вовсе, поэтому при
   подключении `bf7vnc` дёргает `xrefresh`, и композитор выдаёт кадр.

Проверено: в VNC видна живая картинка рабочего стола вместе с экранной
клавиатурой.

### 2026-09-16 01:49 — топливомер батареи и upower

Пункт из списка закрыт. Причина, почему `capacity` была `-1`: **алгоритм
заряда у MediaTek живёт не в ядре, а в пользовательском демоне**. В журнале
ядра это видно прямо:

```
car[-1,0,0,0,0] tmp:33 soc:0 uisoc:0 vbat:4347 ibat:-12 algo:0 gm3:0
```

`algo:0` — считать некому. Демон нашёлся в стоковом разделе:
`/vendor/bin/fuelgauged`. Он оказался обычным андроидным ELF с зависимостями
только `libcutils`, `libutils`, `liblog`, `libc++`, `libc` — то есть
libhybris ему не нужен, достаточно смонтированных `/system` и `/vendor`.
Запуск:

```
fuel gauge daemon init
get control socket success, sock_id:5
get data from kernel:660 660 0 data:111 0 0 0
capacity: было -1, стало 100, init_done=1
```

Оформлено юнитом `bf7-fuelgauge.service`.

**Заодно нашлась причина падения `upower`** и первое подтверждение, что новый
конфиг ядра нужен по делу: юнит Debian содержит `PrivateUsers=yes`, а это
требует `CONFIG_USER_NS`, которого в текущем ядре нет — systemd выходил с
`217/USER`. В собранном ядре v2 опция включена. Пока поставлен временный
обход `/etc/systemd/system/upower.service.d/99-bf7-no-userns.conf`, после
перепрошивки его можно убрать.

Теперь:

```
present: yes   state: charging   percentage: 100%
voltage: 4,347 V   temperature: 33,1 degrees C
```

Сбойных юнитов в системе не осталось.

### 2026-09-16 01:51 — видеодекодинг: разведка и что реально мешает

**Со стороны ядра делать нечего.** Драйвер уже собран и работает: в нашем
конфиге и в OEM-конфиге одинаково `CONFIG_MTK_VIDEOCODEC_DRIVER=y`,
узлы `/dev/Vcodec` и `/dev/Vcodec2` на месте. Это **не** V4L2, а
собственный legacy-драйвер MediaTek
(`drivers/misc/mediatek/videocodec/mt6765/videocodec_kernel_driver.c`,
`VCODEC_DEVNAME "Vcodec"`).

Мейнлайновый V4L2-драйвер `drivers/media/platform/mtk-vcodec` в дереве тоже
есть, но знает mt6768, mt6779, mt6781, mt6785, mt6833, mt6853, mt6873,
mt6877, mt6885, mt8173, mt8167, mt2712, mt6771 — **mt6765 в списке нет**, и в
`mt6765.dtsi` нет ни узла vcodec, ни vcu. Порт возможен по аналогии с
mt6768, но он потребует ещё и демона `vpud`, которого на этом разделе нет.

**Живой путь — Codec2.** Стек такой:

```
libcodec2_mtk_vdec.so  (компоненты)
  -> libvcodec_oal.so  (обёртка над потоками и памятью)
     -> libvcodec_utility.so  (единственная, кто открывает /dev/Vcodec)
```

OMX отпадает: `libMtkOmxCore.so` не экспортирует ни одного символа
`OMX_*` — в Android 12 OMX уже вытеснен Codec2, а от ядра остался огрызок.

**Проверено на железе: стек грузится через libhybris и создаёт фабрики
компонентов.**

```
libcodec2_mtk_vdec.so -> 0x643c9869
  CreateCodec2AvcFactory       фабрика=0x55c1b79600   H.264
  CreateCodec2HevcFactory      фабрика=0x55c1b7a300   H.265
  CreateCodec2Vp9Factory       фабрика=0x55c1b7e940   VP9
  CreateCodec2VpxFactory       фабрика=0x55c1b81170   VP8
  CreateCodec2Mpeg4Factory     фабрика=0x55c1b7f260
  CreateCodec2Mpeg2Factory     фабрика=0x55c1b849c0
  CreateCodec2Av1Factory       фабрика=0x55c1b85050
```

То есть аппаратный декодер достижим, и полный набор кодеков на месте
(есть ещё H263, VC1, HEIF и защищённые варианты).

**Что мешает.** Имена функций сишные, но возвращают они
`C2ComponentFactory *` — абстрактный класс C++, а весь API Codec2 построен на
`std::shared_ptr`, `std::vector`, `C2Param`. Эти типы собраны с
**bionic-овской libc++**, и вызывать такие методы из glibc-программы нельзя:
несовместимы раскладки и аллокаторы. Виртуальные вызовы сами по себе прошли бы
(это ABI), но всё, что передаётся по значению, — нет.

**Решение то же, что у Halium для HWC: прослойка, собранная как андроидная
библиотека.** Маленький `.so`, собранный NDK (bionic + libc++), который:

1. линкуется с заголовками Codec2 из AOSP
   (`frameworks/av/media/codec2/{core,vndk}/include`);
2. наружу отдаёт **простой C API**: открыть декодер, отдать пакет, забрать
   кадр дескриптором dma-buf;
3. грузится линковщиком libhybris в андроидное пространство имён;
4. со стороны glibc вызывается только этот C API.

Дальше кадр приходит как `C2GraphicBlock` поверх gralloc/ION, то есть как
dma-buf — а это ровно то, что уже умеет наш вывод слоем наложения и EGLImage.
Декодирование и показ сложились бы без единого копирования.

**Чего для этого не хватает в проекте:** Android NDK на хосте и заголовки
`frameworks/av` (сейчас склонирован только `frameworks/native`).
Потребителем логичнее всего сделать элемент GStreamer или обёртку для ffmpeg.

Оценка: это отдельный проект на несколько дней, а не вечер. Но разведка
показала главное — упирается не в железо и не в ядро, а только в границу
между libc++ bionic и glibc.

### 2026-09-16 07:47 — ядро v2 прошито и проверено

Слот A прошит образом `bf7-alps191-rootfs-v1.img` с ядром
`kernel-work/build/bf7-x657b-clang11-v2`, модули связности заменены тем же
заходом (`/root/modules-backup-v1` — копия прежних). Загрузилось с первого
раза.

```
CONFIG_POSIX_MQUEUE=y CONFIG_CGROUP_PIDS=y CONFIG_CGROUP_DEVICE=y
CONFIG_IPC_NS=y CONFIG_USER_NS=y CONFIG_NETFILTER_XT_MATCH_ADDRTYPE=y
CONFIG_USB_CONFIGFS_NCM=y
cgroup-контроллеры: cpu io memory pids      (было: cpu io memory)
ypanstep=1                                  (было: 0)
```

Что подтвердилось на живой системе:

- **`upower` работает без костыля.** Файл
  `upower.service.d/99-bf7-no-userns.conf` удалён; `PrivateUsers=yes`
  теперь проходит, потому что появился `CONFIG_USER_NS`. Батарея 100%,
  состояние «charging».
- **Готовность к контейнерам.** Из обязательного по проверялке moby осталось
  только `NETFILTER_XT_MATCH_IPVS` (это Swarm) и контроллер `cpuset`,
  которого в cgroup v2 ядра 4.19 не существует в принципе — он появился в
  5.x вместе с механизмом разделов. Контроллер `pids` появился.
- **Панорамирование фреймбуфера заработало.** Запасной путь с копированием
  больше не печатает «панорамирование недоступно» и живёт на двух страницах,
  то есть разрыв кадра ушёл и там.
- **Консоль на панели цела.** Проверено без глаз: привязали `vtcon1`,
  напечатали 60 строк, прочитали `/dev/fb0` — `yoffset=0` (значит fbcon
  остался в режиме перерисовки и не панорамирует), 7982 светлых точки цвета
  `0xaaaaaa`, то есть ровно консольный текст. Расчёт на то, что fbcon
  смотрит на `FBINFO_HWACCEL_YPAN` в `info->flags`, а не на один
  `ypanstep`, оказался верным.
- **Wi-Fi поднялся** с новыми модулями, адрес на месте.
- Сбойных юнитов нет; рабочий стол, VNC, экран, своп, топливомер — всё
  активно.

### 2026-09-16 07:48 — гонка в зеркале VNC после включения панорамирования

Побочный эффект `ypanstep=1`: запасной путь с копированием снова стал
переключать страницы, а зеркало для VNC пользуется тем же кодом. Получалось,
что кадр копировался на одну страницу, а `bf7vnc` читал видимую — в снимке
оказывался то предыдущий кадр, то остатки консольного текста.

Исправлено: когда кадр показывается слоем наложения, фреймбуфер служит
**только снимком**, поэтому копия идёт в текущую видимую страницу
(`m_var.yoffset / m_fbHeight`) и `FBIOPAN_DISPLAY` не вызывается вовсе.
Переключать страницы там незачем: на панель смотрит наложение, а не
фреймбуфер, и когерентность с дисплеем не нужна — пишет и читает процессор.

### 2026-09-16 07:58 — видеодекодинг: прослойка собрана, найдена настоящая причина

Собрана прослойка `hybris/bf7-codec2/`: `bf7c2dec.h` (простой C-интерфейс),
`bf7c2dec.cpp` (реализация поверх Codec2), `build.sh`. Компилируется NDK
r25c, заголовки Codec2 взяты из `frameworks/av` тега
`android-12.0.0_r34`, компонуется с библиотеками, снятыми с телефона.

**Ловушка с libc++.** NDK собирает libc++ в пространстве имён `__ndk1`, а
платформенная libc++ телефона — в `__1`. Мангленные имена не совпадают, и
линковка падает на `std::__ndk1::basic_string::~basic_string()` и
`android::GetCodec2BlockPool(..., std::__ndk1::shared_ptr<...>)`. В
`__config` макрос объявлен под `#ifndef`, поэтому хватает
`-D_LIBCPP_ABI_NAMESPACE=__1`; libc++ при этом не подмешивается своя
(`-nostdlib++`), берётся телефонная.

**Ловушка с CFI, и это исправление пригодится не только здесь.** Медиа-
библиотеки Android собраны с Control Flow Integrity: каждый косвенный вызов
идёт через `__cfi_slowpath`, который сверяется с теневой картой. Карту
создаёт андроидный линковщик (`__cfi_init`), а линковщик libhybris этого не
делает — проверка лезет по мусорному адресу. Симптом: SIGSEGV в
`libdl.so + 0x1164` (это середина `__cfi_slowpath`, 0x1140 + 100 байт),
вызов из `libcodec2_soft_common.so`. В `hooks.c` добавлены пустые
`__cfi_slowpath` и `__cfi_slowpath_diag`.

**Собственная оплошность.** Ранние правки `hooks.c` (хук
`__ctype_get_mb_cur_max` и свойства на всех SDK) делались прямо на
устройстве через `sed`, а не в хостовой копии. Когда я отправил на телефон
хостовый `hooks.c` с новыми хуками CFI, обе прежние правки затёрлись, и
вернулась старая поломка — падение в `__ctype_get_mb_cur_max`. Восстановлено;
теперь хостовая копия единственный источник.

После этого компонент **создаётся**, и в логе появляется внятная причина
отказа:

```
C2MtkVdec: in CreateCodec2AvcFactory
C2MtkVdec: Number of instances increased to 1(c2.mtk.avc.decoder)
C2MtkVdec: IntfImpl: name=c2.mtk.avc.decoder, mediaType=video/avc, driverIntf=proprietary
memory_c2: [MemoryDevice] ION_MEMORY
VCodec: [ProprietaryDevice] [FATAL] dlopen failed
```

#### Настоящая причина: драйвер кодека только 32-битный

`libcodec2_mtk_vdec.so` открывает по абсолютному пути
`/vendor/lib64/libvcodecdrv.so`. **Этого файла на разделе нет.** Есть:

| файл | разрядность | содержимое |
|---|---|---|
| `/vendor/lib/libvcodecdrv.so` | ELF32 | настоящий драйвер, 707 функций (`H264_HAL_*`, `H265_*`, `vp8_sw_dec_*`) |
| `/vendor/lib64/libvcodecdrv.so` | — | **отсутствует** |
| `/vendor/lib64/libvcodecdrv_header_stub.so` | ELF64 | заглушка на 3 символа, чтобы 64-битный компонент слинковался |

И службы это подтверждают: `android.hardware.media.c2@1.2-mediatek` — ELF32,
а `...-64b` — ELF64. То есть **на этом устройстве аппаратное декодирование
видео всегда шло в 32-битном процессе**, а 64-битный компонент
работоспособен лишь наполовину и обязан падать ровно так, как у нас.

#### Что это значит и куда двигаться

Хорошая новость: 32-битный режим доступен.

```
CONFIG_COMPAT=y  CONFIG_COMPAT_BINFMT_ELF=y  CONFIG_KUSER_HELPERS=y
/system/bin/linker -> /apex/com.android.runtime/bin/linker   (есть)
```

Более того, 32-битный андроидный бинарник у нас **запускается**: пробный
`android.hardware.media.c2@1.2-mediatek` дошёл до динамического линковщика и
остановился только на отсутствующей `libminijail.so` — то есть на песочнице,
а не на архитектуре.

Отсюда правильная архитектура, и она проще той, что я планировал:

1. **32-битный помощник — обычный андроидный бинарник**, собранный NDK под
   `armeabi-v7a`. Он пользуется Codec2 напрямую, без libhybris и без
   границы между libc++, потому что сам живёт в мире bionic. Ровно так же,
   как уже работает `/vendor/bin/fuelgauged`.
2. **Обмен с 64-битной стороной — юникс-сокет**: пакеты туда, дескрипторы
   dma-buf готовых кадров обратно через `SCM_RIGHTS`.
3. 64-битная сторона отдаёт кадр в наш вывод слоем наложения или в EGLImage —
   **по-прежнему без копирования**.

Собранная 64-битная прослойка при этом не выбрасывается: она остаётся рабочей
основой для 32-битного помощника, там тот же код Codec2.

### 2026-09-16 08:01 — аппаратный декодер достигнут

32-битная проба `hybris/bf7-codec2/c2probe32.cpp` (собирается `build32.sh`
под armeabi-v7a, запускается как обычный андроидный бинарник, без libhybris):

```
libcodec2_mtk_vdec.so -> 0xbc8bc727
--- c2.mtk.avc.decoder ---
    фабрика 0xf6805060
    createComponent -> 0, компонент 0xf5283260
    setListener_vb -> 0
    пул входных буферов -> 0, 0xf7b54a1c
[Err, h264_dec_setParameter] Invalid input argument
```

Последняя строка — сообщение **самого драйвера** из
`/vendor/lib/libvcodecdrv.so`. То есть путь пройден до конца: мы внутри
аппаратного кодека. Осталась обычная работа с Codec2 — задать параметры
потока перед стартом, скармливать пакеты и забирать кадры.

#### Чего не хватало: узлов binder

До этого проба падала на `Binder driver could not be opened. Terminating.`
Разбор упал ровно в конструктор компонента:

```
#08 android::C2MtkVdec::C2MtkVdec(...)+568        /vendor/lib/libcodec2_mtk_vdec.so
#09 android::C2MtkVdecFactory::createComponent(...)+498
#10 main+308                                      /root/codec2/c2probe32
```

Ядро собрано с `CONFIG_ANDROID_BINDER_IPC=y` и
`CONFIG_ANDROID_BINDERFS=y`, но узлы никто не создавал. Лечится двумя
строками:

```sh
mount -t binder binder /dev/binderfs
ln -sf /dev/binderfs/binder /dev/binder     # и hwbinder, vndbinder
```

Оформлено юнитом `bf7-binderfs.service` на самом раннем этапе загрузки.

**Это открывает не только видео.** С живым binder становятся возможны
`hwservicemanager` и вендорские HIDL-службы, а значит и gralloc4, и
разговор про `hwcomposer.mt6761.so`, который мы отложили из-за отсутствия
hwbinder.

#### Отладка андроидных падений

Полезное наблюдение: когда 32-битный андроидный бинарник падает, `crash_dump32`
из стокового раздела печатает **полный разбор стека с именами функций** в
liblog — то есть в наш `/var/log/bf7-android.log`. Для отладки блобов это
куда удобнее gdb: символы разрешаются сами.

### 2026-09-16 08:04 — находка про binder занесена в план

`docs/bf7-kernel/server-readiness.md`: отсутствие узлов binder добавлено в
исправленное, и отдельным пунктом записано, что живой binder открывает
отложенное — `hwservicemanager`, gralloc4 вместо нынешнего gralloc0 и
возвращение к `hwcomposer.mt6761.so`. Пункты про видео и про композитор
приведены к нынешнему состоянию.

### 2026-09-16 08:09 — декодер: конвейер поднимается, `start()` не возвращается

Проба расширена: помимо слушателя и пула входных буферов теперь создаётся
выходной пул кадров и сообщается компоненту.

```
createComponent -> 0, компонент 0xf5501cd0
setListener_vb -> 0
пул входных буферов -> 0
пул выходных кадров -> 0            (CreateCodec2BlockPool, аллокатор GRALLOC)
привязка пула кадров -> 0           (C2PortBlockPoolsTuning::output)
размер 1280x720 -> 0                (C2StreamPictureSizeInfo::output)
вызываю start()...
[Err, h264_dec_setParameter] Invalid input argument
```

В логе при этом `C2MtkVdec: VCodec started.` и `mtkpower_client:
perf_lock_acq`, то есть компонент дошёл до железа. Поднимается весь конвейер
MediaTek — девять потоков:

```
c2probe32  VcodecProcess  mtk.avc.decoder  decoder.preproc
ecoder.postproc  postprocasyncs1  postprocasyncs2
```

Но `start()` не возвращается.

#### Приём для отладки блобов

Зависшему процессу посылается `SIGABRT`, и аварийный обработчик Android со
стокового раздела печатает **полный разбор стека с именами функций** в наш
`/var/log/bf7-android.log`. gdb для 32-битного процесса на 64-битном хосте не
нужен:

```
#02 pc 0007fb85  libc.so (pthread_cond_wait+32)
#03 pc 00013851  libstagefright_foundation.so (ALooper::awaitResponse(...)+172)
#04 pc 000164c5  libstagefright_foundation.so (AMessage::postAndAwaitResponse(...)+344)
#05 pc 0003057d  libcodec2_mtk_vdec.so (android::C2MtkComponent::start()+276)
#06 pc 000029d8  c2probe32 (main+1692)
```

То есть `C2MtkComponent::start()` отправляет сообщение своему циклу и ждёт
ответа, а ответа нет.

Состояние потоков объясняет почему: все ждут на фьютексах, а
`mtk.avc.decoder` **крутится в пользовательском пространстве** (пустой стек
ядра). Вместе с `[Err, h264_dec_setParameter] Invalid input argument` это
складывается так: драйвер отверг параметр, поток-обработчик застрял в цикле
ожидания и не отвечает на сообщение о старте.

#### Что пробовать дальше

Набор параметров, который мы задаём, уже небеден, но явно неполон. Android в
`CCodecBufferChannel` перед работой опрашивает и задаёт заметно больше.
Порядок проверок:

1. `C2StreamMaxPictureSizeTuning::output` — в строках драйвера есть
   `VDEC_DRV_SET_TYPE_SET_TOTAL_OUTPUT_BUF_SIZE`, и «неверный аргумент»
   похож именно на неизвестный размер выходного буфера.
2. `C2StreamPixelFormatInfo::output` и `C2StreamUsageTuning::output` —
   без формата и флагов использования пул кадров нечем описать.
3. `C2StreamProfileLevelInfo::input` — профиль и уровень H.264.
4. `C2StreamBufferTypeSetting` для обоих портов.

Отдельно стоит попробовать поднять `hwservicemanager` и службу
`vendor.mediatek.hardware.mtkpower`: `perf_lock_acq` — это обращение к ней,
и хотя оно, судя по логу, вернулось, часть вендорского кода может ждать
других служб. Теперь, когда узлы binder есть, это стало возможным.

### 2026-09-16 08:38 — аппаратный декодер MediaTek запущен: пять подпорок вместо init

Цепочка Codec2 доведена до `start() -> C2_OK` на всех трёх компонентах
(`c2.mtk.avc.decoder`, `c2.mtk.hevc.decoder`, `c2.mtk.vp9.decoder`), включая
успешное выделение выходного кадра через gralloc4
(`fetchGraphicBlock -> 0 (кадр получен)`).

Висло всё по цепочке причин, каждая из которых маскировала следующую.

**1. `hwservicemanager` падал в `AccessControl::AccessControl()`.** Ядро
загружено с `security=none`, selinuxfs нет, настоящая libselinux не открывает
состояние. Написана разрешающая заглушка на девять точек входа, которые он
импортирует (`selinux_status_open`, `selinux_check_access`, `selabel_lookup`,
`selinux_android_hw_service_context_handle`, `getcon`, `getpidcon`, `freecon`,
`selinux_set_callback`, `selinux_log_callback`). Ловушка: SONAME заглушки
нельзя делать `libselinux.so` — линковщик тогда считает настоящую библиотеку
уже загруженной и `libvintf.so` не находит `security_policyvers`. Своё имя +
LD_PRELOAD: настоящая грузится следом, а наши символы перекрывают нужные.

**2. Любой клиент HIDL крутился на 100 % CPU.** `init` не запускается, значит
`/dev/__properties__` не создаётся. А `HidlServiceManagement.cpp` делает

```cpp
while (GetProperty("hwservicemanager.ready", "") != "true")
        WaitForProperty("hwservicemanager.ready", "true", 1s);
```

и без области свойств `WaitForProperty` возвращается мгновенно — выходит
плотный цикл. Настоящую область не построить: ей нужен `property_info`,
который собирает init из `*_property_contexts`. Написана подмена нижнего слоя
bionic (`__system_property_*`) с таблицей из настоящих `build.prop` плюс файл
переопределений `/vendor.local/etc/bf7.prop`. Ловушка: `PROP_VALUE_MAX` в
bionic включает завершающий ноль, буфер вызывающего ровно такой — лишний байт
в поле структуры давал переполнение стека у вызывающего.

**3. Ни одна транзакция binder не проходила (`FAILED_TRANSACTION`).** Драйвер
сказал точно: `transaction failed 29201/-95, size 44-0 line 3280`. Строка 3280
в `drivers/android/binder.c` — это

```c
if (target_node && target_node->txn_security_ctx) {
        security_cred_getsecid(proc->cred, &secid);
        ret = security_secid_to_secctx(secid, &secctx, &secctx_sz);
        if (ret) { return_error = BR_FAILED_REPLY; ... }
```

`hwservicemanager` регистрируется через `BINDER_SET_CONTEXT_MGR_EXT` с флагом
`FLAT_BINDER_FLAG_TXN_SECURITY_CTX`, то есть просит ядро прикладывать контекст
SELinux к каждой транзакции, а `security_secid_to_secctx` без LSM возвращает
`-EOPNOTSUPP`. Но `ProcessState::becomeContextManager()` при отказе
расширенного ioctl откатывается на обычный — заглушка `ioctl` отвечает
`EINVAL` на `BINDER_SET_CONTEXT_MGR_EXT` (0x4018620d), и всё заработало.

**4. Причины не было видно.** `logd` без init не получает свои сокеты, весь
вывод ALOGE терялся. Написан перехват нижнего слоя liblog в stderr — именно он
показал и `gralloc-mapper is missing`, и `Waited one second for IMtkPerf`.

**5. 32-битная bionic отказалась запускаться.** «`32 bit bionic libc only
accepts pid <= 65535, but current pid is 74768`»: pid хранится внутри
`pthread_mutex_t`. systemd поднимает `kernel.pid_max` до 4194304. Закреплено
`/etc/sysctl.d/90-bf7-pid-max.conf` → 65536. Аппаратный декодер существует
только в 32 битах, так что это постоянное требование.

Попутно: `vndservicemanager` надо запускать с аргументом `/dev/vndbinder`,
иначе он садится на `/dev/binder`; библиотеки VNDK лежат в
`/apex/com.android.vndk.v31/lib64`, и без `ld.config.txt` линковщик их не
находит — путь задаётся через `LD_LIBRARY_PATH`.

**Служба питания не нужна.** `vendor.mediatek.hardware.mtkpower@1.0-service`
поднимается и регистрирует `IMtkPerf`, но её поток `mtkScnHanler` жжёт целое
ядро: `checkFPSupdate()` → `getFPSvalue()` в `/vendor/lib64/libpowerhal.so`
опрашивает частоту кадров без пауз. Вместо службы декодеру подставляется наша
заглушка клиента `/vendor.local/lib/libmtkperf_client_vendor.so` — теряется
только подъём частот, декодер работает.

Итоговый набор: `hwservicemanager` + `vndservicemanager` + gralloc4. Скрипт
запуска — `/usr/local/bin/hal-start.sh`, исходники подпорок в
`hybris/bf7-codec2/{selinux-stub,propstub,logstub,binderstub}.c`.

Следующий шаг — скормить настоящий поток H.264 и забрать декодированный кадр.

### 2026-09-16 08:42 — аппаратное декодирование H.264 работает

`hybris/bf7-codec2/c2dec32.cpp` — 32-битный декодер: разбирает Annex-B на
единицы доступа, подаёт их в `c2.mtk.avc.decoder` и собирает кадры через
слушателя. Тестовый поток 640×480, 30 кадров, сгенерирован ffmpeg
(Constrained Baseline):

```
поток test640.h264: 22682 байт, 35 NAL, 32 единиц доступа, кадр 640x480
start -> 0
подано единиц доступа: 32
  кадр 1: 640x480, смещение 0,0, pts 33333, dma-buf fd 197
  ...
  кадр 30: 640x480, смещение 0,0, pts 1033323, dma-buf fd 21
ожидание: готово, завершено работ 33, кадров 30, конец потока да
ИТОГ: декодировано кадров 30
```

Весь прогон, включая запуск процесса и разбор потока, — 0,27 с. У каждого
кадра свой dma-buf, то есть кадр можно отдать дальше без копирования.

Тонкости, которые пришлось учесть:

- параметры последовательности и картинки (NAL 7 и 8) идут отдельной посылкой
  с `FLAG_CODEC_CONFIG`, иначе декодер не настраивается;
- новая единица доступа начинается на срезе (NAL 1 или 5), если срез в текущей
  уже был;
- последняя посылка помечается `FLAG_END_OF_STREAM`, иначе последние кадры
  остаются внутри декодера;
- выходной пул кадров создаёт клиент и сообщает его идентификатор через
  `C2PortBlockPoolsTuning::output` — компонент сам его не заводит.

### 2026-09-16 08:42 — что мешает завести это в браузере

Проверил, чем вообще умеет пользоваться наш браузер.

**У Chromium из Debian для arm64 нет VA-API вообще.** В двоичном файле
`/usr/lib/chromium/chromium` нет ни одной строки `libva`, нет ни одного файла
`media/gpu/vaapi/*.cc`. Единственная строка `VaapiVideoDecoder` — это имя
флага, а не код. То есть собран с `use_vaapi=false`, и писать драйвер libva
для него бессмысленно.

**Зато вкомпилирован V4L2:** `media/gpu/v4l2/v4l2_video_decoder.cc`,
`V4L2VideoDecoderBackendStateful`, `V4L2VideoDecoderBackendStateless`,
`V4L2VideoDecoderDelegateVP9`. Это путь ChromeOS/ARM. Он даже лучше подходит к
Codec2: V4L2 stateful принимает целые единицы доступа и отдаёт кадры — ровно
как наш декодер.

Но требуется **устройство `/dev/videoN` с V4L2 M2M**, то есть модуль ядра.
Свой драйвер к железу написать нельзя: вендорский `/dev/Vcodec` — это тонкая
прослойка, а вся логика кодека живёт в 32-битной `libvcodecdrv.so`. Значит
нужен модуль-посредник: регистрирует V4L2 M2M, а работу отдаёт нашему
32-битному демону.

И есть ещё одно препятствие, более раннее: **декодер V4L2 у Chromium живёт в
процессе GPU**, а у нас X-клиенты вообще без OpenGL (Xwayland:
`GBM backend is not available`, `Failed to initialize glamor`). Пока процесс
GPU в Chromium не поднимется, аппаратный декодер он не включит, даже если
устройство появится.

Итого до «работает в браузере» два больших этапа: GL/GBM для X-клиентов и
модуль-посредник V4L2. Оба — отдельные работы, а не доводка сегодняшней.

### 2026-09-16 09:10 — аппаратный GL для клиентов Wayland

libhybris пересобран с `--enable-wayland`. Появились платформа
`eglplatform_wayland.so` и серверная часть `server_wlegl`, композитор теперь
объявляет глобальный объект `android_wlegl` (версия 2). Проверено своим
минимальным клиентом `hybris/bf7-gpu/wlgltest.c`:

```
подключились к композитору
композитор объявляет android_wlegl (версия 2)
EGL 1.5 / EGL_VENDOR: Android
EGL_WL_bind_wayland_display у клиента: есть
GL_VENDOR:   Imagination Technologies
GL_RENDERER: PowerVR Rogue GE8300
GL_VERSION:  OpenGL ES 3.2 build 1.15@6070602
отрисовано кадров: 120
```

То есть приложение рисует на GPU и отдаёт композитору буфер GPU, без
разделяемой памяти и без загрузки текстуры на каждый кадр.

Для сборки понадобился пакет `libwayland-egl-backend-dev` (заголовок
`wayland-egl-backend.h`; `libwayland-dev` его не ставит), и ко всей сборке
принудительно подключается `/root/hybris/android-compat.h`: файлы
`server_wlegl*` компилируются впервые и тянут заголовки NDK с атрибутами
Clang (`__INTRODUCED_IN`, `_Nonnull`), которых gcc не понимает.

### 2026-09-16 09:10 — дыра в libhybris: нижний слой свойств

При первом же перезапуске сеанса композитор завис на 100 % CPU и не создал
сокет Wayland. Рабочий стол лежал, пока причина не нашлась; резервная копия
установленного libhybris снималась заранее и пригодилась.

Виновата оказалась не пересборка, а более ранняя работа — монтирование
binderfs. `HidlServiceManagement` проверяет наличие `/dev/hwbinder`. Пока узла
не было, блобы пропускали путь HIDL целиком. Сеанс, работавший с 07:44,
стартовал до монтирования binderfs в 08:00 и поломки не видел. Первый же
перезапуск после этого дал:

```
main W HidlServiceManagement: Waited for hwservicemanager.ready for a second, waiting another...
```

и так десятки тысяч раз в секунду — журнал `/var/log/bf7-android.log` разбух
до 257 МБ.

Корень: начиная с Android 8 `libbase` читает свойства не через
`property_get`, а парой `__system_property_find` + `__system_property_read_callback`.
В libhybris `__system_property_find` был заглушкой, возвращавшей NULL, а
`read_callback` не перехватывался вовсе — `GetProperty` всегда отдавала
значение по умолчанию. Само по себе тихо, но в паре с

```cpp
while (GetProperty("hwservicemanager.ready", "") != "true")
        WaitForProperty("hwservicemanager.ready", "true", 1s);
```

и с `__system_property_wait`, возвращавшимся мгновенно, получался плотный
цикл. Написан настоящий нижний слой: таблица описателей, `find` ищет значение
обычным путём и отдаёт устойчивый указатель, `read_callback` возвращает имя и
значение, `wait` честно выжидает свой таймаут. Плюс исправлен
`__system_property_read`: он принимал выходной параметр `name` за ключ.
Добавлены перехваты `__system_property_read_callback` и
`__system_property_area_serial`.

Свойство `hwservicemanager.ready=true` дописано в `/system.local/build.prop` —
именно оттуда libhybris берёт значения. Блоб теперь доходит до
`getService()`, получает `DEAD_OBJECT` (диспетчер не запущен) и спокойно идёт
дальше.

### 2026-09-16 09:10 — Chromium: клиентом Wayland поднимается, процесс GPU падает

С `--ozone-platform=wayland` Chromium 152 подключается к нашему композитору,
видит выход 1075×480 и запускает процесс GPU. Но:

- `--use-gl=egl` отвергается: «Requested GL implementation (gl=egl-gles2,
  angle=none) not found in allowed implementations: [(gl=egl-angle,
  angle=opengl), (gl=egl-angle,angle=opengles), (gl=egl-angle,angle=vulkan)]».
  Эта сборка умеет только через ANGLE;
- с `--use-angle=gles` процесс GPU падает по SIGSEGV. Дамп памяти показывает
  адрес в `/vendor/lib64/egl/libGLES_meow.so +0x994bc` — вендорская обёртка
  GLES от MediaTek, символов там нет. ANGLE обращается с EGL не так, как
  обычное приложение: безповерхностный контекст, вызовы из нескольких потоков.

Запасной путь — бэкенд ANGLE на Vulkan. Драйвер `vulkan.mt6761.so` на месте,
libhybris собрал свой загрузчик `libvulkan.so.1.2.183` и
`vulkanplatform_wayland.so`. После добавления `ro.hardware.vulkan=mt6761` в
`/system.local/build.prop` загрузчик действительно грузит драйвер
(`vndksupport: Loading vulkan.mt6761.so`, `vulkan: searching for layers`), но
`vkEnumeratePhysicalDevices` возвращает **ноль устройств** без единой ошибки.
Открытый вопрос.

Проверка: `hybris/bf7-gpu/vktest.c`.

### 2026-09-16 09:36 — паника ядра: mmap звука в драйвере MediaTek

Телефон перезагрузился во время проверки видео в Firefox. Причина найдена
точно, журнал сохранён в
`docs/bf7-kernel/crashes/2026-09-16-0931-snd_pcm_mmap_data_fault.log`.

```
Unable to handle kernel paging request at virtual address ffffffbf00000008
Internal error: Oops: 96000005 [#1] PREEMPT SMP
pc : snd_pcm_mmap_data_fault+0x7c/0xb0
Comm: cubeb r~ thread
Call trace:
 snd_pcm_mmap_data_fault
 __do_fault
 handle_mm_fault
 do_page_fault
 el0_da
```

`cubeb` — звуковая библиотека Firefox. Она отображает буфер ALSA в память и
трогает его; ядро идёт в `snd_pcm_mmap_data_fault`, тот зовёт
`substream->ops->page`, а у MediaTek это:

```c
static void *dummy_page[2];          /* нигде не инициализируется */

static struct page *mtk_pcm_page(struct snd_pcm_substream *substream,
                                 unsigned long offset)
{
        return virt_to_page(dummy_page[substream->stream]); /* the same page */
}
```

`dummy_page[]` остаётся нулевым, `virt_to_page(NULL)` даёт мусорный указатель
`0xffffffbf00000000` (он же `x0` в регистрах), а `get_page()` по нему роняет
ядро. При этом драйвер объявляет
`SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID`, то есть сам приглашает
приложения отображать буфер.

**Так во всех 29 драйверах PCM** в `sound/soc/mediatek/common_int/`: везде
`dummy_page` объявлен и ни разу не присвоен. В Android это не срабатывало —
его звуковой HAL пользуется обычной записью (`.copy_user = mtk_pcm_copy`), а
не отображением. Любое приложение glibc, играющее звук через mmap ALSA, роняет
телефон. Это, по сути, локальный отказ в обслуживании без всяких привилегий.

**Как это удалось достать.** В `/sys/fs/pstore` было пусто, и сперва показалось,
что записи нет. На самом деле её забрал `systemd-pstore.service`, который при
загрузке переносит содержимое в `/var/lib/systemd/pstore/`. Там и лежал
`console-ramoops-0` на 212 КБ с полной паникой. Запомнить на будущее.

Обманки по дороге: `androidboot.bootreason=kernel_panic` в командной строке
остаётся от прошлой паники и был там ещё до этого падения; `charger_thread`
каждые пять секунд печатает `Call trace` из-за `can't find primary charger` —
это хроническое и к падению отношения не имеет; ошибки `[RXD][ERROR]` контроллера
USB случались, но последние за четыре минуты до падения.

**Что делать.** В следующую сборку ядра: убрать `SNDRV_PCM_INFO_MMAP` и
`SNDRV_PCM_INFO_MMAP_VALID` из `snd_pcm_hardware.info` этих драйверов — тогда
ALSA откажет в отображении и приложения перейдут на обычную запись, которая
работает; плюс подстраховать `mtk_pcm_page`, чтобы он возвращал NULL вместо
мусора (приложение получит SIGBUS, ядро останется живым). До пересборки звук
в браузере включать нельзя.

### 2026-09-16 09:47 — правка ядра готова, звук проверен обычной записью

**Временная защита.** До прошивки звук закрыт с двух сторон: устройство ALSA
по умолчанию подменено на пустое (`/etc/asound.conf`) и снят доступ к узлам
(`/usr/local/bin/snd-guard.sh off`, возврат — `on`). Firefox запущен
пользователю как обычное приложение X и телефон не роняет.

**Звук по обычному пути работает.** Проверено безопасно: `aplay` использует
`snd_pcm_writei`, а не отображение.

```
access : RW_INTERLEAVED, format : S16_LE, rate : 44100
buffer_size : 9216, period_size : 2304
```

Тон 440 Гц проигран, ядро живо. То есть тракт исправен, ломается только
отображение. Чтобы звук дошёл до динамика, останется включить маршрутизацию
кодека: в микшере есть `Audio_Amp_L_Switch`, `Audio_Amp_R_Switch`,
`Lineout_PGAL_GAIN`/`Lineout_PGAR_GAIN` — их выставляет звуковой HAL Android,
у нас этого никто не делает. Отдельная небольшая задача.

**Правка ядра** — коммит `51c7ad7c4` в `kernel-work/x657b-4.19`, 33 файла:

- из `snd_pcm_hardware.info` убраны `SNDRV_PCM_INFO_MMAP` и
  `SNDRV_PCM_INFO_MMAP_VALID` (29 файлов с битым `.page` плюс четыре
  голосовых, у которых `.page` нет вовсе и ядро пошло бы в
  `virt_to_page(dma_area)` — там буфер тоже не из линейной карты);
- сняты четыре отдельных `runtime->hw.info |= SNDRV_PCM_INFO_MMAP_VALID;`;
- `mtk_pcm_page()` теперь возвращает NULL, если `dummy_page` пуст, — ядро
  ответит `VM_FAULT_SIGBUS`, и в худшем случае умрёт приложение.

Приложения после этого переходят на обычную запись, которая и так работает.

Собрано: ядро `build/bf7-x657b-clang11-v2`
(`Image` sha256 `5b84bd26…`), модули связности пересобраны тем же заходом
(правило `bf7-kernel-rebuild-rule`), boot-образ
`native-linux-build/alps191-rootfs-v1/bf7-alps191-rootfs-v1.img`
(sha256 `26480cf1…`). **Не прошито — ждёт разрешения.**

### 2026-09-16 16:06 — ядро с починкой звука прошито; две мины по дороге

Прошито в слот A, система вернулась за минуту. Проверка починки:

```
aplay    -D hw:0,0 tone.wav   -> код 0        (обычная запись, работала и раньше)
aplay -M -D hw:0,0 tone.wav   -> "Access type not available"
```

Раньше вторая строка роняла телефон. Теперь ALSA честно отказывает, ядро живо.
Итоговое состояние: `systemctl is-system-running` = **running**, сбойных
юнитов ноль, Wi-Fi со всеми тремя модулями, своп zram 1,9 ГБ, рабочий стол и
браузер на месте.

**Мина первая: скрипт модулей связности собирал против не того ядра.**
В `scripts/build-bf7-connectivity-x657b.sh` переменная `KOUT` по умолчанию
указывала на `build/bf7-x657b-clang11-v1`, а ядро у нас давно **v2**. Сборка
проходит молча, установка тоже, а на загрузке:

```
insmod: ERROR: could not insert module .../wmt_drv.ko: Invalid module format
```

`bf7-wifi.service` виснет в `activating`, держит `multi-user.target`, sshd не
стартует — снаружи выглядит как незагрузившееся ядро. Спасла аварийная консоль
на порту 2324 (`systemctl list-jobs` → `systemctl stop bf7-wifi`).
Исправлено: значение по умолчанию переведено на v2, и скрипт теперь печатает,
против какого ядра собирает.

Осадок: даже после пересборки против v2 `wlan_drv_gen4m.ko` всё равно даёт
`Invalid module format`, хотя `wmt_drv` и `wmt_chrdev_wifi` встают. Рабочим
остаётся комплект из `/root/modules-v2` — он и возвращён. Разобраться отдельно.

**Мина вторая: `rmmod` вендорских модулей роняет ядро.**
Попытка перезагрузить драйверы без перезагрузки системы:

```
Internal error: Oops: 96000005 [#1] PREEMPT SMP
Comm: rmmod
pc : klist_del+0x1c/0x84
```

Телефон перезагрузился. Модули связности MediaTek выгружать нельзя — только
перезагрузка целиком.

**Мелочь:** `bf7-zram.service` падал с `swapon: Device or resource busy` —
генератор systemd успевает включить своп первым. Добавлен drop-in, который
считает успехом сам факт включённого свопа.

### 2026-09-16 16:12 — Vulkan заработал: дело было в путях поиска библиотек HAL

```
vkCreateInstance -> 0
устройств: 1 (код 0)
  [0] PowerVR Rogue GE8300
      Vulkan 1.1.170, драйвер 0x5ca14a, вендор 0x1010, тип 1
```

Причина оказалась не в драйвере и не в обёртке libhybris, а в **андроидном
загрузчике** `/system/lib64/libvulkan.so`. Его `LoadDriver()` собирает
**голое** имя `vulkan.<железо>.so` (по `ro.hardware.vulkan`, затем по
`ro.board.platform`) и отдаёт в `android_load_sphal_library()`. Пространства
имён `sphal` у нас нет — нет `/system/etc/ld.config*.txt` и `/linkerconfig`, —
поэтому `libvndksupport` делает обычный `dlopen("vulkan.mt6761.so")` через
линковщик libhybris. А в путях по умолчанию (`kDefaultLdPaths` в
`hybris/common/q/linker.cpp`) значатся только `/system/lib64`, `/odm/lib64`,
`/vendor/lib64` и два apex — **каталога `/vendor/lib64/hw`, где лежит драйвер,
там нет**.

Дальше самое неприятное: на провал загрузки андроидный загрузчик **не ругается**,
а тихо подставляет заглушку (`stubhal::kDevice`, в строках библиотеки видно
`vkstub`, `invalid stub function called`). Она возвращает `VK_SUCCESS` и
`count = 0`. Отсюда «ноль устройств без единой ошибки».

Единственный внешний признак был в stderr, а не в `/var/log/bf7-android.log`:
`library "vulkan.mt6761.so" not found` — два раза, по одному на каждое
проверенное свойство.

Для сравнения: gralloc грузится нормально, потому что libhardware передаёт
**полный путь** (`Loading /vendor/lib64/hw/gralloc.rogue.so`). Голое имя
передаёт только загрузчик Vulkan.

**Исправлено насовсем** в `hybris/common/q/linker.cpp`: в `kDefaultLdPaths`
добавлены `/vendor/lib64/hw` и `/odm/lib64/hw` (и 32-битные варианты).
libhybris пересобран и установлен, проверено **без** всякой предзагрузки.
Регрессии нет: клиент Wayland по-прежнему получает `PowerVR Rogue GE8300`,
GL ES 3.2.

Что это открывает: у ANGLE есть бэкенд на Vulkan (путь к Chromium), а **Zink**
даёт поверх Vulkan настоящий десктопный OpenGL — то есть приложения, которым
нужен GL, а не GLES.

Расширения инстанса настоящие, 11 штук, включая `VK_KHR_wayland_surface` —
подмена android→wayland в слое libhybris работает.

### 2026-09-16 16:12 — разведка: KMS для дисплея MT6761, два пути

**Полный порт `drivers/gpu/drm/mediatek/` — недели работы.** В нашем дереве это
вендорский форк, и в `mtk_drm_of_ids[]` есть mt2701, mt2712, mt6779, mt8173,
mt6885, mt6873, mt6853, mt6877, mt6833, mt6781 — **mt6761/6765/6768 нет ни
строкой**, mt8183 тоже нет. Хуже того, наш `arch/arm64/boot/dts/mediatek/mt6761.dts`
использует легаси-имена (`mediatek,disp_ovl0`, `mediatek,mdp_rdma0`,
`mediatek,mm_mutex`), которых драйвер не сопоставляет вообще; у disp-узлов
**нет свойств `clocks`** (тактирование делается внутри вендорского драйвера
через scpsys/mtcmos); **узла панели нет вовсе** — имя LCM приходит строкой из
тега загрузчика (`mtkfb.c`, `videolfb_tag->lcmname`). То есть это переписывание
дисплейного фрагмента DT с нуля плюс новый `mt6761_mmsys_driver_data` с
выверенными офсетами регистров плюс драйвер панели. По набору блоков ближе
всего mt2701/mt2712 (то самое поколение с `shadow_register = true`), но
совпадение только по составу, не по биндингу.

**Минимальная прослойка — на порядок дешевле и это рекомендуемый путь.**
Один платформенный драйвер на 500–1000 строк: один CRTC, один connector без
hotplug (панель несъёмная), один primary plane, а вместо работы с регистрами —
вызовы уже проверенного вендорского тракта:

- `primary_display_init()`, `primary_display_get_width/height/bpp()` — геометрия;
- `primary_display_frame_cfg(struct disp_frame_cfg_t *)` и
  `primary_display_config_input_multiple()` — конфигурация слоя из `.atomic_flush`;
- `primary_display_trigger()` — запуск кадра;
- `primary_display_wait_for_vsync()` — вертикальная синхронизация.

Образец такой архитектуры в самом ядре — `drivers/gpu/drm/udl/`: свой
CRTC/encoder/connector, а вывод кадра отдаётся внешнему тракту. Каталога
`drivers/gpu/drm/tiny/` и `simpledrm` в 4.19 ещё нет.

Ловушка при линковке: ни одна `primary_display_*` не помечена `EXPORT_SYMBOL`,
а `CONFIG_MTK_FB` — это `bool`, не `tristate`. Значит прослойку проще всего
добавить тем же `.o` в `drivers/misc/mediatek/video/mt6765/videox/Makefile` и
собрать внутрь ядра; иначе придётся дописывать `EXPORT_SYMBOL_GPL`.

Конфиг к этому почти готов: `CONFIG_DRM=y`, `CONFIG_DRM_KMS_HELPER=y`,
`CONFIG_DRM_GEM_CMA_HELPER=y`, `CONFIG_DRM_MIPI_DSI=y`, `CONFIG_DRM_PANEL=y`.
Не хватает `CONFIG_DRM_FBDEV_EMULATION`, если понадобится эмуляция fbdev.
`CONFIG_DRM_MEDIATEK` выключен закономерно — он всё равно не сопоставится.

### 2026-09-16 16:13 — ANGLE на Vulkan: дошёл до драйвера, упирается в одно расширение

С `--use-angle=vulkan` Chromium впервые **доходит до драйвера** (раньше не мог
получить даже дисплей EGL) и отказывает уже осмысленно:

```
ANGLE Display::initialize error 0: Internal Vulkan error (-9):
The requested version of Vulkan is not supported by the driver or is otherwise
incompatible ... vk_renderer.cpp, initialize:2646
```

Версия ни при чём: наш загрузчик объявляет `vkEnumerateInstanceVersion -> 1.1.0`
и принимает `vkCreateInstance` с любым `apiVersion` от 1.0 до 1.3 (проверка —
`hybris/bf7-gpu/vkver.c`). `VK_ERROR_INCOMPATIBLE_DRIVER` тут означает, что
ANGLE отверг само физическое устройство.

Перечисление расширений устройства (`/tmp/vkdevext.c`) показывает, что почти всё
нужное есть:

| расширение | |
|---|---|
| `VK_KHR_swapchain` | есть |
| `VK_KHR_external_memory_fd` | есть |
| **`VK_EXT_external_memory_dma_buf`** | **есть** |
| `VK_KHR_external_semaphore_fd` | есть |
| `VK_KHR_image_format_list` | есть |
| `VK_EXT_queue_family_foreign` | есть |
| `VK_KHR_maintenance1` | есть |
| **`VK_EXT_image_drm_format_modifier`** | **НЕТ** |

Не хватает ровно одного — `VK_EXT_image_drm_format_modifier`. На Linux ANGLE
опирается на него при импорте dma-buf с модификаторами, и без него бракует
устройство.

**Отдельно очень важное:** `VK_EXT_external_memory_dma_buf` у драйвера **есть**.
То есть ограничение «наш стек не умеет импортировать dmabuf», верное для
блоба EGL, **на стороне Vulkan не действует**. Это меняет расклад для
прослойки KMS/GBM и для Zink: обмен буферами можно строить через Vulkan.

Следующий шаг по этой ветке — выяснить, можно ли заставить ANGLE не требовать
`VK_EXT_image_drm_format_modifier` (у ANGLE есть переопределения возможностей,
у Chromium — соответствующие ключи), либо идти путём Zink.

### 2026-09-16 16:14 — разведка: как снять переменные окружения через glvnd

Сейчас клиенты Wayland получают GPU только с полным набором переменных
(`LD_LIBRARY_PATH`, `EGL_PLATFORM`, `LIBEGL`, `LIBGLESV2`, `LD_PRELOAD`).
Выяснилось, что почти всё это снимается штатно.

**Главное: код поставщика glvnd в libhybris уже написан** —
`hybris/egl/glvnd/eglglvnd.cpp`, `egldispatchstubs.c`, шаблон
`10_libhybris.json.in`. Просто наша сборка сделана **без** `--enable-glvnd`
(в `config.log` `WANT_GLVND_TRUE='#'`, `DEFAULT_EGL_PLATFORM='null'`).

В системе glvnd настоящий: пакет `libglvnd0` 1.6.0, системная
`/usr/lib/aarch64-linux-gnu/libEGL.so.1` тянет `libGLdispatch.so.0` и ищет
поставщиков в `/etc/glvnd/egl_vendor.d` и `/usr/share/glvnd/egl_vendor.d`.
Зарегистрирован пока один — `50_mesa.json`. Наша `/usr/local/lib/libEGL.so.1`
не экспортирует `__egl_Main`, то есть поставщиком не является: она просто
перебивает системную через `LD_LIBRARY_PATH`, в обход glvnd целиком.

Что нужно сделать:

1. Пересобрать с `--enable-glvnd --with-default-egl-platform=wayland`.
   Единственная недостающая зависимость — пакет `libglvnd-core-dev`
   (`egl.pc` и `glesv2.pc` уже свои, от libhybris).
2. Дать `configure` ещё и `--datarootdir=/usr/share`. Иначе при
   `--prefix=/usr/local` файл поставщика ляжет в
   `/usr/local/share/glvnd/egl_vendor.d/`, куда диспетчер **не смотрит**.
   Имя `10_libhybris.json` по алфавиту раньше `50_mesa.json` — наш поставщик
   будет опрошен первым.
3. `LD_LIBRARY_PATH` после этого не нужен: библиотека получает уникальное имя
   `libEGL_libhybris.so.0`, конфликта имён с системной нет, а `/usr/local/lib`
   и так в `ld.so.conf.d`.
4. `EGL_PLATFORM` тоже не нужен: в режиме glvnd приложение зовёт
   `eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, …)`, платформа приходит
   аргументом.
5. **`LIBEGL`/`LIBGLESV2` без правки исходников не снять.** В `egl.c:87-88`
   дефолты — `"libEGL.so"`/`"libGLESv2.so"`, а наши блобы называются
   `libEGL_mtk.so`/`libGLESv2_mtk.so` и лежат в подкаталоге `egl/`. Правка
   мелкая: завести опции `--with-default-libegl=`/`--with-default-libglesv2=`
   по образцу уже существующей `--with-default-egl-platform`.
6. **Сторож TLS.** Проверены реальные клиенты: у `xfce4-panel`,
   `xfce4-session`, `xfce4-terminal`, `thunar`, `xfconfd` сегмента `PT_TLS`
   нет вовсе, у `firefox-esr` он есть, но с `memsz=0`. То есть сторож ложится
   в нужное смещение 16..272. Снять переменную полностью можно только через
   `/etc/ld.so.preload` — но он действует на **все** процессы системы, включая
   systemd и sshd. Без отдельного разрешения не делать.
7. Qt6 и GTK3 к пути Wayland готовы (плагины на месте), переменные
   `GDK_BACKEND`/`QT_QPA_PLATFORM` избыточны при автоопределении. **Qt5 не
   пойдёт**: пакет `qtwayland5` не установлен вовсе.

Осторожность при внедрении: сборка с `--enable-glvnd` меняет раскладку —
библиотека становится `libEGL_libhybris.so.0`, и нынешняя
`/usr/local/lib/libEGL.so.1`, на которой держится работающий композитор, может
исчезнуть или устареть. Это ровно та мина с отложенным взрывом, что уже
сработала сегодня с binderfs: всё живое продолжает работать, а ломается при
следующем перезапуске сеанса. Делать только с полной резервной копией
`/usr/local/lib` и немедленной проверкой перезапуском сеанса.

### 2026-09-16 16:16 — разведка: маршрутизация звука, аналоговый тракт включается

Разобрано по `sound/soc/mediatek/codec/mt6357/mtk-soc-codec-6357.c`. Имена
элементов микшера обманчивы:

| элемент | что на самом деле |
|---|---|
| `Audio_Amp_L/R_Switch` | питание аналогового выхода **наушников** внутри PMIC MT6357 (cap-less LDO, NV −1.2 В, зарядовый насос, ЦАП) |
| `Speaker_Amp_Switch` | питание внутреннего драйвера **LOL** (линейный выход на динамик) той же PMIC плюс переключение мультиплексора LOL на ЦАП |
| `Lineout_PGAL/R_GAIN` | усиление **колоночного** тракта (таблица `DAC_DL_PGA_Speaker_GAIN`, регистр ZCD_CON1), а не «линейного выхода» |
| `Headset_PGAL/R_GAIN` | то же для наушников |
| `AUD_CLK_BUF_Switch` | буфер тактовой частоты PMIC, нужен обоим трактам, со счётчиком ссылок |
| `Ext_Speaker_Amp_Switch` | должен дёргать GPIO внешнего усилителя, но состояний pinctrl `extamp-pullhigh/pulllow` в нашем device tree **нет** (`fail -19`) — сейчас это пустышка |

**Подтверждение не на слух, а по регистрам PMIC** (через
`/sys/kernel/debug/regmap/1000d000.pwrap:main_pmic-pmic_ipi/registers`, снимок
до и после):

- `Audio_Amp_L/R_Switch=On` → `AUDDEC_ANA_CON0` 0x3000→0x3aff,
  `CON12` 0x0000→**0x1055** (буквально то значение, которое пишет
  `Ana_Set_Reg(AUDDEC_ANA_CON12, 0x1055, ...)`), `CON13` →0x0001,
  `NCP_CON3` 0x0001→0x0000;
- `Speaker_Amp_Switch=On` → `CON4` (драйвер LOL) 0x0010→**0x011b**, побитно то
  же, что драйвер пишет при переключении мультиплексора LOL на ЦАП;
  `CON6` →0x0201, `CON11` →0x0001, `CON0` →0x3009.

То есть аналоговый тракт внутри PMIC действительно включается ровно так, как
задумано в драйвере.

**Почему динамик всё равно может молчать.** На шине i2c-3 по адресу 0x34 висит
чип `speaker_amp` (`compatible = "mediatek,speaker_amp"`). Драйвер
`mtk-soc-speaker-amp.c` — это диспетчер: он пробует опознать Richtek RT5509,
TI TAS5782M, MediaTek MT6660, и если ни один драйвер не собран (а в нашем ядре
не собран ни один), **тихо и «успешно» откатывается на пустышку**, ни разу не
обратившись к микросхеме по I2C. Вдобавок GPIO `aud_smartpa_reset/on/off` в
device tree тоже отсутствуют. Значит внешний усилитель никогда не выводится из
сброса и не размучивается — это дыра в порте, одним `amixer` не закрывается.
Кандидаты по `SmartPa_AudioParam.xml` из `/vendor`: NXP TFA9887/9890/9874,
Maxim 98926, Richtek RT5509, Cirrus CS35L35/41.

**Наушники от внешней микросхемы не зависят** и с высокой вероятностью уже
работают.

Оставлено на устройстве: `/root/audio-mixer-before.txt` (снимок микшера до
правок) и `/root/bf7-audio-route.sh` — скрипт включения тракта с умеренной
громкостью (0 дБ из диапазона +8…−40) и пояснениями по каждому элементу.
В автозапуск не прописан.

### 2026-09-16 16:25 — переход на glvnd: собрано, но цель не достигнута

Сделано:

- перед началом снята полная копия `/usr/local/lib` →
  `/root/usrlocal-backup-latest` (23 МБ);
- установлен `libglvnd-core-dev` (единственная недостающая зависимость);
- в `configure.ac`, `egl/Makefile.am` и `egl/egl.c` добавлены опции
  `--with-default-libegl=` и `--with-default-libglesv2=` по образцу уже
  существующей `--with-default-egl-platform`. Теперь пути к андроидным
  библиотекам зашиваются при сборке, и переменные `LIBEGL`/`LIBGLESV2` не
  нужны;
- libhybris пересобран с
  `--enable-glvnd --with-default-egl-platform=wayland --datarootdir=/usr/share
   --with-default-libegl=/vendor/lib64/egl/libEGL_mtk.so
   --with-default-libglesv2=/vendor/lib64/egl/libGLESv2_mtk.so`.

Результат сборки правильный: `/usr/local/lib/libEGL_libhybris.so.0`
экспортирует `__egl_Main`, грузится вручную через `dlopen` без ошибок, все
зависимости на месте, ABI совпадает (major 0 с обеих сторон), файл поставщика
`10_libhybris.json` сгенерирован и лёг в `/usr/share/glvnd/egl_vendor.d/`
раньше `50_mesa.json` по алфавиту. Строки `wayland`,
`EGL_EXT_platform_wayland` и наш `/vendor/lib64/egl/libEGL_mtk.so` в
библиотеке присутствуют, то есть `WANT_WAYLAND` определён и дефолты зашиты.

**Но диспетчер в наш поставщик не маршрутизирует.** Даже с
`__EGL_VENDOR_LIBRARY_FILENAMES`, указывающим только на наш json:

```
eglGetDisplay            -> (nil), ошибка 0x3000
eglGetPlatformDisplayEXT -> есть
  (WAYLAND_KHR)          -> (nil), ошибка 0x300c
eglGetPlatformDisplay    -> (nil), ошибка 0x300c
```

Добавленная в `__eglHybrisGetPlatformDisplayCommon` трассировка (переменная
`BF7_EGL_TRACE`) **не печатается ни разу** — функция не вызывается вообще.
Значит `EGL_BAD_PARAMETER` возвращает сам libglvnd, не найдя поставщика для
платформы, а не наш код. При этом `__eglGLVNDGetVendorString` на
`__EGL_VENDOR_STRING_PLATFORM_EXTENSIONS` честно отдаёт
`EGL_KHR_platform_android EGL_EXT_platform_wayland EGL_KHR_platform_wayland`.
Нужен разбор отбора поставщиков внутри libglvnd 1.6 — задача на отдельный
заход.

**Состояние оставлено безопасным:**

- старая автономная `/usr/local/lib/libEGL.so.1` не тронута (сборка с glvnd
  делает библиотеку с другим именем и её не перезаписывает), поэтому привычный
  путь через `LD_LIBRARY_PATH` работает как работал;
- сеанс перезапущен и проверен: стол, Xwayland, Xfce на месте; клиент Wayland
  получает `PowerVR Rogue GE8300`; Vulkan отдаёт устройство;
- файл поставщика **намеренно отключён** (`10_libhybris.json.disabled`, рядом
  пояснение в `README.bf7`). Пока он активен, glvnd затягивает libhybris и
  андроидные блобы в каждый процесс, использующий EGL, — лишняя нагрузка и
  риск без пользы. Проверено: с отключённым файлом обычные приложения снова
  получают Mesa, как и раньше.

Что из задуманного всё-таки закрыто: переменные `LIBEGL` и `LIBGLESV2` больше
не нужны — они зашиты в сборку. Остаются `LD_LIBRARY_PATH`, `EGL_PLATFORM`
и предзагрузка сторожа TLS.

### 2026-09-16 16:35 — glvnd заработал: клиенты Wayland получают GPU без единой переменной

```
EGL_VENDOR:  Imagination Technologies
GL_VENDOR:   Imagination Technologies
GL_RENDERER: PowerVR Rogue GE8300
GL_VERSION:  OpenGL ES 3.2 build 1.15@6070602
```

Запуск — только с `XDG_RUNTIME_DIR` и `WAYLAND_DISPLAY`, которые любое
приложение получает от сеанса само. Ни `LD_LIBRARY_PATH`, ни `EGL_PLATFORM`,
ни `LIBEGL`/`LIBGLESV2`, ни даже предзагрузки сторожа TLS. Проверено после
перезапуска сеанса.

По дороге было три отдельных препятствия.

**1. Я сам себя обманул устаревшим файлом.** Вывод «диспетчер не вызывает наш
поставщик» был сделан по библиотеке, до которой пересборка не доехала: команда
`make install` в одной из связок молча не отработала. Проверял наличие
трассировки через `strings` — а он по умолчанию **не показывает кириллицу**,
и выдал ноль совпадений для существующей строки. Правильная проверка —
`grep -a` по самому файлу. Урок: сверять время файла и искать по двоичному
файлу, а не через `strings`.

**2. Отсутствовал диспетчер GL.** Пакета `libgles2` (это
`/usr/lib/aarch64-linux-gnu/libGLESv2.so.2` от glvnd) в системе не было вовсе,
и клиент подхватывал нашу автономную обёртку со своим состоянием. Установлен.

**3. Главное: функции GL нельзя брать через `eglGetProcAddress`.** Наша
собственная обёртка `libGLESv2` достаёт их прямым `android_dlsym` из
`libGLESv2_mtk.so`, а диспетчер glvnd строит таблицу через
`eglGetProcAddress`. Голый вендорский драйвер (`libEGL_mtk.so`, без андроидной
обёртки `libEGL`) возвращает оттуда указатели, которым нужны хуки в слотах TLS
bionic, — и без них `glGetString` молча отдаёт NULL. Именно поэтому EGL уже
работал, а GL-строки были пустые, причём **обоими путями**: и через диспетчер,
и напрямую по указателю.

Починка: в `egl.c` добавлена `hybris_egl_lookup_glesv2()` — прямой поиск
символа в андроидной libGLESv2; в `glvnd/eglglvnd.cpp` имена, начинающиеся на
`gl`, резолвятся через неё, и только потом через `eglGetProcAddress`.

Приятный побочный итог: **сторож TLS клиентам больше не нужен**. Слоты TLS
использует андроидная обёртка `libEGL`, а мы теперь ходим прямо в вендорский
драйвер мимо неё.

Сделанное суммарно:

- `configure.ac`, `egl/Makefile.am`, `egl/egl.c` — опции
  `--with-default-libegl=` и `--with-default-libglesv2=`;
- `egl/egl.c` — `hybris_egl_lookup_glesv2()` плюс трассировка под
  `BF7_EGL_TRACE`;
- `egl/glvnd/eglglvnd.cpp` — прямой поиск функций GL и отметки под
  `BF7_EGL_TRACE`;
- сборка с `--enable-glvnd --with-default-egl-platform=wayland
  --datarootdir=/usr/share`;
- установлены пакеты `libglvnd-core-dev` и `libgles2`;
- файл поставщика — `/usr/share/glvnd/egl_vendor.d/10_libhybris.json`.

Снимок рабочих библиотек: `/root/glvnd-working-latest` на устройстве, полная
копия каталога до начала работ — `/root/usrlocal-backup-latest`.

### 2026-09-16 16:36 — уточнение к предыдущей записи: причин было две, и первую нашёл не я

Свежий взгляд со стороны (агент без моего контекста) нашёл настоящую первую
причину, а моё объяснение «библиотека на диске была устаревшей» оказалось
неверным. Записываю как есть.

**Дефект первый (нашёл агент): диспетчер выбрасывал поставщика при загрузке.**
`libglvnd 1.6.0`, `src/EGL/libeglvendor.c`, `LoadVendor()` →
`LookupVendorEntrypoints()`: там макросом запрашиваются **27 обязательных**
точек входа EGL через `imports->getProcAddress`, и любой NULL даёт
`return GL_FALSE` → `goto fail`, молча, без единого сообщения.

Хибрисовский `eglGetProcAddress()` отдаёт только то, что есть в таблице
`_eglHybrisOverrideFunctions` (было около двадцати имён). Сами функции в
библиотеке реализованы, но сборка для glvnd идёт с `-fvisibility=hidden`,
поэтому запасного пути нет, а андроидный `eglGetProcAddress` ядро EGL не
отдаёт — только расширения. **18 обязательных имён возвращали NULL**, и
поставщик отбрасывался целиком. Косвенный признак был на виду: строка
«клиентские расширения» была пустой — glvnd собирает её из загруженных
поставщиков, а их было ноль.

Почему я этого не увидел: моя проверка `procprobe` линковалась с **автономной**
`/usr/local/lib/libEGL.so.1`, собранной **без** `-fvisibility=hidden`, — там
все 27 имён находились, и я сделал вывод «эта проверка пройдена». Проверять
надо было именно библиотеку поставщика. Урок: если сборки две, проверять ту,
на которую жалуются.

Исправление агента: 18 недостающих имён плюс `eglBindAPI`/`eglQueryAPI`
добавлены в `_eglHybrisOverrideFunctions` (теперь в таблице 46 записей).

**Дефект второй (нашёл я): функции GL нельзя брать через `eglGetProcAddress`.**
Описан в предыдущей записи. После первого исправления EGL проходил до
`eglMakeCurrent` включительно, но `glGetString` молча возвращал NULL.

**Нужны были обе правки.** Сверил: и на хосте, и на устройстве в `egl.c`
присутствуют и 46 записей таблицы, и `hybris_egl_lookup_glesv2()`. Сквозная
проверка после этого даёт `PowerVR Rogue GE8300`, `OpenGL ES 3.2` без единой
переменной окружения, рабочий стол цел, сбойных юнитов ноль.

Побочно: агент отметил перезапуск `bf7-session.service` в 16:34 как событие
неизвестного происхождения — это был я, проверял живучесть сеанса.

### 2026-09-16 16:45 — начат минимальный драйвер KMS: каркас собирается

Файл: `drivers/misc/mediatek/video/mt6765/videox/bf7_drm.c`, 273 строки.
Компилируется без ошибок и предупреждений.

Выбранная схема:

- основа — **tinydrm** из этого же ядра (`include/drm/tinydrm/tinydrm.h`). Он
  создан ровно для случая «одна несъёмная панель, один конвейер, буферы CMA»:
  даёт `TINYDRM_MODE`, `tinydrm_display_pipe_init()` (сам заводит соединитель
  с фиксированным режимом), `devm_tinydrm_init()`, `devm_tinydrm_register()`
  и обработку грязных областей. Образцом взят `drivers/gpu/drm/tinydrm/ili9225.c`;
- один CRTC, один соединитель `DRM_MODE_CONNECTOR_DSI` без обнаружения,
  один primary plane, форматы `XRGB8888`/`ARGB8888`;
- размеры панели берутся у вендорского кода: `primary_display_get_width()`,
  `_get_height()`. Если дисплей ещё не поднят — `-EPROBE_DEFER`;
- узла в device tree у нас нет и быть не может (дисплей описан по старой
  вендорской схеме), поэтому платформенное устройство заводится самим модулем
  в `late_initcall`.

Почему файл лежит в `videox`, а не в `drivers/gpu/drm`: функции
`primary_display_*` не помечены `EXPORT_SYMBOL`, а `CONFIG_MTK_FB` — `bool`,
не `tristate`, то есть весь вендорский видеокод линкуется прямо в ядро.
Соседство решает и вопрос с путями к заголовкам.

Правки в сборку:

- `drivers/misc/mediatek/video/Kconfig` — опция `DRM_BF7_DISP`
  (зависит от `MTK_FB && DRM && DRM_TINYDRM`);
- `drivers/misc/mediatek/video/mt6765/videox/Makefile` — объект и путь
  `-I.../drivers/misc/mediatek/include/mt-plat` (без него `primary_display.h`
  не находит `mtk_smi.h`);
- конфиг `bf7-x657b-v2.config`: `CONFIG_DRM_TINYDRM=y`,
  `CONFIG_DRM_KMS_CMA_HELPER=y`, `CONFIG_DRM_BF7_DISP=y`. Проверено, что
  переживают `olddefconfig`.

Осталась одна функция — `bf7_post_frame()`: отдать вендорскому тракту буфер
CMA по физическому адресу. Ждёт разведки того, как это делает `mtkfb`.

### 2026-09-16 16:54 — драйвер KMS дописан, ядро собрано целиком

Коммит `e6d2a5b44`, 427 строк. Ядро и модули связности собраны, всё
линкуется. **Не прошито.**

Главное, что дала разведка по вендорскому тракту и что определило устройство
драйвера: **`src_phy_addr` в `disp_input_config` — это не физический адрес, а
MVA блока M4U**. В структуре вообще нет поля под дескриптор ION, а
`_convert_disp_input_to_ovl()` кладёт это значение прямо в регистр адреса OVL
(`dst->addr = (unsigned long)src->src_phy_addr`). mtkfb работает потому, что
`disp_hal_allocate_framebuffer()` один раз при пробе прогоняет весь его регион
через `m4u_alloc_mva()`, **запрашивая MVA, равный физическому адресу**. Взять
`drm_gem_cma_object.paddr` и просто подставить — не подтверждено кодом и
рискованно.

Поэтому драйвер заводит **свой буфер вывода**: `dma_alloc_wc()`, затем
отображение в M4U тем же способом (своя `sg_table` на один элемент,
`m4u_create_client()`, порт `M4U_PORT_DISP_OVL0`, запрошенный MVA == PA).
Кадр клиента копируется в него построчно — шаг строки у клиента и у нас
разный. Копия на кадр не бесплатна, но зато нет возни со временем жизни
отображений; переход на передачу без копирования — следующий шаг.

Последовательность показа повторяет `mtkfb_pan_display_impl()`:
заполняется один `disp_input_config`, затем
`primary_display_config_input_multiple()` (только настраивает) и отдельно
`primary_display_trigger(1, NULL, 0)` (показывает).

Тонкости, учтённые по разведке:

- `src_pitch` задаётся **в пикселях**, не в байтах — в байты его переводит
  уже `_convert_disp_input_to_ovl()`;
- структуру обязательно обнулять: ненулевой `dirty_roi_addr` вендорский код
  примет за указатель из userspace;
- `buffer_source = DISP_BUFFER_MVA` — «адрес готов, резолвить нечего»;
- формат `DISP_FORMAT_BGRA8888` соответствует `DRM_FORMAT_XRGB8888`: синий в
  младшем байте, ровно тот выбор, что делает mtkfb при `blue.offset == 0`;
- `primary_display_init()` **не звать** — он не идемпотентен и принадлежит
  mtkfb. Вместо этого проба ждёт `primary_display_is_alive()` и возвращает
  `-EPROBE_DEFER`;
- `primary_display_wait_for_vsync(NULL)` **не звать никогда**: при подключённой
  панели функция разыменовывает аргумент без проверки. Нам она и не нужна —
  блокирующий `trigger` сам ждёт вертикальной синхронизации;
- слой берём тот же, что и mtkfb (`FB_LAYER`, он же 0). Слои в тракте
  постоянные: чужие сохраняют состояние, и класть кадр в свободный слой
  означало бы композицию поверх консоли.

Отмечено разведкой как непроверенное: поведение M4U именно для нашего
свежего буфера CMA. Если `m4u_alloc_mva()` откажет, проба вернёт ошибку и
ничего не сломает; если отобразит неверно — это увидим только при первом
кадре, то есть при сознательной проверке, а не при загрузке.

Также отмечено: есть глобальный флаг `gTriggerDispMode`, при котором и
настройка, и запуск молча становятся пустыми. Если кадр не появится без
ошибок — смотреть туда.

Сборка: `Image` sha256 `076c99c2…`, модули связности пересобраны тем же
заходом против правильного каталога (`KOUT` уже исправлен).

### 2026-09-16 17:18 — KMS заработал: есть устройство, соединитель и показанный кадр

```
bf7-disp: буфер вывода: 4747264 байт, PA 0xff200000, MVA 0x41400000
[drm] Initialized bf7disp 1.0.0 for bf7-disp on minor 1
bf7-disp: KMS поверх вендорского тракта: 720x1612
```

Появились `/dev/dri/card1` и `/sys/class/drm/card1-DSI-1` в состоянии
`connected`. Клиент DRM (`/root/kmstest.c`) получает режим 720x1612, заводит
dumb-буфер, заливает зелёным, ставит режим — и ядро отвечает:

```
[drm] bf7: первый кадр показан (слой 0, MVA 0x41400000)
```

Ни одной ошибки от вендорского тракта. Чтение буфера обратно подтверждает
записанный цвет. Панель глазами не проверена — хозяин не у телефона.

Три поломки по дороге, все теперь закрыты.

**Первая: `m4u_alloc_mva` отказывал с -EINVAL.** Сообщение нашлось не сразу,
потому что я смотрел только начало списка совпадений в `dmesg`, а проба
случается позже. Настоящая причина:

```
[M4U] unaligned: iova 0x41400000 pa 0xff200000 size 0x486a00 min_pagesz 0x1000
[M4U] m4u_map_sgtable: ret: -22
```

То есть **MVA выделился успешно**, а отображение отвергло некратный странице
размер. Лечится `PAGE_ALIGN`. Попутно выяснилось, что требовать конкретный
MVA (как делает mtkfb) нам не нужно и вредно: наш буфер из общего пула, и
запрос фиксированного адреса тоже даёт -EINVAL. Просим M4U выбрать сам.

**Вторая: `drmModeSetCrtc` проходил, а на панель не попадало ничего.**
`tinydrm_display_pipe_update()` вызывается только при **смене буфера**, а при
установке режима меняется состояние конвейера. Первый показ обязан сделать
обработчик включения — как это делают все драйверы tinydrm.

**Третья, и не про ядро вовсе: телефон загрузился с чужого слота.**
Штатное ядро 4.19.191 вместо нашего 4.19.325, `androidboot.slot=b`. Причина:
у слота есть счётчик попыток загрузки, наш Debian никогда не сообщает
загрузчику об успехе, счётчик тает — и после нескольких перезагрузок
загрузчик уходит на слот B. Сбрасывает счётчик `fastboot set_active a`, а я
в последних двух прошивках эту команду пропустил ради экономии шага.
**Вывод: `set_active a` обязателен после каждой прошивки, не опционален.**

Итог: ключевой недостающий кусок — устройство DRM/KMS с соединителем — теперь
есть. Дальше по этому направлению: GBM поверх него, затем glamor в Xwayland и
OpenGL у приложений X.

### 2026-09-16 17:55 — кадр из чужого dma-buf идёт в панель без копирования

Главный шаг после появления KMS: кадр, лежащий в чужом dma-buf, теперь
показывается панели напрямую. Проверено на железе клиентом, который берёт
буфер из ION (та же куча, из которой его берут gralloc и декодер), красит
полосами, импортирует в `/dev/dri/card1` и ставит режим:

```
[drm] bf7: кадр из чужого dma-buf показан без копирования (MVA 0x42800000, шаг 2944)
[drm] bf7: первый кадр показан (слой 0, MVA 0x42800000)
```

Ни одного отказа M4U за всё время показа. Это именно тот мост, которого не
хватало: и кадры GPU, и кадры декодера приходят как dma-buf.

**Почему потребовался свой `gem_prime_import_sg_table`.** Помощник CMA
(`drm_gem_cma_prime_import_sg_table`) принимает только непрерывную память, а
буферы ION из общей кучи рассыпаны по страницам. Непрерывность нам и не нужна:
её обеспечивает M4U. Страницы отображаются один раз при импорте, и слою
отдаётся готовый MVA. Объект наследует `drm_gem_cma_object` не ради памяти
CMA, а чтобы остались работоспособными готовые помощники: при `vaddr == NULL`
и выставленном ядром `import_attach` `drm_gem_cma_free_object()` сам делает
ровно то, что нужно.

Длину отображения считать надо **так же, как `m4u_map_sgtable()`**: `sg_dma_len`,
а при нулевом `sg_dma_address` — `sg->length`. Просить `PAGE_ALIGN(size)`
нельзя: он дойдёт до конца списка раньше заказанного и откажет.

При выключении конвейера обязательно возвращаемся на собственный буфер. Чужой
отпустят сразу после выключения, отображение в M4U снимется — а слой продолжит
читать тот же адрес. Это отказ доступа на ровном месте.

### 2026-09-16 17:56 — снимок панели: зачем и как он чуть не стоил дисплея

Чтобы проверять вывод без хозяина у телефона, нужен способ прочитать то, что
тракт действительно показал. В вендорском коде он есть: `_screen_cap_by_cpu()`
подключает WDMA0 к выходу OVL0 и пишет кадр в память. Функция статическая и в
этом дереве **не вызывается ниоткуда** — что и оказалось важным.

**Она самозаклинивается.** Сразу после `_primary_path_lock()` идёт
`primary_display_idlemgr_kick(__func__, 1)`, а единица означает «возьми замок
сам» — тот же `disp_sw_mutex_lock(&pgc->lock)`. Поток навсегда уходит в D:

```
[<0>] disp_sw_mutex_lock+0xc/0x18
[<0>] primary_display_manual_lock+0x50/0xa0
[<0>] primary_display_idlemgr_kick+0xe8/0x2a4
[<0>] primary_display_capture_to_mva+0x3ac/0x708
```

Вместе с потоком навсегда заперт замок тракта дисплея: рабочий стол замирает,
`disp_check` встаёт рядом, и снять это можно только перезагрузкой — задача
неубиваемая. Соседний `_screen_cap_by_cmdq()` передаёт сюда 0, то есть
правильный вызов лежит в том же файле пятью десятками строк выше.

Уроки, которые дороже самой починки:

- **Мёртвый вендорский код — не «проверенный, просто не вызываемый».** Если у
  функции нет ни одного вызывающего, она вполне может не работать вовсе.
  Звать такую впервые стоит там, откуда падение не утащит систему.
- **Читать в D-состоянии нельзя прервать.** `timeout 30 dd` не помог: процесс
  завис в ядре, и ssh-команда не вернулась вовсе. Такую пробу надо сразу
  пускать через `nohup ... &` и опрашивать отдельно — что и сделано теперь.
- Вытащил ровное место: `/proc/PID/stack` показывает стек зависшего потока
  целиком, и по нему причина видна с первого взгляда.

Перезагрузка после этого — только `echo b > /proc/sysrq-trigger`: обычный путь
в fastboot проходит через штатное выключение, а оно упирается в тот же замок.

**Проверка прошивки.** Отдельно стоит запомнить, как убедиться, что прошилось
именно то: `lsblk -o NAME,PARTLABEL` даёт номер раздела (у нас boot_a —
`mmcblk0p23`), и `dd bs=<размер образа> count=1 | sha256sum` на устройстве
сверяется с хостовой суммой байт в байт. Симлинков `by-name` в нашем Debian нет.

### 2026-09-16 18:05 — снимок панели работает; рабочий стол увиден глазами

Снимок получен и рассмотрен. На нём живой Xfce: обои по умолчанию, панель
вдоль правого края, значки «Файловая система» и «Домашняя папка», экранная
клавиатура — и часы, показывающие ровно время снимка. То есть снимок именно
живой, а не остаток фреймбуфера от загрузчика.

**Режим сессии оказался 2 — развязка.** Это выяснилось не сразу: первый заход
упёрся в предохранитель «не прямая связь», и только печать номера показала
какой. В развязке OVL собирает кадр в промежуточный буфер, а панель кормит
RDMA из него же. Следствий два.

Первое: WDMA0 действительно занят, вендорский захват через `add_memout` в этом
режиме невозможен в принципе. Второе, и хорошее: **снимать ничего и не надо** —
достаточно прочитать тот самый промежуточный буфер. Он лежит в
зарезервированной области фреймбуфера (`pgc->framebuffer_va`, три штуки подряд,
формат RGB888, шаг в ширину панели), текущий — `pgc->dc_buf_id`. Тракт при этом
не трогается вообще: никаких подключений, никаких ожиданий событий, нечему
зависать.

Замок тракта при копировании сознательно не берётся. Десяток миллисекунд под
ним остановил бы вывод, а худшее, что грозит снимку без замка, — разрыв по
горизонтали, если кадр как раз дописывается.

**Проверка показа без копирования — с картинкой.** Клиент кладёт в буфер ION
четыре полосы и отдаёт его через `card1`. Снимок после этого:

```
строка 193:  0xff0000   красный
строка 596:  0x00ff00   зелёный
строка 999:  0x0000ff   синий
строка 1402: 0xffffff   белый
```

Цвета точные, полосы во весь экран, порядок верный. Значит правильна и вся
цепочка соответствий: `DRM_FORMAT_XRGB8888` → `DISP_FORMAT_BGRA8888` → OVL →
WDMA → RGB888 → разбор на хосте. Ни одного отказа M4U.

После выхода клиента экран становится чёрным — так и задумано, обработчик
выключения возвращает слой на собственный буфер. Рабочий стол возвращается
любым обновлением, проще всего `DISPLAY=:1 xrefresh`: композитор рисует только
по повреждению, а сам он о подмене слоя не знает.

Мелочь, стоившая одного захода: `pkill -f bf7-dmabuf-kms` убивает и саму
ssh-команду — её строка содержит образец. Снимать процесс только по
`/proc/PID/exe`.

### 2026-09-16 18:20 — разведка перед своим бэкендом GBM: что уже есть на железе

Прежде чем писать код, проверил на устройстве то, чего не узнать по исходникам.
Результат лучше ожидаемого: **у блоба PowerVR есть всё, что требует glamor.**

```
EGL 1.5 — Imagination Technologies | 1.5 build 1.15@6070602
GL: PowerVR Rogue GE8300 | OpenGL ES 3.2
```

Расширения дисплея, все на месте: `EGL_EXT_image_dma_buf_import` и
`..._modifiers`, `EGL_KHR_image_base`, `EGL_KHR_gl_texture_2D_image`,
`EGL_KHR_surfaceless_context`, `EGL_KHR_no_config_context`,
`EGL_EXT_buffer_age`, `EGL_KHR_fence_sync`, `EGL_KHR_wait_sync`,
`EGL_ANDROID_native_fence_sync`. Со стороны GL — `GL_OES_EGL_image`,
`GL_OES_EGL_image_external`, `GL_EXT_texture_format_BGRA8888`,
`GL_OES_texture_npot`, `GL_OES_rgb8_rgba8`, `GL_OES_vertex_array_object`.

То есть **непроходимого препятствия нет**: ни одно из обязательных расширений
не отсутствует. Ровно этот вопрос и был главным риском всей затеи.

Два расширения отсутствуют — `GL_EXT_unpack_subimage` и
`GL_EXT_map_buffer_range`. Обе возможности входят в ядро GLES 3.0, так что
функциональность есть; опасность в другом — если glamor проверяет **строку
расширения**, а не версию, проверка провалится на ровном месте. Отдано
разведке как отдельный вопрос.

**Тонкость с glvnd, которую легко принять за победу.** Клиентская строка
расширений уже содержит `EGL_MESA_platform_gbm` и `EGL_KHR_platform_gbm` — но
это не наша заслуга: glvnd склеивает строки всех поставщиков, и эти два пришли
от Mesa. Практический вывод двоякий. Хороший: проверка расширений в Xwayland
пройдёт сама собой. Плохой: `eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, ...)`
уедет туда, куда решит glvnd, и по умолчанию это Mesa — которая на нашем
устройстве драйвера не найдёт. Значит задача не только «написать бэкенд GBM»,
но и **направить платформу GBM в libhybris**.

**Что композитор показывает клиентам сейчас** (11 глобальных объектов):

```
android_wlegl (версия 2)   <<< так libhybris передаёт буферы GPU
qt_hardware_integration, wl_compositor, wl_data_device_manager,
wl_output, wl_seat, wl_shell, wl_shm, wl_subcompositor,
wp_viewporter, xdg_wm_base
```

Ни `wl_drm`, ни `zwp_linux_dmabuf_v1`. А glamor в Xwayland требует именно
`wl_drm`. Значит к работе добавляется серверная сторона протокола в bf7gui.

Попутно замечено: у нашего EGL есть `EGL_WL_bind_wayland_display` — но в
libhybris он поднимает `android_wlegl`, а не `wl_drm`, так что даром это не
достанется.

Мелочь, стоившая одного захода: EGL через hybris по умолчанию идёт на
платформу wayland, и без `WAYLAND_DISPLAY` проба падает с «failed to connect
to the server». Проверять так:
`XDG_RUNTIME_DIR=/run/bf7-gui WAYLAND_DISPLAY=wayland-1 ./eglext`.

### 2026-09-16 18:30 — ABI бэкендов GBM в Mesa 22.3.6: точные условия

Разведка по установленной у нас версии (не по main — там могло быть иначе;
оказалось, что не иначе: `GBM_BACKEND_ABI_VERSION` равен 1 и структуры
побайтово те же вплоть до текущего main, так что обновление Mesa нас не сломает).

Заголовки `gbm_backend_abi.h` и `gbmint.h` **не ставятся** ни одним пакетом —
их надо брать из дерева Mesa по тегу `mesa-22.3.6` и держать у себя.

Что обязан делать наш `.so`:

- экспортировать ровно один видимый символ `gbmint_get_backend`, всё остальное
  прятать. Загрузчик делает `dlopen` с **`RTLD_GLOBAL`**, так что любые лишние
  видимые символы полезут в общее пространство процесса;
- **не** линковаться с `libgbm.so.1` — это она нас загружает;
- вернуть вечно живой указатель на `struct gbm_backend`; вернуть NULL нельзя,
  загрузчик его не проверяет и падает;
- в `create_device(fd, version)` скопировать `version` в
  `dev->v0.backend_version` **дословно**. Если не совпадёт — загрузчик молча
  выбросит устройство и вернёт NULL, вообще без сообщения;
- звать `gbm_core->v0.format_canonicalize()` на каждом полученном формате:
  фронтенд этого не делает, он вообще почти ничего не делает — все публичные
  поля `gbm_bo` и `gbm_surface` заполняет бэкенд.

**Как нас найдут.** Порядок такой: переменная `GBM_BACKEND`, затем имя драйвера
из `drmGetVersion(fd)->name`, затем встроенный «dri». Файл ищется как
`<имя>_gbm.so` в `/usr/lib/aarch64-linux-gnu/gbm/`. Наш драйвер KMS называется
**`bf7disp`** — значит файл `bf7disp_gbm.so` подхватится сам, как только кто-то
откроет `card1`. Никаких переменных окружения не потребуется.

**Находка, которая могла бы всё обрушить.** Собственная Mesa'вская `libEGL` на
платформе `EGL_PLATFORM_GBM_MESA` сторонние бэкенды **не поддерживает в
принципе**: `dri2_initialize_drm()` сверяет имя бэкенда со строкой `"drm"` и
дальше приводит наш `gbm_device` к своей внутренней `struct gbm_dri_device`,
лазая по приватным полям. То есть идеально написанный по ABI бэкенд через
Mesa'вскую EGL работать не будет никогда.

Нас это не задевает, и вот почему: мы через Mesa'вскую EGL и не идём. Механизм
сторонних бэкендов и добавляли ровно под такой случай — чужая проприетарная EGL
поверх Mesa'вского фронтенда libgbm (это была нужда NVIDIA). У нас точно та же
расстановка: фронтенд libgbm от Mesa, бэкенд наш, EGL от libhybris. **Но отсюда
следует, что развилка glvnd — не деталь, а несущая конструкция:** если
`eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, ...)` уедет в Mesa, всё встанет
намертво на проверке имени.

**И следствие для объёма работ.** Mesa в своём бэкенде оставляет
`surface_lock_front_buffer`, `surface_release_buffer` и
`surface_has_free_buffers` пустыми — её EGL их обходит, переписывая логику у
себя. Нам их придётся реализовать по-настоящему: наша EGL пойдёт через
публичный API.

### 2026-09-16 18:40 — проверено на железе: куда сейчас уходит платформа GBM

Написал пробу, которая создаёт устройство GBM на `card1` и спрашивает у EGL,
кто ответил. Результат:

```
MESA-LOADER: failed to open bf7disp: /usr/lib/dri/bf7disp_dri.so: нет такого файла
failed to load driver: bf7disp
gbm_create_device(/dev/dri/card1) -> 0x556c0c19b0  бэкенд: drm
eglGetPlatformDisplayEXT(GBM) -> 0x556c1a41a0
ОТВЕТИЛ: Mesa Project | 1.4 | EGL 1.4
```

Три вывода, все важные.

**Первый: имя нашего драйвера уже ищется.** Mesa попыталась открыть драйвер по
имени `bf7disp` — то самое, что возвращает `drmGetVersion()`. Значит и
`bf7disp_gbm.so` будет найден сам, без переменных окружения.

**Второй: сейчас устройство GBM всё-таки создаётся,** бэкендом `drm`, несмотря
на отсутствие драйвера DRI, — и Mesa'вская EGL на нём даже поднимается,
отвечая EGL 1.4. То есть glamor в Xwayland сегодня, вероятно, **завёлся бы — на
программной Mesa**. Это не то, что нам нужно: рисование осталось бы на
процессоре, только с лишними пересылками. Полезно как предупреждение: «glamor
заработал» само по себе ничего не доказывает, надо смотреть `GL_RENDERER`.

**Третий, главный: платформу GBM забирает Mesa.** Ровно то, о чём предупреждал
отчёт по ABI: наш бэкенд она отвергнет по имени, и запасного пути не будет,
потому что дисплей к поставщику привязывается ещё в `eglGetPlatformDisplay`, а
отказ случится позже, в `eglInitialize`.

**Чем лечится — видно оттуда же.** Поставщики glvnd перебираются в порядке имён
файлов: `10_libhybris.json` идёт раньше `50_mesa.json`. Это подтверждается
опытом: `eglGetDisplay(EGL_DEFAULT_DISPLAY)` достаётся hybris (отвечает
Imagination, EGL 1.5), а GBM уходит к Mesa — то есть hybris спросили первым, он
отказался, и очередь дошла до Mesa. Значит достаточно **научить libhybris
принимать `EGL_PLATFORM_GBM_KHR`**, и он заберёт платформу себе просто потому,
что его спрашивают раньше. Переставлять приоритеты и отключать Mesa не нужно.

### 2026-09-16 18:50 — сторона libhybris: что уже есть, чего нет, и где главный риск

**Развилка glvnd прочитана по исходникам, не угадана.** `GetPlatformDisplayCommon()`
в libglvnd 1.6.0 перебирает поставщиков **в порядке списка и на каждом вызове**,
беря первого, кто вернул не `EGL_NO_DISPLAY`. То есть наблюдение с железа
объяснено точно, и отсюда же — требование к нам: наш `GetDisplay` обязан
**вежливо отказываться** от чужого `gbm_device`, вернув NULL, а не падать (как
делает вейландовский, который на пустом дисплее зовёт `abort()`). Иначе мы
перехватим GBM у всех остальных программ в системе — молча, без ошибок.

**Готовый приём для главной механики уже лежит в нашем же дереве.** Вытащить
dma-buf из буфера gralloc нечем: `hybris_gralloc_allocate()` отдаёт только
`buffer_handle_t` и один шаг строки, функции «дай мне fd» нет ни одной. Но в
`mtkfb_window.cpp` мы это уже решали:

```c
/* у буфера gralloc несколько дескрипторов (у PowerVR — три),
   нужен тот, чей размер не меньше самого кадра */
for (int i = 0; i < h->numFds; i++) {
    off_t len = lseek(h->data[i], 0, SEEK_END);
    if (len > 0 && (size_t)len >= need) { b->dmaFd = h->data[i]; ... }
}
```

Постоянного номера у нужного дескриптора нет — только проба по размеру.

**Главный неизвестный риск — раскладка буфера.** Ни модификаторов, ни смещений
плоскостей ни один слой в нашем дереве не показывает. Если gralloc PowerVR под
`HW_RENDER|HW_TEXTURE` отдаёт не линейный буфер, а тайловый или сжатый, то
наивная обёртка «линейный, одна плоскость» покажет мусор.

**Но проверить это мы теперь можем прямо, и это приятный поворот:** нарисовать
в буфер gralloc через GLES, вытащить fd, показать его на панели через `card1` и
**снять снимок экрана**. Оба куска — показ чужого dma-buf и снимок — сделаны
сегодня. Если картинка выйдет правильной, буфер линейный, и вопрос закрыт
опытом, а не догадкой.

**Мелочи, которые нельзя изменить из своей платформы:** в общем базовом классе
`NATIVE_WINDOW_MIN_UNDEQUEUED_BUFFERS` зашит как 1, а `NATIVE_WINDOW_BUFFER_AGE`
как 2 — без правки общего файла, задевающей все платформы, их не поменять. Все
существующие платформы живут с этим.

**И догадка, которую надо проверить третьим отчётом.** Похоже, glamor в Xwayland
**окон EGL вообще не создаёт**: он делает пиксельные карты из `gbm_bo`, тащит их
в `EGLImage` через `EGL_EXT_image_dma_buf_import` и рисует в них как в текстуры,
а контекст держит безоконный (`EGL_KHR_surfaceless_context` у нас есть). Если
так, то целая платформа libhybris с `ANativeWindow`, очередями буферов и
фенсами — **не нужна вовсе**: хватит принять `EGL_PLATFORM_GBM_KHR` и отдать
обычный андроидный дисплей. Это разница между «5–8 дней» и «день-полтора».
Жду подтверждения по списку вызовов Xwayland.

### 2026-09-16 19:00 — Xwayland 22.1.9 разобран по исходникам: чего он хочет на самом деле

Разведка по точному тегу, не по main. Итог: **непроходимых мест нет ни одного**,
а объём работы меньше, чем казалось.

**Догадка подтвердилась: окон EGL glamor не создаёт.** Он делает буферы через
`gbm_bo_create*`, тащит их в `EGLImage` и рисует как в текстуры, а контекст
держит безоконный — `eglCreateContext(dpy, EGL_NO_CONFIG_KHR, ...)` и
`eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)`. Причём проверок на
`EGL_KHR_surfaceless_context`/`EGL_KHR_no_config_context` он не делает — просто
пользуется. Оба расширения у нас есть. **Значит платформа libhybris с
`ANativeWindow`, очередями буферов и фенсами для Xwayland не нужна вовсе.**

**Требования glamor на пути GLES — все выполнены.** Сверено со вчерашним
дампом расширений: GLES ≥ 2.0 (у нас 3.2), `GL_EXT_texture_format_BGRA8888`,
`GL_OES_texture_border_clamp`, `GL_OES_vertex_array_object` — всё на месте.
`GL_OES_texture_npot` не проверяется вообще, PBO на пути GLES не используется.
Настольный GL не нужен.

**Ложной победы не будет.** Xwayland сам отказывается от glamor, если
`GL_RENDERER` содержит `llvmpipe` или `softpipe`:
`"Refusing to try glamor on llvmpipe"`. То есть вчерашнее опасение снято: тихо
уехать на программную Mesa не получится, будет явный отказ.

**Xwayland прямо рассчитан на чужие бэкенды GBM.** После инициализации он зовёт
`gbm_device_get_backend_name()` и, если имя **не** `"drm"`, записывает его в
`xwl_screen->glvnd_vendor`. То есть наш случай предусмотрен в его коде.

**Важная поправка: устройство GBM берётся не с `card1`.** Xwayland открывает тот
узел, который ему назовёт композитор событием `device` протокола `wl_drm`. Имена
драйверов у нас такие:

```
/dev/dri/card0        'pvr'      1.15.6070602  Imagination Technologies PVR DRM
/dev/dri/renderD128   'pvr'      1.15.6070602  Imagination Technologies PVR DRM
/dev/dri/card1        'bf7disp'  1.0.0         BF7 display over MediaTek primary_display
```

Значит наш файл должен называться **`pvr_gbm.so`**, а не `bf7disp_gbm.so`. Это и
правильнее по смыслу: буферы выделяет GPU, а показ — отдельная забота `card1`.

**И отсюда же — целый кусок работы отпадает.** Xwayland смотрит тип узла: если
это узел рендера, аутентификация не нужна вовсе. У нас `renderD128` — настоящий
узел рендера. Значит композитору достаточно назвать его в событии `device`, и
`drmAuthMagic` реализовывать не придётся. Попутно развеяно ходовое заблуждение:
объявление `WL_DRM_CAPABILITY_PRIME` аутентификацию **не** отменяет — поле
`capabilities` в этой версии Xwayland вообще нигде не читается.

**Что всё же нужно от композитора.** `wl_drm` версии 2 обязателен: без него
`xwl_glamor_gbm_has_wl_interfaces()` отвечает `"glamor: 'wl_drm' not supported"`
и glamor выключается. Плюс `zwp_linux_dmabuf_v1` версии 3 — формально
необязателен, но фактически необходим: Debian собирает Xwayland против Mesa
22.3.6, поэтому живы пути с модификаторами, а буфер с модификатором в `wl_buffer`
можно превратить только через dmabuf. У Qt есть готовый серверный модуль
`linux-dmabuf-v1` — надо проверить, собран ли он у нас.

**Что обязан уметь наш бэкенд** (по той же причине — сборка против Mesa 22.3.6
включает оба макроса): `gbm_bo_create_with_modifiers`, `gbm_bo_get_fd_for_plane`,
`gbm_bo_get_plane_count`, `gbm_bo_get_modifier`, `gbm_bo_get_offset`,
`gbm_bo_get_stride_for_plane`. Запасные пути без модификаторов у нас мёртвые.

### 2026-09-16 19:15 — найдено самое крупное препятствие, и оно чинится парой строк

Собрал пробу «GPU рисует в буфер gralloc — панель показывает его», чтобы
опытом закрыть вопрос о раскладке буфера. Проба упала сразу после выделения
буфера. Стек:

```
#0  0x0000000000000000 in ?? ()
#1  _my_eglCreateImageKHR (target=12608, ...) at egl.c:597
```

Вызов по нулевому адресу. Причина — в самом libhybris. Макрос `HYBRIS_DLSYSM`
ищет функцию **только** через `android_dlsym`:

```c
#define HYBRIS_DLSYSM(name, fptr, sym) \
    if (*(fptr) == NULL) *(fptr) = (void *) android_dlsym(name##_handle, sym);
```

А вендорский драйвер экспортирует только ядро EGL. Проверено на устройстве:

```
eglCreateImageKHR    НЕТ        eglGetProcAddress   ЕСТЬ
eglDestroyImageKHR   НЕТ        eglInitialize       ЕСТЬ
eglCreateSyncKHR     НЕТ
```

Расширения драйвер отдаёт **только** через свой `eglGetProcAddress`, как и
положено расширениям. Запасного пути в макросе нет, указатель остаётся нулевым,
и первый же вызов роняет процесс.

**Почему это важнее самой пробы.** `eglCreateImageKHR` — основная функция
glamor: именно ею он превращает буфер в текстуру. То есть Xwayland с glamor
упал бы на первой же пиксельной карте, и выглядело бы это как «PowerVR не
годится», хотя виновата одна недостающая ветка в `HYBRIS_DLSYSM`.

Это ровно тот же род поломки, что уже ловился с функциями `gl*`: символ в
библиотеке есть или должен браться иначе, а путь его добычи выбран не тот.
Тогда лечилось прямым `android_dlsym`, здесь — наоборот, обращением к
`eglGetProcAddress` драйвера.

Затронуты все точки входа расширений, что резолвятся этим макросом: пока это
`eglCreateImageKHR` (egl.c:588) и `eglDestroyImageKHR` (egl.c:629); остальные
двенадцать — ядро EGL, они экспортируются и работают.

Лечение: помощник, который сперва пробует `android_dlsym`, а при неудаче —
`eglGetProcAddress` драйвера. По той же схеме, что уже сделанный
`hybris_egl_lookup_glesv2()`.

### 2026-09-16 19:00 — починено, и сразу проверено: GPU рисует, панель показывает

Правка в libhybris: точки входа расширений теперь ищутся и через
`eglGetProcAddress` драйвера, а не только через `android_dlsym`. Затронуты
четыре функции — `eglCreateImageKHR`, `eglDestroyImageKHR`,
`glEGLImageTargetTexture2DOES`, `glEGLImageTargetRenderbufferStorageOES`.

Для имён `gl*` порядок обратный и важен: сперва прямой `android_dlsym` в
libGLESv2, потому что указатели из `eglGetProcAddress` требуют хуков в слотах
TLS bionic, и только для точек входа расширений, которых в библиотеке нет,
остаётся `eglGetProcAddress`. Если имя не нашлось нигде — теперь честная
ошибка, а не вызов по нулевому адресу.

**Проба прошла целиком:**

```
GL: PowerVR Rogue GE8300
буфер gralloc: шаг 736 пикселей
GPU отрисовал
в дескрипторе: 47 чисел, 3 дескриптора
  fd[0]=16 размер -1
  fd[1]=13 размер 4747264  <= подходит
  fd[2]=14 размер 4096
показано: кадр 33, шаг 2944 байт
```

Приём с перебором по размеру подтвердился ровно в том виде, в каком он записан
в `mtkfb_window.cpp`: дескрипторов три, у первого размер вообще не берётся, у
третьего — одна страница, и только второй равен `736*1612*4`. Постоянного
номера нет, годится только проба.

**Снимок экрана ответил на главный вопрос: раскладка линейная.**

```
верх       строка    80: 0xff0000
четверть   строка   483: 0x00ff00
середина   строка   886: 0x0000ff
низ        строка  1450: 0x000000
угол сверху-слева : 0xffff00
```

Полосы той самой ширины, что рисовались (1/8, 1/4, 1/2), цвета точные,
метка на месте. Кадр перевёрнут по вертикали — так и должно быть, у GL
начало координат внизу, у панели вверху. Никакого мусора, никакого тайлинга:
gralloc под `HW_RENDER|HW_TEXTURE|HW_COMPOSER` отдаёт обычный линейный буфер,
который контроллер дисплея читает шагом `736*4` без всяких модификаторов.

**Тем самым цепочка GPU → панель без копирования работает уже сейчас**, и
последний крупный неизвестный риск затеи с бэкендом GBM снят опытом.

После правки библиотеки проверено, что ничего не сломалось: EGL по-прежнему
отвечает `Imagination Technologies, EGL 1.5`, GL — `PowerVR Rogue GE8300,
OpenGL ES 3.2`, система `running`, сбойных юнитов нет, рабочий стол на панели
(снимок показывает те же цвета, что и раньше). Старая библиотека сохранена в
`/root/glvnd-backup-latest/`.

### 2026-09-16 19:45 — glamor на PowerVR заработал; и три поломки показа

**Главное: X-клиенты рисуются через GPU.** В журнале больше нет
`Failed to initialize glamor, falling back to sw`, появилось расширение DRI3,
Xwayland выделяет буферы через наш бэкенд (в журнале ядра —
`ION ... dbg=gralloc.RGX`, куски по 2088960 байт = 1088*480*4), а снимок
X-экрана показывает рабочий стол Xfce целиком и правильно.

Что для этого понадобилось, помимо бэкенда GBM:

- **`wl_drm` в композиторе.** Без него `xwl_glamor_gbm_has_wl_interfaces()`
  отвечает `glamor: 'wl_drm' not supported` и glamor выключается. Реализован
  на сырой libwayland-server (~130 строк): объявление узла рендера, список
  форматов, признак PRIME. Аутентификацию писать не пришлось —
  `/dev/dri/renderD128` настоящий узел рендера, и Xwayland её пропускает.
- **`zwp_linux_dmabuf_v1`**: переключил клиентскую интеграцию буферов Qt на
  `linux-dmabuf-unstable-v1`. Композитор объявил 11 форматов с линейным
  модификатором, среди них `AR24` и `XR24`.
- **Ту же дыру с расширениями, но в другой библиотеке.** Композитор берёт
  standalone-сборку libhybris из `/usr/local/lib`, а она при сборке с glvnd
  вообще не пересобирается — там остался бинарник от старой конфигурации, в
  котором и `eglCreateImageKHR` нулевой, и умолчание ведёт в андроидную
  обёртку `Android META-EGL` без `EGL_EXT_image_dma_buf_import`. Отсюда было
  `qt.waylandcompositor: Failed to initialize EGL display`. Лечится тем, что
  устаревшие `libEGL.so.1*` и `libGLESv2.so.2*` убраны из `/usr/local/lib`, и
  композитор пошёл общим путём через glvnd.

**Поломка первая: переставлены каналы R и B.** Измерено, не предположено.
X сообщает цвет стола `#31303a`, панель показывает `#3a3031` — те же байты в
обратном порядке. Причина в исходниках glamor:

```c
if (glamor_priv->is_gles) {
    glamor_add_format(screen, 32, PICT_a8b8g8r8, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, TRUE);
} else {
    glamor_add_format(screen, 32, PICT_a8r8g8b8, GL_RGBA, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, TRUE);
}
```

То есть на пути GLES экран объявляется как `a8b8g8r8` (красный в младшем
байте), а буфер композитору отдаётся как `ARGB8888` — и этот формат Xwayland
берёт **из глубины окна**, а не из буфера (`wl_drm_format_for_depth`). Обе
стороны по-своему правы, а вместе не сходятся. Это настоящая ошибка Xwayland
на драйверах без настольного GL.

Попутно она же объясняет поток ошибок
`glamor0: GL error: TexSubImage2D ... GL_INVALID_OPERATION`: загрузка идёт с
`GL_RGBA`, а образ EGL создан как `ARGB8888`, и PowerVR такое сочетание не
принимает. Драйвер тут ни при чём — отдельная проба показала, что **fourcc он
уважает честно**: в буфер `ARGB8888` красный кладётся как `00 00 ff`, в
`ABGR8888` — как `ff 00 00`.

Лечится это в одном месте: у нас есть `GL_EXT_texture_format_BGRA8888`, и на
пути GLES можно объявлять `PICT_a8r8g8b8` с `GL_BGRA_EXT`. Тогда сходится всё
разом — и цвета, и загрузка пикселей.

**Поломка вторая: кадр иногда пустой.** Снимок ловит кадры, где нарисован
только фон окна композитора (`#0b0f14`), без поверхности X. Хозяин описывает
это как «иногда просто очищается, поведёшь пальцем — снова рисует».
Протокольный журнал показывает, что коммиты и обратные вызовы кадра ходят
исправно, так что дело не в них.

**Поломка третья: окно с альфой прозрачно** (экранная клавиатура), и
выделение под ним не проступает.

**И отдельно — про сам прибор.** Снимок экрана достоверен **только в режиме
развязки**. Режим сессии переключается на ходу: пять снимков подряд дали
2,2,1,2,1. В режиме прямой связи идёт вендорский путь через WDMA0, и он отдаёт
чёрное — то самое мёртвое место, о котором говорилось выше. Несколько «экран
чёрный» из сегодняшних наблюдений оказались артефактом прибора, а не панели.
Проверять надо по строке `bf7: снимок из буфера развязки` в журнале ядра.

### 2026-09-16 19:50 — цвета сошлись: X-клиенты ускорены и показаны верно

Заплата на glamor наложена, Xwayland пересобран на самом телефоне
(`meson` + `ninja`, 430 целей, ~8 минут) и поставлен вместо дебиановского.
Пакет закреплён `apt-mark hold xwayland`, оригинал сохранён в
`/root/Xwayland.debian`, сама заплата — в
`patches/xwayland-22.1.9-glamor-gles-bgra.patch`.

Суть правки в одном условии: если драйвер умеет
`GL_EXT_texture_format_BGRA8888`, то на пути GLES экран объявляется как
`PICT_a8r8g8b8` с `GL_BGRA_EXT` вместо `PICT_a8b8g8r8` с `GL_RGBA`. Тогда
порядок каналов совпадает с тем, что Xwayland сообщает композитору, и всё
сходится. Выключается переменной `GLAMOR_NO_BGRA`, чтобы можно было сравнить.

**Проверено измерением, а не на глаз.** X сообщает для листвы обоев
`#e5d8b2`, панель показывает `#e5d8b2`; фон стола `#31303a` и на панели
`#31303a`. До правки панель давала `#3a3031` — те же байты наоборот.

**Одной правкой ушли все три поломки показа сразу.**

- Цвета верные.
- Пустые кадры пропали: три снимка подряд одинаковые. Они и были следствием
  того же — загрузка пикселей отказывала, и содержимое портилось.
- Поток `TexSubImage2D: GL_INVALID_OPERATION` иссяк: было около 25 в минуту,
  стало **ноль за две минуты**. Причина та же — теперь загрузка идёт с
  `GL_BGRA_EXT` в образ `ARGB8888`, и строгий драйвер её принимает.
- Окно с альфой (экранная клавиатура) больше не прозрачное.

Состояние: система `running`, сбойных юнитов нет, на панели рабочий стол Xfce
с верными цветами, значками, панелью и клавиатурой.

**Итог направления.** Приложения X на этом телефоне рисуются GPU PowerVR.
Цепочка целиком: клиент X → glamor → EGL/GLES из андроидных блобов через
libhybris → буфер gralloc, выделенный нашим бэкендом GBM → dma-buf → композитор
Qt через `zwp_linux_dmabuf_v1` → панель. Ни одного программного растеризатора
на этом пути нет.

### 2026-09-16 20:25 — застывающий стол: буфер отдавался композитору до того, как GPU дорисовал

После починки цветов оставались два дефекта показа, и оба назвал хозяин, а не
приборы: **рамка выделения не стиралась после отпускания пальца**, и картинка
время от времени пропадала в чёрное, возвращаясь от любого касания.

Разбор занял несколько заходов, и почти все первые догадки оказались неверны.
Полезнее записать именно их.

**Не подтвердилось:** гонка потоков отрисовки (однопоточный режим Qt ничего не
изменил); управление простоем MediaTek (неподвижный кадр держался минуту и не
гас); нехватка кадров у композитора (постоянно движущийся квадрат шёл плавно, а
стол всё равно застывал); двойная отправка кадра в слой наложения.

**Что дало разгадку.** Три наблюдения, сложенные вместе:

1. Опыт мимо композитора, прямо через наш узел KMS: показали полосы — на панели
   сначала возник **предыдущий** кадр рабочего стола, и только повторная
   отправка того же самого кадра вывела полосы.
2. Неподвижный кадр держится сколько угодно и сам не гаснет.
3. Постоянно движущийся квадрат от композитора идёт плавно — значит и
   композитор, и вывод на панель исправны, а виновата передача между Xwayland и
   композитором.

Остаётся единственное объяснение, и оно точно ложится на «отстаёт ровно на
кадр»: **композитор забирает буфер раньше, чем GPU в него дорисовал.**

В Android кадр передаётся вместе с fence'ом — явной меткой готовности. Через
`zwp_linux_dmabuf_v1` её нет, а неявной синхронизации у буферов gralloc тоже
нет. `glamor_flush()` не спасает: `glFlush()` лишь отправляет работу в GPU, но
не ждёт. Поэтому композитор стабильно показывает предыдущее содержимое, а
свежее становится видно только когда придёт следующий кадр — отсюда и
неисчезающая рамка, и чернота после затишья.

Лечится одной строкой: `glamor_finish()` ровно перед `wl_surface_attach()`.
Заплата — `patches/xwayland-22.1.9-dmabuf-gpu-sync.patch`, выключатель
`XWL_NO_FINISH`.

Проверено хозяином: рамка стирается сразу, за две минуты покоя чернота не
появилась.

**Отдельный урок про приборы.** Два раза я сам портил то, что измерял. Проба
`gbmtest` осталась висеть на несколько часов и всё это время держала `card1`
со своим кадром на том же слое наложения, которым рисует композитор. А снимок
экрана в режиме прямой связи идёт вендорским путём через WDMA0 — он
перенастраивает живой тракт. Правило: **проба, захватывающая экран, должна
сниматься сразу**, а снимок достоверен только в режиме развязки.

### 2026-09-16 20:40 — шлейф от окон: драйвер не принимает пиксели в бесальфовый буфер

Хозяин запустил браузер и заметил, что окна оставляют шлейф — область под
передвинутым окном не стирается. И сделал наблюдение, которое сразу сузило
поиск вчетверо: **шлейф виден на снимке самого X**. Значит картинка портится
уже внутри X-сервера, а композитор, передача через dma-buf и панель получают
её готовой и ни при чём.

В журнале при этом — **2491 отказ `glTexSubImage2D` за три минуты**. Это и есть
непроисходящее стирание: glamor грузит пиксели в текстуру, драйвер отказывает,
кадр остаётся прежним.

Что оказалось неверным по дороге (каждое проверено отдельной пробой):

- не порядок каналов — сочетание BGRA/BGRA драйвер принимает;
- не шаг строки источника (`GL_UNPACK_ROW_LENGTH`) — принимает;
- не петля обратной связи, когда текстура одновременно цель рисования —
  принимает;
- не привязанный буфер распаковки — его и не было.

**Настоящая причина** нашлась пробой по всем форматам сразу:

```
XRGB8888 (буфер окна X): загрузка BGRA -> ОТКАЗ,  RGBA -> ОТКАЗ
ARGB8888:                загрузка BGRA -> принято, RGBA -> принято
ABGR8888:                загрузка BGRA -> ОТКАЗ,  RGBA -> принято
```

PowerVR **отказывается принимать пиксели в образ EGL, сделанный из
бесальфового dma-buf**. А Xwayland для глубины 24 просит у GBM ровно
`XRGB8888`. Заодно видно и второе правило: формат загрузки обязан совпадать с
форматом самого буфера, иначе тоже отказ.

**Чинить надо было у нас, и это оказалось не обходом, а исправлением лжи.** У
Android бесальфовых 32-битных форматов нет вовсе: наш бэкенд GBM на запрос
`XRGB8888` и так выделял через gralloc `BGRA_8888`, то есть буфер с альфой, а
наружу сообщал бесальфовый формат. Теперь сообщает тот, что на самом деле
выделен. Раскладка байтов у X- и A-вариантов одна и та же, так что показу это
ничем не грозит: `wl_drm_format_for_depth()` всё равно объявляет композитору
`XRGB8888`, и альфа игнорируется.

Итог: отказов загрузки ноль, шлейфа нет, Firefox рисует страницу RUTUBE с
видео целиком и правильно.

Заплаты Xwayland сведены в один файл `patches/xwayland-22.1.9-bf7-powervr.patch`:
формат экрана BGRA на пути GLES, ожидание GPU перед отдачей буфера и
диагностика отказов загрузки (включается `GLAMOR_UPLOAD_DIAG`, оставлена — на
этом железе она ещё пригодится).

### 2026-09-16 20:50 — аппаратное декодирование: кадр дошёл до 64-битного мира

Собран демон `bf7-videod` и проба к нему. Итог первого захода:

```
декодер открыт
поток подан целиком
кадр 10: буфер 640x480 шаг 640 формат 0x32315659 (YV12),
         видимая часть 0,0 640x480, dma-buf 462848 байт
  содержимое: ненулевых 4751 из 4772 проб, яркость от 0 до 247 -> КАРТИНКА ЕСТЬ
ИТОГ: получено кадров 30
```

Кадр сохранён и восстановлен в PNG — на нём тестовая таблица с цветными
полосами и счётчиком. То есть железо действительно декодирует, а не отдаёт
пустые буферы.

**Почему два процесса, а не один.** Проверил перед тем, как городить: 64-битные
библиотеки Codec2 в `/vendor/lib64` есть, и даже служба собрана в варианте
`-64b`. Но настоящий драйвер кодека `libvcodecdrv.so` существует **только
32-битным**, 64-битной версии нет вовсе. Значит декодировать обязан 32-битный
процесс, а показывать — 64-битный, где композитор и EGL. Дескриптор dma-buf
переходит между ними через `SCM_RIGHTS`, так что разрыв в разрядности не стоит
ни одного копирования.

Три поломки по дороге, все поучительные.

**Первая: `gralloc-mapper is missing`, демон падал сразу.** Не хватало
поднятых андроидных служб — `hwservicemanager`, `vndservicemanager`, службы
выделения буферов. Их поднимает наш же `hal-start.sh`, и он просто не был
запущен. Запомнить: аппаратный декодер без этих служб не живёт.

**Вторая: кадр отдавался не тем дескриптором.** Брал `data[0]`, а у буфера
gralloc их несколько, и постоянного номера у настоящего нет — ровно та же
ловушка, что с буферами GPU. Лечится той же пробой по размеру: ищем
дескриптор не меньше `stride*height`. Заодно метаданные (шаг, формат) теперь
берутся честно, через `_UnwrapNativeCodec2GrallocMetadata`.

**Третья, чисто протокольная: ответ демона был короче кадра.** Получатель
читает сообщения одним размером и различает их по первому полю, а короткий
ответ съедал начало следующего — связь рассыпалась ровно после последнего
кадра. Ответ выровнен по длине кадра.

И отдельно: **перед чтением dma-buf процессором нужна синхронизация кэша**
(`DMA_BUF_IOCTL_SYNC`). Без неё чтение отдаёт нули, и «декодировано N кадров»
выглядит успехом, а картинки нет.

Дальше по этому направлению: показать кадры на панели. Композитор уже
объявляет `NV12`/`YV12` с линейным модификатором, а у GLES есть внешние
текстуры (`GL_OES_EGL_image_external`), которые сами делают преобразование
цвета — то есть показ обойдётся без единого копирования и без шейдера
вручную.

### 2026-09-16 21:00 — аппаратное видео на панели

Проигрыватель `bf7-vplay` показывает декодированное железом видео на экране
телефона. Снимок панели во время показа подтверждает: тестовая таблица на
месте, цвета верные.

Путь кадра целиком: декодер MediaTek кладёт кадр в буфер gralloc → демон
отдаёт дескриптор dma-buf в 64-битный процесс → GPU переводит YUV в RGB
одной выборкой из внешней текстуры → тот же приём dma-buf уходит композитору
через `zwp_linux_dmabuf_v1` → панель. **Ни одного копирования процессором.**

Две поломки, обе поучительные.

**Первая: окно EGL от libhybris больше не работает.** Клиент вставал намертво
в `eglSwapBuffers`, и это следствие нашей же сегодняшней правки: композитор
переключён на клиентскую интеграцию буферов `linux-dmabuf`, а глобальный
объект `android_wlegl` — тот, которым окна EGL из libhybris отдают кадры, — при
этом исчез. Снятие интервала обмена не помогает: буфер некому забрать.

Лечится не возвратом `android_wlegl`, а отказом от окна EGL вовсе: кадр
отдаётся композитору напрямую как dma-buf. Это и проще, и честнее.

**Вторая: композитор принимает YV12, но нарисовать его не может.** Протокол
чист, буфер принят, а в журнале — `Empty shader passed to graphics pipeline` и
`Failed to build graphics pipeline state`: у Qt нет шейдера для
многоплоскостного YUV. Отсюда первая попытка и выглядела так, будто кадры
уходят в никуда.

Поэтому преобразование цвета делаем сами, и это ничего не стоит: GPU читает
кадр как внешнюю текстуру (`GL_OES_EGL_image_external`), где YUV → RGB делает
сам драйвер при выборке, и пишет результат в буфер RGB, выделенный **нашим же
бэкендом GBM**. Композитору уходит уже `ARGB8888`, который он рисует без
затруднений.

Заодно пригодилось всё, что сделано за день: свой бэкенд GBM выделяет буферы
вывода, приём `EGL_PLATFORM_GBM_KHR` в libhybris даёт безоконный контекст, а
починенные точки входа расширений — те самые `eglCreateImageKHR` и
`glEGLImageTargetTexture2DOES`, без которых ничего бы не собралось.

Мелочь, которая стоила бы часа: буферов вывода нужно несколько (сейчас три) и
занятость каждого надо отслеживать по `wl_buffer.release`. С одним буфером
композитор показывает кадр, который мы уже переписали следующим.

### 2026-09-16 21:15 — какую прослойку делать: VA-API, и почему не V4L2

Разведка по первоисточникам. Вывод: делать **драйвер VA-API**. Это обычная
разделяемая библиотека, ядерного кода не нужно вовсе, а путь V4L2 закрыт
вполне конкретным препятствием.

**Поправка к моему же предположению.** Я говорил, что Firefox не умеет V4L2.
Умеет: в `FFmpegVideoDecoder.cpp` есть обе ветки, `InitVAAPIDecoder()` и
`InitV4L2Decoder()`, обе через FFmpeg, и вторая включена по умолчанию как раз
на arm64 с GTK — то есть в дебиановской сборке она у нас есть. Сперва
пробуется VA-API, V4L2 — запасным.

**Но V4L2 для нас всё равно закрыт, и по другой причине.** Декодер FFmpeg
`h264_v4l2m2m` **всегда** просит буферы вида `V4L2_MEMORY_MMAP` и пишет в них
поток через `mmap()` — в `v4l2_context.c` другого пути просто нет. А **CUSE,
единственный способ сделать символьное устройство из пользовательского
пространства, `mmap` не поддерживает**: заплаты предлагались ещё в 2009–2012
годах и в ядро так и не вошли. Значит устройство V4L2 пришлось бы делать
настоящим драйвером ядра — больше кода, и ровно тот род риска, на котором мы
уже дважды обжигались (паника на mmap звука, падение на rmmod).

**VA-API проще, чем казалось.** Загрузчик libva требует, чтобы в таблице были
заполнены все примерно 34 указателя, иначе драйвер не примут вовсе, — но
проверяется только «не NULL». Всё, что нам не нужно (подкартинки, палитры,
атрибуты экрана), может быть однострочными заглушками. Настоящей работы
требует малая часть: создание конфигурации, контекста и поверхностей, буферы,
`vaBeginPicture`/`vaRenderPicture`/`vaEndPicture`, `vaSyncSurface` — и
`vaExportSurfaceHandle`, который libva считает необязательным, а Firefox
вызывает безусловно.

**И главное: нашлась почти точная заготовка.** `radxa-pkg/libva-v4l2` — драйвер
VA-API поверх **stateful**-декодера, то есть такого же, как наш: он тоже хочет
целые единицы доступа в Annex-B. Его `h264.cpp` — всего 341 строка — делает
ровно то, чего я опасался:

- слайсы **не восстанавливает, а просто склеивает**: приписывает
  четырёхбайтовый стартовый код и кладёт байты подряд. Данные слайса приходят
  от VA-API как есть;
- SPS и PPS **пишет заново** по полям `VAPictureParameterBufferH264`, битовым
  писателем с кодом Голомба, и кэширует их, переизлучая только при изменении.

То есть самое страшное место оказалось решённой задачей с открытым кодом под
MIT. Лицензия позволяет заимствовать.

Оценка: **примерно 1500–2500 строк**, меньше, чем у заготовки, потому что
тяжёлую часть — разговор с 32-битным блобом — у нас уже делает демон.

Риски, по убыванию:

1. **Соответствие полей SPS/PPS.** Ошибка на уровне бита (не тот
   `log2_max_frame_num_minus4`, не тот порядок зигзага в таблицах
   квантования) даст поток, который наш декодер отвергнет или разберёт криво.
   Сверять по стандарту и по заготовке.
2. **Раскладка при выдаче кадра наружу.** Тот же род ошибок, что уже ловили с
   шагами и смещениями плоскостей; плюс у нас YV12, а Firefox ждёт своего.
3. **Песочница процесса декодирования у Firefox.** Ставить драйвер надо в
   тот же `dri/`, который libva и так просматривает, — иначе открыть файл не
   дадут.
4. **Список одобренного железа у Mozilla.** PowerVR там нет, так что понадобится
   `media.hardware-video-decoding.force-enabled`.

## 2026-09-16 21:34 — VA-API: аппаратный декодер заработал из ffmpeg

30 кадров из 30, картинка совпадает с программным декодером попиксельно.
Путь: ffmpeg -> libva -> bf7_drv_video.so -> сокет -> bf7-videod (32 бита) ->
Codec2 MediaTek -> dma-buf.

Четыре разные поломки, каждая маскировала следующую.

1. **Демон отдавал кадры только перед чтением очередной просьбы.** Главная.
   Цикл в `serve()` устроен так: сначала сбросить готовое, потом заблокироваться
   на `read_all(sock)`. Клиент, который подал картинку и сел ждать кадр, ждал
   вечно: демон спал на сокете. Проверенная проба `bf7-vtest` этого не замечала,
   потому что скармливала поток целиком (каждая посылка прокручивала цикл), а
   выдачу просила один раз в конце. Лечение: `collect()` шлёт `BF7V_MSG_DRAIN`
   и читает до ответа-отчёта. Для этого `daemon_poll_frame` теперь различает
   «кадр» (1), «отчёт, кадров больше нет» (2) и «истекло время» (0).

2. **Компонент опознаёт работы по метке времени, а не по frameIndex.**
   В логе видно прямо: `CSD work returned: workId 66666` при нашей метке 66666.
   Мы давали одну метку набору параметров и картинке — компонент разруливал
   столкновение, молча сдвигая метку на единицу (66666, 66667, потом
   `configUpdate ... ts: 66668`), и кадр возвращался под номером, которого
   драйвер не знает. Отсюда `Unknown output workId`. Лечение: сквозной
   счётчик, своя метка у каждой посылки, шаг 33333 мкс.

3. **SPS без VUI.** Уровень мы писали «с запасом» (5.1), а из уровня декодер
   выводит, сколько кадров ему копить перед выдачей — на 5.1 это шестнадцать.
   Клиент VA-API ждёт первый кадр прежде чем прислать второй: взаимная
   блокировка. Лечение: честный уровень по числу макроблоков плюс VUI с
   `bitstream_restriction`, где `max_num_reorder_frames = 0`. Порядок показа
   VA-API ведёт сам, декодеру переставлять нечего. Проверка на `bf7-vtest`:
   было 2 кадра из 5 картинок, стало 5 из 5.

4. **SPS и PPS в одной посылке.** Компонент разбирает посылку с пометкой
   «настройка кодека» целиком как одну единицу и второй набор теряет.
   Лечение: по одному NAL на посылку, как делает `bf7-vtest`.

Дописано в драйвере: `vaDeriveImage` (отображение dma-buf без копии),
`vaCreateImage` и `vaGetImage` с перекладкой YV12 -> NV12. Проба ffmpeg на
`vaDeriveImage` идёт по пустой поверхности и всегда отказывает, поэтому
`derive_works` у него выключается и нужен именно `vaGetImage`.

Тонкость раскладки: у gralloc шаг цветности выравнивается до 16 байт, а
порядок плоскостей YV12 — Y, V, U. Без `DMA_BUF_IOCTL_SYNC` чтение отдаёт нули.

Запасной рычаг на будущее, если задержка первого кадра окажется велика:
`C2GlobalLowLatencyModeTuning` (`kParamIndexLowLatencyMode`) в C2Config.h.

## 2026-09-16 21:57 — Браузеры: почему ни один не видит наш VA-API

Драйвер VA-API работает (ffmpeg декодирует аппаратно), но ни Firefox, ни
Chromium до него не доходят. Причины у них **разные**, и это важно.

**Firefox 140 ESR.** На `about:support`: «Композитинг: WebRender (Software)»,
`EGL_VENDOR: Mesa Project`. То есть браузер работает на программном EGL от
Mesa и нашего GL не касается. Дальше цепочка сама себя выключает: нет
настоящего EGL -> не поднимается dmabuf -> Firefox не включает аппаратное
декодирование -> до libva дело не доходит. Никакие настройки профиля этого
не обходят: `media.hardware-video-decoding.force-enabled`,
`media.ffvpx-hw.enabled`, `widget.dmabuf.force-enabled`,
`gfx.x11-egl.force-enabled` проставлены — без толку.

Почему Mesa, а не мы: под Xwayland Firefox просит у EGL дисплей платформы
X11. Наш вендор X11 не обслуживает — он умеет андроидные окна и GBM. Glvnd
перебирает вендоров и отдаёт запрос Mesa. При этом сам вендор загружен:
в списке клиентских расширений на странице видно `EGL_KHR_platform_android`,
которого Mesa на Debian не объявляет.

Вывод по прослойкам: Xwayland нужен рабочему столу (весь Xfce — X11), но
браузеру он прямо мешает. Браузер должен стать родным клиентом Wayland.

**Chromium.** Оказался ближе. Процесс GPU доходит до нашего вендора и
пробует поднять настоящий драйвер PowerVR — в журнале говорит наш libhybris:

    library "/system/vendor/lib64/egl/libGL_mtk.so" not found
    Fatal: failed to connect to the server!

Две поломки, обе знакомые: путь к андроидному драйверу ищется в
`/system/vendor/lib64/egl/` (ловушка путей HAL), и блоб не достучался до
своего сервиса — нашим рабочим программам это даёт `hal-start.sh` с
подменой свойств, а Chromium запускается без такого окружения.

Похоже, Chromium дешевле довести до конца, чем Firefox: ему нужно окружение,
а не новый модуль Wayland.

Мелочи для повторения опытов: у этой сборки Chromium `--use-gl=egl` не
принимается («not found in allowed implementations: egl-angle»), нужен
`--use-angle=gl-egl`. Безголовый режим подменяет `chrome://gpu` на новую
вкладку, поэтому отчёт снимать бесполезно — смотреть журнал
`--enable-logging=stderr --v=1`. Экран Xorg снимается так:
`ffmpeg -f x11grab -video_size 1075x480 -i :1 -frames:v 1` (снимок панели
через debugfs показывает окно поверх и потому бывает белым).

## 2026-09-16 21:59 — Chromium: упирается в android_wlegl, и серверная половина у нас есть

Уточнение к предыдущей записи. Сообщение про `libGL_mtk.so not found` —
безобидное: libhybris сначала ищет объединённый драйвер, а потом честно
берёт раздельные `libEGL_mtk.so` и `libGLESv2_mtk.so`, которые на месте.

Настоящая поломка — строка `Fatal: failed to connect to the server!`. Она
не из блоба и не из PowerVR, а из нашего же libhybris:
`hybris/egl/platforms/wayland/eglplatform_wayland.cpp:119`. То есть Chromium
просит у EGL **платформу Wayland**, наш вендор берёт свой модуль ws для
wayland, тот зовёт `wl_display_connect(NULL)` и не находит сервера —
в окружении процесса не было `WAYLAND_DISPLAY` (у нас сокет `wayland-1`,
а по умолчанию ищется `wayland-0`).

Если это поправить, следующей стеной будет строка десятью строками выше:
`Fatal: the server doesn't advertise the android_wlegl global!` — модуль
обменивается буферами с композитором по протоколу `android_wlegl`, а наш
композитор его не объявляет (у него есть `xdg_shell`, `wl_shell`, наш
самописный `wl_drm` и `zwp_linux_dmabuf_v1` от Qt).

**Хорошая новость: серверная половина уже лежит у нас в дереве** —
`hybris/libhybris/hybris/platforms/common/server_wlegl.cpp` вместе с
`server_wlegl_buffer` и `server_wlegl_handle`. Её надо собрать с
композитором и вызвать рядом с уже работающим `bf7InstallWlDrm()` в
`hybris/bf7-gpu/gui/main.cpp`.

Это одно исправление закрывает обоих: и Chromium, и Firefox в режиме
родного клиента Wayland получают настоящий EGL, за ним поднимается dmabuf,
а за dmabuf — наш драйвер VA-API.

Окружение, которое нужно клиенту GL (снято с живого композитора):
`LD_LIBRARY_PATH=/usr/local/lib`,
`LD_PRELOAD=/usr/local/lib/libbf7tlsguard.so`,
`LIBEGL=/vendor/lib64/egl/libEGL_mtk.so`,
`LIBGLESV2=/vendor/lib64/egl/libGLESv2_mtk.so`,
`XDG_RUNTIME_DIR=/run/bf7-gui`, `WAYLAND_DISPLAY=wayland-1`.

## 2026-09-16 22:08 — Браузер поехал на настоящем GPU; декодирование пока софтовое

Вернул в композитор объявление `android_wlegl` — явным вызовом
`server_wlegl_create()` из libhybris-platformcommon, рядом с нашим
`wl_drm`. Новый файл `gui-wlegl/wlegl.cpp`, вызов в
`hybris/bf7-gpu/gui/main.cpp`. Заодно написан сборочный скрипт
`hybris/bf7-gpu/gui/build.sh`: раньше композитор собирался вручную и
команда нигде не была записана.

Предыстория, которую я упустил: `android_wlegl` объявлялся раньше сам
собой, и пропал, когда композитор перевели на клиентскую интеграцию
`linux-dmabuf` (запись от 21:0x, «окно EGL от libhybris больше не
работает»). Тогда мы обошли пропажу, отдавая кадры проигрывателя прямо
как dma-buf. Для браузеров такой обход не годится: у них окно EGL, и им
нужен именно этот протокол. Теперь объявлены оба, и одно другому не мешает.

**Итог.** Chromium 152 родным клиентом Wayland поднимается на настоящем
PowerVR: процесс GPU привязывает GL без единой жалобы (раньше падал в
`abort()` внутри нашего же `eglplatform_wayland.cpp`), окно рисуется на
панели, видео проигрывается — снимки экрана с разницей в две секунды
отличаются, значит кадры идут.

**Чего ещё нет.** Аппаратного декодирования. Демон `bf7-videod` подключений
не получает, а в журнале Chromium нет ни одной строки от его обвязки VA-API
даже при `--vmodule="*vaapi*=3"` — то есть до неё дело не доходит,
возможность отключена выше. Безголовый режим ни при чём: в окне то же самое.

Имена возможностей в этой сборке (найдены строками в двоичном файле):
`AcceleratedVideoDecodeLinuxGL`, `AcceleratedVideoDecodeLinuxZeroCopyGL`,
`VaapiIgnoreDriverChecks`. Последняя важна: Chromium сверяет название
драйвера VA-API со своим списком проверенных, и наш «BF7 hardware decoder
(MediaTek Codec2)» там не значится. Все три передавались — не помогло.
Следующее подозрение — вынос декодирования в отдельный процесс
(`UseOutOfProcessVideoDecoding`): драйвер ходит к демону через сокет в
/run, а тому процессу туда может быть закрыто.

### Разведка по Firefox (подзадача)

Отчёт по исходникам esr140 поправил две мои догадки.

1. **Firefox нигде не просит платформу X11 и не просит платформу Wayland
   через `eglGetPlatformDisplay`.** В `gfx/gl/GLLibraryEGL.cpp`
   (`CreateDisplayLocked`) для GTK он зовёт обычный `eglGetDisplay()`:
   с `wl_display*` от GDK, если стал родным клиентом Wayland, и с
   `EGL_DEFAULT_DISPLAY` во всех прочих случаях, X11 включая.
2. **Пути «взять дисплей прямо с устройства GBM» у Firefox нет вовсе** —
   короткая дорога, на которую я рассчитывал, не существует. `gbm` ему
   нужен только для выделения буферов.
3. Почему настройки не помогали: `gfxPlatformGtk::InitX11EGLConfig()`
   принудительно гасит X11-EGL, если вспомогательная проба `glxtest` не
   отчиталась «EGL». Настройки `gfx.x11-egl.force-*` управляют лишь
   попыткой, а не обходом этого отказа.
4. Наш `egl.c` в ветке обычного `eglGetDisplay` **не смотрит на
   переданный указатель** — платформу он выбирает по переменной окружения
   `HYBRIS_EGLPLATFORM`/`EGL_PLATFORM`, по умолчанию «null». Значит
   Firefox, скорее всего, лечится не кодом, а окружением:
   `MOZ_ENABLE_WAYLAND=1` плюс `HYBRIS_EGLPLATFORM=wayland`.

## 2026-09-16 22:09 — Chromium отпадает: он собран без VA-API

Проверено по двоичному файлу `/usr/lib/chromium/chromium` (152.0.7977.82-1~deb12u1,
arm64): нет ни строки `libva.so.2`, ни символов `vaInitialize` и прочих, ни
путей `media/gpu/vaapi/*.cc`. Строки с названиями возможностей
(`kVaapiIgnoreDriverChecks`, `VaapiVideoDecoder`) в файле есть — это
константы списка возможностей, — но самой поддержки не собрано. Debian
собирает Chromium под arm64 с `disable_vaapi`.

Отсюда: **никакими флагами Chromium аппаратно декодировать не заставить.**
Ровно поэтому в его журнале не было ни одной строки от обвязки VA-API —
обвязки просто нет. Все опыты с `AcceleratedVideoDecodeLinuxGL` и
`VaapiIgnoreDriverChecks` были мимо цели.

Для Chromium остались бы два пути, оба дорогие: своя сборка с `use_vaapi=true`
либо ожидание изменений в Debian. Не наш случай.

**Firefox, наоборот, собран с VA-API.** В `libxul.so` есть `libva.so.2`,
`libva-drm.so.2` и символы `vaInitialize`, `vaGetDisplayDRM`,
`vaQueryConfigProfiles`, `vaCreateContext`, `vaDeriveImage`,
`vaExportSurfaceHandle` — последние два мы в своём драйвере как раз и
написали. Значит цель — Firefox, и подход к ней уже намечен разведкой:
`MOZ_ENABLE_WAYLAND=1` плюс `HYBRIS_EGLPLATFORM=wayland`.

## 2026-09-16 22:22 — Firefox: как он на самом деле ищет видеокарту, и наш EGL_EXT_device_query

Firefox поехал на нашем GPU: хватило двух переменных окружения —
`MOZ_ENABLE_WAYLAND=1` (чтобы GDK стал родным клиентом Wayland, а не пошёл
через Xwayland) и `HYBRIS_EGLPLATFORM=wayland` (чтобы наш `eglGetDisplay`
выбрал модуль wayland, а не «null» по умолчанию). Правок кода для этого не
понадобилось.

Но аппаратного декодирования всё равно не было, и в выводе стояло:

    [GFX1-]: No GPUs detected via PCI
    [GFX1-]: vaapitest: VA-API test failed: failed to open renderDeviceFD.

**Firefox не ищет /dev/dri сам.** Его вспомогательная проба `glxtest`
(toolkit/xre/glxtest/glxtest.cpp) узнаёт путь к узлу отрисовки исключительно
через EGL:

    eglQueryDisplayAttribEXT(dpy, EGL_DEVICE_EXT)      -> устройство
    eglQueryDeviceStringEXT(устр., EGL_DRM_DEVICE_FILE_EXT) -> путь

и печатает его как `DRM_RENDERDEVICE`. Только этот путь потом попадает в
настоящую пробу `vaapitest`. Наш вендор расширения `EGL_EXT_device_query`
не знал — путь оставался пустым, проба падала, и дальше всё выглядело так,
будто на устройстве нет аппаратного декодирования вовсе.

Дописал расширение в `hybris/egl/egl.c`: `eglQueryDevicesEXT`,
`eglQueryDeviceAttribEXT`, `eglQueryDeviceStringEXT`,
`eglQueryDisplayAttribEXT`, плюс объявление
`EGL_EXT_device_base EGL_EXT_device_enumeration EGL_EXT_device_query` в
строке клиентских расширений и четыре записи в таблице `eglGetProcAddress`
(в сборке для glvnd символы скрыты, диспетчер добирается до них только так).
Устройство — одиночка, узел ищется перебором `/dev/dri/renderD12[8-9]...`
и переопределяется переменной `BF7_DRM_RENDER_NODE`.

**Чего про себя намеренно не говорим:** `EGL_MESA_device_software`. Увидев
это расширение на устройстве, Firefox записывает отрисовку в программные и
гасит всё ускорение.

Итог правки: ошибка `failed to open renderDeviceFD` исчезла, и в журнале
подсистемы декодирования впервые появилось
`Support AV_CODEC_ID_H264 for hw decoding`.

**Что осталось.** При настоящем воспроизведении аппаратный модуль всё ещё
отказывается брать `video/avc` («FFmpeg decoder rejects requested type»), а
берёт встроенный ffvpx программно. Проверки шли в безголовом режиме, а он
сам по себе гасит dmabuf и Wayland-часть виджетов, от которых зависит
`CanUseHardwareVideoDecoding()`. Следующая проверка — в окне.

## 2026-09-16 22:31 — Аппаратный GL в обычном окне X11: платформа x11 для libhybris

Заработало. Проба `hybris/bf7-gpu/x11gltest.c` открывает обычное окно X11 на
рабочем столе Xfce и рисует в него через наш GPU:

    EGL 1.5, поставщик: Imagination Technologies
    GL_RENDERER: PowerVR Rogue GE8300
    GL_VERSION:  OpenGL ES 3.2 build 1.15@6070602
    ИТОГ: кадров 40699 за 20.0 с (2035.0 в секунду)

На снимке экрана — окно с рамкой xfwm4, кнопками свёртывания и закрытия, и
записью в панели задач. То есть именно то, чего не хватало: не полноэкранный
захватчик сеанса, а обычное окно среди прочих.

**Что написано.** Новая платформа `hybris/egl/platforms/x11/`:
`eglplatform_x11.cpp` (модуль ws) и `x11_window.{h,cpp}` (окно
ANativeWindow). Собирается своим `build.sh` вне autotools, как и mtkfb.

**Как устроено.** Буфер выделяется обычным gralloc; из `native_handle`
достаётся дескриптор dma-buf (перебором по размеру, как в бэкенде GBM);
дескриптор отдаётся серверу X по DRI3 (`xcb_dri3_pixmap_from_buffer`) и
становится Pixmap; показ — расширением Present (`xcb_present_pixmap`).
Освобождение буфера ловится событием idle-notify: до него переиспользовать
буфер нельзя, иначе на экране окажется кадр, который ещё рисуется.

**Почему это вообще возможно.** Xwayland объявляет DRI3 и Present (проверено
`xdpyinfo`), а наш бэкенд GBM умеет `GBM_BO_IMPORT_FD` — значит сервер
принимает наш буфер по дескриптору. Круг замкнулся: Xwayland рисует через
наш GBM, а клиенты отдают ему буферы того же gralloc.

**Ловушки по дороге.**
1. `hybris_gralloc_initialize` объявлена в `hybris/gralloc/gralloc.h`, а не
   в `eglplatformcommon.h`.
2. `eglplatformcommon.h` **не имеет** своей обёртки `extern "C"`, а символы
   в библиотеке — с привязкой C. Включать его надо внутри `extern "C" {}`,
   иначе ws-модуль не загрузится: `undefined symbol:
   _Z32eglplatformcommon_eglQueryString...`, и `ws_init` падает на assert.
3. `-lGLESv2` на этой системе нет, есть только `libGLESv2.so.2` — линковать
   надо `-l:libGLESv2.so.2`.

**Наблюдение про скорость.** 2035 кадров в секунду для окна 320x240 значит,
что вертикальная синхронизация нас не держит. Для оконного (не
полноэкранного) показа Xwayland копирует Pixmap в буфер окна и сразу
отпускает наш буфер. Копия эта на GPU, процессор не участвует. Для
полноэкранного окна должен включиться обмен страницами без копии.

**Как пользоваться:** `DISPLAY=:1 HYBRIS_EGLPLATFORM=x11` плюс обычное
окружение клиента GL (`LD_LIBRARY_PATH=/usr/local/lib`,
`LD_PRELOAD=/usr/local/lib/libbf7tlsguard.so`). Отладочный вывод —
`BF7_X11_TRACE=1`.

## 2026-09-16 22:45 — Почему Firefox всё ещё на Mesa: разведка по своей истории

Проверяли, не осталось ли от прошлых заходов чего-то заданного руками.
**Не осталось.** Файлы вендоров glvnd в порядке: `10_libhybris.json`
действующий, указывает на существующую `libEGL_libhybris.so.0`, и по алфавиту
идёт раньше `50_mesa.json`. `/etc/glvnd/egl_vendor.d/` пуст. `ld.so.conf`,
`/etc/environment`, `/etc/profile.d`, юниты systemd — чисто. В настройках
Firefox ничего вредного мы себе не проставили.

Два хвоста для уборки, к делу не относятся: рядом с файлами glvnd лежит
устаревший `README.bf7`, где описано состояние `10_libhybris.json.disabled`,
которого давно нет; и есть забытый `/usr/local/bin/firefox-wayland.sh` со
своим отдельным профилем `/root/.ff-wayland`.

**Настоящая причина — из двух частей.**

1. **Дыра у нас.** В `__eglHybrisGetPlatformDisplayCommon` (egl.c) нет
   разбора `EGL_PLATFORM_X11_KHR`/`EGL_PLATFORM_X11_EXT`: такой запрос
   падает в `default:` и возвращает `EGL_NO_DISPLAY`, после чего очередь
   glvnd доходит до Mesa, и та честно идёт искать драйвер DRI по имени
   «pvr» — отсюда `MESA-LOADER: failed to open pvr`. Ни в `egl.c`, ни в
   `glvnd/eglglvnd.cpp` слова X11 нет вовсе.

2. **Гейт у Firefox.** `gfxPlatformGtk::InitX11EGLConfig()` гасит
   возможность X11-EGL на весь процесс, если вспомогательная проба
   `glxtest` не отчиталась об успехе EGL заранее. Настройки
   `gfx.x11-egl.force-*` управляют только самой попыткой, а не обходом
   этого отказа.

**Своё измерение, которое сужает картину.** Запустил Firefox с
`BF7_EGL_TRACE=1` (трассировка уже была вшита в egl.c) — наш вендор не
напечатал **ни одной** строки, хотя подпорка TLS в тех же процессах печатала
исправно. Значит Firefox `eglGetDisplay` вообще не звал: он с самого начала
идёт по GLX, а не по EGL. Отсюда вывод: правка (1) нужна и правильна, но
сама по себе не поможет — сперва должен открыться гейт (2).

Порядок работ: сначала добавить платформу X11 в `egl.c` и объявить
`EGL_EXT_platform_x11 EGL_KHR_platform_x11` — это нужно и другим программам
(Qt, SDL просят платформу явно), — а потом разбираться с пробой glxtest.

## 2026-09-16 22:53 — Платформа X11 в диспетчере EGL

Дописан случай `EGL_PLATFORM_X11_KHR` в `__eglHybrisGetPlatformDisplayCommon`
(`hybris/egl/egl.c`) — он ведёт на модуль ws «x11». Раньше такой запрос
проваливался в `default:`, возвращал `EGL_NO_DISPLAY`, и очередь glvnd
уходила к Mesa. В строку клиентских расширений добавлены
`EGL_EXT_platform_x11 EGL_KHR_platform_x11`.

Проверено обоими способами, которыми программы спрашивают дисплей:

    платформу не называю (как Firefox)     -> PowerVR Rogue GE8300, 2035 к/с
    спрашиваю платформу X11 явно (как Qt)  -> PowerVR Rogue GE8300, 2040 к/с

В пробу `x11gltest` добавлена переменная `BF7_PLATFORM_X11=1` — она
переключает на явный `eglGetPlatformDisplayEXT`, это другая ветка
диспетчера и проверять её надо отдельно.

Дальше по цели (docs/bf7-native/hw-accel-goal.md) — проверка на обычной
программе из репозитория Debian, а не на своей пробе. Ни mpv, ни vlc, ни
glmark2 на телефоне не стояло; ставлю mpv: он умеет и GL через EGL
(`--gpu-context=x11egl`), и декодирование через VA-API (`--hwdec=vaapi`),
то есть закрывает обе половины цели сразу.

## 2026-09-16 23:24 — mpv из репозитория Debian: аппаратный GL и аппаратный декодер

Обычная программа, не наша проба: `mpv` из Debian. Итог одного запуска —

    Using hardware decoding (vaapi-copy).
    VO: [gpu] 640x480 nv12
    GL_RENDERER: PowerVR Rogue GE8300
    процессор: mpv 11%, bf7-videod 4%

Окно с рамкой xfwm4 и записью в панели задач, видео идёт, цвета верные.
Демон `bf7-videod` в журнале: «клиент подключился», «декодер открыт».

Запускается так:

    DISPLAY=:1 XDG_RUNTIME_DIR=/run/bf7-gui HYBRIS_EGLPLATFORM=x11 \
    LD_PRELOAD=/usr/local/lib/libbf7tlsguard.so \
    mpv --gpu-context=x11egl --vo=gpu --hwdec=vaapi-copy файл

**`LD_LIBRARY_PATH=/usr/local/lib` задавать нельзя** — наша заглушка
libOpenCL перекрывает системную, и ffmpeg внутри mpv падает по сегментации.
Вендор EGL и так находится по своему файлу описания glvnd.

### Что пришлось починить по дороге

1. **Окно приезжало указателем, а не номером.** По стандарту EGL 1.5
   `eglCreatePlatformWindowSurface` принимает **указатель** на окно, а
   `eglCreateWindowSurface` — сам номер. Для андроидных платформ и Wayland
   разницы нет, поэтому в libhybris на этом месте стояла заметка «TODO», и
   указатель уходил дальше как есть. Симптом: номером окна становился
   усечённый до 32 бит указатель, сервер отвечал BadDrawable, буферы
   выходили 1x1, окно оставалось пустым. Старшие биты «номера» менялись от
   запуска к запуску — по этому подмена и опознана.
2. **Номер визуала X.** Драйвер PowerVR отдавал своё внутреннее значение, и
   mpv останавливался на «Getting X visual failed!». Теперь `egl.c`
   подставляет настоящий TrueColor нужной глубины; функция поиска живёт в
   модуле x11 и достаётся через `eglGetProcAddress` по особому имени, чтобы
   не править общий интерфейс ws_module.
3. **Порядок байтов.** Выделяли RGBA, а визуал X хранит пиксель как B,G,R,X.
   Красный и синий менялись местами. Теперь буфер всегда BGRA, а просьбу
   драйвера о формате мы не слушаем: раскладку задаёт сервер.
4. **Очередь событий.** Запросы DRI3 и Present шлём по **своему**
   подключению, даже если приложение дало своё: в связке Xlib и xcb очередью
   владеет Xlib и забирает наши ответы себе. Размеры окна, наоборот,
   спрашиваем через Xlib приложения — иначе окна ещё «нет» на сервере.
5. **Поверхность без памяти.** Проигрыватели сразу после `vaCreateSurfaces`
   просят вывезти поверхность дескриптором, чтобы проверить формат. У нас до
   первого кадра за поверхностью не было ничего, вывоз отказывал, и mpv
   решал, что аппаратный декодер не умеет ни одного формата. Теперь при
   первом обращении заводится свой буфер через gralloc (библиотека
   подгружается по необходимости, чтобы не тащить libhybris в каждый процесс
   с libva).
6. **Утечка буферов декодера.** Получая в поверхность новый кадр, мы закрывали
   дескриптор старого, но не возвращали его демону. Тот держал буфер у себя,
   пул декодера кончался, кадры переставали приходить — и ровно через сотню
   кадров mpv объявлял «Failed to sync surface» и уходил на процессор.
   Теперь прежний кадр освобождается.

### Чего пока нет: показа совсем без копий

`--hwdec=vaapi` (без `-copy`) не заводится, и причина в железе. Спросили у
EGL напрямую, какие форматы он готов втянуть дескриптором:

    AB30 AR24 AB24 XR24 XB24 RG16 NV12 NV21 YV12 YU12 YUYV

Однобайтового `R8` в списке нет. А проигрыватели (и mpv, и Firefox) втягивают
кадр YUV **по плоскости за раз**: яркость как R8, цветность как GR88. Наш
драйвер теперь и отдаёт три слоя R8, как это делают драйверы Mesa, но EGL
отвечает `EGL_BAD_MATCH`.

То есть путь без копий на этом GPU возможен только целым кадром NV12/YV12
через `GL_OES_EGL_image_external` — так работает наш собственный
проигрыватель. Ни mpv, ни Firefox так не умеют: им нужен R8.

Остаётся либо мириться с одной копией на кадр (сейчас это 11% процессора на
640x480), либо отдавать кадр мимо EGL — сразу композитору через
`zwp_linux_dmabuf_v1`, как делает `bf7-vplay`. Для второго нужен композитор,
умеющий рисовать многоплоскостной YUV; наш на Qt этого не умеет.

## 2026-09-16 23:35 — Попытка показа без копий через RGB: упёрлись в ffmpeg

Раз драйвер PowerVR не принимает дескриптором отдельные плоскости (нет R8),
попробовали обойти: пусть декодер отдаёт кадр сразу в RGBA. Преобразование
всё равно делает MDP — железо, — а кадр становится одним слоем, который EGL
берёт без возражений.

Сделано и **работает** до последнего шага:

- демон умеет выдавать RGBA по переменной `BF7V_RGB=1`
  (`C2StreamPixelFormatInfo::output = HAL_PIXEL_FORMAT_RGBA_8888`);
  проверено `bf7-vtest`: 30 кадров из 30, формат 0x1, 640x480x4 байта;
- драйвер по `BF7VA_RGB=1` объявляет поверхности `VA_FOURCC_RGBA`,
  формат образа RGBA, `VA_RT_FORMAT_RGB32` в свойствах конфигурации и
  вывозит кадр одним слоем `AB24` (DRM_FORMAT_ABGR8888);
- mpv проходит пробу форматов: **`Supported formats: rgba`** и берётся за
  аппаратный путь.

**Где встало.** `[ffmpeg/video] h264: No usable formats for decoding!` —
ffmpeg не соглашается декодировать H.264 в поверхности RGB. Это его
внутреннее ограничение, к нашему драйверу отношения не имеющее.

Поэтому режим RGB оставлен как есть — выключенным по умолчанию и доступным
по двум переменным. Он ещё пригодится: тому, кто берёт кадр у VA-API сам, без
посредничества ffmpeg, он даст показ вовсе без копий.

### Состояние цели (docs/bf7-native/hw-accel-goal.md)

| Признак | Как | Итог |
|---|---|---|
| Отрисовка на PowerVR | mpv, `GL_RENDERER` | **есть** |
| Аппаратное декодирование | `Using hardware decoding (vaapi-copy)`, демон подключился | **есть** |
| Нормальное окно | рамка xfwm4, панель задач | **есть** |
| Кадр без копий процессором | — | **нет**, упирается в железо |

Нагрузка при проигрывании 640x480: mpv 11%, демон 1%. Для сравнения, чисто
программное декодирование того же файла — 31%.

**Что мешает последнему пункту, одной фразой:** проигрыватели разбирают кадр
YUV по плоскостям (R8 и GR88), а драйвер PowerVR принимает дескриптором
только целые кадры (NV12, YV12, AB24 и подобные). Обход через RGB упирается
в ffmpeg. Остаётся либо путь мимо ffmpeg (своя программа — у нас такая есть,
`bf7-vplay`, и она показывает без единой копии), либо правка проигрывателя.

## 2026-09-17 00:08 — Показ без копий заработал: GStreamer

Все четыре признака цели закрыты обычной программой из репозитория Debian —
GStreamer 1.22 с плагином vaapi:

    DISPLAY=:1 XDG_RUNTIME_DIR=/run/bf7-gui HYBRIS_EGLPLATFORM=x11 \
    LD_PRELOAD=/usr/local/lib/libbf7tlsguard.so GST_VAAPI_ALL_DRIVERS=1 \
    GST_GL_PLATFORM=egl GST_GL_API=gles2 GST_GL_WINDOW=x11 \
    gst-launch-1.0 filesrc location=файл ! qtdemux ! h264parse ! \
                   vaapih264dec ! glimagesink

На экране обычное окно с рамкой xfwm4 и записью в панели задач, картинка
верная, цвета верные.

**Почему вышло у него, а не у mpv.** GStreamer втягивает кадр YUV **целиком**,
одной внешней текстурой (`gst_egl_image_from_dmabuf_direct_target`), а mpv и
Firefox разбирают его по плоскостям как R8 и GR88. Драйвер PowerVR отдельные
плоскости дескриптором не принимает — в его списке только целые кадры.

**Доказательство, что копий нет.** В драйвер добавлена жалоба на отображение
кадра в память. За прогон: кадров выдано дескриптором — 9, отображений в
память — 1 (на согласовании формата). В установившемся режиме процессор
кадра не касается вовсе.

### Что для этого дописано

1. **`vaAcquireBufferHandle` и `vaReleaseBufferHandle`** — GStreamer берёт
   дескриптор именно ими (предшественник `vaExportSurfaceHandle`). Ловушка:
   `VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME` — это `0x20000000`, а не
   `0x40000000` (то `DRM_PRIME_2`). С неверной константой запрос отвергался
   молча.
2. **`eglCreateImage` и `eglDestroyImage`** в libhybris — имена без
   суффикса KHR, из EGL 1.5. GStreamer зовёт именно их; без переходников
   диспетчер glvnd уводил вызов к Mesa, та видела чужой дисплей и отвечала
   `EGL_BAD_DISPLAY`.
3. **Ожидание кадра внутри `vaDeriveImage`.** GStreamer не зовёт
   `vaSyncSurface` вовсе: у настоящих драйверов поверхность готова уже к
   моменту вывоза. У нас кадр приходит от демона, и ждать приходится самим —
   иначе клиент получал пустой запасной буфер и рисовал зелёный экран.
4. **Объявлять надо то, что отдаёшь.** Мы объявляли поверхности как NV12, а
   отдавали YV12 — GStreamer разбирал кадр по чужой раскладке, картинка
   выходила сдвоенной и в неверных цветах.
5. **Возврат кадра демону по `vaReleaseBufferHandle`**, а не при повторном
   использовании поверхности. Иначе у декодера кончались буферы: их меньше,
   чем поверхностей заводит проигрыватель, и демон падал с «Too many open
   files». Возвращать на `vaDestroyImage` нельзя — GStreamer уничтожает образ
   сразу, а рисует из дескриптора потом.
6. **`send` с `MSG_NOSIGNAL` вместо `write`.** Если демон исчезал, `write`
   присылал SIGPIPE и убивал программу-хозяина целиком. Мы библиотека внутри
   чужого процесса — так нельзя.
7. **Отложенное отображение буфера.** Раньше `vaDeriveImage` отображал
   полмегабайта со сбросом кэша на каждый кадр, даже когда клиенту нужен был
   только дескриптор. Теперь это делает `vaMapBuffer` — платит тот, кто
   правда читает пиксели.
8. **Демон стал службой** (`bf7-videod.service`, `Restart=always`). До того
   он падал вместе с жёстко убитым клиентом, и следующий запуск получал
   «демон декодирования недоступен».

### Чего не хватает: скорости

Проигрывание идёт примерно вчетверо медленнее реального времени: за двадцать
секунд — девять кадров. Само декодирование при этом быстрое: с `fakesink`
проходит шестьсот кадров за двадцать секунд, то есть тридцать в секунду.
Значит упирается не декодер, а согласование: клиент просит кадр сразу после
подачи картинки, а конвейеру MediaTek нужно несколько картинок вперёд.
Принудительный сброс конвейера (`drain_nb` после каждой картинки) кадр
достаёт, но обходится в три четверти процессора; оставлен под переменной
`BF7VA_DRAIN_EACH`.

Это уже задача на производительность, а не на устройство пути.

## 2026-09-17 08:17 — Firefox поехал на GPU; два зрительных дефекта и их причины

Firefox наконец берёт наш GPU. В `about:support`: «Видеокарта №1 — активна,
PowerVR Rogue GE8300, Imagination Technologies», в журнале решений
`HW_COMPOSITING`, `OPENGL_COMPOSITING`, `WEBRENDER` — все `available`,
в списке расширений WebGL появились `GL_IMG_*`, которых у Mesa не бывает.

Открыла дверь платформа X11 в диспетчере EGL, дописанная ради mpv: в следе
видно `getPlatformDisplay: платформа 0x31d5, ws "x11"` — это
`EGL_PLATFORM_X11_KHR`. Нужны переменные `HYBRIS_EGLPLATFORM=x11` и
`MOZ_X11_EGL=1`.

### Дефект первый: страница дрожала на высоту одного шага

Запись экрана без единого касания: за десять секунд экран имел **ровно два
различимых состояния** и метался между ними, каждое изменение одной и той же
величины. Разностная картинка показала, что состояния отличаются постоянным
вертикальным сдвигом — каждый элемент удвоен.

Причина — частичная перерисовка. Firefox дорисовывает только изменившийся
кусок, полагаясь на «возраст буфера» (`EGL_EXT_buffer_age`): сколько кадров
назад в этом буфере была картинка. Наша платформа X11 буферы вращает сама и
возраст не сообщает, поэтому свежий кусок ложился поверх чужого старого
кадра. Обход: `gfx.webrender.max-partial-present-rects = 0`. Правильное
лечение на будущее — сообщать настоящий возраст буфера.

### Дефект второй: возврат к предыдущему кадру

Самый заметный на видео. Причина нашлась в нашем же коде: в
`x11_window.cpp` метка готовности от драйвера выбрасывалась —

    if (fenceFd >= 0)
            close(fenceFd);

То есть буфер показывался, пока GPU в него ещё рисовал, и сервер брал
наполовину старое содержимое. Теперь перед показом ждём:
`sync_wait(fenceFd, 1000)`. Отдать метку серверу нельзя — Present умеет
ждать только свои объекты синхронизации X, а у нас метка ядра.

Хозяин подтвердил, что видео стало плавным, — **но причинной связи это не
доказывает**, и я поторопился её приписать. Контрольный опыт: тот же путь
через проигрыватель (mpv на `--gpu-context=x11egl`, ролик со вшитым
счётчиком), запись экрана, счёт возвратов к позапрошлому кадру. С правкой и
без неё (переключатель `BF7_X11_NO_FENCE=1`) — **по нулю возвратов в обоих
случаях**. То есть на простой полноэкранной отрисовке проигрывателя дефект не
воспроизводится, и чем именно помогла правка, пока не установлено.

Правка при этом верна по существу: показывать буфер, в который GPU ещё
рисует, нельзя. Но записывать её себе в заслугу за наблюдение хозяина
оснований нет.

### Чем оно работает сейчас (замеры под проигрыванием)

| Что | Сколько |
|---|---|
| GPU | 62–81% на полной частоте 660 МГц |
| Процессор, все процессы Firefox | 83% |
| Аппаратный декодер | **не задействован** |

То есть отрисовка честно на GPU, а видео декодируется процессором — отсюда и
остаточная вялость интерфейса: процессор занят целиком.

Почему декодер не берётся: демон видит подключение и сразу отключение, а в
журнале Firefox — `VideoBridgeParent receives IPC close with
reason=AbnormalShutdown`, трижды. Процесс декодирования падает на попытке.
Рядом жалоба `DMABufSurface::UseDmaBufExportExtension(): MESA_image_dma_buf
import/export extensions!` — наш EGL не объявляет
`EGL_MESA_image_dma_buf_export`, которым Firefox достаёт дескриптор из
картинки EGL. Это следующая ниточка.

## 2026-09-17 08:28 — fastfetch: два хвоста

На экране fastfetch отрабатывает и называет железо верно:
`MT6761 (4) @ 2.00 GHz`, `Imagination Technologies PowerVR GE8300 (1) @
0.39 GHz [Integrated]`, память, диск, адрес, батарея.

Два дефекта:

1. **Посторонние строки в середине вывода.** Наш загрузчик libhybris ищет
   `libGLES_mali.so` (вариант для Mali) и, не найдя, пишет об этом в поток
   ошибок. У fastfetch вывод свёрстан колонками, и эти строки его ломают.
   Лечится тем, чтобы загрузчик молчал о заведомо необязательных именах.
2. **`Segmentation fault` после вывода.** Печатает всё, падает в конце.

Отдельно записан TODO хозяина: аппаратное декодирование в браузере есть, но
работает неверно — подробности в docs/bf7-native/hw-accel-goal.md.

## 2026-09-17 08:42 — Падение fastfetch: два разных дефекта, один поверх другого

### Первый: блоб OpenCL от PowerVR

Посмертный дамп (`ulimit -c unlimited`, ядро пишется в текущий каталог)
показал адрес возврата в `/vendor/lib64/libPVROCL.so`. Цепочка:
fastfetch опрашивает GPU -> `libOpenCL.so.1` -> наша обёртка из
`/usr/local/lib` -> `/vendor/lib64/libOpenCL.so` -> `libPVROCL.so` -> падение.

Почему находилась именно наша: `ldconfig -p` показывал две —
`/lib/aarch64-linux-gnu/libOpenCL.so.1` (загрузчик ICD от системы) и нашу.
Наша выигрывала.

Сам блоб в обычной программе непригоден: без подпорки TLS падает, с
`LD_PRELOAD=libbf7tlsguard.so` — зависает. Ему нужно всё андроидное
окружение, как демонам из /vendor.

**Сделано:** обёртка убрана из общего пути в
`/usr/local/lib/libhybris-optional/` вместе с пояснением, `ldconfig`
перечитан. Теперь находится только системный загрузчик ICD, который честно
сообщает, что платформ нет. Тем же самым, кстати, объясняется падение ffmpeg
внутри mpv, когда `/usr/local/lib` попадал в `LD_LIBRARY_PATH`.

### Второй: наш драйвер KMS вешает card1 намертво

После правки fastfetch перестал падать, но стал зависать в состоянии D.
Стек назвал причину:

    fastfetch  drm_modeset_lock <- drm_mode_getcrtc     (ждёт)
    lagtest    bf7_fb_dirty <- tinydrm_fb_dirty <- drm_mode_dirtyfb_ioctl

`lagtest` — моя же проба, забытая запущенной с ночи, — застряла внутри
`bf7_fb_dirty` и держит замок смены режима. Всё, что после этого открывает
`card1` и хоть что-то спрашивает, встаёт в очередь навсегда и **не
убивается**: пять процессов fastfetch так и висят в D.

Где именно блокировка: `bf7_post_frame()` зовёт
`primary_display_trigger(1 /* блокирующий */, NULL, 0)`. Блокирующий запуск
был выбран нарочно, чтобы буфер дожил до показа. Но когда панелью владеет
другой сеанс (композитор выводит слоем наложения через `/dev/mtk_disp_mgr`),
этот запуск не возвращается вовсе.

**Чинить так:** в `bf7_fb_dirty` нельзя уходить в неограниченное ожидание.
Нужно либо не пускать показ, когда панель занята другим сеансом, либо
ограничить ожидание по времени и вернуть `-EBUSY`. Пока не сделано.

**Сейчас:** `card1` отравлен до перезагрузки — замок держат процессы в D.
Рабочий стол это не задевает: композитор ходит через `disp_mgr`, Xwayland —
через узел отрисовки. Но всякая программа, опрашивающая DRM, зависнет.

**Урок, уже знакомый:** забытая проба, держащая устройство, отравляет
посторонние программы. Ровно так же `gbmtest` держал `card1` часами. Пробы
надо закрывать сразу, а не «потом».

## 2026-09-17 09:06 — Обе починки: молчаливый загрузчик и небесконечное ожидание в KMS

### Мусор в чужом выводе

В порте загрузчика `hybris/common/q/linker_globals.h` макрос `DL_ERR` печатал
сообщение в поток ошибок **всегда**. В настоящем bionic он только запоминает
его для `dlerror()`. А неудачный поиск библиотеки для этого загрузчика —
обычное дело: блобы перебирают имена (`libGLES_mali.so`, `libpq_cust.so`,
`libGL_mtk.so`) и спокойно живут дальше. В итоге строки лезли в вывод
посторонних программ и, например, ломали вёрстку колонками у fastfetch.

Теперь печать под условием `HYBRIS_LINKER_VERBOSE=1`, по умолчанию тихо.
Проверено на `x11gltest`: посторонних строк 0, полезный вывод цел
(`PowerVR Rogue GE8300`, 2336 кадров за 2 с).

### Небесконечное ожидание в bf7_fb_dirty

Две правки в `bf7_drm.c`:

1. Вместо `mutex_lock(&tdev->dirty_lock)` — попытки в течение ста
   миллисекунд, потом `-EBUSY`. Это лекарство от лавины: вызов приходит из
   ioctl, который держит замок смены режима всего устройства DRM, и один
   застрявший поток утягивал за собой каждого, кто потом хоть что-то
   спрашивал у `card1` — причём в состояние D, без права быть убитым.
2. Признак `atomic_t posting`: пока вендорский вызов
   (`primary_display_config_input_multiple` и `primary_display_trigger`) не
   вернулся, следующие вызовы отвечают отказом сразу.

Первопричину — блокирующий `primary_display_trigger(1, ...)`, который не
возвращается, когда панелью владеет другой сеанс, — это не убирает. Но
превращает «намертво и навсегда» в «отказ и живём дальше». Настоящее лечение
(не пускать показ, когда панель занята чужим сеансом) остаётся на потом.

Ядро `4.19.325-cip124-st8-bf7-x657b-v1` собрано, модули связности
пересобраны тем же заходом. Запись и перезагрузка ждут слова хозяина;
перезагрузка заодно снимет отравление с `card1`.

## 2026-09-17 09:15 — Записано и проверено: fastfetch отрабатывает целиком

Образ `c27df7ddad2581b8` записан в `boot_a`, телефон перезагрузился,
рабочий стол поднялся. Проверка:

    fastfetch: код возврата 0, посторонних строк 0, вывод до конца
    процессов в состоянии D: 13 — все потоки ядра (сторож зависаний,
      батарея, зарядка, работники дисплея), ничего не держит card1
    службы bf7-session и bf7-videod: active

Оба дефекта закрыты: падение (блоб OpenCL убран из общего пути) и мусор в
выводе (загрузчик замолчал). Отравление `card1` снято перезагрузкой.

### Ловушка сборки, которую едва не пропустил

Скрипт `build-bf7-x657b.sh` по умолчанию берёт `configs/bf7-x657b-v1.config`,
а работающее ядро собрано из `bf7-x657b-v2.config`. Разница не косметическая:
в v1 **нет нашего драйвера вовсе** (`CONFIG_DRM_BF7_DISP`, `DRM_TINYDRM`,
`DRM_KMS_CMA_HELPER`) и нет настроек cgroup (`CGROUP_PIDS`, `CGROUP_DEVICE`,
`IPC_NS`), без которых виснет systemd — то самое, что выглядит как
незагрузка.

Поймано сравнением `.config` новой сборки с `.config` работающего ядра.
**Правило: перед упаковкой сверять настройки сборки с настройками ядра,
которое сейчас работает.** Верный вызов:

    CONFIG=kernel-work/configs/bf7-x657b-v2.config \
    OUT=kernel-work/build/bf7-x657b-clang11-v2 \
    sh kernel-work/scripts/build-bf7-x657b.sh

### 2026-09-17 09:45 — чёрный экран после перезагрузки: чужая EGL-ошибка съела рабочий стол

После прошивки нового ядра панель показывала ровный тёмно-синий цвет. Первое,
что сузило поиск: этот цвет — `#0b0f14`, то есть **фон нашего же композитора**
из `compositor.qml` (в снимке панели байты 11/15/20). Значит композитор жив,
кадры в панель уходят, а не показывается ровно одно — рабочий стол. Ядро и
драйвер дисплея ни при чём, хотя подозревали в первую очередь их.

Протокольный журнал Wayland (`BF7_XW_DEBUG=1`) подтвердил: Xwayland создаёт
`xdg_toplevel`, отвечает на конфигурацию — и **ни разу не делает `attach`**. В
его выводе одна строка: `Error getting buffer`.

Дальше пришлось идти по коду Xwayland с временными метками (собирается на
телефоне, `ninja` пересобирает один файл за полминуты). Цепочка:

1. `xwl_window_post_damage` не получил `wl_buffer`;
2. у пиксмапа не оказалось спутника `xwl_pixmap` — то есть он создан **не через
   gbm**, а обычным `glamor_create_pixmap` по запасному пути;
3. запасной путь включился потому, что `xwl_glamor_gbm_create_pixmap_for_bo`
   отказал: `glEGLImageTargetTexture2DOES` → `eglGetError()` = `0x300C`
   (`EGL_BAD_PARAMETER`).

Проба, повторяющая то же самое вручную (gbm → `eglCreateImageKHR` из dma-buf →
привязка к текстуре), принимала **все** форматы и размеры, включая злополучный
1075×480. Значит ошибка не от этого вызова. Метка, снявшая `eglGetError()`
**до** привязки, показала: ошибка уже висела.

**Причина.** Вчера в libhybris я объявил в строке клиентских расширений
`EGL_EXT_device_base` и `EGL_EXT_device_enumeration` (нужны были Firefox'у —
ему хватает `EGL_EXT_device_query`). По `device_base` Xwayland решает, что
стоит попробовать запасной путь EGLStream: зовёт `eglQueryDevicesEXT`, а затем
`eglGetPlatformDisplayEXT(EGL_PLATFORM_DEVICE_EXT, …)`. Такой платформы мы не
знаем и законно отвечаем отказом — Xwayland это переживает и уходит на GBM, но
**EGL-ошибку не забирает**. А `eglGetError()` липкая: она дожила до первой же
проверки внутри glamor после привязки образа к текстуре. Ошибка одна, съедается
одной проверкой — поэтому и отказ был ровно один, и доставался он первому же
буферу окна, то есть всему рабочему столу. Остальные окна потом создавались
нормально, но Xwayland больше не пытался показать стол, и панель осталась с
фоном композитора.

**Починка** — одна строка: объявлять только `EGL_EXT_device_query`. Проверено
на чистом Xwayland (без диагностических вставок): отказов ноль, 28 прикреплений
буфера за 14 секунд, рабочий стол на панели, часы идут.

Уроки:

- **Объявить расширение — не бесплатно.** Строка расширений это не витрина, а
  переключатель чужих путей исполнения: одно лишнее имя увело Xwayland в код
  для NVIDIA.
- **EGL-ошибка живёт до тех пор, пока её кто-нибудь не заберёт.** Неудачный
  вызов в одном месте всплывает отказом совсем в другом; искать источник надо
  не там, где сработала проверка.
- **Цвет на панели — улика.** Совпадение с фоном композитора сразу сняло с
  подозрения ядро, драйвер дисплея и сам вывод кадров.
- `glamor: … depth 30 … Falling back to software` — шум: корневое окно у нас
  глубины 24. На эту строку я потратил время зря.

### 2026-09-17 18:25 — службы Android поднимаются сами, mtkpower убран

Два хвоста, всплывших при разборе чёрного экрана.

**1. Набор служб Android не переживал перезагрузку.** `hwservicemanager`,
`vndservicemanager` и `android.hardware.graphics.allocator@4.0` поднимались
руками скриптом `hal-start.sh`, службы для них не было — после каждой
перезагрузки аппаратный декодер выглядел сломанным. Теперь есть
`bf7-android-hal.service`: `Type=oneshot` + `RemainAfterExit=yes`, после
`bf7-binderfs` и `bf7-android-mounts`, до `bf7-videod`. Демоны запускаются
через `setsid`, но остаются в группе службы, поэтому `systemctl stop` их
снимает.

Заодно из скрипта убран перечень регистраций (`lshal`) при неинтерактивном
запуске: он ждёт до 25 секунд и задержал бы загрузку. Спрашивается руками или
по `BF7_HAL_VERBOSE=1`.

**2. `vendor.mediatek.hardware.mtkpower@1.0-service` исключена из набора.** Она
жгла целое ядро девять часов подряд. Поток `mtkScnHanler` крутится **в
пользовательском коде, не заходя в ядро вовсе**: `strace -f -c` за три секунды
не показал ни одного системного вызова, `/proc/TID/syscall` — `running`,
`wchan` — ноль. Это `checkFPSupdate()` → `getFPSvalue()` в
`/vendor/lib64/libpowerhal.so`, опрос частоты кадров в пустом цикле; починить
нечем — исходников нет.

Что важно: **это уже было выяснено 2026-09-16** («Служба питания не нужна»,
декодеру подставлена заглушка клиента `libmtkperf_client_vendor.so`), но строка
в `hal-start.sh` осталась. Урок: вывод, записанный в журнал, ничего не значит,
пока он не внесён в то, что запускается.

Проверено после удаления службы питания: `ffmpeg -hwaccel vaapi` через наш
драйвер — **1800 кадров, 93 fps, 3.1× от реального времени**, процессор при
этом свободен.

Попутно: средняя нагрузка на этой платформе всегда показывает ~14 и это не
признак беды — двенадцать потоков ядра MediaTek (`hang_detect`, `disp_check`,
`charger_thread`, `battery_thread` и прочие) постоянно висят в `wait_event` в
непрерываемом сне и учитываются в load average, не тратя процессор.

### 2026-09-17 18:35 — что ещё попалось по дороге (открытые хвосты)

**1. Ошибка в самом Xwayland/glamor, не у нас.** `xwl_glamor_gbm_create_pixmap_for_bo`
проверяет `eglGetError()` после `glEGLImageTargetTexture2DOES`, не сняв перед
этим накопленную ошибку. EGL-ошибка липкая и общая на поток, поэтому **любой
неудачный вызов EGL где угодно раньше** (в том числе совершенно законный отказ
на неизвестном расширении) уносит первый же буфер окна. Сегодня это стоило
рабочего стола целиком. Мы убрали свой повод, но сама ловушка на месте: любой
следующий отказ EGL сработает так же. Лечится одной строкой в нашей заплате —
снять ошибку прямо перед привязкой; не сделано.

**2. Там же — молчаливый откат.** Если создать пиксмап через gbm не вышло,
Xwayland молча берёт обычный `glamor_create_pixmap`, у которого нет ни `bo`, ни
спутника `xwl_pixmap`. Отдать композитору такой пиксмап нельзя, и наружу
выходит единственное `Error getting buffer` — без причины и без имени окна.
Настоящая причина находится только временными метками в исходнике.

**3. Экранная клавиатура не всплывает сама.** `Onboard` пишет
`Atspi typelib missing, auto-show unavailable`: не установлен
`gir1.2-atspi-2.0`. На телефоне это заметная потеря — клавиатура не
показывается при переходе в поле ввода. Пакет в репозитории есть.

**4. Настройки экрана не применяются.** На запуске сеанса
`xfsettingsd: Unknown mode '720x1612 @ 59.9' for output XWAYLAND0, aborting`,
дальше `Failed to configure CRTC 32` и `Failed to apply display settings`.
Сохранённая раскладка ссылается на режим панели, а X у нас работает в
масштабированном 1075x480.

**5. `xfce4-panel` будит службу `org.xfce.Xfconf` ровно раз в десять минут** —
в журнале ровный ряд активаций. Для телефона это лишние пробуждения.

**6. Устаревшее описание в заголовке `bf7-session`:** там написано, что клиенты
X рисуют в разделяемую память и своего ускорения GL у них нет. С появлением
glamor и нашей платформы EGL для X это уже неправда — комментарий введёт в
заблуждение того, кто будет читать скрипт следующим.

**7. Подозрение, не проверено: `glamor_finish()` на каждом отданном кадре.**
Это наша заплата от отставания на кадр; она синхронно ждёт GPU перед каждой
отдачей буфера композитору и вполне может быть причиной того, что интерфейс
«немного тормозит». Сравнить есть чем — в заплате оставлен выключатель
`XWL_NO_FINISH=1`. Правильное решение, если подтвердится, — отдавать fence
вместо ожидания.

**Проверено и оказалось не поломкой:** флаг `posting` в `bf7_drm.c` не мёртвый
код (есть вызов `bf7_post_frame` из пути выключения тракта, без `dirty_lock`);
пакет `xwayland` уже под `apt-mark hold`, так что обновление не затрёт нашу
сборку; средняя нагрузка ~14 — это потоки ядра MediaTek в непрерываемом сне;
строка glamor про `depth 30` — шум, корневое окно глубины 24.

### 2026-09-17 18:45 — защита от липкой ошибки EGL; строка расширений возвращена целиком

Утреннюю починку («объявлять только `EGL_EXT_device_query`») я принял на веру:
что Firefox'у хватает именно этого имени, было взято из памяти и не проверено.
Проверка заняла одну команду и показала обратное — в `libxul.so` есть все три:

```
1 EGL_EXT_device_base
2 EGL_EXT_device_enumeration
2 EGL_EXT_device_query
```

То есть урезанная строка тихо отбирала у Firefox путь, который утром работал.

Поэтому чинить надо было там, где настоящая поломка, — в Xwayland. В
`xwl_glamor_gbm_create_pixmap_for_bo` перед привязкой образа к текстуре
добавлен холостой `eglGetError()`: он снимает чужую накопленную ошибку, и
проверка ниже видит только свою. Строка расширений возвращена полностью.

**Порядок проверки был важен:** сперва заплата, потом возврат строки. Возврат
строки — это и есть испытание защиты: он заново создаёт ровно ту ошибку,
которая утром гасила экран. Результат: отказов ноль, рабочий стол на панели,
часы 18:40.

Заплата в репозитории обновлена: новая часть для
`hw/xwayland/xwayland-glamor-gbm.c`, проверена `patch -p1 --dry-run` на чистом
файле. Заодно из `patches/xwayland-22.1.9-bf7-powervr.patch` убраны пять строк
мусора в начале — туда когда-то попало содержимое `/etc/default/bf7-display`
(`BF7_SCALE=1.5`, `BF7_ROTATION=90` и далее).

Урок: **«я это помню» — не проверка.** Утверждение «Firefox'у нужен именно
device_query» выглядело обоснованным, стояло в комментарии к коду и было
неверным. Одна команда `strings | grep` отделила бы память от факта до того,
как правка уехала на устройство.

### 2026-09-17 18:46 — Firefox проверен: GPU наш, строка расширений цела

Запущен `firefox-esr about:support` с `HYBRIS_EGLPLATFORM=x11` и `MOZ_X11_EGL=1`.
Показания сняты **без разглядывания снимка**: нажата кнопка «Копировать текст в
буфер обмена» (`xdotool mousemove … click 1`), буфер X прочитан из консоли
(`xclip -o -selection clipboard`) — 40 КБ текста, который дальше разбирается
`grep`'ом. Приём годится для любой программы с окном: буфер читается, пока
владелец жив.

Итог:

```
Композитинг: WebRender
Видеокарта №1 — Активна: Да
  Описание: PowerVR Rogue GE8300
  Код производителя: Imagination Technologies
HW_COMPOSITING / OPENGL_COMPOSITING / WEBRENDER / X11_EGL /
DMABUF / HARDWARE_VIDEO_DECODING — все available
EGL_VENDOR: Imagination Technologies
```

И главное для сегодняшней правки — в `EGL_EXTENSIONS(nullptr)` Firefox видит
**все три** имени: `EGL_EXT_device_base EGL_EXT_device_enumeration
EGL_EXT_device_query`. То есть возврат полной строки ничего у него не отнял, а
защита в Xwayland держит ту самую липкую ошибку, которую этот набор порождает.

Убрано за собой: браузер закрыт, процессов не осталось (проверено по
`/proc/PID/exe`, а не `pgrep -f` — тот ловит собственную командную строку),
снимок панели чистый, клавиатура на месте.

### 2026-09-17 18:52 — экранная клавиатура всплывает сама

Onboard писал при каждом запуске `Atspi typelib missing, auto-show unavailable`.
Оказалось, дело не в одном пакете, а в трёх вещах сразу:

1. не установлен `gir1.2-atspi-2.0` — без него Onboard не видит, куда ушёл
   фокус ввода;
2. `org.onboard.auto-show enabled` = **false**;
3. `org.onboard start-minimized` = **false** — клавиатура показывалась сразу
   при запуске, поэтому автопоказу нечего было показывать, а автоскрытию
   нечего прятать: окно висело постоянно.

Плюс включённое `auto-show tablet-mode-detection-enabled` — на телефоне
определять нечего, выключено, чтобы не глушило показ.

Проверено полным циклом на том экземпляре, который запускает сам сеанс
(родитель — `xfce4-session`): фокус в поле поиска `xfce4-appfinder` —
клавиатура всплывает рядом с полем, окно закрыли — спряталась. Настройки
лежат в dconf и переживают перезапуск сеанса.

Вернуть постоянно видимую клавиатуру, если так окажется удобнее:
`gsettings set org.onboard start-minimized false`.

**Ошибка разведки, стоившая двух заходов.** Я проверял настройки командой
`gsettings … 2>/dev/null` и получил «схем нет вообще» — сделал вывод, что
сломан кэш схем glib. На самом деле `gsettings` просто **не был установлен**
(пакет `libglib2.0-bin`), а `2>/dev/null` съел `command not found`. Правило:
**на разведке не глушить stderr**; пустой ответ и отсутствующая программа
выглядят одинаково, но лечатся по-разному.

### 2026-09-17 19:05 — кто рисует видео в браузере: замер живого сеанса

Хозяин смотрел видео в Firefox и спросил, кто занят. Замер прироста
процессорного времени за 5 секунд, ничего не трогая:

```
 49113  44.2%  firefox-esr (родительский)   — WebRender, рисование
 48743  17.4%  Xwayland :1                  — передача кадров композитору
 49529  15.0%  firefox-esr rdd              — ДЕКОДИРОВАНИЕ, потоки MediaPDecoder
 48735  12.8%  bf7gui compositor.qml        — вывод на панель
 49293   5.2%  firefox-esr utility
  + kworker/u8 pvr_misr ~3% x2, cmdq_auto_release ~2%
```

**Рисует наш GPU.** Родительский процесс держит `/dev/dri/renderD128`,
`/dev/pvr_sync` и `/dev/ion` и отобразил `libIMGegl.so` с `libPVRMtkutils.so`
через нашу libhybris. Мешевский `swrast_dri.so` в карте памяти тоже есть, но
это след пробы GLX, а не рисование.

**Декодирует процессор.** Наш демон `bf7-videod` за всё время просмотра не
получил **ни одного байта** (журнал как был 1371008, так и остался), к его
сокету никто не подключён. Кадры готовит программный декодер в процессе rdd.

**Почему — нашлось точно.** Firefox честно пробует аппаратный путь, и libva
открывает именно наш драйвер (`pvr_drv_video.so` — символическая ссылка на
`bf7_drv_video.so`, поэтому переменная `LIBVA_DRIVER_NAME` не нужна):

```
libva info: Trying to open .../pvr_drv_video.so
libva info: va_openDriver() returns 0
[GFX1-]: VideoBridgeParent receives IPC close with reason=AbnormalShutdown
```

И сразу после этого процесс rdd падает. В профиле накопилось **12 минидампов**;
сохранены в `/root/ffcrash`. Разбор последнего:

```
причина: SIGSEGV / SEGV_MAPERR по адресу 0x0
  #0  ip=0x0                                    ← прыжок по нулевому указателю
  #1  bf7_drv_video.so +0x691c  bf7va::ensure_backing(Surface&) +0x148
  #2  bf7_drv_video.so +0x9294  bf7va::DeriveImage(...) +0x164
  #3  libva.so.2
  #4  libavutil.so.57
```

То есть **наш `ensure_backing()` зовёт нулевой указатель** — почти наверняка
gralloc, до которого libhybris не дозвалась, а мы результат не проверяем.
Таблица функций драйвера тут ни при чём: все 59 записей `VADriverVTable`
заполнены (проверено сравнением с заголовком libva 2.17 на устройстве).

Отдельно: звука в браузере нет — `OpenCubeb() failed to init cubeb`,
`OnMediaSinkAudioError`.

Ближайший шаг — не «починить декод», а **перестать падать**: проверять
результат инициализации gralloc и возвращать честный отказ. Тогда браузер
спокойно уйдёт на программный путь вместо дюжины падений, и станет видно
настоящую причину (подсказка: сегодня я сделал сообщения линковщика libhybris
молчаливыми, включаются `HYBRIS_LINKER_VERBOSE=1` — там и будет видно, что
именно не загрузилось).

### 2026-09-17 19:15 — почему аппаратный декодер не работает в браузере: процесс rdd заперт в пустом каталоге

Измерено на живом процессе (`rdd`, pid 49529), ничего не запуская:

```
/proc/49529/root -> /proc/49530/fdinfo      ← чрут в пустой каталог
ls -a этого корня                           ← пусто, код возврата 0
/run НЕТ   /run/bf7-video.sock НЕТ   /dev/ion НЕТ
/system НЕТ   /vendor НЕТ   libgralloc.so.1 НЕТ
открытые описатели: только /dev/null и сокеты
пространства имён: net и user — свои, mnt и pid общие
```

Это песочница Firefox: процесс декодирования запирается в `/proc/N/fdinfo`
(пустой каталог) и лишается файловой системы целиком. Настройка
`security.sandbox.rdd.level = 0` в профиле стоит, но чрут она не отменяет.

**Отсюда главное: в таком окружении наш драйвер не может работать в принципе.**
Ему нужны gralloc (через `/vendor/lib64/hw/`), `/dev/ion` и сокет демона
`/run/bf7-video.sock` — ничего этого изнутри не видно. Даже если убрать
падение, аппаратного декодирования не появится.

**Чего установить НЕ удалось — и почему я остановился.** Кадры минидампа
указывают на `ensure_backing` → `gralloc()`, но разбор машинного кода этого не
подтверждает: во всём загрузчике gralloc **нет ни одного косвенного вызова**
(`blr`), а значит прыгнуть по нулю там неоткуда, и раскрутка по указателям
кадров (`trust: frame_pointer`) в деталях врёт. Список модулей в дампе тоже
неполный — пять штук на процесс Firefox, потому что в чруте недоступен `/proc`
и карту памяти собрать не вышло. Строить починку на этих кадрах нельзя.

Дальше нужен опыт с трассировкой на браузере, который я запускаю сам:
`MOZ_DISABLE_RDD_SANDBOX=1` (снимает чрут с процесса декодирования),
`HYBRIS_LINKER_VERBOSE=1` (сегодня я сделал сообщения линковщика молчаливыми —
включаются этим) и `BF7VA_TRACE=1`. Это ответит сразу на оба вопроса: работает
ли аппаратный путь без песочницы и что именно не открывается.

Отдельно на будущее: правильная развязка, если песочница подтвердится, —
**переносить выделение буфера в демон** (он и так снаружи и отдаёт дескрипторы
через сокет), а в песочницу отдавать только готовый дескриптор. Но и тогда
сокет надо открывать **до** чрута и передавать процессу готовым описателем.

### 2026-09-17 19:20 — поправка к замеру: видео было на паузе

Выше в записи 19:05 сказано, что при воспроизведении в браузере нагрузка
низкая. **Это неверно.** Хозяин указал: видео стартует само, а мой клик по
кадру, которым я «запускал» воспроизведение, наоборот ставил его на паузу. То
есть цифры 16,6 % / 3 % снимались с остановленного видео.

Честный замер без клика: родительский процесс Firefox **99,2 %**, Xwayland
15,4 %, вкладка 11,4 %, композитор 11,4 %. Программное декодирование этого
крошечного ролика 640×480 занимает почти целое ядро — при том, что аппаратный
декодер рядом простаивает.

Урок: **не трогать то, что измеряешь**. Клик по окну — это ввод, а не
наблюдение; проверять состояние надо сравнением кадров или счётчиками, а
управлять воспроизведением — только если точно знаешь текущее состояние.

## 2026-09-17 19:30 — Почему Firefox не возьмёт наш кадр: проба дескрипторов

Прямая проба (`yv12test.c`, повторяет то, что делает Mozilla) на живом
PowerVR — что драйвер принимает дескриптором dma-buf, а что нет:

| дескриптор | образ | привязка |
| --- | --- | --- |
| YV12 одним слоем, 3 плоскости | создан | принята |
| I420 одним слоем, 3 плоскости | создан | принята |
| NV12 одним слоем, 2 плоскости | создан | принята |
| ARGB (для сравнения) | создан | принята |
| YV12, описана только плоскость 0 | **НЕТ, 0x300c** | — |
| NV12, описана только плоскость 0 | **НЕТ, 0x300c** | — |
| R8 отдельной плоскостью (яркость) | **НЕТ, 0x3009** | — |

Две нижние строки и есть ответ. `DMABufSurfaceYUV::CreateTexture()` в Firefox
создаёт **по образу EGL на каждую плоскость буфера** и передаёт при этом
только атрибуты `EGL_DMA_BUF_PLANE0_*`, а формат берёт свой для каждой
плоскости (`mDrmFormats[aPlane]`). Отсюда:

- если отдать кадр **целым слоем** (наш `BF7VA_COMPOSED`), Mozilla опишет ему
  одну плоскость из трёх нужных → `EGL_BAD_PARAMETER` (0x300c). Ровно эта
  ошибка и стоит в её журнале;
- если отдать **по слою на плоскость**, как это делают Intel и AMD, слои
  выходят однокомпонентные (R8 для яркости, GR88 для цветности), а их PowerVR
  не импортирует вовсе → `EGL_BAD_MATCH` (0x3009).

Раскладки, которая устраивала бы обе стороны, не существует: Mozilla умеет
только поплоскостный импорт, PowerVR — только целые кадры. Это **граница
железа и чужого кода, а не наша недоделка**.

Обходной путь через RGBA тоже закрыт: `strings libxul.so` показывает в
импорте кадров VA-API единственное сообщение `Unsupported VA-API surface
format %d` и ни одного упоминания ветки RGB — класс `DMABufSurfaceRGBA`
существует, но кадры VA-API через него не ходят.

Что из этого следует для дела:

1. Экспорт остаётся **YV12** (раньше стоял I420 — Mozilla честно сообщала
   `Unsupported VA-API surface format 808596553`, и это была наша ошибка
   разметки, её стоило исправить в любом случае).
2. `MOZ_DISABLE_RDD_SANDBOX=1` был опытом, а не решением: песочницу вернуть.
3. Открытым остаётся другое — падения процесса RDD с `ip=0x0`. Это отдельная
   поломка: она объясняется не поплоскостным импортом, а чем-то в нашем стеке,
   и чинить её надо независимо от Firefox.

## 2026-09-17 19:45 — Драйвер VA-API ронял чужой процесс: найдено и исправлено

Отдельная от Firefox поломка, и виноваты в ней мы. Браузер, запущенный как
есть — без единой переменной окружения, — терял процесс декодирования: в
каталоге посмертных журналов прибавлялся ещё один (`ProcessType: rdd`,
`SIGSEGV / SEGV_MAPERR`, адрес `0x0`), в журнале браузера стояло
`VideoBridgeParent receives IPC close with reason=AbnormalShutdown`.

**Воспроизведено без браузера.** Firefox запирает RDD в пустой каталог;
то же самое делается пространством монтирования:

    unshare -m sh -c 'mount -t tmpfs none /system; mount -t tmpfs none /vendor;
                      LIBVA_DRIVER_NAME=pvr ./sandtest'

`sandtest` — двадцать строк на libva: открыть узел, создать поверхность,
вывезти её дескриптором. Падает там же. Стек под отладчиком:

    #0  0x0000000000000000 in ?? ()
    #1  hybris_gralloc_initialize (framebuffer=0) at gralloc.c:118
    #2  bf7va::ensure_backing(bf7va::Surface&)
    #3  bf7va::ExportSurfaceHandle(...)
    #4  vaExportSurfaceHandle () from libva.so.2

**Причина — в том, как libhybris связывает символы Android.** Макрос
`HYBRIS_DLSYSM` берёт адрес через `android_dlsym` и тут же зовёт по нему:

    #define HYBRIS_DLSYSM(name, fptr, sym) \
        if (!name##_handle) hybris_##name##_initialize(); \
        if (*(fptr) == NULL) { *(fptr) = (void *) android_dlsym(...); }
    ...
        static return_type (*f)(a1, a2) = NULL;
        HYBRIS_DLSYSM(name, &f, #symbol);
        return f(n1, n2);          /* проверки нет */

Неудачу ловит `assert`, а собрано с `NDEBUG` — то есть не ловит вовсе.
`gralloc.c:118` — это вызов `hw_get_module` из `libhardware.so`. Если
андроидных библиотек не видно, указатель остаётся нулевым и управление
уходит по адресу 0.

**Заплата — в нашем драйвере**, в единственном месте, откуда он вообще
трогает андроидную часть (`gralloc()` в `decode.cpp`): перед обращением
проверяем, на месте ли `/system/lib64/libhardware.so` и `/vendor/lib64/hw`.
Нет — отказываем, а `ExportSurfaceHandle` возвращает
`VA_STATUS_ERROR_ALLOCATION_FAILED` (раньше там стоял `SURFACE_BUSY`, что
получателя только путало: «занято» он читает как «попробуй ещё раз»).

Проверено, все четыре опыта:

| Опыт | До | После |
| --- | --- | --- |
| проба, андроидная часть на месте | вывоз удался | вывоз удался |
| проба, андроидная часть спрятана | SIGSEGV, код 139 | отказ «не вышло выделить», процесс жив |
| Firefox как есть | +1 посмертный журнал, `AbnormalShutdown` | 13 → 13, жалоб нет, рабочий стол цел |
| GStreamer, показ без копий | работает | работает (проверено снимком) |

Правило, которое из этого следует: **библиотека, лежащая в общем каталоге
драйверов libva, не имеет права ронять процесс, который её открыл.** Не
можем — обязаны вернуть ошибку. Вызывающая сторона (тот же Firefox) к
отказу готова и спокойно уходит на процессор; к падению — нет.

## 2026-09-17 19:48 — Хвосты к разбору: коды ошибок и раскладка экспорта

Два добавления к записям 19:30 и 19:45.

**Тот же неверный код был и во втором месте.** В пути образа (`DeriveImage`
/ `GetImage`) неудача выделения буфера возвращала
`VA_STATUS_ERROR_INVALID_SURFACE` — то есть «такой поверхности нет», хотя
поверхность цела и не вышло именно выделить память. Получатель по такому
коду ищет поломку у себя. Тоже заменено на `ALLOCATION_FAILED`. Проверено
пробой и показом на экране с окончательной сборкой.

**Решение по раскладке экспорта: флаг соблюдаем.** `SEPARATE_LAYERS` даёт
три слоя (R8), `COMPOSED_LAYERS` — один слой YV12 с тремя плоскостями, как
и просят. Соблазн всегда отдавать составной слой отвергнут: на этом железе
ни один получатель от этого не выигрывает (см. запись 19:30 — оба
проигрывателя всё равно разбирают кадр по плоскостям), а поведение по
спецификации ценнее. Переменная `BF7VA_COMPOSED` остаётся рычагом для
опытов.

Пробы, которыми всё это выяснялось, положены в репозиторий:
`vaapi-shim/probes/` (там же описано, как их запускать).

## 2026-09-17 19:51 — Сколько на самом деле экономит аппаратный декодер

Замер по вопросу хозяина: «декодер-то завёлся?». Мерялось на mpv,
`--vo=null --no-audio`, чтобы считать только декодирование.

**Ролик 640×480 (тот, на котором всё проверялось):**

| Путь | Стена | ЦП у mpv | ЦП у демона |
| --- | --- | --- | --- |
| программно | 2,5 с | 6,6 с | — |
| аппаратно (`vaapi-copy`) | 25,5 с | 7,8 с | 12,7 с |

**То есть на мелком ролике аппаратный путь проигрывает** — и по времени, и
по процессору. Четыре ядра A53 разбирают 640×480 быстрее, чем наш конвейер
успевает передавать кадры туда-обратно.

**Ролик 1920×1080, 450 кадров** (`testsrc2`, профиль high, 6 Мбит/с):

| Путь | Стена | ЦП у mpv | ЦП у демона | Всего ЦП |
| --- | --- | --- | --- | --- |
| программно | 5,3 с | 18,2 с | — | **18,2 с** |
| аппаратно (`vaapi-copy`) | 12,1 с | 5,8 с | 4,4 с | **10,2 с** |

При просмотре в реальном времени (30 кадров/с): программно **139 %**
процессора, аппаратно **42,5 %**.

Выводы, которые стоит держать в голове:

1. **Декодер работает и его действительно используют** — mpv сам сообщает
   `Using hardware decoding (vaapi-copy)`, работу выполняет демон.
2. **Выигрыш появляется с разрешением.** На 1080p процессора вдвое меньше,
   у самого проигрывателя — втрое. На 640×480 выигрыша нет вовсе.
3. **Узкое место — не декодер, а согласование.** Пропускная способность
   нашего пути на 1080p ≈ 37 кадров/с (реального времени хватает, запаса
   мало), тогда как демон сам по себе выдаёт 93 кадра/с на 640×480. Это
   та же задача на скорость, что записана в цели: клиент просит кадр сразу
   после подачи картинки, а конвейеру MediaTek нужно несколько вперёд.

Образец 1080p оставлен на телефоне: `/root/hd.mp4`.

## 2026-09-17 19:55 — Можно ли декодировать на GPU через Vulkan: нет, и почему

Вопрос хозяина. Проверено не строками в файле, а самим API — проба
`hybris/bf7-gpu/vkvideo.c`, запуск через **наш** загрузчик:

    LD_LIBRARY_PATH=/usr/local/lib LD_PRELOAD=/usr/local/lib/libbf7tlsguard.so ./vkvideo

(через загрузчик Debian 1.3.239 драйвер отказывает с
`VK_ERROR_INCOMPATIBLE_DRIVER`; это давно известно, см. `vkver.c`.)

Ответ драйвера:

    загрузчик: Vulkan 1.1.0
    [0] PowerVR Rogue GE8300, драйвер объявляет Vulkan 1.1.170
      семейств очередей: 1
        [0] очередей 2, флаги 0x07: графика вычисления передача
      расширений устройства: 62
      расширений со словом video: 0

Три независимых причины, любой из которых достаточно:

1. **Расширений нет.** `VK_KHR_video_queue` и `VK_KHR_video_decode_h264`
   вошли в Vulkan 1.3 (конец 2022 года), а блоб объявляет 1.1.170 — он
   старше самих расширений. Новый блоб взять неоткуда.
2. **Очереди нет.** Семейство очередей одно, флаги 0x07; бита
   `VK_QUEUE_VIDEO_DECODE_BIT_KHR` (0x20) нет. Vulkan Video работает именно
   через отдельную очередь.
3. **И главное — это другое железо.** Декодер на MT6761 — блок VDEC от
   MediaTek, им управляет `/vendor/lib/libvcodecdrv.so` (тот самый, ради
   которого наш помощник 32-битный). GPU от Imagination к нему отношения не
   имеет, и драйвер Imagination его никогда не покажет. Даже свежий блоб
   Vulkan декодер бы не открыл.

Отдельно про «посчитать декодирование шейдерами»: это не вариант. CABAC —
последовательный по своей природе, на GPU ложится плохо; практических
декодеров H.264 на вычислительных шейдерах нет ни у кого. На GE8300 это
вышло бы медленнее четырёх ядер A53 и горячее.

**Что на GPU и так уже делается:** преобразование цвета, масштабирование,
композиция и показ без копий. На пути GStreamer процессор кадра не касается
вовсе — то есть «гнать на видеокарте» в той части, в какой это возможно,
уже сделано.

## 2026-09-17 20:04 — Перепись железа: что на телефоне есть и что из этого живо

По просьбе хозяина прошёлся по всему железу. Только чтение: дерево
устройств, узлы `/dev`, классы `/sys`, список модулей, вендорские HAL.
Итог — отдельным файлом, `docs/bf7-native/hardware-inventory.md`.

Коротко, что выяснилось нового:

- **Звук и микрофон ближе всего к готовности.** Карта живая, 32 устройства
  PCM, 119 элементов микшера, в дереве объявлен внешний усилитель динамика.
  Упирается в уже разобранную панику на mmap (`dummy_page` не
  инициализирован в 29 драйверах PCM); правка известна и ждёт сборки ядра.
  Не проверено: `aplay`/`arecord` пишут без отображения и по идее должны
  работать уже сейчас — но если предположение неверно, телефон уходит в
  перезагрузку.
- **Bluetooth сделать реально.** Стек в ядре есть (`CONFIG_BT=y`,
  BREDR+LE), общий слой связности `wmt_drv` уже загружен — тот же, через
  который работает Wi-Fi. Нет модуля `bt_drv` (а значит и `/dev/stpbt`) и
  не установлен BlueZ. Исходники BT лежат в донорском дереве
  `kernel_ubports_mt6781/drivers/misc/mediatek/connectivity/bt/mt66xx/`.
  Порядок действий тот же, что с Wi-Fi.
- **Камер две**, объявлены в дереве (`camera_main@1a` с автофокусом и
  EEPROM, `camera_sub@2d`, вспышка `strobe_main@63`). Узлы ядра есть, но
  V4L2 нет вовсе и сенсор не поднят: у MediaTek ядро пассивно, сенсор
  включает пользовательская часть. Путь тот же, что у видеодекодера —
  вендорский HAL через binder.
- **Датчики висят на сопроцессоре SCP.** Драйвер сопроцессора есть,
  интерфейса к датчикам нет. Через iio доступны только АЦП батареи и
  температур.
- **Модем жив на уровне канала** — 48 узлов `ccci_*`, 21 интерфейс
  `ccmni*`, но наверх это отдаёт RILD, которого у нас нет.
- **Отпечаток объявлен** — `/spi0/fingerprint@0`, `goodix`. Драйвера нет.
- **Вибро, подсветка и светодиод доступны прямо сейчас** обычной записью в
  `/sys/class/leds/*/brightness`.

## 2026-09-17 20:09 — Проверка звука и мелочей: микрофон работает, динамик молчит

С разрешения хозяина. Паники не случилось ни разу, телефон не
перезагружался.

**Вибро, светодиод, подсветка — управляются напрямую.** У узла вибро
«спусковой» интерфейс: писать надо не яркость, а `duration` и `activate`:

    echo 250 > /sys/class/leds/vibrator/duration
    echo 1   > /sys/class/leds/vibrator/activate

Светодиод `blue` и `lcd-backlight` берут обычную запись в `brightness`
(проверено: 255 и 40, потом вернул исходные 128).

**Микрофон работает — это доказано измерением.** Запись
`arecord -D hw:0,1 -f S16_LE -r 48000 -c 2 -d 4`, затем уровень:

    mean_volume: -65,3 dB     max_volume: -28,3 dB

Цифровая тишина дала бы около −91 дБ, так что это настоящий шум комнаты.
Перед записью включались `Audio_ADC_1_Switch`, `Audio_Preamp1_Switch`,
`Audio_MIC1_Mode_Select`.

**Паники на mmap можно не бояться при обычном воспроизведении.**
`aplay --dump-hw-params -D hw:0,0` показывает `ACCESS: RW_INTERLEAVED`, и
проигрывание через `aplay` отработало штатно. Ронять телефон умеют только
те, кто отображает буфер (cubeb в Firefox) — ровно как и было разобрано.

**Но звука из динамика нет.** Проверено объективно, без ушей: играл тон
1000 Гц и одновременно писал микрофоном, затем считал Гёрцелем энергию на
1000 Гц. До тона 0,12, во время тона 0,04 — то есть ничего.

Что перепробовано:

- включены `Audio_Amp_L_Switch`, `Audio_Amp_R_Switch`,
  `Speaker_Amp_Switch`, `Ext_Speaker_Amp_Switch` — приняты, но без
  результата;
- усиления не в нуле: `Lineout_PGAL/R_GAIN` стоят на `8Db`;
- включён буфер тактов `AUD_CLK_BUF_Switch` (он, кстати, счётчик, а не
  выключатель: после записи «1» читается 2, потом 3);
- подмешан **внутренний генератор кодека** в `O3O4` (тракт DL1 → ЦАП) при
  открытом потоке — тоже тишина.

Последнее и есть самое говорящее: если молчит даже генератор внутри
кодека, дело **не в цифровом маршруте**, а в аналоговой части — питании
ЦАП, смещениях, включении усилителя класса D. У MediaTek это программирует
звуковой HAL прямыми записями в регистры PMIC, а не через ALSA.

Полезное попутно: драйвер внешнего усилителя **привязан** (i2c `3-0034`,
`speaker_amp`), а `MTK_SPK_TYPE_GET` = `MTK_SPK_NOT_SMARTPA` — то есть
усилитель простой, управляется ножкой, а не по i2c.

**Вывод: путь к звуку тот же, что был у видео** — поднять андроидный
`audio.primary.mt6761.so` через binder (у нас уже есть и binderfs, и
hwservicemanager, и опыт с Codec2), либо найти последовательность
включения аналоговой части в исходниках кодека и повторить её.

Ручки микшера возвращены в исходное, временные файлы убраны.

## 2026-09-17 20:13 — Светодиод: драйвер до железа доходит; и находка про MT6370

Хозяин подтвердил вибро и яркость экрана, а про светодиод сказал «хз» — не
разглядел. Проверил программно.

**Драйвер настоящий.** `blue` висит на `leds-mt6357`, то есть на канале
ISINK самого PMIC (`platform/1000d000.pwrap/...:main_pmic/leds-mt6357`), и в
дереве устройств у него объявлен ровно один канал — `Isink1` с меткой
`blue`.

Чтобы отделить запись в железо от пустышки, снял три среза регистров PMIC
через `debugfs/regmap`: два подряд с выключенным светодиодом (это шум) и
один с включённым. Единственный регистр, который **переключается и
возвращается**:

    регистр 5140: выкл 8000 -> вкл 0000 -> снова выкл 8000

Остальные отличия — счётчики: меняются один раз и назад не идут. Значит
драйвер действительно пишет в чип. Есть ли на этой модели сам светодиод
физически — программно не выяснить.

**Находка попутно: чип MT6370 остался без драйвера.** В дереве устройств у
него объявлены `mt6370_pmu_fled1`, `mt6370_pmu_fled2` (вспышка камеры),
`rgbled` (полноцветный индикатор), `bled` (подсветка) и `charger`. Ни одного
модуля `mt6370` не загружено, светодиодов-вспышек в системе ноль, в
`/sys/class/leds` только `blue`, `lcd-backlight` и `vibrator`.

То есть **фонарика у нас сейчас нет**, хотя железо для него на плате есть.
Это дешёвая и заметная победа на будущее: привязать драйвер mt6370 —
получить вспышку, а возможно и настоящий RGB-индикатор (тогда станет ясно,
что за светодиод хозяин не разглядел).

## 2026-09-17 20:20 — Первый подход к камере: паника ядра, и она наша

Хозяин попросил камеру. Разведка дала неожиданно хорошую отправную точку, а
первая же проба уронила телефон. Разбор — ниже; телефон поднялся сам.

**Что выяснилось до пробы (всё только чтением):**

- Драйверы камеры **в ядре есть и собраны**: 99 символов `imgsensor`, 430
  `isp_`, в дереве исходников `imgsensor` (215 файлов .c), `cameraisp` (55),
  `cam_cal` (37).
- Ядро собрано **под наш телефон**, и камеры известны поимённо:

      CONFIG_ARCH_MTK_PROJECT="bf7_h6127"
      CONFIG_CUSTOM_KERNEL_IMGSENSOR="s5k3l6_mipi_raw hi556_mipi_raw"

  То есть **S5K3L6** (основная, Samsung, 13 Мп, опознание `0x30C6`) и
  **HI556** (фронтальная, 5 Мп, опознание `0x0556`). В ядре ровно два
  `SensorInit`, они и есть.
- В `/proc/driver/` есть `camera_info`, `camsensor`…`camsensor4` — пустые:
  сенсор никто никогда не поднимал.

**Ловушка в самом интерфейсе.** Команды `KDIMGSENSORIOC_T_OPEN`,
`T_CLOSE`, `T_CHECK_IS_ALIVE` объявлены в заголовке как `_IO` (без
аргумента), но обработчик начинается с

    if (_IOC_DIR(a_u4Command) != _IOC_NONE) { ... }
    else { i4RetValue = -EFAULT; goto ...; }

то есть **любую команду без аргумента отвергает всегда**. Работать можно
только через `KDIMGSENSORIOC_X_FEATURECONCTROL`. Проверено: первая проба
получила `Bad address` на всех трёх.

**Паника.** Вторая проба звала `SENSOR_FEATURE_CHECK_IS_ALIVE` (3108)
напрямую. Телефон ушёл в перезагрузку. Посмертный журнал
(`/var/lib/systemd/pstore/console-ramoops-0`):

    __pi_strlen+0x10/0x84
    imgsensor_hw_power+0x78/0x120
    imgsensor_ioctl+0xde4/0x2a7c
    ...
    x0 : 0000000000000000        lr : strstr+0x20/0x78

Причина в `imgsensor_hw.c`:

    if (phw->enable_sensor_by_index[sensor_idx] &&
        !strstr(phw->enable_sensor_by_index[sensor_idx], curr_sensor_name))

`curr_sensor_name` берётся из имени выбранного драйвера сенсора, а драйвер
ещё не выбран — там NULL. `strstr` зовёт `strlen(NULL)` → обращение по
нулевому адресу в ядре → паника.

**Два вывода, оба полезные:**

1. **Моя ошибка в порядке вызовов.** Сперва надо
   `SENSOR_FEATURE_SET_DRIVER` (3107) — он выставляет `sensor_idx` и зовёт
   `imgsensor_set_driver()`, который подбирает драйвер и заполняет имя, — и
   только потом `SENSOR_FEATURE_CHECK_IS_ALIVE` (3108). Андроидный HAL так и
   делает, поэтому у него не падает.
2. **Ошибка в ядре, и её стоит починить.** `imgsensor_hw_power` не проверяет
   `curr_sensor_name` на NULL. Любая программа одной командой ioctl роняет
   ядро — вендорский код уверен, что пользовательская часть всегда всё
   делает правильно. Это третья находка того же сорта после `dummy_page` в
   звуке и вызова по нулевому адресу в libhybris.

**Что из этого следует по порядку работ.** Правку в
`imgsensor_hw_power` (проверка на NULL) стоит внести **до** дальнейших
опытов с камерой — вместе с уже известной правкой звука (убрать флаги MMAP).
Тогда ошибка в пробе будет стоить сообщения об ошибке, а не перезагрузки. То
есть накопилось на одну сборку ядра.

**Попутно: перезагрузка проверила службы.** После неё сами поднялись
`bf7-binderfs`, `bf7-android-mounts`, `bf7-android-hal`, `bf7-videod`,
`bf7-session`, `bf7-fuelgauge` — все `active`. Это первая настоящая проверка
`bf7-android-hal.service` перезагрузкой, она прошла.

## 2026-09-17 20:24 — Камера: сенсор не отвечает, потому что ему не дают питание

Повтор пробы с правильным порядком вызовов (`SENSOR_FEATURE_SET_DRIVER`
3107, затем `CHECK_IS_ALIVE` 3108). **Паники нет**, телефон жив, проба
дожила до конца. Драйвер вернул −5 для обеих камер, и журнал ядра объяснил,
почему.

**Что произошло на самом деле.** Драйвер отработал по-настоящему: включил
такты, прошёл последовательность питания и пошёл по I2C:

    i2c i2c-4: addr:0x28,ACK error
    i2c i2c-4: addr:0x20,ACK error
    [imgsensor][imgsensor_check_is_alive] Fail to get sensor ID ffffffff
    [imgsensor][imgsensor_check_is_alive] ERROR: No imgsensor alive

и перед этим:

    [imgsensor][regulator_set] regulator == NULL 0 3 3

Расшифровка последнего: третий аргумент — `IMGSENSOR_HW_PIN_AVDD` (3),
второй — тот же вывод, первый — `reg_type_offset` = 0 = `REGULATOR_TYPE_VCAMA`.
То есть **не подалось аналоговое питание 2,8 В**, и по I2C, естественно,
никто не ответил.

**Почему его не подали.** В дереве устройств узел
`kd_camera_hw1@15040000` (`mediatek,camera_hw`) описывает плату так:

- питание от PMIC: `cam0_vcamd-supply`, `cam0_vcamio-supply`,
  `cam1_vcamd-supply`, `cam1_vcamio-supply`;
- аналоговое питание — **ножкой**: в `pinctrl-names` есть `cam0_vcama_on`,
  `cam0_vcama_off`, `cam1_vcama_on`, `cam1_vcama_off`.

А таблица в ядре (`src/mt6761/camera_hw/imgsensor_cfg_table.c`) раздаёт
выводы иначе:

    IMGSENSOR_SENSOR_IDX_MAIN:
        {IMGSENSOR_HW_ID_REGULATOR, IMGSENSOR_HW_PIN_AVDD},   <- должен быть GPIO
        {IMGSENSOR_HW_ID_REGULATOR, IMGSENSOR_HW_PIN_DOVDD},  <- верно
        {IMGSENSOR_HW_ID_GPIO,      IMGSENSOR_HW_PIN_DVDD},   <- должен быть REGULATOR
    IMGSENSOR_SENSOR_IDX_SUB:
        {IMGSENSOR_HW_ID_GPIO,      IMGSENSOR_HW_PIN_AVDD},   <- верно
        {IMGSENSOR_HW_ID_REGULATOR, IMGSENSOR_HW_PIN_DOVDD},  <- верно
        {IMGSENSOR_HW_ID_GPIO,      IMGSENSOR_HW_PIN_DVDD},   <- должен быть REGULATOR

Это таблица общей площадки mt6761, а не нашей платы: у нас **AVDD и DVDD
поменяны местами** для основной камеры, и DVDD не туда для фронтальной.
Отсюда `regulator == NULL` на AVDD — регулятора с именем `cam0_vcama` в
дереве нет и быть не должно, там ножка.

**Заплата, которую надо внести:** в `imgsensor_cfg_table.c` для нашей
площадки — MAIN: AVDD → `GPIO`, DVDD → `REGULATOR`; SUB: DVDD →
`REGULATOR`. Три строки.

**Какие модули стоят — пока неизвестно.** Дерево разрешает по два варианта
на гнездо:

    гнездо 0 (основная):    hi1336_mipi_raw  s5k3l6_mipi_raw
    гнездо 1 (фронтальная): hi556_mipi_raw   gc05a2_mipi_raw
    гнездо 2:               fake_serial_yuv

В ядро собран по одному из каждой пары (`s5k3l6`, `hi556`), исходников
`hi1336` и `gc05a2` нет ни у нас, ни в донорских деревьях. Но пока питания
нет, опознать модуль всё равно нельзя: адреса 0x28 и 0x20 молчали именно
из-за этого. Сперва питание, потом разговор о том, тот ли драйвер.

**Итог по накопившимся правкам ядра — их теперь три:**

1. звук: убрать флаги MMAP и вернуть NULL из `mtk_pcm_page`;
2. камера: проверка `curr_sensor_name` на NULL в `imgsensor_hw_power`
   (иначе ошибка в пробе = перезагрузка);
3. камера: раздача выводов питания в `imgsensor_cfg_table.c` по нашей плате.

## 2026-09-17 20:35 — Ядро собрано и прошито: питание камеры починено, сенсоры чужие

Хозяин дал «го» на правки, сборку и прошивку сразу. Сделано всё, телефон
загрузился, ничего не сломалось.

**Что вошло в сборку.**

1. *Звук* — оказалось, правка уже лежала в дереве с прошлого разбора
   (флаги `SNDRV_PCM_INFO_MMAP` сняты во всех 29 драйверах PCM, `.page`
   возвращает NULL при пустой странице), но её **ни разу не собирали**.
   Теперь она в прошивке.
2. *Камера, защита* — `imgsensor_hw_power` проверяет `curr_sensor_name` на
   NULL. Больше неверный порядок вызовов из пользовательской части не роняет
   ядро.
3. *Камера, дело* — раздача выводов питания в `imgsensor_cfg_table.c`
   приведена к нашей плате: основная камера AVDD → GPIO, DVDD → регулятор;
   фронтальная DVDD → регулятор.

Заплата по камере выделена в `patches/x657b-4.19-bf7-imgsensor-power.patch`
(проверена `patch -p1 --dry-run` на чистой копии).

**Порядок сборки, по правилу:** ядро собрано в
`build/bf7-x657b-clang11-v2` с настройкой `configs/bf7-x657b-v2.config`
(сверено: 0 отличий от работающего ядра), тем же заходом пересобраны модули
связности и положены на телефон, `depmod`, затем упаковка образа с **этим**
`Image`, `fastboot flash boot_a`, `fastboot set_active a`, перезагрузка.
После загрузки: слот `a`, все три модуля (`wmt_drv`, `wmt_chrdev_wifi`,
`wlan_drv_gen4m`) загружены, Wi-Fi с адресом, все наши службы `active`.

Две ловушки по дороге:

- `bf7-cycle.sh` жёстко указывает на сборку **v1**; запускать его вслепую
  нельзя — упаковал бы не то ядро. Собирал по шагам.
- каталог с запасными модулями я назвал кириллицей, и `depmod` упал:
  `FATAL: Module index: bad character` — он принимает только семибитные
  имена. Каталог унесён из `/lib/modules` в `/root`, `depmod` повторён.

**Результат для камеры: питание починено.** Сообщение
`regulator == NULL` пропало, при подаче питания регулятор `vcamio`
включается (поймано опросом), ошибок pinctrl для `cam0_vcama`/`cam1_vcama`
при загрузке нет — то есть аналоговое питание ножкой найдено. В журнале
загрузки ругань осталась только на то, чего у нас и не должно быть:
`cam0_vcamd_on`, `cam0_vcamio_on` (это регуляторы, не ножки) и на гнёзда
2–5, которых на плате нет.

**Но сенсоры всё равно молчат — и теперь понятно почему.** Карта шин:

| Гнездо | Шина | Адрес по дереву | Драйвер привязан |
| --- | --- | --- | --- |
| основная | i2c-2 | 0x1A | `kd_camera_hw` |
| основная, автофокус | i2c-2 | 0x0C | `MAINAF` |
| основная, EEPROM | i2c-2 | 0x50 | `CAM_CAL_DRV` |
| фронтальная | i2c-4 | 0x2D | `kd_camera_hw_bus2` |
| фронтальная, EEPROM | i2c-4 | 0x51 | `CAM_CAL_DEV2` |

А наши собранные драйверы стучатся совсем не туда: на i2c-2 в `0x10`
(это S5K3L6), на i2c-4 в `0x20` и `0x28` (это HI556). Ни один из этих
адресов на плате не объявлен.

Складывается с тем, что уже было известно: дерево разрешает по два модуля на
гнездо — `hi1336_mipi_raw`/`s5k3l6_mipi_raw` для основной и
`hi556_mipi_raw`/`gc05a2_mipi_raw` для фронтальной, а собран по одному из
каждой пары. **Похоже, в телефоне стоят вторые варианты: HI1336 и GC05A2.**
Исходников для них нет ни у нас, ни в донорских деревьях.

**Что дальше, по порядку:**

1. Убедиться сканированием шины, а не рассуждением: включить
   `CONFIG_I2C_CHARDEV` (сейчас снят) — появятся `/dev/i2c-2` и `/dev/i2c-4`,
   и можно будет посмотреть, кто отзывается, **пока сенсор под питанием**.
   Питание держать: `SENSOR_FEATURE_OPEN` (3105) вместо `CHECK_IS_ALIVE`.
2. По найденному адресу опознать модуль и добыть его драйвер (в наших
   деревьях его нет — искать снаружи).
3. Только потом — разговор о конвейере ISP и картинке.

## 2026-09-17 20:38 — Звук после прошивки: микрофон да, динамик нет

Проверка на новом ядре, тем же способом (играю 1000 Гц, слушаю микрофоном,
считаю энергию Гёрцелем):

    до тона        на 1000 Гц: 0,05   на 1700 Гц: 0,05
    во время тона  на 1000 Гц: 0,10   на 1700 Гц: 0,03

Это уровень шума, а не сигнал. **Динамик по-прежнему молчит.** Микрофон
работает: ср.кв 286 при максимуме 13508.

Так и должно было быть: правка mmap — про то, чтобы **не ронять телефон**,
а не про то, чтобы появился звук. Причина молчания другая и уже разобрана
(запись 21:10 за прошлый заход): не включена аналоговая часть кодека, её
программирует андроидный звуковой HAL прямыми записями в PMIC, минуя ALSA.
Подтверждение прежнее: молчит даже внутренний генератор кодека.

Попутно проверено: среди собранных модулей звуковых только
`mt6357-accdet`/`mt6359-accdet` — это определение гарнитуры в разъёме, к
тракту динамика отношения не имеет. Загрузка их звука не даст (но может
пригодиться потом для наушников).

Что даёт прошивка для звука на деле: **браузер больше не уронит телефон**.
Устройство объявляет только `RW_INTERLEAVED`, отображать буфер нечем.
Звука в браузере от этого не появится — выхода всё ещё нет.

Путь к динамику остаётся один: поднять `audio.primary.mt6761.so` через
binder, как это сделано с декодером видео.

## 2026-09-17 20:47 — Камера: шину видно, EEPROM отвечает, сенсор молчит

Второй заход сборки: включён `CONFIG_I2C_CHARDEV` (ровно одно отличие в
настройке), пересобраны модули связности, образ упакован, прошито, слот A
закреплён. Телефон загрузился, узлы `/dev/i2c-0…6` появились.

**Сканирование шин.** Без питания — тишина на обеих. Под питанием (рядом в
цикле крутится `camalive2`, каждый заход подаёт питание на ~25 мс):

| Шина | Кто отозвался |
| --- | --- |
| i2c-2 (основная камера) | **0x50** в 56 проходах из 60, **0x58** в 51 из 60 |
| i2c-4 (фронтальная) | никто |

0x50 — это EEPROM модуля основной камеры, он и в дереве объявлен
(`camera_main_eeprom@50`). Прочитан, данные настоящие: заголовок с датой,
текстовая метка `SPRD`, дальше таблицы калибровки. Дамп сохранён в
`docs/bf7-native/evidence/camera-main-eeprom-dump.txt`. Имени сенсора в
открытом виде там нет — формат вендорский.

0x58 отзывается, но на чтение отдаёт одни `ff`.

**Главное: сам сенсор не отзывается ни по одному адресу** из 0x08…0x77 —
ни по 0x10, куда стучится собранный S5K3L6, ни по 0x1A, который объявлен в
дереве.

**Что это сужает.** Раз EEPROM модуля отвечает, питание 1,8 В до модуля
доходит — то есть наша правка раздачи выводов работает, и дело не в ней.
Значит сенсору не хватает чего-то ещё: тактов MCLK на ножке, снятия
сброса, или же аналогового питания (EEPROM обычно живёт от 1,8 В и без
AVDD).

**Что видно по тактам.** Во время подачи питания включаются `cam_seninf`,
`cam_larb2`, `mm_smi_cam_ck` и `camtg_sel` (24 МГц, счётчик доходит до 5).
Гейт `camtg` при этом остаётся нулевым. Ошибок pinctrl для `cam0_mclk_on`,
`cam0_rst0/rst1`, `cam0_pnd0/pnd1` при загрузке нет — состояния найдены.

**Фронтальная камера не запитана вовсе** — на её шине молчит даже EEPROM
(0x51), хотя для гнезда 1 регуляторы `cam1_vcamd`/`cam1_vcamio` драйвер
получил. Это отдельная ниточка.

**Следующий шаг (на следующий заход):** окно в 25 мс слишком коротко и
мешает измерять. Нужен переключатель в ядре, который подаёт питание на
гнездо и **оставляет** его — тогда можно спокойно и просканировать шину, и
померить MCLK на ножке, и проверить состояние сброса. Это небольшая правка
рядом с `imgsensor_hw_power` плюс запись в `/proc/driver/`.

Пробы и порядок их запуска сложены в `kernel-work/probes/`.

## 2026-09-17 20:57 — Камера: переднее гнездо не запитано по нашей вине, и это в таблице

Совет наблюдателя был «перед постройкой переключателя опознай сенсоры и
перечитай `imgsensor_proc.c`». Оба совета окупились, но главное нашлось
попутно.

**Опознание: завод знал про все четыре.** В заводской настройке ядра
(`configs/bf7-oem-4.19.191.config`, проект `bf7_h6127`) стоит

    CONFIG_CUSTOM_KERNEL_IMGSENSOR="hi1336_mipi_raw s5k3l6_mipi_raw hi556_mipi_raw gc05a2_mipi_raw fake_serial_yuv"

а на разделе `/vendor/lib64/` лежат файлы настройки **для всех четырёх**, да
ещё в двух вариантах (`hi1336_*` и `pblchi1336_*` и так далее). То есть по
вендорской части различить, что именно стоит в этом телефоне, нельзя: раздел
общий на партию, где гнёзда набивали разными модулями.

Различает **дерево устройств живого телефона**:

    cam0_enable_sensor = hi1336_mipi_raw s5k3l6_mipi_raw
    cam1_enable_sensor = hi556_mipi_raw gc05a2_mipi_raw

Порядок в этом списке — порядок перебора, первым идёт ожидаемый. То есть
главная камера скорее всего **HI1336**, передняя — **HI556**.

**Готовой ручки питания в `/proc` нет.** `imgsensor_proc.c` заводит
`driver/camsensor`…`camsensor4` с обработчиком записи `CAMERA_HW_Reg_Debug`,
но тот умеет только читать и писать регистры сенсора — через
`imgsensor_sensor_feature_control`, то есть **через уже привязанный
драйвер**. А драйвер привязывается только после удачного `CHECK_IS_ALIVE`,
для которого сенсор должен ответить. Замкнутый круг, переключатель придётся
писать.

**Главное. У переднего гнезда нет записи в таблице питания.**

    grep -c "HI556\|GC05A2\|HI1336" .../mt6761/camera_hw/imgsensor_cfg_table.c
    0

В `sensor_power_sequence[]` записи есть для полусотни сенсоров — но ни для
одного из четырёх наших. Главное гнездо живёт только потому, что
`s5k3l6_mipi_raw` в таблице **есть** (строка 195), и его последовательность
применяется.

Дальше в `imgsensor_hw_power_sequence()`: если имя в таблице не нашлось —
`return IMGSENSOR_RETURN_ERROR`. А вызывающая `imgsensor_hw_power()` этот
возврат **не проверяет** и отдаёт наверх `SUCCESS`. Получается тихий отказ:
драйвер бодро сообщает, что питание подано, а к гнезду не приложено ничего.

Это ровно то, что мы видели 21:20: на `i2c-4` **не отвечает даже EEPROM**,
хотя на `i2c-2` он отвечает. Никакой «неисправной передней камеры» нет —
есть недостающие полтора десятка строк в таблице.

**И ещё: платформенная последовательность у нас пустая.**
`platform_power_sequence[]` целиком под `#ifdef MIPI_SWITCH`, который не
определён, так что таблица вырождается в `{NULL}`. Значит и MCLK, и сброс
подаются **только** из сенсорной последовательности. Для главного гнезда это
строки `{SensorMCLK, Vol_High, 0}` и `{RST, …}` записи S5K3L6 — они есть,
поэтому такты мы и видели.

**Доноров нет.** Ни `hi1336`, ни `gc05a2`, ни даже записей питания для
`hi556` нет ни в одном из деревьев в `kernel-work/donors/`. Сам драйвер
`hi556_mipi_raw` у нас есть (`common/v1/hi556_mipi_raw/`) и вкомпилирован —
ему не хватает только питания.

**Что делаем этим заходом:**

1. Записи питания в `imgsensor_cfg_table.c` для `hi556_mipi_raw` (переднее
   гнездо) — без них оно мертво по определению.
2. Переключатель `/proc/driver/camsensor_power`, который подаёт питание на
   гнездо и **оставляет** его. Тогда шину можно сканировать спокойно, а не в
   окне 25 мс.
3. Сборка, прошивка, замеры: состояние `vcama`/`vcamd`/`vcamio` при
   удержанном питании и скан обеих шин.

Отдельно взять на заметку замечание наблюдателя: в окне 25 мс мы поймали
включение `vcamio`, но **ни разу не видели `vcamd`**. Если при удержанном
питании цифрового питания так и не будет — это и есть причина молчания
сенсора: EEPROM живёт на одном DOVDD и отвечает, а сенсору нужен DVDD.

## 2026-09-17 21:10 — Питание камеры держим и меряем: рельсы в порядке, сенсор всё равно молчит

Собрано и прошито ядро с тремя правками (`imgsensor_cfg_table.c`,
`imgsensor_hw.c`, `imgsensor_proc.c`), модули связности пересобраны тем же
заходом. Телефон поднялся, `systemctl is-system-running` → `running`.

**Переключатель работает.**

    # cat /proc/driver/camsensor_power
    гнездо 0: обесточено, разрешены [hi1336_mipi_raw s5k3l6_mipi_raw], запись питания есть у [s5k3l6_mipi_raw]
    гнездо 1: обесточено, разрешены [hi556_mipi_raw gc05a2_mipi_raw], запись питания есть у [hi556_mipi_raw]

Запись `0 on` подаёт питание и **оставляет** его; `0 off` снимает. Учёт
ссылок общий с `/dev/kd_camera_hw`, так что чужое закрытие устройства не
гасит удержанное.

**Переднее гнездо ожило.** До правки проверка живости на гнезде 1
отрабатывала за 14–20 мкс — драйвер просто не находил имя в таблице и
выходил. Теперь:

    imgsensor_hw_power: sensor_idx 1, power 1, curr_sensor_name hi556_mipi_raw
    imgsensor_i2c_read: I2C read failed (0x0)! ... (×12)
    CheckIsAlive: Profile = 27188 us

27 миллисекунд настоящей работы: питание подано, такты идут, драйвер
двенадцать раз стучится по трём адресам. То есть **гнездо больше не мёртвое**
— просто никто не отвечает.

**Измерения под удержанным питанием (гнездо 0).** Вот ради чего всё
затевалось:

| Что | Значение |
| --- | --- |
| `vcamd` (цифровое) | **включено, 1,2 В** |
| `vcamio` (ввод-вывод) | **включено, 1,8 В** |
| `vcama` (регулятор PMIC) | выключено — и правильно, AVDD у нас ножкой |
| GPIO153 (ключ AVDD) | выход, **уровень 1** |
| GPIO102 (сброс cam0) | выход, **уровень 1** — сброс снят |
| GPIO98 (PDN cam0) | выход, **уровень 0** |
| GPIO100 (MCLK cam0) | режим **1** = `CMMCLK0`, не GPIO |
| `camtg_sel` | включён, **24 МГц** |
| ошибки `pinctrl err` в журнале | нет ни одной |

Догадка наблюдателя про недостающее цифровое питание **закрыта**: `vcamd`
включается, 1,2 В. В окне 25 мс мы его просто не успевали поймать.

**Скан шин под удержанием.** `i2c-2`: отвечают 0x50 (EEPROM) и 0x58, оба во
всех 40 проходах. `i2c-4`: не отвечает никто. Сенсор не отзывается ни на
одном адресе 0x08–0x77 ни на одной шине.

**Встречная проверка — и она важная.** Сняли удержание и просканировали
снова: на `i2c-2` **не отвечает никто**, все три регулятора выключены,
GPIO102 и GPIO153 упали в 0, GPIO100 вернулся в режим 0 (обычный GPIO). То
есть подача питания у нас настоящая, а не бумажная: с ней EEPROM говорит,
без неё молчит. И выключение возвращает всё на место.

**Где мы теперь.** Модуль главной камеры запитан, его EEPROM отвечает
осмысленными данными, сброс снят, MCLK заведён на ножку и тактируется 24 МГц
— а сама микросхема сенсора не подтверждает свой адрес. Оставшиеся версии:

1. **PDN.** Последовательность S5K3L6 ножку PDN не трогает вовсе, и она всё
   время лежит в нуле. Если у нашего модуля это не «power down» (активный
   высокий), а «shutdown» (активный низкий), сенсор так и остаётся
   выключенным. Проверяется одной попыткой поднять GPIO98.
2. **Напряжение DVDD.** У HI1336 ядро обычно 1,1 В, а мы даём 1,2 В от
   записи S5K3L6. Регулятор умеет 1,0–1,8 В, так что попробовать можно.
3. **Способ опроса.** Проба стучится чтением одного байта. Стоит повторить
   сканирование записью (как это делает `i2cdetect`) — вдруг разница есть.

Следующий шаг: расширить переключатель до управления отдельными ножками
(`<гнездо> pin PDN 1` и тому подобное), чтобы перебирать версии без
пересборки ядра на каждую.

## 2026-09-17 21:21 — Камера опознана: не хватало тактов MCLK. Обе микросхемы названы

Сенсор молчал не из-за питания и не из-за отсутствия драйвера. **Ему не
подавали тактовый сигнал.**

**Как нашлось.** Переднее гнездо после правки таблицы ожило и отозвалось по
шине, главное — нет. Разница должна была быть в чём-то, что не одинаково для
двух гнёзд. Нашлась она в таблице выводов MT6761:

    GPIO99  функция 1 = CMMCLK0
    GPIO100 функция 1 = CMMCLK1

А в дереве устройств `cam0_mclk_on` переводит в режим 1 **GPIO100**, а
`cam1_mclk_on` — **GPIO99**. То есть **нумерация гнёзд и нумерация CMMCLK
переставлены местами**: главная камера тактируется от `camtg1_sel`,
фронтальная — от `camtg_sel`. В срезе тактов было видно:

    camtg_sel    enable=5   24 МГц     <- фронтальная, включён
    camtg1_sel   enable=0   24 МГц     <- главная, НЕ включён

Мультиплексор выводов последовательность питания переключает
(`mclk_set()` только выбирает состояние pinctrl), а сам источник тактов
включает `imgsensor_clk_set()` — и его зовут **из ioctl `SET_MCLK`**, то есть
из вендорского HAL, которого у нас нет. Получалось: ножка переведена в режим
CMMCLK, а сигнала на ней нет.

GC05A2 это пережил — его шина работает от одного DOVDD. Samsung — нет.

**Что сделано.** Переключатель `/proc/driver/camsensor_power` расширен:
команда `on` теперь заодно включает нужный гнезду мультиплексор тактов на
24 МГц, плюс появились ручки для перебора догадок без пересборки ядра:

    echo "0 mclk 1 1"        # мультиплексор camtg1_sel, 24 МГц
    echo "0 mclk 1 1 26"     # он же, 26 МГц
    echo "0 pin PDN 1"       # поднять ножку PDN
    echo "0 pin DVDD 1100"   # цифровое питание 1,1 В

**Результат сразу после включения тактов.** На `i2c-2` появился новый
отозвавшийся — **0x10**, которого не было ни в одном прошлом скане. Чтение
регистра:

    ./i2creg /dev/i2c-2 0x10 1 0x0000 4   ->   30c6b000

И штатная проверка ядра:

    imgsensor_set_driver: sensor_list s5k3l6_mipi_raw hi556_mipi_raw
    imgsensor_check_is_alive:  Sensor found ID = 0x30c6
    imgsensor_set_driver: [0][0][s5k3l6_mipi_raw]

**Главная камера — Samsung S5K3L6 (13 Мп).** Драйвер для неё у нас уже
вкомпилирован; ничего доставать не надо.

**Передняя камера — GalaxyCore GC05A2 (5 Мп).** Опознана раньше, тем же
способом: под удержанным питанием на `i2c-4` отозвался 0x37 (это 0x6E>>1), а
чтение дало

    ./i2creg /dev/i2c-4 0x37 1 0x03f0 4   ->   05a20094

то есть код 0x05A2. Драйвера `gc05a2_mipi_raw` у нас нет ни в дереве, ни у
доноров — её ещё предстоит доставать или писать. Запись питания, которую я
добавил под именем `hi556_mipi_raw`, ей при этом подошла: гнездо питается, и
сенсор по шине отвечает.

**Итог по догадкам из прошлой записи:** PDN ни при чём, напряжение DVDD ни
при чём, способ опроса ни при чём. Не хватало тактов.

**Что дальше.** Включение тактов надо перенести из отладочного переключателя
в сам путь питания, чтобы камера опознавалась без ручного `echo` — иначе
любая программа, открывшая `/dev/kd_camera_hw`, снова упрётся в молчание.

**Про 0x58 — осторожно.** Чтение регистра 0x0716 у 0x58 отвечает ошибкой
шины, а 0x0000 отдаёт одни 0xff. Похоже, это устройство с однобайтовой
адресацией, и двухбайтовый адрес оно понимает как «регистр 0x07, записать
0x16». Больше 16-битными чтениями его не трогать.

## 2026-09-17 21:28 — Сенсор запущен и выдаёт кадры

Включение тактов перенесено из отладочного переключателя в сам драйвер —
`mclk_set()` в `mt6761/camera_hw/mclk/mclk.c` теперь вместе с выбором режима
ножки включает и источник тактов через `imgsensor_clk_set()`. После прошивки
камера опознаётся **без единой ручной команды**:

    ./camalive2
    камера 0 (основная): выбор драйвера принят, номер драйвера 0
                         проверка живости: принята

Дальше — запуск потока. Проба `probes/camstream.c`:

    SET_DRIVER (3107) -> SENSOR_FEATURE_OPEN (3105) -> ioctl X_CONTROL, сценарий 0

**Две ловушки по дороге.**

1. Команды вида `_IO(...)` этот драйвер не принимает вовсе:
   `imgsensor_ioctl()` отвечает `-EFAULT` всему, у чего
   `_IOC_DIR == _IOC_NONE`. Значит `KDIMGSENSORIOC_T_OPEN` и `T_CLOSE`
   бесполезны — открывать надо «свойством» 3105.
2. `adopt_CAMERA_HW_FeatureControl()` отвечает `-EFAULT` на любой вызов с
   `pFeaturePara == NULL`, даже там, где параметров нет по смыслу. Нужен
   буфер-пустышка.

**Результат:**

    драйвер подобран, номер 0
    сенсор открыт
    сценарий 0 запущен
    регистр 0x0100 (поток): 0x01 — включён
    счётчик кадров (регистр 0x0005): ff 01 03 05 07 08 0a 0c 0e 10

Счётчик кадров сенсора растёт примерно на 2 за 60 мс — то есть **около 30
кадров в секунду**. Сенсор настроен и считает кадры. Осторожно с выводом:
это говорит о работе самого сенсора, но **не** о том, что данные доходят до
приёмника в чипе — это отдельная проверка (сделана ниже, и ответ оказался
«не доходят, приёмник выключен»).

В журнале ядра видно весь путь: `kdCISModulePowerOn` 19,9 мс, `SensorOpen`
27,5 мс, `SensorControl` 3,5 мс.

**Важно про узел устройства:** `/dev/kd_camera_hw` нельзя закрывать, пока
нужен поток. На закрытии `imgsensor_release()` гасит такты и
`imgsensor_hw_release_all()` сбрасывает все ножки — сенсор выключается.
Поэтому проба держит узел открытым и ждёт сигнала.

**Чего ещё нет:** самих пикселей. Данные идут по MIPI в приёмник seninf, а
дальше их надо завести в блок CAMSV или ISP и выгрузить в память. Это
следующий кусок работы.

## 2026-09-17 21:40 — За кадром: приёмник молчит, но путь к нему найден целиком

Сенсор стримит, а пикселей нет. Разбирались, где обрыв.

**Регистровое окно наружу было заперто.** Из пользовательской части регистры
ISP доступны через `/dev/camera-isp` (`ISP_READ_REGISTER` /
`ISP_WRITE_REGISTER`). Смещения отмеряются от третьего диапазона `reg` узла
`ispsys` — а это `0x15000000` длиной `0x52000`. Но проверка в драйвере
обрезала доступ до `ISP_RANGE = 0x10000`, то есть отсекала ровно нужное:

    смещение 0x40000 — seninf (приёмник MIPI)
    смещение 0x50000 — CAMSV (выгрузка кадра в память)

Отображение и так покрывало их целиком. Окно расширено до `0x52000`
(`cameraisp/src/mt6761/camera_isp.c`). Заодно выяснилось, что собран **не**
`isp_50`, как я сперва решил, а вариант `mt6761` — он отображает все шесть
диапазонов узла одним `of_iomap` по индексу, и раскладка совпадает с нашим
деревом.

Инструмент — `probes/ispreg.c`. Кроме чтения и записи у него есть `diff`:
дважды снимает кусок регистров с паузой и печатает только изменившееся.
Живые счётчики так видно без всякой документации.

**Приёмник выключен — проверено.** При работающем потоке:

    ./ispreg diff 0x40000 0x44000 150
    в этом куске ничего не шевелится

И ровно то же без потока. Значит сенсор передаёт в MIPI, а приёмник seninf не
включён и не слушает. Ничего удивительного: у MediaTek seninf настраивает
**пользовательская часть**, ядро его только читает для отладки.

**Где лежит эта настройка — нашли.** В `/vendor/lib64/libcam.halsensor.so`.
И это хорошая новость: символы обычные, не спрятанные.

    SeninfDrv::createInstance()
    SeninfDrvImp::init() / uninit()
    SeninfDrvImp::setSeninfCSI2(SENINF_CONFIG_STRUCT*, bool)
    SeninfDrvImp::setSeninfMuxCtrl(...)
    SeninfDrvImp::setSeninfTopMuxCtrl(unsigned int, SENINF_ENUM)
    SeninfDrvImp::setTopCamMuxCtrl(SENINF_TOP_P1_ENUM, SENINF_MUX_ENUM)
    SeninfDrvImp::setSVGrabRange(...)      <- окно захвата для CAMSV
    SeninfDrvImp::configMclk(SENINF_MCLK_PARA*, unsigned long)
    ImgSensorDrv::getInstance() / init() / uninit()

То есть весь путь «включить приёмник CSI-2 → направить поток в CAMSV →
задать окно захвата» лежит готовым в вендорской библиотеке, и берётся он тем
же приёмом, что мы уже отработали на видеодекодере: загрузить блоб через
libhybris и позвать функции.

**Оценка.** Это не недели, как я записывал в перепись железа. Это работа
уровня одного-двух заходов: поднять `libcam.halsensor.so` под libhybris,
восстановить раскладку `SENINF_CONFIG_STRUCT` (её в наших заголовках нет),
позвать `init` → `setSeninfCSI2` → мультиплексоры → `setSVGrabRange`, затем
через `/dev/camera-isp` настроить DMA у CAMSV и забрать сырой байеровский
кадр. Ловушка на этом пути известна заранее: адрес буфера для ISP — это
адрес M4U за larb2, а не физический (та же грабля, что в `bf7-kms-driver`).

**Проверка, когда дойдём до кадра:** хозяин сказал, что в поле зрения лежит
круглая жёлтая крышка. Если на снимке она окажется жёлтой и круглой — значит
и цвета, и геометрия, и байеровская раскладка разобраны верно. Если серой
или синей — искать ошибку в порядке цветовых каналов.

**Уборка и проверка утечек (21:50).** Оставленную работать пробу `camstream`
сняли; после этого `camtg_sel` и `camtg1_sel` показывают счётчик включений
**0**, все три регулятора `vcam*` — `disabled`, оба гнезда в переключателе
«обесточено», на `i2c-2` не отвечает никто. То есть правка `mclk_set()`
считает ссылки честно: цикл «открыть — снять поток — выйти» ничего не
оставляет включённым. Это и была единственная непроверенная сторона той
правки.

## 2026-09-17 22:30 — Приёмник MIPI поймал кадры: главная камера сидит на входе CSI1

Большой шаг. Кадр доходит до чипа.

**Как удалось.** Настройку приёмника делает вендорская часть, и она нашлась
целиком в `/vendor/lib64/libcam.halsensor.so` с обычными, не спрятанными
символами. Написана программа `hybris/bf7-camera/bf7-camshot.c` — обычный
андроидный бинарник (NDK, bionic), как `bf7-videod`; libhybris не нужен. Она
сама поднимает сенсор через `/dev/kd_camera_hw`, потом зовёт у блоба

    SeninfDrv::createInstance() -> SeninfDrvImp::init()
    SeninfDrvImp::configSensorInput(cfg, dyn, sensorInfo, true)

и смотрит на регистры, отобразив их через `mmap` на `/dev/camera-isp`
(физический адрес идёт как смещение): IMGSYS 0x15000000, приёмник
0x15040000, аналог MIPI 0x11c10000.

**Пять ловушек по дороге, каждая стоила отдельного захода.**

1. **Отображать регистры надо до того, как поднят сенсор.** Если сперва
   открыть `/dev/kd_camera_hw`, то последующий `mmap` отдаёт одни нули.
2. **Раскладки `SENINF_CONFIG_STRUCT` нет ни в одном заголовке.** Поля
   восстановлены по коду `configInputForCSI`: обязателен источник
   `cfg+80 = 8` (CSI-2) и номер гнезда в `cfg+252`; окно захвата — четыре
   слова с `cfg+16`; вход CSI — `cfg+88`.
3. **Частота пикселей — 64-битное поле.** Без него библиотека отвечает
   «sensor does not provide mipi_pixel_rate 0 or pixel_rate 0». Лежит по
   смещению 72 (а при номере мультиплексора ≥2 — по 144).
4. **Число полос и задержка тоже наши.** `getCSIpara()` заполняет лишь два
   слова по таблице входа CSI, остальное ждёт готовым. Нашлись перебором,
   по тому, что печатает сама библиотека: `cfg+116` = число полос минус
   один (для четырёх полос — 3), `cfg+104` = задержка (у нас 85).
5. **Библиотеку можно заставить говорить.** Подробные сообщения включаются
   не свойством, а переменной `NSCam::Utils::ULog::ULogger::sDetailsLevel`
   в `libmtkcam_ulog.so` — обычный символ, пишем в него из программы. После
   этого в журнале появляется строка вида

       CSI2-0 TermDelay:0 SettleDelay:85(cnt:21) LaneNum:4 NCSI2_EN:1 ...

   именно она и показала, что полос было 1 вместо 4.

**Главная находка.** Перебрали вход CSI (`cfg+88`) от 0 до 4. При **CSI1**
в ISP ожили счётчики:

    вход CSI 0 — тишина
    вход CSI 1 — изменилось регистров 7      <---
    вход CSI 2..4 — тишина

То есть главная камера заведена на **вход CSI1**, а не CSI0, как можно было
предположить по номеру гнезда.

**Что теперь видно живьём.** При работающем потоке сами по себе меняются
регистры мультиплексора приёмника (0x40d10, 0x40d1c, 0x40d24, 0x40d28,
0x40d30) и блока CSI-2 (0x42a1c, 0x42a2c, 0x42a30). А в регистрах
мультиплексора стоит размер кадра:

    +0x40d14 = 08380618
    +0x40d18 = 08380618        (0x838 = 2104, 0x618 = 1560)

Это наш кадр предпросмотра. Он принят чипом.

**Чего не хватает.** Данные не доходят до блока захвата: ни CAM_A
(0x15004000), ни CAM_B (0x15006000), ни CAMSV (0x15050000) не оживают,
`TG_INTER_ST` стоит на 0x102. Пробовали руками: `TG_SEN_MODE`, `TG_VF_CON`
с битом включения, окно захвата, маршрут в `SENINF_TOP_CAM_MUX_CTRL`,
`CAM_CTL_EN`, программный сброс блоков. Не запускается.

Похоже, у MediaTek блок P1 включается не прямой записью, а через командную
очередь, либо ему нужен такт или выход из режима пониженного питания,
который делает только ioctl драйвера ISP. Это и разбираем дальше.

**Заодно выяснено про нумерацию:** `configTg(0)` настраивает CAM_A,
`configTg(1)` — CAM_B (видно по тому, какие регистры меняются: +0x4410 и
+0x6410).

## 2026-09-17 22:49 — CAMSV принимает кадры: не хватало такта подблоков

Тупик «данные доходят до мультиплексора, но блок захвата мёртв» пробит.
Причина оказалась в одном регистре, которого **нет в заголовках ядра**:

    0x15050020   CAMSV_CLK_EN

Он стоял в нуле, то есть внутренние такты CAMSV (TG, DMA, упаковщик) были
выключены. Ни `MODULE_EN`, ни `TG_VF_CON`, ни маршрут в `SENINF_TOP` роли не
играли: без такта конечный автомат TG стоял на месте, и `TG_INTER_ST` намертво
показывал `0x00000102`.

Имя регистра нашлось не в исходниках, а в строках вендорской библиотеки
`/vendor/lib64/libcam.iopipe.so` — там лежит её собственный дамп регистров:

    (0x9000) CAMSV_MODULE_EN    (0x9004) CAMSV_FMT_SEL
    (0x9008) CAMSV_INT_EN       (0x9020) CAMSV_CLK_EN
    (0x903C) CAMSV_PAK          (0x9208) CAMSV_IMGO_SV_BASE_ADDR
    (0x920C) IMGO_SV_OFST_ADDR  (0x9210) IMGO_SV_XSIZE
    (0x9214) IMGO_SV_YSIZE      (0x9218) IMGO_SV_STRIDE

Смещения считаются от `0x9000`, то есть от базы CAMSV `0x15050000`.

**Какие биты железо вообще принимает** (записали `0xffffffff`, прочитали):

    CAMSV_CLK_EN    0x00008005   биты 0, 2, 15
    CAMSV_MODULE_EN 0xc000001d   биты 0, 2, 3, 4, 30, 31
    CAMSV_FMT_SEL   0x0f030037   биты 0,1,2,4,5, 16,17, 24..27

**Рабочая последовательность** (после того, как вендорская `SeninfDrvImp`
настроила приёмник и `setSVCfg`/`setSVGrabRange` заполнили TG):

    0x15050020 = 0x00008005   такты подблоков CAMSV
    0x15050010 = 0x00000000   снять сброс
    0x15050000 = 0x40000001   включить блок (бит 30 стоял по умолчанию)
    0x15050414 = 0x00001003   TG_VF_CON, бит 0 = VFDATA_EN

Результат — `CAMSV_TG_FRMSIZE_ST` (0x1505_0448) показывает **0x08380619**,
то есть 2104×1561: ровно тот кадр, что отдаёт S5K3L6 в предпросмотре
(1560 строк изображения плюс строка встроенных данных). `TG_INTER_ST`
перестал стоять на 0x102 и меняется между 0x1003 и 0x1007 — автомат крутится.

**Ловушка на будущее:** вендорская `setSVViewFinderMode` ставит `TG_VF_CON`
в `0x1002`, то есть **без** бита 0. Включение видоискателя у MediaTek делает
драйвер P1 отдельно, уже после того, как отдан буфер под DMA.

**Про нумерацию портов.** Число, которое `SeninfDrvImp` называет
`SENINF_TOP_P1_ENUM`, — это просто номер ниббла в `SENINF_TOP_CAM_MUX_CTRL`
(0x1504_0010): `setTopCamMuxCtrl(p1, mux)` кладёт номер мультиплексора в
ниббл `p1`. `configTg(0)` настраивает CAM_A, `configTg(1)` — CAM_B, для
CAMSV `configTg` не делает ничего (у него свой `setSVCfg`).

## 2026-09-17 22:53 — первый кадр с камеры в память

CAMSV пишет кадр в оперативную память по DMA. Снимок настоящий: видна
комната, коробка, предметы. Тёмный и не в фокусе — выдержка стоит
минимальная, а линза никуда не наведена, но это полноценный кадр с матрицы.

**Буфер.** Адрес, который понимает блок MMU камеры (M4U), берётся у ION без
единой правки ядра:

    открыть /dev/ion
    ION_IOC_ALLOC   len, heap_id_mask = 1 << 10   (мультимедийная куча)
    ION_IOC_CUSTOM  cmd = ION_CMD_MULTIMEDIA(1),
                    mm_cmd = ION_MM_GET_IOVA(8),
                    module_id = 24 (M4U_PORT_CAM_IMGO_S — это выход CAMSV0)
      -> phy_addr = 0x42300000
    ION_IOC_MAP     -> dma-buf, его же mmap'ом читает процессор

Порт именно `CAM_IMGO_S`: в таблице `m4u_platform.h` буква S у портов larb2
означает CAMSV, а не «second». Фолтов M4U в журнале нет ни одного.

**Регистры DMA CAMSV0** (от 0x15050000):

    +0x208 IMGO_SV_BASE_ADDR = адрес от ION
    +0x20C IMGO_SV_OFST_ADDR = 0
    +0x210 IMGO_SV_XSIZE     = байт в строке − 1
    +0x214 IMGO_SV_YSIZE     = строк − 1
    +0x218 IMGO_SV_STRIDE    = байт в строке
    +0x01C IMGO_FBC          = 0   (без кольца кадров)
    +0x000 MODULE_EN         = 0x4000001d
    +0x020 CLK_EN            = 0x00008005
    +0x010 SW_CTL            = 0
    +0x414 TG_VF_CON        |= 1   — включать ПОСЛЕДНИМ, после адреса

**Формат по умолчанию.** При `FMT_SEL = 0x01000000` и `PAK = 0` CAMSV пишет
не упакованный MIPI RAW10, а **два байта на пиксель**: 16-битное слово,
младший порядок, значение = отсчёт 10 бит, сдвинутый влево на 4. То есть
`пиксель = слово >> 4`, чёрный уровень 64 — обычный для 10 бит. Шаг строки
поэтому `ширина * 2` = 4208 байт, а не 2630. Первая попытка с упакованным
шагом дала обрезанные строки — картинка была, но вдвое уже.

Встроенную строку данных CAMSV не пишет: при высоте 1561 последняя строка
осталась с меткой, реальных строк ровно 1560.

**Инструмент:** `kernel-work/probes/camgrab.c` — обычная программа glibc,
собирается на телефоне `gcc`. Поток при этом держит `bf7-camshot`, она же
настраивает приёмник вендорской библиотекой.

## 2026-09-17 22:57 — резкий цветной кадр: выдержка, усиление и фокус

Кадр наведён и проявлен. На снимке читается этикетка «КЛЮКВА в сахарной
пудре, 120 г», красные ягоды красные, листья зелёные, а внизу слева — та
самая круглая жёлтая крышка, по которой хозяин просил сверять цвет. Значит
раскладка Байера определена верно.

**Порядок Байера — GRBG.** То есть первый пиксель строки — зелёный в красной
строке; это согласуется с `sensor_output_dataformat = SENSOR_OUTPUT_FORMAT_RAW_Gr`
в драйвере S5K3L6. Пиксель = `слово >> 4`, вычитать чёрный уровень 64.

**Выдержка и усиление — прямой записью по шине** (i2c-2, адрес 0x10),
менять можно на ходу, поток не срывается:

    0x0340  длина кадра в строках (по умолчанию 3260)
    0x0202  выдержка в строках (не больше длины кадра минус несколько)
    0x0204  аналоговое усиление, 0x0020 = 1x

Проверено лесенкой: 0x0020 → средняя яркость 77, 0x0080 → 97, 0x0200 → 197
при той же сцене. Чтение регистров сенсора через `I2C_RDWR` с повторным
стартом почему-то не проходит, а запись проходит — для управления этого
достаточно.

**Фокус работает и правок не требует.** Линза — DW9714AF на i2c-2, адрес
0x0C, узел `/dev/MAINAF` создаётся с загрузки (`CONFIG_MTK_LENS=y`,
встроенный, поэтому в `lsmod` его нет). Последовательность:

    fd = open("/dev/MAINAF")
    ioctl(fd, AFIOC_S_SETDRVNAME, "DW9714AF")   _IOW('A',10,char[32])
    ioctl(fd, AFIOC_T_MOVETO, положение)        _IOW('A',1,u32), 0..1023

**Узел держать открытым**: закрытие снимает питание с катушки и линза
уезжает. Питание линзы — отдельный регулятор `vldo28` главного PMIC MT6357,
он включается самим открытием узла; в таблице питания гнезда камеры `AFVDD`
не значится, поэтому без открытого `/dev/MAINAF` чип на 0x0C молчит.

**Не звать** `AFIOC_S_SETPOWERDOWN` (11): на mt6761 она безусловно шлёт
последовательность выключения от DW9718SAF, какой бы мотор ни был выбран.

Лесенка по резкости (зелёный канал, дисперсия лапласиана) дала максимум
около положения 240 при съёмке предмета на расстоянии вытянутой руки.
Снимок: `docs/bf7-native/shots/camera-first-frame.png`.

## 2026-09-17 22:57 — дальномера для фокуса на плате нет

Хозяин предположил, что фокус наводится инфракрасным дальномером. Проверено —
такого датчика на этом телефоне нет:

* в дереве устройств ни одного узла про дальномер, приближение или
  освещённость (искали `vl53`, `tof`, `laser`, `alsps`, `proximity`, `ltr`,
  `stk`, `apds`, `cm36`, `tmd`);
* прочёс всех семи шин i2c при поднятом питании камеры даёт на i2c-2 только
  линзу `0x0c`, матрицу `0x10` и две страницы EEPROM модуля `0x50`/`0x58`;
  классических адресов лазерных дальномеров (0x29, 0x52) нет нигде;
* в вендорских библиотеках камеры под наш проект нет ни `vl53`, ни `laser`,
  ни `tof`; единственный упомянутый способ помощи фокусу — PDAF, фазовый,
  прямо на матрице, но в драйвере S5K3L6 он не объявлен.

Значит наводка возможна только контрастная: двигать линзу и смотреть на
резкость кадра — ровно то, что и сделано. Тёмное окошко у камеры — это,
судя по дереву, вспышка `strobe_main` на i2c-5, адрес 0x63.

## 2026-09-17 23:09 — камера стала /dev/video0

Обычный `ffmpeg -f v4l2 -i /dev/video0` снимает с камеры телефона резкий
цветной кадр. Снимок: `docs/bf7-native/shots/camera-v4l2-ffmpeg.png`.

**Из чего собрано.** Три части, и разделены они не по прихоти:

1. `v4l2loopback` 0.12.7, собранный против нашего ядра
   (`insmod v4l2loopback.ko video_nr=0 exclusive_caps=1 max_buffers=4`).
   `exclusive_caps=1` обязателен: без него узел объявляет себя и входом, и
   выходом, и браузеры такую камеру пропускают.
2. `hybris/bf7-camera/bf7-seninf-up` — единственная часть, которой нужен мир
   Android: приёмник MIPI настраивает вендорская `libcam.halsensor.so`, а она
   под bionic. Поднимает сенсор, отдаёт настройку приёмника библиотеке,
   запускает предпросмотр — и дальше спит, держа `/dev/kd_camera_hw`
   открытым. Закрытие узла гасит такты и сбрасывает ножки.
3. `hybris/bf7-camera/bf7-camd` — **обычная программа glibc, собирается прямо
   на телефоне** (`gcc -O2 -o bf7-camd bf7-camd.c -lm`). Берёт кадры из
   CAMSV, разбирает мозаику Байера, ведёт выдержку, баланс белого и наводку,
   пишет YUYV в `/dev/video0`.

**Порядок обязателен:** вендорские `setSVCfg`/`setSVGrabRange`/
`setSVViewFinderMode` надо звать **после** запуска сценария предпросмотра.
Если позвать их раньше, окно захвата остаётся пустым и DMA не пишет ни байта,
хотя приёмник кадры уже принимает.

**Готовность приёмника проверять по 0x1504_0d14** — размеру кадра,
намеренному мультиплексором seninf (там появляется 0x0838 x 0x0618). По
`CAMSV_TG_FRMSIZE` проверять нельзя: пока демон не включил `TG_VF_CON`, там
ноль, и проверка ложно говорит «потока нет».

**Адрес буфера DMA на лету не меняется.** Попытка чередовать два буфера,
переписывая `IMGO_SV_BASE_ADDR` по прерыванию конца кадра, не работает:
железо продолжает писать по адресу, который стоял в момент взвода блока, и
каждый второй кадр приходит пустым (проверено тремя способами: простая
запись, взвод бита 31 в `MODULE_EN`, снятие бита 30). Поэтому буфер один.

Разрыва кадра при этом не возникает, и вот почему: разбор занимает 28 мс, а
запись следующего кадра начинается только через ~17 мс после конца
предыдущего (вертикальный интервал) и идёт сверху вниз медленнее, чем мы
читаем. Пишущий нас не догоняет. Пустых кадров за 400 — ровно один, самый
первый.

**Буферы ION брать кэшируемыми** (`flags = ION_FLAG_CACHED`), а перед чтением
выбрасывать из кэша: `ION_IOC_CUSTOM`, `ION_CMD_SYSTEM`, `ION_SYS_CACHE_SYNC`,
тип `ION_CACHE_INVALID_BY_RANGE`. С некэшируемым буфером разбор одного кадра
занимал ~600 мс (1,6 кадра в секунду), с кэшируемым — 28 мс и 33 кадра в
секунду.

**Что умеет автоматика демона:** выдержка и усиление правятся по средней
яркости прямой записью в 0x0202/0x0204 (через кадр — сенсор применяет с
задержкой), баланс белого — «в среднем серое», наводка — перебор по контрасту
(грубо девятью шагами, затем точно пятью) с повторной наводкой, если резкость
упала вдвое и держится так 25 кадров.

## 2026-09-17 23:22 — поток, а не один кадр: перевзвод выгрузки CAMSV

Хозяин заметил то, что счётчики скрывали: картинка не обновлялась. Проверка
в лоб — снять двадцать кадров подряд и сравнить их побайтово — показала один
и тот же кадр двадцать раз.

**Причина.** Блок CAMSV укладывает в память ровно один кадр и замолкает. В
состоянии прерываний (0x1505_000C) при этом горит бит 19, `IMGO_DROP_ST`: все
последующие кадры отбрасываются. Покадровый учёт (FBC, регистр 0x1505_001C)
показывает «буферов 1, занят 1» — то есть свободного места нет, а забрать
кадр некому. Для CAM_A ядро в таком случае гасит бит 0x4000 в его FBC, а про
CAMSV в драйвере прямо написано, что адрес буфера оно не трогает вовсе:

    /* ISP_WR32(ISP_REG_ADDR_IMGO_SV_BASE_ADDR, ...); */
    pr_debug("[rtbc][ENQUE]IMGO_SV:addr should write by MVHDR");

То есть выгрузку CAMSV полагается взводить заново самим.

**Разбор FBC.** Записали 0xffffffff и прочитали: принимаются биты 11, 14, 15 и
16..19. Биты 20..23 (сколько буферов занято) и 24..27 (сколько их всего) —
только для чтения. Ни бит 16, ни обнуление регистра потока не возобновляют:
проверено, кадр остаётся прежним.

**Что помогает.** Перебрали шесть способов, считая повторы подряд идущих
кадров за 300 кадров:

    ничего не делать                        210 повторов из 300
    MODULE_EN выключить/включить            206
    видоискатель + MODULE_EN + адрес        1
    штатный сброс выгрузки                  0
    обнулить FBC                            206
    дёрнуть SW_CTL без ожидания             244

Взят штатный сброс выгрузки — тот же, что делает ядро в `ISP_Reset()` для
CAMSV, только после каждого кадра:

    0x1505_0010 = 1          IMGO_RST_TRIG
    ждать, пока не встанет бит 1 (IMGO_RST_ST)
    0x1505_0010 = 4          SW_RST
    0x1505_0010 = 0
    0x1505_0208 = адрес буфера
    0x1505_0000 = 0x4000001d MODULE_EN

**Проверка по времени, а не по счётчикам:** три секунды съёмки обычным ffmpeg
дают 90 кадров, из них 80 различных — около 27 новых кадров в секунду.
`guvcview` показывает в заголовке окна «25,16 fps».

**Урок.** Счётчик кадров блока захвата и число отданных кадров не доказывают
ничего: и то и другое прекрасно растёт, когда в памяти лежит один и тот же
снимок. Проверять надо содержимым — сравнением соседних кадров.

Снимок экрана телефона с guvcview: `docs/bf7-native/shots/camera-guvcview-on-panel.png`.

## 2026-09-17 23:52 — наводка: /dev/MAINAF молчит, катушка слушается шины

Автофокус заработал, но не через узел ядра.

**Главное: `AFIOC_T_MOVETO` на `/dev/MAINAF` принимается без ошибки и ничего
не делает.** Доказано полным проходом по всему ходу линзы: резкость не
менялась ни на йоту (0,0015 на всех одиннадцати положениях). Те же положения,
записанные прямо по шине, дают настоящую кривую с вершиной:

    положение    0   96  192  288  384  480  576  672  768  864  960
    резкость   .0016 .0016 .0017 .0061 .0064 .0074 .0073 .0020 .0020 .0020 .0020

**Как двигать катушку самим.** DW9714 на `i2c-2`, адрес 0x0C, две посылки:

    [положение >> 4, (положение & 0xF) << 4]

Ровно то же делает `s4AF_WriteReg()` в
`drivers/misc/mediatek/lens/mtk/main/common/dw9714af/DW9714AF.c`. Узел
`/dev/MAINAF` всё равно надо держать открытым — его открытие включает питание
катушки (`vldo28` главного PMIC MT6357).

Почему ядро молчит — отдельный вопрос: `AF_SetMotorName()` перебирает
`g_stAF_DrvList` и выходит на первой записи с `uEnable != 1`, так что список,
похоже, пуст. Разбираться отдельно; для работы камеры это не нужно.

**Метрика резкости.** Первая считалась по яркости после гаммы и «плыла»
вместе с автоэкспозицией: один и тот же кадр при разной выдержке давал разную
«резкость», и наводка уходила куда попало. Теперь — сумма квадратов перепадов
по зелёному каналу **до** гаммы, по горизонтали и по вертикали, делённая на
квадрат средней яркости той же области. Область — середина 60% кадра:
центральная треть оказалась слишком узкой, мишень в неё не попадала, и в
замер шёл ровный кусок стола.

**Выдержка на время наводки замораживается.** Перебор сравнивает кадры между
собой, значит сравнивать надо равное с равным.

**В темноте удлиняем кадр.** Когда выдержка и усиление уже в потолке,
поднимаем длину кадра (регистр 0x0340) с 3260 до 9780 строк — до десяти
кадров в секунду. Лучше десять кадров и видно, чем тридцать и темно.

**Баланс белого — по светлым местам, и без насыщенных точек.** Две ловушки
подряд. Первая: «в среднем серое» по всему кадру врёт, когда полкадра
занимает тёплый деревянный стол — белая бумага уходила в розовое. Вторая, уже
после перехода на светлые места: зелёный канал упирается в потолок раньше
прочих, и если считать баланс по пересвеченным точкам, выходит, что зелёного
мало, — красный с синим задираются, и бумага снова розовеет. Лечится
исключением точек выше 900 из 959 и защитой светов в автоэкспозиции: если
пересвечено больше двух сотых кадра, выдержка убавляется, что бы ни говорила
средняя яркость.

**Множители баланса нормируем так, чтобы наибольший был единицей** — то есть
приглушаем сильный канал, а не задираем слабые. Иначе света упираются в
потолок и белое красится.

## 2026-09-17 23:52 — почему ядро залило журнал

Побочно вскрылось: `systemd-journald` крутился на целом ядре и не мог
подняться, журнал распух до 200 МБ. Виноваты были мы сами. В CAMSV были
включены **все** прерывания (`INT_EN = 0xffffffff`), и обработчик ISP в ядре
на каждом кадре писал две строки:

    [ISP]SV0:Err IRQ, Type(3), Status(0x...)
    [ISP]SV0:Lost done_N

Первая — потому что в состоянии горят биты ошибок (`IMGO_DROP`), вторая —
потому что на начале кадра покадровый учёт полон. И то и другое — следствие
нашего же перевзвода выгрузки. Тридцать кадров в секунду давали шестьдесят
строк.

Лечится не ограничителем журнала, а тем, что прерывания вообще не включаются:
`INT_EN = 0`. Готовность кадра тогда ловится по самому покадровому учёту —
поле 20..23 регистра `CAMSV_IMGO_FBC` (0x1505_001C) показывает, сколько
буферов занято; стало не ноль — кадр в памяти. **Важно:** биты в `INT_ST`
при выключенных прерываниях не взводятся, поэтому опрашивать надо именно FBC.

Побочная выгода: кадров стало больше (37,8 в секунду против 33), а разбор
кадра ускорился с 28 до 24 мс — процессор больше не отвлекается на журнал.

## 2026-09-18 00:01 — цвет по палитре: матрица и отбор серых точек

Хозяин показал на экране второго телефона палитру — чистые красный, зелёный,
синий и белый. По ней и откалибровали цвет.

**Разрыв кадра нашёлся именно на палитре.** Поперёк снимка шёл шов: верх
темнее низа. Это читающий догонял пишущего — разбор занимает 24 мс при кадре
в 27 мс. Раньше казалось, что запаса хватает, но после перевзвода выгрузки
кадры пошли чаще, и запас исчез.

Лечится чередованием двух буферов, и теперь оно работает: само по себе
переписывание `IMGO_SV_BASE_ADDR` железо не подхватывает, но **после сброса
выгрузки новый адрес вступает в силу**. Проверено: 420 кадров подряд, ни
одного пустого, ни одного повтора, 30 кадров в секунду.

**Цветовая матрица.** Светофильтры на матрице перекрываются: чистый зелёный
засвечивает и красные, и синие точки. Без поправки насыщенность была
никакая. Посчитали матрицу по снимку палитры — задача наименьших квадратов с
условием, что сумма каждой строки равна единице (тогда серое остаётся серым
и баланс белого матрица не трогает). В долях 1/256:

     486  -236     6
     -39   348   -53
      19  -159   396

Применять к линейным значениям, **до** гаммы.

**Ловушка: баланс белого сбивают сами цветные полосы.** С матрицей белая
полоса стала розовой. Причина: в замер баланса шли и красная, и зелёная, и
синяя полосы — расчёт считал их «серым в среднем». Лечится отбором: в замер
идут только точки, где самый сильный канал не больше чем вдвое сильнее
самого слабого.

**Что получилось** (средние значения полос в снимке):

    было (без матрицы)      стало
    красный 132/ 50/ 36     173/ 49/ 34
    зелёный 113/141/110       0/188/ 51
    синий    31/ 59/154       0/ 45/188
    белый   177/152/180     174/186/187

То есть зелёный и синий стали чистыми, а белый — нейтральным.

**Ручное управление.** Демон читает команды из файла `/run/bf7-camd` и тут же
его убирает. Программам это не нужно, а человеку при настройке необходимо:

    echo "фокус 420"          > /run/bf7-camd
    echo "фокус авто"         > /run/bf7-camd
    echo "выдержка 300"       > /run/bf7-camd
    echo "усиление 0x40"      > /run/bf7-camd
    echo "яркость 130"        > /run/bf7-camd
    echo "баланс 1.0 0.9 1.1" > /run/bf7-camd

Слова понимаются и по-английски (focus, exposure, gain, target, wb, auto).

## 2026-09-18 00:09 — вспышки на этой плате, похоже, нет

Собрали ядро с четырьмя опциями, которых не хватало для MT6370
(`CONFIG_RT_REGMAP`, `CONFIG_MFD_MT6370_PMU`, `CONFIG_MT6370_PMU_FLED`,
`CONFIG_MTK_FLASHLIGHT_MT6370`), прошили, загрузились. Драйвер не завёлся:

    mt6370_pmu: probe of 5-0034 failed with error -121
    flashlights_mt6370: mt6370_probe: Failed to get ht flashlight device
    flashlights-mt6370: probe of flashlights_mt6370 failed with error -14

`-121` — это `EREMOTEIO`, то есть чип по адресу 0x34 на i2c-5 не отвечает.
И это согласуется с прочёсом шин: **на i2c-5 не отозвался никто** — ни
`subpmic_pmu@34`, ни `strobe_main@63`. Узлы в дереве устройств есть, а
микросхем за ними нет: дерево у MediaTek общее на всю платформу, а на дешёвом
аппарате эти места не распаяны.

Значит вспышка (если она тут вообще есть) висит не на MT6370. Тот белый
светодиод, которым мигали раньше, сидит на `leds-mt6357`, то есть на главном
PMIC. Разбираться отдельно; опции в ядре оставлены — вреда от них нет, драйвер
просто не привязывается.

Побочно проверено, что сборка v3 в остальном здорова: Wi-Fi поднялся
(`wlan0` получил адрес), модули связности пересобраны тем же заходом, камера
после перезагрузки поднимается сама.

## 2026-09-18 00:13 — камера поднимается сама и снимает с первого кадра

Проверено холодной загрузкой: после перезагрузки `/dev/video0` появляется
сам, обе службы поднимаются, и обычный `ffmpeg` снимает кадр без единой
ручной команды.

Модуль грузится штатным путём Debian, а не строчкой в службе:

    /etc/modules-load.d/bf7-camera.conf   -> v4l2loopback
    /etc/modprobe.d/bf7-camera.conf       -> options v4l2loopback video_nr=0
                                             exclusive_caps=1 max_buffers=4

**Состояние запоминается между запусками** — `/var/lib/bf7-camd.state`:
положение линзы, выдержка, усиление, длина кадра и три множителя баланса
белого. Без этого первые секунды уходили на перебор, и кадр, снятый сразу,
выходил смазанным и зелёным — а обычная программа именно так себя и ведёт:
открыла камеру, взяла кадр, закрыла. С запоминанием первый же кадр резкий и
верный по цвету.

Баланс белого в первые шестьдесят кадров подтягивается втрое резче, дальше
плавно — чтобы при смене освещения не дёргался.

## 2026-09-18 00:38 — почему картинка дёргалась и что с этим сделали

Хозяин посмотрел поток в guvcview и в браузере: «картинка дёрганая».
Замер честным читателем (`kernel-work/probes/vread.c`, читает `/dev/video0`
и печатает распределение промежутков) показал ровно то, на что жаловались:

    медиана 43,9 мс (22,8 к/с), макс 254 мс, 100% кадров дольше 40 мс

Разбор занял пять заходов, и каждый вскрыл отдельную причину.

**1. Разбор кадра не успевал в такт сенсора.** Один поток тратил 24 мс при
кадре в ~20 мс, и пока мы считали предыдущий кадр, следующий приходил и
уходил. Наружу выходило то через два такта, то через три — рваное движение.
Разбор разложен по строкам между ядрами (`convert_band`, `bworker`,
`convert_frame`).

**2. Полосы дрались за кэш.** Счётчики соседних полос (`sumR`, `hiN`,
`sharp` и прочие) лежали в одной кэш-линии, и четыре ядра отнимали её друг
у друга на каждом пикселе: разбор стоил 60 мс процессорного времени вместо
20. Накопление перенесено в локальные переменные, структура полосы выровнена
на 64 байта. Заняло процессора: 211% → 152% → 61% от одного ядра
при трёх потоках (замер с читателем, работающим весь замер; первая цифра,
которую я получил для трёх потоков, была снята на отрезке, где читатель уже
закончил, и показывала половину холостого хода).

**3. Защита светов работала рывком.** `if (clipf > 0.02) err = -40;` —
автоматика уводила яркость вниз, пересвет пропадал, защита отпускала,
автоматика гнала обратно. На тёмной комнате со светящимся окном в кадре
усиление металось от 0x60 до 0x300 несколько раз в секунду. Поправка сделана
плавной (от нуля при 6% пересвета до -40 при 16%) — у пары «яркость —
пересвет» появилась общая точка покоя. Скачок яркости между соседними
кадрами: 9,08 → 0,28.

**4. Автоматика мерила кадры, снятые до её команды.** Выдержка и усиление
доезжают до сенсора за два-три кадра. Пока поток шёл 22 к/с, замер раз в три
кадра успевал; на 50 к/с три кадра — это 60 мс, меньше задержки сенсора.
Добавлена пауза `ae_hold` 0,12 с после каждой команды, шаг ограничен
множителем 0,88…1,12.

**5. Наводка рыскала.** Пауза после шага линзы отсчитывалась в кадрах
(«три кадра»); на 50 к/с это 50 мс, а DW9714 за такое время до места не
доходит — резкость мерилась на летящей линзе, перебор давал случайный
максимум. Пауза переведена во время (`settle`, по умолчанию 130 мс).
Добавлено остывание: пять секунд после наводки не перенаводимся, и падение
резкости должно держаться 0,8 с. Планка удержания больше не ползёт вверх
рывком от случайно резкого кадра. Наводок за 33 с: 3 → 1.

**6. Провал ровно 433 мс оказался нашим собственным.** В покое демон спал
`usleep(400000)`; когда программа открывала камеру, первая секунда шла
рывками — мы ещё спали. Ожидание раздроблено на куски по 50 мс с выходом по
появлению зрителя, опрос зрителей вынесен в отдельный поток (перебор
описателей всех процессов стоит до 400 мс и в главном цикле ему не место).

**Главное наблюдение по ходу:** в замере по участкам ожидание кадра всегда
равно нулю — железо снимает быстрее, чем мы разбираем, и готовый кадр лежит
наготове всегда. Значит темп задаём мы, а он гулял вместе с временем разбора
(17…28 мс). Поэтому выдача переведена на ровный шаг по часам (`fps`, по
умолчанию 30), а не «как получится».

Итог тем же замером:

    медиана 33,4 мс (30,0 к/с), 95% 35,0 мс, макс 36,7 мс, дольше 45 мс — ни одного

guvcview в заголовке: было 21,59 к/с, стало 29,32 к/с.

**Заодно проверено на Chromium** (`--no-sandbox --use-fake-ui-for-media-stream`):
страница показывает «камера: BF7-Camera — 1024x768», картинка резкая. Это
важнее Firefox: на Chromium построены настольные месенджеры, и `exclusive_caps`
у v4l2loopback существует именно ради него. Снимок:
`docs/bf7-native/shots/camera-chromium-getusermedia.png`.

Мелочь, на которой потерял время: `python3` в роли читателя врёт — свои
задержки сборщика мусора он показывает как провалы потока. Мерить надо
программой на C.

И ещё: `/proc/*/fd` внутри комментария на C закрывает комментарий.

## 2026-09-18 00:52 — три поправки после разбора и итоговые числа

Наблюдатель указал на два места, которые я не проверил, и на одно, где
соврал бы журнал.

**Баланс белого подстраивался втрое чаще, чем задумано.** Ворота блока
автоматики раньше стояли на «каждом третьем кадре»; я перевёл их на паузу
после команды сенсору — выдержке так правильнее, но баланс сидит в том же
блоке и получил бы тридцать поправок в секунду вместо десяти. Вернул деление
на три перед балансом. На неподвижной сцене за 20 с: ббR 1.00 ббG 0.46
ббB 0.73 без единого шевеления.

**Такт сенсора нельзя мерить по себе.** С тех пор как выдача идёт по часам,
промежуток между нашими кадрами равен 33 мс по построению, и `line_us`
считался из него — то есть потолок `minfps` не сработал бы никогда. Теперь
такт берётся у железа: поле 16..23 регистра `TG_INTER_ST` (SV+0x44C) —
счётчик снятых кадров; делим прирост счётчика на прошедшее время.

**Цифра нагрузки в журнале была снята неверно** и поправлена выше: 61% от
одного ядра при трёх потоках, замер с читателем, работающим весь отрезок.

Итог на службе, без отладочного вывода, 859 кадров подряд:

    медиана 33,4 мс (30,0 к/с), 95% 35,5 мс, макс 47,9 мс,
    дольше 45 мс — 0,3% (три кадра за 29 с)
    наибольший скачок яркости между соседними кадрами 4,27 из 84
    наводок за 29 с — одна

Перезапуск андроидной части (`systemctl restart bf7-camera.service`) при
живом читателе: поток возвращается сам, блок захвата не залипает, 30,0 к/с.

**Что осталось непонятым в камере** (к резкости и плавности отношения не
имеет, но к качеству картинки — да): длина кадра и усиление ведут себя не
по книге. При `flen` 9780 сенсор давал 50 к/с, при 3260 — 38, то есть
наоборот; учетверение усиления поднимает яркость всего на пятую часть.
Похоже на групповую фиксацию настроек (0x0104) или на потолок аналогового
усиления в 16×. Проверять при свете, сейчас в комнате темно.

## 2026-09-18 01:10 — что на самом деле делают длина кадра, выдержка и усиление

Полтора часа опытов, которые я едва не оставил незаписанными. Три ручки
сенсора S5K3L6 ведут себя не так, как я считал, и ошибка была в моих выводах,
а не в железе.

**Длина кадра (0x0340) такт не меняет.** Прогон 3260 → 6520 → 13000 → 19560 →
обратно: выдача держится 54 кадра в секунду при **нуле повторов** — то есть
каждый кадр свой, сенсор их и правда столько выдаёт. Ноль повторов тут и есть
доказательство: счётчики соврали бы, содержимое — нет.

**Выдержка (0x0202) работает и растит свет линейно.** При постоянном усилении
0x40: 1600 → 21,9; 3200 → 30,9; 4800 → 37,1; 6400 → 42,9; 12800 → 58,9;
18000 → 70,0; и дальше 24000 → 90,2; 32000 → 103,2; 39000 → 112,3. В свете
(яркость в степени 2,2) это ровная прямая: 18000/1600 = 11,25 раза по свету
даёт 2,96 по яркости, намерено 3,2.

**Усиление (0x0204) тоже линейно:** 0x20 → 0,7; 0x80 → 3,5; 0x200 → 17,6.
Мой прежний вывод «усиление почти не отзывается» был неверен — он снимался с
несвежей строки журнала, пока демон дремал на двух кадрах в секунду.

**Поправка к сказанному хозяину.** Я написал «вся лестница выдержки была
холостой» — это неправда. Длина кадра не замедляет сенсор, но **поднимает
потолок выдержки** (`expo_max = flen - 12`), а потолок и есть то, что даёт
свет в темноте. Лестницу надо не убирать, а понимать правильно.

**Чего выяснить не удалось.** Как выдержка в 39000 единиц укладывается в кадр
длиной 1/54 с. Настоящее накопление столько длиться не может; остаются
внутреннее домножение в сенсоре и несовпадение единиц. Замер шума, который
это различил бы, я испортил: снимал на ровной серой заливке, кадр упирался в
потолок, а у насыщенных точек шума нет — вышло «шум упал в пять раз при той
же яркости», чего не бывает.

**Чего не существует:** счётчика кадров CAMSV в поле 16..23 регистра
TG_INTER_ST (SV+0x44C). Он не тикает — в дампе `camsv1.txt` там и было
0x00000102 с нулями в этих битах. Всё, что я на нём построил (`sens_fps`,
`line_us`, потолок `minfps`), не работало ни дня и снесено.

## 2026-09-18 01:13 — почему всё зеленело: «серость» мерили до баланса

Хозяин поставил камеру прямо на монитор, а на мониторе — ровная нейтральная
серая заливка (окно-лампа `hybris/bf7-camera/lamp.py`, цвет читается из
`/tmp/lamp-colour`, так что менять его можно снаружи). Камера рисовала эту
заливку **ярко-зелёной**. Заведомо известный цвет в кадре — лучший разбор,
какой можно пожелать.

Причина: отбор точек для баланса белого шёл по **сырым** значениям:

    else if (g >= hithr && grayish(p01, g, p10))

а `grayish` требует `max <= min * 2`. У матрицы зелёных точек вдвое больше,
и ровный нейтральный серый в сыром виде даёт G ≈ 2,2·R — то есть отбор
отбраковывал именно то, ради чего он существует. Замер оставался пустым,
баланс не двигался вовсе.

Поправка на строку: судить о «серости» по значениям **после** баланса, но до
цветовой матрицы. Тогда отбор означает «точки, которые нынешний баланс уже
считает серыми», и грей-ворлд сходится к настоящему серому.

Проверка на той же заливке, через 25 с после запуска:

    с матрицей   R 176 G 176 B 175
    без матрицы  R 177 G 177 B 176

То есть ровно ноль, и цветовая матрица нейтраль не портит (суммы строк = 1).
Сырые средние при этом R 186, G 408, B 287 — зелёного вдвое с лишним больше,
как и положено.

Побочно: в отладочной строке разъехался порядок значений (добавил каналы, а
время разбора осталось вторым) — «разбор 293 мс» на самом деле был сырой
красный. Числа в журнале проверять по смыслу, а не по подписи.

## 2026-09-18 01:18 — настоящие ручки V4L2: камера настраивается как обычная

Оставалась последняя подпорка: фокус, выдержка и баланс задавались через
`/run/bf7-camd` — посторонний файл, о котором ни браузер, ни месенджер, ни
«Камера» знать не могут. Обычная программа настраивает камеру через
`VIDIOC_S_CTRL` на самом узле, а ручки объявляет драйвер. Значит, ручки нужно
добавить в `v4l2loopback`.

**Заплата** — `kernel-work/v4l2loopback/bf7-camera-controls.patch`, 66 строк.
Модуль объявляет стандартные ручки (`v4l2_ctrl_new_std`) и **только хранит**
их значения; в `v4l2loopback_set_ctrl` для них добавлен `break` вместо
`-EINVAL`. Применяет их тот, кто пишет кадры. Добавлены:

    Brightness                  0..255      цель автоматики по яркости
    Gain                       32..768      единицы сенсора, 0x20 = единица
    White Balance, Automatic    0..1
    White Balance Temperature  2500..9000 K
    Auto Exposure               список      авто / вручную
    Exposure Time, Absolute     1..400      сотни микросекунд, как велит V4L2
    Focus, Absolute             0..1023     ход линзы DW9714
    Focus, Automatic Continuous 0..1

Сборка: `kernel-work/v4l2loopback/build.sh` (KOUT должен указывать на
прошитую сборку ядра — иначе `Invalid module format`). Подмена модуля на
живой системе прошла без перезагрузки: `rmmod v4l2loopback` безопасен, запрет
из памяти касается только модулей связности MediaTek.

**Демон** читает ручки через `VIDIOC_G_CTRL` на своём же описателе раз в
0,3 с и реагирует **только на изменения** — иначе они затирали бы настройки,
сделанные через файл команд. Пересчёт выдержки: V4L2 задаёт её в сотнях
микросекунд, сенсор — в своих единицах; единица равна 5,7 мкс (кадр в 3260
единиц идёт 1/54 с). Проверено: `EXPOSURE_ABSOLUTE = 50` (5 мс) легло в
регистр как **880** — ровно по расчёту.

Ручной баланс отсчитывается не от чистой формулы по температуре, а от того,
к чему сошёлся самостоятельный баланс: у матрицы свои чувствительности по
каналам, и формула без них промахнулась бы сильно. Температура даёт наклон.

Проверка вживую (`/tmp/setc` через `VIDIOC_S_CTRL`):

    FOCUS_AUTO=0, FOCUS_ABSOLUTE=120  -> «ручка наводки -> 120», линза поехала
    EXPOSURE_AUTO=вручную, =50, GAIN=200 -> «выдержка -> 880, усиление 0xc8»
    обратно в авто                    -> «выдержка снова сама», «наводка снова сама»

guvcview показывает эти ручки ползунками, и «White Balance Temperature»
гаснет, пока включён самостоятельный баланс — как и положено. Снимок:
`docs/bf7-native/shots/camera-v4l2-controls-guvcview.png`.

## 2026-09-18 01:31 — выдержка это настоящий свет; и как я повесил сенсор

**Замер шума, теперь честный.** В демон добавлен временной шум: одна и та же
точка (диагональ из 256 сырых зелёных) в двух соседних кадрах. При постоянном
усилении 0x20 и длине кадра 9780:

    выдержка 1200: сырой зелёный 25,8  шум 0,98  сигнал/шум 26,3
    выдержка 2400: сырой зелёный 52,2  шум 1,33  сигнал/шум 39,2

Сигнал вырос ровно вдвое (2,02), шум — в 1,36 раза. Это √2, то есть дробовой
шум фотонов. **Выдержка накапливает настоящий свет, а не домножает готовое
число.** Значит порядок лестницы «выдержка → усиление → длина кадра» верен:
лишний свет всегда лучше лишнего усиления.

Прошлый замер этого не показал, потому что я снимал его на ровной серой
заливке, кадр упирался в потолок, а у насыщенных точек шума нет — выходило
«шум упал впятеро при той же яркости», чего не бывает. Мерить шум можно
только там, где сигнал не прижат ни к нулю, ни к потолку.

**Ловушка, которую я нашёл собой.** Запись 40000 в регистр длины кадра
(0x0340) **вешает сенсор намертво**: поток идёт, кадры приходят со счётчиком,
но все чёрные — сырые R 0,1 G 0,0 B 0,1. Не помогает ничего: ни возврат
прежней длины, ни перезапуск `bf7-camera.service` с переинициализацией
вендорской библиотекой. Спасает только перезагрузка телефона — то есть снятие
питания с сенсора. Команда `длина` теперь зажата в пределы режима
(3260..9780).

Признак в журнале демона, по которому это видно сразу: растёт счётчик
**пустых** кадров (`пустых 1193`) при живом темпе. Если пустые кадры пошли —
сенсор в ступоре, перезагружать телефон, не тратя время на перезапуск служб.

## 2026-09-18 08:01 — 15 кадров в guvcview: это не камера, это отрисовка

Хозяин: «guvcview пишет 15 кадров», и «часть кадра как будто зависает, а
потом медленно сползает вниз». Разобрал по частям.

**Сам поток идёт 30 к/с.** Три независимых замера при работающем guvcview:
обычный читатель (`vread`) — медиана 33,4 мс, ни одного кадра дольше 45 мс;
`ffmpeg -f null` без показа — 295 кадров за 10 секунд ровно.

**Узкое место — показ на самом телефоне.** guvcview занимает 105% одного
ядра и рисует 15,6 к/с. Более лёгкий `ffplay` не лучше: 188% процессора и
**312 брошенных кадров** за 20 секунд. То есть нарисовать 1024x768 тридцать
раз в секунду телефон не успевает — при том, что этот же телефон эти кадры
одновременно и готовит (демон занимает 1,5–1,8 ядра из четырёх).

**Сползающая полоса — нехватка буферов у v4l2loopback.** Их было четыре. Наш
писатель обходит кольцо за 133 мс, а медленный читатель держит свой буфер
дольше — и получает буфер, который мы уже переписываем: верх кадра новый, низ
старый, граница ползёт. Поставлено 12 (`max_buffers=12` в
`/etc/modprobe.d/bf7-camera.conf`). После этого доля кадров дольше 45 мс у
читателя упала с 6,9% до нуля.

**Заодно найдена настоящая гонка в перевзводе.** Перевзвод включал выгрузку,
пока `SV_IMGO_BASE` ещё указывал на тот буфер, который мы сейчас будем
разбирать, и только потом переводил стрелку на свободный. В это окно железо
успевало начать кадр поверх разбираемого. Теперь стрелка переводится **до**
взвода. На глаз это не проявлялось (замер «застывших строк» между соседними
кадрами ничего не нашёл), но ошибка настоящая.

**Что пробовал ради скорости и что это дало:**

    замеры (суммы, пересветы, резкость) не на каждом кадре   185% -> 179%
    сборка -O3 с векторизацией вместо -O2                    185% -> 179%
    без цветовой матрицы (для оценки, в дело не идёт)        185% -> 119%

То есть треть работы — цветовая матрица, а остальное размазано по пикселю
ровным слоем. Автовекторизации мешают выборки из таблицы гаммы и шаг по
мозаике Байера. Настоящее ускорение здесь — только NEON вручную; это
отдельная работа.

**Вывод, который надо помнить:** 30 к/с камера отдаёт всегда, и по сети или
для программы это полные 30. Но если смотреть поток **на экране самого
телефона**, четырёх слабых ядер на «готовить и рисовать одновременно» не
хватает — будет около 15. Ручка на этот случай есть:
`echo "кадры 15" > /run/bf7-camd` — тогда демон занимает вдвое меньше, и
показ идёт ровно 15 без брошенных кадров, что на глаз плавнее дёрганых 15 из
30.

## 2026-09-18 08:09 — первый дневной кадр вскрыл две ошибки сразу

Хозяин показал камере вид в окно — первый по-настоящему яркий сюжет за всё
время работы (до этого была только тёмная комната и монитор). Кадр вышел
выжженным в малиновое, и причин оказалось две.

**1. Автоматика не могла убавить выдержку.** В ветке «слишком светло» стояло

    else if (expo > flen_min / 2) expo = (unsigned)(expo * k);

то есть ниже 1630 единиц выдержка не опускалась. Усиление к тому моменту уже
лежало на минимуме (0x20), и убавлять было нечего — дневной свет сжигал кадр
целиком. Порог заменён на `expo > 8`. После правки автоматика сама дошла до
669 единиц (около 3,8 мс) и кадр стал нормальным.

Почему это не вылезло раньше: всю ночь снимали тёмную комнату, где выдержка
упиралась в верхний предел, а не в нижний. Нижней границы просто никто не
трогал.

**2. Света уходили в малиновое.** Зелёный упирается в потолок матрицы первым,
а красный с синим продолжают расти; после баланса белого (где зелёный ещё и
прижат множителем 0,42) пересвеченные стены и небо давали чистую фуксию.
Добавлено обесцвечивание светов: чем ближе точка к насыщению по любому из
сырых каналов, тем сильнее все три тянутся к наибольшему из них.

    rawmax > 880  ->  t = (rawmax - 880) / 79,  c += (max(r,g,b) - c) * t

Пересвет становится белым, переход плавный, цветной каймы по краю нет.
Стоимость — несколько действий только на ярких точках.

Снимок в публичную копию не вошёл: на нём вид из окна.

**Урок:** всю ночь я проверял камеру на одном типе сюжета и считал автоматику
проверенной. Первый же кадр другого рода нашёл ошибку, которая лежала прямо в
лестнице экспозиции. Проверять надо на краях диапазона, а не в середине.

## 2026-09-18 08:40 — режим фотографии и почему Wi-Fi медленный

**Фотография.** Хозяин попросил отдельный режим «мощной камеры» — не для
видео. Для видео мозаика Байера сворачивается вдвое (четыре точки матрицы
дают одну точку кадра), потому что надо успеть в такт. Для снимка спешить
некуда, и разбор идёт по-честному, билинейно: **2104x1560 вместо 1024x768,
вчетверо больше точек**.

Сделано внутри `bf7-camd`, а не отдельной программой: узел
`/dev/camera-isp` держит демон, второй процесс к нему не пустят.

  * команда `фото <путь> [сколько кадров]`, обвязка `bf7-photo`;
  * перед съёмкой свет перекладывается с усиления на выдержку (усиление в
    единицу, длина кадра до предела режима, выдержка добирает) — по замерам
    выдержка даёт настоящий свет, а усиление множит вместе с шумом;
  * несколько кадров складываются в 32-битный накопитель;
  * разбор — в отдельном потоке с `nice +10`, поток в `/dev/video0` при этом
    не прерывается (проверено: 30 к/с во время съёмки);
  * цветовая цепочка (баланс → обесцвечивание светов → матрица → гамма)
    **вынесена в общую функцию** `pixel_chain`, по ней идут оба пути. Две
    копии со временем разошлись бы, и снимок перестал бы совпадать по цвету
    с видоискателем;
  * путь, оканчивающийся на `.ppm`, отдаётся без сжатия — JPEG сам сглаживает
    зерно и прячет разницу между одним кадром и стопкой.

Снимок целиком занимает 2,9 с. 

**Сложение кадров: измерено, и меньше, чем обещает теория.** Считал на самом
телефоне (`kernel-work/probes/ppmnoise.c`), на дневной сцене:

    один кадр     зерно по кадру 1,545, в тенях 0,913
    восемь кадров зерно по кадру 1,457, в тенях 0,739

В тенях чище в 1,24 раза, а теория для восьми кадров обещает 2,83. Причина в
том, что на ярком свету дробового шума почти нет, а остаётся то, что
сложением не убирается: разброс чувствительности самих ячеек матрицы. Плюс
мой измеритель считает зерном и мелкую фактуру стола. Сложение должно
окупаться в темноте, при большом усилении — это ещё предстоит проверить.
Косвенно видно по размеру JPEG при одинаковом качестве: 426 КБ против 326.

**Почему Wi-Fi медленный.** Хозяин справедливо спросил: «у меня бывает 300
Мбит». Разложено на две причины, обе измерены передачей 60 МБ по ssh:

    с работающей камерой     115,8 с -> 0,52 МБ/с
    камера остановлена        36,0 с -> 1,67 МБ/с
    раньше, когда был 5 ГГц    0,9 с -> 5,3 МБ/с

1. Демон занимает два ядра из четырёх, и sshd остаётся без процессора —
   втрое медленнее. Шифрование ни при чём: AES-128-GCM на этом телефоне
   идёт 831 МБ/с (есть аппаратные команды ARMv8).
2. Телефон сидит на 2,4 ГГц (2427 МГц, скорость канала 39–78 Мбит).
   Прицельный поиск по всем каналам 5 ГГц показал: **точки доступа HOME-AP на
   5 ГГц из этого места не видно вообще** — видна только чужая сеть на 5745
   МГц. Радиомодуль 5 ГГц с VHT80 умеет (`iw phy` это подтверждает), просто
   не достаёт. Раньше телефон стоял ближе и был на 5240 МГц.

**Вывод для работы:** большие файлы по этому каналу не гонять. Замер зерна
перенесён на сам телефон — 20 МБ передачи превратились в две строчки вывода.

## 2026-09-18 08:43 — провод против Wi-Fi: 21 раз

Замер один и тот же — 60 МБ через ssh, одним заходом подряд:

    провод (RNDIS), камера работает    2,157 с -> 27,8 МБ/с (222 Мбит)
    провод, камера остановлена         2,152 с -> 27,9 МБ/с
    Wi-Fi, камера остановлена         46,656 с ->  1,29 МБ/с

**Провод быстрее в 21 раз.** 27,8 МБ/с — это почти потолок USB 2.0
(практический предел около 35 МБ/с, остальное съедает обвязка RNDIS).

**И вот что важнее числа: на проводе камера не мешает вовсе** (2,157 против
2,152 — разница в шуме измерения), а на Wi-Fi она же отнимала втрое. Значит
дело не в шифровании и не в самом sshd: обработка пакетов в драйвере Wi-Fi
стоит несравнимо дороже, чем в RNDIS, и именно она дерётся за процессор с
разбором кадров. Через провод тот же поток байт проходит почти даром.

**Правило на будущее:** пока провод воткнут — работать через него
(192.168.7.2). Wi-Fi (192.168.27.140) нужен ровно тогда, когда телефон надо
носить в руках.

## 2026-09-18 08:46 — сложение кадров: где потолок и почему он есть

Хозяин выключил свет и посветил фонариком — то, чего не хватало для честной
проверки. Условия: выдержка 9768 (предел режима, около 56 мс), усиление 0x5f.
Снято по одному сюжету с разным числом складываемых кадров, зерно считалось
на самом телефоне (`kernel-work/probes/ppmnoise.c`), по тёмным местам:

    кадров   измерено   по модели   выигрыш
    1        0,984      0,984       1,00x
    2        0,837      0,838       1,18x
    4        0,760      0,755       1,29x
    8        0,714      0,721       1,38x
    16       0,686      0,686       1,43x

Теория для шестнадцати кадров обещает 4,0 раза, а вышло 1,43. Причина видна,
если разложить шум на две части: **постоянную** (одинаковую во всех кадрах) и
**случайную**. Модель `дисперсия = F + R/N` ложится на замеры с точностью до
третьего знака:

    постоянная часть          0,662
    случайная (на одном кадре) 0,728

То есть сложением убирается только половина шума — вторая половина это
разброс чувствительности самих ячеек матрицы (он в каждом кадре один и тот
же) плюс мелкая фактура сюжета, которую мой измеритель тоже считает зерном.
Убрать её можно было бы калибровкой по тёмному и плоскому полю, но это
отдельная работа и своё хранилище калибровок.

**Практический вывод:** четыре кадра берут бо́льшую часть выигрыша (1,29 из
предельных 1,49), восемь — почти всё. Дальше складывать бессмысленно: разница
между 8 и 16 кадрами — три сотых, а время съёмки удваивается. По умолчанию
оставляю четыре.

Для сравнения, тот же замер на дневном свету давал всего 1,24 раза при восьми
кадрах — там дробового шума почти нет, и складывать почти нечего.

## 2026-09-18 08:55 — серия сырых кадров: правильный способ мерить сложение

Хозяин остановил меня на полпути и предложил лучшее: не складывать на
телефоне, а снять **одну серию из 16 кадров**, отдать её целиком на ПК и уже
там перебрать 2, 4, 6, 8, 16. Мысль верная, и разница в результате оказалась
больше, чем я ожидал.

Прежний способ сравнивал **разные съёмки**: между «четырьмя кадрами» и
«шестнадцатью» успевала измениться и сцена, и настройки автоматики, а сверху
ложился порог JPEG. Теперь все варианты берутся из одного и того же набора
кадров, и мерить есть что.

Сделано:

  * команда `серия <каталог> <N>` в демоне: кадры не складываются, а
    копируются целиком в память (`memcpy` 6,5 МБ на кадр, поток не рвётся) и
    потом пишутся по одному как `NN.raw` плюс `опись.txt` с условиями
    съёмки — размер, чёрное, порядок мозаики, выдержка, усиление, баланс,
    матрица;
  * обвязка `bf7-burst`, кнопки на экране телефона (`bf7-camera-gui.py`),
    отклик вибро: один импульс — начали, короткий — кадры сняты и можно
    двигаться, два коротких — готово;
  * разбор на ПК `hybris/bf7-camera/разбор-серии.py` — та же цветовая
    цепочка, что в телефоне, чтобы числа были сравнимы.

Проба на 16 кадрах (выдержка 9768, усиление 0xb3):

    кадров   зерно в тенях
    1        0,602
    2        0,455
    4        0,363
    6        0,328
    8        0,308
    12       0,288
    16       0,276

    постоянная часть 0,239, случайная (на кадре) 0,553
    выигрыш от 1 к 16 кадрам: 2,18 раза

Модель ложится точно: для 4 кадров считает 0,365 при измеренных 0,363, для
8 — 0,309 при 0,308.

**Сравните с прежним замером** (отдельные съёмки, JPEG): там выходило
«постоянная 0,662, случайная 0,728, выигрыш 1,43». То есть прежний способ
завышал постоянную часть почти втрое и занижал выигрыш вдвое — всё это была
не матрица, а разница между съёмками и сжатие. **Урок: сравнивать варианты
обработки можно только на одних и тех же исходных данных.**

Заодно починено: разбор команды читал путь только до 31 байта, и
`/root/фото/...` с кириллицей молча обрезался — снимок уходил не туда.

## 2026-09-18 09:03 — разбор серий из темноты и разгадка «выдержки длиннее кадра»

Хозяин снял две серии по 16 кадров в темноте и предупредил: «супер ровно не
получилось».

**Дрожание не помешало.** Смещение между первым и последним кадром серии,
измеренное взаимной корреляцией по Фурье, — **ноль точек**. Шестнадцать
кадров снимаются за полсекунды, за это время рука не успевает уйти.

**Сложение работает и в темноте.** Обе серии дали одно и то же, что само по
себе хороший знак:

    серия 1: 1 кадр 2,453 -> 16 кадров 1,116   выигрыш 2,20 раза
    серия 2: 1 кадр 2,613 -> 16 кадров 1,169   выигрыш 2,24 раза

**Но сцена была за пределом камеры.** Сырой уровень: среднее 0,7 из 959, то
есть **сцена занимала 0,1% разрядности матрицы**, а медиана вообще ноль —
больше половины ячеек не получили ни одного фотона. И это при выдержке на
пределе и усилении 0x300 (24 раза). Вытянув кадр в 12 раз, видно фактуру и
разницу: зерно 24,0 на одном кадре против 9,5 на шестнадцати.

**Разгадка старой загадки.** Всю ночь я не мог объяснить, как выдержка
длиннее кадра вообще отрабатывает при неизменных 54 к/с, и честно писал «не
выяснено». Теперь измерено. Две серии по 8 кадров одной сцены, одинаковое
усиление, **одни и те же точки кадра** (прошлый замер я испортил тем, что
брал разные наборы точек):

    выдержка 9768  (в кадр)        сигнал 154,2  шум 4,15  сигнал/шум 37,2
    выдержка 19548 (вдвое длиннее) сигнал 313,6  шум 5,60  сигнал/шум 56,0

    сигнал x2,03, шум x1,35, сигнал/шум x1,50
    настоящий свет дал бы          x2,00      x1,41        x1,41
    домножение внутри сенсора      x2,00      x2,00        x1,00

Совпадение с дробовым шумом почти точное. **Это настоящий свет.** Объяснение
простое, и его стоило сообразить раньше: у сенсора с бегущим затвором
накопление вполне может быть длиннее кадра — в каждый момент в матрице идёт
несколько волн накопления сразу, и частота выдачи от этого не меняется.

Временной шум по стопке одинаковых кадров — лучший измеритель из всех, что я
пробовал за ночь: в нём нет ни деталей сюжета, ни порога JPEG, ни разницы
между съёмками. Раньше надо было.

**Следствие:** потолок длины кадра поднят с 9780 до **19560** — это лишняя
ступень света в темноте даром, частота кадров не страдает. Выше не ходить:
40000 вешает сенсор намертво. Проверено выдержкой две минуты: 30,0 к/с, ни
одного срыва захвата.

## 2026-09-18 09:14 — тёмный кадр: работает, но только для своих условий

Хозяин снял серию с наглухо закрытой камерой. Разбор дал больше, чем я
рассчитывал.

**Что в тёмном кадре.** 16 кадров, выдержка 12856, усиление 0xbd:

    уровень 63,63 (чёрное по описи считалось 64 — на самом деле 63,63)
    постоянный рисунок      0,851
    временной шум (считывание) 0,649
    рисунок по столбцам 0,073, по строкам 0,088 — полос почти нет
    горячие точки: 2268 из 3,3 млн (0,069%), самая яркая 474 из 959

**Сколько даёт вычитание.** Проверено честно — калибровка по первой половине
кадров, замер на второй, которая в ней не участвовала. И отдельно на
независимой серии, снятой позже при тех же настройках:

    один кадр   1,174 -> 0,765   в 1,54 раза чище
    сложено 8   0,962 -> 0,369   в 2,60 раза
    самая горячая точка 479 над фоном -> 45

Главное здесь вот что: **сложение само упиралось в 0,96 и дальше не шло**,
сколько кадров ни складывай — это и была та «постоянная часть», которую я
намерил ещё вчера. Вместе с вычитанием потолок снялся, и восемь кадров дошли
до 0,37.

**Чего тёмный кадр НЕ умеет.** Две калибровки, снятые при усилении 189 и 43,
дают рисунки со схожестью всего **0,44** — то есть наполовину разные. Ни один
простой закон пересчёта не подходит: измеренный множитель между ними 0,47,
тогда как пересчёт по усилению обещал 4,40, по выдержке 0,66, по их
произведению 2,89. Значит калибровку нельзя растянуть на другие условия, и
окно применения сделано узким (0,7…1,4 по произведению выдержки на усиление).
Снимать её надо для тех условий, в которых собираешься снимать.

**Сделано в демоне:**

  * команда `темнота [N]` и обвязка `bf7-dark`: снимает калибровку при
    длине кадра и выдержке на пределе и усилении в единицу — там же, куда
    приходит съёмка фотографии в темноте;
  * калибровка хранится в `/var/lib/bf7-camd-тёмный.raw` и подхватывается
    при запуске;
  * при съёмке фотографии вычитается, если условия подходят (в журнале
    видно множитель);
  * в сырую серию не вмешиваемся — она на то и сырая, — но кладём тёмный
    кадр рядом файлом `тёмный.raw`, пусть разбирающий решает сам.

Попутно исправлено: сравнивать надо с настройками, при которых кадры **сняты**,
а не с теми, что были до съёмки — путь фотографии сам перекладывает свет с
усиления на выдержку, и калибровка не подходила никогда.

## 2026-09-18 09:19 — две серии одной сцены: лучший измеритель и моя ошибка в калибровке

Хозяин снял две серии по 16 кадров одной и той же тёмной сцены. Две серии
одного сюжета — это подарок: **в их разности сцена исчезает целиком**, и
остаётся чистый случайный шум, без примеси деталей, без порога сжатия, без
догадок. Лучший измеритель из всех, что были за сутки.

    сложено  1: случайный шум 1,554
    сложено  2: 1,161
    сложено  4: 0,902
    сложено  8: 0,728
    сложено 16: 0,625      выигрыш 2,49 раза

А вот в самой сложенной стопке: всего 2,585, из них случайного 0,273,
**постоянного 2,571**. Сцена при этом имеет уровень 0,92 из 959 — то есть
собственный рисунок матрицы втрое сильнее самого изображения. Видно и глазом:
 — сложение вытащило из шума
узнаваемую картинку, но цветная крошка осталась.

**Попутно поймал себя на вранье измерителя.** Первый замер по этим сериям
показал «сложение даёт всего 1,23 раза», и это была ошибка метода: я считал
перепад между **соседними** точками матрицы, а у них разные светофильтры.
Считать надо по одноцветной сетке. После поправки — 2,49.

**И ошибка в устройстве калибровки, моя.** Я заставил команду `темнота`
сниматься при усилении в единицу. А съёмка фотографии в темноте упирается в
предел выдержки и оставляет усиление большим — то есть калибровка снималась
при одних условиях, а применяться должна была к другим, где рисунок совсем
иной (схожесть 0,44). Подбор множителя по самим данным это подтвердил: лучший
множитель 1,5 при том, что пересчёт по усилению требовал бы 17,9.

Исправлено: `темнота` теперь делает тот же перенос света с усиления на
выдержку, что и съёмка фотографии. Значит калибровка, снятая в той же сцене с
закрытым объективом, попадёт ровно в те условия, в которых снимается фото.

## 2026-09-18 09:29 — круг замкнулся: сложение плюс тёмный кадр дают вчетверо

Хозяин сказал «съёмка тёмного кадра не удалась». Она удалась — не удалось её
**обнаружение**: служба `bf7-camd` пишет вывод в `/tmp/camera.log`
(`StandardOutput=append:`), а обвязка `bf7-dark` ждала появления строки в
journalctl и через минуту объявляла неудачу. Калибровка при этом уже лежала на
месте. Исправлено: ждём изменения времени самого файла калибровки, а не
строчки в журнале. **Урок: обвязка не должна зависеть от того, куда служба
складывает вывод.**

Зато калибровка снялась правильно — при выдержке 19548 и усилении 0x300,
ровно в тех условиях, что и сами серии. Именно это я чинил прошлой правкой.

Итог по трём сериям одной тёмной сцены (уровень 0,87 из 959, разброс тёмного
кадра 2,471 — то есть собственный рисунок матрицы сильнее самой сцены):

    сложено  1, без вычета   2,983
    сложено  1, с вычетом    1,576    в 1,89 раза
    сложено 16, без вычета   2,621
    сложено 16, с вычетом    0,733    в 3,58 раза

    от «один кадр как есть» до «16 сложено и вычтено»: в 4,07 раза

Обратите внимание на среднюю пару: **одно сложение дало всего 1,14 раза**
(2,983 -> 2,621), потому что упиралось в рисунок матрицы. И **одно вычитание
на одном кадре — 1,89**. Вместе — 4,07. Ни то ни другое по отдельности не
работает: сложение убирает случайную часть, вычитание постоянную, и обе надо
убирать.

Видно и глазом: `docs/bf7-native/shots/camera-dark-stacking-and-darkframe.png`.

## 2026-09-18 09:33 — калибровка в светлой сцене: повторяемость 0,984

Хозяин снял калибровку и серию в сцене посветлее. Два вывода.

**Рисунок матрицы повторяется.** Две калибровки, снятые с разницей в шесть
минут при одних и тех же условиях (19548 / 0x300), похожи на **0,984**. Для
сравнения, при разном усилении (189 и 43) было 0,44. То есть дело не в
случайности: рисунок устойчив, и вычитать его осмысленно — просто калибровка
обязана быть снята при тех же настройках.

Попутно: с закрытым объективом автоматика всегда уходит на предел выдержки и
усиления, в светлой комнате или в тёмной — поэтому калибровка выходит одна и
та же. Это удобно, но означает, что для съёмки **при свете** (где автоматика
выберет короткую выдержку и малое усиление) нужна своя калибровка, а снять её
«просто закрыв объектив» не получится: надо задать выдержку и усиление руками
перед съёмкой тёмного кадра.

**В светлой сцене вычитание помогает меньше, и это правильно.** Уровень сцены
114 из 959 против 0,87 в прошлый раз — в 130 раз ярче:

    сложено  1, без вычета  4,896
    сложено  1, с вычетом   4,277   в 1,14 раза
    сложено 16, без вычета  3,042
    сложено 16, с вычетом   1,842   в 1,65 раза

Против 1,89 и 3,58 на тёмной сцене. Причина простая: чем больше света, тем
сильнее дробовой шум самого света, и тем меньше доля постоянного рисунка в
общем шуме. Вычитание тёмного кадра — лекарство для темноты.

**Но горячие точки убираются всегда**, и это заметнее любых чисел:
`docs/bf7-native/shots/camera-darkframe-hotpixels.png` — на средней панели
(сложено 16) десятки цветных точек, на правой (с вычетом) их нет.

**Добавлена проверка.** В журнале видно, что первая попытка калибровки дала
уровень 68,24 вместо 63,7 — под крышку попал свет, и в калибровку записался
слабый снимок вместо рисунка матрицы. Теперь демон об этом предупреждает.

## 2026-09-18 09:36 — короткое нажатие кнопки питания выключало телефон

Хозяин машинально нажал кнопку питания, и телефон «перезагрузился». Разбор:

  * посмертный журнал (`/sys/fs/pstore`) **пуст** — паники не было;
  * в конце прошлой загрузки только сообщения о зарядке: USB ушёл в сон
    (`MUSB SUSPEND`), потом `online:0`, напряжение 2441 мВ — питание пропало;
  * батарея при этом жива: 93%, 4,284 В, заряжается. То есть не разряд.

Виновник — **systemd-logind**. Он держит `/dev/input/event1` (`mtk-pmic-keys`,
кнопка питания), а `HandlePowerKey` по умолчанию `poweroff`. Телефон корректно
выключился; журнал при этом не успел дописаться, поэтому строк о выключении и
не осталось. А раз воткнут провод — MediaTek при наличии VBUS включается сразу
обратно. Со стороны это неотличимо от самопроизвольной перезагрузки.

Поправлено: `/etc/systemd/logind.conf.d/bf7-power-key.conf` (в дереве —
`hybris/bf7-gpu/units/bf7-power-key.conf`):

    HandlePowerKey=ignore
    HandlePowerKeyLongPress=poweroff

Короткое нажатие теперь не делает ничего, длинное осталось как способ
выключить нарочно. Проверено по `busctl`, службы целы, поток 30 к/с.

**Общий урок:** настройки systemd, разумные для настольной машины, на телефоне
могут быть опасны. Кнопка питания у телефона под пальцем, и «выключить по
короткому нажатию» — не то поведение, которое здесь нужно.

## 2026-09-18 09:41 — кнопка питания гасит экран

Сна у этого порта нет, поэтому «выключить экран» здесь означает ровно одно:
погасить подсветку панели (`/sys/class/leds/lcd-backlight/brightness`).
Кнопку теперь обрабатывает сам композитор — фильтр событий в
`hybris/bf7-gpu/gui/main.cpp`, класс `Подсветка`.

Что важно понимать про такое «выключение»:

  * **тачскрин продолжает работать.** Гаснет только подсветка; контроллер
    TD4160 остаётся под питанием, прошивка загружена, прерывания идут. Ничего
    не усыпляется;
  * **но ввод до приложений не доходит.** Пока экран погашен, композитор
    съедает нажатия и касания: иначе первое же касание в кармане ушло бы
    вслепую в то, что окажется под пальцем. Любое нажатие или касание в этом
    состоянии просто зажигает экран обратно;
  * значит разбудить можно и кнопкой, и касанием.

Проверено подделкой нажатий через uinput (`kernel-work/probes/pwrkey.c`):

    подсветка 140 -> кнопка питания -> 0 -> посторонняя клавиша -> 140

и в журнале сеанса «экран погашен, яркость запомнена: 140» / «экран зажжён».

**Чего это стоит:** контроллер тача продолжает потреблять ток. Настоящий
телефон усыпил бы и его (у `synaptics_tcm` для этого есть `irq_en`), но тогда
будить можно было бы только кнопкой. Пока оставлено так — решать хозяину.

## 2026-09-18 09:46 — усыпление тачскрина и сколько это даёт

По просьбе хозяина при гашении экрана теперь усыпляется и тачскрин: композитор
пишет 0 в `irq_en` драйвера `synaptics_tcm`. Разбудить после этого можно
только кнопкой — касание уже не проходит, и это как раз то, что нужно в
кармане.

**Подстраховка обязательна.** При запуске композитор безусловно пишет в
`irq_en` единицу. Иначе, упади он с погашенным экраном, тач остался бы мёртвым
до перезагрузки — и это выглядело бы как поломка сенсора, а не как последствие
падения.

**Сколько это стоит по току.** Телефон на зарядке, поэтому мерится ток,
уходящий **в батарею**: чем больше, тем меньше потребляет сам аппарат.
Усреднение по десяти отсчётам:

    экран горит, тач слушает    92,0 мА
    экран горит, тач спит      110,5 мА
    экран погашен, тач спит    267,0 мА

Отсюда:

  * **тачскрин сам по себе — около 18,5 мА.** Немного, но на батарее около
    3000 мА·ч это примерно 0,6% в час, то есть в режиме «лежит в кармане»
    разница за ночь заметная;
  * **подсветка — около 156,5 мА** при яркости 140;
  * всего гашение экономит **175 мА**.

Проверено подделкой нажатий через uinput; в журнале сеанса «экран погашен и
тачскрин усыплён» / «экран зажжён».

## 2026-09-21 22:24 — Проба wlroots (phoc) на нашей панели: KMS проходит, упирается в CMA и в узел рендера

Вопрос был: можно ли на нашем ядре поднять «телефонные» оболочки (phosh/phoc,
Plasma Mobile, Sxmo) — то есть может ли композитор, написанный без знания про
наше железо, вести наш дисплей. Поставил `phoc 0.24.0-1` из bookworm (притащил
`phosh 0.24.0-1`, `gnome-settings-daemon`, `feedbackd`), `grim`, `wlr-randr`.

Скрипт пробы: `/root/phoc-proba.sh` на телефоне (режимы `pixman` и `gpu`).
Он останавливает `bf7-session`, снимает состояние KMS до и во время, пробует
снимок через `grim`, затем возвращает сеанс.

### Что получилось: KMS наш драйвер отдаёт правильно

Состояние `/sys/kernel/debug/dri/1/state` до пуска и при работе phoc:

    до:   plane-0 fb=0    crtc-0 active=0   DSI-1
    при:  plane-0 fb=33   crtc-0 active=1   DSI-1

phoc сам нашёл выход (`Output 'DSI-1' added, 62mm x 139mm`), сделал настоящую
атомарную настройку режима и привесил буфер. То есть `bf7_drm.c` с его
`DRIVER_GEM | DRIVER_MODESET | DRIVER_PRIME | DRIVER_ATOMIC` достаточно
соответствует, чтобы его вёл чужой композитор. Это главный результат.

### Отказ 1 (pixman): пул CMA исчерпан

    [render/allocator/drm_dumb.c:70] Failed to create DRM dumb buffer: Cannot allocate memory
    [render/swapchain.c:109] Failed to allocate buffer
    [types/wlr_screencopy_v1.c:541] Failed to capture output: no read format supported by renderer

Программный рендерер wlroots берёт буферы обмена через `DRM_IOCTL_MODE_CREATE_DUMB`,
а наш `bf7-disp` раздаёт их из CMA:

    CmaTotal: 16384 kB   CmaFree: 0 kB
    кадр 720x1612x4 = 4 642 560 байт, нужно 3 таких = ~13,6 МБ

Остановка `bf7-camd` вернула 10 304 кБ — то есть камера держит ~10 МБ CMA под
буферы CAMSV. Даже так на три буфера не хватает. Ещё ~6 МБ держит
`android.hardware.graphics.allocator@4.0-service-mediatek` (он же единственный,
кто открыл `/dev/ion` и `renderD128`).

Источник ограничения — `CONFIG_CMA_SIZE_MBYTES=16` (значение по умолчанию);
в `/proc/cmdline` параметра `cma=` нет. Лечится либо `cma=128M` в командной
строке загрузки, либо пересборкой ядра.

### Отказ 2 (gles2): у card1 нет узла рендера, и libgbm отдаёт его Mesa

    [render/egl.c:346] Failed to create EGL display
    [render/gles2/renderer.c:698] Could not initialize EGL

Причина не в расширениях. Отдельной пробой (`/tmp/eglgbm.c`) проверено, что на
`renderD128` весь путь работает:

    gbm_create_device(renderD128) → ОК, backend: pvr
    eglGetPlatformDisplayEXT(EGL_PLATFORM_GBM_KHR) → ОК
    eglInitialize → ОК 1.5, поставщик Imagination Technologies

Из 41 расширения дисплея присутствуют **все обязательные для wlroots**:
`EGL_KHR_no_config_context`, `EGL_KHR_surfaceless_context`, `EGL_KHR_image_base`,
`EGL_EXT_image_dma_buf_import`, `EGL_EXT_image_dma_buf_import_modifiers`,
`EGL_EXT_buffer_age`, `EGL_IMG_context_priority`, `EGL_KHR_fence_sync`,
`EGL_ANDROID_native_fence_sync`, `EGL_KHR_wait_sync`. Нет только
`EGL_*_swap_buffers_with_damage` (для wlroots необязательно) и
`EGL_MESA_configless_context` (заменяется первым из списка).

glvnd уже настроен на нас: `/usr/share/glvnd/egl_vendor.d/10_libhybris.json` →
`libEGL_libhybris.so.0`.

Ломается всё на дисплейном узле. У `card1` **нет узла рендера** — рядом только
`controlD65`. Поэтому:

    gbm_create_device(card1) → ОК, но backend: drm (мешовский), и следом
    MESA-LOADER: failed to open bf7disp: /usr/lib/dri/bf7disp_dri.so ... нет файла

`GBM_BACKEND=pvr` не помогает — наш `pvr_gbm.so` сам отказывается от `card1`,
он привязан к драйверу PowerVR. А если оставить только нашего поставщика EGL
(`__EGL_VENDOR_LIBRARY_FILENAMES=...10_libhybris.json`), то на мешовском
`gbm_device` отказывает уже `eglGetPlatformDisplayEXT`.

### Вывод

Рендер обязан идти через `renderD128`, показ — через `card1`, то есть нужен
раздельный режим (`WLR_RENDER_DRM_DEVICE`, wlroots его понимает — строка есть в
`libwlroots.so.10`). Но и в раздельном режиме буферы показа на `card1`
выделяются «тупыми», из того же CMA. **Расширение CMA нужно на обоих путях** и
поэтому делается первым.

Попутно: `QT_QPA_PLATFORM=eglfs` — наш композитор гонит кадр андроидным путём
(hybris/gralloc/HWC), `card1` он вообще не открывает. Отсюда и то, что при
работающем сеансе `crtc-0 active=0`: KMS-драйвер сделан для показа без
копирования и в обычной жизни простаивает.

## 2026-09-21 22:36 — Phosh работает на панели с ускорением на PowerVR

Доведено до конца за три шага. Все три отказа из записи 22:24 сняты.

### Шаг 1. Что на самом деле мешало GPU-пути

Запуск с `G_MESSAGES_DEBUG=all` показал, что `WLR_RENDER_DRM_DEVICE` wlroots
**понимает** и узел открывает правильный:

    [render/wlr_renderer.c:371] Opening DRM render node '/dev/dri/renderD128' from WLR_RENDER_DRM_DEVICE
    [render/egl.c:437] Using EGL device /dev/dri/renderD128
    [render/egl.c:346] Failed to create EGL display

Дело в порядке предпочтений. wlroots 0.15.1 сперва пробует платформу
`EGL_PLATFORM_DEVICE_EXT` и откатывается на GBM только если устройств нет.
Перечисляет устройства мешовский поставщик glvnd, а создать на них дисплей
умеет только он же — наш hybris-EGL работает исключительно через GBM. Отката
после неудачи в этой ветке нет.

Убрать Mesa из списка поставщиков нельзя: вместе с ней пропадает
`EGL_EXT_platform_base`, без которого wlroots отказывается работать вовсе
(`[render/egl.c:183] EGL_EXT_platform_base not supported`).

### Шаг 2. Прослойка `libbf7eglfilter.so`

Исходник — `hybris/bf7-gpu/eglfilter.c`, собирается на телефоне:

    gcc -shared -fPIC -O2 -o /usr/local/lib/libbf7eglfilter.so eglfilter.c -ldl -ldrm

Делает три вещи.

1. **Прячет устройственные расширения EGL.** Из строки, которую отдаёт
   `eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS)`, вычёркиваются
   `EGL_EXT_device_base`, `EGL_EXT_device_enumeration`, `EGL_EXT_device_query`,
   `EGL_EXT_platform_device`. Mesa остаётся на месте, `EGL_EXT_platform_base`
   сохраняется, а wlroots перестаёт лезть в устройственную ветку и печатает
   `Using EGL_PLATFORM_GBM_KHR`.

2. **Дописывает `GL_EXT_unpack_subimage` в `glGetString(GL_EXTENSIONS)`.**
   Рендерер wlroots его требует, драйвер PowerVR не объявляет — потому что в
   OpenGL ES 3.0 оно вошло в ядро стандарта и перестало быть расширением.
   Мы получаем ES 3.2, то есть `GL_UNPACK_ROW_LENGTH` работает по-настоящему;
   недоставало только имени в строке. Та же болезнь ломала glamor.

3. **Отдаёт узел рендера для bf7-disp.** `drmGetRenderDeviceNameFromFd()` на
   нашем `card1` возвращает NULL (узла рендера у него нет, рядом только
   `controlD65`), и распределитель wlroots переоткрывает сам `card1`.
   Подставляем `/dev/dri/renderD128` — но **только** если `drmGetVersion()`
   даёт имя `bf7disp`, чужие устройства не трогаем.

После первых двух правок появилось:

    Creating GLES2 renderer
    Using OpenGL ES 3.2 build 1.15@6070602
    GL vendor: Imagination Technologies
    GL renderer: PowerVR Rogue GE8300

Третья правка себя пока не оправдала: распределитель ушёл на `renderD128`, но
там упёрся в `[render/allocator/gbm.c:183] PRIME export not supported` —
`pvrsrvkm` не объявляет `DRM_CAP_PRIME` на экспорт, буферы gralloc отдаются
мимо него. wlroots откатился на «тупые» буферы `card1`. Правку оставил: она
безвредна и размечает дорогу к показу без копирования, когда дойдут руки до
экспорта dma-buf из gralloc.

### Шаг 3. CMA 16 → 128 МБ

Оставался единственный упор — буферы обмена:

    Allocated 720x1612 DRM dumb buffer          ← первый прошёл
    Failed to create DRM dumb buffer: Cannot allocate memory   ← второй нет

Узла `linux,cma-default` в дереве устройств нет, `early_param("cma")` в
`kernel/dma/contiguous.c:70` на месте — значит параметр загрузки работает.

Ядро **не пересобиралось**. Снял текущий образ прямо из слота
(`/dev/disk/by-partlabel/boot_a` → `mmcblk0p23`), распаковал `unpack_bootimg`,
собрал заново с тем же ядром, ramdisk и dtb и единственной добавкой `cma=128M`
в командной строке. Обратная проверка: `kernel`, `ramdisk`, `dtb` совпадают
побайтно с исходными. Прошил `boot_a`, перезагрузка заняла ~64 с.

    CmaTotal: 131072 kB   CmaFree: 125000 kB
    cma=128M в /proc/cmdline

Строку добавил и в `native-linux-build/alps191-rootfs-v1/build.sh`, чтобы
будущие образы её несли.

### Итог

`phoc -S -C /usr/share/phosh/phoc.ini -E /usr/libexec/phosh` под
`dbus-run-session` поднимает **настоящую Phosh 0.24 на панели телефона**.
Снимок через `grim` (`/tmp/phosh.png`, 720x1612): часы, дата по-русски,
быстрые настройки — сотовая, Wi-Fi, Bluetooth, батарея 100% на зарядке
(значит `fuelgauged` она читает), поворот экрана, уведомления, ползунки
громкости и яркости, кнопки блокировки и питания, полоса «домой».

Попутно из журнала:

* `Output DPI is 294.566895 for mode 720x1612, using scale 2.000000` — phosh
  сама выбрала верный масштаб;
* `Mapping touch device mtk-tpd2 to DSI-1` — тач привязан без настройки;
* `Using atomic DRM interface`, `Modesetting with '720x1612@1mHz'` — наш
  `bf7_drm.c` чужой композитор ведёт штатно;
* `ADDFB2 modifiers unsupported` — поэтому нужен `WLR_DRM_NO_MODIFIERS=1`;
* `Cannot get renderer DRM FD, disabling wl_drm` и `Failed to init default
  linux-dmabuf feedback` — побочный след первой правки: спрятав
  устройственные расширения, мы лишили wlroots способа назвать свой узел
  клиентам. Композитор работает, но клиенты не получат dma-buf. Чинится
  отдельно, узким исключением для `wlr_renderer_get_drm_fd`.

Скрипты проб на телефоне: `/root/phoc-proba.sh` (режимы `pixman`/`gpu`) и
`/root/phosh-proba.sh`. Оба останавливают `bf7-session`, `bf7-camera`,
`bf7-camd` и возвращают их обратно.

**Осторожно:** имена переменных в bash могут быть только латиницей. Первая
версия скрипта пробы была написана с кириллическими именами, молча их
потеряла и пошла не по той ветке — отказ выглядел как чужой.

## 2026-09-21 22:54 — Экран блокировки Phosh глотает касания: виновата она, не мы

После запуска Phosh (запись 22:36) хозяин не смог ввести пароль: шторка
открывается, клавиатура появляется, а цифры не набираются. Разобрано до конца.

### Ложный след и как он возник

Первые записи касаний показали, что нажатия ложатся в y≈714…772 при клавише
«5» на y=860, и что за всю запись не было ни одной точки ниже y=781 — то есть
нижняя половина экрана будто недостижима. Вывод «сенсор отдаёт только верх»
оказался **неверным**: я сравнивал сырые события ядра с экранными
координатами, забыв, что правило `99-bf7-touch.rules` разворачивает устройство
на 180° матрицей `LIBINPUT_CALIBRATION_MATRIX="-1 0 1 0 -1 1"`, и libinput её
применяет (`libinput list-devices` → `Calibration: -1.00 0.00 1.00 ...`).

С пересчётом всё сходится идеально: сырой (360, 747) → экранный (359, 864) —
это клавиша «5»; сырой (356, 214) → экранный (363, 1397) — это кнопка Unlock.
Проверка по трём точкам («1», «9», «Unlock») дала ошибку не больше толщины
пальца.

**Правило на будущее: сырые события `/dev/input/eventN` сравнивать с экраном
только после матрицы калибровки.**

Диапазоны сенсора, кстати, точные: `ABS_MT_POSITION_X` 0..719,
`ABS_MT_POSITION_Y` 0..1611 — ровно панель, пересчёт один в один.

### Новые средства

* `kernel-work/probes/touchinj.c` — виртуальный тачскрин через uinput.
  Нажатия и свайпы в заданные точки, координаты сразу экранные (правило
  разворота к нему не относится — имя устройства другое). Обязателен
  `INPUT_PROP_DIRECT`, иначе libinput считает устройство тачпадом.
* `kernel-work/probes/keyinj.c` — виртуальная клавиатура через uinput.
  Читает строку со своего входа с погашенным отображением и набирает её.
* `kernel-work/probes/gtktouch.c` — голое окно GTK3 с кнопкой и печатью
  событий касания.

### Где на самом деле ломается

Запуск Phosh под `WAYLAND_DEBUG=1` и впрыск трёх нажатий в клавишу «1»:

    -> wl_touch@9.down(18, …, wl_surface@50, 0, 94.50, 344.00)   frame()
    -> wl_surface@50.attach(wl_buffer@54)  damage(54, 305, 80, 80)  commit()
    -> wl_touch@9.up(19, …)                                       frame()
    -> wl_touch@9.down(20, …, 94.50, 344.00)                      frame()
    -> wl_touch@9.up(21, …)                                       frame()
    -> wl_touch@9.down(22, …, 94.50, 344.00)                      frame()

Координаты (94.5, 344) — клавиша «1» в логических единицах при масштабе 2.
`wl_surface@50` — это `zwlr_layer_surface_v1` с именем **"phosh lockscreen"**.

Phosh получает все три касания, на `down` перерисовывает область 80×80 ровно
под клавишей (подсветка нажатия) — и на `up` не делает **ничего**. Кнопка
подсвечивается, щелчок не рождается.

Решающая проверка — голое окно GTK3 (`gtktouch`) под тем же phoc, тем же
wlroots, тем же нашим драйвером:

    нажатие 1 → ЩЕЛЧОК 1
    нажатие 2 → ЩЕЛЧОК 2
    нажатие 3 → ЩЕЛЧОК 3

**Три касания — три щелчка.** Значит ядро, KMS, libinput, wlroots, phoc, GDK и
GTK3 отрабатывают верно; беда только в экране блокировки Phosh.

Скорее всего дело в её обработчике жестов прокрутки (libhandy): он забирает
последовательность касаний себе и не отдаёт кнопке. Косвенное подтверждение —
предупреждение phoc

    phoc-cursor-WARNING: wlroots lacks wlr_seat_touch_send_wl_cancel support,
    can't cancel gesture

phoc 0.24 при сборке ищет имя `wlr_seat_touch_send_wl_cancel` (появилось в
wlroots 0.17), а в нашей wlroots 0.15.1 та же возможность называется
`wlr_seat_touch_send_cancel` — и она **есть**, `nm -D` её показывает. То есть
отменять касание библиотека умеет, phoc просто не зовёт. Это дефект
раскладки Debian, а не нехватка возможностей.

### Обходной путь

Экран блокировки объявляет `set_keyboard_interactivity(1)`, то есть
клавиатурный ввод берёт. Поэтому пароль набирается через `keyinj`:

    ssh -t root@192.168.7.2 /tmp/keyinj

Строка читается с погашенным отображением и не попадает ни в терминал, ни в
журнал, ни в список процессов.

### Настоящее лечение (не сделано)

Пересобрать phoc 0.24 из исходников Debian, заменив отсутствующее имя на
`wlr_seat_touch_send_cancel`. Либо проверить другую оболочку — Plasma Mobile
или Sxmo, у них другой клиентский стек.

## 2026-09-21 23:00 — Phosh разблокирована; граница: клиентам под phoc нет GPU

### Разблокировка

Обходной путь сработал: свайп впрыском (`touchinj свайп 360 1450 360 500`),
затем код через `keyinj` — виртуальную клавиатуру uinput. Экран блокировки
объявляет `set_keyboard_interactivity(1)`, поэтому клавиатуру берёт, а
касания его обработчик жестов глотает.

За разблокировкой — домашний экран Phosh: строка поиска, «Show All Apps»,
сетка приложений (btop++, Cheese, Firefox ESR, guvcview, Onboard, XTerm,
Веб-браузер, Настройки, Терминал, Фотоаппарат). **Касание впрыском по
«Show All Apps» открыло сетку** — то есть в разблокированной оболочке тач
работает. Сломан ровно экран блокировки.

X11-программа (XTerm) под встроенным Xwayland тоже поднялась и показала
приглашение.

### Граница: клиенты Wayland не получают GPU под phoc

Cheese и guvcview падают одинаково:

    Fatal: the server doesn't advertise the android_wlegl global!

Это libhybris. Наш EGL, когда им пользуется **клиент** Wayland, требует от
композитора протокол `android_wlegl` — передачу буферов gralloc. Наш
собственный `bf7gui` его объявляет (`hybris/bf7-gpu/gui/wlegl.cpp`), phoc —
разумеется, нет.

Поэтому под phoc расклад такой:

* **композитор** рисует на GPU (PowerVR Rogue GE8300, GLES 3.2) — доказано;
* **клиенты** рисуют программно через `wl_shm` — в протокольной отладке
  phosh видно `wl_shm_pool@9.create_buffer(..., 720, 1612, 2880, 1)`;
* **клиенты с GL** не запускаются вовсе.

Это тот же корень, что у `[render/wlr_renderer.c:257] Cannot get renderer DRM
FD, disabling wl_drm` и `Failed to init default linux-dmabuf feedback`.

Увести клиента на программный EGL от Mesa
(`__EGL_VENDOR_LIBRARY_FILENAMES=.../50_mesa.json`, `LIBGL_ALWAYS_SOFTWARE=1`)
не помогло: guvcview падает с ошибкой сегментации.

Настоящее лечение — объявить `android_wlegl` в phoc, то есть перенести туда
то, что уже написано в `wlegl.cpp` нашего композитора. Тогда клиенты получат
и GPU, и показ без копирования.

### Мелочи на будущее

* Часы в верхней строке Phosh замерли на времени запуска (22:56 при реальном
  22:58) — верхняя панель не перерисовывается. Отдельно не разбиралось;
  подозрение на отсутствующий `org.gnome.SessionManager`
  (`phosh-session-manager-WARNING: Failed to register client`).
* `phosh-osk-manager-WARNING: Unable to toggle OSK: имя sm.puri.OSK0 не
  предоставлено` — экранной клавиатуры нет, нужен пакет `squeekboard`.
* Скрипты проб глушат `bf7-camera` и `bf7-camd`, а возвращают не всегда обе —
  после опытов проверять `systemctl is-active bf7-camd`.

## 2026-09-21 23:05 — Уточнение к двум предыдущим записям

Проверил два места, где выводы были сделаны не по измерениям.

### Клиенты действительно рисуют программно — теперь это измерено

В записи 23:00 утверждение «клиенты рисуют через `wl_shm`» опиралось на вид
пула разделяемой памяти в отладке, а он мог принадлежать `grim`. Проверено по
цепочке вызовов:

    -> wl_shm@5.create_pool(new id wl_shm_pool@52, fd 17, 4642560)
    -> wl_shm_pool@52.create_buffer(new id wl_buffer@54, 0, 720, 1612, 2880, 0)
    -> wl_surface@50.attach(wl_buffer@54, 0, 0)

`wl_surface@50` — это `zwlr_layer_surface_v1` с именем «phosh lockscreen».
То есть буфер свой, не grim'овский (тот ходил через другой объект, `wl_shm@4`).
Phosh рисует в разделяемую память, стороной клиента, без GPU. Утверждение
подтверждено.

### Застоя кадровых коллбэков нет, но часы стоят

Подозрение, что оболочка встала из-за неотданных `frame`-коллбэков, не
подтвердилось:

    запрошено wl_surface@50.frame(): 792
    пришло wl_callback@N.done():     934

Ответов больше, чем запросов (в счёт попадают и `wl_display.sync`). Доставка
идёт.

При этом часы в верхней строке **стоят**: два снимка с разницей 75 секунд
(23:00:58 и 23:02:13) различаются в области часов ровно на **ноль** точек.
Остальная оболочка при этом перерисовывается — сетка приложений открывается и
закрывается по касанию.

Значит верхняя панель не обновляется, а причина не установлена. Наиболее
вероятное — таймер `GnomeWallClock` из libgnome-desktop, который слушает
`org.freedesktop.timedate1`; в журнале рядом есть
`phosh-session-manager-WARNING: Failed to register client:
org.gnome.SessionManager was not provided`.

### Поправка к выводу записи 22:54

Формулировка «виновата она, не мы» слишком сильна. Измерено следующее:
касания доходят до phosh с верными координатами, на `down` она перерисовывает
подсветку клавиши, на `up` не делает ничего; голое окно GTK3 под свежим phoc
отвечает тремя щелчками на три касания. Это **не доказывает**, что наш стек
чист во всех режимах: проверка GTK шла на только что запущенном композиторе и
несколько секунд, а отказ в Phosh мог развиться позже. Объяснение через
обработчик жестов libhandy остаётся догадкой — при захвате жеста GTK получил
бы `cancel` и вернул кнопку в обычный вид, а перерисовки после `up` нет
вообще.

**Причина не установлена.** Следующий шаг — повторить проверку `gtktouch`
через несколько минут работы композитора и посмотреть, не отваливаются ли
касания и там.

## 2026-09-21 23:15 — android_wlegl в phoc без правки: abort снят, картинка не появилась

Проверял, можно ли дать клиентам GPU под чужим композитором подпоркой, а не
переписыванием. Ответ: наполовину.

### Прослойка

`hybris/bf7-gpu/wleglshim.c` → `/usr/local/lib/libbf7wlegl.so`. Перехватывает
`wl_display_add_socket_auto()` (phoc её импортирует — проверено
`nm -D --undefined-only /usr/bin/phoc`), после настоящего вызова поднимает
gralloc и объявляет глобальный объект:

    hybris_gralloc_initialize(0);
    server_wlegl_create(display);   // _Z19server_wlegl_createP10wl_display

Вся работа уже написана в libhybris и лежит в `libhybris-platformcommon.so.1`
(символ на месте: `0000000000003d90 T _Z19server_wlegl_createP10wl_display`).
Композитору нужно её только позвать — ровно то же делает наш собственный
`gui/wlegl.cpp`, весь в 61 строку.

Применяется вместе с прежней прослойкой:

    LD_PRELOAD="/usr/local/lib/libbf7eglfilter.so /usr/local/lib/libbf7wlegl.so"

### Что заработало

В журнале композитора: `bf7-wlegl: android_wlegl объявлен`.
Со стороны клиента наш пробник `wlgltest` отвечает:

    подключились к композитору
    композитор объявляет android_wlegl (версия 2)

(дальше он спотыкается о `wl_shell`, которого wlroots 0.15 уже не даёт — это
ограничение пробника, не стека).

Программы с GL перестали падать. Раньше было

    Fatal: the server doesn't advertise the android_wlegl global!

теперь `guvcview` живёт, подхватывает **вендорские** `libEGL_mtk.so` и
`libGLESv2_mtk.so` через libhybris, открывает `renderD128` и `/dev/ion`, и
спотыкается уже гораздо дальше — на `SDL2: Couldn't find matching EGL config`.
Firefox поднимается десятью процессами и компилирует шейдеры
(`GetShaderSource()`, `#extension GL_EXT_frag_depth`).

### Чего не хватило

Окна остаются пустыми. Причина найдена:

    nm -D libwlroots.so.10 | grep -iE "bind_wayland|wl_drm|texture_from_wl"
    (пусто)

wlroots 0.15 **не умеет импортировать буферы клиента через EGL**. Она
принимает только `wl_shm` и `linux-dmabuf`; путь
`eglBindWaylandDisplayWL` / `eglQueryWaylandBufferWL`, которым живёт
android_wlegl, из неё выпилен в пользу dma-buf. Наш композитор на QtWayland
этот путь имеет, phoc — нет.

То есть объявить протокол **необходимо, но не достаточно**: клиент теперь
может отдать буфер, а композитор не может его взять.

### Вывод

Подпоркой этот разрыв не закрыть — нужен либо импортёр буферов gralloc в
рендерере wlroots, либо (правильнее) экспорт dma-buf из gralloc, чтобы всё
пошло стандартным путём `linux-dmabuf`. Именно ко второму пришёл Droidian:
их `libgbm-hybris` даёт GBM поверх gralloc, и в выпуске 2026-08-30 появился
вариант, который берёт wlroots, phoc и phosh из Debian **без единой правки**.

У нас половина этого уже есть — `pvr_gbm.so`, и системная libgbm его
принимает. Упор прежний: `pvrsrvkm` не объявляет `DRM_CAP_PRIME` на экспорт,
поэтому буфер нельзя вынести наружу дескриптором dma-buf.

**Следующий шаг, если продолжать: научить gralloc/pvr_gbm отдавать dma-buf.**
Это и есть настоящая развилка всего направления — она же закроет
`disabling wl_drm`, `linux-dmabuf feedback` и показ без копирования разом.

### Попутно

Часы Phosh замирают не сразу, а спустя время работы: в 23:11 снимок показывал
верное время, в 23:14 — по-прежнему 23:11. То есть это не «панель никогда не
рисуется», а «перестаёт». Причина по-прежнему не установлена.

## 2026-09-21 23:20 — Буферы показа пошли из gralloc: CMA свободна, wl_drm и linux-dmabuf ожили

Одна правка сняла сразу три хвоста.

### Что мешало

`pvr_gbm.c` **уже умеет** отдавать dma-buf: `find_dma_fd()` ищет дескриптор в
ручке gralloc, `pvr_bo_get_fd()` его отдаёт. Но распределитель wlroots перед
этим спрашивает у драйвера разрешение:

    drmGetCap(fd, DRM_CAP_PRIME, &v)
    /dev/dri/renderD128  имя=pvr       значение=0x0   ← ни импорта, ни экспорта
    /dev/dri/card1       имя=bf7disp   значение=0x3   ← и то, и другое

Получив 0x0, wlroots печатала `PRIME export not supported` и отступала на
«тупые» буферы из CMA. То есть драйвер не объявлял того, что умеет прослойка
поверх него.

### Правка

В `eglfilter.c` добавлен перехват `drmGetCap`: если спрошено `DRM_CAP_PRIME`,
ответ пустой и `drmGetVersion()->name == "pvr"` — отвечаем `0x3`.

Заодно **сужен** фильтр расширений EGL. Пряталось четыре имени, а достаточно
одного — `EGL_EXT_platform_device`: именно по нему wlroots решает, лезть ли в
устройственную ветку. Пряча остальные три, мы лишали её
`eglQueryDisplayAttribEXT`, а без него она не узнаёт свой узел DRM и отключает
`wl_drm` вместе с `linux-dmabuf` — то самое, чем клиенты отдают буферы.

### Что стало

    [render/allocator/gbm.c:203] Created GBM allocator with backend pvr
    [render/allocator/gbm.c:206] Using DRM node /dev/dri/renderD128
    [render/allocator/gbm.c:140] Allocated 720x1612 GBM buffer
                                 (format 0x34325241, modifier 0x0)
    [backend/drm/drm.c:708] connector DSI-1: Modesetting with '720x1612@1mHz'

Ни одного `DRM_IOCTL_MODE_CREATE_DUMB`. **CmaFree при работающем phoc — 119 744
кБ**, то есть непрерывная память практически не тронута: буферы показа идут из
gralloc и входят в панель импортом PRIME через `bf7_gem_prime_import_sg_table`.
Модификатор 0x0 — линейная раскладка, как и должно быть у gralloc.

Пропали обе жалобы: `Cannot get renderer DRM FD, disabling wl_drm` и
`Failed to init default linux-dmabuf feedback`. Композитор теперь объявляет
клиентам:

    interface: 'wl_drm'                version: 2
    interface: 'zwp_linux_dmabuf_v1'   version: 4
    interface: 'android_wlegl'         version: 2

Модификаторы для `AR24`, `AB24`, `XR24`, `AB30`: и `INVALID`
(0x00ffffffffffffff), и **`LINEAR` (0x0)**. Это и есть ответ на вопрос, что
посылать из клиента.

Попутно выяснилось, что `phosh` умеет `-U, --unlocked` — экран блокировки
перестал быть помехой для опытов.

### Что это открывает

`cma=128M` теперь, строго говоря, избыточна для показа — но пусть остаётся
запасом: камера берёт оттуда ~10 МБ, а 16 МБ было мало и ей.

Главное — путь `linux-dmabuf` до клиентов открыт. Осталось научить
libhybris им пользоваться вместо `android_wlegl`. Клиентская сторона собрана
отдельным модулем `/usr/local/lib/libhybris/eglplatform_wayland.so`, так что
пересобирать всю libhybris не нужно.

## 2026-09-21 23:30 — Клиент рисует на GPU под чужим композитором: linux-dmabuf в libhybris

Обычный Wayland завёлся. Клиент получает контекст на PowerVR, рисует кадр и
отдаёт буфер **стандартным** протоколом, который принимает любой композитор.

### Что было не так

Родной для libhybris путь — `android_wlegl`: клиент отдаёт композитору всю
ручку gralloc, а тот разбирает её своей половиной той же libhybris. Так умеют
лишь композиторы, собранные с оглядкой на hybris. В wlroots этого нет: импорт
буферов клиента через EGL из неё выпилен в пользу `linux-dmabuf`, и чужие
`wl_buffer` она не может превратить в текстуру.

Записью 23:15 было показано, что объявить протокол мало: клиент отдаёт буфер,
композитор не может его взять.

### Правка libhybris

Дерево на телефоне: `/root/hybris/libhybris`. Собирается один модуль,
`egl/platforms/wayland`, за полминуты.

1. **Перенастройка на клиентские буферы.** Было `--enable-wayland_serverside_buffers`
   (по умолчанию), то есть буфер выделял композитор через `android_wlegl`.
   Нужен обратный режим: клиент выделяет через gralloc, а мы отдаём его
   дескриптор. Ключ: `--disable-wayland_serverside_buffers`.

2. **`platforms/wayland/wayland_window_common.cpp`** — добавлен
   `WaylandNativeWindowBuffer::wlbuffer_from_dmabuf()`. Он ищет в ручке gralloc
   настоящий dma-buf пробой по размеру (постоянного номера у дескриптора нет: у
   PowerVR их три, и только один — сам буфер), переводит формат Android в
   четырёхбуквенный код DRM и отдаёт буфер через
   `zwp_linux_buffer_params_v1.add` + `create_immed`. Модификатор — нули,
   `DRM_FORMAT_MOD_LINEAR`: раскладка gralloc линейна.

   `ClientWaylandBuffer::init()` сперва пробует этот путь и откатывается на
   `android_wlegl`, если композитор обычного протокола не объявляет — так
   продолжает работать наш собственный `bf7gui`.

3. **`egl/platforms/wayland/eglplatform_wayland.cpp`** — привязка
   `zwp_linux_dmabuf_v1` (третья редакция и выше: с неё есть `create_immed`),
   и прекращение работы теперь только если композитор не даёт **ни одного**
   пути. Раньше здесь падали все программы с GL под любым чужим композитором.

4. Разбор протокола порождён `wayland-scanner` в
   `platforms/wayland/bf7-dmabuf-{client-protocol.h,protocol.c}` и включён
   прямо в `wayland_window_common.cpp`: отдельной целью в порождённый automake
   Makefile его пришлось бы добавлять правкой, которую съела бы следующая
   настройка дерева.

### Проверка

Новый пробник `kernel-work/probes/glarea.c` — окно GTK3 с `GtkGLArea`,
заливка меняющимся цветом средствами GL ES, через `libepoxy` (прямое
связывание с `-lGLESv2` даёт пустоту из-за glvnd).

    контекст GL создан
      GL_VENDOR:   Imagination Technologies
      GL_RENDERER: PowerVR Rogue GE8300
      GL_VERSION:  OpenGL ES 3.2 build 1.15@6070602
    кадр 1 нарисован
    кадр 60 нарисован

Окно видно на панели, заливка живая: два снимка с разницей 3 с дают цвета
`[115 64 140]` и `[72 64 183]`.

Протокольная отладка клиента (`WAYLAND_DEBUG=1`) — окончательное
доказательство:

    zwp_linux_dmabuf_v1@35.create_params                        ×3
    zwp_linux_buffer_params_v1@38.add(fd 45, 0, 0, 2944, 0, 0)
    zwp_linux_buffer_params_v1@38.create_immed(wl_buffer@39, 720, 1468, 875708993, 0)

    android_wlegl_create_buffer:  0 раз

Формат 875708993 = 0x34324241 = `AB24` (ABGR8888), модификатор нули, шаг 2944
байта — gralloc дополнил 720 точек до 736. Три буфера. `android_wlegl`
объявлен в реестре, но не использован.

### Ловушка GTK3

`GtkGLArea` с `use_es` всё равно зовёт настольную `glDrawBuffer`, и epoxy
прекращает работу: `No provider of glDrawBuffer found. Requires one of:
Desktop OpenGL 1.0`. Лечится переменной `GDK_GL=gles` — с ней GTK3 держится
в пределах ES. **Это нужно задавать всем программам GTK3 с GL.**

### Что это меняет

Теперь на нашем железе работает любой композитор Wayland, а не только
написанный нами: клиенты получают GPU через стандартный `linux-dmabuf`.
Запасная копия прежнего модуля — `/root/eglplatform_wayland.so.до-dmabuf`.

## 2026-09-22 00:06 — Динамик заговорил: два условия вместо одного

Звук молчал с самого начала работ. Разгадан до конца и закреплён службами.

### Как проверялось без ушей

Слышать я не могу, поэтому мерил микрофоном: играем тон 440 Гц и одновременно
пишем, потом смотрим энергию на 440 Гц в записи. Главное правило — сравнивать
две записи, с тоном и без: первая же попытка дала «превышение над шумом в 22
раза», а честное сравнение показало отношение **1.0** — это был просто рисунок
шума.

### Что мешало: три отдельных препятствия

**1. Микрофон.** Запись выходила пустой (ровно нули). Нужны
`Audio_ADC_1_Switch=1`, `Audio_Preamp1_Switch=1` (IN_ADC1),
`Audio_MicSource1_Setting=0`, усиление `Audio_PGA1_Setting` на 24 дБ.

**2. Внешний усилитель не включался никогда.** В дереве устройств есть

    mt_soc_dl1_pcm@11220000/extamp_gpio = <phandle 0x97 0x00>   → линия 151
    mt_soc_dl1_pcm@11220000/extamp_mode = <0x02>

но драйвер эту линию не трогает: `AudDrv_GPIO_EXTAMP_Select()` ищет состояния
pinctrl с именами `extamp_high` / `extamp_low`, а узел объявляет только
`aud_smartpa_on` / `aud_smartpa_off` — условие `aud_gpios[...].gpio_prepare`
никогда не выполняется, и функция тихо ничего не делает.

Линию приходится держать самим. `/sys/class/gpio` в ядре не собран
(`CONFIG_GPIO_SYSFS is not set`), зато есть `/dev/gpiochip0`, так что
`gpioset --mode=signal gpiochip0 151=1`. Проверка — в отладочном узле MTK
`/sys/devices/platform/pinctrl@10005000/mt_gpio` строка пина меняется с
`0000000110` на `0111000110` (направление «выход», уровень высокий).

**3. Ключи кодека принимаются только при работающем потоке.**
`Speaker_Amp_Change()` в драйвере проверяет `GetDLStatus()`, и щелчок до
открытия устройства пропадает впустую. Рабочий набор, найденный перебором с
посекундным замером:

    AUD_CLK_BUF_Switch = 1
    Audio_Amp_L_Switch = 1
    Audio_Amp_R_Switch = 1

Энергия тона в записи подскакивает с ~5 тысяч до ~15 миллионов, то есть
в **три тысячи раз**.

**`Speaker_Amp_Switch` включать нельзя.** С ним звук пропадает, и обратное
выключение его уже не возвращает — нужен полный сброс всех ключей.
`Ext_Speaker_Amp_Switch` и `Voice_Amp_Switch` на слышимость не влияют.

### Главная тонкость: путь живёт только пока открыт поток

Замерено прямо: во время потока энергия 18 121 368, через 20 секунд после его
закрытия на новом потоке — 8 909, то есть тишина. Поэтому «поднять один раз при
загрузке» не работает в принципе. Устройство должно оставаться открытым.

### Два падения ядра по дороге

Попытка поставить pipewire дважды уронила телефон
(`androidboot.bootreason=kernel_panic`, pstore пуст). Причина не в самом
сервере, а в переборе: wireplumber открывает **все** устройства карты, а их
двадцать три — голосовые каналы модема, петли, TDM, SCP. Одно из них роняет
ядро.

Отсюда же понятно, почему не годится `dmix`: он работает отображением памяти,
которого драйвер не поддерживает (`Function not implemented`, -38) — и это же
объясняет давнюю запись про панику на mmap звука.

### Что сделано

* `/usr/local/bin/bf7-audio-up` — сброс ключей, короткий поток, подъём набора,
  настройка микрофона.
* `bf7-audio.service` — `Type=exec`, в `ExecStart` держит
  `gpioset gpiochip0 151=1` (отпустить — значит замолчать), в `ExecStartPost`
  зовёт скрипт.
* `/etc/pulse/bf7.pa` — PulseAudio **без** `module-udev-detect`: только
  `module-alsa-sink device=hw:0,0 mmap=false tsched=0` и
  `module-alsa-source device=hw:0,1`. `module-suspend-on-idle` намеренно не
  грузится: постоянно открытое устройство и есть то, что держит путь.
* `bf7-pulse.service` — сервер, за ним `bf7-audio-up` через три секунды.
* `/etc/asound.conf` — `default` как обычный `plug` на `hw:0,0` для программ
  мимо сервера.
* Телефонный сеанс получил `PULSE_SERVER=unix:/run/bf7-pulse/native`.

### Итог

`paplay` тона через сервер: в записи **пик всего спектра ровно на 440 Гц**,
энергия 21 120 430, среднеквадратичное записи 369.6. Динамик и микрофон
работают.

## 2026-09-22 01:40 — Происшествие: смена слота, повреждение файловой системы и восстановление

Тяжёлый час. Записываю подробно, потому что все три беды связаны и повторимы.

### 1. Загрузчик ушёл на слот B

После нескольких перезагрузок подряд телефон загрузился **стоковым ядром**
`4.19.191-g216400aba14e-dirty` со слота B. Наш корень при этом смонтировался, и
systemd запустился — выглядело как «наши службы сломались», хотя дело было в
ядре: модулей для него нет, `/dev/loop*` не создаются, `bf7-android-mounts`
отваливается с `losetup: cannot find an unused loop device`, Wi-Fi не
поднимается.

Причина: в нашем образе **намеренно нет** `bf7-mark-boot-success`, поэтому
счётчик попыток у загрузчика уменьшается с каждой загрузкой и рано или поздно
доходит до нуля. Это задумано как страховка при опытах с ядром, но при частых
перезагрузках подряд превращается в мину.

**Признак:** `uname -r` показывает `4.19.191-…-dirty`, в `/proc/cmdline`
`androidboot.slot=b`. **Лечение:** `fastboot set_active a`.

### 2. Файловая система повредилась

Череда паник и жёстких перезагрузок оставила корень в состоянии
`not clean with errors`, и наш init смонтировал его `ro,norecovery` — то есть
журнал не проигрывался никогда, и система была без записи.

Чинить пришлось с невмонтированным корнем. `fastboot boot` на этом загрузчике
не работает («Status read failed»), так что образ восстановления пришлось
прошить. В initramfs `e2fsck` не было — добавил его вместе с библиотеками в
наложение `native-linux-build/alps191-rootfs-v1/overlay/`.

    e2fsck -f -y /dev/mmcblk0p48
    ...
    BF7_DEBIAN: ***** FILE SYSTEM WAS MODIFIED *****
    77767/3342336 files, 1695015/13361016 blocks

После починки корень монтируется `rw`.

### 3. Ловушка: образ восстановления собрал не то ядро

Пересобирая образ, я взял `KERNEL_IMAGE` из сборки `bf7-x657b-clang11-v1`, а
модули на телефоне собраны против другой. Итог — `insmod: could not insert
module wmt_drv`, служба `bf7-wifi` **повисла навсегда** и удержала весь
`systemd` в состоянии `starting`: ни ssh, ни telnet, только отклик на ping.

Спасло то, что в initramfs заложены аварийные каналы: **telnet на 2323 из
initramfs и на 2324 из Debian**. Через 2324 удалось остановить повисшую службу
и разблокировать загрузку.

Это ровно та ловушка, что записана в памяти: «модули связности обязаны
пересобираться тем же заходом». Правильное лечение — вернуть тот образ, из
которого система работала: у меня был снят `boot_a.img` перед правкой CMA, и
перепакованный `boot_a-cma128.img` с тем же ядром.

### 4. Что унесла починка диска

454 файла ушли в `lost+found`. Проверка `dpkg -V` показала 302 записи, и **все
до одной** — `/usr/share/lintian/overrides/`, то есть безобидные. А вот
важного пропало четыре вещи, и каждая ломала своё:

| потеря | как проявилось |
|---|---|
| `/usr/share/glvnd/egl_vendor.d/10_libhybris.json` | `DRI2: gbm device using incorrect/incompatible backend`, оболочка не стартует |
| `/etc/systemd/system/bf7-android-hal.service` (обрезан до 110 байт) | нет вендорских служб графики, gralloc мёртв |
| `/root/camera/` целиком | камера: `Failed at step EXEC spawning /root/camera/bf7-seninf-up` |
| `/etc/NetworkManager/system-connections/HOME-AP.nmconnection` (обрезан до 24 байт) | `nmcli connection up` отвечает «unknown connection» |

Всё восстановлено из репозитория и пересобрано на телефоне. Отдельная тонкость:
`bf7-seninf-up` **обязан** собираться андроидным компилятором из NDK — собранный
обычным gcc падает с SIGTRAP.

### Выводы

* **Не перезагружать подряд без нужды.** Каждая перезагрузка приближает откат
  на слот B.
* **Держать в репозитории всё, что лежит на телефоне.** Восстановление заняло
  минуты именно потому, что исходники и описания служб были под управлением
  версий. `/root/camera` восстановился полностью, а вот если бы демон жил
  только на телефоне — пропал бы.
* **Аварийные каналы окупились.** Без telnet на 2324 пришлось бы прошивать
  вслепую.
* **Проверять целостность после починки диска**: не только `dpkg -V`, но и свои
  файлы — обрезанные до пары десятков байт выглядят целыми в `ls`.

## 2026-09-22 01:50 — Камера в покое: 31% ядра превратились в 3%

Замер на стоящем телефоне показал, что демон камеры жжёт треть ядра, когда
никто не смотрит. Разобрано на две причины и обе сняты.

### Причина первая: холостой кадр считался целиком

Холостой ход и так сбавлен до 2,2 кадра в секунду, но каждый такой кадр
проходил полное преобразование — по 140 мс процессора на картинку, которую
никто не видит. Отдавать что-то в узел обязательно (иначе v4l2loopback
перестанет считаться камерой), но отдавать можно **прошлый** кадр: он уже
лежит в выходном буфере.

Теперь в покое пересчитывается каждый восьмой кадр — раз в три с половиной
секунды, чего хватает, чтобы выдержка не отстала от освещения и первый кадр
после открытия камеры был уже примерно верным.

Стало 15% вместо 31%.

### Причина вторая: вопрос «смотрит ли кто-нибудь» стоил 8% ядра

Проверка зрителей перебирала весь `/proc`, открывала `/proc/*/fd` у каждого
процесса и читала все ссылки — четыре раза в секунду. Замер отдельной
программой: **19,7 мс на один перебор**, то есть 7,9% ядра на один вопрос.

Ответ всё это время лежал рядом: у модуля есть `open_count`. Добавил в нашу
сборку v4l2loopback атрибут `openers`, который его показывает, и демон теперь
читает одно число. Медленный путь оставлен запасным на случай непатченного
модуля.

    /sys/devices/virtual/video4linux/video0/openers

Считаются все открывшие, включая пишущего, поэтому зритель — каждый сверх
первого.

### Итог

| состояние | было | стало |
|---|---|---|
| никто не смотрит | 31% ядра | **3%** |
| открыт зрителем | — | 172% (полный ход, четыре потока) |
| снова покой | — | 3% |

Десятикратная разница на телефоне, который большую часть времени просто лежит.

## 2026-09-22 02:35 — Виновник ночных паник найден: wireplumber

Полтора часа телефон падал раз в несколько минут, каждый раз с
`androidboot.bootreason=kernel_panic`, и дважды загрузчик от этого откатывался
на слот B со стоковым ядром. Причина оказалась одна и та же с самого начала.

### Кто это делал

    [5416:wireplumber] Unable to handle kernel paging request at ffffff8009e5c000
    [5416:wireplumber] Internal error: Oops: 96000047
    [5416:wireplumber] pc : __memset_io+0x3c/0x68
    [5416:wireplumber] lr : set_mem_block+0x60/0x70
    mtk-audio 11220000.audio: invalid position: pos = 6192, buffer size = 4096

`wireplumber` остался в системе от неудачной попытки поставить pipewire. Его
никто не запускал нарочно — он поднимался сеансом, перебирал все двадцать три
устройства звуковой карты и натыкался на `mtk_pcm_dl2` (устройство 23,
MultiMedia2_PLayback), чей обработчик затирает область звукового ОЗУ за её
границами.

### Почему не помогло правило udev

Правило `70-bf7-snd-safe.rules` закрывает опасные устройства для всех, кроме
root. Но у нас **всё работает от root**, а root права обходит. Ограничение прав
против этой беды бесполезно.

Единственное, что помогло, — удалить пакеты и закрепить их:

    apt-get remove --purge wireplumber pipewire pipewire-bin pipewire-alsa
    apt-mark hold wireplumber pipewire

Правило udev оставлено: оно защитит, если однажды что-то будет работать не от
root, и служит описанием того, какие устройства опасны.

### Побочная беда: /dev у нас tmpfs

По дороге выяснилось, что в ядре **не задан `CONFIG_DEVTMPFS`**, поэтому `/dev`
— обычный tmpfs, и узлы там появляются только стараниями udev. После череды
паник он пропустил больше двухсот событий, и система осталась без
`/dev/snd/pcmC0D1c` (звуковой сервер не поднимал источник), без `/dev/loop*`
(стоковые разделы Android не монтировались, а без них нет GPU) и без
`/dev/gpiochip0` (усилитель звука не включался).

Написана служба `bf7-devnodes-all`: обходит `/sys/dev/char` и `/sys/dev/block`
и создаёт недостающие узлы. При первом запуске создала **232** штуки.

### Внешний усилитель: два состояния вместо одного

Ещё одна путаница, стоившая времени. Вывод включения усилителя (линия 151)
бывает в двух состояниях, и какое сложится — зависит от порядка запуска:

* линию держит драйвер (в `gpioinfo` подписана `fs1512_extamp_gpio`) — тогда
  ею управляет ключ `Ext_Speaker_Amp_Switch`, причём **только при смене**
  значения: повторная единица ничего не делает;
* линия свободна (`unused`) — ключ мёртв, и держать вывод приходится самим.

Перехватывать линию раньше драйвера нельзя: он её тогда не получит. Поэтому
`bf7-audio-amp` сперва пробует ключ (через ноль), смотрит на уровень вывода и
только если тот не поднялся — берёт линию себе через `gpioset`.

### Ещё: нельзя поднимать период звукового сервера

Попытка увеличить `fragment_size` с 4096 до 16384 кадров (ради трёх пробуждений
в секунду вместо двенадцати) дала ту же беду: буфер перестаёт помещаться в
звуковое ОЗУ, и драйвер затирает память за его границей. Телефон уходил в
перезагрузку раз в минуту. 4096 при четырёх периодах — проверенное сочетание.

### Итог

Самопроверка `bf7-selftest`: **33 в порядке, 0 с изъяном**. На экране сетка
приложений: браузер, Foliate, Megapixels, настройки, калькулятор, консоль,
погода, часы. Звук, сеть, камера, тач, вибро — всё работает.

## 2026-09-22 03:00 — Bluetooth: разведка. Сборочная часть решена, упор в поколение исходника

Разобрался, чего именно не хватает, чтобы не начинать в следующий раз с нуля.

### Что уже есть

* Ядро связности поднято: `wmt_drv`, `wmt_chrdev_wifi`, `/dev/stpwmt`,
  `/dev/wmtdetect`, работает `wmt_launcher`.
* **Наш `wmt_drv` экспортирует всё нужное.** Проверено по `/proc/kallsyms`:
  `mtk_wcn_stp_send_data`, `mtk_wcn_stp_receive_data`,
  `mtk_wcn_stp_register_event_cb`, `mtk_wcn_stp_register_if_rx`,
  `mtk_wcn_stp_is_ready`, `mtk_wcn_stp_is_rxqueue_empty`,
  `mtk_wcn_wmt_func_on`, `mtk_wcn_wmt_func_off` — всего 116 символов `stp`/`wmt`
  и 269 `mtk_wcn_*`.
* **`mtk_wcn_stpbt_drv_init` объявлен слабым символом внутри `wmt_drv`**

      ffffff996880c560 w mtk_wcn_stpbt_drv_init  [wmt_drv]

  То есть ядро связности готово позвать драйвер Bluetooth, как только тот
  появится и определит этот символ. Пока символ слабый и пустой — обращение
  просто ничего не делает, и `/dev/stpbt` не создаётся.

**Осторожно:** первые проверки этих символов дали ноль, и я едва не записал
Bluetooth в невозможное. Дело было в шаблоне: у символов из модулей в
`/proc/kallsyms` после имени стоит `\t[имя_модуля]`, и `grep -c " имя$"` их не
находит. Правильный шаблон — `grep -cE "[ \t]имя([ \t]|$)"`.

### Где упор

Исходник драйвера есть только в доноре `kernel_ubports_mt6781`, и он от более
нового поколения:

* `bt/mt66xx/btif/` и `bt/mt66xx/connac2/` — для CONNAC2.0, обмен через BTIF;
* `bt/mt66xx/legacy/` (1629 строк, `stp_chrdev_bt.c` + `dbg_bt.c` +
  `fw_log_bt.c`) — как раз для старых комбо-чипов через STP, наш случай;
* `bt/linux_v2/` — вообще для SDIO-чипов (MT7668 и родня).

Сборочная обвязка сделана и работает: `kernel-work/bf7-bluetooth/` с Makefile
и build.sh, пути к заголовкам связности и `mt-plat` подобраны, внешние символы
подхватываются из `Module.symvers` нашего `wmt_drv`.

Но компиляция `legacy` против наших заголовков даёт **20 ошибок
несовместимости типов**: драйвер ждёт `ENUM_WMTMSG_TYPE_T` и родню, которых в
`wmt_exp.h` поколения mt6761 нет (там `ENUM_WMTRSTMSG_TYPE_T` и другой набор).

### Что нужно, чтобы доделать

Исходник драйвера Bluetooth **той же эпохи**, что и наша связность — из дерева
mt6761 или mt6765 ALPS. В имеющихся донорах (`motorola-mt6761-connectivity-sta32`,
`motorola-mt6765-connectivity`) есть только `common`, `adaptor` и `gen4m`;
каталога `bt` в них нет. Либо переписать вызовы под наш API вручную — работа
на несколько часов с неочевидным исходом, потому что за API стоит ещё и
прошивка чипа.

Дальше, когда драйвер соберётся: `/dev/stpbt` — это HCI H4, и BlueZ цепляется
к нему обычным `btattach -B /dev/stpbt -P h4`.

### Побочная находка

`setlocalversion` добавлял к версии ядра плюс, потому что видел незафиксированные
правки в репозитории, — а с ним не совпал бы ни один собранный модуль. Подавлено
пустым файлом `kernel-work/x657b-4.19/.scmversion`.

## 2026-09-22 03:15 — Всё поднимается само: 36 проверок после чистой загрузки

После перезагрузки без единого вмешательства: `bf7-selftest` даёт **36 в
порядке, 0 с изъяном**, Wi-Fi подключён, яркость управляется, батарея
читается, звук играет и пишется. Проверено замером через микрофон — пик
спектра ровно на 440 Гц.

Включены при загрузке тринадцать служб: `bf7-devnodes-all`,
`bf7-android-hal`, `bf7-audio`, `bf7-audio-amp`, `bf7-pulse`, `bf7-blank`,
`bf7-vibrad`, `bf7-camera`, `bf7-camd`, `bf7-phosh`, `bf7-wifi-watch`,
`bf7-fuelgauge`, `NetworkManager`.

### Браузер: почему нужна особая переменная

Epiphany (и всё на WebKit) прекращал работу сразу при запуске:

    Could not create GBM EGL display: EGL_NOT_INITIALIZED. Aborting...

Проверено и отброшено: узел берётся правильный (`/dev/dri/renderD128`, видно в
`strace`), прослойки в дочерние процессы попадают, песочница ни при чём
(`WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS=1` не помогает), точка входа тоже —
отдельной пробой показано, что и основная `eglGetPlatformDisplay`, и
расширенная `...EXT` на платформе GBM у нас работают.

Остаётся единственное объяснение: окно уже держит дисплей EGL на платформе
Wayland, а наш hybris-EGL **второго дисплея в том же процессе не заводит**.
Отключение ускоренной композиции (`WEBKIT_DISABLE_COMPOSITING_MODE=1`) убирает
вторую попытку, и браузер работает — страница рисуется, просто без GPU.
Переменная добавлена в описание сеанса.

### Экран блокировки: проверить не вышло

Хотел перепроверить, не починился ли он сам. Не вышло: **впрыснутые жесты
оболочка не принимает**. Нажатия проходят (сетка приложений открывается по
впрыску), а свайпы — нет: ни шторка сверху, ни разблокировка снизу. Пальцем
те же жесты работают. Похоже, обработчику жестов libhandy нужны свойства
движения, которых у ровного впрыска нет.

Поэтому оболочка по-прежнему поднимается с `--unlocked`.

### Мелочи

* `bf7-phosh` получил `Restart=on-failure`: без этого одна случайная беда
  оставляла телефон с чёрным экраном до ручного вмешательства.
* Установщик кладёт правило подсветки как `71-bf7-backlight.rules` — число
  впереди определяет порядок чтения, и привязка светодиода должна случиться
  после создания узла.

## 2026-09-22 03:25 MSK — Загрузка была плавающей: systemd молча резал цепочку

Самопроверка дала 35 из 37: не поднялись `bf7-android-hal` и `bf7-fuelgauge`,
батарея показывала −1. Обе службы значились `enabled`, обе — `inactive (dead)`,
и в журнале по ним **ни строчки**. Не «упали», а вовсе не запускались.

Причина — два цикла в порядке запуска:

```
systemd-udevd -> bf7-devnodes-all -> bf7-android-mounts -> systemd-udevd
basic.target  -> bf7-devnodes     -> basic.target
```

Первый: у `bf7-devnodes-all` стояло `After=systemd-udevd`, а у
`bf7-android-mounts` — `Before=systemd-udevd`, и между ними
`Before=bf7-android-mounts` замыкало кольцо. Второй: у старого `bf7-devnodes`
не было `DefaultDependencies=no`, поэтому он неявно получал `After=basic.target`
при явном `Before=basic.target`.

Цикл systemd разрывает, удаляя одно задание, — и **какое именно, решает
случайный порядок обхода хеш-таблицы**. Когда жертвой падал
`bf7-android-mounts`, вместе с ним по `Requires=` слетали зависимые
`bf7-android-hal` и `bf7-fuelgauge`. Когда жертвой падал `bf7-devnodes` —
всё выглядело здоровым. Отсюда 36 из 36 в одну загрузку и 35 из 37 в другую
**без единой правки между ними**. Искать такое по самопроверке бесполезно:
она не отличает исправную загрузку от удачной.

Проверять надо так: `journalctl -b | grep -c "ordering cycle"` должно давать
ноль.

### Заодно опровергнуто: `/dev` у нас devtmpfs, а не tmpfs

`Before=systemd-udevd` у `bf7-android-mounts` — не лишний: udev при опросе
железа грузит модули, а те просят прошивки, и
`/sys/module/firmware_class/parameters/path` = `/vendor/firmware`. Убирать
нельзя.

А вот собственный разбор ночи на 2026-09-22 оказался неверен. Я записал тогда,
что `CONFIG_DEVTMPFS` не задан и потому `/dev` — обычный tmpfs, где узлы
создаёт только udev. По `/proc/config.gz` работающего ядра:

```
CONFIG_DEVTMPFS=y
CONFIG_DEVTMPFS_MOUNT=y
```

и `mount` показывает `devtmpfs on /dev type devtmpfs`. Узлы заводит ядро.
Те 232 узла отсутствовали не из-за файловой системы, а из-за неподнявшихся
модулей: нет устройства — нет записи в `/sys/dev/char`, из которой ядро делает
узел. На здоровой загрузке `bf7-devnodes-all` создаёт **ноль** узлов, а
`losetup -f` сам выдаёт `/dev/loop12` без всякой заготовки.

Поэтому: `bf7-devnodes` отставлен совсем (он и не запускался ни разу — задание
удалялось каждую загрузку, а права на `/dev/null` при этом были верные; копия
сохранена в `hybris/bf7-gpu/units/отставлены/`). `bf7-devnodes-all` оставлен
подстраховкой на случай подменённого `/dev`, но без `Before=bf7-android-mounts`.

### Ещё: средняя нагрузка 13 — это не занятость

`load average 13` при 69% простоя. Считаются ядерные нити MediaTek, вечно
спящие в состоянии `D`: `hang_detect`, `battery_thread`, `charger_thread`,
`ccci_poll1`, `disp_check` и ещё с десяток. В Linux непрерываемый сон входит в
среднюю нагрузку. Смотреть надо на `%Cpu(s)`, не на `load average`.

### И: правки libhybris не попадали в git

`hybris/libhybris` — встроенный репозиторий со своим `.git`, и `git add` молча
делал бы gitlink вместо файлов. Коммит `a443248` при всём своём сообщении унёс
**ноль** файлов libhybris. Путь кадра через `zwp_linux_dmabuf_v1` — тот самый,
из-за которого на нашем GPU рисуют обычные вейландовы программы, — жил только
на диске и на телефоне. Теперь он в дереве заплаткой
`hybris/bf7-patches/libhybris-bf7.patch` (17 файлов, база libhybris `00dac8e6`).
Новые файлы в `git diff` не попадают сами — нужен `git add -N` перед снятием.

### Как прочитать счётчик попыток слота, ничего не трогая

Раздела `misc` на BF7 нет — метка A/B лежит в `para`, это `mmcblk0p3`.
Каталога `/dev/block/` тоже нет (это андроидное соглашение), узел зовётся
просто `/dev/mmcblk0p3`. Читать безопасно:

```sh
dd if=/dev/mmcblk0p3 bs=1 skip=2048 count=48 2>/dev/null | od -A d -t x1
```

```
5f 61 00 00  42 43 41 42  01 02 00 00  1f 00 9e 00
```

`5f 61` — `_a`, текущий слот. `42 43 41 42` — магия `bootloader_control`.
Дальше версия 1 и число слотов 2. Затем по два байта на слот; первый байт
разбирается по битам: младшие 4 — приоритет, следующие 3 — оставшиеся попытки,
старший — отметка успешной загрузки.

```
A: 0x1f -> приоритет 15, попыток 1, успех 0
B: 0x9e -> приоритет 14, попыток 1, успех 1
```

`fastboot set_active a` выставляет попыткам потолок, и каждая загрузка отнимает
одну. Единица означает: ещё одна перезагрузка — и слот A исчерпан, загрузчик
уйдёт на B со стоковым ядром. Поднять счётчик или поставить отметку успеха —
значит писать в `para`, а это не `boot_a`; без отдельного слова не делаю.
Поэтому `bf7-boot-ok.service` стоит выключенным.

## 2026-09-22 03:38 MSK — На wlan0 сидели два супликанта, и я сам их туда посадил

После починки циклов самопроверка сказала «Wi-Fi: unavailable». В журнале
NetworkManager — бесконечное:

```
device (wlan0): Couldn't initialize supplicant interface: Timeout was reached
device (wlan0): re-acquiring supplicant interface (#2).
```

а у системного `wpa_supplicant` — дословная причина:

```
ctrl_iface exists and seems to be in use - cannot override it
Delete '/run/wpa_supplicant/wlan0' manually if it is not used anymore
```

На интерфейсе работали **два** посредника: системный `wpa_supplicant.service`,
которым правит NetworkManager, и наш `bf7-wpa.service` с собственным файлом
настроек. Второй захватывал сокет управления первым, и NetworkManager не мог
получить интерфейс никогда.

Самое неприятное: `bf7-wpa.service` значился **выключенным**. Запускала его
`bf7-static-wifi.service` строкой `Requires=bf7-wpa.service` — а `Requires=`
поднимает и выключенную службу. Поэтому `systemctl disable` тут бесполезен,
нужна маскировка:

```sh
systemctl mask bf7-static-wifi bf7-wpa
```

Причём если файл службы лежит в `/etc/systemd/system/`, `mask` откажется:
«File ... already exists». Сначала убрать файл, потом маскировать имя.

`bf7-static-wifi` вдобавок прибивал `wlan0` статическому адресу
192.168.27.140, ставил свой маршрут по умолчанию и переписывал
`/etc/resolv.conf` — всё это поверх того, что делает NetworkManager. Обе службы
отставлены, копии в `hybris/bf7-gpu/units/отставлены/`.

Ещё тонкость при снятии на ходу: уходящий `bf7-wpa` успел отправить
`mtk_cfg80211_disconnect` и оборвал связь, которую NetworkManager только что
поднял. В dmesg это видно по номеру процесса:

```
[639.527] (1)[4242:wpa_supplicant] mtk_cfg80211_disconnect:(REQ INFO) ucBssIndex = 0
[639.528] (0)[2978:main_thread] kalIndicateStatusAndComplete: [wifi] wlan0 netif_carrier_off
```

После `nmcli con up HOME-AP` всё встало: адрес 192.168.27.52 по DHCP, маршрут по
умолчанию, наружу 74 мс до 1.1.1.1.

Задним числом понятно и то, почему в прошлые разы Wi-Fi «то работал, то нет»:
кто успевал первым захватить сокет, тот и выигрывал. Ещё одна гонка, как с
циклами systemd.

## 2026-09-22 03:38 MSK — Загрузка занимает пять минут, и это не графика

Замерено по часам ПК: от `systemctl reboot` до ответа ssh — **255 секунд**.
Показаниям `systemd-analyze` верить нельзя: часы во время загрузки прыгают
вперёд примерно на четыре минуты, и он приписывает этот скачок работе служб
(говорит «NetworkManager @4min48s», тогда как монотонный журнал показывает
запуск на 112-й секунде). Мерить только снаружи.

Найдено три виновника.

**Первый — `bf7-wifi-watch.service` с `Type=oneshot` и `TimeoutStartSec=400`.**
Служба стоит в `Wants` у `multi-user.target`, а oneshot держит цель
недостигнутой, пока не вернётся. Скрипт же ждёт подключения Wi-Fi. Всё это
время в очереди стоят `bf7-phosh`, `bf7-blank`, `bf7-vibrad` — то есть телефон
показывает чёрный экран не потому, что графика медленная, а потому, что она
ждёт сеть. Исправлено на `Type=simple`.

**Второй — активация служб через шину D-Bus, которая не удаётся.**
`org.freedesktop.hostname1` и `org.freedesktop.nm_dispatcher` каждая отъедают
по 25 секунд:

```
Failed to activate service 'org.freedesktop.hostname1': timed out (service_start_timeout=25000ms)
```

**Третий — гонка супликантов из записи выше:** NetworkManager по три раза
пересоздавал интерфейс, каждый заход по десятку секунд.

Процессор ни при чём: четыре ядра в строю, регулятор `schedutil`, потолок
2001 МГц, при замере частоты 1.0–1.8 ГГц.

## 2026-09-22 03:38 MSK — У слота A кончились попытки

```
dd if=/dev/mmcblk0p3 bs=1 skip=2048 count=16 | od -A d -t x1
5f 61 00 00 42 43 41 42 01 02 00 00 0f 00 9e 00
```

Байт слота A стал `0x0f`: приоритет 15, **попыток 0**, отметки успеха нет. До
перезагрузки было `0x1f` — одна попытка. Следующая перезагрузка уйдёт на слот B
со стоковым ядром, и вернуть телефон можно будет только `fastboot set_active a`
с кнопок.

Отметить слот успешным — значит писать в `para`, а это не `boot_a`; жду слова.
Вдобавок помощника, которого зовёт `bf7-boot-ok.sh`, попросту нет:
`/bin/bf7-mark-boot-success` отсутствует, так что служба была бы пустышкой,
даже если бы её включили.

**Пока слова нет — всё, что проверяется только перезагрузкой, копится в
очередь на одну-единственную загрузку.**

## 2026-09-22 03:47 MSK — Касания теряются, когда в фокусе поверхность самой оболочки

Давняя загадка: экран блокировки Phosh глотает нажатия — кнопка подсвечивается,
щелчок не рождается, — а обычное окно GTK под тем же композитором отвечает
нормально. Сегодня нашлась граница, и она проходит не там, где я думал.

Опыты (касания через `bf7-touchinj`, свой узел uinput; правило калибровки
`99-bf7-touch.rules` его не касается — оно привязано к имени `mtk-tpd2`, так что
координаты идут как есть):

| что нажимаю | что в фокусе | результат |
|---|---|---|
| поле поиска в оболочке | поверхность оболочки | **работает** |
| значок калькулятора | поверхность оболочки | **работает** |
| клавиша `q` у squeekboard | поверхность оболочки | не доходит |
| клавиша `123` у squeekboard | окно калькулятора | **работает**, раскладка сменилась |
| цифра на экране блокировки | экран блокировки | не доходит |

Долгое нажатие в 2,5 секунды по клавише `q` при фокусе на оболочке не даёт даже
подсветки — событие не приходит вовсе, дело не в потерянном «отпускании».

Отсюда правило: **phoc 0.24 (wlroots 0.15) не доставляет касания на слоевую
поверхность, пока в фокусе другая слоевая поверхность.** Когда в фокусе обычное
окно, всё работает.

Это разом объясняет всё накопленное:

* экран блокировки — слоевая поверхность, и в фокусе она же: нажатия пропадают;
* `squeekboard` при открытой оболочке — не работает, а поверх приложения — работает;
* `onboard` работал всегда, потому что это обычное окно, а не слой.

### Как разблокировать, пока не починено

Клавиатура проходит там, где не проходит касание. Проверено:

```sh
export XDG_RUNTIME_DIR=/run/bf7-phosh WAYLAND_DISPLAY=wayland-0
bf7-touchinj 360 800 3 400 80          # разбудить
bf7-touchinj свайп 360 1400 360 500    # поднять клавиатуру пароля
printf '1234\n' | bf7-keyinj           # ввести и подтвердить
```

Телефон разблокировался с первого раза.

Ещё замечено: `phosh --unlocked` снимает замок только при запуске; после
гашения экрана он возвращается. `org.gnome.ScreenSaver.SetActive false` через
шину не помогает — `GetActive` остаётся `true`. И `lock-enabled` в
`org.gnome.desktop.screensaver` стоит `false`, а пароль всё равно спрашивают.

Свежее phoc в Debian 12 не предлагают (0.24.0-1 и установлен, и кандидат), а
поднимать wlroots нельзя: на 0.15 завязаны наши правки EGL.

### Заодно убрана вторая клавиатура

На экране жили сразу две: `squeekboard` от gnome-session (родная для Phosh) и
`onboard`, который запускал наш `bf7-keyboard.desktop` — наследие эпохи Xfce.
83 МиБ впустую. `onboard-autostart.desktop` из пакета сам бы не запустился, у
него `NotShowIn=Phosh;GNOME;`; запускал именно наш файл. Отставлен.

### Мелочи, которые стоят времени

* Панель гасится композитором на уровне KMS (`/sys/class/drm/card1-DSI-1/enabled`
  становится `disabled`), **а подсветка остаётся гореть**. Это впустую жжёт
  батарею; отдельный пункт в доводку.
* `bf7-touchinj` каждый запуск заводит новый узел uinput, и первые события
  композитор теряет, пока добавляет устройство. Одиночное нажатие может не
  дойти — просить сразу три.
* Снимок экрана: `grim` отвечает `failed to copy output DSI-1`, когда панель
  погашена. Это не поломка захвата, а спящий экран — сначала разбудить.

## 2026-09-22 08:09 MSK — Экран блокировки починен. И прежний мой вывод был неверен

Утром я записал, что «phoc не доставляет касания на слоевую поверхность, пока в
фокусе другая слоевая». **Это неправда.** Вывод был построен на совпадении
опытов, а не на измерении. Настоящая причина другая и лежит на три шага раньше.

### Что показали приборы

Собрал phoc 0.24 из исходников Debian с отладочным выводом в трёх точках:
на входе в обработчик нажатия, в поиске поверхности и на самой доставке.

Первое же измерение опровергло догадку. Касание **доходит** до слоевой
поверхности, даже когда в фокусе другая слоевая:

```
BF7HIT:  проход1 слой=3 имя=osk зона=210 geo=0,596 360x210 точка=18,622
BF7TOUCH: нажатие 18,622 ... исключительный=(nil) пускаем=1
BF7DOWN: отдаю клиенту sx=18.0 sy=26.5 id=0
BF7UP:   отпускание id=0 точка_до=0x… точка_после=0x…
```

А `WAYLAND_DEBUG=1` у оболочки показал, что до неё доходит безупречная
последовательность:

```
wl_touch@9.down(24, …, wl_surface@69, 0, 94.00000000, 343.50000000)
wl_touch@9.frame()
wl_touch@9.up(25, …, 0)
wl_touch@9.frame()
```

Заодно выяснилось, что экран у нас логически **360×806**: панель идёт с
удвоением, и координаты касания делятся пополам. Это не ошибка, а масштаб.

### Настоящая причина

В `handle_touch_down` (src/seat.c):

```c
PhocOutput *output = g_hash_table_lookup (desktop->input_output_map,
                                          event->device->name);

if (output && !output->wlr_output->enabled) {
  g_debug ("Touch event ignored since output '%s' is disabled.", …);
  return;                       /* выход ДО сброса простоя */
}
wlr_idle_notify_activity (desktop->idle, cursor->seat->seat);
phoc_cursor_handle_touch_down (cursor, event);
```

Тачскрин привязывается к встроенному выводу **всегда**: если привязки нет в
настройках, берётся `phoc_desktop_get_builtin_output()`. Значит, при погашенном
экране условие срабатывает на каждое касание — и выход происходит раньше, чем
`wlr_idle_notify_activity`. Палец не считается действием пользователя.

**Разбудить телефон касанием было невозможно в принципе.** Сколько ни жми —
экран остаётся чёрным, и каждое нажатие уходит в никуда:

```
BF7SEAT: событие от «bf7-virt-touch» вывод=DSI-1 включён=0
BF7SEAT: касание выброшено — вывод «DSI-1» выключен
(четыре раза, экран так и остался погашен)
```

Отсюда же росло наблюдение «клавиатура проходит там, где не проходит касание»:
у клавиатурных событий своя дорога, этого заслона на ней нет. А экран
блокировки гаснет быстро — поэтому он и выглядел «глотающим нажатия».

### Починка

Сброс простоя перенесён до проверки вывода. Само касание клиенту при
выключенном выводе по-прежнему не отдаём: первое нажатие будит экран и ничего
не нажимает, как и положено телефону в кармане.

Заплатка — `hybris/bf7-patches/phoc-0.24-touch-wake.patch`, двоичный файл
собран на телефоне и лежит в `/usr/local/bin/bf7-phoc`; `bf7-phosh.service`
переведена на него.

### Проверено в обе стороны

С пакетным phoc: экран до касаний `disabled`, четыре нажатия, экран после
касаний `disabled`, ввода пароля нет.

С исправленным: `disabled` → касание → `enabled`, затем свайп, 1-2-3-4,
«Unlock» — **телефон разблокирован одними касаниями**. Повторено трижды,
длительность нажатия 90 и 110 мс, снимок каждый раз побитово совпал с эталоном
разблокированного экрана.

Оболочку пересобирать не потребовалось: работает обычная пакетная phosh
0.24.0-1. Я успел заподозрить и её, собрал с приборами, получил рабочий
результат — и поставил контрольный опыт с пакетной. Она тоже разблокировалась.
Значит, дело было только в phoc. Без этого контроля я бы приписал починку не
тому.

### Что осталось

`squeekboard` при открытой оболочке по-прежнему не набирает в её поле поиска,
хотя касание до клавиатуры доходит (раскладка по «123» меняется) и поверх
обычного окна она работает. Это отдельная поломка, не связанная с этой.

## 2026-09-22 08:22 MSK — Загрузка 110 секунд вместо 255; две поправки к себе

Перезагрузка после всех правок, замер по часам ПК от `systemctl reboot` до
ответа ssh: **110 секунд** против прежних 255. Циклов в порядке запуска ноль,
удалённых заданий ноль, упавших служб нет, самопроверка 38 из 38.

### Поправка первая: помощник отметки слота был на месте

Утром я записал, что `/bin/bf7-mark-boot-success` отсутствует и потому
`bf7-boot-ok` была бы пустышкой. Я искал в `/bin`, `/usr/local/bin` и `/sbin`,
но не в `/usr/local/sbin` — а он лежал именно там, собранный ELF от 01:42.
Установленный `bf7-boot-ok` звал его по этому пути, репозиторный — по `/bin`.

Толку от него, впрочем, не было: он отказывался работать со словами

    refusing: BCB does not describe active slot B

и служба падала с кодом 12, оставляя систему в состоянии `degraded`. Разбор
метки у него неверен. Заменён на наш `bf7-mark-boot-success` на Python —
тот проверяет магию и контрольную сумму, пишет ровно 32 байта и перечитывает
записанное. Старый отложен в `/root/backup/`.

Пути в репозитории и на телефоне сведены к `/usr/local/sbin/`.

### Поправка вторая: `bf7-boot-ok` держала загрузку три минуты

Та же ошибка, что была у `bf7-wifi-watch`: `Type=oneshot` при скрипте, который
выжидает 180 секунд. Всё это время `systemctl is-system-running` отвечает
`starting`, и настоящую поломку в таком состоянии легко проглядеть. Переведена
на `Type=simple`.

Правило, которое стоит запомнить: **`Type=oneshot` годится только тому, кто
завершается быстро**. Всё, что ждёт события или времени, должно быть `simple`,
иначе оно держит свою цель, а вместе с ней и всё, что за ней стоит.

## 2026-09-22 08:47 MSK — Клавиатура набирает, но набранного не видно

Оставшийся пункт: `squeekboard` вроде бы не печатает. Прошёл цепочку до конца,
ставя приборы на каждом переходе. Итог неожиданный: **буква доходит до поля
ввода и там остаётся**. Не работает то, что после.

Цепочка, каждое звено измерено:

1. **Касание доходит до клавиатуры.** phoc с прибором:
   `BF7DOWN: отдаю клиенту sx=18.0 sy=26.5 id=0` — это клавиша «q».
2. **Клавиатура посылает вставку.** `WAYLAND_DEBUG=1` у её процесса:
   `-> zwp_input_method_v2@29.commit_string("q")`, следом `commit(2)`.
3. **phoc принимает и передаёт.** Прибор в `handle_im_commit`:
   `BF7IM: вставка принята` — и в боевой сборке тоже, не только в отладочной.
4. **GTK получает и применяет.** Собрал отдельно модуль
   `im-wayland.so` из исходников той же версии (3.24.38-2~deb12u3) с выводом:

   ```
   BF7GTK: done(2), свой серийный 2, ожидающая вставка «q»
   BF7GTK: done(2) до применения: текст=«» удалить(0,0) вставить=«q»
   BF7GTK: применяю «q» к виджету GtkSearchEntry (0x…), слушателей 1
   BF7GTK: done(2) после применения: текст=«q» отображён=1 видим=1
   ```

То есть поле поиска оболочки **содержит «q»**, виджет отображён и видим, и
никакое следующее событие букву не удаляет — событие `done` было ровно одно.

А на экране поле пустое, и список приложений не отфильтрован. При этом
подсказка «Search apps…» из поля **исчезла** — значит, виджет перерисовался и
про текст знает. Не нарисован именно глиф.

Отсюда граница: ввод в порядке от касания до содержимого поля; ломается
отрисовка набранного и отклик оболочки на него. Это уже не клавиатура и не
композитор.

### Побочно проверенное и отвергнутое

* `keyboard_interactive` у поверхности клавиатуры равен 0 — фокус на неё не
  переносится, догадка «клавиатура крадёт фокус» неверна.
* `im-wayland.so` на месте и цел; я сперва решил, что его нет, потому что
  `ls | head` обрезал список по алфавиту — модуль идёт после `im-ti-et.so`.
* `GTK_IM_MODULE=wayland` и `QT_IM_MODULE=wayland` ничего не меняют.
* Длительность нажатия (110, 300, 600 мс) ничего не меняет.
* Не меняет и `gnome-text-editor` вместо оболочки — там то же самое.

### Ловушка в собственном пробнике

`bf7-touchinj` заводил **новый узел uinput на каждый запуск**. Для композитора
это появление и исчезновение устройства: пока он его добавляет, первые события
теряются, а удаление узла способно оборвать ещё не обработанное касание. Палец
у хозяина — одно постоянное устройство, и проверять надо так же.

Добавлен режим потока: одно устройство на всю череду нажатий, координаты
строками со входа.

```sh
printf '36 1245\n108 1245\n' | bf7-touchinj поток
```

Задержка перед уничтожением узла поднята с 300 мс до полутора секунд.

### Как собрать отдельно модуль ввода GTK3

Пригодится ещё не раз — пересобирать всю libgtk-3 не нужно:

```sh
apt-get source libgtk-3-0
PROTO=$(pkg-config --variable=pkgdatadir wayland-protocols)
wayland-scanner client-header "$PROTO/unstable/text-input/text-input-unstable-v3.xml" \
    text-input-unstable-v3-client-protocol.h
wayland-scanner private-code  "$PROTO/unstable/text-input/text-input-unstable-v3.xml" \
    text-input-unstable-v3-protocol.c
printf '#define GETTEXT_PACKAGE "gtk30"\n#define GTK_LOCALEDIR "/usr/share/locale"\n' > config.h
gcc -shared -fPIC -O2 -o im-wayland.so imwayland.c text-input-unstable-v3-protocol.c \
    -I. -I../gtk+3.0-3.24.38 \
    $(pkg-config --cflags --libs gtk+-3.0 gtk+-wayland-3.0 wayland-client)
```

Пакетный модуль после опытов возвращён на место, `dpkg -V libgtk-3-0` чист.

## 2026-09-22 08:59 MSK — Браузер: причина синего экрана найдена, починка не здесь

Epiphany открывался пустым синим окном. Причина видна в его выводе сразу:

```
libEGL warning: MESA-LOADER: failed to open pvr: /usr/lib/dri/pvr_dri.so:
    cannot open shared object file
Could not create GBM EGL display: EGL_NOT_INITIALIZED. Aborting...
```

WebKit заводит себе **отдельный** дисплей EGL вызовом
`eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, устройство_gbm, ...)`. Наш
hybris-EGL платформенного вызова не знает и поставщику glvnd о поддержке GBM не
заявляет. glvnd отдаёт вызов мешовскому поставщику, тот честно пытается
загрузить драйвер по имени чипа — `pvr_dri.so`, которого нет и быть не может, —
и возвращает отказ. Браузер после этого завершается.

Выключатели WebKit разбирают картину по частям:

* `WEBKIT_DMABUF_RENDERER_DISABLE_GBM=1` — отказа больше нет, основной процесс
  живёт, но процесс отрисовки падает по кругу;
* `WEBKIT_DMABUF_RENDERER_FORCE_SHM=1` сам по себе не помогает — GBM всё равно
  пробуется первым;
* песочница ни при чём: с `WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS=1` та же
  ошибка (а `WEBKIT_FORCE_SANDBOX` в 2.50 уже ничего не выключает и честно об
  этом пишет).

### Почему прослойкой это не чинится

Я добавил в `libbf7eglfilter.so` перехват `eglGetPlatformDisplay`,
`eglGetPlatformDisplayEXT` и даже `eglGetProcAddress`, чтобы для платформы GBM
отдавать обычный `eglGetDisplay(EGL_DEFAULT_DISPLAY)`. **Не сработало, и
отладочный вывод показал почему: подмена не вызывается ни разу.** WebKit ходит
за функциями EGL через libepoxy, а та ищет их прямым `dlsym` по собственной
ручке библиотеки — мимо `LD_PRELOAD`. Правка откачена, в системе не осталось.

Правильное место — **поставщик glvnd в самой libhybris**
(`hybris/egl/glvnd/eglglvnd.cpp`): он должен заявлять поддержку
`EGL_PLATFORM_GBM_KHR` и обслуживать платформенный вызов. Тогда glvnd
перестанет отдавать его Mesa. Это следующий заход, не сделано.

Попутно: у окна браузера при открытии отрицательная ширина виджета
(`attempt to allocate widget with width -29`) и следом `pixman_region32_init_rect:
Invalid rectangle passed`. Это не причина синего экрана, но тоже стоит внимания.

## 2026-09-22 09:08 MSK — Браузер заработал. Чинить надо было в libhybris

Продолжение предыдущей записи. Место починки нашлось точно там, куда указывала
трасса, — в `__eglHybrisGetPlatformDisplayCommon`:

```c
if (ws_init(hybris_ws) == EGL_FALSE) { // Other ws already loaded.
	__eglHybrisSetError(EGL_BAD_PARAMETER);
	return EGL_NO_DISPLAY;
}
```

Модуль оконной системы в процессе **один**: какой загрузили первым, тот и
стоит, а `ws_init` на другое имя отвечает отказом. Для платформы GBM мы просим
сквозной `"null"`. И вот порядок событий у браузера:

1. GTK заводит дисплей для окна — платформа `0x31d8` (Wayland), модуль
   `"wayland"` загружен.
2. WebKit просит **второй** дисплей — платформа `0x31d7` (GBM), нужен модуль
   `"null"`.
3. `ws_init("null")` отказывает, весь запрос проваливается наружу как
   `EGL_NOT_INITIALIZED`, браузер завершается.

Это и есть то давнее «WebKit не умеет создать второй дисплей EGL», записанное
когда-то без объяснения.

Окна безоконной платформе не нужны: ей хватает контекста без поверхности и
ввоза кадра через `EGL_EXT_image_dma_buf_import`. Значит, **любой уже
загруженный модуль её устроит**. Так и сделано — для GBM и
`EGL_PLATFORM_SURFACELESS_MESA` при отказе берётся тот модуль, что стоит.
Трасса подтверждает:

```
[hybris] getPlatformDisplay: платформа 0x31d7, ws "null"
[hybris]   ws_init("null") отказал
[hybris]   беру уже загруженный ws "wayland"
[hybris]   андроидный eglGetDisplay -> 0x1
```

Отказ исчез, основной процесс браузера живёт.

### Вторая половина: песочница

Процесс отрисовки WebKit после этого всё равно падал по кругу — его прячет
bubblewrap, а она закрывает от него `/vendor`, `/apex` и `/usr/local`, то есть
весь наш слой EGL. С `WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS=1` процессы
живут, окно рисуется: адресная строка, вкладки, телефонный вид.

Песочницу снимаю **только для браузера**, отдельным запуском
`/usr/local/bin/bf7-web` и своим `bf7-web.desktop`, а не для всего сеанса.
Уступка в нашем случае невелика — сеанс и так идёт от root, — но она названа и
обратима: убрать строку из запуска.

Попутно замечено и не чинено: при открытии окна браузера GTK жалуется на
отрицательную ширину виджета (`width -29`) и следом `pixman_region32_init_rect:
Invalid rectangle passed`. На работу не влияет.

Самопроверка после всех правок: 38 из 38.

## 2026-09-22 09:18 MSK — Поле поиска не перерисовывается. Ввод тут ни при чём

Продолжение разбора клавиатуры. Довёл до конца и нашёл, где именно обрывается
цепочка. Всё измерено собственным модулем `im-wayland.so`, собранным из
исходников той же версии.

**Ввод работает полностью.** Буква доходит до буфера поля и там остаётся:

```
BF7GTK: done(2) до применения: текст=«» удалить(0,0) вставить=«q»
BF7GTK: применяю «q» к виджету GtkSearchEntry, слушателей 1
BF7GTK: после применения текст=«q»
BF7GTK: через секунду: текст=«q» отображён=1
```

Через секунду текст на месте — **никто его не стирает**.

**Поле то самое, что на экране.** Положение в окне 16,46 при окне 360×774;
окно `PhoshHome` стоит на 32 логических точки ниже верха, то есть строка поиска
приходится на 78 логических (156 физических) — ровно туда, где она и видна.
Фокус на ней, `has_focus=1`, непрозрачность 1.00, цвет текста белый
(1.00/1.00/1.00, alpha 1.00), `gtk_entry_get_visibility` = 1.

**И всё же не рисуется.** Задал текст прямо из модуля —
`gtk_entry_set_text(w, "ZZZ")` плюс `gtk_widget_queue_draw(w)`: буфер стал
«ZZZ», на экране по-прежнему пусто.

**Причина найдена: виджет не перерисовывается вовсе.** Повесил обработчик на
сигнал `draw` этого поля — за всё время опытов он **не сработал ни разу**:

```
BF7GTK: слежу за отрисовкой поля
(и больше ничего)
```

Ни после ввода, ни после прямой задачи текста, ни после касания в другом месте
окна.

Клавиатура «железная» (`bf7-keyinj`) даёт ровно тот же итог — значит, дело не в
экранной клавиатуре и не в протоколе ввода. Оба пути кладут букву в буфер, и
оба упираются в то, что окно оболочки не перерисовывается.

**Поправка к самому себе, тут же.** Вывод «окно не перерисовывается» я сделал
из молчания обработчика — и он неверен. Проверка прокруткой списка сразу после
ввода: снимок меняется, то есть **окно перерисовывается исправно**. Значит,
обработчик молчал не потому, что отрисовки нет, а потому, что висел не на том
виджете.

А это разворачивает картину: **поле, в которое попадает буква, и поле, которое
мы видим, — по всем признакам разные экземпляры**. Совпадают и окно
(`PhoshHome`), и положение (16,46), и состояние — но видимое поле рисуется
пустым, а у измеренного в буфере «q», и прямая `gtk_entry_set_text` на нём
ничего не меняет на экране.

**Граница на сегодня:** ввод исправен от касания до буфера виджета; дальше
буква попадает в экземпляр поля, который на экран не выводится. Следующий
заход — искать второй `GtkSearchEntry` в дереве виджетов оболочки и понять,
почему фокус ввода достаётся ему.

Пакетный модуль после опытов возвращён, `dpkg -V libgtk-3-0` чист,
самопроверка 38 из 38.

## 2026-09-22 09:23 MSK — Поле поиска: двойника нет, раскладка верна, глиф не доезжает

Догадку про два экземпляра поля проверил обходом дерева виджетов из своего
модуля — и она **не подтвердилась**. Поле в окне ровно одно:

```
BF7TREE: обхожу дерево окна
BF7TREE: GtkSearchEntry 0x… в 16,46 отображён=1 видим=1 текст=«q»
```

Больше того, GTK знает про букву всё, что нужно для отрисовки:

```
раскладка «q» 9x18 смещение 37,11 xalign=0.00
```

То есть pango-раскладка содержит «q», её размер 9×18, и рисоваться она должна
в точке (37,11) внутри поля. Поле стоит в (16,46), окно `PhoshHome` сдвинуто на
32 логических точки вниз — итого глиф приходится на (53,89) логических,
(106,178) физических. **Ровно туда, где на снимке стоит курсор и пусто.**

Запуск с `PHOC_DEBUG=damage-tracking` подсветки повреждённых областей в снимке
не дал — то ли флаг в 0.24 рисует иначе, то ли захват берёт буфер до наложения.
Вывод отсюда не делаю.

**Что установлено твёрдо.** Ввод исправен целиком: от касания до
pango-раскладки включительно. Поле одно, оно видимое, цвет белый, положение
верное, размер глифа посчитан. На экран глиф не попадает. Клавиатура
«железная» даёт то же самое.

Следующий заход — смотреть, объявляет ли оболочка повреждение этой области
(`wl_surface.damage`) и доезжает ли оно до нашей панели. Это уже про
отслеживание повреждений, то есть про наш слой, а не про ввод.

## 2026-09-22 09:25 MSK — Домашняя поверхность оболочки не перерисовывается. Точка

Довёл клавиатуру до конца. Протокольный вывод пришлось писать в файл, а не в
журнал: при `WAYLAND_DEBUG=1` его глотает ограничитель скорости journald, и
подсчёты по журналу давали обманчивые нули.

Номера поверхностей в сеансе:

```
wl_surface@36 — "osk"              (клавиатура)
wl_surface@50 — "phosh top-panel"  (верхняя строка)
wl_surface@54 — "phosh home"       (оболочка, здесь поле поиска)
wl_surface@60 — "phosh background"
```

Что происходит после нажатия клавиши (разница файла до и после):

```
zwp_input_method_v2@29.commit_string("q")
zwp_input_method_v2@29.commit(2)
 -> zwp_text_input_v3@62.commit_string("q")
zwp_text_input_v3@62.commit()
wl_surface@36.attach(wl_buffer@39, 0, 0)
wl_surface@36.damage(0, 0, 360, 210)
wl_surface@36.commit()
```

**Коммитит только клавиатура.** Домашняя поверхность `wl_surface@54` не
прикрепляет буфер, не объявляет повреждения и не коммитит — ни разу. За весь
отрезок счёт коммитов: `@36` — 11, `@60` — 4, `@73` — 3, у `@54` — ноль.

А мелкие повреждения вроде `(102,93,72,27)`, которые я поначалу принял за
признак живой отрисовки, принадлежат `@50` — это тикающие часы верхней панели.
Потому часы и идут, а поле поиска стоит.

### Итог по всей цепочке

Исправно: касание → клавиатура → `commit_string` → phoc → `zwp_text_input_v3`
→ GTK → буфер поля → pango-раскладка «q» 9×18 с верным смещением.

Сломано: GTK внутри оболочки **не перерисовывает своё окно**. Проверено с трёх
сторон: сигнал `draw` у поля не срабатывает ни разу; явный
`gtk_widget_queue_draw` ничего не даёт; поверхность не коммитится по протоколу.
С «железной» клавиатурой ровно то же самое.

Это окно — слоевая поверхность (`zwlr_layer_shell_v1`), а такие окна phosh
создаёт через `gdk_wayland_window_set_use_custom_surface`. Туда и копать
дальше: почему у этого окна не идут кадровые часы GDK, когда меняется только
содержимое виджета. Ввод, композитор и наш слой EGL к этому отношения не имеют
— их части цепочки измерены и работают.

### Довесок: принудительная перерисовка тоже не помогает

Последняя проба из того же модуля: после вставки буквы беру у окна оболочки
`GdkWindow` и бужу отрисовку насильно —

```c
gdk_window_invalidate_rect (gw, NULL, TRUE);
gdk_window_process_updates (gw, TRUE);
```

`gdk_window_is_visible` отвечает 1, обе строки отрабатывают — и на экране
по-прежнему ничего. Значит, окно, которое отдаёт `gtk_widget_get_window`, до
поверхности `wl_surface@54` не доходит: рисование этого окна обрывается раньше
протокола.

Дальше без правки самой phosh не продвинуться, а это уже отдельная работа.
Граница названа точно, все измерения записаны, система возвращена в исходное:
пакетный `im-wayland.so` на месте (`dpkg -V libgtk-3-0` чист), подмен в `/run`
нет, `phoc 0.24.0-1+bf7.1`, самопроверка 38 из 38.

## 2026-09-22 09:31 MSK — Подсветка горела при погашенном экране

Замер, с которого всё началось:

```
экран включён:  соединитель=enabled  подсветка=53 светодиод=53 bl_power=0
экран погашен:  соединитель=disabled подсветка=53 светодиод=53 bl_power=0
```

Композитор выключает вывод на уровне KMS, панель не показывает ничего — а
светодиод подсветки продолжает питаться на той яркости, которую выставил
хозяин. Всё время сна.

`bf7-blank` научился вести подсветку за выводом. Цикл переписан с блокирующего
чтения кнопки на `poll` с таймаутом в полсекунды: состояние соединителя в sysfs
пробуждать `poll` не умеет, поэтому опрашиваем.

Две тонкости, обе вылезли на проверке:

1. **Одного гашения мало.** `gsd-power` приглушает подсветку уже *после* того,
   как погасил вывод, и оставляет её на двойке. Поэтому проверка сделана
   самоисправляющейся: пока вывод выключен и яркость отлична от нуля — гасим.
   В покое это одно чтение файла в полсекунды, записей нет.
2. **Нельзя запоминать приглушённое значение.** Сначала я сохранял яркость в
   момент гашения — и получал после пробуждения еле живой экран, потому что к
   тому мигу `gsd-power` уже приглушил до двойки. Теперь запоминаем последнюю
   яркость, пока вывод включён, и только если она не ниже пяти: всё, что ниже,
   считаем приглушением, а не выбором хозяина.

Проверено: выставил 120 (читается 121) → погасил → светодиод **0**, держится →
разбудил касанием → **121**.

## 2026-09-22 09:35 MSK — systemd-networkd отключён: он ничем не управлял

`networkctl list` показывал все связи как `unmanaged`, а `/etc/systemd/network/`
пуст — ни одного файла сети. При этом networkd исправно дёргал `wlan0`
(«Lost carrier / Gained carrier» в журнале) и мешался под ногами у
NetworkManager.

Проверял осторожно, потому что `rndis0` — единственный надёжный канал к
телефону. Сначала остановил со сторожем (`setsid sh -c "sleep 40; systemctl
start systemd-networkd"`), чтобы вернуть даже при потере связи. Адрес
`192.168.7.2/24` остался на месте, Wi-Fi не шелохнулся.

Отключён насовсем вместе с `systemd-networkd.socket` и
`systemd-networkd-wait-online`. Перезагрузка подтвердила: USB-канал поднялся,
адрес тот же, ноль циклов, ноль упавших, самопроверка 38 из 38, загрузка 99 с.

На будущее: запасной путь к телефону по Wi-Fi прописан как `bf7w`
(192.168.27.52) — если USB-канал когда-нибудь отвалится, есть чем достучаться.

## 2026-09-22 09:44 MSK — Загрузка: графика перестала ждать сеть и андроидные службы

Замерил, когда на экране появляется сеанс, и взялся за то, что его держит.

**Первое: `bf7-wifi` держал всю загрузку.** Служба была `Type=oneshot`, а её
скрипт пишет в `/dev/wmtWifi`, и запись блокируется, пока драйвер не поднимет
`wlan0` — на нашем железе тридцать секунд. Всё это время не достигалась
`multi-user.target`, а за ней и `graphical.target`. Замер: графика на **79-й**
секунде. Переведена на `Type=simple`; доведением Wi-Fi и так занимается
`bf7-wifi-watch`. Стало **72 с**.

В журнале ядра заодно видна причина самих тридцати секунд:

```
opfunc_pwr_on(1197): WMT-CORE: already powered on, WMT DRV_STS_[0x2]
mtk_wcn_wmt_pwr_on: OPID(1) type(4) fail
opfunc_wlan_probe(3467): not implemented yet hifType: 0x2, unspecified wifi_hif
osal_wait_for_signal_timeout: wait completion timeout ... duration:4057ms
```

Чип к этому мигу уже включён вендорским `wmt_launcher -o 1`, повторное
включение отвечает отказом, и ядро печатает трассы. Не трогал: работает.

**Второе: неподвижные выдержки в `hal-start.sh`.** На каждую андроидную службу
там стояли `sleep 3` и `sleep 5` — восемнадцать секунд ожидания вслепую.
Заменено ожиданием по факту: опрос раз в пятую долю секунды плюс короткая
выдержка на регистрацию. Службы стали подниматься за 1,8 с вместо 6,2, а вся
служба укладывается в 11,6 с вместо 18–27.

Тут я снова наступил на кириллицу — на этот раз в имени функции: скрипт
отвалился с `status=1`. Имена только латиницей, сколько можно.

**Третье: `udisks2`** — управление дисками для рабочего стола, на телефоне
незачем, а держал 27 секунд. Отключён.

Итог по замерам: ssh отвечает через **90 секунд** вместо 99, сеанс на экране —
через **71,6** вместо 79. От исходных 255 секунд это в два с половиной раза
быстрее. Ноль циклов, ноль упавших, самопроверка 38 из 38.

Осталось: пятёрка служб по-прежнему заканчивает разом около 71-й секунды —
похоже, все упираются в нехватку процессора при загрузке, а не в собственные
задержки. Это отдельный заход.

## 2026-09-22 09:55 MSK — Экран появляется на 56-й секунде вместо 79-й

Чтобы понять, почему пять служб заканчивали разом, поставил временный
замерщик: раз в две секунды писал в файл нагрузку и первую шестёрку по
процессору. Перезагрузился, прочитал — и всё стало ясно:

```
=== 45.24 c ===  нагрузка 11.38
  94.9 insmod
  69.6 mtk_wmtd
  46.0 systemd
=== 51.49 c ===
  44.5 mtk_wmtd
  40.4 systemd
  29.2 mtk_wmtd_worker
```

С 39-й по 65-ю секунду процессор занят почти целиком: сначала `insmod`
драйвера wlan (до 95%), потом нити связности MediaTek. Всё, что стартует в
этом окне, стоит в очереди за ними — отсюда и четыре службы, начавшиеся в 39-ю
и закончившиеся в 65-ю секунду разом.

Вывод простой: **экрану телефона незачем делить процессор с подъёмом Wi-Fi**.
`bf7-wifi` перенесён за сеанс, следом за ним и `bf7-wifi-watch`.

### Кольцо, которое я сам и завёл

Сначала написал `After=graphical.target` при `WantedBy=graphical.target` — и
получил ровно то, что чинил утром: цикл, молча выкинутое задание и сторож
Wi-Fi, который не запускался.

```
graphical.target: Found ordering cycle on bf7-wifi-watch.service/start
graphical.target: Found dependency on bf7-wifi.service/start
graphical.target: Found dependency on graphical.target/start
```

Цель тянет службу, служба ждёт цель. Правильно ждать не цель, а саму оболочку:
`After=bf7-phosh.service`. После этого ноль циклов и ноль удалённых заданий.

### Итог замеров

| когда | ssh отвечает | сеанс на экране |
|---|---|---|
| было ночью | 255 с | — |
| после циклов и лишних служб | 99 с | 79 с |
| после `bf7-wifi` Type=simple и правки выдержек | 90 с | 71,6 с |
| после переноса Wi-Fi за сеанс | **77 с** | **56,2 с** |

Ноль циклов, ноль упавших, Wi-Fi подключается, самопроверка 38 из 38.

### Довесок 10:02: где загрузка перестала поддаваться

Дальше пробовал ещё три правки и измерял каждую.

**Помогло:** `bf7-devnodes-all` при `devtmpfs` больше не обходит `/sys`
целиком. Обход стоил **5541 мс** на критическом пути и находил ноль пропаж —
узлы заводит ядро. Теперь сперва смотрим `/proc/mounts`, и если `/dev` на
devtmpfs, остаётся только дешёвая заготовка петель: **386 мс**. (И снова
кириллица в `/bin/sh` — поймал сразу, но написал-то опять.)

**Не помогло, хотя логически верно:** снял звуковой сервер из `After=` у
сеанса (он поднимается к 52-й секунде, а графике незачем его ждать) и перенёс
камеру за сеанс (`bf7-camd` тринадцать секунд ждал приёмник, держа цель). Обе
правки оставлены — они правильные, — но замер не сдвинулся: 56,4 → 58,3 → 57,0
секунды, то есть в пределах разброса между загрузками.

Значит, дальше упирается не в порядок, а в саму работу: ядро 18 секунд,
пространство пользователя 40, и в нём толчея за процессор. Следующий заход
лежит в другой плоскости — обрезать модули ядра и удешевить подъём андроидных
служб. Это отдельная работа.

**Итог по замерам:** ssh отвечает через **76 секунд** вместо 255 исходных,
сеанс на экране — через **57** вместо 79. Ноль циклов, ноль упавших,
самопроверка 38 из 38.

## 2026-09-22 10:06 MSK — Убраны службы, которых телефону не нужно

Оболочка тянула за собой полный набор служб настроек GNOME — девятнадцать
процессов, — а с ними остатки прежних заходов: демон настроек Xfce, хранитель
экрана из иксов, апплет печати, демон геолокации и `onboard`, которого мы уже
отставили.

Телефону не нужны планшеты (Wacom), смарт-карты, принтеры, обмен файлами,
защита USB (у нас по USB идёт единственный надёжный канал) и модем — модемов в
системе нет, `ModemManager` отключён по той же причине.

**Первая попытка была неверной.** Я подумал, что службы задаёт состав сеанса, и
положил свой `phosh.session` в `/usr/local/share/gnome-session/sessions/`,
задав `XDG_DATA_DIRS`. Счёт процессов не изменился: **их поднимает обычный
автозапуск** `/etc/xdg/autostart/`, а не список `RequiredComponents`. Свой
состав сеанса оставлен — он верен и не мешает, — но работает не он.

Правильный путь: положить рядом свои файлы в `~/.config/autostart` с
`Hidden=true`. Пакетные при этом не тронуты, вернуть любую службу — убрать один
файл. Сделано скриптом `bf7-trim-autostart`, погашено одиннадцать записей.

Итог замеров после перезагрузки:

| | было | стало |
|---|---|---|
| служб настроек | 19 | 12 |
| занято памяти | 480 МиБ | **311 МиБ** |
| сеанс на экране | 57 с | **54,8 с** |
| ssh отвечает | 76 с | **73 с** |

Ноль циклов, ноль упавших, самопроверка 38 из 38, экран снят и цел.

## 2026-09-22 10:07 MSK — Установщик снова знает всё

За ночь и утро прибавилось довольно много такого, чего `bf7-install.sh` не
ставил: помощник отметки слота, ожидание приёмника камеры, гашение автозапуска,
запуск браузера с его ярлыком, свой состав сеанса, пределы ожидания для
oneshot-служб и обновлённый обход узлов.

Это опасная дыра: при пересборке телефона всё перечисленное молча не встало бы,
а выглядело бы как отдельные поломки — ровно то, ради чего установщик и писался
после происшествия с файловой системой.

Дополнен. Проверка: разложил репозиторий на телефон и запустил — установщик
прошёл все разделы и доложил «всё уже на месте», то есть разложенное вручную
за эти часы совпадает с тем, что он поставит с нуля. Самопроверка 38 из 38.

## 2026-09-22 10:21 MSK — Bluetooth: собрался, загрузился, hci0 есть; упирается в прошивку

Взялся за Bluetooth и прошёл его дальше, чем когда-либо. Сразу поправка к себе:
я записал когда-то, что драйвер не собирается, потому что в нашем `wmt_exp.h`
нет `ENUM_WMTMSG_TYPE_T`. **Это неправда.** Тип есть, и в доноре, и у нас.

Настоящая причина одна и простая: в `stp_exp.h` объявления типов стоят под
`#ifdef MTK_WCN_WMT_STP_EXP_SYMBOL_ABSTRACT`, а без этого макроса компилятор
уходит в ветку `#else`, которая теми же типами и пользуется, — отсюда два
десятка «unknown type name». Драйвер связности определяет тот же макрос у себя
(`src/common/Kbuild`, `src/adaptor/Makefile`), а сборка Bluetooth — нет.
Одна строка `ccflags-y += -DMTK_WCN_WMT_STP_EXP_SYMBOL_ABSTRACT`, и модуль
собрался.

### Вторая ловушка: лишний плюс в версии

`bt_drv.ko` вышел с `vermagic ...-v1+`, а ядро на телефоне без плюса — модуль
не загрузился бы. Плюс приписывает `setlocalversion` грязному дереву. Пустой
`.scmversion` уже лежал, но `include/generated/utsrelease.h` и
`include/config/kernel.release` были сгенерированы раньше и хранили плюс.
Поправил оба — версия совпала.

### Что заработало

* `bt_drv.ko` загружается без ошибок, регистрирует `192 mtk_stp_bt_chrdev`;
* узел `/dev/stpbt` создаётся и **открывается** — значит, драйвер живой;
* в ядре не было виртуального HCI и профилей, но всё это собирается **модулями
  против уже прошитого ядра**, перепрошивка не нужна: `CONFIG_BT_HCIVHCI=m`,
  `CONFIG_BT_RFCOMM=m`, `CONFIG_BT_HIDP=m` — тот же приём, что с подсветкой;
* написан мост `bf7-btbridge`: перекладывает пакеты H4 между `/dev/stpbt` и
  `/dev/vhci`. Драйвер MediaTek отдаёт HCI ровно в том виде, в каком его ждёт
  виртуальный транспорт, так что нужен не драйвер, а перекладчик;
* **`hci0` появляется** и виден `hciconfig`: `Type: Primary Bus: Virtual`.

### Где упёрлось

Стек шлёт первую команду (TX 12 байт, 2 команды), ответа нет, `hciconfig hci0
up` отвечает «Connection timed out». В журнале ядра видно почему:

```
[HIF-SDIO][E]wmt_lib_put_act_op(1674):opId(41) completion timeout
[STPDbg]stp_dbg_soc_paged_dump: get consys dump num packet_num(25)
exception type: ASSERT;;[CONNSYS]core dump start ...
```

**Прошивка связности сваливается в исключение при включении функции BT.** Чип и
базовая прошивка исправны — Wi-Fi на них работает и пережил это падение, — но
BT требует своей подготовки, которую в Android делает `libbt-vendor`: загрузку
патча и настройку через WMT. Это следующий заход.

Телефон после опытов перезагружен и чист: самопроверка 38 из 38, Wi-Fi
подключается.

### Довесок 10:22: прошивка включается чисто, молчит уже HCI

Повторил с чистой загрузки — и **исключения не было**:

```
[MTK-BT]BT_open: WMT turn on BT OK!
[MTK-BT]BT_open: Now it's in MTK Bluetooth Mode
[MTK-BT]BT_open: STP is ready!
```

Значит, прежний `ASSERT` был от грязного состояния чипа после моих же опытов, а
не от нехватки прошивки. Патч BT на месте: `/vendor/firmware/soc1_0_ram_bt_1_1_hdr.bin`
и `BT_FW.cfg`.

Но `hci0` по-прежнему не отвечает: стек шлёт две команды (TX 12 байт), RX ноль,
`hciconfig hci0 up` — «Connection timed out». Регистры BTIF при этом говорят
«data in tx dma is completely sent by HW», то есть байты ушли в чип.

Вывод: транспорт работает, а чип ждёт вендорской последовательности. В Android
её делает `libbt-vendor`: своя команда включения, установка скорости, догрузка
патча BT уже по HCI. Просто перекладывать пакеты недостаточно. Это и есть
следующий заход по Bluetooth.

Телефон после опытов чист: мост опущен, Wi-Fi подключён, самопроверка 38 из 38.

## 2026-09-22 11:01 MSK — Bluetooth заработал

Адаптер поднимается сам при загрузке, `bluetoothctl` видит контроллер, поиск
находит соседние устройства. Путь оказался из четырёх узлов, и на каждом
пришлось разбираться.

### 1. Сборка драйвера

Прежняя запись «нет `ENUM_WMTMSG_TYPE_T`» была неверной. Тип есть. В
`stp_exp.h` объявления стоят под `#ifdef MTK_WCN_WMT_STP_EXP_SYMBOL_ABSTRACT`,
а без макроса компилятор идёт в ветку `#else`, которая теми же типами
пользуется — отсюда два десятка «unknown type name». Драйвер связности
определяет макрос у себя, сборка Bluetooth — нет. Одна строка в Makefile.

Вторая мелочь: `vermagic` вышел с плюсом (`...-v1+`), а ядро на телефоне без.
Плюс приписывает `setlocalversion`; пустой `.scmversion` уже лежал, но
`utsrelease.h` и `kernel.release` хранили старое значение. Поправил оба.

### 2. Чего не было в ядре

Виртуального HCI и профилей: `CONFIG_BT_HCIVHCI`, `CONFIG_BT_RFCOMM`,
`CONFIG_BT_HIDP` — все «is not set». Но `CONFIG_BT=y`, то есть ядро Bluetooth
встроено, а значит, недостающее собирается **модулями против уже прошитого
ядра** — тот же приём, что с подсветкой. Перепрошивка не понадобилась.

### 3. Мост

Драйвер MediaTek отдаёт HCI не устройством, а символьным узлом `/dev/stpbt`, и
кладёт туда пакеты ровно в том виде, в каком их ждёт виртуальный транспорт H4.
Значит, нужен не драйвер, а перекладчик — `bf7-btbridge`.

**Ловушка первая: poll.** Драйвер не реализует `.poll`, и ядро отвечает
«всегда готов». Мост на `poll` немедленно лез читать из `stpbt`, застревал в
блокирующем чтении и никогда не передавал чипу команду. Переписан на две нити.

**Ловушка вторая: собственный служебный пакет.** Тот самый `ff 00`, которым
создаётся HCI-устройство, возвращается при чтении из `vhci`. Уйдя в чип, он
сбивал его, и следом отправленный HCI Reset оставался без ответа — при том что
прямая проба той же командой отвечала «успех». Вендорские пакеты теперь
отбрасываются.

**Ловушка третья: выдержка.** Открытие `/dev/stpbt` включает функцию BT через
WMT, и чипу нужно время. Без паузы стек шлёт Reset раньше готовности.

### 4. Чип объявляет умение, которого у него нет

После всего этого адаптер отвечал (72 события, прочитан адрес и размеры
буферов), но подъём срывался:

```
< HCI Command: Read Synchronization Train Parameters (0x03|0x0077)
> HCI Event: Command Status — Status: Unknown HCI Command (0x01)
= Close Index
```

В возможностях страницы 2 стоят биты широковещания без соединения и
синхронизирующего поезда, ядро им верит и шлёт команду, а чип её не знает.
Мост гасит четыре нижних бита `features[2][0]`. Сперва я погасил не тот байт
(второй вместо первого) — не помогло; после правки заявка снялась («было
0x05») и адаптер поднялся.

### Итог

```
hci0: Type: Primary  Bus: Virtual
      BD Address: 00:00:46:76:63:01  ACL MTU: 1021:6  SCO MTU: 184:1
      UP RUNNING

Controller 00:00:46:76:63:01 (public)  Powered: yes
  UUID: Generic Attribute Profile / Generic Access Profile /
        PnP Information / A/V Remote Control Target

Поиск: Device F8:17:2D:xx:xx:xx TY
       Device 9C:E9:51:xx:xx:xx Xenium E2602
```

Поднимается службой `bf7-bluetooth` после сеанса — как Wi-Fi и камера, экрану
ждать радио незачем. Самопроверка выросла до **40 проверок**, все проходят.

Осталось на будущее: адрес `00:00:46:76:63:01` начинается с нулей и похож на
заглушку — как было с Wi-Fi, где настоящий адрес лежит в NVRAM. Пока не мешает.

## 2026-09-22 11:02 MSK — Звук всё ещё способен уронить ядро

По дороге к Bluetooth телефон однажды перезагрузился сам, и я сперва подумал на
свои опыты. Посмертная запись показала другое:

```
[123.7] alsa-sink-Multi  do_alignment_fault
                         __arch_copy_from_user
                         soc_rtdcom_copy_user
                         interleaved_copy
                         __snd_pcm_lib_xfer
                         snd_pcm_common_ioctl
```

Невыровненный доступ при копировании из пространства пользователя в звуковом
слое MediaTek — тот же класс, что паника на `mtk_pcm_dl2_params` в ночь на
22-е. Сработало через две минуты после загрузки, при обычной работе
PulseAudio.

Значит, подпорки (снятый wireplumber, `fragment_size=4096`, явные устройства)
уменьшили частоту, но не убрали причину. Записываю как есть: воспроизвести по
желанию пока не умею, ловится случайно. Bluetooth к этому отношения не имеет —
проверено по времени и по стеку вызовов.

## 2026-09-22 11:22 MSK — Адрес Bluetooth: команда есть, толку нет

Взялся за заглушечный адрес `00:00:46:76:63:01` — он начинается с двух нулей
ровно как placeholder MediaTek у Wi-Fi. Вендорская команда нашлась и работает:

```
пишу адрес FC1A     -> 04 0e 04 01 1a fc 00      (успех)
читаю адрес снова   -> 04 0e 0a 01 09 10 00 77 66 55 44 33 02
```

То есть `02:33:44:55:66:77` — записался и перечитался.

**Но пользы от этого нет, и вот почему.**

Если менять адрес на живой связи, чип пересобирает STP: чтение из `/dev/stpbt`
начинает отвечать `EIO`, нити моста умирают с «Input/output error». Сброс и
выдержка после записи не спасают.

Если менять и закрывать узел, чтобы мост открыл заново, — открытие
перезапускает питание чипа через WMT, прошивка перечитывает адрес из своего
источника и возвращает заглушку. Проверено: после закрытия и повторного
открытия снова `00:00:46:76:63:01`.

Заглушка косметическая: поиск устройств и профили с ней работают. Настоящий
адрес лежит в NVRAM, как и MAC у Wi-Fi, — доставать его оттуда отдельная
работа. Разбор `bf7_postavit_adres` оставлен в исходнике моста, чтобы не искать
команду заново.

Заодно поправлен порядок: `bf7-bluetooth` идёт после `bf7-wifi`, потому что
`bt_drv` зависит от `wmt_drv`, а грузит его подъём Wi-Fi. Без этого `insmod`
отвечал «Unknown symbol in module».

## 2026-09-22 11:23 MSK — Клавиатура печатает. Поломка только в поле поиска оболочки

Написал крошечное окно GTK3 с полем ввода (`kernel-work/probes/entrytest.c`),
собрал прямо на телефоне и нажал три клавиши экранной клавиатуры. Итог:

```
BF7TEST: текст теперь «q»
BF7TEST: текст теперь «qw»
BF7TEST: текст теперь «qwe»
```

и на экране в поле честно написано **qwe**.

**Значит, экранная клавиатура исправна.** Она печатает в обычные окна, текст
виден, приложение получает сигнал об изменении. Все прежние подозрения —
композитор, протокол ввода, модуль GTK, отрисовка — сняты окончательно.

Поломка узкая: **не работает только поле поиска приложений в самой оболочке**.
Туда буква доходит (я это измерил: `gtk_entry_get_text` возвращает «q»), но не
рисуется и не запускает отбор приложений. Это внутреннее дело phosh и её
слоевого окна.

Практический вывод, который стоит сказать прямо: **печатать на телефоне
можно** — в браузере, в редакторе, в любом окне. Нельзя только искать
приложения набором в оболочке; открывать их значком можно.

### Довесок 11:52: оболочка текст получает, но окно не обновляет

Собрал phosh из исходников с приборами в `search_changed` и `do_search`
(`src/app-grid.c`) и набрал две буквы. Обработчики срабатывают оба:

```
BF7PH: search_changed <q>
BF7PH: do_search poshel
BF7PH: search_changed <qw>
BF7PH: do_search poshel
```

То есть оболочка и текст получает, и отбор приложений запускает. А на экране
по-прежнему пустое поле и неотфильтрованный список.

Пробовал заставить окно обновиться изнутри самой оболочки:
`gtk_widget_queue_resize` на поле и на окно верхнего уровня — не помогло, как
не помогал раньше `gtk_widget_queue_draw` и прямая `gtk_entry_set_text`.

**Итог по этой ветке.** Разобрано до предела, что можно сделать снаружи:
клавиатура исправна (проверено своим окном GTK3 — печатает и видно),
композитор доставляет, GTK применяет, оболочка получает и обрабатывает. Не
обновляется именно её слоевое окно, и ни одно из обычных приглашений к
перерисовке этого не меняет. Дальше — разбирательство внутри связки
phosh + GTK + слоевые поверхности, отдельная работа.

Пакетная оболочка возвращена побитово, самопроверка 40 из 40.

## 2026-09-22 11:53 MSK — Паника звука: причина найдена

Разобрал по коду и нашёл источник. Цепочка из посмертной записи:

```
alsa-sink-Multi  do_alignment_fault
                 __arch_copy_from_user
                 soc_rtdcom_copy_user
                 interleaved_copy
                 __snd_pcm_lib_xfer
```

`soc_rtdcom_copy_user` зовёт реализацию нашего компонента — для DL1 это
`mtk_pcm_copy` в `sound/soc/mediatek/common_int/mtk-soc-pcm-dl1.c`. Она делает:

```c
copy_from_user(Afe_Block->pucVirtBufAddr + Afe_WriteIdx_tmp, data_w_ptr, size);
```

А `pucVirtBufAddr` — это `runtime->dma_area`
(`mtk-soc-afe-control.c:1880: pblock->pucVirtBufAddr = dma_area`), то есть
память от `dma_alloc_coherent`.

**И вот в чём беда.** На arm64 такая память отображается некэшируемой, как
устройство. Доступ к памяти устройства обязан быть выровнен по своему размеру,
а `__arch_copy_from_user` для кусков от шестнадцати байт пользуется парными
командами `LDP`/`STP` — шестнадцатибайтовыми. Смещение в буфере выравнивается
драйвером лишь по слову (`word_size_align`, четыре байта). Четырёх мало:
шестнадцатибайтовая пересылка по адресу, кратному четырём, но не шестнадцати,
и даёт `do_alignment_fault`.

Отсюда и редкость: падает не всякий раз, а когда очередное смещение в кольце
оказывается не кратно шестнадцати.

**Оговорка, и важная.** Это обоснованная догадка, а не доказанный факт.
Невыровненный доступ запрещён к памяти **устройства**, а `dma_alloc_coherent`
на arm64 обычно отдаёт «обычную некэшируемую», где невыровненное как раз
позволено. Проверил: зарезервированной памяти у звука в дереве устройств нет,
своей области в `/proc/iomem` тоже — то есть буфер самый обычный. Значит,
объяснение сходится не до конца, и чтобы закрыть вопрос, нужен адрес сбоя и
ESR из свежей посмертной записи. Прежнюю уже вытеснило: в `pstore` сейчас
лежат только предупреждения нити зарядки.

Что установлено твёрдо: сбой приходит из `copy_from_user` в буфер DMA звука,
по пути `mtk_pcm_copy` → `soc_rtdcom_copy_user`, и это тот же путь, где раньше
падал `__memset_io` в `mtk_pcm_dl2_params`.

### Два пути починки

1. **В ядре, правильно.** Копировать через промежуточный буфер: `copy_from_user`
   в обычную память, затем `memcpy_toio`. Либо выравнивать смещение по
   шестнадцати вместо четырёх. Требует пересборки ядра и перепрошивки слота A.
2. **Снаружи, подпоркой.** Держать все передачи кратными шестнадцати байтам,
   чтобы смещение в кольце всегда оставалось выровненным. У нас
   `fragment_size=4096` уже кратен шестнадцати, но PulseAudio вправе дослать
   остаток меньшего размера — и тогда смещение «съезжает».

Отображение памяти (`mmap`) обошло бы путь копирования целиком, но драйвер его
не поддерживает — это проверено раньше и записано в самой настройке.

Драйвер собран в ядро (`obj-y += common_int/`), модулем его не подменить —
значит, любая правка требует пересборки и перепрошивки слота A.

Пока не чиню, и вот почему: править ядро по недоказанной догадке дорого и
рискованно. Сначала нужна свежая посмертная запись с адресом сбоя. Ловушку на
неё оставляю: `pstore` включён, запись появится сама при следующем случае.

## 2026-09-22 12:14 MSK — Bluetooth: три моих же огреха на пути к устойчивости

Адаптер работал, но поднимался через раз, а один раз я подвесил телефон
насмерть. Все три беды — моего изготовления, и все три поучительны.

### Первая: мост не выходил по сигналу, и телефон не мог выключиться

`systemctl reboot` — и система осталась в состоянии `stopping` на **тридцать
пять минут**. Держала её моя же служба: нити моста сидят в блокирующем чтении,
а обработчик сигнала только ставил флаг. Пока из узла ничего не приходит,
`read` не возвращается, флаг никто не смотрит, `pthread_join` ждёт вечно.

Теперь обработчик **закрывает узлы** — чтения тут же возвращаются с ошибкой,
нити видят флаг и выходят. `close()` из обработчика сигнала вызывать можно.

Сверх того в описание службы добавлены `TimeoutStopSec=8` и `KillMode=mixed`.
Это оказалось не лишним: драйвер MediaTek держит чтение **непрерываемым**, и
если чип уже отвечает EIO, процесс не берёт даже SIGKILL —
«Processes still around after final SIGKILL». С пределом ожидания systemd
просто идёт дальше, и выключение занимает свои тридцать секунд.

Заодно убран `Restart=on-failure`: при застрявшем чипе он только плодил
неубиваемые процессы и портил чип заново.

### Вторая: драйвер вставлялся раньше связности

`insmod: Unknown symbol in module`. `bt_drv` опирается на символы `wmt_drv`, а
тот грузит подъём Wi-Fi — служба `Type=simple`, то есть «запущена» сразу, а
модули появляются позже. Одного `After=bf7-wifi.service` мало. Подготовка
теперь ждёт `wmt_drv` в `/proc/modules`.

### Третья: BT влезал в чужую работу с чипом

Модуль дождались, а подъём Wi-Fi ещё продолжает говорить с чипом через WMT.
Влезть в этот промежуток — получить EIO на открытии `/dev/stpbt` и умерший
мост. Признак завершения — появление `wlan0`; ждём его и даём три секунды на
успокоение.

### Итог

Перезагрузка проходит за тридцать секунд, `hci0 UP RUNNING` сам, самопроверка
40 из 40, ноль упавших служб.

Правило, которое стоит запомнить: **если служба держит устройство, у неё
обязан быть предел ожидания при остановке.** Иначе однажды телефон не
выключится, и виновата будет не железка.

## 2026-09-22 12:23 MSK — Паника звука воспроизведена нарочно. Догадка стала фактом

Чтобы не править ядро вслепую, решил вызвать панику по заказу. Если дело в
выравнивании, то передачи некратного размера должны сдвигать смещение в кольце.
Очистил `pstore`, остановил звуковой сервер и сыграл шум с периодом в 1021
кадр — это 4084 байта, не кратно шестнадцати:

```sh
aplay -D hw:0,0 -f S16_LE -r 48000 -c 2 --period-size=1021 --buffer-size=4084 /dev/urandom
```

Связь оборвалась на двадцать пятой секунде. Телефон вернулся сам, и в
посмертном журнале лежало ровно то, что нужно:

```
Unable to handle kernel paging request at virtual address ffffff8009dd10e4
ESR = 0x96000061
pc : __arch_copy_from_user+0x1b0/0x280
lr : mtk_pcm_copy+0x15c/0x3fc
do_alignment_fault
```

Разбор `ESR = 0x96000061`: класс 0x25 — отказ доступа к данным; младшие шесть
бит `0x21` — **сбой выравнивания**; бит 6 равен единице — **запись**.

Адрес назначения `…0e4`: кратен четырём, **не кратен шестнадцати**. Это и есть
доказательство: `__arch_copy_from_user` на кусках от шестнадцати байт берёт
парные команды `LDP`/`STP`, а память буфера их невыровненно не принимает.

Что память именно такая, сказано в самом драйвере — в комментарии к
`word_size_align`:

> sram is device memory, need word size align

Там же видно, что выравнивание по шестнадцати драйвер делает
(`in_size & 0xFFFFFFF0`) — но только для **размеров**. Адрес назначения
складывается из базы буфера и смещения, и на каком-то пути эта сумма всё же
оказывается не кратной шестнадцати.

**Вывод для починки.** Искать, какая именно ветка арифметики съезжает, — долго
и ненадёжно: веток три, и каждая со своими `%` и выравниваниями вниз. Правильно
сделать безопасным само копирование: перекладывать через промежуточный буфер в
обычной памяти, а оттуда `memcpy_toio`, который умеет писать в память
устройства по любому адресу. Тогда все пути чинятся разом.

Драйвер собран в ядро, так что это пересборка и перепрошивка слота A.

## 2026-09-22 12:45 — звук больше не роняет ядро: посредник вместо прямого copy_from_user

Паника воспроизводилась по заказу:

    aplay -D hw:0,0 -f S16_LE -r 48000 -c 2 --period-size=1021 --buffer-size=4084 /dev/urandom

    Unable to handle kernel paging request at virtual address ffffff8009dd10e4
    ESR = 0x96000061   (сбой выравнивания, запись)
    pc : __arch_copy_from_user+0x1b0/0x280
    lr : mtk_pcm_copy+0x15c/0x3fc

Буфер DMA звука отображён как **память устройства** — про это прямо сказано в
комментарии к `word_size_align` в `mtk-soc-afe-control.c`. Такая память
принимает только естественно выровненные доступы, а `__arch_copy_from_user`
на arm64 пишет парными командами LDP/STP.

**Сначала я починил не то.** Выровнял приёмник: неровный край побайтово через
`writeb`, ровную середину — обычным `copy_from_user`. Получил ту же панику по
тому же адресу, только теперь из своей функции
(`lr : bf7_copy_from_user_io+0x1d8/0x35c`). Причина: **arm64 выравнивается по
источнику, а не по приёмнику**. В приёмник он пишет как придётся, и никакое
выравнивание `dst` этого не отменяет.

**Правильная починка** — посредник: `copy_from_user` в обычную память,
оттуда `memcpy_toio`, который умеет писать в память устройства. Кусками по
4 КиБ, чтобы не зависеть от размера периода.
`bf7_copy_from_user_io()` в `sound/soc/mediatek/common_int/mtk-soc-pcm-dl1.c`,
все три копирования в `mtk_pcm_copy` переведены на неё.

**Проверено** на `4.19.325-cip124-st8-bf7-x657b-v1`: периоды
1021, 1023, 511, 257, 4093, 129, 63; форматы S16_LE, U8, S32_LE; моно и стерео.
Ни одной паники, `dmesg | grep -c "alignment fault"` = 0.

Прежний образ загрузки сохранён: `scratchpad/boot-до-звука.img`.

## 2026-09-22 12:51 — та же беда на записи и ещё в четырёх драйверах

Наблюдатель подсказал проверить обратный путь — и оказался прав. Запись звука
роняет ядро так же, только на чтении:

    arecord -D hw:0,1 -f S16_LE -r 48000 -c 2 --period-size=33 --buffer-size=133 -d 4 /dev/null

    Unable to handle kernel paging request at virtual address ffffff8009dd0084
    ESR = 0x96000021, WnR = 0        (сбой выравнивания, чтение)
    (1)[8062:arecord]

Адрес выровнен по четырём байтам, но не по восьми. `copy_to_user` читает из
памяти устройства восьмибайтовыми командами — тот же механизм, зеркально.
Окно узкое: беда вылезает на заворачивании кольцевого буфера, когда хвост
короче шестнадцати байт, поэтому микрофон «работал».

**Обошёл все места разом.** Пара помощников
`bf7_copy_from_user_io`/`bf7_copy_to_user_io` переехала в
`mtk-soc-afe-control.c`, объявления — в `mtk-soc-afe-control.h`.
Переведены:

| файл | места | сторона |
|---|---|---|
| `mtk-soc-afe-control.c` | 3 записи + 3 чтения | оба |
| `mtk-soc-pcm-dl1.c` | 3 | воспроизведение |
| `mtk-soc-pcm-dl2.c` | 2 определения `dataTransfer` | воспроизведение |
| `mtk-soc-pcm-hdmi.c` | 3 | воспроизведение |
| `mtk-soc-pcm-bt-dai.c` | 3 | запись |

**Отдельная находка в dl2.** Там два определения `dataTransfer`, и то, которое
собирается (`AUDIO_DL2_ISR_COPY_SUPPORT` определён в `mtk-auddrv-def.h`),
пишет в буфер DMA **обычным `memcpy`** — без всякого пользователя. Это тоже
паника, просто с другой стороны; переведено на `memcpy_toio`. Сперва я
починил только ветку `#else`, то есть мёртвый код.

Проверено, что ни одно копирование не стоит под спин-блокировкой: перед каждым
последним идёт `spin_unlock`. `kmalloc(GFP_KERNEL)` внутри помощника законен —
исходный `copy_from_user` там тоже мог уснуть.

## 2026-09-22 13:14 — поле поиска оболочки: где именно обрывается

Поставил приборы в phosh 0.24 (`src/app-grid.c`, сборка в
`/root/phoc-work/phosh-build`, подменяется `mount --bind` поверх
`/usr/libexec/phosh`). Установлено твёрдо, по проводу и по журналу:

1. **Текст доходит до поля.** Экранная клавиатура шлёт
   `zwp_input_method_v2.commit_string("c")`, оболочка отвечает
   `set_surrounding_text("calc", 4, 4)` — в поле лежит всё набранное.
2. **Поиск работает.** `search_changed` и `do_search` вызываются на каждую
   букву, список перефильтровывается: я случайно запустил калькулятор,
   ткнув в **невидимую** отфильтрованную строку.
3. **Окно не рисуется.** После `commit_string` у поверхности `wl_surface@54`
   («phosh home») **ноль** `commit` — не «рисует не то», а не рисует вовсе.
4. Окно правильное: `gtk_widget_get_toplevel` даёт `PhoshHome`, тип 1
   (верхнего уровня), часы кадров у поля и у окна **одни и те же**,
   `gtk_widget_get_mapped` = 1, `gdk_window_is_visible` = 1.
5. **Область обновления есть** (`gdk_window_get_update_area != NULL`), то есть
   `queue_draw` отработал, — а **счётчик кадров застыл на 4** с самого запуска.

Что перепробовано и не помогло: `gtk_widget_queue_draw`,
`gtk_widget_queue_resize` (и на поле, и на окне верхнего уровня),
`gdk_window_invalidate_rect` + `gdk_window_process_updates(окно, TRUE)`,
сорок вызовов `gdk_window_thaw_toplevel_updates_libgtk_only`.

Два подозрения **сняты измерением**:
- `update_and_descendants_freeze_count` равен нулю: из сорока разморозок все
  ответили руганью `g_return_if_fail`. Значит, GTK не держит окно в ожидании
  `configure`.
- Композитор не зажимает кадры: разобрал журнал `WAYLAND_DEBUG` с учётом
  переиспользования номеров — **у всех восьми поверхностей ноль неотвеченных**
  `wl_callback.frame`.

Остаётся `update_freeze_count` самого окна (другой счётчик, публичного
чтения нет) либо заморозка внутри `GdkFrameClockIdle`.

**Заодно поправил своё же заблуждение.** Решил было, что phoc отдаёт касания
с вдвое уменьшёнными координатами: подал `bf7-touchinj 180 97`, а оболочка
получила `(90, 16.5)`. Композитор прав, ошибся я: **`bf7-touchinj` ждёт
физические координаты панели** (720×1612), а я дал логические. Сходится точно:
97/1612·806 − 32 = 16,5. С физическими `360 195` поле берёт фокус, клавиатура
поднимается, касания по клавишам доходят.

## 2026-09-22 13:25 — почему оболочка «замерзала»: экран заявлял 1 мГц

Докопался. Вывод сообщал клиентам `wl_output.mode(1, 720, 1612, **1**)` —
частота обновления **1 мГц**, одна тысячная герца.

Из неё GTK считает период кадра. Замер внутри часов кадров `PhoshHome`
(чтение приватной структуры `GdkFrameClockIdle` по разметке из исходников
GTK 3.24.38):

    период = 1 000 000 000 мкс   (1000 секунд!)
    не раньше = 3 897 975 096     при «сейчас» = 1 932 027 228

То есть следующая отрисовка назначена **через тридцать три минуты**.
`freeze_count` при этом ноль, источник перерисовки стоит в очереди
(`paint_idle_id = 683`), запрошены фазы LAYOUT|PAINT|RESUME_EVENTS — всё
исправно, просто ждёт.

**Причина в нашем же драйвере.** `bf7_drm.c` строит режим макросом
`TINYDRM_MODE`, а тот ставит `.clock = 1 /* pass validation */` — один
килогерц. Отсюда 1000/(720·1612) ≈ 0,00086 Гц, наружу уходит 1 мГц.

**Чинится одной строкой:** пиксельная частота под настоящие 60 Гц,
`clock = 60 · htotal · vtotal / 1000` = 69638 кГц, плюс `vrefresh = 60`.

Отсюда же, задним числом, объясняется многое: «оболочка глотает нажатия»,
«браузер подтормаживает», «экран блокировки не отзывается». Ничего не
глоталось — всё рисовалось раз в тысячу секунд.

Путь к находке (чтобы не повторять): текст доходил до поля, `do_search`
отрабатывал, список фильтровался — я случайно запустил калькулятор, ткнув в
**невидимую** отфильтрованную строку. Значит, ломалась только отрисовка.
Дальше по счётчикам: заморозки окна пусты (обе), кадры композитором
отвечены все, `begin_updating` не даёт ни одного тика — осталось лезть в
приватную структуру часов, и там всё стало видно сразу.

## 2026-09-22 13:39 — браузер: окно есть, страница не рисуется

После починки частоты обновления проверил браузер целиком. Итог:

- Окно открывается, полоса загрузки идёт, сеть работает: у
  `WebKitNetworkProcess` **установленное** соединение на 443
  (`/proc/PID/net/tcp`, состояние 01), `urllib` с телефона отдаёт 200.
- **Страница не рисуется ни сетевая, ни местная** (`file:///tmp/proba.html`
  с ярким фоном — пусто). Значит дело не в сети и не в декодировании.
- Процессы живы и не крутятся: в `top` простой, ни одного занятого ядра.
- Постоянная жалоба при запуске:
  `Disabled hardware acceleration because GTK failed to initialize GL:
  No available configurations for the given pixel format`.
  Не помогают ни `WEBKIT_DISABLE_COMPOSITING_MODE=1`, ни
  `WEBKIT_FORCE_COMPOSITING_MODE=1` с `GDK_GL=gles`.

**Где искать.** Конфигов у EGL вдоволь — проба `eglconfigs` показывает 67,
среди них RGBA8888 с `EGL_WINDOW_BIT`. Но **ни одного с битом настольного
OpenGL**: у всех `rend=0x45` = ES | ES2 | ES3, бита `EGL_OPENGL_BIT` (0x08)
нет. GDK же берёт дисплей EGL не так, как проба: через платформу Wayland, а у
нас на этом пути стоит своя правка libhybris. Похоже, GDK получает дисплей,
на котором конфигов не видно вовсе.

Отдельная находка по дороге: **пять зомби `gsd-backlight-helper`** —
gnome-settings-daemon не собирает потомков, и они копятся.

**Проверено заодно и работает:** разблокировка целиком пальцем — касание
будит экран, свайп поднимает клавиатуру кода, цифры набираются, «Unlock»
отпирает. Важная тонкость пробника: **весь ход должен идти одним запуском**
`bf7-touchinj поток`. Каждый новый запуск заводит новый узел uinput, и экран
блокировки от этого сбрасывает набранное. Поэтому научил поточный режим
делать и свайпы: строка вида `свайп x1 y1 x2 y2 [шагов]`.

## 2026-09-22 13:50 — браузер: WebKit уносил дисплей EGL у GTK

Довёл до причины. Трассировка `BF7_EGL_TRACE=1` показала порядок:

    [hybris] запрошена платформа 0x31d8 ... ws "wayland"   ← GTK берёт дисплей
    ...
    [hybris] запрошена платформа 0x31d7 ... ws "null"      ← WebKit просит GBM
    [hybris]   ws_init("null") отказал
    [hybris]   беру уже загруженный ws "wayland"           ← наша выручка
    [hybris]   андроидный eglGetDisplay -> 0x1             ← тот же дисплей!
    ...
    getProcAddress("eglTerminate")
    WARNING: Disabled hardware acceleration because GTK failed to initialize GL

**WebKit, закончив свои проверки, зовёт `eglTerminate`** — и уносит дисплей у
GTK, потому что после нашей выручки дисплей у них один на двоих. GTK следом
не находит ни одного конфига и отключает ускорение.

Проверено от противного: отдельный пробник `gdkeglprobe`, повторяющий путь
GDK шаг в шаг (платформа Wayland, `eglInitialize`, `eglBindAPI(OPENGL)`,
те же признаки конфига), находит **42** подходящих конфига из 67. И обычное
окно GTK с `GtkGLArea` заводит GL без единой жалобы. Значит, дело не в
драйвере и не в признаках, а именно в чужом `eglTerminate`.

**Починка** в `hybris/egl/egl.c`: как только дисплей стал общим, `eglTerminate`
не выполняем — возвращаем успех и ничего не трогаем. Терять нечего: всё, что
он освобождает, освобождается при выходе из процесса, а дисплей у нас живёт
ровно столько же. Заплатка обновлена: `hybris/bf7-patches/libhybris-bf7.patch`.

**Что теперь.** Жалобы на GL нет, страница **рисуется**: пробная страница с
ярким фоном заливает окно. Осталась вторая половина:

- с ускоренной сборкой слоёв страница видна, но **закрывает панель браузера**
  (адресная строка и кнопки не видны);
- с `WEBKIT_DISABLE_COMPOSITING_MODE=1` наоборот: панель на месте, страница
  пуста.

Похоже, поверхность ускоренного слоя цепляется не к вложенной поверхности
окна, а поверх всего окна. Копать в подчинённые поверхности (wl_subsurface)
на стороне WebKit и нашего композитора.

Мелочь для повторения опытов: запускать браузер надо
`systemd-run --scope`, иначе он умирает между вызовами ssh. И появился
`bf7-otperet` — разблокировка экрана одним заходом.

## 2026-09-22 19:18 — порча корневой файловой системы и её починка

**Нашлось случайно**, когда `rm` отказался удалять испорченный файл журнала
со словами `Bad message`. Это не журнал — это файловая система:

    EXT4-fs error (device mmcblk0p48): ext4_lookup:1824:
        inode #262922: comm rm: iget: checksum invalid

Проверка только чтением нашла ещё и блоки, заявленные двумя inode сразу.
Почти наверняка это след паник, которыми я ловил сбой выравнивания в звуке:
каждая из них — неснятое размонтирование.

**Чинить пришлось до монтирования.** В командной строке ядра стоит
`fsck.mode=skip`, и не зря: корень монтирует сам ramdisk, и единственный миг,
когда файловая система никем не занята, — там же. `e2fsck` в наложении
ramdisk **уже лежал**, но его никто не звал. Добавил в `init` обычный для
загрузчиков порядок: сперва `-p`, а если он сдался (код 4 и выше), то `-fy`.

Сработало с первого раза, по журналу ядра:

    BF7-ROOTFS: e2fsck -p exit 4
    BF7-ROOTFS: filesystem needs full repair, running e2fsck -fy
    BF7-ROOTFS: e2fsck -fy exit 1        (1 = ошибки исправлены)

**Цена оказалась заметной.** В `lost+found` легло **498** записей, и
самопроверка сразу нашла пять изъянов. Повреждено было больше, чем один inode:

- `liblapack.so.3` пропала — сломался `ffmpeg`;
- файл службы `bf7-audio.service` **обрезан** на 693 байтах, systemd отказался
  его читать («bad unit file setting»);
- каталоги дополнений перепутаны типами: `bf7-console-quiet.service.d` стал
  **символической ссылкой** на чужой файл службы, а
  `bf7-touch-init.service.d` — файлом с данными на 78 КБ;
- списки apt в `/var/lib/apt/lists` не разбирались;
- у двух пакетов пропали списки файлов, пропал `/etc/manpath.config`.

Своё вернул `bf7-install.sh` из хранилища — ради этого всё и держится в
дереве. Чужое ищу `debsums -c` и доставляю заново.

**Вывод на будущее.** Порча накопилась незаметно и вылезла случайно. Проверка
в ramdisk теперь идёт каждую загрузку, так что подобное всплывёт сразу, а не
через неделю. И ещё: после серии паник стоит смотреть `dmesg | grep EXT4-fs`,
а не только «загрузилось ли».

## 2026-09-22 19:38 — разбор последствий: Bluetooth, динамик, опровержение про усилитель

После починки файловой системы самопроверка показала пять изъянов. Разобрал
все.

**1. Bluetooth пропал — я нарушил собственное правило.** Файл службы был на
месте, но `systemctl is-enabled` отвечал `disabled`: проверка файловой системы
убрала ссылку включения, и по одному взгляду это не заметно. Включил — но мост
всё равно падал:

    WARNING: CPU: 0 PID: 5564 at BT_read+0x498/0x5d0 [bt_drv]
    bf7-btbridge: чтение stpbt: Input/output error

Причина — записанное правило [[bf7-kernel-rebuild-rule]]: **модули связности
обязаны пересобираться тем же заходом, что и ядро**. Я трижды пересобрал ядро
и ни разу не тронул `wmt_drv`, `wmt_chrdev_wifi`, `wlan_drv_gen4m`, `bt_drv`.
Строка версии не менялась, поэтому модули грузились — и вели себя неверно.
Пересобрал все четыре, поставил, перезагрузил: `hci0 UP RUNNING`.

Чтобы включение больше не терялось молча, `bf7-install.sh` теперь ещё и
включает службы, а не только раскладывает файлы.

**2. Динамик молчал, и прежнее объяснение было неверным.** В `bf7-audio-up`
записано, что вывод внешнего усилителя забирает и ведёт сам драйвер, а ключа
`Ext_Speaker_Amp_Switch` достаточно. **Это опровергнуто замером:**

    ключ туда-сюда, линия отпущена  -> энергия записи 175
    линия поднята снаружи (gpioset) -> энергия записи 41733, пик 9292

Драйвер её и не может вести — при загрузке он не находит ни одного нужного
состояния pinctrl и честно пишет об этом:

    AudDrv_GPIO_probe pinctrl_lookup_state aud_smartpa_on fail -19
    AudDrv_GPIO_probe pinctrl_lookup_state extamp-pullhigh fail -19

а `gpioinfo` показывает линию 151 как `unused`. Завёл `bf7-extamp.service`:
долгоживущий `gpioset --mode=signal`, потому что отпущенная линия возвращается
во вход и усилитель замолкает.

Попутно поправил `bf7-audio-up`: он открывал `hw:0,0` напрямую, а устройство
держит звуковой сервер, который стартует раньше. Теперь поток тишины идёт
через сервер, а прямое открытие осталось запасным путём.

**3. Прочие потери** от проверки файловой системы: `liblapack.so.3` (сломался
ffmpeg), `/etc/manpath.config`, `/etc/mpv` превратился в файл, списки apt не
разбирались. Всё доставлено заново. `debsums -c` по 1465 пакетам нашёл
только это; `/usr/bin/Xwayland` отличается намеренно — он наш, с glamor.

Итог: самопроверка 41 из 41, ноль упавших служб, `e2fsck -p exit 0` за треть
секунды при загрузке.

## 2026-09-22 19:55 — Bluetooth при загрузке, zram и мои же грабли

**Bluetooth поднимается сам.** После пересборки модулей связности мост встаёт
при загрузке: `hci0 UP RUNNING`, `bf7-bt-prepare` докладывает о готовности за
три секунды. Ожидание в скрипте теперь по связи (`operstate` у `wlan0`
должен стать `up`), а не просто «появился узел», плюс выдержка. Службе
разрешён перезапуск: мост научился закрывать оба описателя по сигналу, так
что висящей остановки больше не будет. Предел — четыре попытки за три минуты.

**Наступил на собственные грабли.** Хотел проверять готовность чипа делом:
`timeout 2 dd if=/dev/stpbt bs=1 count=1`. Забыл записанное: **чтение
/dev/stpbt непрерываемо**. Когда чип готов, read не возвращается, а процесс
переживает даже SIGKILL — systemd написал «Processes still around after final
SIGKILL» и оставил висеть `dd`, державший устройство. Снялось только
перезагрузкой. Пробу убрал, в скрипте теперь стоит предупреждение.

**Второй мой же промах, из-за которого хозяин спросил про задачу на семь
часов.** Ожидание опыта я написал циклом без предела:

    until ssh bf7 'grep -q ЗАКОНЧИЛ /tmp/stress.log'; do sleep 10; done

Телефон с тех пор перезагружался с десяток раз, `/tmp` очистился, и ждать
стало нечего — цикл крутился семь часов. Впредь у всякого ожидания должен
быть предел по числу попыток.

**zram больше не портит картину.** Служба падала с «Device or resource busy»:
своп успевает включить генератор `systemd-zram-setup@zram0`, и система уходила
в `degraded`, хотя своп работал и стоял с нужным приоритетом. Теперь служба
судит по итогу, а не по попытке: пробует включить, молча терпит отказ и
проверяет `/proc/swaps`.

Состояние: `running`, упавших ноль, самопроверка 42 из 42.

## 2026-09-22 20:15 — заводские адреса найдены; Bluetooth свой принять не смог

**Нашёл, где лежат настоящие адреса.** Имена разделов читаются так:
`lsblk -o NAME,PARTLABEL,SIZE /dev/mmcblk0` — каталога `by-name` у нас нет.
Нужные: `nvdata` — `mmcblk0p7`, `nvram` — `mmcblk0p15`.

Монтирую строго на чтение и без журнала:
`mount -o ro,noload -t ext4 /dev/mmcblk0p7`. Внутри:

| файл | что там |
|---|---|
| `APCFG/APRDEB/BT_Addr` | первые 6 байт — адрес Bluetooth |
| `APCFG/APRDEB/WIFI` | со смещения 4 — MAC Wi-Fi |

У этого телефона: **BT e4:f8:be:xx:xx:xx**, **Wi-Fi e4:f8:be:xx:xx:xx** —
настоящий OUI, идут подряд. Сейчас же в ходу выдуманные: Bluetooth носит
заглушку прошивки `00:00:46:76:63:01`, а Wi-Fi — `be:f1:a8:xx:xx:xx`,
локально назначенный (NetworkManager, `wifi.cloned-mac-address=stable`).

`bf7-bt-prepare` теперь достаёт заводской адрес **один раз** и кладёт в
`/etc/bf7-bt-addr`; при обычной загрузке чужой раздел не трогается вовсе.

**Но принять свой адрес чип не смог.** Вендорская команда `0xFC1A` его
берёт — в журнале честно появляется «адрес задан E4:F8:BE:xx:xx:xx». А
следом связь STP рассыпается: первое же чтение из `stpbt` отвечает EIO,
мост падает, `hci0` исчезает совсем. Пробовал выдержку 1+1 секунды и 3+5 —
одинаково. Пережить закрытие узла адрес тоже не может: открытие заново
перезапускает питание чипа через WMT, и прошивка возвращает заглушку.

Откатил: рабочий Bluetooth дороже красивого адреса. Код и кэш остались на
месте — если найдётся способ пересобрать связь STP после смены, вернуть
вызов будет делом одной строки.

**И ещё раз про непрерываемое чтение.** Каждая неудачная попытка оставляла
`bf7-btbridge`, застрявший в `read` и переживший SIGKILL; новый мост потом не
мог открыть узел («Input/output error»), и снималось это только
перезагрузкой. Отладка тут дорогая: каждая проба — перезагрузка.

## 2026-09-22 20:21 — поправка: страница в браузере НЕ рисуется, я ошибся

Выше, в записи про `eglTerminate`, я написал «страница рисуется: пробная
страница с ярким фоном заливает окно». **Это неверно, снимаю.**

Поймал так: запустил тот же браузер на `about:blank` — и получил ровно ту же
заливку. Сверил цвет по пикселям:

    на экране      (2, 60, 136) = #023C88
    моя проба была (34, 102, 204) = #2266CC

Цвет один и тот же и для моей страницы, и для `about:blank`, и во всех
прежних попытках. Это не страница, а **пустая, ничем не закрашенная
поверхность GL**, растянутая на всё окно.

Мораль та же, на которой я уже обжигался с касаниями: **не объявлять успех по
сходству, сверять числа.** Синее было «похоже на мою страницу» — и этого
хватило, чтобы записать неверный вывод.

**Что на самом деле.** Починка `eglTerminate` настоящая и нужная: GTK больше
не жалуется «No available configurations» и ускорение не отключается. Но
дальше:

- со сборкой слоёв окно заливает пустая поверхность `#023C88`, панель
  браузера под ней не видна, страницы нет;
- без сборки слоёв (`WEBKIT_DISABLE_COMPOSITING_MODE=1`) панель на месте, а
  область страницы пуста.

То есть **WebKit не рисует страницу ни тем, ни другим путём**. Жалобы на
отрицательную ширину (`width -29`, `DzlSuggestionEntry`) есть в обоих
случаях, значит различают не они.

Решение о том, что делать дальше, принимаю без наблюдателя — он недоступен.

## 2026-09-22 20:39 — У меня появились глаза: камера на экран

Хозяин нашёл старую веб-камеру и направил её на панель телефона. Это то самое,
что лежало в планах под именем «будущие глаза» — и оно оказалось даже полезнее,
чем ожидалось.

Камера: `USB2.0 Camera` (Aveo Technology, `1871:0141`), `/dev/video0` на хосте,
единственный формат `YUYV 4:2:2`, максимум 640×480 при 30 кадрах.

**Две тонкости, без которых кадр бесполезен.**

1. Первые кадры камера отдаёт почти чёрными, пока подбирает выдержку. Первый
   же снимок вышел угольным пятном с одной янтарной точкой. Лечится
   прогревом: `select=gte(n\,25)` — берём двадцать шестой кадр, а не первый.
2. Телефон лежит боком, поэтому кадр надо разворачивать: `transpose=2`
   (против часовой стрелки) ставит верх панели наверх.

Инструмент: `tools/bf7-glaz.sh` — снимок одной командой.

**Первая же проверка что-то дала.** Подсветка стояла на 12 из 255 — экран был
едва различим даже вблизи. Снимок `grim` этого не показывает никак: он берёт
кадр из композитора, до матрицы. Камера показала сразу.

**Чем это ценно дальше.** `grim` отвечает на вопрос «что композитор собрал»,
камера — «что панель показала». Между ними лежит весь путь показа: слой
`bf7-disp`, MVA, собственно матрица. Раньше расхождение этих двух было
непроверяемым, и любая поломка на этом отрезке выглядела бы как исправная
работа. Теперь это два независимых свидетеля.

Сверка на обычном рабочем столе: оба снимка показали одно и то же —
сетку приложений с полем поиска и кнопкой «Show All Apps». Путь показа чист.

## 2026-09-22 20:56 — Инструмент: снимок экрана камерой, обрезанный по панели

Сырой кадр камеры мало на что годится: экран на нём — маленький кривой
четырёхугольник среди стола, проводов и чужих предметов. Нужен инструмент,
дающий ровно экран и ничего больше.

`tools/bf7-glaz.sh` — три приёма: `--nastroyka`, обычный снимок, `--syroj`,
`--sverka`. Разгибание перспективы — `tools/bf7-glaz.py` (numpy + PIL,
OpenCV на хосте нет).

**Четыре грабли, каждая стоила попытки.**

1. **Яркость.** Камера подбирает выдержку медленно: на 25-м кадре средняя
   яркость 48, на 120-м — 120. Прогрев меньше ста двадцати кадров даёт
   тёмный снимок. Настроек выдержки у этой камеры нет вовсе — только
   `brightness`, `contrast`, `gamma`, `backlight_compensation`; пересвета
   при этом нет ни при каких значениях (максимум 242 из 255).
2. **Мишень для границ.** Чтобы найти экран на кадре, нужно показать на нём
   заведомо белое поле. Своей программы для этого не было — написал
   `hybris/bf7-gpu/bf7-zalivka.c`: окно во весь экран, сплошной цвет,
   нарочно **только `wl_shm`, без EGL и без hybris**. Так она остаётся
   годной, даже когда сломано ускорение, и разделяет «сломан показ» от
   «сломано ускорение».
3. **Поворот на четверть.** Телефон лежит боком, и найденный
   четырёхугольник выходит горизонтальным. Отдать его как есть нельзя —
   панель 720×1612 растянется поперёк. Сравниваю оба положения с настоящим
   отношением сторон и беру ближайшее.
4. **Где верх.** Сначала определял по вырезу под камеру — он у BF7 сверху и
   на белом поле виден тёмной выемкой. **Не работает:** ошибся ровно на
   пол-оборота, снимок вышел вверх ногами. Заменил на честную сверку:
   вторым шагом снимаю обычный рабочий стол и камерой, и через `grim`, и
   сравниваю корреляцией в двух положениях. Вышло 0.231 против −0.072 —
   различает уверенно.

**Про порог сходства.** Экран почти всегда тёмный, светлого на нём мало, и
даже при полном согласии корреляция редко переваливает за половину: сверка
рабочего стола дала 0.232. Порог согласия поэтому 0.15, а не 0.8 — иначе
инструмент будет кричать о поломке на исправной системе.

**Ещё одна ловушка, найденная на ходу:** белое поле держится 25 секунд, и к
началу второго шага оно ещё на экране. Оба снимка выходили белыми,
корреляция — ровно ноль, верх определялся подбрасыванием монетки. Теперь
поле снимается явным `pkill` перед сверкой.

Проверено: снимок камеры показывает сетку приложений с читаемыми подписями
(«Search apps», «Foliate», «Megapixels», «Веб-браузер», «Погода», «Show All
Apps») — то же самое, что видит `grim`. `--sverka` отвечает «свидетели
согласны».

## 2026-09-22 21:10 — Браузер: виноват не WebKit, а Epiphany

Разбор шёл от снимка, а надо было от процессов. Вот что выяснилось.

**WebKit полностью исправен.** `MiniBrowser` из комплекта
(`/usr/lib/aarch64-linux-gnu/webkit2gtk-4.1/MiniBrowser`) рисует пробную
страницу безупречно: фон `#ff00ff` на 932 884 пикселях, зелёный текст
`#00ff00`, заголовок, адресная строка. Это при том же окружении сеанса, с той
же отключённой сборкой слоёв. Значит ни ускорение, ни hybris, ни песочница,
ни заплатка про `eglTerminate` тут ни при чём.

**Epiphany 43.1 не рисует ничего** — ни внешнюю страницу, ни свою внутреннюю
`about:overview`. Панели при этом на месте и в узком виде работают правильно.

**Загрузка не начинается вовсе, а не идёт медленно.** Три независимых
свидетельства: полоса загрузки стоит на десятой доле; трассировка сетевого
процесса за двенадцать секунд не содержит ни одного обращения к файлу
(`strace` дал две строки на весь срок); в `ephy-history.db` чистого профиля
таблица `urls` пуста.

**Что проверено и отвергнуто:**

- отрисовка на процессоре (`WEBKIT_SKIA_ENABLE_CPU_RENDERING=1`) — то же самое;
- сборка слоёв включена (сняли `WEBKIT_DISABLE_COMPOSITING_MODE`) — то же самое;
- чистый профиль (`--profile=/tmp/chistyj-profil`) — то же самое;
- режим без сохранения сессии (`--incognito-mode`) — то же самое;
- расширение Epiphany, внедряемое в процесс отрисовки, убрано — то же самое
  (и оно исправно грузится: видно в трассировке `openat`);
- вложенный композитор пошире — опыт не удался, снимок пустой, бросил.

**Процессы живы и здоровы, а не падают.** Стек процесса отрисовки под `gdb`:
главный поток в своей петле `g_main_loop_run`, десять потоков на местах,
ничего не заблокировано. Стек Epiphany — тоже обычная петля `g_application_run`.
Сообщение «Web process crashed» появляется через 25 секунд после запуска и
означает не падение, а истёкшее ожидание.

**Побочная находка: восемь вкладок.** В рабочем профиле Epiphany
восстанавливал сессию из восьми вкладок, и под каждую заводился свой процесс
отрисовки — отсюда «шесть процессов подряд», которые я принимал за
перезапуски после падения. На чистом профиле процесс ровно один.

**Устаревшая подпорка.** В `bf7-phosh.service` стоит
`WEBKIT_DISABLE_COMPOSITING_MODE=1`, поставленная из-за
`Could not create GBM EGL display: EGL_NOT_INITIALIZED`. Эта причина закрыта
заплаткой в libhybris ещё утром; подпорку надо снимать, но сперва починить
Epiphany — иначе не отличить одно от другого.

**Подозрение, пока не доказанное.** В обоих случаях в журнале висит
`gtk_widget_size_allocate(): attempt to allocate widget with width -29`
у `DzlSuggestionEntry` и следом `pixman_region32_init_rect: Invalid rectangle`.
Число **−29 одинаково и на узком экране, и в широком вложенном композиторе** —
значит это не нехватка ширины, а постоянная ошибка расчёта.

**Инструменты, сделанные по дороге:** `/usr/local/bin/bf7-webproba2` — запуск
любой программы в окружении сеанса, со снимком и уборкой, с отключением
гашения экрана на время опыта; перехватчик `WebKitWebProcess` (рядом лежит
`WebKitWebProcess.real`), пишущий вывод, доводы и код выхода в
`/tmp/webproc.log`.

**Грабли опыта.** Запускать браузер из ssh «как есть» бесполезно: сеанс Phosh
живёт со своим окружением — своя шина D-Bus от `dbus-run-session`,
`GDK_GL=gles` и две наши прослойки в `LD_PRELOAD`. Без них epiphany молча
уходит, не показав окна, и это легко принять за поломку. Стенд берёт
окружение прямо из `/proc/<phosh>/environ`.

## 2026-09-22 21:49 — Браузер починен: виноват был блокировщик рекламы

Разгадка оказалась там, где её никто не искал.

**Epiphany откладывает каждое решение о переходе, пока не готов блокировщик.**
`decide_policy_cb` в `src/ephy-window.c` (исходники взяты `apt-get source`):

```c
if (!ephy_filters_manager_get_is_initialized (filters_manager)) {
  /* Queue request while filters initialization is in progress. */
  ... window->pending_decisions = g_list_append (...);
  return TRUE;
}
```

Очередь разбирается по сигналу `notify::is-initialized`. Пока его нет,
не грузится **ни одна** страница — ни внешняя, ни своя внутренняя.

**Готовым он не становился никогда.** Список правил берётся с
`easylist-downloads.adblockplus.org`, и с телефона этот узел недоступен:
имя разрешается, TCP на 443 открывается за 0,1 с, а рукопожатие TLS истекает
по сроку. Каталог `/root/.cache/epiphany/adblock/compiled` был пуст с ночи.
MTU ни при чём — пакет 1472 байта проходит без фрагментации; остальной
интернет работает (`deb.debian.org` отвечает 200 за 0,6 с). Хозяйская машина
качает тот же список за 1,4 секунды, так что дело в пути с телефона.

**Починка.** Список качает хозяйская машина и кладёт телефону в
`/usr/local/share/bf7/`, а `content-filters` указывает на местный файл:
`hybris/bf7-gpu/bf7-adblock-obnovit.sh` плюс запись в системной базе dconf.
Первая загрузка после смены списка занимает около двух минут — WebKit
перегоняет 6,8 МБ правил в двоичный вид (вышло 20 МБ); дальше мгновенно.
**Блокировщик при этом работает**, его не пришлось выключать.

**Снята устаревшая подпорка.** `WEBKIT_DISABLE_COMPOSITING_MODE=1` из
`bf7-phosh.service` убрана: причина, ради которой её ставили
(`EGL_NOT_INITIALIZED`), закрыта заплаткой в libhybris ещё утром. Заодно
выяснилось, что она и не помогала — пустая страница была от блокировщика.
Аппаратное ускорение Epiphany (`hardware-acceleration-policy='always'`)
работает; все настройки браузера вернулись к заводским, кроме
`content-filters`.

**Проверено до конца.** `bf7-web` с настоящим профилем (девять вкладок)
грузит `ru.wikipedia.org` целиком: текст, кириллица, картинки, вёрстка.
Снимок камеры с самой матрицы показывает то же самое — сходство с `grim`
0.162, свидетели согласны. Самопроверка: **44 в порядке, 0 с изъяном.**

**Чему это учит.** Я четыре часа искал поломку в отрисовке, потому что
симптом был «пустое окно». А отличить «не рисует» от «не грузит» стоило
одной проверки: пуста ли таблица `urls` в `ephy-history.db` и видит ли
сетевой процесс запрос к файлу. Обе дали ответ за минуту — и обе были
сделаны слишком поздно. **Сначала выясняй, на каком шаге цепочки обрыв, и
только потом чини шаг.**

Второе: голый `MiniBrowser` из комплекта WebKit — бесценный разделитель.
Он снял подозрение с WebKit, hybris, EGL и песочницы одним запуском.

**Заведены проверки, чтобы это не повторилось:** в самопроверку добавлены
две строки про список блокировщика (есть ли файл, скомпилирован ли), а в
установщик — громкое предупреждение, если файла нет. Отладочные перехватчики
`WebKitWebProcess`/`WebKitNetworkProcess` сняты, настоящие двоичные файлы
возвращены на место.

**Осталась отдельная странность для будущего:** почему с телефона не
проходит рукопожатие TLS именно к `easylist-downloads.adblockplus.org`
(Akamai, 23.53.32.x), когда TCP открывается мгновенно и MTU в норме.

## 2026-09-22 22:08 — Журнал ядра: тысяча строк в минуту, четверть от зарядника

Камера и починка браузера закончились, и я взялся смотреть, чем телефон занят
в покое. Оказалось — пишет журнал. За 103 минуты работы 102 186 строк ядра и
47 МБ в `/var/log/journal`. Это износ флеша и шум, в котором тонет настоящее.

**Разбор по источникам (строк в минуту):**

| источник | было | стало | как |
|---|---|---|---|
| зарядник: дамп регистров при WARN | ~360 | 0 | правка ядра |
| драйвер Wi-Fi, уровень INFO | ~500 | ~24 | `/proc/net/wlan/dbgLevel` = `0x03` |
| опрос термодатчиков питания | ~100 | 0 | правка ядра |
| `mt635x-auxadc`, канал VBIF | ~13 | 0 | правка ядра |

**Зарядник — настоящая ошибка, а не многословие.** `charger_routine_thread`
зовёт `charger_init_algo` по кругу, пока не удастся. Удаться не может: на
плате нет `primary_chg` — заряд ведёт вендорский демон из `/vendor`, а не
ядро. Но до этой проверки работа успевает зарегистрировать оповещателей у
найденных алгоритмов (`pe4`, `pd`), и на следующем круге регистрирует **тех
же самых повторно**. `srcu_notifier_chain_register` отвечает `WARN_ON` и
вываливает полный дамп регистров — каждые пять секунд.

Правка в `drivers/power/supply/mtk_charger.c`: оповещателей ставим один раз
за жизнь (`bf7_opoveshchateli_postavleny`), а бесплодные попытки после шести
громких переходят на молчаливые раз в минуту.

**Заводить драйвер зарядной микросхемы я не стал.** `aw32257` в дереве есть,
и он, вероятно, дал бы `primary_chg` — но это значит поставить второго
управляющего зарядом на живую батарею рядом с вендорским демоном. Заряд и так
работает. Цена ошибки несопоставима с выгодой от тишины в журнале.

**Термодатчики: вывод назвали отладочным, но пишут через `pr_notice`.**
В `thermal/mt6765/src/mtk_pmic_efuse.c` стояло `int mtktspmic_debug_log = 1`,
а `mtktspmic_dprintk` под ним — не `pr_debug`, а `pr_notice`. То есть
«отладка» шла в журнал по-настоящему. Поставил ноль; переменная осталась,
включить обратно можно отладчиком без пересборки.

**Про уровень Wi-Fi — честно: у меня нет объяснения.** По разрядам
`DBG_CLASS_INFO` это `BIT(5)`, значит `0x07` (ошибки, предупреждения,
состояние) обязан был убрать строки INFO. **Не убрал** — поток шёл прежний,
это измерено. `0x03` убирает. Почему — не знаю; значение подобрано опытом и
так и записано в `bf7-wifi-init`, чтобы следующий не тратил на это время.
Проверка `0x00` (всё выключить) понадобилась, чтобы вообще убедиться, что
рычаг действует.

**Ещё одна ловушка измерения.** Сначала я убавил уровень Wi-Fi и увидел, что
общий поток почти не изменился — 993 строки в минуту стали 850. Решил было,
что рычаг не работает. На деле у шума было несколько источников сравнимой
величины, и убрав самый заметный на глаз, я убрал лишь часть. **Считать надо
по источникам, а не по итогу**: разбивка `uniq -c | sort -rn` по
обезличенным строкам сразу показала и зарядник, и термодатчики.

## 2026-09-22 22:17 — Остановка по просьбе хозяина

Новое ядро прошито в `boot_a`, слот A выбран, телефон загрузился:
`4.19.325-cip124-st8-bf7-x657b-v1`, `uptime 0 минут`. Все модули пересобраны
тем же заходом и уложены в `/lib/modules/.../extra/`, `depmod` выполнен.

**Не проверено после прошивки** (делать при продолжении):

1. замолчал ли зарядник — `journalctl -b -k | grep -c charger_thread`;
2. замолчали ли термодатчики и `auxadc`;
3. поток строк ядра в минуту — было ~1000, ожидаю около 30;
4. поднялись ли Wi-Fi и Bluetooth на пересобранных модулях;
5. `bf7-selftest` целиком (до прошивки было 44 в порядке, 0 с изъяном);
6. встал ли уровень журнала Wi-Fi в `0x03` сам, из `bf7-wifi-init`.

## 2026-09-22 23:20 — После прошивки: Wi-Fi откатил я сам, Bluetooth падал на ложном пробуждении

Модель переключена хозяином на Opus 5.5. Наблюдатель по-прежнему недоступен;
по дополненному правилу все развилки ниже решены **без него и без хозяина**.

### Wi-Fi не поднялся — моя собственная ошибка

`bf7-wifi-init` грузил `connfem` и `wmt_drv` из `/opt/bf7/wifi/`, где лежат
**стоковые вендорские модули под чужое ядро** (`vermagic
4.19.191-g216400aba14e-dirty`) — `insmod` отвечал «Invalid module format».
Виноват я: поставил на телефон копию скрипта из `native-linux-build/`, а она
отстала от той, что жила на телефоне (там путь шёл через переменную). Правка
«убавить журнал Wi-Fi» потянула за собой откат всего остального.

Исправлено: путь один, `WIFI_MOD_DIR=/lib/modules/$(uname -r)/extra`,
`connfem` грузится только если есть (своей сборки нет, и он не нужен).
**Урок:** установщик `bf7-install.sh` этот скрипт не раскладывает — он живёт
отдельно, поэтому копии и разошлись. Перед заменой файла на телефоне
сравнивать его с тем, что ставлю (`diff`), а не верить, что в дереве свежее.

### Ещё одно нарушение моего же правила: `rmmod`

Чтобы перегрузить `wmt_drv`, я сделал `rmmod` — и он повис в состоянии D на
`kthread_stop`, оставив модуль наполовину выгруженным (`refcnt -1`). Ядро на
этот раз не упало, но исправить это можно было только перезагрузкой. Заметка
`bf7-modules-and-rmmod` предупреждала ровно об этом. Новые модули — только
через перезагрузку.

### «Три минуты» между открытием и мостом — скачок часов

Журнал показывал: `BT_open` в 23:07:46, «мост поднят» в 23:10:52. По
монотонным меткам — 66,3 с и 69,3 с, то есть ровно `sleep(3)` из кода.
Посередине, в 68,6 с, `systemd-timesyncd` синхронизировал часы и перевёл их
вперёд на три минуты. **Сравнивать события одной загрузки только по
`journalctl -o short-monotonic`**, особенно если рядом поднимается Wi-Fi.

### Bluetooth: ложное пробуждение в `BT_read`

Через 20 мс после создания `hci0` мост упал с `EIO`, а в ядре —
`WARNING at BT_read+0x498`: «Blocking read is waken up with no data but
rstflag=0». То есть **сброса чипа не было**: читателя разбудили, а очередь
пуста. Код считал это невозможным («Should never happen») и сдавался.

Страховка в мосте (терпеть до пяти подряд ложных `EIO`, успешное чтение
сбрасывает счёт) вернула Bluetooth сразу, без перезагрузки: `hci0 UP
RUNNING`. И показала масштаб: ложные `EIO` шли **десятками**, каждый снимался
со второй попытки — и каждый ронял в журнал ядра полный дамп регистров.

Корень исправлен в `kernel-work/bf7-bluetooth/stp_chrdev_bt.c`: при
`retval == 0 && rstflag == 0` — не `WARN`, а снова ждать. Верчения нет:
`flag` сбрасывается после каждого пробуждения. Настоящий сброс (`rstflag` 1
и 2) обрабатывается по-старому. Модуль собран и лежит в `extra/`,
вступает с перезагрузкой.

Отчего раньше работало — точно не знаю. Прежняя догадка «модули связности
не пересобраны» была, по-видимому, верна лишь отчасти: сейчас всё пересобрано
тем же заходом, а падение то же. Похоже на гонку, которой всё равно, в какую
сторону её подтолкнуть.

### Термодатчики: правил не тот каталог

Моя правка `mtktspmic_debug_log = 0` ушла в `thermal/mt6765`, а собирается
`thermal/mt6761` (проверено по `.o` в каталоге сборки). Хуже того, значение
переменной ни на что не влияло вовсе: `mtk_ts_pmic_probe` в
`thermal_zones/mtk_ts_pmic.c` **безусловно ставит 1** при старте. Правка
`mt6765` откачена, исправлен сам probe.

Пока ядро с этой правкой не прошито — рычаг на лету
`/proc/driver/thermal/tzpmic_log` и правило `hybris/bf7-gpu/tmpfiles/bf7-tiho.conf`
на каждую загрузку (в установщике). Проверено: после записи нуля — 0 строк
за 30 с вместо 48.

### Что уже измерено после прошивки

- предупреждение зарядника: **0** за загрузку (было 598);
- `auxadc VBIF`: **0**;
- уровень журнала Wi-Fi встал в `0x03` сам;
- поток строк ядра: **222 в минуту** (было ~1000) — ещё до гашения
  термодатчиков и до правки Bluetooth.

## 2026-09-22 23:28 — Проверено перезагрузкой: всё встало

Одна перезагрузка проверила всё разом (правки драйвера Bluetooth и правило
`tmpfiles` вступают только с загрузкой).

| что | итог |
|---|---|
| Wi-Fi | `wlan0 up`, модули из `extra/` |
| Bluetooth | `hci0 UP RUNNING`, **0** ложных `EIO` в мосте, **0** `WARN` в `BT_read` |
| зарядник | **0** предупреждений (было 598 за загрузку) |
| термодатчики | молчат с 27-й секунды, когда срабатывает `tmpfiles` |
| уровень журнала Wi-Fi | `0x03`, выставляется сам |
| поток строк ядра | **~200 в минуту** (было ~1000) |
| подсветка после пробуждения | 141/255 |
| самопроверка | **44 в порядке, 0 с изъяном**, упавших служб нет |

**Правка драйвера Bluetooth устранила корень**: страховка в мосте ни разу
не понадобилась — ни одного ложного `EIO` за всю загрузку, хотя до правки
они шли десятками.

**Выключение застряло на ~25 с** перед перезагрузкой — `stopping` при
аптайме 694 с. Похоже на нить моста в непрерываемом чтении `/dev/stpbt`
(известная ловушка, `SIGKILL` её не берёт); systemd дождался своего срока и
пошёл дальше. Не страшно, но заметно. Лечится переводом ожидания в
`BT_read` на `wait_event_interruptible` — отложено, это меняет поведение
для всех читателей узла и требует отдельной проверки.

**Самопроверка «44 против 43»** — не поломка: при спящем экране проверка
«панель включена» пишется справкой, а не засчитывается. Проверено: разбудил
экран — 44.

**Хвост журнала**, оставшиеся ~200 строк в минуту, — много мелких
источников по 3–17 строк: предупреждения очереди Wi-Fi, сообщения
энергосбережения SPM, «Trustonic TEE: daemon not connected», сторож. Дальше
гоняться за ними — убывающая отдача; оставлено.

**Ещё не прошито:** правка probe термодатчиков в `mtk_ts_pmic.c` (уберёт и
те ~117 строк первых 27 секунд). Войдёт со следующей пересборкой ядра.

## 2026-09-22 23:36 — Всё пересобрано против одной сборки; чтение stpbt больше не вечное

Вторая прошивка за вечер. Решения — самостоятельные, наблюдатель недоступен.

**Сборка связности указывала на ядро v2 при прошитом v3.** Модули вставали
лишь потому, что строка версии совпала. `KOUT` в
`scripts/build-bf7-connectivity-x657b.sh` теперь v3, с комментарием о двух
прошлых разах; та же сборка обязана стоять в `bf7-bluetooth/build.sh`.
Проверено на телефоне: `modinfo` показывает `4.19.325-cip124-st8-bf7-x657b-v1`.

**Термодатчики починены до конца.** Правка probe в
`thermal_zones/mtk_ts_pmic.c` вошла в ядро: периодического опроса в журнале
нет с самой загрузки, ещё до `tmpfiles`. Остались 67 строк — разовые
сообщения инициализации и калибровки в первые 4 секунды. Правило `tmpfiles`
теперь избыточно, но безвредно; оставлено как страховка на случай старого ядра.

### Непрерываемое чтение `/dev/stpbt` — давняя ловушка закрыта

С самого начала работы с Bluetooth в заметках стояло: «чтение `/dev/stpbt`
непрерываемое, застрявший читатель переживает `SIGKILL`, лечится только
перезагрузкой». Из-за этого однажды выключение висело 35 минут, а сегодня —
около 25 секунд.

Прошлая починка (закрывать узлы в обработчике сигнала моста) **не работала,
и я этого не знал**: в Linux `close()` не будит нить, уже заблокированную в
`read()` того же файла, — вызов держит свою ссылку на файл.

Теперь:

- в `BT_read` семафор на входе — `down_interruptible`, оба ожидания —
  `wait_event_interruptible`, прерывание отдаёт `-ERESTARTSYS`;
- мост по сигналу делает `_exit(0)`: ядро само шлёт смертельный сигнал всем
  нитям, и теперь они могут умереть.

Порознь включать нельзя: новый мост со старым драйвером оставил бы нить в
состоянии D. Поэтому оба вошли одной перезагрузкой.

**Проверено:** `systemctl stop bf7-bluetooth` — **54 мс**, процессов моста
не осталось, `/dev/stpbt` никто не держит; `start` — `hci0 UP RUNNING` через
4 секунды. Bluetooth теперь можно перезапускать без перезагрузки телефона.

**И адаптер действительно работает, а не только «UP».** `bluetoothctl scan on`
и `btmgmt find` находят реальные устройства с уровнем сигнала (−51…−68 дБм),
LE Random и LE Public.

Ловушка измерения: первый подсчёт через `grep "CHG\] Device"` дал ноль — в
выводе `bluetoothctl` вокруг `CHG` стоят цветовые коды ANSI. Вывод «поиск не
работает» был бы ложным. Смотреть сырой вывод, прежде чем считать.

### Итог после второй прошивки

Wi-Fi и Bluetooth поднялись с загрузки, ложных `EIO` — 0, `WARN` в
`BT_read` — 0, предупреждений зарядника — 0, упавших служб — 0.

## 2026-09-22 23:40 — Заводской адрес Bluetooth встал: мешала та же ложная побудка

Утром записано: «заводской адрес `E4:F8:BE:xx:xx:xx` известен, команда
0xFC1A его принимает, но связь STP смены не переживает — следующее чтение из
`stpbt` отвечает `EIO`, мост падает». Выдержки 1+1 и 3+5 секунд не помогали.
Вызов `bf7_postavit_adres` был выключен.

Починив вечером ложное пробуждение в `BT_read`, я заподозрил, что это оно и
было: смена адреса как раз даёт пробуждение без данных, а драйвер на нём
сдавался. Проверка стала дешёвой — Bluetooth теперь перезапускается без
перезагрузки телефона, — так что просто включил вызов обратно.

**Встал с первого раза.** `bluetoothctl show`: `Controller
E4:F8:BE:xx:xx:xx (public)`, `Powered: yes`; поиск находит 8 устройств;
через минуту мост жив, ложных `EIO` — 0, `WARN` в `BT_read` — 0.

Мораль та же, что с браузером: **причина, записанная как свойство железа
(«чип не принимает адрес»), оказалась ошибкой в нашем коде**, которая просто
проявлялась в этом месте. Стоит перепроверять старые «так устроено железо»
каждый раз, когда чинится что-то соседнее.

В самопроверку добавлено: адрес-заглушка `00:00:46:76:63:01` — это провал,
значит установка адреса сломалась.

## 2026-09-23 00:00 — Фонарик работает; сеанс logind для оболочки — откачен

### Фонарик: MT6370 на плате нет, вспышка — клон LM3644 на i2c-5:0x63

В переписи железа фонарик числился за MT6370 («чип без драйвера, дешёвая
победа»). Драйвер MT6370 в ядре, оказывается, собран — и его probe на 5-0034
кончается `-121`: **по этому адресу никого нет**. Дерево устройства описывает
соседний вариант платы.

Настоящий драйвер вспышки — узел `mediatek,strobe_main` на i2c-5 по адресу
`0x63`, без драйвера. В дереве для него два взаимозаменяемых узла,
`flashlights_aw36515` и `flashlights_ocp81375`, оба с ножкой включения HWEN =
GPIO 152 (вычислено по `pinmux` состояний `hwen_high/hwen_low`). Драйверов ни
для того, ни для другого в нашем ядре нет.

Регистры после сброса (прочитаны `i2cget`, поставил `i2c-tools`) **в точности
совпадают с TI LM3644 / AW3644** — `01=80 02=01 03=bf 04=3f 05=bf 06=3f 07=09
08=1a 09=08` — кроме номера устройства `0c=3a`. Смысл регистров сверен с
`flashlights-aw3644.c` из нашего дерева: `0x01=0x09` — фонарь на первом
светодиоде. Проверка руками: флаги неисправностей нулевые, а камера в тёмной
комнате показала среднюю яркость кадра 0,3 → 129,6 → 0,2.

Свой модуль `kernel-work/bf7-torch/bf7_torch.c`: платформенный драйвер на узле
`flashlights_aw36515` (ножка HWEN — из его состояний, без жёстких номеров),
чип — через `of_find_i2c_device_by_node` узла `strobe_main`. Устройство класса
leds **`white:torch`**, пять ступеней тока до значения самого чипа после
сброса (выше не поднимаемся). При выключении HWEN опускается — чип
обесточен целиком. При выключении телефона фонарь гасится (`.shutdown`).
Грузится при старте (`/etc/modules-load.d/bf7-torch.conf`).

Проверено: ступени 1/3/5 дают ток `0x8c/0xa6/0xbf`, флаги нулевые, камера —
130,5 включённый и 0,2 выключенный.

**Грабли опыта:** `gpioset --mode=signal ... &` внутри `ssh` держит сеанс
открытым, команда повисает — а фонарь в это время **горит**. Выключил вторым
подключением. Фоновые процессы в `ssh` — только через `setsid` с
перенаправлением всех трёх потоков.

### Сеанс logind для оболочки — попытка и откат

Зачем был нужен: кнопка фонарика в шторке Phosh ищет устройство `*:torch`
(это теперь есть) и включает его через `SetBrightness` сеанса logind, по пути
`/org/freedesktop/login1/session/auto`. Оболочка живёт в `system.slice`, сеанса
у неё нет — вызов не проходит. Попутно без сеанса ругаются gnome-session
(«Could not get session id… pam_systemd») и PolKit («No session for pid»).

Что выяснилось по дороге, по порядку:

1. **`PAMName=` без явного `User=` молча не работает** — systemd PAM не
   открывает, в журнале ни строчки. Нужен `User=root`, хоть служба и так от root.
2. **`pam_systemd` переставляет `XDG_RUNTIME_DIR` на `/run/user/0`** поверх
   окружения службы — сокет композитора уезжает, и отваливается всё, что ищет
   его в `/run/bf7-phosh` (отпиралка, браузер, снимки, самопроверка). Лечится
   `env XDG_RUNTIME_DIR=...` прямо в строке `ExecStart`.
3. **`SetBrightness` требует сеанс на переднем плане** — «Session is not in
   foreground, refusing». На свободном `vt7` сеанс «в сети», но не активен
   (активен `tty1`). Я сперва записал в комментарии, что «в сети» хватит, —
   неверно.
4. **Главное, из-за чего откат:** с `PAMName` процессы оболочки переезжают из
   cgroup службы в `session-N.scope`, и **`systemctl restart` их больше не
   достаёт**. После двух перезапусков жили три поколения оболочки сразу
   (сеансы 57, 64, 68), экран держал старый `phoc`, новый отвечал
   «no wl_output». Не помогал и `loginctl terminate-session` — сироты
   пережили его. Вычистил `kill-session --signal=SIGKILL`.

`LIBSEAT_BACKEND=builtin` сработал как задумано: доступ к экрану и вводу с
сеансом не изменился.

Откат — к копии службы до правки; одно поколение оболочки, снимок снимается.
Вернуться к сеансу можно, но только с продуманным жизненным циклом: при
остановке службы — завершать свой сеанс (номер сеанса сценарий сеанса может
записать в `/run/bf7-phosh`), и проверить несколько циклов stop/start подряд.

**Что осталось от попытки полезного:** из `bf7-phosh-session` убраны запуски
`pipewire`, `wireplumber` и `pipewire-pulse` — их в системе нет, они лишь
сыпали «not found» и стоили **двух секунд `sleep`** на каждом старте сеанса.

## 2026-09-23 00:13 — «Фонарик» как приложение; отпиралка перестала давить вслепую

**Сначала был ярлык на переключатель** — и плохой опыт: Phosh после любого
запуска сворачивает сетку и ждёт окна приложения. Окна нет — остаётся голый
фон, а чтобы погасить фонарь, сетку надо разворачивать заново.

Поэтому «Фонарик» — маленькое приложение (`hybris/bf7-gpu/fonarik/`,
Python + GTK3 + libhandy): открыл — горит на последней выбранной яркости;
большая круглая кнопка — погасить/зажечь; ползунок — пять ступеней, выбор
запоминается; закрыл окно **или прибил сигналом** — фонарь гаснет. Значок
нарисован свой: в теме нет ни `torch-*`, ни `flashlight-*`.

Проверено пальцем (инжектор касаний) от значка до закрытия: открыл — 5,
ползунок на вторую — 2 на ходу, кнопка — 0, кнопка — 2, `SIGTERM` — 0.

**Три ошибки по дороге, все мои:**

1. `from gi.repository import Gdk, ...` без `require_version("Gdk", "3.0")`
   подтянул Gdk 4.0, и GTK3 падал на старте молча — сетка сворачивалась,
   окна не было.
2. Забыл `show_all()`: в GTK3 дочерние виджеты скрыты, окно выходило
   ровным серым фоном. Выглядело точь-в-точь как давняя «пустая страница
   браузера» — но причина совсем другая.
3. Координаты значка снял со снимка `grim`, который **уже** в физических
   пикселях (720×1612), — и зачем-то удвоил. Касания уходили мимо.
   `bf7-touchinj` принимает физические, и снимок физический: пересчитывать
   ничего не надо.

### `#023c88` — это фон GNOME, а не чьё-то окно

На экране несколько раз оказывалось ровное синее поле `(2, 60, 136)`. Это
`org.gnome.desktop.background primary-color` по умолчанию: виден голый фон
Phosh, когда сетка свёрнута и окон нет. **Задним числом объясняет** «браузер
заливает всё синим»: браузер ничего не рисовал, сквозь пустое окно
просвечивал фон.

### Отпиралка `bf7-otperet` теперь спрашивает, прежде чем давить

Прежняя вслепую давила касание, свайп вверх, четыре цифры и «Unlock». На уже
открытом экране свайп сворачивал сетку, а «цифры» попадали в значки. Теперь
она спрашивает оболочку (`org.gnome.ScreenSaver.GetActive` на шине её
сеанса): заблокирован — полная череда; открыт, но погашен — одно касание в
пустое место; открыт и светится — ничего. Исходник впервые в дереве:
`hybris/bf7-gpu/bf7-otperet.sh`.

Самопроверка: **46 в порядке, 0 с изъяном** (добавлен фонарик).

## 2026-09-23 00:20 — Вибрация уведомлений не работала никогда; теперь работает

В переписи вибро числилось сделанным («через uinput»). Проверка через
`fbcli -E message-new-instant`: `feedbackd` событие принимает, мотор — ни
разу. Три слоя поломки, каждый прятал следующий:

1. **`feedbackd` не видел устройство.** Мост `bf7-vibrad` выставляет мотор как
   устройство ввода `bf7-vibrator` с `FF_RUMBLE`/`FF_PERIODIC`, но `feedbackd`
   берёт только устройства с меткой udev `FEEDBACKD_TYPE=vibra`, а его правило
   метит лишь путь `platform-vibrator`. Своё правило:
   `hybris/bf7-gpu/units/90-bf7-feedbackd.rules`. После него `feedbackd`
   открыл `/dev/input/event3` — а мотор всё равно молчал.
2. **Мост включал мотор не тем способом.** Писал в `brightness`, а этот мотор
   включается спуском: срок в `duration`, единица в `activate`. В памяти это
   было записано давно («вибро — не яркостью, а спуском»), а мост писался
   мимо записи. Проверено на железе: яркость 200 — `state` 0; спуск на 300 мс
   — `state` 1 и через 0,3 с снова 0.
3. **Сила эффекта читалась не из того поля.** `u` в `struct ff_effect` —
   объединение, и мост брал `u.rumble` у любого эффекта, а `feedbackd` шлёт
   `FF_PERIODIC`, где на том месте лежат тип волны и период. Теперь разбор
   по типу; мотор всё равно умеет только вкл/выкл, важен лишь ноль.

Итог по следу `strace`: на `button-pressed` — `duration=15`, `activate=1`;
на `message-new-instant` — серия импульсов по 150 мс. Мой первый замер
«раз в 100 мс» пятнадцатимиллисекундный импульс просто не ловил — выводы
делать только по следу.

**Попутно — расход батареи.** Мост опрашивал устройство без ожидания со
сном 20 мс: **~500 пробуждений процессора за 10 секунд покоя, круглосуточно**.
Переведён на `poll()` — стало **0**.

## 2026-09-23 00:27 — Кто будит процессор при погашенном экране

После находки с опросом в `bf7-vibrad` я замерил пробуждения всех процессов
за 20 секунд покоя с погашенным экраном: разность `voluntary_ctxt_switches`
плюс `nonvoluntary_ctxt_switches` из `/proc/*/status`. Всего ~400 в секунду
(треть — мой собственный замерочный `bash`). Главные:

| кто | пробуждений в секунду | что это |
|---|---|---|
| `rpmb_open` | 41 | поток ядра, ждёт демона TEE Trustonic, которого нет |
| `bf7-camd` | 26 + 4,7 % ядра | мой демон камеры — камеру никто не смотрит |
| `rcu_*`, `migration/*`, `kworker` | десятки | служебные потоки ядра |
| потоки Wi-Fi | ~20 | драйвер |
| `bf7-blank` | 2,4 | мой, опрос состояния вывода раз в 0,5 с |

### `bf7-camd`: ожидание зрителя — на событиях

В покое демон не может замолчать совсем: `v4l2loopback` объявляет узел
камерой, только пока в него пишут. Поэтому он писал поддерживающий кадр
2,5 раза в секунду, а между кадрами спал восемью кусками по 50 мс, чтобы
быстро заметить зрителя. Отдельный поток перечитывал счётчик открытий раз
в 250 мс.

Теперь:

- наблюдатель ждёт открытия и закрытия `/dev/video0` через **inotify**
  (проверено `inotifywait`: `OPEN` и `CLOSE` для узла устройства приходят) и
  перепроверяет без события лишь раз в полминуты;
- при смене числа зрителей он будит главный цикл через `eventfd`;
- главный цикл в покое спит на `poll` до будильника или секунды —
  поддерживающий кадр раз в секунду.

**Замер:** было ~520 пробуждений за 20 с у одной главной нити и 4,7 % ядра;
стало **52 на все нити и 1,6 %**. Зритель (`ffmpeg`, 60 кадров) — около
25–30 кадров в секунду, как прежде. Сравнение со старой версией в тех же
условиях дало тот же кадр (объектив прижат к столу — оба почти чёрные, это
не поломка).

**Следующий шаг, не сделан:** матрица и блок обработки изображения в покое
всё равно крутятся на 30 кадрах в секунду — это, скорее всего, главный
расход, больший, чем процессор. Остановить их в покое — сложная правка
большого демона; измерить выгоду током нельзя, пока телефон на кабеле
(при 100 % систему питает USB, в батарею идёт лишь подпитка ~39 мА).

`rpmb_open` — в ядре, отложен до следующей пересборки.

## 2026-09-23 00:48 — Звуковой сервер засыпает; ожидание Trustonic — раз в секунду

### `rpmb_open`: правил не тот каталог — и это вскрылось замером

Первая правка ушла в `drivers/misc/mediatek/gud/410`, прошивка — а замер
сдвинулся лишь с ~830 до 714 пробуждений за 20 с. Стек потока
(`/proc/PID/stack`) показал `msleep ← request_send ← tee_object_get`, а
объектного `admin.o` для `gud/410` в каталоге сборки не оказалось вовсе.
Собирается `drivers/tee/gud/500` (`CONFIG_TRUSTONIC_TEE_VERSION="500"`), где
цикл ждёт демона через `msleep(20)`. Правка там — `msleep(1000)`, вторая
прошивка: **19 пробуждений за 20 с вместо ~830**. Второй промах каталогом за
ночь; урок записан в память отдельно — сначала найти `.o`.

### Звуковой сервер: `module-suspend-on-idle` включён

При подсчёте по всем нитям процессов лидером оказался `pulseaudio` —
~113 пробуждений в секунду при погашенном экране. Приёмник и источник
стояли в `IDLE`, а не `SUSPENDED`: засыпание было выключено **намеренно**,
с записью «через 20 секунд после закрытия потока звук пропадает».

Перемерено приборно — **акустической петлёй** (`tools/bf7-zvuk-petlya.sh`):
телефон играет тон 1 кГц через динамик и пишет его своим микрофоном.

| условие | тон над шумом |
|---|---|
| опора, устройство открыто | 68–70 дБ |
| после 36 с сна | 68–70 дБ |
| после 3 мин сна | 68–71 дБ |
| после перезапуска сервера с новой настройкой | пик 70 дБ |

Шум без тона — 8–18 дБ. Начало звука после сна не обрезано. Прежнее
опасение не подтверждается — вероятно, его сняли позднейшие починки тракта
(усилитель на GPIO 151 держит `bf7-extamp`, ключи включает `bf7-audio-up`).

Пробуждений сервера во сне — **0 за 30 секунд вместо ~3400**.

### Итог по системе при погашенном экране

Всего пробуждений (без замерочного `bash`) — **5672 за 20 с вместо 8392**.
В верхних строках остались лишь внутренние потоки ядра (`rcu_*`,
`kworker`, `migration`, `ipi_cpu_dvfs_rt`).

## 2026-09-23 00:50 — «Не проходит TLS» — это фильтр провайдера по имени сайта, не телефон

Открытый вопрос с браузера: почему `easylist-downloads.adblockplus.org` не
отвечает на рукопожатие TLS при мгновенном TCP. Проверил дюжину сайтов с
телефона. Не проходят трое: `www.google.com`, `www.youtube.com` и тот самый
`easylist-downloads.adblockplus.org`; девять (Debian, GitHub, Cloudflare,
Apple, Microsoft, Википедия, Яндекс, Akamai, ВКонтакте) — за 0,2–0,3 с.

С хоста те же трое проходят — но хост ходит в сеть через WireGuard
(`ip route get 8.8.8.8` → `dev nyc-homepc table 51820`), а телефон напрямую.

**Решающий опыт — подмена имени в рукопожатии** (проверка сертификата
выключена, важен лишь сам обмен):

| адрес | имя в рукопожатии | итог |
|---|---|---|
| GitHub | github.com | прошло, 0,17 с |
| GitHub | www.youtube.com | **зависло** |
| Google | www.google.com | **зависло** |
| Google | example.com | прошло, 0,13 с |

Глушится соединение с определённым именем, куда бы оно ни шло, а сервер
Google с «чужим» именем отвечает. Это фильтрация провайдера по SNI, у
телефона со стеком всё в порядке.

**Что из этого следует.** Местный список правил блокировщика (починка
браузера) — правильное решение и по этой причине. Для Google и YouTube на
телефоне нужен свой обход — VPN, как у хоста. Это ключи и сеть хозяина,
поэтому сам не настраиваю; отмечено в списке доводки.

## 2026-09-23 01:05 — Первая проба сна: телефон уснул и сам не проснулся

**Что хотел.** Проверить, умеет ли телефон спать по-настоящему (разница между
часами и днями работы от батареи). Ядро сон поддерживает: `/sys/power/state`
= `freeze mem`, `mem_sleep` = `s2idle [deep]`; есть RTC `mt6397-rtc` с
будильником и `rtcwake`. Выбрал самый лёгкий сон, `freeze` (s2idle), на
15 секунд с пробуждением по RTC. Решение принято самостоятельно —
наблюдатель недоступен.

**Что вышло.** `rtcwake` застрял ещё до сна, на подготовке консоли:
`pm_suspend → pm_prepare_console → vt_move_to_console → vt_waitactive`.
Ядро перед сном переключает виртуальный терминал на свою консоль и ждёт,
пока текущий терминал отпустят, а им владеет композитор (libseat в режиме
`builtin` держит терминал в `VT_PROCESS`) и отпускать не стал.

**Моя ошибка.** Ожидание прерываемое, и я снял `rtcwake` сигналом, решив,
что сон этим отменится. Не отменился: `pm_prepare_console` ошибку не
возвращает, процедура пошла дальше — и телефон уснул. А будильник RTC к
тому времени уже отзвенел впустую, так что будить стало нечему. Пять минут
ни пинга, ни `ssh`; камера — темнота. Попытка разбудить со стороны хоста
(снять и вернуть `authorized` у USB-устройства) не помогла: устройство не
ответило даже на отключение.

**Нужно нажать кнопку питания.** Разбудить сам я не могу.

**Что чинить, чтобы сон заработал:** ядро не должно переключать терминал
перед сном. Для этого драйвер экрана объявляет
`pm_vt_switch_required(dev, false)` (так делает обычный DRM через
fb_helper; у нас mtkfb/bf7-disp этого не делают), либо параметр ядра
`no_console_suspend`. И впредь: **прерывать сон на полпути нельзя** —
если `rtcwake` завис, ждать или разбираться, но не убивать.

## 2026-09-23 01:22 — Пока телефон спит: разбор оставшихся пробуждений по исходникам

Телефон уснул в первой пробе сна и не просыпается. Удалённо разбудить не
вышло ничем: у корневого концентратора хоста нет управления питанием портов
(`wHubCharacteristic 0x000a`, «No power switching»), сброс шины и
переподключение `authorized` не будят, по Wi-Fi телефон не отвечает даже на
ARP (`192.168.27.52`, пробуждения по пакету нет), по Bluetooth вызов на
`E4:F8:BE:xx:xx:xx` — «Host is down». Нужна кнопка питания.

**`kworker/…-events_freezable`, ~33 в секунду.** В эту очередь из
собираемого кода кладут: десяток термозон MediaTek (процессор, PMIC,
батарея, Wi-Fi, матрица камеры, стабилизаторы 6357buck1/2, dctm), ядро
тепловой подсистемы, MMC, USB storage. Термозоны опрашиваются по таймеру
раз в секунду каждая (`interval = 1` → `polling_delay = 1000` мс) — это и
есть основная часть. **Трогать не стал сознательно:** замедлить можно лишь
через вендорский формат настройки в `/proc/driver/thermal/tz*`, где тем же
письмом задаются и пороги срабатывания; ошибка в нём способна отключить
защиту от перегрева, в том числе батареи. Несколько пробуждений в секунду
этого не стоят.

**`bf7-blank`, 2 пробуждения в секунду.** Опрос атрибута `enabled`
соединителя: на смену состояния DRM событий не шлёт. Сделать событием можно
лишь своим uevent из `bf7_drm` — но событие изменения на устройстве DRM
увидят и udev, и композитор; ради двух пробуждений не рискую.

**Кандидат покрупнее, к проверке после пробуждения:** режим
энергосбережения Wi-Fi (`iw dev wlan0 get power_save`). Если он выключен,
радио не спит между маяками точки доступа — обычно это больше, чем все
пробуждения вместе. Меняется на лету.

**Подготовлено к пробуждению:** `tools/bf7-proshit.sh` (прошивка со сверкой
модулей по контрольным суммам — проверена на подпорченном `Module.symvers`),
образ с починкой сна (`skip_vt_switch` в `mtkfb`), сон матрицы камеры в
`bf7-camd` (не зафиксирован до испытания) и его проверка
`tools/bf7-kamera-son-proverka.sh`.

## 2026-09-23 01:30 — Пока телефон спит (2): «погашенный» экран не гас, и это держит сон

**Главная находка — по исходникам.** `bf7_pipe_disable` (так композитор
гасит экран) лишь выводил чёрный кадр. Тракт — панель, её контроллер, канал
DSI, блок дисплея — оставался под питанием и обновлялся 60 раз в секунду при
погашенной подсветке. Из-за этого же висели оба держателя пробуждения,
замеченные перед пробой сна:

- `pri_disp_wakelock` (`primary_display.c`) берётся при старте и отпускается
  **только** в `primary_display_suspend()`, которую звал лишь старый путь
  гашения `mtkfb_blank`;
- `cmdq_wake_lock` (`mtk-cmdq-mailbox.c`) держится, пока включены часы CMDQ
  (`usage > 0`), а главный их пользователь — циклы того же дисплея.

Пока они активны, ядро отменит любой сон, даже после починки переключения
терминала. Починка в `bf7_drm.c` повторяет `mtkfb_blank` (FB_SUSPEND +
`primary_display_suspend`, FB_RESUME + `primary_display_resume`), собрана в
образ **выключенной** и включается на лету:
`echo 1 > /sys/module/bf7_drm/parameters/usyplyat_panel`. Разбуживание при
включении срабатывает всегда, даже если параметр успели выключить.

**Глубокий сон (`deep`), обзор.** В сборке есть `mtk_sleep.c` (операции
сна платформы), драйверы SPM для MT6761, SSPM. Источники пробуждения из
глубокого сна задаются не в ядре, а в защищённой прошивке:
`pwrctrl->wake_src = SMC_CALL(GET_PWR_CTRL_ARGS, …)` в
`mtk_spm_suspend.c`. Раз Android на этом телефоне просыпается по будильнику
и кнопке, RTC почти наверняка среди них — но пробовать `deep` только после
того, как заработает `freeze`.

**Инструменты к пробуждению** (всё в git):

- `tools/bf7-posle-probuzhdeniya.sh` — весь план одним заходом с откатами:
  прошивка, гашение тракта экрана, энергосбережение Wi-Fi, сон матрицы
  камеры, замер; пробу сна намеренно не включает;
- `tools/bf7-kto-budit.sh` — кто будит процессор с точностью до функции
  работы, таймера и прерывания (точки трассировки ядра; разбор проверен на
  образце строк);
- `tools/bf7-proshit.sh` — прошивка со сверкой модулей по CRC символов.

**Разбудить телефон удалённо не удалось ничем** (USB, Wi-Fi, Bluetooth —
подробности в записи выше). Нужна кнопка питания.

## 2026-09-23 02:31 — Где остановились: телефон спит, план ждёт кнопки

Телефон спит с первой пробы сна (около 00:58) и сам не просыпается; удалённо
разбудить нечем. На хосте в фоне взведён наблюдатель (до 8 часов): как только
телефон появится на связи, он запускает `tools/bf7-posle-probuzhdeniya.sh` —
прошивка (починка терминала + выключенное по умолчанию гашение тракта экрана),
испытание гашения экрана с откатом, режим энергосбережения Wi-Fi, сон матрицы
камеры с откатом, замер «кто будит» с горячим подключением ядер.

Если сессия прервётся раньше — после нажатия кнопки питания запустить этот
сценарий руками. Пробу сна после него — только `bf7-son-proba freeze` и
только если шаг с гашением экрана прошёл (без него держатели не отпустят).

## 2026-09-23 07:50 — Телефон не спал с 01:56; я смотрел в сломанный канал

Хозяин: «он уже 5 часов как не спит». Так и есть. Лёгкий сон из первой пробы
**сработал и завершился сам**: `pm_system_irq_wakeup: 269 triggered
mt6358-irq`, «PM: suspend exit» в 01:56 по Москве — разбудило прерывание
PMIC (кнопка или событие зарядки). Аптайм непрерывный (7 ч 11 мин), система
`running`, Wi-Fi живой. Во сне раз в минуту будил контроллер топлива
(`fg_update_routine_wakeup`), и система засыпала обратно — то есть s2idle
на этом ядре в целом работает, и выход из него тоже.

**Моя ошибка — шесть часов.** Я проверял, жив ли телефон, только пингом
USB-сети `192.168.7.2`. А этот канал я сам и сломал в 01:18, пытаясь
разбудить телефон отключением порта (`usb1-port9/disable`, `authorized`):
порт застрял в состоянии `default`, устройство не перечислилось, интерфейс
на хосте исчез. Наблюдатель до утра честно писал «телефон спит». Проверь я
второй канал — Wi-Fi (`192.168.27.52` отвечал) или камеру — узнал бы сразу.

Вернул USB: `echo 1` / `echo 0` в `usb1-port9/disable` — порт
`configured`, `192.168.7.2` отвечает. Урок записан в память
(bf7-candidate-boot-checklist): прежде чем объявлять телефон спящим или
мёртвым — второй канал.

## 2026-09-23 09:00 — Сон тракта экрана, tty1 под phoc, загрузка втрое быстрее

Утро после плана пробуждения. Всё ниже проверено на телефоне, закоммичено
(ядро: 1dbc0efa7, 0946bfa29, b6d8ee807; репозиторий: ad9b535, bc1dad5,
6f90f1a, fa4b19d).

**Камера.** Сон матрицы работает: 0,2% ядра в покое (было 1,6%), 40 живых
кадров за 1,8 с, после зрителя снова сон. Проверочный сценарий искал
сообщения в журнале systemd, а демон пишет в `/tmp/camera.log` — поправлен.

**Гашение тракта экрана — ложный провал плана.** Шаг 3 плана сказал «не
вышло». Две причины, обе в проверке, а не в ядре: экран к тому времени уже
погас по простою (`wlr-randr --off` выключать было нечего — в
`/sys/kernel/debug/dri/1/state` CRTC `enable=0`), а из журнала бралась
последняя строка, и «усыплён» прятался за «разбужен» через 0,3 с. Новая
проверка `tools/bf7-ekran-son-proverka.sh` гасит тем же путём, что система по
простою (`PowerSaveMode` у `org.gnome.Mutter.DisplayConfig`), и смотрит всё с
начала.

**Первая версия гашения была неполной.** Прямой `primary_display_suspend()`
усыплял тракт, но без оповещений fb: сторож тачскрина TD4160 (касание и
дисплей — одна микросхема) раз в секунду читал спящую микросхему — 11 «Failed
to read from device» за 10 с, — а вендорские правила «экран погашен»
(dvfsrc, cm_mgr, mtk_menu, ged, ION, звук, ток зарядки) не включались вовсе.
Теперь `bf7_drm` зовёт `fb_blank()` на буфере mtkfb с замками как у
`FBIOBLANK` (console_lock, lock_fb_info, `FBINFO_MISC_USEREVENT`); своего
fbdev у bf7_drm нет, так что порядок замков безопасен. Просмотрел каждого
слушателя: m4u лишь бережёт защищённые регистры, звук разрешает себе снижать
напряжение ядра, PPM LCM_OFF выключен. Попутно в обработчике тачскрина
разыменовывался неприсвоенный `transition` — исправлено.
Итог проверки: тракт спит, `pri_disp_wakelock` и `cmdq_wake_lock` отпущены,
«SCREEN OFF» дошёл, сторож молчит, после пробуждения тач в рабочей прошивке
(та же последовательность, что при загрузке), снимок и камера показывают
экран. Включено по умолчанию через `tmpfiles/bf7-ekran-son.conf`.
**Цена:** погасший экран будит только кнопка питания — касание уже нет
(пока экран лишь притушен, будит как прежде).

**Утечка sysctl в каждом сетевом пространстве.** WARNING со слепком
регистров в `retire_sysctl_set` при выходе каждой службы с
`PrivateNetwork=yes`. Пары `register_net_sysctl`/`unregister_sysctl_table`
через kprobe вокруг `unshare -n true`: 52 регистрации, 51 снятие, не снята
таблица по пути `net` — это MediaTek'овский `net/optr` (vzw_hdr) в
`ipv6_sysctl_net_init`, в локальной переменной. Значение глобальное —
регистрирую один раз в init_net. После: 0 WARNING, `/proc/sys/net/optr` на
месте.

**Wi-Fi.** Энергосбережение было выключено (NM его не трогал). Замер по ssh
через Wi-Fi: прерывания 9→6/с, приём 28→26,6 МБ/с, отдача 24→22,5 МБ/с (в
пределах разброса), редкий ping 2,6→55 мс, плотный — 3,3 мс. Включено для
всех сетей (`bf7-nm-powersave.conf`), NM ставит его сам при подключении.

**Курсор консоли** будил процессор 2×4,9/с — `cursor_blink=0` в tmpfiles.
Замер «кто будит» при усыплённом тракте: курсора нет, SSPM_MBOX 4,2→2,3/с,
timer@10017000 51,7→44/с. Глубокий простой (DP/SODI3/SODI) по-прежнему 0 —
его держит `SPM_RESOURCE_USER_SSUSB` (бит 2 в `resource_req_block user:
0x4`), то есть USB-кабель отладки. На батарее этого держателя нет; не чиню.

**tty1 был в текстовом режиме под phoc.** Нажатие кнопки (uinput KEY_POWER
от нового `bf7-knopka`) будило усыплённый тракт из kworker в обход DRM:
`console_callback` → `poke_blanked_console` → `fb_blank(UNBLANK)`. Так бывает,
только если активный терминал в `KD_TEXT` — и он был: `KD_TEXT`,
клавиатура `K_UNICODE`, `VT_PROCESS`, а на tty1 работал `agetty`. seatd
(libseat 0.7, встроенный) графику ставит (`vt_open`: KD_GRAPHICS, K_OFF) —
после перезапуска сеанса tty1 в графике. Наблюдатель режима 10 Гц с ранней
загрузки поймал: 52,24 с seatd поставил графику, 53,17 с процесс «(agetty)»
вернул текст. getty@.service — `Type=idle`: systemd выполняет его до пяти
секунд после «Started», и его `TTYReset`/`TTYVHangup` ложатся уже после
phoc. Вставка `bf7-getty-tty1.conf` (ConditionPathExists): getty не идёт на
tty1, пока включён bf7-phosh. Маскировать не стал — установщик сознательно
ничего не маскирует.

**bf7-blank отставлена.** Кнопку обрабатывает Phosh (проверено: гасит и
зажигает сам). Её свой счётчик расходился с Phosh — в 07:47:53 нажатие,
разбудившее экран через Phosh, она приняла за гашение и выключила тачскрин
на горящем экране. Её записи в `/sys/class/backlight` gsd-power запоминал
как выбор хозяина: после каждого пробуждения экран возвращался на 12 из 255
(gsd показывал 4%, хотя сеанс при старте поднял до 55%).

**Загрузка 24,5 с вместо 59–77.** Критическая цепочка показала три
задержки:
1. `bf7-vibrad` с `After=multi-user.target` и `Before=bf7-phosh` держал
   сеанс до конца загрузки ВСЕХ служб;
2. `bf7-pulse` с `ExecStartPost=sleep 4; bf7-audio-up` считался запущенным
   через 11 с, а сеанс стоит после него; `bf7-audio-up` при этом шёл дважды.
   Теперь `Type=notify` (PulseAudio шлёт READY=1) — готов за 115 мс;
   `bf7-audio-up` при запущенной службе сервера ждёт его, а не открывает
   устройство напрямую (второй поток роняет ядро);
3. PID 1 простаивал секундами: к нашей `console=ttyGS0` загрузчик добавляет
   `console=tty0 console=ttyS0,921600`, и с `loglevel=8 ignore_loglevel`
   каждое сообщение ядра шло синхронно в три консоли; строка состояния
   systemd стоила 180–240 мс (драйвер USB_ACM на каждую запись печатает
   свою отладочную строку). Теперь `quiet` и `ramoops.record_size=0x20000`
   (посмертная запись — 128 КБ полного журнала вместо 4 КБ; сама паника в
   console-ramoops попадает целиком — ядро включает подробный вывод в
   `oops_enter`/`panic`).

|                      | было      | стало   |
|----------------------|-----------|---------|
| ядро                 | 16–17 с   | 3,2 с   |
| пространство пользов.| 42–61 с   | 21,4 с  |
| старт сеанса         | 43–59 с   | 6,9 с   |
| всего                | 59–77 с   | 24,5 с  |

Заодно: `slub_debug=OFZPU page_owner=on` из командной строки загрузчика
выглядели тяжёлыми, но `CONFIG_SLUB_DEBUG` и `CONFIG_PAGE_OWNER` в ядре
нет — параметры игнорируются, цены нет.

**Моя ошибка.** Проверяя, как вызвать установщик, я запустил
`bf7-install.sh --help` на хозяйской машине. Файлы не легли (нет прав), но
`systemctl daemon-reload` вызвал окно пароля polkit у хозяина; оно истекло
без ответа. Установщик теперь отказывается работать не на MT6761 и не от
root, понимает `--help`.

Звук после всех перезагрузок — 68–69 дБ по петле, самопроверка 49/0.

## 2026-09-23 09:15 — Сон freeze работает сам; почему обе пробы висели на терминале

Вторая проба `bf7-son-proba freeze 20` (держатели экрана уже отпускаются)
снова застряла: `rtcwake` в `pm_prepare_console → vt_move_to_console →
vt_waitactive`. `skip_vt_switch` в mtkfb стоял — не помог. Две причины:

1. **`console_suspend_enabled = 0` от MediaTek.** Драйвер сна
   (`spm/common/mtk_sleep.c`, при `SLP_SUSPEND_LOG_EN`) выключает сон
   консолей, чтобы печатать и во сне. А `pm_vt_switch()` при выключенном
   сне консолей требует переключения терминала ВСЕГДА, до проверки списка
   (`/sys/module/printk/parameters/console_suspend` = N).
2. **seatd не получает запрос.** Ядро просит владельца tty1 отпустить
   терминал сигналом `relsig` = SIGUSR1. Владелец — встроенный seatd,
   дочерний процесс phoc; у него `SigBlk: 0x200` и `ShdPnd: 0x200` —
   SIGUSR1 заблокирован и висит. Маску он наследует при fork от phoc: у
   `dbus-run-session` маска пустая, у главной нити phoc — 0x200. Код seatd
   (0.7.0) отпускает терминал сразу по сигналу — просто сигнал не доходит.

Выход из застрявшей пробы без убийства rtcwake: сперва страховочный
будильник (`wakealarm` +90 с), затем `VT_RELDISP 1` на /dev/tty1 от
имени seatd. Переключение завершилось, сон вошёл (595,99 с), будильник
разбудил через PMIC (`269 mt6358-irq`, 685,9 с), rtcwake вернул 0, терминал
вернулся на tty1 в графике. Во сне ядро раз в минуту будил контроллер
топлива — система засыпала обратно.

Починка — `tmpfiles/bf7-son.conf`: `console_suspend=Y`. Переключения
терминала больше нет (в списке только mtkfb с required=false). Проба
freeze: вход 803,5 с, выход по будильнику 824,9 с, 21 с по часам, без
вмешательства. `suspend_stats`: 2 успеха, 0 провалов. После сна: экран по
кнопке (снимок и камера — часы 09:13, яркость 141), тач в рабочей прошивке,
Bluetooth, Wi-Fi, звук 67 дБ, камера — живые кадры.

Осталось: SIGUSR1 у seatd по-прежнему заблокирован — для сна это больше не
важно, но любое переключение терминала (chvt) повиснет так же. И решить,
кто и когда будет усыплять телефон сам (сейчас сон только по команде).

## 2026-09-23 09:36 — systemd и gsd-power усыпляют телефон; моя ошибка с проверкой на кабеле

**systemd.** По умолчанию `systemctl suspend` пишет «mem», а `mem_sleep`
у нас «deep» (не испытан). `sleep.conf.d/bf7-sleep.conf`:
`SuspendState=freeze`. Проверка: `systemctl suspend` со страховочным
будильником +40 с — s2idle, «Resume caused by IRQ 334, mt6397-rtc» ровно
через 40 с, «System returned from sleep state».

**gsd-power.** Автосон стоял `'nothing'` и на кабеле, и на батарее — в
пользовательской базе dconf root, я сам его когда-то выключил, пока сон не
работал. Проверка на кабеле: `sleep-inactive-ac-type=suspend`, тайм-аут
60 с. Через минуту простоя logind: «The system will suspend now!», s2idle;
разбудил не будильник, а контроллер топлива (`IRQ 332, mtk_gauge_nafg`) через
99 с. Вторая проверка показала, что после такого непрошеного пробуждения
gsd **усыпляет снова** — на батарее это ровно то, что нужно.

Кнопка питания как источник пробуждения: в дереве устройств у узла
`mt6357keys/power` есть `wakeup-source`, драйвер при сне делает
`enable_irq_wake` на её прерывание (дочернее к PMIC 269, через которое уже
будили RTC и контроллер топлива). Руками нажать не могу — проверит первое
же нажатие хозяина.

**Моя ошибка.** Во второй проверке страховочный будильник был одноразовым
(+100 с), а вернуть `ac-type=nothing` я собирался по ssh. Телефон проснулся
по будильнику, gsd усыпил его снова — и дальше будить некому: на кабеле
при 100% заряда контроллер топлива почти молчит. С 09:25 телефон спит,
связи нет. Запущен ловец: пинг раз в секунду по USB и Wi-Fi до трёх
часов, при первом появлении — `ac-type=nothing`. Проснётся от кнопки —
ловец вернёт настройку за секунду; до того после минуты простоя на кабеле
он будет засыпать. Урок записан в память (bf7-suspend): испытательную
настройку, которая усыпляет, откатывать с самого телефона таймером с
`WakeSystem=yes`.

## 2026-09-23 10:19 — Будильник во сне; кто на самом деле зажигает экран

**Будильник.** GNOME Clocks 43 тикает будильниками только в окне
(AlarmFace), из сна не будит. Новая служба `bf7-budilnik`: будильники из
настроек Clocks, один таймер systemd с `WakeSystem=yes` на ближайший звонок
(проверил отдельно: таймер будит телефон через RTC из лёгкого сна за
61 с, страховочный будильник на 150 с не понадобился). В миг звонка —
запрет гашения/сна у gnome-session, звук и вибрация через feedbackd,
уведомление «Стоп»/«Отложить», кнопка питания — «отложить». Проверено
**камерой**: наяву; во сне (уснул 09:56:21, проснулся к 09:58:00, звонок
09:58:01); кнопка питания во время звонка — отложено, экран погас и не
вернулся.

**Кнопку питания отрабатывает phoc, не phosh.** `keyboard.c`:
XF86PowerOff → `phoc_desktop_toggle_output_blank` — вывод переключается
напрямую, свойство `PowerSaveMode` у `org.gnome.Mutter.DisplayConfig`
при этом не меняется.

**Спор с gsd-power.** Первая версия зажигала экран `PowerSaveMode=0`.
Замер во время звонка (вывод раз в 0,5 с + dbus-monitor): кнопка погасила
вывод в 10:06:07.0, а в 10:06:07.155 **gsd-power** (`:1.19`) выставил
`PowerSaveMode=0` — экран загорелся обратно. gsd сам погасил экран по
простою и считал сеанс простаивающим; моё включение шло мимо него, а
нажатие он принял за выход из простоя. Правильно — «действие
пользователя»: `bf7-knopka wakeup` (uinput KEY_WAKEUP) — gsd сам выходит из
простоя и зажигает экран, и дальше кнопка питания работает как обычно
(камера: горит → кнопка → погас и остался). `simulate_user_activity` из
`org_kde_kwin_idle` не годится: wlroots сбрасывает таймер только самого
приславшего клиента.

**phosh зажигает экран при КАЖДОМ пробуждении.** Камера после
пробуждения по одному RTC, без человека: горит экран блокировки (10:10).
`screen-saver-manager.c`, `on_logind_prepare_for_sleep` при выходе из сна
шлёт `WakeUpScreen` и сбрасывает таймеры простоя — каждое из этого через
gsd зажигает экран. На батарее контроллер топлива будит телефон раз в
десятки минут (`mtk_gauge_coulomb_high` — через 16 мин, `mtk_gauge_nafg` —
через 99 с на кабеле) — экран вспыхивал бы в кармане на две минуты, а
gsd усыплял бы снова лишь через полный тайм-аут простоя. Дальше: заплатка в
phosh и своё быстрое засыпание после непрошеного пробуждения.

## 2026-09-23 10:50 — Экран больше не вспыхивает при пробуждении; обратно в сон

После заплатки phosh экран после пробуждения по RTC **всё равно
загорелся** (камера, 10:34). Второй виновник — gsd-power:
`handle_resume_actions()` безусловно делает `backlight_enable()` —
логика ноутбука. Пересобирать `gnome-settings-daemon` пакетом тяжело
(тянет mutter, wacom, тесты): собрана одна цель `gsd-power` (12 с), путь к
`libgsd.so` выправлен `patchelf`, поставлена поверх пакетной через
`dpkg-divert` (пакетная — `/usr/libexec/gsd-power.debian`).

Новая служба `bf7-dosyp`: после выхода из сна заводит у phosh
`AddIdleWatch` на 30 с (`GetIdletime` в phosh 0.24 — «Not supported», с
ним служба не усыпила бы ни разу — проверил до испытания); сработал — и
если на батарее, экран тёмный, нет запретов сна, не играет звук — снова
`systemctl suspend`. Пишет причину пробуждения (`Resume caused by IRQ`);
если разбудила кнопка, а экран через 1,5 с тёмный, — зажигает сам.

Испытание с **самооткатом на телефоне** (урок 09:30): вставка «кабель =
батарея» в /run, таймер `WakeSystem=yes` через 240 с будит телефон и
убирает её. Итог: сон 10:42:27 → пробуждение по RTC 10:43:08, **экран
тёмный** (камера) → 10:43:38 «пробуждение было непрошеным — засыпаю снова»
→ 10:46:22 таймер отката разбудил и убрал вставку. Будильник во сне
зажигает экран сам (камера, 10:49). Кнопку питания руками не нажать —
на неё страховка в bf7-dosyp.

Что закреплено от обновлений: phosh (`apt-mark hold`), gsd-power
(`dpkg-divert`); самопроверка ругается, если что-то из этого стало
пакетным.

## 2026-09-23 11:07 — Тише журнал, отставка bf7-zram, установщик сверяет, git догоняет

**Шум журнала.** Свежий замер (с моими испытаниями) — ~430 строк в минуту.
Главные источники и что сделано:
- `fuelgauged` (вендорский): stdout — отладка обмена с ядром, ~70/мин →
  `StandardOutput=null` в bf7-fuelgauge.service (stderr остаётся);
- сводки простоя MediaTek «Power/swap DP: No enter…», причины блокировки,
  «mcdi cpu: …» — раз в 5 с из пути простоя, ~18/мин → раз в 10 минут
  (правлена собираемая копия `spm/common`, не `spm_v4`), параметры модуля
  `mtk_idle_profile.cnt_dump_ms/block_log_ms`,
  `mtk_mcdi_main.heart_beat_log_ms` возвращают частоту на лету;
- Trustonic «daemon not connected after …» — раз в 10 с на поток → на 10,
  20, 40, 80… секунде;
- Wi-Fi: «QM WARN … Dropped Packet» на уровне 0x03 → `dbgLevel` 0x01;
  «Drop Gratuitous ARP»/«NAdv Flag»/«Drop Unsolicited NA» печатались
  `kalPrint` безусловно (отладка Hotspot 2.0 в `hs20.c`) → в слое правок
  связности переведены на `DBGLOG(RX, TRACE)`, само отбрасывание не
  тронуто. Сборка упала раз на моём же комментарии: `/proc/…/*_state` —
  это «/*» внутри блочного комментария, ядро собирается с `-Werror`.

**bf7-zram отставлена.** После прошивки система `degraded`: bf7-zram
получил «swapon: Device or resource busy». Своп при этом работал — его
поднимает сам systemd (генератор, `swap-priority = 100`). bf7-zram
заводили 16.09 как обход «systemd считает себя в контейнере» (лишний
`/.dockerenv`), та причина давно устранена; ускорившаяся загрузка вскрыла
гонку. После перезагрузки: своп на месте, система `running`, загрузка
22,7 с (ядро 1,6 с).

**Установщик.** Не клал 8 служб из корня `hybris/bf7-gpu`, хотя включал
их; `postavit()` перезаписывала tmpfiles и dconf молча. Теперь всё через
`put()` со сверкой, и есть `--proverka` — только показать расхождения.
Первый прогон: расходился один файл (дописанный комментарий).

**git догоняет.** Слой правок связности (`bf7-connectivity-overlay`, в нём
`plat_priv.c` — без него нет Wi-Fi) и сценарии сборки ядра
(`build-bf7-x657b.sh` и др.) ни разу не были в репозитории — добавлены.

Я опять заменил файл на телефоне (bf7-fuelgauge.service), не сверив
сначала; сверил задним числом — совпадал. Теперь для этого есть
`--proverka`.

## 2026-09-23 11:19 — Шум, вторая пачка: rtnl-слежка MediaTek и ошибка формата в PBM

Замер в покое после первой пачки: 285 строк за 3 мин, из них половина —
мои же ssh (ожидание по опросу аптайма). Остаток — служебная печать ядра:

- **rtnl-слежка MediaTek** (`net/core/rtnetlink.c`): на КАЖДЫЙ
  `rtnl_lock()` снимается стек и заводится таймер на 2 с; таймер
  срабатывал всегда, даже когда замок давно отпущен, и печатал «There is no
  process hold rtnl lock» — ~10 строк в минуту и лишнее пробуждение на
  каждую сетевую операцию. Таймер теперь снимается при отпускании (до
  `mutex_unlock` — иначе можно снять таймер следующего владельца);
  предупреждения о долгом удержании остались.
- «[wdtk] kick watchdog» на каждый пинок сторожа, «[ma_to_mw]» и
  «[DLPT_final]» — `pr_debug`. В auxadc внешний `__ratelimit` сам печатал
  «N callbacks suppressed» при выключенной `dev_dbg` — убран.
- **Ошибка в `mtk_pbm.c`**: `pr_notice_ratelimited("[%s] wrong volt=%d …",
  bat_vol)` — два спецификатора, один аргумент. Если `get_battery_volt()`
  однажды вернёт 0, printk прочтёт число как указатель на строку — паника.
  Добавлен `__func__`.

Прошито, самопроверка 55/0.

**Замер для чистоты теперь так:** ждать на хозяйской машине по часам и
подключаться к телефону один раз — опрос по ssh сам пишет в журнал по
восемь строк на подключение (sshd, pam, logind, session scope).

## 2026-09-23 11:33 — Батарея врала о проектной ёмкости; таймер AED раз в 20 с

Чистый замер после второй пачки: **170 строк за 5 минут (~34 в минуту)**
без моих подключений; утром было ~1000. Разбор хвоста дал две настоящие
находки.

**Проектная ёмкость батареи в 10 раз меньше.** upower дважды в минуту
писал «energy_full (…) is greater than energy_full_design (…)». В
`/sys/class/power_supply/battery`: `charge_full` = 2946000 мкА·ч,
`charge_full_design` = 294000. В `mtk_battery.c` `CHARGE_FULL` = `q_max`
× 1000, а `CHARGE_FULL_DESIGN` = `q_max` / 10 × 1000 — код рассчитан на
`q_max` в десятых долях мА·ч, а у нас он уже в мА·ч. Любой показ «здоровья
батареи» (upower, настройки GNOME) показывал бы 1000%. После правки:
12,8 Вт·ч = 12,8, 100%, предупреждений 0.

**Таймер AED** (`aed-main.c`): hrtimer будил процессор раз в 20 с только ради
строки «UTC-время … android time …» — для сверки времени в вендорских
журналах. Выключен (`aee_aed.utc_sync_ms`, 0 по умолчанию).

Ещё: отсчёт hang_detect на каждом витке — `pr_debug`; charger_init, который
на этой плате удаться не может (нет `primary_chg`, заряд ведёт вендорский
демон — решено 22.09), повторяется раз в полчаса, а не раз в минуту.

Брандмауэр: широковещательное и групповое теперь отбрасываются молча до
записи в журнал — ограничитель «2 в минуту» раньше целиком уходил на
пакеты умных розеток соседей (UDP 6667), и настоящие попытки достучаться в
журнал не попадали.

## 2026-09-23 11:45 — Заводская политика температуры; «экран погашен» по разъёму DRM

**Опрос термозон 26/с → 8,5/с, и защита, которой не было.** Точка
`thermal:thermal_temperature` за 20 с: `mtktscpu` опрашивалась 22,75 раза в
секунду при 36 °C. `TZCPU_INITCFG_INTERVAL = 40` мс — начальная настройка,
которую на Android сразу переписывает `thermal_manager` из `/vendor`
(однократно, `oneshot` в `init.thermal_manager.rc`). Конфигурация
`/vendor/etc/.tp/thermal.conf` зашифрована — что он пишет, снял **в
песочнице**: `unshare -m`, копии всех 73 файлов `/proc/driver/thermal`
смонтированы поверх настоящих, `/data/vendor/.tp` тоже подменён (без
каталога он выходит с 255), `strace -e write`. Политика: процессор — пороги
54/68 °C (адаптивное охлаждение), 117 °C перезагрузка, опрос **200 мс**;
батарея — выключение при **60 °C**; плата (`tzbts`) — ограничение тока
заряда при 45–47 °C, охлаждение камеры при 53, выключение 90–100 °C; до
этого у датчика платы не было **ни одного** порога. Применил:
`bf7-thermal.service` при загрузке. После — опрос зон 8,5/с (процессор
4,9/с), самопроверка следит.

Ещё 13 зон `tzimgs` (температура матриц камер) показывают −127 °C —
датчиков нет, драйвер реже опрашивает их сам (×10), вместе ~1,3/с;
оставил.

**«Экран погашен» — по разъёму.** При спящем тракте экрана
`lcd-backlight/brightness` показывал 141: класс backlight пропускает первое
гашение после загрузки (`fb_bl_on` ещё не выставлен), физически подсветка
выключена (ШИМ дисплея гаснет вместе с трактом). bf7-dosyp судил по этому
файлу и после гашения кнопкой **не усыпил бы телефон никогда**. Теперь всё
смотрит `/sys/class/drm/card1-DSI-1/enabled`.

Загрузка теперь 22,3 с, и система «running» раньше, чем поднимается мост
Bluetooth (~33 с) — самопроверка сразу после загрузки ложно ругалась на
hci0; теперь в первые 90 с ждёт его до 30 с.

## 2026-09-23 11:55 — PSI, усилитель, VNC: ещё минус пробуждения; установщик полон

Замер пробуждений при погашенном экране после политики температуры и
`psi=0` (всё — за секунду): `tick_sched_timer` 90,8 утром → 45,6;
`arch_timer` 55,9 → 40,2; `timer@10017000` 51,7 → 21,0;
`delayed_work_timer_fn` 39,5 → 10,4; опрос термозон 26,4 → 8,5.

Разбор `hrtimer_wakeup`/`process_timeout` по пробуждаемым задачам (пара
«срабатывание таймера → sched_wakeup на том же процессоре»): rcu_preempt
7,1/с (норма), hif_thread 2,3 и pvr_cacheop 1,9 (вендорские драйверы),
**gpioset 1,0 и bf7vnc 2,0** — это наши:

- `bf7-extamp` держит GPIO 151 процессом `gpioset --mode=signal`, а тот в
  libgpiod 1.6 опрашивает сигналы раз в секунду. `--mode=time --sec=2000000`
  — 0 пробуждений, линия держится, динамик 67 дБ. Заодно исправил абзац в
  bf7-audio-up, утверждавший «подпорка снаружи не нужна» (опровергнуто
  22.09).
- `bf7vnc.py`: `socketserver.serve_forever()` проверяет флаг остановки каждые
  0,5 с. `poll_interval=None` — 0 пробуждений, сервер отвечает.

Установщик теперь кладёт и сценарии служб (firewall, vnc, minilogd,
android-mounts, bt-prepare, hal-start, wifi-init) — все совпадали с
телефоном; `--proverka` по полной раскладке — 0 расхождений.

Опять ловушка `pgrep -f`: замер VNC поймал мою ssh-оболочку. Процессы
брать по `systemctl show -p MainPID`.

## 2026-09-23 12:04 — Ошибки ядра, а не шум: пропущенный break, четыре дампа при загрузке

Сводка `journalctl -k -p 0..4` за загрузку, сгруппированная по тексту.

**`mt6357-gauge`: пропущенный `break`.** «driver failed to report
`energy_empty`» — 20 раз, на каждое событие батареи. В
`psy_gauge_get_property` у `case POWER_SUPPLY_PROP_ENERGY_EMPTY` нет
`break`, выполнение проваливалось в `default: return -EINVAL`: значение
(признак выключения по DLPT) не доходило до читателя никогда. В копии
`mt6359-gauge.c` `break` есть. Исправлено, прошито: читается 0, ошибок 0.

**Четыре дампа стека при загрузке — вендорская небрежность, безвредно,
оставил:**
1. `WARN_ON(*type == IRQ_TYPE_NONE)` в `gic_irq_domain_translate` — у узла
   рядом с CMDQ в дереве устройств не указан тип срабатывания прерывания;
   прерывание назначается, CMDQ (весь экран на нём) работает;
2. `cmdq_mbox_create`: запрос канала 16 при 16 каналах (0–15);
3. `musb_init_debugfs` повторно создаёт свой каталог в /proc;
4. `pinctrl_mtk_debug_v2_init` повторно создаёт `mt_gpio` в sysfs.

Ещё безвредное: «proc: unrecognized mount option hidepid=invisible» —
systemd просит опцию, которой нет в 4.19, и обходится без неё.

## 2026-09-23 12:10 — Обои: вместо узора голый синий цвет

phosh писал «Failed to load background: Unrecognized image file format».
Пакетные обои GNOME — WebP 4096×4096, а загрузчика WebP для gdk-pixbuf на
телефоне нет. Загрузчик ставить не стал: раскодировать ≈64 МБ на каждом
старте ради экрана 720×1612 незачем. Взял те же обои Adwaita, вырезал по
центру под 720×1612 и сохранил в JPEG (32 и 44 КБ, CC BY-SA 3.0, см.
`hybris/bf7-gpu/fon/README.md`). phosh 0.24 не знает `picture-uri-dark`, а
тема тёмная, поэтому в `picture-uri` лежит тёмный вариант. Заводское
WebP-значение в пользовательской базе dconf снято. Проверено камерой: узор
на месте, ошибок загрузки нет.

## 2026-09-23 12:40 — Светодиод уведомлений: цепочка работает, сам светодиод включён наоборот

**Задача:** чтобы пропущенные уведомления мигали, как на любом телефоне.

**Что было нужно программно — сделано и проверено до регистров:**
- в 4.19 нет триггера `pattern`, а feedbackd мигает только им. Перенёс
  `ledtrig-pattern.c` из 5.16 (программный режим, без аппаратных паттернов),
  `CONFIG_LEDS_TRIGGER_PATTERN=y`;
- правило udev метит `blue` для feedbackd (штатные правила ловят только
  `*:status`/`*:indicator`) и через `fbd-ledctrl` ставит триггер;
- `bf7-proshit` теперь кладёт `modules.builtin`: без него modprobe не знал о
  встроенном триггере и писал «Failed to find module 'ledtrig-pattern'».

**Ловушка в проверке.** Первый прогон `fbcli -E message-missed-notification`
показал яркость 0 всё время. Отладочный вывод feedbackd: шаблон записан, и
через 3 мс событие закрыто. Закрыл его сам `fbcli`: он ждёт Enter на stdin,
а у фонового запуска stdin — `/dev/null`, то есть сразу конец файла, и
fbcli зовёт EndFeedback. С `sleep 30 | fbcli …` яркость ходит 204→255→26→76,
шаблон `0 1000 255 1000`.

**А камера показала обратное.** Кадры «мигает» и «не мигает» совпали
попиксельно. Проверил вручную по очереди 0 и 255, по четыре раза: у верхнего
края телефона, рядом с отверстием фронтальной камеры (кадр ≈ x 627, y 273),
горит беловато-зелёный огонь **при яркости 0** и гаснет **при 255**. Без
исключений, и в режиме ШИМ, и в режиме постоянного тока (CC) на шагах 3 и 7
(регистры `1e98=2000`, `1e8a=7fe0`/`ffe0`, `1e96=0822`/`0802`). Синего
огонька нет ни на одном кадре: искал перевес синего в окне светодиода, там
только тёмно-синий фон корпуса.

**Что это НЕ такое (проверено):**
- не индикатор зарядки PMIC (CHRIND). Его такт был выключен
  (`1e10=0x2d`, бит 5), поэтому записи в `CHRIND_EN_CTRL` (`1f12`) не
  действовали. С включённым тактом программное «выкл» сбрасывает
  `DA_INDICATOR_EN` (`1f14` e000→c000), а огонь горит по-прежнему;
- не вывод STAT зарядника. У AW32257 (i2c-3:0x6a) `reg0=0xe0`: по раскладке
  bq24157 это STAT=10, «заряд окончен», вывод отпущен, а огонь горит.

Вывод: что-то другое питает светодиод, а включённый ISINK1 его «перетягивает»
или шунтирует. Все подходящие схемы включения расходятся лишь в одном:
**горит ли он без кабеля**. Проверить это я не могу: кабель не выдернуть,
а переводить зарядник в HZ ради опыта не стал. Драйвер вслепую не
переворачивал. В ШИМ видимая яркость обратна скважности, поэтому на кабеле
пропущенное уведомление видно как «обратное дыхание» ровного огня: ровный
свет плавно гаснет и загорается.

Все тронутые регистры PMIC возвращены (`1e10=002d`, `1f08/1f0a/1f12=0`,
`1e96/1e98=0`, `1e8a=1fe0`), триггер снова `pattern`.

**Нужно одно наблюдение хозяина:** без кабеля горит ли огонёк справа от
фронтальной камеры. Если горит постоянно — это заметный расход, и драйвер
надо перевернуть (яркость 0 → канал включён). Если не горит — светодиод
годится только как индикатор зарядки, и правило feedbackd можно оставить
как есть.

## 2026-09-23 12:58 — Переключение терминалов больше не виснет; демон тепловых алгоритмов

**seatd.** Встроенный seatd запускается из phoc через `fork()` без `exec` и
наследует маску сигналов phoc, а phoc блокирует SIGUSR1. Видно по потомку
phoc: `SigBlk 0000000000000200` при `SigCgt …4a02`, то есть обработчик на
SIGUSR1 есть, но сигнал заблокирован. seatd просит ядро слать SIGUSR1 при
отпуске терминала, сигнал не доходит, и любое переключение висит в
`VT_WAITACTIVE`. Сон мы раньше обошли через `console_suspend=Y`, но корень
остался. Правка в libseat: в потомке маска сбрасывается
(`hybris/bf7-patches/seatd-0.7-builtin-unblock-signals.patch`), пакеты
`libseat1`/`libseat-dev` 0.7.0-6+bf7.1 собраны на телефоне и закреплены.
После перезагрузки `SigBlk 0`. Проверено камерой: терминал 2 (приглашение
getty) и обратно, phosh на месте, вывод DSI включён. `chvt` на телефоне нет
(нет пакета kbd), переключал ioctl'ами VT_ACTIVATE/VT_WAITACTIVE.

**Шум, который внёс я сам.** Замер после включения заводской тепловой
политики: ~24 строки в минуту «wakeup_ta_algo error, g_tad_pid=0» и
«out of memory». Политика включает CATM+, а считает его демон
`thermalloadalgod` из /vendor (32-битный, грузит `libthermalalgo.so`):
ядро на каждом замере шлёт ему данные по netlink (протокол 27). Песочница
strace на 25 с: файлов не открывает, кроме библиотек, 17 сообщений от ядра
и ответы, частоты процессора не тронуты. Теперь это служба
`bf7-thermalloadalgod` (2,3 МБ, процессор почти не ест).

В ядре (`mtk_ta.c`): флаги `g_ta_status` не сбрасываются до перезагрузки,
и одна давняя ошибка печаталась каждый третий вызов вечно. Теперь статус
печатается только при изменении, об отсутствии демона сообщается один раз,
а умерший демон (`netlink_unicast` → `-ECONNREFUSED`) забывается. В
`mtk_ts_bts.c`/`mtk_ts_dctm.c` «out of memory» печатается только при
настоящей `-ENOMEM`: раньше так называлось отсутствие демона. После
прошивки за всю загрузку две строки: до старта демона и одна строка статуса.

**Попутно.** `bf7-wifi-init` опускает уровень WMT и STP до предупреждений
(`/proc/driver/wmt_dbg`, ключ 0xDB9DB9, команды 9 и a). `bf7-proshit` кладёт
`modules.builtin` и `modules.order`. Уровень PSM снаружи не меняется, его
оставил: 11 строк за 5 минут, и только под нагрузкой Wi-Fi.

## 2026-09-23 13:07 — Интерфейс на русском; дата на экране блокировки

На экране блокировки стояло «Среда, сентября 23» и «Slide up to unlock».
Причин две, и обе тихие:
- корень собран из докерного образа Debian, а там
  `/etc/dpkg/dpkg.cfg.d/docker` при установке выбрасывает все переводы
  (`path-exclude /usr/share/locale/*`); `/usr/share/locale` весил 4 КБ;
- `bf7-phosh.service` с первого дня ставил `LANG=C.UTF-8`. Причины
  записано не было. Системная локаль при этом `ru_RU.UTF-8`, но systemd
  `/etc/default/locale` сам не читает.

Английский шаблон даты («%A, %B %-d») накладывался на русские названия
дней и месяцев, отсюда и «сентября 23».

Сделано: `dpkg/zz-bf7-ru-locale` (`path-include /usr/share/locale/ru/*`,
читается после «docker»), переустановлены 46 пакетов, видимых в интерфейсе
(GTK 3/4, GLib, libadwaita, libhandy, squeekboard, приложения GNOME,
порталы, GStreamer, WebKit и др.). phosh поставлен из своего
`0.24.0-1+bf7.1` через `dpkg -i`, а не через apt, иначе слетела бы заплатка.
Консольные утилиты, systemd, NetworkManager и PulseAudio не трогал: их
вывод разбирают сценарии, а переустановка дёрнула бы сеть и звук. В службе
сеанса `LANG=ru_RU.UTF-8`.

Проверено камерой: «Среда, 23 сентября», «Проведите вверх для
разблокировки», сетка приложений по-русски. Самопроверка 57/0 (phosh и
gsd-power наши). По-английски остались названия-бренды (Foliate, Megapixels)
и «Mobile Settings»: у phosh-mobile-settings русского перевода нет вовсе.

## 2026-09-23 13:19 — Сетка приложений: дубль браузера, хлам, нерабочие; Firefox по-мобильному

Прошёл «Показать все приложения» глазами (grim, затем камера).

**Два одинаковых глобуса.** В избранном стоял `org.gnome.Epiphany.desktop`
(наша подмена, спрятанная NoDisplay и ведущая на bf7-web), а в сетке —
видимый `bf7-web.desktop`. Сетка не повторяет избранное только при
совпадении идентификатора, поэтому браузер был дважды. В избранном теперь
`bf7-web.desktop` (и умолчанием в `dconf/00-bf7`).

**Хлам и нерабочее.** Демонстрации GTK (5 шт.), ImageMagick, XTerm/UXTerm,
xvkbd, Onboard и его настройки, служебные окна Thunar, терминал Xfce (есть
Консоль), поиск приложений Xfce, настройки печати. Нерабочие здесь: Cheese и
guvcview (см. «Камерные программы»), Megapixels (нет описания сенсора; при
этом он стоял в основной сетке как «мобильный»), Chromium — под root без
`--no-sandbox` не стартует вовсе («Running as root without --no-sandbox is
not supported»). Снимать песочницу у браузера под root не стал: это прямая
дыра, а работающих браузеров два. Всё это спрятано **только из Phosh**:
`bf7-phosh-skryt` кладёт в `/usr/local/share/applications` копию пакетной
записи с `NotShowIn=Phosh;`, а в сеансе Xfce всё на месте.

**Firefox на телефоне.** Окно было настольным: страница не влезала по
ширине. Поставил `firefox-esr-mobile-config` 3.2.0 из Debian, и Firefox
140 стал показывать при каждом старте «Configuration Error: Failed to read
the configuration file». Его автоконфиг делал `Cu.import(...Services.jsm)`,
а модулей .jsm с Firefox 136 нет. Исправил: `Services` глобальный,
`FileUtils` из `FileUtils.sys.mjs`. Строка агента была «Android 10 …
Firefox/91» (2021), стала Firefox/140. Пакетный файл отведён dpkg-divert,
наш лежит в `hybris/bf7-gpu/firefox/`. Проверено камерой: новая вкладка по
ширине экрана, экранная клавиатура всплывает на адресной строке.

## 2026-09-23 13:32 — «Камера»: видоискатель, спуск, ночь; снимки открываются по-телефонному

Камера давно отдаёт обычный `/dev/video0`, но приложения для неё не было.
Megapixels ждёт описания сенсора, Cheese и guvcview здесь падают, а
«Фотоаппарат» — окно X11 с кнопками серий без видоискателя, шире экрана и
поверх панелей Phosh. Написал `hybris/bf7-camera/bf7-kamera.py` (GTK 3 +
GStreamer):
- видоискатель: `v4l2src` 1024×768 → 512×384, 15 кадров/с → `videoflip`
  → `gtksink`; окно не в фокусе — поток стоит;
- спуск через `bf7-camd` (команда «фото» в `/run/bf7-camd`): он разбирает
  Байера полностью (2104×1560) и складывает кадры, «Фото» 4, «Ночь» 16, не
  прерывая видео, так что видоискатель живёт и во время съёмки;
- подсветка фонарём, миниатюра последнего снимка (открывает eog), поворот
  кнопкой с запоминанием, запрет гашения экрана, пока окно открыто, звук и
  вибро спуска через feedbackd (`camera-shutter`).

**Ловушка:** у `videoflip` в GStreamer 1.22 свойство `method` (устаревшее)
молча не поворачивает: выход 1024×768 при `method=clockwise`. Работает только
`video-direction` (90r → 768×1024). Видоискатель из-за этого сперва стоял
неповёрнутым, а снимок (поворот через GdkPixbuf) — повёрнутым. Поймал
сравнением сырого кадра видео и сырого фото с подсветкой: сырые совпадают,
расходился поворот.

Ориентацию сенсора проверить нечем: объектив смотрит в стол. По умолчанию
четверть оборота по часовой, как у задних камер Android; кнопка ⟳ это
исправит.

Цена открытой камеры: окно ~60% одного ядра, `bf7-camd` ~140% (разбор
Байера, пока у `/dev/video0` есть читатель).

Попутно: `/etc/xdg/mimeapps.list`. Своего списка не было, и снимки открывал
ImageMagick `display`, теперь eog (PDF — Evince, ссылки — bf7-web). У eog
по умолчанию открыта боковая панель «Свойства», и снимок сжимался до 3%,
выключена в `dconf/00-bf7`. Старый «Фотоаппарат» скрыт из сетки (нужен
ради тёмного кадра). Проверено: grim и камера-наблюдатель, «Снято», файлы
1560×2104, в основной сетке «Камера» вместо Megapixels.

## 2026-09-23 14:08 — Сотовая связь: модем поднят без Android, SIM и сеть в ModemManager

Хозяин вставил SIM. Сперва — резервные копии разделов NVRAM модема (IMEI,
калибровка): `nvram nvdata nvcfg protect1 protect2 persist` сняты на ПК в
`firmware-backup/radio-nv-20260923`, суммы сверены с телефоном. Эти копии в
git не кладутся.

**Как модем запускается на Android:** LK уже загрузил образ («LK load MD
success»), а стартует его `ccci_mdinit 0`. Внутри у него fsd, который отдаёт
модему его файлы NVRAM. Ядро пускает ioctl запуска только от процесса с
именем `ccci_mdinit`.

**Песочница** (`hybris/bf7-modem/pesochnica.sh`): копии разделов на
loop-устройствах в частном пространстве монтирования (`unshare -m`) на
андроидных путях `/mnt/vendor/*`, свойства через libbf7prop, всё под
strace. Пять ловушек, по порядку:
1. `LD_PRELOAD` с бионик-заглушкой нельзя отдавать glibc-программам
   (timeout, strace, mkdir в ExecStartPre): только самому демону
   (`strace -E`, `env`).
2. `ccci_mdinit` в цикле ждёт `vendor.service.nvram_restore`. Значение нашёл
   дизассемблером: сравнение с «Ready» (4 байта + «y») или «Pre_Ready». На
   Android его выставляет nvram_daemon в своём процессе, а заглушка свойств
   у каждого процесса своя, поэтому значение лежит в `bf7.prop`.
3. fsd читает NVRAM через HIDL-службу `vendor.mediatek.hardware.nvram@1.1`,
   запускаю её так же, как gralloc.
4. На первом запросе модема fsd берёт wakelock через
   `android.system.suspend@1.0::ISystemSuspend`, которой нет, и ждёт вечно.
   Прослойка `libbf7wakelock.so` (NDK) — прежний путь через
   `/sys/power/wake_lock`.
5. Без `ccci_rpcd 0` модем получает свои файлы, но второе рукопожатие
   срывается (`MD_BOOT_HS2_FAIL`). С ним `md1:4` (READY).

**Что модем пишет** (сравнение копий с настоящими разделами по файлам):
`nvdata/md/NVRAM/NVD_DATA/*` (данные о сетях и частотах), `INFO_FILE`,
`protect*/md/NA77_006`. IMEI и калибровку он не трогает. Это обычная работа,
как на заводском Android, поэтому службы работают с настоящими разделами.

**AT-канал:** `/dev/ttyC0` (CCCI_UART2, очередь AT). Модем «MTK2
MOLY.LR12A.R3.MP.V241.4.P33». Стартует в `CFUN=0`, и в этом режиме SIM не
опрашивается (`+CME ERROR: 14`). Нужен `CFUN=4`, тогда `+CPIN: READY`, PIN
не нужен. IMEI корректный (15 цифр, контрольная сумма Луна сходится).
Идентификаторы SIM в журнал не пишу.

**ModemManager:** `ttyC0` — не tty (ENOTTY), а MM работает только с
терминалами, видимыми в sysfs. Включил в ядре `CONFIG_LEGACY_PTYS` (4 штуки),
мост `bf7-at-most` (C) гоняет байты `/dev/ttyC0` ↔ `/dev/ptyp0`, а udev
вешает на `/dev/ttyp0` `ID_MM_PHYSDEV_UID=bf7-modem` и
`ID_MM_DEVICE_PROCESS=1` (так MM собирает встроенные модемы Qualcomm).
`bf7-modem-sim` до MM переводит модем в `CFUN=4`. Иначе MM застаёт SIM
«занятой» и объявляет sim-missing. Итог: плагин generic, «registered», сигнал
100%, сеть 250xx (роуминг), EDGE, пакетная связь подключена. В строке phosh
значок сети с «2.75G». phosh при старте ищет MM через D-Bus, и пока служба
была выключена, повторять не стал; теперь MM включён.

Службы: `bf7-modem-mounts` (e2fsck -p, опции из fstab), `bf7-nvram-hal`,
`bf7-ccci-rpcd`, `bf7-modem`, `bf7-modem-at`, `bf7-modem-sim`, drop-in к
ModemManager. Всё в установщике.

Открыто: при загрузке `/sys/kernel/ccci/boot` показывал `md1:0`, но при
остановке ядро писало «md_state change from 4 to 7». Проверить на следующей
загрузке. LTE: сперва был отказ (`CREG 0,3`), потом роуминг в 2G. Данные
пока не пробовал. Звонки и SMS без слова хозяина не отправляю.

## 2026-09-23 14:34 — Голос в звонке: речевой тракт модема через заводской HAL

Разговор соединяет модем, а звук идёт мимо процессора: модем говорит с
кодеком сам через PCM `Voice_MD1`. Включает этот путь на Android звуковой
HAL (`audio.primary.mt6761.so`): в `AUDIO_MODE_IN_CALL` его SpeechDriver
открывает `Voice_MD1` и шлёт модему «речь вкл» по `/dev/ccci_aud`. Со стороны
ядра всё есть (PCM, регуляторы `Speech_*`).

`bf7-rech` (C, NDK) грузит HAL напрямую (dlopen, символ HMI) и делает то же,
что AudioFlinger. Три находки:
1. **Порядок:** сначала `set_mode(IN_CALL)`, потом маршрут. Разговор
   открывается маршрутом, пришедшим уже в этом режиме (`routingOutputDevice(),
   phonecall open`). Маршрут до смены режима только переключает динамик.
2. **Висло минутами:** HAL при маршрутизации опрашивает Bluetooth-чип через
   `/vendor/lib64/libbluetooth_mtk_pure.so`, а та до 100 раз ждёт
   `vendor.connsys.driver.ready` и ищет `persist.vendor.connsys.chipid` в своей
   таблице (0x6761 там есть). Оба свойства — в `bf7.prop`, вызов теперь
   занимает доли секунды.
3. **HAL выключает усилитель ЦАП**, и после разговора PulseAudio остаётся без
   звука. После каждого разговора зову `bf7-audio-up`.

Проверено без звонка: модем подтверждает всё — «речь вкл» (`ack 0xaf20`),
смену параметров при переключении трубка ↔ громкая (`0xaf2b`), «речь выкл»
(`0xaf21`). `Voice_MD1` открыт в обе стороны, 32 кГц.

**Кто включает:** «Звонки» просят звук через `org.mobian_project.CallAudio`
(SelectMode, EnableSpeaker, MuteMic). Пакетный callaudiod переключает
профили PulseAudio и у нас на всё отвечал `false`. На том же имени теперь
`bf7-zvonki` (активация D-Bus из `/usr/local/share/dbus-1/services`): держит
`bf7-rech` на время разговора, передаёт громкую связь и микрофон, держит
запрет сна (logind и gnome-session) и после разговора возвращает звук.
Изображённый через gdbus разговор прошёл целиком.

**Настоящий звонок не проверен** — позвонить сам без слова хозяина не могу.

## 2026-09-23 15:27 — Модем будит спящий телефон; Wi-Fi не будит; отладочные отчёты модема

**Проблема:** прерывание модема `CCIF_AP_DATA` (irq 37) не будило s2idle.
Входящий звонок и SMS не доходили бы до спящего телефона. Две причины:
- `ccci_hif_ccif.c` зовёт `irq_set_irq_wake` только при `MD_GENERATION >=
  6297`. На Android с MT6761 сон deep, и модем будит его маской SPM, а у
  нас s2idle, где будят только помеченные прерывания. Вызов теперь
  безусловный.
- У `mtk_sysirq_chip` не было `IRQCHIP_SKIP_SET_WAKE`, и `irq_set_irq_wake`
  на любой линии MT_SYSIRQ отвечал ENXIO (-6). Флаг добавлен.

**Последствие флага:** он глобальный. Будить стали все, кто просил и раньше
получал отказ: BTIF (80), журнал прошивки связи `CONN_LOG_IRQ` (82) и
`wlan0` (83). Первая проба сна оборвалась через 0,5 с: «Resume caused by IRQ
83, wlan0». Фильтров WoWLAN в прошивке никто не ставит, поэтому будил любой
пакет. Wi-Fi и журнал сняты в модулях (оверлей: `hif/axi/axi.c`,
`debug_utility/connsys_debug_utility.c`). Регрессии нет: раньше они не будили
вовсе. Будят сейчас 166 (type-c), 269 (PMIC), 37 и 46 (CCIF), 80 (BTIF).

**Модем болтлив:** сам по себе, без ModemManager, на свежезапущенном модеме
шлёт `+EDMFAPP: 21001,…` (отчёты о соте, «DMF») каждые 3–30 с; при
регистрации 69 штук за 3 минуты. Что будит именно AT-канал, установлено
разницей `/sys/kernel/debug/wakeup_sources` до и после сна: `ttyC0` получил
данные за 0,08 с до «Resume caused by». Выключить не удалось:
`AT+EDMFAPP` в любой форме (`=?`, `=0`, `=0,2100`, `=2,2100`, `=3,2101` как у
заводского RIL) даёт `CME ERROR 50`, `AT+ESCREENSTATE=0` не помогает. Больше
вслепую не гадаю.

**Решение как на Android (автосон):** `bf7-dosyp` после пробуждения от модема
даёт ModemManager 2,5 с показать звонок (`CallAdded`) или SMS
(`Messaging.Added`). Если их нет, сразу обратно в сон, без 30-секундного
простоя. Звонящий модем повторяет RING каждые несколько секунд, SMS лежит в
памяти модема, так что короткое окно безопасно. Замер по отметкам SPM (UTC,
идут и во сне), 6 минут на батарейном пути: ожидание 4 с — спал 51%,
2,5 с — 83% (21 цикл). Звонки: `bf7-dosyp` не усыпляет при незавершённом
звонке и зажигает экран на входящем.

Побочно подтверждено: после прошивки зарядник больше не пишет строк про
свойства (`psy_*_get_property`: 0).

## 2026-09-23 15:48 — Фонарик в шторке, «Портрет», поиск в сетке, контакты

- **Фонарик в шторке.** Плитка была, но нажатие давало «Failed to set torch
  brigthness: … Unknown object '/org/freedesktop/login1/session/auto'»:
  phosh включает фонарь через сеанс logind, а у нашей оболочки сеанса нет.
  Заплатка `phosh-0.24-torch-sysfs.patch`: если logind отказал, писать
  яркость в sysfs (оболочка работает от root). Пакет 0.24.0-1+bf7.2 собран
  на телефоне за 12 минут. Проверено: нажатие даёт `white:torch` = 5,
  плитка «100%», камера видит свет, второе нажатие гасит.
  **`dpkg -i` сбрасывает hold**, закрепил заново.
- **Подписи на плитках** «Пор… тная»: «Portrait»/«Landscape» переводились как
  «Портретная»/«Ландшафтная». Теперь «Портрет»/«Альбом» (bf7-perevody). Так же
  переведено «Start Chatting» у «Чатов», в переводе chatty 0.7.0 его не было.
  Ошибку в самом bf7-perevody поймал на проверке: таблица шла в python через
  конвейер, «python3 -» читает скрипт из heredoc, и конвейер терялся.
- **Поиск в сетке оболочки печатает** (давняя поломка ушла сама).
- **«Контакты»** (gnome-contacts) с адресной книгой evolution-data-server:
  «Вызовы» и «Чаты» берут контакты оттуда, пропали ошибки про
  `org.freedesktop.secrets` (приехал gnome-keyring).
- **Моя ошибка:** проверял поиск, не посмотрев, заблокирован ли телефон, и
  набранное «foli» с Enter ушло в поле кода разблокировки. Код не подошёл,
  вреда нет. Теперь перед вводом текста проверяю
  `org.gnome.ScreenSaver.GetActive`.

## 2026-09-23 16:09 — Зависшая загрузка и корень без журнала ext4

**Что было.** После `systemctl reboot` в 15:49 телефон не поднялся: ping
был, но SSH не открылся. Telnet на 2324 (`bf7-recovery-telnet`) принимал
соединение и сразу рвал, экран пустой. Хозяин видел на консоли «access
denied» и перезагрузил кнопкой. Журнала той загрузки нет ни на диске, ни в
pstore (жёсткий сброс его не сохраняет). Моя цепочка Upholds/PartOf у служб
модема оправдана: циклов порядка нет, следующая загрузка поднялась целиком.

**Что нашлось в следующей загрузке:** `e2fsck -p` в ramdisk ответил **4**
(«нужна рука»), `e2fsck -fy` чинил (код 1), а корень смонтирован
`rw,noatime,norecovery`. Причина в `init` ramdisk: `MOUNT_OPTS=ro,noload`,
осторожность первых дней. `noload` переживает перемонтирование systemd на
запись, так что **корень всё это время работал без журнала ext4**. Любое
нечистое выключение (паника, сброс кнопкой, неудачный размонтаж при
перезагрузке) оставляло рассогласованную ФС. Правдоподобная причина и
зависшей загрузки, и её «access denied»: разрушенные метаданные читаются
как отказ в доступе.

**Починка:** `MOUNT_OPTS=ro` (журнал к этому моменту проигран e2fsck), после
systemd корень `rw,noatime` с журналом. Проверено после прошивки,
самопроверка следит (`корень с журналом ext4`, 62/0). В `lost+found` новых
файлов нет, всё там ещё от 22 сентября.

**Открыто:** если загрузка ещё раз встанет, подробностей не будет. Стоит
сделать, чтобы аварийный режим systemd сам сохранял журнал на диск и
поднимал sshd.

## 2026-09-23 16:12 — «Access denied» с фото хозяина: безвредная строка ramdisk; экран загрузки чище

На фото консоли зависшей загрузки: `/init: line 57: can't create
…/rndis.usb0/ifname: Permission denied`. Её печатает наш ramdisk при каждой
загрузке, обычно её закрывает phosh. Строка уже кончалась на
`2>/dev/null || true`, но «> файл» открывается раньше, чем действует
`2>/dev/null`. Теперь запись в фигурных скобках. Аварийного режима systemd на
фото нет, значит, зависло позже, без вывода, скорее всего на повреждённом
корне (см. запись выше).

Там же на экране были ошибки драйверов (камерные «Driver 'MAINAF' is
already registered», extcon, «autofs4», «hidepid=invisible» у timesyncd).
К `quiet` добавлен `loglevel=3`: на экран при загрузке идёт только
критическое, dmesg и журнал хранят всё. Проверено: `Permission denied` в
dmesg 0, в командной строке `quiet loglevel=3`, самопроверка 62/0.

Сводка состояния для хозяина — `docs/bf7-native/состояние-телефона.md`.

## 2026-09-23 16:38 — Мобильные данные: у модема MediaTek нет PPP, только CGACT и ccmni0

Хозяин разрешил мобильные данные («роуминг формальный»). Позже он же уточнил,
что эта SIM звонит только на приём, SMS отправляет, 10 ГБ интернета; исходящий
звонок на ней проверить нельзя в принципе.

**PPP здесь нет.** NetworkManager через ModemManager звонит `ATD*99***1#`. Сначала
pppd падал: «Couldn't set tty to PPP discipline», в ядре не было
`CONFIG_PPP_ASYNC` (теперь `=y`, ядро прошито). Потом LCP остался без ответа:
модем принял ATD за голосовой вызов, после этого AT-порт замолчал до
`systemctl restart bf7-modem`. На MTK данные идут не через AT-порт.

**Как на самом деле** (проверено вручную на AT-порту):
- `AT+CGDCONT=1,"IP","internet.tele2.ru"`, затем `AT+CGACT=1,1`;
- модем отвечает URC `+EIF: 100, ifup, 1, <адрес>, 1` и `+EIF: 100, mtu, 1500`.
  Интерфейс 100 — это `ccmni0`;
- `AT+CGCONTRDP=1` отдаёт DNS;
- поднять адрес /32 на `ccmni0`, маршрут через устройство — ping до 1.1.1.1
  проходит.

Отключение — `AT+CGACT=0,1`.

**Что мешает ModemManager 1.20.4:** общий класс bearer всегда берёт порт данных
типа AT (`common_get_at_data_port`) и метод IP ставит PPP. Сетевой порт
`ccmni0` MM видит (правило udev с тем же `ID_MM_PHYSDEV_UID`), но общий
bearer его не использует. Плагина MediaTek в 1.20 нет. Следующий шаг —
заплатка к `mm-broadband-bearer.c`: если у модема есть порт NET, то
`+CGACT=1,<cid>` на основном порту, порт данных `ccmni0`, статическая
настройка IP из `+CGCONTRDP`.

## 2026-09-23 17:12 — Входящие звонки: MM хоронит вызов по пустому +CLCC; потом сеть стала отвечать «занято»

**Звонки 16:37 и 16:41 (дошли, но принять нельзя).** Модем шлёт
`+CIEV: 5, 1` (индикатор «call»), затем каждые 4 с `+CLIP: "<номер>",145,…`
и `+CRING: VOICE`. MM заводит вызов (ringing-in), через ~2 с опрашивает
`AT+CLCC`, получает пустой `OK` и объявляет вызов завершённым; на следующий
гудок заводит новый. Так было пять объектов вызова за один звонок. «Вызовы»
показывали «Вызов завершён», ответ (`ATA` от MM) сразу давал ошибку.
Подпись «Этот вызов не зашифрован» — штатная для сотового звонка, не ошибка.
Заводской RIL (`libmtk-ril.so`) шлёт `AT+EAIC=2` и `AT+ECPI=4294967295` и
работает через мультиплексор (`/dev/radio/pttycmd*`). У нас `+EAIC: 3`: по
phhusson/treble_experimentations#57 это «без одобрения входящих».

**С ~16:45 сеть отвечает «занято» сразу, без гудков.** До модема вызов не
доходит: в подробном журнале MM ни одной строки о вызове, хотя SMS в те же
минуты приходят (значит, сеть телефон находит). Не помогли: `+ECPI=0`
(мой опыт в 16:43 включил все биты), перерегистрация `CFUN=4/1` (после неё
телефон на LTE), перезапуск `bf7-modem`. Запросы к сети: запретов нет
(`CLCK "AI"/"IR"` = 0), безусловной переадресации нет. Условные
(занят / нет ответа / недоступен) ведут на служебный номер Tele2 7900…,
это заводская настройка оператора. Из формального роуминга переадресация,
вероятно, и даёт «занято». В 17:09 перезагрузил телефон целиком, жду
пробного звонка.

Просьбы позвонить — SMS хозяину (он разрешил), ответы он шлёт SMS; их текст
MM пишет в журнал на уровне DEBUG (`converted SMS part text`).

## 2026-09-23 17:22 — Полная перезагрузка «занято» не сняла; исходящий по просьбе хозяина; мусор +EDMFAPP ломал MM

**«Занято» не от телефона.** После полной перезагрузки (17:09) хозяин снова
услышал «занято» сразу, без гудков, а модем опять не получил ни строки о
вызове. SMS в обе стороны ходят. Между удачным звонком (16:41) и первым
«занято» (~16:48) модем не записал в NVRAM ни одного файла. Баланс SIM
0,00 ₽ (`*105#`) — это норма: тариф помесячный (10 ГБ и SMS, без исходящих,
входящие бесплатны), к звонкам отношения не имеет. Остаются сторона сети
(номер «занят» в MSC после серии сброшенных вызовов в 16:37) или сторона
звонящего. Хозяин ставит вторую SIM из своего телефона, «заведомо
рабочую».

**Исходящий (с разрешения хозяина, на его номер).** MM: `ATD<номер>;` →
сразу `OK` и `+ECPI: 1,130,…` (назначение вызова; приходит и при
`+ECPI: 0`), `+CIEV: 5, 1`, через 13 с `NO CARRIER`, `+CEER: 0,NONE`. MM
при этом объявил вызов завершённым сразу после `OK` — у MediaTek `ATD`
отвечает `OK` до соединения, ход вызова идёт отдельными сообщениями.

**+EDMFAPP ломал разбор ответов MM.** USSD `*105#` через MM упал: «Invalid
USSD message received: '+EDMFAPP: …'» — две строки отладки модема встали
перед `+CUSD`. Тот же запрос отладочной командой `+CUSD=1,"*105#",15`
прошёл. Мост `bf7-at-most` теперь вырезает строки `+EDMFAPP:` (на телефон
ещё не поставлен — хозяин остановил, пока меняет SIM).

**Пакет MM 1.20.4-1+bf7.1 собран на телефоне** с двумя заплатками
(`hybris/bf7-patches/modemmanager-1.20-mtk-*.patch`): данные через
`+CGACT`/`ccmni0`/`+CGCONTRDP` и отказ от `+CLCC` для MediaTek; обе
включаются метками udev из `77-bf7-modem.rules`. В изменённых файлах 0
предупреждений компилятора. Не установлен и не проверен.

## 2026-09-23 18:28 — Мобильный интернет через MM работает; для звонков нужен мультиплексор CMUX

Хозяин поставил вторую SIM второго оператора (250xx, домашняя сеть, EDGE).

**Данные — пять ловушек подряд**, все закрыты в заплатке
`modemmanager-1.20-mtk-cgact-data.patch` (пакет MM 1.20.4-1+bf7.2):
1. У модема общего плагина `data_net_supported = FALSE` — `ccmni0` в порты
   данных не попадал («net ports available but ignored»), и MM звонил
   `ATD*99***0#`: AT-порт виснет до перезапуска модема. Теперь сетевой порт с
   меткой `ID_MM_BF7_MTK_DATA` берётся всегда, а для модема MediaTek
   `ATD*99#` не посылается никогда (ошибка вместо него).
2. Контекст 0 у MediaTek — служебный (LTE attach): `+CGACT=1,0` → `+CME ERROR:
   5745`. Нижняя граница cid поднята до 1.
3. `+CME ERROR: 5745` и на cid 1: передача данных не разрешена. Заводской RIL
   на однослотовом телефоне шлёт при загрузке `AT+EDALLOW=1` — теперь его шлёт
   `bf7-modem-sim`. **Запрос `AT+EDALLOW?` вешает командный канал** — только
   установка.
4. `+CGCONTRDP` у MediaTek отдаёт пустой адрес и только DNS; MM-овский разбор
   при пустом поле сдвигает DNS. Адрес — из `+CGPADDR`, DNS — своим разбором.
5. **Модем сам выбирает интерфейс, по кругу**: `+EIF: 100, ifup…` → ccmni0,
   дальше 201 → ccmni1, 302 → ccmni2, 403, 504… (интерфейс ccmni<id % 100>).
   MM ловит `+EIF` (мост его пропускает), ccmni0…ccmni20 — порты модема.

Итог: подключение через NM («Оператор-2», `internet.example`), ping 4/4 ~420 мс,
HTTPS по одной сотовой сети, ~17 КБ/с на EDGE, переподключения ловят новый
ccmniN. DNS сотовых профилей опущены ниже Wi-Fi (`ipv4.dns-priority 150`),
иначе запросы шли к DNS оператора через Wi-Fi. По SIM хозяина данные
выключены (автоподключение только у Tele2 с её 10 ГБ).

**Звонки: на голом ttyC0 ответить нельзя.** Вызов приходит (`+CLIP`,
`+CRING`), с заплаткой no-CLCC больше не пропадает, но `ATA` → `+CME ERROR:
100`, `AT+CHUP` → `OK` без действия, `+CLCC` пуст. В Android на ttyC0 сидел
`gsm0710muxd` (CMUX), RIL работал по его каналам. Заводской muxd (32-бит)
запускается с нашими 32-бит подпорками: порт берёт не из `-s`, а
`/dev/ttyUSB1` (симлинк на ttyC0), стартовать сразу после перезапуска модема
(ждёт `+EIND: 128`, иначе канал остаётся в CMUX и следующий старт не
проходит). Каналы `/dev/radio/ptty*`; незапрошенные сообщения идут только в
`pttynoti`, команды он тоже принимает. MM посажен на `pttynoti` через
`bf7-at-most`: регистрация, SMS туда и обратно работают. Жду звонка, чтобы
проверить ответ.

Ускорение загрузки: `hal-start.sh` искал процессы внешним `readlink` на
каждый процесс (2078 мс на обход, ~2800 порождений при загрузке) — теперь
`[[ -ef ]]` (28 мс); `bf7-android-hal` 16,1 → 1,6 с, модем готов на 7,4 с
вместо ~19,5, загрузка 21,8 с.

## 2026-09-23 18:56 — Звонки работают целиком: входящий ждал одобрения (+EAIC); голос в разговор

**Корень «звонит, но не ответить».** Модем MediaTek сообщает о входящем
`+EAIC: <id>,"<номер>",<тип>,<режим>,<seq>` и держит вызов, пока телефон не
одобрит его `AT+EAIC=0,<id>,<seq>`. Заводской RIL включает этот режим
(`AT+EAIC=2`) и одобряет каждый вызов. У нас был режим 3: `+CLIP`/`+CRING` шли,
но `+CLCC` пуст, `ATA` → `+CME ERROR: 100`, `AT+CHUP` → `OK` без действия.
Мультиплексор gsm0710muxd оказался не нужен: на голом ttyC0 с одобрением
всё работает (проверено отдельным звонком). Одобрение с неверным `seq`
(взял последнее поле вместо пятого) модем 20 с ждёт и снимает вызов с
причиной 102 («истёк таймер») — хозяин слышал «абонент временно недоступен».

**Что сделано** (заплатка `modemmanager-1.20-mtk-no-clcc.patch`, пакет
1.20.4-1+bf7.3; `bf7-modem-sim`; мост):
- MM у модема с меткой MediaTek сам отвечает `AT+EAIC=0,<id>,<seq>`;
  `bf7-modem-sim` при каждом старте модема шлёт `AT+EAIC=2`;
- `+CLCC` для MediaTek не используется (пуст до ответа);
- `NO CARRIER` ловится в любом месте буфера: модем присылает его в одном
  куске с `+CIEV: 5, 0`, а MM искал его только в конце — вызов оставался
  «активным», и «Вызовы» тут же клали каждый новый исходящий;
- мост держит недописанный хвост `<CR><LF>+E…` до следующего пакета:
  разорванный `+EDMFAPP` прилип к ответу на `ATA`, и MM бросил принятый
  вызов (модем его принял — хозяин слышал писк без тракта разговора);
- `+EAIC` и `+EIF` мост пропускает к MM, прочие `+E…` режет и пишет в журнал.

**Проверено звонками хозяина (SIM второго оператора):** входящий — одобрение, гудки,
ответ, звук в обе стороны, конец по `NO CARRIER`, возврат обычного звука;
исходящий через «Вызовы» — хозяин ответил, разговор. Громкость разговора
0,8 → 1,0 (`set_voice_volume`; таблица SpeechVol даёт на верхней ступени
на 5–9 дБ меньше затухания) — «вроде нормально».

**Голос в разговор** (`bf7-skazat "текст"`): в режиме IN_CALL HAL MediaTek
отдаёт запись в основной выход модему как «фоновый звук»
(`AudioALSAPlaybackHandlerVoice`), но к собеседнику он заглушён — нужен
`set_parameters("Set_BGS_UL_Mute=0")`. `bf7-rech` получил команду
`igraj <файл>`, `bf7-zvonki` — канал команд `/run/bf7-rech.cmd`. Синтез:
Festival с `festvox-ru` (espeak-ng хозяин назвал «калл»). Хозяин: «Легенда!
… а так слышно».

## 2026-09-23 19:10 — Первый живой голосовой разговор с телефоном: whisper + Piper

По просьбе хозяина («чтобы у нас диалог в реальном времени был»).

**Слух.** `bf7-rech` получил команду `zapis <файл>`: вход HAL
`AUDIO_DEVICE_IN_TELEPHONY_RX`, источник `AUDIO_SOURCE_VOICE_DOWNLINK`,
16 кГц моно — голос собеседника пишется в `/run/bf7-sobesednik.raw`
(проверено: растёт по 32 КБ/с). Свой голос в запись не попадает:
`Set_BGS_DL_Mute=1`. Распознавание — whisper.cpp:
- на ПК (`golos-pc/sluh-pc`): поток тянется по SSH (`tail -F`), фразы
  режутся по паузам (порог энергии втрое выше фона, 700 мс тишины),
  `large-v3-turbo-q5_0` на RTX 3060 — фраза 1–7 с распознаётся за 0,8 с;
- на телефоне (`bf7-sluh`) — запасной путь, whisper.cpp собирается на A53.

**Голос.** Piper (`ru_RU-dmitri-medium`): на ПК 3,4 с речи за 0,4 с, на
телефоне 3,9 с за 9,5 с (под нагрузкой сборок) — поэтому синтез на ПК
(`golos-pc/skazat-pc`: Piper → 48 кГц стерео → scp → «igraj»).

**Разговор 19:08–19:10.** Позвонил хозяину через «Вызовы» (`gnome-calls
--dial` в среде сеанса phosh). Распознано: «Как дела?», «Прекрасно. Мне
интересно, что будет, если позвонит другой номер телефона? Что ты сделаешь?»,
«Ладно.», «Ну скажем, до свидания. Давай, отключайся.» — на каждую фразу
ответ голосом через ~7–8 с (распознавание 0,8 с, остальное — я и синтез).
Положил трубку сам по просьбе хозяина. Одна фраза распознана невнятно
(«Так взяла.»).

## 2026-09-23 19:27 — Автоответ хозяину, интернет по SIM, аппаратное видео было выключено

**Хозяин звонит — Клауд отвечает.** `bf7-hozyain` (служба на телефоне) слушает
`CallAdded` у MM и через 2 с принимает входящий, только если последние
10 цифр номера совпали с `/etc/bf7/hozyain-nomer` (номер вне git), пишет
«ЗВОНОК: хозяин, принят» в `/run/bf7-dialog.log`. На ПК
`golos-pc/dialog-storozh` ловит эту строку, здоровается (Piper) и включает
`sluh-pc`. Проверено звонком 19:17: трубка взята сама, распознано «Как там у
меня компьютер поживает?», «До новых встреч!», «Понял, спасибо. Давай, до
связи. Ещё позвоню». Фильтр галлюцинаций whisper («Продолжение следует»,
«Субтитры…», «Музыка») и фраз короче 0,5 с. Хозяин: писать и звонить ему
можно, но только с 9:00 до 21:00.

**Мобильный интернет по SIM.** NM поднимал «Tele2» на SIM второго оператора: при старте
модема оператор SIM ещё неизвестен, и `gsm.sim-operator-id` проверку
пропускает. У сотовых профилей автоподключение выключено; `bf7-mobil-avto`
поднимает профиль, только когда MM назвал оператора SIM и он совпал, и только
из `/etc/bf7/mobil-avto` (сейчас «Tele2»).

**Аппаратное видео не работало.** `bf7-videod` был выключен (`disabled`, его
не было в списке включения установщика) — VA-API: «Failed to create decode
context». Включён, внесён в установщик и самопроверку (65/0). Падение демона
при `kill -9` клиента посреди раскода `Restart=always` лечит за 2 с. Один раз
сразу после перезапуска демона 185 из 600 кадров 720p дали «Failed to end
picture decode»; повторы и ролики 640×480…1080p, Baseline/High — чисто.

**Прочее.** Средняя нагрузка ~16 при свободном процессоре — это спящие в
состоянии D потоки ядра MediaTek (`tee_irq_bh`, `rpmb_open`, `gauge_coulomb`,
`disp_idlemgr`…), не работа. Счётчик слота A в `para` снова 7 попыток с
отметкой успеха (чтение, без записи). Адрес Bluetooth настоящий. Запись с
микрофона — оба канала с сигналом. `bf7-install.sh --proverka`: расхождений
репозитория и телефона 0.

## 2026-09-23 19:55 — Аппаратное видео: драйвер на телефоне отставал, демон копил буферы до падения

**Три слоя поломки.**
1. `bf7-videod` был выключен (см. запись выше) — включён.
2. **Установленный драйвер VA-API** (`/usr/lib/aarch64-linux-gnu/dri/bf7_drv_video.so`)
   собран 2026-09-17 19:34, а отпускание кадров демону (`BF7V_MSG_RELEASE`
   при повторном использовании поверхности) добавлено в исходник в 19:49 того
   же дня — на телефоне его не было. Пересобран из `vaapi-shim/` на телефоне
   (`sh build.sh`), старый сохранён в `/root/bf7_drv_video.so.2026-09-17`.
   Сборка упала: копии заголовка `bf7-videod-proto.h` в `vaapi-shim/src` и
   `hybris/bf7-codec2` разошлись (`BF7V_FLAG_DRAIN` только во второй) —
   выровнены.
3. **Демон копил выходные буферы до «Too many open files».** Разбивка при
   1024 дескрипторах: 568 dma-buf и 274 `/dev/ion` — буферы gralloc. Клиент,
   который кадры не забирает (`ffmpeg -f null` с кадрами в видеопамяти не
   зовёт `vaSyncSurface`, трассировка: ни одной просьбы RELEASE/DRAIN),
   закидывает декодер всем потоком, и декодер MediaTek заводит выходной буфер
   под каждую ждущую работу; демон падал (`Restart=always` поднимал), у всех
   «Failed to end picture decode». Лечение в демоне: не больше 16 работ в
   декодере (обратное давление: ждать готовые кадры до 0,5 с), не больше 12
   выданных и 12 готовых кадров (лишние старые — в пул). Пределы подобраны под
   пул (~50 буферов): при 32+32 декодер ждал свободный буфер, а демон — кадр.

**Проверено (640×480, 1800 кадров):** честный клиент (`-vf hwdownload`) —
0 ошибок, ≤212 дескрипторов, ~90 кадров/с; клиент без забора кадров — демон
жив, ≤348 дескрипторов, но медленно (душится обратным давлением).

## 2026-09-23 19:58 — Проверка графики по старым записям (просьба хозяина)

Сверено с памятью (bf7-wlroots-phosh, bf7-standard-wayland,
bf7-xwayland-glamor-working, bf7-hybris-hal-paths) — всё подтвердилось:
- **Композитор phoc рисует на PowerVR.** Ловушка проверки: процессов phoc
  два — настоящий (здесь PID 779: `renderD128` ×4, `libIMGegl.so` в карте
  памяти) и его дочерний встроенный seatd (только `card1`, без GL). `pidof
  phoc` отдаёт дочерний первым — по нему выходит «композитор без GPU».
- **GLES у обычных программ Wayland:** `glmark2-es2-wayland --off-screen`
  (с `LD_PRELOAD=libbf7eglfilter.so`) — PowerVR Rogue GE8300, OpenGL ES 3.2
  build 1.15@6070602; build 416, texture 542, phong 200, jellyfish 107,
  terrain 18 кадров/с, балл 255.
- **Xwayland — glamor на GPU:** в процессе libhybris, `libIMGegl.so`,
  `libsrv_um.so`, `pvr_gbm.so`.
- **Vulkan** только нашим загрузчиком (`LD_LIBRARY_PATH=/usr/local/lib
  LD_PRELOAD=/usr/local/lib/libbf7tlsguard.so vulkaninfo`): GE8300, API
  1.1.170. Загрузчик Debian драйверов не находит (`icd.d` пуст) — так и было:
  андроидный драйвер не ICD Khronos; обёртка потребовала бы оборачивать каждый
  объект драйвера (первое слово — указатель диспетчеризации загрузчика).
- Экран объявляет 60 Гц (самопроверка).

## 2026-09-23 20:18 — Vulkan на Wayland: три поломки в libhybris, vkcube идёт ~57 кадров/с

Хозяин спросил, получают ли программы Vulkan («эмулятор на вулкане»).
Через наш загрузчик (`LD_LIBRARY_PATH=/usr/local/lib LD_PRELOAD=libbf7tlsguard.so`)
`vkcube-wayland` видел PowerVR Rogue GE8300, но:
1. «wl_buffer: unknown buffer type», ошибка протокола — Vulkan-часть libhybris
   не подключала `zwp_linux_dmabuf_v1` (EGL-часть подключала), кадры шли в
   андроидном типе `android_wlegl`, которого wlroots не знает;
2. после этого — вечное ожидание в `dequeueBuffer`. В отладчике: буферы 1×1 —
   окно заводится `wl_egl_window_create(surface, 1, 1)`, андроидный Vulkan
   сообщал «текущий и наибольший размер 1×1», и swapchain выходил в пиксель.
   Теперь «размер не задан», как у Mesa;
3. всё равно висело: буфер 0 (500×500) у композитора, буферы 1–2 уже 1280×720
   — программа получила размер окна и пересоздала swapchain, андроидный Vulkan
   берёт все картинки заново, свободных два из трёх, а третий вернётся только
   после нового кадра. Теперь окно при нехватке заводит ещё буфер (до шести).

Проверено на втором, безэкранном phoc (`WLR_BACKENDS=headless`, свой
`XDG_RUNTIME_DIR`): `vkcube --c 300` за 5,7 с (~57 кадров/с, упор в 60 Гц).
На настоящем экране глазами не проверено: телефон заблокирован, PIN я не ввожу.
Тонкость замера: при погашенном экране композитор не шлёт «кадр показан», и
любая программа с вертикальной синхронизацией стоит.

## 2026-09-23 21:20 — Вход без PIN, «палец» для тача, звонок хозяину, GL против Vulkan в RetroArch

**Экран блокировки без PIN** (хозяин: «Отключай, потом включим»). Phosh 0.24
ключ `org.gnome.desktop.screensaver lock-enabled` не читает — блокирует при
каждом сне и простое, а от блокировки здесь зависит гашение экрана. Заплатка
`phosh-0.24-lock-enabled.patch`: при `lock-enabled` = false перелистывание на
страницу PIN сразу разблокирует. Пакет `0.24.0-1+bf7.3`, hold. Проверено
камерой: кнопка питания ×2 → часы и стрелка → свайп вверх → сетка
приложений, клавиатуры PIN нет. Вернуть PIN: `gsettings set
org.gnome.desktop.screensaver lock-enabled true`.

**`bf7-palec`** — касания и свайпы без рук: запись `input_event` прямо в
`/dev/input/event2` (ядро принимает запись в evdev как ввод устройства).
Ловушка: тач развёрнут на 180° матрицей `LIBINPUT_CALIBRATION_MATRIX=-1 0 1
0 -1 1` (99-bf7-touch.rules) — первый свайп «вверх от низа» открыл шторку
(стал «вниз от верхнего края»). Теперь экранные координаты идут через
обратную матрицу из udev.

**Исходящий звонок: приветствие ушло в гудки.** MM ставит вызову «active»
сразу после ATD (у MTK нет CLCC, ATD отвечает OK до гудков). Настоящий ответ —
`+ECPI: 1,6` в журнале bf7-at-most (последовательность: 3, 4, 2 — гудки, 132,
затем 6 через ~8 с). `bf7-pozvonit` ждёт именно 6 и лишь потом пишет «принят»
для ПК-сторожа; хозяину — только 09:00–21:00. SMS через mmcli: текст в
кавычках (`text="…"`), иначе «Клауд:» и запятые ломают разбор.

**RetroArch: GL против Vulkan** (µCity на gambatte, безэкранный phoc,
vsync и синхронизация звука выключены, 3000 кадров):
- первый замер (GL ~32, Vulkan ~28 кадров/с) — **ошибка замера**: виджеты
  RetroArch перестраивались на каждом кадре и 3 раза за кадр читали шрифты
  `ozone/*.ttf` (strace: 903 открытия на 300 кадров). У безэкранного вывода
  нет физического размера (wlr-randr: без «Physical size»), у настоящей
  панели 62×139 мм. Для замера — `menu_enable_widgets = "false"`;
- без виджетов: в окне GL 83–104, Vulkan 84–108; на весь экран 1280×720 GL 71
  (один раз 167), Vulkan 72–73. Эмуляция без вывода (`video_driver null`) —
  ~1180 кадров/с, то есть оба упираются не в процессор и не в GPU, а в
  передачу кадра композитору. **Поровну**; Game Boy на полной скорости с любым.
- Vulkan-окно libhybris отдавало кадр композитору ДО ожидания готовности GPU
  (`presentBuffer`, потом `sync_wait`); в EGL-части наоборот. Исправлено в
  дереве ПК, на телефоне ещё не собрано и не проверено.

## 2026-09-23 22:30 — Экран не гаснет в играх; RetroArch на весь экран: три поломки, найдены камерой

**phoc без `zwp_idle_inhibit_manager_v1`.** RetroArch, mpv и SDL-игры держат
экран только этим протоколом — экран гас посреди игры (порог простоя 120 с),
а программа с vsync вставала. Заплатка `phoc-0.24-idle-inhibit.patch`
(`wlr_idle_inhibit_v1` → `wlr_idle_set_enabled`, только для видимых окон,
перепроверка раз в 5 с), пакет `phoc 0.24.0-1+bf7.2`, hold. Проверено: на
весь экран 3+ минуты — подсветка 141, «Idle inhibited» / «no longer inhibited».

**RetroArch на весь экран застывал на своей заставке** (полоса со значком — её
RetroArch рисует через wl_shm, пока грузится). Нашла только камера: grim при
прямом показе (scan-out полноэкранного окна) отдаёт СТАРЫЙ кадр композитора —
у нас он был белым, панель же показывала заставку. Стек: `dequeueBuffer` →
`readQueue(true)`, все 6 буферов «заняты», 5 без wl_buffer. Две ошибки учёта в
libhybris `wayland_window_common.cpp` (подробно — README заплаток): занятые
буферы уничтожались с вычитанием из свободных; брошенные картинки прежних
swapchain (андроидный `api_disconnect/connect` в libhybris пустой) не
возвращались. После первой правки RetroArch пошёл, но пересоздавал swapchain
без конца (OUT_OF_DATE на «чужой» буфер) — старые буферы надо уничтожать, а не
возвращать в запас. Итог: swapchain создаётся один раз, кадры идут.

**RetroArch на весь экран грузил ядро на 98% и шёл 15 кадров/с.** Горячая
точка — `libufwriter.so` (компилятор шейдеров PowerVR). Не прямой показ:
воспроизводится на безэкранном phoc с выходом 720×1612 и масштабом 2. gdb:
`vulkan_create_swapchain` каждый кадр из `gfx_ctx_wl_set_resize`, текстуры и
конвейеры заново. Причина в RetroArch 1.14 (`gfx_ctx_wl_check_window_common`):
уже умноженный на масштаб размер сравнивался с ещё раз умноженным
(`*width * last_buffer_scale`) — при масштабе 2 «изменение размера» каждый
кадр. Исправлено в `retroarch-1.14-vulkan.patch`, пакет `+bf7.2`.

**Камера.** Хозяин: смотреть полный кадр, чинить только цвет. У камеры скрытые
автоэкспозиция и автобаланс, подстраиваются под сцену: постоянная поправка по
таблице цветов красит тёмный интерфейс в розовый. Оставлены уровни по каналам
в каждом кадре (`bf7-glaz.sh --polnyj`). На тёмном экране яркие значки
пересвечены в датчике — лечится только светом или другой камерой.

**23:10 — итог по полноэкранному RetroArch.** Правка RetroArch поначалу «не
сработала» — оказалось, `dpkg-buildpackage -nc` не перекомпилировал (debhelper
счёл сборку сделанной), в пакет ушёл старый двоичный файл; после `make` —
сработала: без экрана 720×1612@2 стало 22% ядра. На панели ещё оставалось
27 кадров/с: при прямом показе композитор держит буферы дольше, и давняя
подпорка libhybris «нет свободного буфера — заведи ещё» подсовывала
андроидному Vulkan незнакомый буфер (OUT_OF_DATE → пересоздание). Подпорка
убрана (её задачу теперь решает setBufferCount). Итог на панели, при
подсветке 10 (ночь): Vulkan на весь экран 60 кадров/с, 24% ядра, 0
пересозданий, камера показывает µCity. vkcube без взаимной блокировки.
Ночь: по просьбе хозяина `bf7-tishina do 09:00` — звук выключен до утра.

**23:15 — GL против Vulkan на панели, на весь экран, после всех правок** (µCity,
vsync, подсветка 10): оба 60 кадров/с; ядро — GL 36%, Vulkan 24%. Под
нагрузкой на GPU (шейдер 64 выборки, 1280×720 без экрана): с циклом Vulkan
быстрее на 23% (15,5 против 12,5), с раскрытым циклом GL на 6% (11,7 против
11,0) — решает компилятор шейдеров драйвера, а не API. Ответ хозяину:
для эмуляторов оба тянут, Vulkan экономнее по процессору.

## 2026-09-24 08:40 — Исходящий звонок по +ECPI: MM +bf7.5 и мост, проверено с хозяином

Хозяин разрешил проверку до 9:00 («можно сейчас, я не сплю»). Поставлены вместе
MM `+bf7.4` и мост с пропуском `+ECPI`. Первый звонок оборвался в MM: «Unhandled
response '+CIEV: 5, 1'» — регулярка `+ECPI` съедала и завершающий `<CR><LF>`, а
`+CIEV`, пришедший вплотную, без него не опознавался своим обработчиком и попадал
в ответ на ATD (модем при этом продолжал набор — трубку клал `--voice-hangup-all`).
Исправлено lookahead `(?=\r\n)`, пакет `+bf7.5`. Второй звонок: dialing →
ringing-out (+ECPI 2) → active (+ECPI 6), `bf7-pozvonit` отдал разговор сторожу
только после ответа; хозяин: «Слышу, всё прекрасно». `bf7-pozvonit` получил
`BF7_HOZYAIN_RAZRESHIL=1` — звонок хозяину вне 09–21 только с его слова.

## 2026-09-24 09:05 — 4G и скорость мобильного интернета (вопрос хозяина)

ModemManager режимы у MTK менять не умеет; предпочтение LTE — `AT+ERAT=6,4`
(теперь в bf7-modem-sim). Модем остаётся на EDGE; `AT+ERAT=3` (только LTE)
принимается лишь при CFUN=4 (иначе CME 100) и даёт «нет сети» 2 минуты, хотя
сота LTE видна (`+CREG` с AcT 7, stat 0). Возврат: CFUN=4 → ERAT=6,4 → CFUN=1
(первая попытка не применилась — проверять `AT+ERAT?`). Вывод: 4G на SIM
второго оператора не пускают (с Tele2 LTE был). Скорость EDGE: ~125/40 кбит/с, пинг ~400 мс.

## 2026-09-24 09:12 — 4G заработал: не хватало APN входа в LTE (AT+EIAAPN)

Хозяин: «4G на этой SIM 1000% работает… APN точно настроил?». В строках
libmtk-ril.so — `AT+EIAAPN="%s",%d,"%s","%s",%d,"%s","%s"` (обработчик
RFX_MSG_REQUEST_SET_INITIAL_ATTACH_APN) и `AT+EIAREG=1`. APN второго оператора с сайта
оператора: internet.example, user/internet, PAP/CHAP. `AT+EIAAPN="internet.example",0,
"IPV4V6","IPV4V6",3,"user","internet"` → CFUN=1 → `+COPS: 0,0,"OPERATOR2",7`.
Замер на LTE (маршрут /32 через ccmni2): загрузка 18,6 Мбит/с (20 МБ за 8,6 с),
отдача 4,4 Мбит/с, пинг 46–197 мс (ср. 80). На EDGE было 125/40 кбит/с.
bf7-modem-sim ставит APN входа по MCC+MNC SIM (таблица IA_APN), проверено
путём загрузки (CFUN=0 → служба → MM): сразу `registered lte`.

**09:14 — звонок на LTE.** Входящий от хозяина при LTE: пришёл, модем ушёл на
EDGE (CSFB, VoLTE нет), bf7-hozyain взял трубку, разговор слышен «как обычно».
После отбоя модем вернулся в LTE в пределах 5 с.

## 2026-09-24 09:35 — START на экранных кнопках: касания в RetroArch уходили на 48 точек вниз

gdb в `input_overlay_poll`: X точно, Y +0,03 у всех кнопок (START 0,915 → 0,945);
широкие крестовина и A это прощали, узкие START/SELECT (±0,018) — нет. GTK-окно
на весь экран получает касания точно — тач и phoc ни при чём. WAYLAND_DEBUG:
RetroArch через libdecor (модуль cairo) рисует заголовок подповерхностью на
(0,-24) и ставит геометрию (0,-24,…); перейдя на весь экран, исправляет
геометрию на (0,0), но окно в phoc остаётся сдвинутым на 24 логические точки
(картинка при прямом показе на месте, касания — нет). `video_window_show_
decorations` на Wayland не действует. Лечение без пересборки: libdecor без
модулей (`LIBDECOR_PLUGIN_DIR` в пустой каталог — «falling back on no
decorations») в обёртке bf7-retroarch. Проверено камерой: START открывает меню.
Попутно: тон через paplay и GStreamer, записанный микрофоном вебкамеры, чистый
(0 провалов); драйвер днём под ними не ругается, под RetroArch — ругается
(103 за 20 с), слышно ли это — не удалось измерить (запись музыки тихая).
Корень — в phoc (`handle_surface_commit` сдвигает окно при смене геометрии и
для полноэкранных окон): заплатка `phoc-0.24-fullscreen-geometry.patch`, пакет
`+bf7.3`. С ней RetroArch и с рамкой libdecor получает касания точно. Отмечено:
один раз свайп не открыл экран блокировки после серии нажатий кнопки питания;
откат на `+bf7.2` и повтор ровно той же последовательности на обоих пакетах —
открывает, т.е. не заплатка.

## 2026-09-24 10:10 — Заедание звука в играх: драйвер MTK терял хвосты записей (ядро)

Хозяин на слух: «проглатывает, заедает, потом нормально, на одних и тех же
местах». kprobe на `mtk_pcm_copy` (mtk-soc-pcm-dl1.c — именно он обслуживает
MultiMedia1/hw:0,0; первая правка ушла в общий mtk_mem_dlblk_copy, который
этот путь не использует — урок «у драйверов по нескольку копий» снова):
paplay — 945 записей, все кратны 16 байтам; RetroArch за 10 с — 4500 записей,
1408 не кратны 16, 11552 байта потеряно. `copy_size = word_size_align(...)`
округлял вниз до 16 и отбрасывал хвост, ALSA считала его записанным, учёт
расходился до недоигрывания ([AudioWarn] u4DataRemained < 0). Округление
убрано в DL1 и в общем пути (копия идёт через посредника и memcpy_toio).
Ядро x657b-4.19 744adcebc, прошито bf7-proshit.sh: загрузка 35 с, 71/0.
После: RetroArch 20 с — 0 предупреждений (было 103–146), paplay 0, старый
«ронятель» aplay с периодом 1021 кадр (невыровненные записи) — без сбоев,
микрофон пишет. Попутно: после холодной загрузки модем на EDGE, хотя
EIAAPN принят, — утренний переход в LTE повторить не удаётся, разбираюсь.

## 2026-09-24 10:40 — LTE после холодной загрузки не поднимается сам

После перепрошивки ядра модем на EDGE, хотя `AT+EIAAPN` принят (bf7-modem-sim).
Утренний вход в LTE (09:08) и повтор в 10:21 оба случились после «только LTE»
(`AT+ERAT=3`) при включённом радио и возврата `AT+ERAT=6,4` — модем остаётся
в LTE, `+ERAT: 7,0,6,4`. Но надёжно это не повторяется: `ERAT=3` то принят, то
`CME ERROR: 100`; при загрузке (ERAT=3 → CFUN=1 → 45 с) LTE не нашёлся; ручной
замер — 5 минут на GSM. Каждая попытка — минуты без связи, поэтому из загрузки
танец убран: остаются APN входа и ERAT=6,4. Не проверено: `AT+EIAREG=1` (его
тоже шлёт заводской RIL). Урок: `pkill -f` с шаблоном, попадающим в командную
строку самой ssh-оболочки или монитора, убивает их самих (дважды за утро).

**11:05 — LTE при каждой загрузке.** Три недостающих части: (1) `AT+EIAAPN`
(APN входа); (2) контекст по умолчанию `AT+CGDCONT=1,"IPV4V6",<apn>` — без него
при загрузке модем за 120 с в LTE не регистрировался, а в работающей системе
его заводил MM при включении интернета, отсюда «днём работает»; (3) MM слал
`AT+COPS=0` при регистрации, и модем перевыбирал GSM — заплатка
`modemmanager-1.20-mtk-no-cops0.patch` (+bf7.6). Плюс вход «только LTE» до
+ERAT 7 и +CEREG 1/5, затем ERAT=6,4. Ложный след: «+ERAT: 7» значит «нашёл
соту», а не «зарегистрирован»; и первая AT-команда после открытия порта
всегда отвечает CME 100 — из-за этого я полдня думал, что ERAT=3 «то
принимается, то нет». Холодный старт: `LTE — вошёл` через 5 с, MM — lte.

### 2026-09-24 11:07 — Автоподключение данных на SIM второго оператора

Хозяин разрешил мобильный интернет на втором операторе (тариф 50 ГБ). В
`/etc/bf7/mobil-avto` (на телефоне, вне git — как и номер хозяина) добавлена
«Оператор-2». После перезапуска `bf7-mobil-avto` профиль поднялся сам на ccmni0;
маршрутов по умолчанию два — wlan0 с метрикой 600 и ccmni0 с 700, так что при
Wi-Fi трафик идёт через Wi-Fi, а сотовая сеть подхватывает без него.

### 2026-09-24 11:20 — Нагрузка 14 при простое; зарядник и расход в покое

**Средняя нагрузка врёт.** Через 6 минут после загрузки `loadavg` 14, а `top`
показывает 99,4 % простоя. Нагрузку дают 13 потоков ядра MTK, спящих в
состоянии D (`battery_thread`, `charger_thread`, `disp_idlemgr`,
`hang_detect`, `ccci_poll1`, `tee_irq_bh` и др.). На этом ядре смотреть
`top`/%idle, а не loadavg. Чинить — трогать ожидания в дюжине драйверов ради
красивой цифры; не стал.

**Зомби** (7 штук под `gnome-session-binary` и `gsd-power`) — короткие
помощники автозапуска, которых сеанс не пожинает. Косметика, не трогал.

**Кулонометр виден:** `/sys/class/power_supply/mtk-gauge/coulomb_get`, единица
0,1 мА·ч (фаза заряда: 12 единиц за 71 с = 61 мА при замеренных 59). Ток —
`battery_current_get`, 0,1 мА. Мгновенный ток врёт в сторону разряда: его
читает проснувшийся ради `cat` процессор. Верить счётчику.

**Замер 11:23–11:35, экран погашен, Wi-Fi и LTE подняты, кабель ПК:**
- заряд окончен (STAT `0xe0`), 631 с: счётчик −134 ед. = 13,4 мА·ч →
  **≈76 мА** уходит на систему (мгновенные выборки давали −111, это смещение);
- дозаряд (STAT `0xd0`), 71 с: **+59 мА** в батарею, значит от кабеля в узел
  батареи приходит ~135 мА;
- в сон (freeze) телефон за 12 минут не ушёл ни разу — записи шли ровно раз в
  10 с. Это «погашен, но не спит», не «в покое».

**Зарядник (только чтение, `i2cget -f`).** AW32257 (клон bq24157):
`00=e0/d0` — STAT; `01=38` — по раскладке bq24157 биты 7:6 = 00, «вход
100 мА», но замер показал ~135 мА в узел батареи, так что предел не тот или
раскладка другая; `02=aa` — 4,34 В; `06=ff`. Важное из исходника Awinic
(`donors/awinic-aw322xx/.../aw32257.c`): `aw32257_set_input_current`
вычисляет значение и **не пишет его** — трогает только регистр 06. То есть и
заводской драйвер входной предел не поднимал; загрузка `aw32257.ko` заряд не
ускорит. Драйвер не заводил, регистры не писал.

`/proc/mtk_battery_cmd/en_power_path=0` не действует: при чтении снова 1,
в журнале `power_path_support:0`. У микросхемы нет раздельного питания
системы: когда заряд окончен, систему питает батарея — отсюда разряд «на
кабеле» при 100 %. Настоящий заряд по счётчику 96,7 % (`soc 9668`), 100 на
экране — удержание `keep_100`.

### 2026-09-24 12:05 — Сон на батарее рвал модем: ответ LTE на сообщение Wi-Fi

**Симптом.** `rtcwake -m freeze` просыпался через 0,5–1,3 с: `Resume caused
by IRQ 37, CCIF_AP_DATA`. Серия 6×30 с — 6 из 6 так. На батарее это значит,
что телефон почти не спит (gsd усыпляет, модем будит, bf7-dosyp усыпляет
снова).

**Кто.** Каждый такой сон — `ccci_ipc_3 +1` и `MT662x +2` в
`wakeup_sources`. По режимам (по три сна): обычный — 3 из 3 рваные; BT
заблокирован rfkill — 3 из 3 рваные; **Wi-Fi выключен — 3 из 3 полные (31 с)**.
`SETSUSPENDMODE 1` драйверу Wi-Fi (как делает Android при гашении экрана) —
не лечит: 1 из 3 всё равно рваный.

**Механизм** (журнал `/proc/driver/conn_md_dbg`, запись `0x2 0x0 0x0`, и
след `power:device_pm_callback_*`). Засыпание Wi-Fi (`ieee80211 phy1`,
асинхронно, 535,0–543,9 мс от начала сна) → прошивка connsys шлёт
сообщение сосуществования → WMT пересылает его модему (`AP_MOD_WMT
0x80000003 → MD_MOD_EL1 5`, 48 байт) → EL1 отвечает (480 байт) через
0,1–0,9 с. Точка невозврата — `noirq suspend of devices complete` —
наступает через ~260 мс после отправки. Ответ до неё — сон целый (сон 27:
ответ через 118 мс, спал до будильника), после неё — IRQ 37 будит (сон 28:
ответ через 920 мс, проснулся через 0,6 с). В целых снах модем потом молчал
всё окно — значит, само сообщение нужное, гасить его нельзя.

Ложные пути: `ccci_modem_suspend_noirq: weak function` — для MT6761 так и
должно быть (платформенные есть только у DPMAIF-чипов); `__pm_wakeup_event`
на приёме сон не рвёт, пока никто не пишет `/sys/power/wakeup_count`.

**Правка — в wmt_drv, оверлей** (`kernel-work/bf7-connectivity-overlay/common/
common_main/{linux/wmt_idc.c,platform/mtk_wcn_consys_hw.c}`), ядро не
трогал:
- каждое сообщение к EL1 помечает «ждём ответ»; ответ снимает метку;
- у платформенного устройства consys новые `.suspend` (с этого момента
  ответы EL1 не пересылаются в connsys сразу, а откладывается последний),
  `.suspend_late` (если ответа нет — ждать до 1 с) и `.resume` (переслать
  отложенное работой после пробуждения BTIF);
- пересылать сразу нельзя: к `.suspend_late` BTIF уже спит (774,9 мс), и
  отправка подняла бы его посреди засыпания (`_btif_exit_dpidle_from_sus`).

CRC экспорта wmt_drv совпадают со старым (108 символов), так что bt_drv и
прочие модули не пересобирались бы — но по правилу собраны все три.
Старые модули на телефоне: `/root/moduli-do-bf7-idc/`.

**Перезагрузка повисла.** `systemctl reboot` в 12:07: sshd и Wi-Fi
остановились, модем тоже, а до `reboot()` дело не дошло — ядро отвечает на
ping по USB 20+ минут, USB не переподключался (узел `ttyACM0` от 11:02).
Новые модули к этому моменту даже не были загружены — висит обычный путь
выключения. Нужна долгая кнопка питания. Разобрать по `journalctl -b -1`,
когда телефон вернётся.

### 2026-09-24 13:12 — Перезагрузка висла на выходе phoc при погашенном экране

Дважды (12:07 и 12:48) `systemctl reboot` не доходил до конца: ping по USB
отвечает, sshd, Wi-Fi и модем остановлены, консоль обрывается посреди строки.
Телефон сам сбрасывался через 15–25 минут (кнопку никто не нажимал —
хозяин на работе).

**Где.** Консоль прошлого ядра сохраняется в ramoops, systemd-pstore при
загрузке кладёт её в `/var/lib/systemd/pstore/console-ramoops-0`. След
обрывался на выходе phoc: `ion_client_destroy ... gralloc.RGX` — и ни строки
больше, даже от systemd.

**Почему.** Экран в оба раза был погашен (по простою). Когда phoc выходит,
tty1 возвращается из `KD_GRAPHICS` в текст, fbcon будит панель
(`fb_blank(UNBLANK)` → mtkfb → возобновление тракта экрана) одновременно с
закрытием KMS phoc — и виснет, держа замок консоли. Отсюда оборванная строка
и вставший systemd (пишет в консоль). Утренние перезагрузки проходили: экран
тогда горел. «После опытов со сном» — совпадение: во время опытов экран
гаснет.

**Проверка.** Экран погашен, fbcon отвязана (`vtcon1/bind=0`) —
`systemctl restart bf7-phosh` проходит; полная перезагрузка при погашенном
экране — 46 с до ssh.

**Правка.** `bf7-phosh.service`: `ExecStartPre` отвязывает fbcon от панели,
обратно не привязывается (привязка к спящей панели — тот же опасный путь).
fbcon нужна только на загрузке, до phoc. Корень — в mtkfb/KMS — не трогал:
обход закрывает и перезагрузку, и перезапуск, и падение phoc.

### 2026-09-24 13:35 — Сон после починки wmt_drv

Серия 6×30 с: **6 из 6 полные** (до починки 0 из 6); на каждый сон в dmesg
«answer received before suspend» и после — «held LTE msg 0x80000053 forwarded
after resume». Расход во сне на кабеле: 10-минутный freeze прерван через
141 с прерыванием PMIC (топливомер, законно), счётчик −2,6 мА·ч → ~66 мА.
Это не цифра батареи: на кабеле SPM не пускает в глубокие состояния
(`DP: No enter --- SODI3: No enter`, `soidle3_block_cnt: [BY_SRR]`) —
USB держит свои такты (см. пункт «Глубокий простой» в доводке). Настоящий
расход во сне — только без кабеля.

### 2026-09-24 14:05 — Поправка: fbcon ни при чём, перезагрузка висит по-прежнему

Вывод 13:12 неверен. С отвязанной fbcon перезагрузка в 13:39 снова повисла
на том же месте (pstore: `ion_client_destroy ... gralloc.RGX` от phoc — и
тишина), телефон сбросился сам в 13:54, на связи в 13:59. Удачная
перезагрузка 13:11 была везением, а не проверкой. Отвязка fbcon и проверка в
самопроверке откачены.

Проверено и НЕ воспроизводит: перезапуск bf7-phosh при погашенном экране
(с fbcon и без); перезапуск bf7-phosh при остановленном bf7-android-hal
(распределитель gralloc4 — при выключении он останавливается до выхода phoc,
порядка между службами нет). Повисает только полное выключение.

Что известно: ping по USB жив, systemd встаёт посреди строки консоли, в
ядре нет детектора зависших задач (`hung_task_timeout_secs` отсутствует),
SysRq включён. Сброс через 15–25 минут — не наш сторож (RebootWatchdog
systemd не успевает включиться). Утренние перезагрузки (две) прошли, из
трёх дневных повисли три.

### 2026-09-24 14:10 — Wi-Fi в режиме сна при погашенном экране

Замер при погашенном экране (`sched_wakeup`, `/proc/interrupts`): прерывания
wlan0 — 264 за 30 с, потоки драйвера `hif_thread` 12,8/с и `main_thread`
8,9/с — главные будильщики после потоков RCU. Android на гашение экрана шлёт
драйверу `SETSUSPENDMODE 1` (фильтр широковещательных в прошивке, пропуск
DTIM); ручная проверка: 264 → 4 за 30 с, ping и ssh по Wi-Fi работают
(задержка растёт до 55–280 мс).

Правка — в gen4m, оверлей `bf7-connectivity-overlay/gen4m/os/linux/gl_kal.c`:
у драйвера уже есть уведомитель гашения (`wlan_fb_notifier_callback`,
приходит от phoc), он только выключал монитор производительности. Теперь
ставит работу, которая под `rtnl_lock` делает то же, что устаревший
`wlan_early_suspend()` (нужен `CONFIG_HAS_EARLYSUSPEND`, в 4.19 его нет):
`wlanSetSuspendMode` + `p2pSetSuspendMode`. Из работы, а не из уведомителя:
FB_EVENT_BLANK идёт под замком консоли. Итог: экран горит — 165 прерываний
за 20 с, погашен — 5; ping и ssh по Wi-Fi при тёмном экране проходят.

### 2026-09-24 14:25 — Ядро: реже опрос датчика процессора, без отладочного монитора модема

Замер при тёмном экране: `thermal_zone_device_check` 8,5/с, из них
`mtktscpu` 4,9/с — между 20 и 40 °C множитель `tscpu_polling_factor1` был 1,
то есть опрос каждые 200 мс, как в жару. Стал 5 (1 с); на 40 °C и выше
частота прежняя, ограничение начинается с 54 °C. После прошивки: `mtktscpu`
1,0/с, все датчики 4,55/с (было 8,5).

Монитор трафика CCIF (`md_ccif_traffic_monitor_func`) каждые 10 с будил
процессор и писал в журнал 3–5 строк (`[ccci1/cif]total cnt=…`,
`[ccci1/net]ccmni0(…)`). Включается теперь только по
`/sys/module/ccci_hif_all/parameters/traffic_log`; за 2 минуты после
прошивки — 0 строк. Сообщения STP-PSM об ожидании connsys понижены до DBG
(оверлей `psm_core.c`).

Попутно: `ssh bf7` через одно постоянное соединение (ControlMaster на ПК) —
журнал телефона с моими наблюдателями 207 → 32 строки в минуту, новых
входов 0. Ловушка сборки: `OUT` скрипту сборки ядра давать АБСОЛЮТНЫМ путём —
make идёт с `-C` в дерево, относительный путь создаёт пустую сборку внутри
дерева, и make отвечает «modules disabled».

### 2026-09-24 14:30 — Причина зависших перезагрузок: сон при горящем экране

Закономерность по всем перезагрузкам дня: повисли все, перед которыми в той
же загрузке был сон (4 из 4), прошли все без сна (5 из 5). По отдельности
после сна не виснут ни перезапуск phosh, ни остановка модема, ни их пара, ни
остановка NetworkManager — только полное выключение.

Поймано живым: одно постоянное ssh-соединение пережило остановку sshd, и
посреди зависшего выключения было видно — всё ждёт остановки
`bf7-modem-at`, мост стоит в D в `tty_release_struct`, NetworkManager и
wpa_supplicant — в `wait_rcu_gp`, logind — в `down`. В журнале ядра —
**зависание RCU** (`rcu_preempt kthread starved … ->cpu=0`,
`rcu_sched self-detected stall`) через ~25 с после пробуждения. А ещё
раньше, **прямо во время засыпания** — `[CMDQ] SW timeout Pre-dump`: очередь
команд дисплея (путь `primary_ovl_memout`, ovl0 → wdma0) не завершается, и
её обработчик таймаута с огромным дампом регистров держит процессор. Всё,
что ждёт synchronize_rcu, встаёт — выключение первым.

Сон в тех опытах шёл через `rtcwake` при **горящем** экране. С погашенным
заранее экраном: 0 таймаутов CMDQ, 0 зависаний RCU. Обычный путь
(gsd-power по простою) и так засыпает с тёмным экраном, но
`systemctl suspend` при горящем попадал в ошибку.

**Правка:** крюк `/usr/lib/systemd/system-sleep/bf7-ekran-pered-snom`
(hybris/bf7-gpu/bf7-ekran-pered-snom.sh): перед сном, если CRTC активен,
гасит экран программной кнопкой (bf7-knopka) и ждёт до 3 с. Проверено:
экран горит → `systemctl suspend` → крюк гасит (SCREEN OFF), сон, пробуждение
по RTC — 0 таймаутов, 0 зависаний. rtcwake идёт мимо systemd — для опытов
гасить экран заранее. Корень в драйвере дисплея (сон при активном тракте) не
лечил; сторож выключения остаётся на случай иных зависаний.

### 2026-09-24 14:45 — Каждая перезагрузка кончается аппаратным сторожем

Время от команды до пропажи USB (по логгеру консоли на ПК): эталон без сна
— 31 с, после сна без Wi-Fi — 27 с, `systemctl reboot -ff` (без остановки
служб, сразу `reboot()`) — **66 с**. В эталоне journald пишет «Journal
stopped» уже через 2 с после команды. Значит, ядро застревает в
`kernel_restart()` (оповещатели перезагрузки или `device_shutdown`), строка
`reboot: Restarting system` не появляется, а телефон сбрасывает аппаратный
сторож mtk-wdt (31 с). Так было и утром — просто укладывалось в тайм-аут.

Где именно — не установлено: консоль ttyGS0 при выключении молчит, pstore
после сброса сторожем отдаёт устаревшую запись, netconsole в ядре нет (и
`NETPOLL` нет — добавлять ради отладки значит менять сетевые структуры).
Следующий шаг, если понадобится: своя сборка с `initcall_debug` и
печатью имени устройства в `device_shutdown` в регистр, переживающий сброс,
либо netconsole.

Проверено попутно: Wi-Fi не причина (сон с выключенным радио, перезагрузка —
та же картина). Итог дня по перезагрузке: опасный вариант (сон при горящем
экране → CMDQ → RCU, 15–25 минут) закрыт крюком; иные долгие зависания —
сторож выключения (≤ 2 мин); обычная перезагрузка — 30–60 с.

### 2026-09-24 14:58 — Ядро перезагружало плату только сторожем: починено

Опыт: `echo b > /proc/sysrq-trigger` (прямо `machine_restart()`, без
выключения устройств) — USB пропал через 73 с; `reboot -ff` — 66 с;
обычная перезагрузка — 31 с (systemd взводит сторож перед `reboot()`).
То есть `machine_restart()` не перезагружает никогда. На arm64 при наличии
PSCI он зовёт только `psci_sys_reset`, а прошивка платы SYSTEM_RESET не
выполняет; обработчики перезагрузки (`do_kernel_restart`, среди них
`mtk_wdt_restart` — обычный программный сброс MediaTek через `WDT_SWRST`)
не вызывались вовсе. В донорских деревьях код тот же.

Правка: `arch/arm64/kernel/process.c` — сперва `do_kernel_restart()`,
PSCI — запасной путь. После прошивки: USB пропадает через **2,3 с** после
команды (было 31), ssh снова через 30,5 с.

Осталось: после сна выключение иногда встаёт (из двух проб одна — 107 с,
сторож выключения; другая — ~25 с). Ни Wi-Fi, ни CMDQ/RCU при тёмном
экране ни при чём. Запасной sshd с `DefaultDependencies=no` к 20-й секунде
уже убит `systemd-shutdown` — значит, застревание позже остановки служб.
Худший случай теперь — сторож: ≤ 2 минут, телефон встаёт сам.

### 2026-09-24 17:40 — Остаток после сна: порядок остановки не спас, опыт повесил телефон

Серии «сон с тёмным экраном → перезагрузка» (время до пропажи USB):
- как есть: 150,7 / 150,7 / 28,4 с;
- phosh остановлен вручную заранее: 2,1 / 2,1 / 2,1 с;
- вставка «ждать phoc» на bf7-camd/camera/videod/android-hal/modem: 23,9 ×3 —
  у camd и camera `After=bf7-phosh`, ожидание phoc давало взаимную
  блокировку (2×10 с); вставка оставлена только на android-hal/videod/modem;
- с ней: 150,7 / 27,2 / 27,9 с, затем 68 / 67 / 27, 68 ×3, 67 / 68 / 293 с.
Сторож выключения теперь 60 с без дампа (дамп SysRq w при уровне 7 во все
консоли растягивал сброс). В случаях 150 и 293 с сторож, видимо, даже не
запускался — PID1 стоит в самом начале выключения; журнал после сна всегда
обрывается на «System is rebooting». Диск после сна исправен (8 МБ с fsync
115 мс против 127).

Запасной sshd для наблюдения изнутри: в `system.slice` его останавливает
выключение (и `IgnoreOnIsolate` не спасает) — нужен `--slice=-.slice`,
`-o UsePAM=no`.

**Телефон повис целиком около 16:58 — на пробуждении из сна, а не на остановке
phosh** (поправка: скрипт до `systemctl stop bf7-phosh` не дошёл, ssh после
сна так и не ответил). Wi-Fi мёртв полностью (даже ping), USB-сеть жива —
подозреваются мои сегодняшние правки драйвера Wi-Fi, работающие при сне
(ожидание ответа модема; режим сна Wi-Fi работой под `rtnl_lock`). Состояние:
ping по USB жив, sshd:22 нет, консоль ttyGS0 не отзывается, запасной sshd
на 2222 рвёт соединение до обмена ключами (sshd на каждое соединение делает
exec самого себя — похоже, встал запуск программ/доступ к файлам). Сам не
сбросился за 40+ минут. Удалённо не перезагрузить. Опыты со сном — только
когда хозяин рядом.

### 2026-09-24 18:50 — Аппаратный сторож во время работы: не защищает, отключён

Хотел, чтобы при зависании пользовательской части телефон перезагружался
сам (systemd `RuntimeWatchdogSec=30s`, mtk-wdt, предел 31 с).
- Без правки ядра сторож считал и во сне: сон 45 с — перезагрузка во сне.
  Настройку сразу убрал.
- Включил `CONFIG_MEDIATEK_WATCHDOG_PM=y` (драйвер останавливает сторож
  перед сном и запускает после): сон 45 с со сторожем — без сброса.
- Обратная проверка: PID 1 остановлен отладчиком на 90 с — телефон НЕ
  перезагрузился. Защиты от зависаний нет, поэтому настройка systemd снята;
  `CONFIG_MEDIATEK_WATCHDOG_PM=y` в ядре оставлен (безвреден).
Хозяин был дома — страховка кнопкой.

### 2026-09-24 20:10 — Без кабеля телефон не засыпал: UPower не видел отключения

Замер без кабеля (журнал `bf7-batareya-zhurnal`, 60 минут): снов 0 за час,
кулонометр −10,3 ед./мин → **~62 мА** при погашенном экране без сна.
gsd-power настроен верно (на батарее — сон через 600 с, запретов в сеансе
нет), но UPower держал `usb` и `mtk-master-charger` в состоянии online=1,
хотя в sysfs был 0: при подключении/отключении драйверы MediaTek шлют
событие только от `mtk_charger_type` и `battery` (видно `udevadm monitor`),
UPower остальные не перечитывает и считает телефон «от сети».

Правка: `hybris/bf7-gpu/units/79-bf7-pitanie.rules` — при изменении
mtk_charger_type/battery разослать «change» и от usb, зарядников, ac.
Проверено: событие battery тянет за собой события usb, mtk-master/slave,
ac. Проверка автосна без кабеля — следующим отключением.

### 2026-09-24 21:00 — Автосон заработал, но s2idle не экономит; глубокий сон (deep) испытан

Второй замер без кабеля (35 мин, после правила udev для UPower): телефон
засыпал сам (3 сна). Но на самом длинном отрезке сна (454 с) кулонометр
−70 ед. = 7 мА·ч → **~55 мА во сне s2idle** — почти как без сна (62 мА):
s2idle на этой плате не доводит SoC до глубоких состояний SPM.

Глубокий сон (`/sys/power/mem_sleep = deep`, `rtcwake -m mem`) — испытан:
- по будильнику RTC: SPM `sleep_count` растёт, `timer_out` = 684399 такта
  32 кГц = 20,9 с — плата реально спала;
- по кнопке питания (разбудил хозяин): Wi-Fi, мобильные данные, модем,
  Bluetooth, оболочка — живы, интернет через Wi-Fi ходит;
- **входящее SMS будит из глубокого сна**: `Resume caused by IRQ 37,
  CCIF_AP_DATA`, bf7-dosyp зажёг экран «новое SMS». Звонок идёт тем же путём.
Попутно: после одного из автоснов NetworkManager остался «asleep» (сигнал
пробуждения не дошёл), ModemManager потерял модем — крюк
`bf7-posle-sna` будит NM и возвращает модем.

Дальше — замер расхода в глубоком сне без кабеля; при хорошем результате
`SuspendState=mem` в `bf7-sleep.conf`.

### 2026-09-24 22:00 — Самоперезагрузка при зависании: детектор hang_detect MediaTek

В ядре есть `drivers/misc/mediatek/monitor_hang` (hang_detect): поток ядра
(SCHED_FIFO) раз в 30 с уменьшает счётчик, дважды дойдя до нуля —
снимает стеки и перезагружает телефон через AEE (`bootreason=hang_detect`).
Кормят его `ioctl(/dev/RT_Monitor, HANG_KICK = 0x8004700a, тайм-аут_с)`,
минимум 300 с. На Android кормит system_server, у нас — никто, поэтому
днём он то молчал, то срабатывал через 15–25 минут.

`bf7-hang-kick` (служба, /usr/local/sbin/bf7-hang-kick): раз в минуту
проверяет, что система жива (новая программа запускается, systemd
отвечает `systemctl show` за 20 с), и только тогда кормит с 300 с. Не
прошло или кормилец сам застрял — кормёжки нет → перезагрузка. Ручная
остановка службы выключает детектор; при выключении системы он остаётся
взведённым — страховка от зависшего выключения, которое не успел поймать
bf7-straza-vykl.

Проверено: кормилец заморожен (`kill -STOP`) → через 309 с ядро само
перезагрузило телефон, `androidboot.bootreason=hang_detect`, ssh через
~6 минут. Без сети, без кнопки.

### 2026-09-25 08:10–08:55 — Видео в браузере: аппаратно и без копий цвета; DMABuf-рендерер WebKit

**Серые страницы.** Epiphany и MiniBrowser грузили страницу (заголовок есть),
а поле оставалось серым. Камера: с `WEBKIT_DISABLE_DMABUF_RENDERER=1` —
рисуется. Проверено по шагам:
- `eglplatform_wayland.so` от 23.09 не виноват — со старой копией
  (`/root/eglpw-staryj.so`, подмена через `mv`) так же серо;
- GBM-цепочка не менялась: `renderD128` — pvrsrvkm, бэкенд `pvr_gbm.so` от 16.09;
- `WEBKIT_DMABUF_RENDERER_FORCE_SHM=1` — рисует;
  `WEBKIT_DMABUF_RENDERER_DISABLE_GBM=1` — рисует.
Значит, веб-процесс рисует на GPU исправно, ломается именно буфер,
выделенный через GBM (`gbm_bo_create` в веб-процессе → ввоз в окно).
Причина пока не найдена; рабочий режим — `DISABLE_GBM` (текстура
экспортируется через EGLImage, в окно уходит dma-buf, GPU на месте).

**Почему видео ело процессор.** С `DISABLE_DMABUF_RENDERER` WebKit рисует
программно и берёт не GL-приёмник, а `webkitvideosink` с кадрами в памяти:
`vah264dec` (YV12) → `videoconvert` в BGRx (поток `vqueue:src` ~40%) →
главный поток ~63%. Итого 105% на 720p (программный avdec — 156%).
Попутно: `vah264dec` у libva с незнакомым драйвером имеет ранг 0 — без
`GST_PLUGIN_FEATURE_RANK=vah264dec:MAX` playbin берёт avdec.

**GL-приёмник.** В режиме `DISABLE_GBM` WebKit ставит glupload, но GstGL
создавал контекст настольного OpenGL 3 («opengl3 context created but the
required ES2 compatibility was not found») — у нас только GLES.
`GST_GL_API=gles2` чинит.

**Зелёный кадр — две ошибки в нашем VA-драйвере** (`vaapi-shim`):
1. GStreamer (`gst_va_dmabuf_allocator_setup_buffer_full`) вывозит
   поверхность один раз, при заведении пула, и дальше показывает этот
   dma-buf. У нас это запасной буфер gralloc (кадра ещё нет), а кадры
   декодера ложатся в другие буферы — на экран шёл пустой YUV (зелёный).
   mpv и Firefox вывозят каждый кадр заново, поэтому у них работало.
   Теперь: поверхность помнит, что её запасной буфер отдан наружу
   (`own_exported`), и каждый пришедший кадр копируется в него
   (`copy_to_own`, ~1,4 МБ на кадр 720p), а буфер декодера сразу
   возвращается демону.
2. Кадры никто не забирал: при выдаче через dma-buf GStreamer не зовёт ни
   vaSyncSurface, ни vaDeriveImage, а демон отдаёт кадры только по
   просьбе. Сотни картинок уходили, ни одна поверхность не становилась
   готовой. Теперь для таких поверхностей `vaEndPicture` сам дожидается
   кадра (`ensure_decoded`).

Итог на `bf7-test-720p.mp4` (снимок grim: полосы SMPTE, цвета верные,
камера: движется): WebKitWebProcess 66%, epiphany 28%, bf7-videod 15%,
phoc 16%. Было 105/17/15/12 (VA + videoconvert) и 156/17/0/12 (avdec).
Из 66% — 55% поток ThreadedCompositor; разбираю perf.

### 2026-09-25 09:35 — Серые страницы найдены: нет eglCreateSync через glvnd

Продолжение записи выше. Профиль (perf заработал: `-B --no-buildid-cache`,
а для `perf report` — `--symfs` с каталогом из ссылок на /usr, /vendor,
/system… без /dev: иначе perf пытается читать как ELF отображённые
устройства `/dev/pvrsrvkm`, `/dev/dri/*` и виснет) показал, что в режиме
`DISABLE_GBM` 88% потока ThreadedCompositor — цикл перестановки байтов
BGRA↔RGBA в `libGLESv2_mtk.so`, вызванный из `glReadPixels`. То есть и
`DISABLE_GBM`, и `FORCE_SHM`, и `DISABLE_DMABUF_RENDERER` — это общая
память: GPU рисует, кадр читается обратно каждый раз.

Пробник `hybris/bf7-gpu/gbmprobe.c` доказал, что сам путь GBM исправен:
`gbm_bo_create` (pvr, шаг 2944 для 720) → ввоз в EGL → FBO → заливка на
GPU → байты в dma-buf верные; со стороны окна (дисплей Wayland) ввоз того
же буфера и чтение через FBO — тоже. gdb в UI-процессе MiniBrowser:
`eglCreateImage(EGL_LINUX_DMA_BUF_EXT)` вызван ОДИН раз (2048×1460, AR24) и
успешно — дальше кадров нет. В трассе libhybris:
`getProcAddress("eglCreateSync") -> NULL` (24 раза). Дисплей объявляет
EGL 1.5, WebKit зовёт `eglCreateSync` ядра, glvnd ищет его у поставщика и
получает NULL → fence кадра не создаётся → обмен кадрами встаёт после
первого, пустого.

Починка в libhybris (`egl.c`): пять функций синхронизации EGL 1.5 —
переходники к `eglCreateSyncKHR` и соседям драйвера. Проверено камерой:
example.com и ru.wikipedia.org рисуются на штатном DMABuf-рендерере, без
переменных. `bf7-web` подпорку больше не ставит.

Замеры, ролик 720p в Epiphany (20 с):

| путь | WebProcess | epiphany | videod | phoc | весь телефон |
|---|---|---|---|---|---|
| avdec, общая память | 156% | 17% | 0 | 12% | ~185% |
| VA + videoconvert, общая память | 105% | 17% | 15% | 12% | ~150% |
| VA → dma-buf → GL, общая память (DISABLE_GBM) | 66% | 28% | 15% | 16% | 135% |
| VA → dma-buf → GL, DMABuf-рендерер | **22%** | 44% | 17% | 5% | **104%** |

Прокрутка Википедии (bf7-kadry, взмах): было 24–38 кадров/с, стало 35–48,
интервалы ровные ~18–20 мс.

Остаток — UI-процесс (44%): GTK3 рисует GL-содержимое в не-GL окно через
программный запасной путь `gdk_cairo_draw_from_gl` (glReadPixels + memcpy).
Перехватчик показал, что условия быстрого пути по преобразованию и
отсечению выполнены, но у окна нет GL-контекста рисования (`use_gl`), и
`GDK_GL=gles,always` его не даёт (замер без изменений). Даже с ним GTK3
заливал бы свою cairo-поверхность в текстуру каждый кадр. Это предел GTK3;
Epiphany 43 в Debian 12 — на GTK3.

Попутно: apt был сломан устаревшим `libmm-glib-dev` (+bf7.3 при MM +bf7.7) —
поставлен +bf7.7 из /root/src, hold.

### 2026-09-25 10:25 — mpv: аппаратное декодирование по умолчанию, окно больше не обрезано

mpv по умолчанию декодировал программно: 720p — mpv 116%, телефон 130%.
`/etc/mpv/mpv.conf` → `hwdec=auto-copy` (vaapi-copy через наш драйвер):
mpv 38%, телефон 70%. Без копии mpv кадр взять не может (граница PowerVR,
см. bf7-hw-accel-for-apps).

Второе: видео было обрезано — окно 1280×720 при масштабе 2, шире экрана.
`WAYLAND_DEBUG=1`: phoc шлёт `xdg_toplevel.configure(360, 734, maximized)`
дважды; mpv 0.35.1 первое отбрасывает (геометрия ещё не задана), но размер
запоминает, второе с тем же размером считает «без изменений». Заплатка
`mpv-0.35-wayland-initial-configure.patch`, пакет `0.35.1-4+bf7.1` (hold).
Проверено: «Resizing due to xdg from 1280x720 to 720x1468», камера — кадр
целиком, по центру.

### 2026-09-25 10:40 — Зависшая перезагрузка после сна: камера держала общую шину через сон

Пункт доводки «перезагрузка после сна иногда ждёт сторожа». Воспроизвелось
3 из 3: сон по будильнику RTC → проснуться → `systemctl reboot` — 94–95 с до
ssh (обычная перезагрузка 30–31 с), журнал обрывается на «System is
rebooting». Пауза после пробуждения значения не имеет (проверено 51 с).

Консоль ядра из ramoops (сохранилась после сброса hang_detect): при
выключении завершается bf7-seninf-up, `ISP_release → ISP_EnableClock`
гасит такты, которые драйвер ISP уже погасил во сне — «cam_larb2 already
disabled/unprepared» через `mtk_smi_clk_disable`/`smi_bus_disable_unprepare`;
одновременно очередь команд дисплея стоит: «CMDQ SW timeout… Wait And Clear
Event: CMDQ_EVENT_DISP_WDMA0_EOF». Шина SMI общая у камеры и дисплея.

Проверка: камера остановлена до сна → перезагрузка после сна 30 с.
Починка: `Conflicts=sleep.target` + `Before=sleep.target` у bf7-camera и
bf7-camd (systemd сам гасит камеру перед сном), крюк bf7-posle-sna
поднимает bf7-camd через 3 с (`systemd-run --on-active=3`: пока идёт
systemd-suspend, запуск службы с конфликтом отвергается как «destructive»).
Итог: камера после сна снова отдаёт кадры (ffmpeg с /dev/video0), перезагрузка
после сна — 31 с. Самопроверка: 83/0. Заодно матрица и её питание во сне
выключены — должно снять часть «долга» 50 мА (замерить без кабеля).

Попутно (мой промах): для отладки ставил `dmesg -n 8` и SysRq w в момент
зависшего выключения — вывод в консоль на спящую панель подвесил и сторожа
bf7-straza-vykl; спас только hang_detect (сброс через 6,5 мин). Так не
делать: для дампа — консоль ядра остаётся на уровне 3, а ramoops сохраняет
её после сброса hang_detect. USB-консоль ttyGS0 (`/dev/ttyACM0` на ПК)
вывода ядра не даёт.

### 2026-09-25 10:45 — VP9 и HEVC через демон: первые пробы

`bf7-vtest` научен IVF (VP9/VP8, кадр целиком на посылку) и H.265 (по
расширению .hevc/.h265; VPS/SPS/PPS — одной посылкой настройки).

- **HEVC 1280×720 4:2:0: 119 кадров, картинка есть** — железо работает.
  Первый прогон дал 0 кадров: тестовый ролик из `testsrc` без `-pix_fmt`
  вышел 4:4:4 («only support 420 format, this format is 3»). Урок: к
  testsrc всегда `-pix_fmt yuv420p`. При наборах параметров порознь
  компонент получал первым срез («invalid NALU 0x2 0x1») — нужна ли склейка
  на 4:2:0, ещё проверить.
- **VP9: 0 кадров**, `libvp9dec_sa.ca7.so` «NOT_SUPPORT video» — проба была
  тоже на 4:4:4, вывод «VP9 программный» требует перепроверки на 4:2:0.

Главный риск драйвера HEVC: VA не передаёт таблицу st_ref_pic_set из SPS, а
заголовки срезов ссылаются на неё по индексу — синтезированному SPS её не
восстановить; путь — переписывать заголовок среза на явный RPS.

### 2026-09-25 11:35 — H.265 (HEVC) аппаратно через VA-API; VP9 у MT6761 программный

**VP9 перепроверен на 4:2:0:** демон декодирует (118 кадров), но
`libvp9dec_sa.ca7.so` — программный декодер MediaTek: 3,1 с процессора на
120 кадров 720p против 2,6 с у ffmpeg. Выигрыша нет, в драйвер не добавлен.

**HEVC Main — в драйвере** (`vaapi-shim/src/h265.cpp`, перенос из
libva-v4l2 `src/hevc.cpp`, MIT). SPS без таблицы st_ref_pic_set, в каждом
заголовке среза набор опорных кадров переписывается явным из ReferenceFrames
VA, списки опорных — через модификацию списков. Наши правки: уровень по
размеру кадра (не 6.2), один подслой, sps_max_num_reorder_pics = 0,
VPS/SPS/PPS одной посылкой настройки. Main10 не объявлен — компонент его не
открывает.

**Особенность декодера MediaTek HEVC: всегда держит один кадр** до следующей
картинки (у H.264 такого нет). Не помогли: reorder 0, один подслой,
drain NO_EOS на каждой картинке, режим видеозвонка `mtk.ext.vdec.vilte`
(0x4200e007 — принят, эффекта нет), разделитель AUD после картинки. Поэтому:
- ffmpeg/mpv (просят кадры в порядке показа) — задержка не мешает; в конце
  файла удержанный кадр выпускается концом потока (AUD + EOS), когда в
  декодере осталась ровно одна картинка (`flush_held`);
- после EOS декодер новых картинок не берёт — следующая картинка переоткрывает
  его (повторный OPEN), а кадры старого предварительно копируются в свои
  буферы поверхностей (`preserve_frames`): пул выходных буферов переходит
  новому экземпляру, и тот писал поверх ещё не забранных кадров;
- GStreamer (ждёт каждый кадр сразу) — ранг vah265dec НЕ поднят: браузер для
  HEVC остаётся на программном декодере.

Проверка (ffmpeg `-hwaccel vaapi … hwdownload` против программного,
framemd5): HEVC 720p с B-кадрами, 720p без B, 1080p, повтор ×3 (351 кадр) —
побитово; H.264 и повтор H.264 — побитово. mpv, HEVC 1080p: программно 401%
(все ядра, не успевает), аппаратно 115% на весь телефон; камера — кадр
целиком.

### 2026-09-25 11:55 — Видоискатель камеры мигал старыми/чёрными кадрами через один

Хозяин заметил в «Камере» полосы и старые кадры. Статичная тёмная сцена этого
не показывала; видно стало при смене выдержки командой демону
(`echo "выдержка 600" > /run/bf7-camd`) и замере средней яркости сырых
кадров /dev/video0: 1 8 1 8 1 8… — каждый второй кадр чужой.

Причина — чередование двух буферов в bf7-camd: после первого переключения
блок захвата пишет только во второй буфер, новый адрес выгрузки (IMGO_BASE)
не подхватывает (отладочная сумма буфера 0 всё время 00000000; перевзводы
1, 2, 4 и «адрес до сброса» не помогли, 2 даёт массу пустых кадров).
Некэшируемые буферы (`cached=0`) «лечили» только замедлением в 5 раз.
Метку конца кадра в буфере железо не затирает (последние байты не пишутся),
так что выбор по метке не работает.

Починка: один буфер (`swap=0` по умолчанию) и копия кадра ДО перевзвода —
дописав кадр, блок захвата стоит до перевзвода, так что копия (~5 мс)
целая и без разрыва, а следующий кадр пишется в тот же буфер уже после.
Проверка: выдержки 600/1500/3000/авто — ни одного чередования; частота
свежих кадров та же, что у прежнего демона (в тёмной сцене ~4–5 в секунду:
автоматика растягивает длину кадра матрицы — это не поломка).

### 2026-09-25 12:05 — Видоискатель в темноте: ~28 кадров/с вместо ~5

После починки буферов стало видно, что в тёмной сцене свежий кадр приходит
раз в ~6 выдач. Причина — длина кадра матрицы (0x0340): автоматика в темноте
растягивала её до 19560 строк, а это делит такт. Замер свежих кадров из 60
при ручной длине: 3260 — 59, 6520 — 43, 9780 — 27, 19560 — 13. Прежний
вывод «такт не меняется» (комментарий в коде) был снят при сломанном
чередовании буферов, где старые кадры считались новыми, — исправлен.

Теперь видоискатель растягивает кадр не дальше 6520 (`vflen=`), а недобор
света копит в `ae_extra` — виртуальную длину, которую получает снимок (для
фото кадр и так поднимается до 19560, свет переносится на выдержку). Итог в
той же тёмной сцене: 56 свежих из 60 (~28 кадров/с), яркость видоискателя
почти та же (2,7 против 2,9 — дальняя выдержка почти не прибавляла); фото:
выдержка 19548, усиление 0x225, как прежде. Состояние хранит flen×ae_extra.

### 2026-09-25 12:30 — Косметика оболочки: эмодзи, имя оператора, переводы

Обход экранов снимками grim:
- **эмодзи** в SMS хозяина (🤔) — пустые квадраты: не было цветного шрифта.
  Поставлен `fonts-noto-color-emoji`; уведомление с 🤔😁👍 — цветные;
- **«250xx» под значком связи**: сеть второго оператора имени не шлёт, MM отдавал
  код. Заплатка MM (`+bf7.8`): если имя — одни цифры и совпадает с кодом
  SIM, берётся имя с SIM (SPN «OPERATOR2»). Данные после перезапуска MM
  поднялись сами;
- **«Мобильные настройки»** были целиком по-английски — в пакете 0.24.1
  перевода нет; поставлен ru.po из исходников проекта (домен
  `mobile-settings`, найден strace);
- **«Notifications»** в шторке — строка в settings.ui не помечена
  переводимой; перевод в phosh.mo + заплатка phosh (`+bf7.4`).
Самопроверка: раздел «Вид» (эмодзи, имя оператора) — 87/0.
- 12:50: phosh `0.24.0-1+bf7.4` поставлен (сборка с тестами падала на 5
  тестах, которым нужен дисплей — favourite-model, app-grid-button,
  notification*; не связаны с правкой; пакет собран с
  `DEB_BUILD_OPTIONS=nocheck`); в шторке «Уведомления». MM `+bf7.9` —
  «+» к международным номерам (тип 145) из +CLCC/+CLIP; тесты MM 30/30.
  «Чаты»: метки «9 минут назад», «Только что», «Вчера». Сетка: «Книги».

### 2026-09-25 13:00 — Шрифты: интерфейс был сжатым, запуск приложений медленнее

perf холодного запуска gnome-calculator: 24% — libfontconfig (компилятора
шейдеров в профиле нет вовсе). Установлено 1727 шрифтов, из них 1540 —
`fonts-noto-extra` (зависимость retroarch-assets). Обычного Noto Sans в нём
нет (он в fonts-noto-core), и шрифт интерфейса Cantarell тоже не стоял —
fontconfig подставлял «Noto Sans SemiCondensed»: весь интерфейс был сжатым.

Поставлен `fonts-cantarell`, `fonts/70-bf7-noto-extra.conf` прячет
fonts-noto-extra от fontconfig, кроме цветных эмодзи и моноширинного Noto
(RetroArch грузит свои шрифты по пути — ему не мешает). Шрифтов 226.
Запуск калькулятора: 2,27 с → 1,85 с (три прогона). Снимками: сетка,
шторка, «Мобильные настройки», «Вызовы» — без обрезаний. Экранная
клавиатура — русская раскладка первой (dconf).

### 2026-09-25 13:10 — libhybris: glvnd тратил 14% запуска приложения на поиск несуществующих функций

После шрифтов в профиле запуска gnome-calculator 14% — `q.so` (компоновщик
libhybris), цикл `walk_dependencies_tree` в `dlsym_handle_lookup`. glvnd на
новый контекст спрашивает у поставщика тысячи имён (3401, из них 2789 — нет
в драйвере), и каждое `android_dlsym` обходит всё дерево зависимостей
libGLESv2_mtk. Теперь список gl* самой библиотеки читается из её .dynsym
один раз, отсутствующее имя — NULL сразу. Процессорное время первых 3 с
запуска: 1780 → 1570 мс (по три прогона, старая и новая библиотека
поочерёдно); время до окна — около 1,9 с (упирается не в процессор).
glmark2 385 — без изменений.

### 2026-09-25 13:30 — GTK4 рисует программно; GL-путь GTK4 на нашем стеке хуже

Попутно при пробе Clapper: у **всех** приложений GTK4 GL-отрисовщик не
поднимается — «Failed to realize renderer GskGLRenderer … #version 150 not
supported». GTK4 не читает `GDK_GL=gles` (это GTK3), берёт «настольный» GL,
контекст у нас всё равно GLES, шейдеры GLSL 150 PowerVR не компилирует — и
GTK4 молча уходит в Cairo. С `GDK_DEBUG=gl-gles` GL поднимается, НО на деле
хуже: gtk4-widget-factory, 6 взмахов — Cairo 60 к/с (16 из 340 кадров
дольше 25 мс), GLES 30 к/с (127 из 178), процессора больше (3,99 с против
3,62). Оставлен Cairo — ничего не менял.

Clapper (плеер GTK4/GStreamer, сенсорный) без GL видео не рисует (просвечивают
обои); с gl-gles кадр декодера идёт в GPU без копий (H.264 1080p — 34%
процессора против 98% у mpv), но окно почти не обновляется: 2 фиксации
композитора за 6 с при идущем декодировании. HEVC в GStreamer через наш
VA — зелёные кадры (декодер держит кадр, GStreamer не ждёт), попытка
забирать готовые в EndPicture — картинка стоит; откачено. Clapper снят,
по умолчанию mpv.

Открыто: почему GTK4 на GL/EGL-Wayland через libhybris идёт 30 к/с и
застревает (вероятно, ожидание release буфера / frame callback в
eglplatform_wayland при двух буферах).
- 13:40, продолжение: GTK4 на GLES упирается в процессор в драйвере —
  perf прокрутки gtk4-widget-factory: libusc.so 47% + libglslcompiler 8%
  (компилятор шейдеров PowerVR); сама GTK за всё время связала лишь 48
  программ (gdb: glLinkProgram 48, glCompileShader 96, glUseProgram 2559) —
  перекомпилирует варианты драйвер. Интервал обмена ни при чём (0 и 1 —
  одинаково 30 к/с). GTK4 остаётся на Cairo.

### 2026-09-25 13:50 — Кэш шейдеров драйвера (EGL_ANDROID_blob_cache) в libhybris

Продолжение: при прокрутке GTK4 на GLES 55% процессора — компилятор шейдеров
PowerVR, хотя программ всего 48. На Android драйверу выдаёт кэш libEGL
системы (`eglSetBlobCacheFuncsANDROID`, в libIMGegl есть); libhybris не
выдавал. Сделан кэш: память + журнал на диске на каждую программу.
gtk4-widget-factory, 6 взмахов, GLES: 4570 → 1630 мс процессора (второй
запуск, кэш 480 КБ). phoc: 2,1 → 1,97 с на 16 жестов. Браузер: ±1,5%
(10,9 с на 6 взмахов — упирается не в шейдеры).
GTK4-GLES всё равно 30 к/с: главный поток ждёт `sync_wait` в
`WaylandNativeWindow::queueBuffer` — libhybris синхронно ждёт GPU на каждом
кадре, CPU и GPU идут по очереди. Лечится отдельным потоком фиксации
(ожидание fence и commit вне eglSwapBuffers) — отложено; GTK4 остаётся на
Cairo (60 к/с).
Ловушка замеров: `__EGL_VENDOR_LIBRARY_FILENAMES` с одним hybris выкидывает
Mesa, и WebKit падает (Aborted) — сравнивать через `__EGL_VENDOR_LIBRARY_DIRS`
с обоими поставщиками.

### 2026-09-25 13:55 — приложения «Файлы» и «Музыка», русские папки, уборка /root

- Поставлены мобильные Portfolio (файловый менеджер, `dev.tchx84.Portfolio`)
  и Lollypop (музыка). У Portfolio перевода на русский не было — написан
  `hybris/bf7-gpu/perevody/portfolio-ru.po` (ставит `bf7-install.sh`), в
  сетке приложений он теперь «Файлы» (`bf7-phosh-skryt.sh`).
- Папки пользователя переведены: `xdg-user-dirs-update` с `ru_RU`, старый
  файл — `~/.config/user-dirs.dirs.en`. Снимки камеры теперь в
  `/root/Изображения`.
- В `/root` лежало ~220 файлов. Пробные ролики, сырые кадры и опытные копии
  библиотек (30 шт.) — в `/root/bf7-proby` (`web-video*.sh` смотрят туда);
  старые журналы и заметки (54 шт.) — в `/root/bf7-arhiv`. Проверено grep'ом:
  службы и сценарии эти файлы только пишут, не читают. Файлы, которые читает
  система (`bf7-batareya.log`, `hybris-backup-latest` и т. п.), оставлены.

### 2026-09-25 14:05 — сколько стоит звук: пересчёт частоты и запись в SRAM

Замер PulseAudio, пока играет paplay (тики процессора за ~15 с): поток
44,1 кГц — 241, 48 кГц — 145, тишина — 23. Значит, пересчёт частоты стоит
~8% ядра, а сам вывод без пересчёта ~7%.

perf со стеками (48 кГц, 723 выборки): 236 из них (33%) приходятся на
`__memcpy_toio` ← `mtk_pcm_copy` ← `__snd_pcm_lib_xfer`. Буфер DL1 лежит во
внутренней SRAM звукового блока, а она отображена `of_iomap` как память
устройства (Device-nGnRE): каждая 8-байтовая запись идёт отдельной
транзакцией. Выходит ~6 МБ/с на поток 192 КБ/с, ~3% ядра всё время, пока
играет звук. Копирование идёт без запрета прерываний; посредник kmalloc
ничего не стоит.

### 2026-09-25 14:20 — музыка без пересчёта частоты; DL1 в DRAM (ядро v4)

- DL1 принимает 44100 напрямую: тон 1000 Гц при 44,1 кГц, записанный
  микрофоном на 48 кГц, даёт пик ровно 1000,0 Гц. В
  `units/bf7-pulse-daemon.conf` стоят `alternate-sample-rate = 44100` и
  `avoid-resampling = yes`. PulseAudio на потоке 44,1 кГц тратит теперь
  155 тиков за 16 с вместо 241 (выход сам переключается на 44100). Проверка
  добавлена в bf7-selftest. После перезапуска bf7-pulse phosh и
  gsd-media-keys переподключились сами, громкость сохранилась.
- Ядро: `mtk-soc-pcm-dl1.c` — буфер воспроизведения в DRAM
  (`dma_alloc_coherent`, Normal-NC) вместо SRAM, отображённой как память
  устройства. Переключатель на лету:
  `/sys/module/mtk_soc_pcm_dl1/parameters/bf7_dl1_dram` (1 — DRAM,
  действует со следующего открытия устройства). В `bf7_copy_from_user_io`
  добавлен `wmb()` после копирования. Собирается как v4 поверх v3 (там уже
  асинхронный тач), прошивать вечером при хозяине. Запасной образ v3 лежит в
  `build/zapas-v3-2026-09-25`. Проверка после прошивки: тот же замер тиков
  PulseAudio на 48 кГц (было 145 за 15 с), тон через микрофон, dmesg без
  AudioWarn посреди потока.

### 2026-09-25 14:20 — load average 14: потоки ядра в TASK_IDLE (ядро v4)

13 потоков ядра MediaTek всё время спали в TASK_UNINTERRUPTIBLE, процессор
при этом свободен. Все 13 проверены: родитель kthreadd, ни один не
замораживается. Правка по месту ожидания (коммит ab9e356a4):
`msleep` → `schedule_timeout_idle`, `wait_event` → `wait_event_idle`, общий
макрос ожидания состояния дисплея → `wait_event_idle_timeout`.
`wait_for_completion_killable` в tee_irq_bh → `_interruptible`: у потоков
ядра сигналов нет. В `sspm_ipi_recv_wait` (её зовут и из обычного контекста)
`_interruptible` берётся только при `PF_KTHREAD`. `request_send` вызывают и
пользовательские процессы, поэтому там именно `schedule_timeout_idle`:
`msleep_interruptible` при ожидающем сигнале крутился бы вхолостую.
`ddp_irq_log_kthread` больше не создаётся — MediaTek его и не запускал.
Ждёт прошивки вместе с остальным v4.

### 2026-09-25 14:57 — GTK4 рисует через GPU: ранняя фиксация кадра и общий кэш шейдеров

Почему GTK4 на GLES шёл 45 к/с с рывками. Первое подозрение — `sync_wait` в
`WaylandNativeWindow::queueBuffer`: без него (опытный переключатель) ровно
60 к/с. Поток фиксации, который ждёт забор вне `eglSwapBuffers`, дал лишь
43–48: главный поток в finishSwap не ждал вовсе (замер
`BF7_COMMIT_TRACE=1`: 0 мс), но GTK4 ведёт кадры по своему frame callback, а
тот приходит только после фиксации, то есть на ~15 мс работы GPU позже.
Нужна именно ранняя фиксация.

Безопасна ли она: `hybris/bf7-gpu/fencetest.c` — кадр по 400 мс GPU, пять
уровней серого на три буфера, серии снимков grim. Без ожидания забора ни
одного недорисованного и ни одного устаревшего кадра: PowerVR ставит
чтение phoc (GPU) после записи клиента в тот же буфер. Первая версия теста
складывала слои сложением — в 8-битном буфере прибавка <1/255 пропадала,
окно было чёрным; и три уровня на три буфера не отличили бы устаревший
буфер — отсюда пять.

Итог в libhybris (`egl/platforms/wayland/wayland_window.*`):
- окно меньше панели фиксируется сразу, забор закрывается без ожидания;
- окно ровно в размер режима панели (`/sys/class/drm/*/modes`) может уйти
  прямым показом, где контроллер дисплея забор не ждёт, — такой кадр
  фиксирует поток фиксации после сигнала забора;
- общий `readQueue` отпускает замок окна на время ожидания сокета
  (prepare_read/read_events), иначе поток и dequeueBuffer запирали бы друг
  друга; счётчик `m_queueReads` сохранён для побудки из cancelBuffer;
- `BF7_SYNC_COMMIT=1` — прежнее поведение.

Кэш шейдеров (`egl/egl.c`): первый запуск каждой GTK4-программы на GLES был
6,3 с вместо 1,8 (компиляция тех же шейдеров GSK заново). Теперь есть общий
журнал `~/.cache/bf7-egl/obshchij.blob`: новые записи дописываются и туда
(flock, один write), программа без своего журнала начинает с его копии.
«Контакты» с чистого листа — 1,0 с.

Замер gtk4-widget-factory, 6 взмахов: GLES 333 тика процессора, Cairo 465
(−28%), phoc 99 против 112 (−12%), оба 60 к/с без длинных кадров.
`GDK_DEBUG=gl-gles` включён в `bf7-phosh.service`, сеанс перезапущен.
Проверены на GLES: часы, контакты, калькулятор, мобильные настройки,
терминал kgx; vkcube на новом общем readQueue. Megapixels на телефоне не
работает вовсе (нет конфига MT6761) — «Камера» у нас своя (bf7-kamera, GTK3), её флаг не касается.

### 2026-09-25 15:15 — сейсмограф (идея хозяина из списка «прикольчиков»)

Акселерометр нашёлся не в дереве устройств: на Android им правит сопроцессор
SCP (в `scp_a` вшиты драйверы sc7a20 и mxc4005x, в заводском ядре —
accelhub через IPI). Но шина общая: на i2c-1 по адресу 0x18 отвечает sc7a20
(WHO_AM_I = 0x11), и процессор читает его напрямую через `/dev/i2c-1`, без
ядра и перепрошивки. SCP держит датчик выключенным (CTRL_REG1 = 0x07) —
служба при выходе возвращает так же.

Две особенности экземпляра:
- только 8 бит (младший байт всегда 0 при любых HR/FS/ODR), шаг 16 мg;
  покой даёт 8–12 мg скз фона;
- после 0x2d адрес не заворачивается на 0x28, поэтому длинное чтение FIFO
  отдаёт мусор. Каждый отсчёт — своя 6-байтовая транзакция (до 16 за вызов
  I2C_RDWR).

`bf7-seismograf` (служба): 50 Гц, FIFO раз в 500 мс, гравитация убирается
медленным средним, раз в секунду в `/var/lib/bf7-seismograf/ДАТА.csv` пишет
скз и пик. Первые 3 с после старта или сна не пишутся (был ложный «толчок»
в 334 мg). Цена: 0,37% ядра и ~54 пробуждения в секунду (одно на отсчёт);
на 100 Гц было 110. Проверено вибромотором: импульс 0,4 с даёт 60–80 мg
при фоне ~22.

`bf7-seismograf-grafik` рисует ночь как барабан сейсмографа: строка на
час, пропуски сна штрихуются, внизу пять сильнейших толчков. Ярлык
«Сейсмограф» со своим значком открывает картинку в eog; она же ложится в
«Изображения/Сейсмограф». Проверка есть в bf7-selftest (91/0).

### 2026-09-25 15:20 — вибромелодия звонка; звонок и будильник до сих пор НЕ вибрировали

`bf7-vibrad` объявлял FF_PERIODIC, но ни одной формы волны. ff-core
принимает периодический эффект, только если его форма (у feedbackd —
FF_SINE) объявлена устройством, иначе загрузка — EINVAL. Поэтому звонок и
будильник не вибрировали вовсе, работали лишь короткие отклики FF_RUMBLE.
Найдено пробником `ffproba` (эффект как у feedbackd плюс снятие
`/sys/class/leds/vibrator/state` каждые 10 мс).

Теперь (feedbackd 0.0.3 узоров не умеет, поэтому ритм играет мост):
- звонок (FF_PERIODIC ≥ 5 с без настоящего нарастания) отбивает ритм из
  `/etc/bf7/vibro-melodiya`: betkhoven (по умолчанию, мотив Пятой
  симфонии), sos, serdce, rovno (сплошная);
- будильник (нарастание с attack_level < magnitude: 4096 против 32767)
  даёт импульсы по 250 мс, паузы сжимаются с 1,5 с до 0,1 с за время
  нарастания. У звонка feedbackd тоже ставит attack_length = 10000, но с
  полным уровнем — это видно по `BF7_VIBRAD_TRACE=1`;
- «стоп» и новый эффект отменяют прежний поток (счётчик поколений); раньше
  поток доигрывал.
Проверено `fbcli` на обоих событиях. Ловушки fbcli: stdin должен быть открыт
и пуст (`sleep N | fbcli`, /dev/zero он читает как Enter); `-P` меняет
профиль для всего телефона. После перезапуска моста перезапускать и
feedbackd (`pkill -x feedbackd`, D-Bus поднимет заново): у него остаётся
дескриптор прежнего устройства.

### 2026-09-25 15:40 — автоповорот экрана от акселерометра

Датчиком владеет одна служба — `bf7-seismograf`. Раз в секунду она по
медленному среднему (вектор силы тяжести) определяет normal / right-up /
bottom-up / left-up: гистерезис 10° у границы сектора, плашмя ориентация не
меняется. При смене пишет в `/run/bf7-akselerometr/orientaciya`.
`bf7-sensor-proxy` (Python/Gio, системная шина) отвечает за
`net.hadess.SensorProxy` вместо iio-sensor-proxy (тот замаскирован: в IIO
датчиков нет). phosh подхватил появившийся сервис без перезапуска.
`monitor-sensor` видит акселерометр. Имитация записью в файл: right-up →
wlr-randr Transform 270, normal → normal. Ручной поворот
(`wlr-randr --transform 90`) phoc выполняет, phosh перестраивается в
альбомную раскладку.

Не проверено (нужен хозяин с телефоном в руках): знак оси X (если экран
поворачивается не в ту сторону — строка «x -1» в `/etc/bf7/akselerometr` и
перезапуск bf7-seismograf) и попадание касаний в альбомном режиме.
Самотест 93/0.

### 2026-09-25 15:45 — что за второй задний «глазок» и остальные датчики

По строкам заводского ядра (boot.img v2366): драйверы камер только
s5k3l6/hi1336 (зад, 13 Мп) и hi556/gc05a2 (перед). Второй задний глазок
стоит на своём шлейфе (хозяин разбирал телефон), но это не камера: вместо
main2 — драйвер Transsion `main2_light` / `aux_psensor` (класс устройства с
show/store значения), то есть вспомогательный датчик света для камеры.
Проба «фонарик рядом вкл/выкл» каналы АЦП (iio:device0) не сдвинула — как
его читать напрямую, не найдено.

Шина i2c-1 целиком: 0x18 — sc7a20 (акселерометр), 0x30 — MMC5603
(магнитометр, ID 0x10, замер идёт), 0x41 — mn78911 (свет и приближение).
Всеми на Android правит SCP. mn78911 по карте EPL259x не запускается;
последовательность запуска достаю из прошивки SCP (дамп
`firmware-backup/scp/scp_a.img`).

### 2026-09-25 16:00 — свет и приближение (mn78911) заработали

Последовательность вынута из прошивки SCP (разбор — `mn78911-iz-scp.md`).
`bf7-sensor-proxy` теперь отдаёт HasAmbientLight/LightLevel и
HasProximity/ProximityNear. Чип работает, только пока есть заявки: режим 0x81
(свет), 0x82 (приближение), 0x83 (оба); без заявок 02=01. Свет с первой
минуты держит gsd-power, хотя автояркость у него выключена
(`ambient-enabled false`) — подсветку он не трогает. Приближение
затребует phosh на время звонка. `monitor-sensor`: 133,5 лк, near 0. Режимы
переключаются по заявкам. Проверить при хозяине: ладонь у верха экрана
(near 1), экран гаснет у уха во время звонка, люксы при свете и в темноте.
Самотест 93/0. Заодно `#fake dual camera` в заводском init.mt6761.rc над
`main2_light` подтверждает: второй задний глазок — «ненастоящая двойная
камера».
- 16:00: проверка света по совету хозяина — гамма обоих мониторов ПК
  (xrandr --brightness 1.0 → 0.15) и белое поле на самом телефоне при
  подсветке 255 и 20: ch1 424–431, ch0 ~705, реакции нет. Чип меряет (числа
  гуляют на ±5), но света не видит. Версии: датчик у разговорного динамика
  закрыт держателем (сверху на снимках камеры чёрные зажимы) или неверно
  понято усиление. Вечером: снять с держателя, палец и фонарик.
- 16:30: зато по времени суток датчик похоже живой: ch0/ch1 15:30 — 730/443,
  16:00 — 705/426, 16:28 — 577/391 (вечереет). Запущена запись раз в 5 мин
  (`bf7-lux-zhurnal` → /var/log/bf7-lux.log); после заката станет ясно.
  Остановить: `systemctl stop bf7-lux-zhurnal`.

### 2026-09-25 16:33 — шум в журнале от светодиода уведомлений

За три часа ~22 тысячи строк `leds_mt6357 … set ISINK_DIM1_DUTY`: с 14:20 до
15:00, пока висели непрочитанные SMS хозяина, feedbackd мигал синим через
ledtrig-pattern, ядро меняло яркость шагами по 50 мс, а драйвер печатал
каждую ступень (11 тысяч строк за 10 минут). В ядре v4 печать регистров
ISINK переведена в pr_debug (коммит 5f5a6c240), ядро и модули связности
пересобраны, сверка CRC чистая. Пробуждения от самого узора остаются —
решение после проверки, виден ли огонёк без кабеля (в списке на вечер).
- 17:52: с 17:03 до 17:48 значения стояли ровно на 355/306 — gsd-power
  отпускает датчик света, когда экран гаснет (ReleaseLight 17:02), и
  bf7-sensor-proxy останавливает чип (02=01): для батареи правильно, а
  самописец читал замороженные регистры. Самописец переделан
  (/root/bf7-lux-zhurnal.sh): при стоящем чипе запускает замер на 1,5 с.
  Ход по времени: ch0/ch1 15:30 — 730/443, 16:28 — 577/391, 16:58 —
  521/381, 17:52 — 189/230. Падает вместе с сумерками — датчик, похоже,
  видит свет; опыты с монитором и своим экраном просто не светили в окошко.
- 17:55: сон freeze на 20 с (rtcwake, тёмный экран): регистры mn78911
  (00/01/40) и sc7a20 (20/23) после пробуждения те же; сейсмограф пишет
  дальше с пропуском ровно на сон; bf7-sensor-proxy жив, ориентация на
  месте. Разбудил RTC (IRQ 334).

### 2026-09-25 18:04 — ядро v4, затем v5 прошиты (хозяин разрешил без него)

`tools/bf7-proshit.sh` оба раза прошёл целиком: сверка CRC модулей, fastboot,
загрузка за ~35 с, Wi-Fi и Bluetooth, самотест 93/0.
- Тач сразу после включения: bf7-knopka wakeup → через 0,4 с взмах сверху →
  шторка открылась (проверено камерой). «Application firmware not running» —
  окно ~120 мс, пока асинхронный поток грузит прошивку контроллера.
- load average через 4 мин после загрузки v4: 1,6 вместо 14. Остаток давал
  disp_check (msleep(2000) в вечном цикле) — в v5 schedule_timeout_idle (и
  в соседнем external_display_check). hang_detect1 в D только первые 120 с
  после загрузки (разовая пауза) — не трогал.
- bf7_dl1_dram = Y; строк ISINK в журнале ядра 0.
- Датчик света на закате: 17:52 — 189/230, 18:03 — 104/181 (ch0/ch1).
- 18:55: закат досмотрен — ch0 730 → 8, ch1 443 → 126 («пол»). Датчик
  исправен; люксы в bf7-sensor-proxy теперь по ch0 × 0,18 (в сумерках 1,1 лк).

### 2026-09-25 19:30 — зомби у всех программ на GLib: ядро не знало waitid(P_PIDFD) (v6)

gsd-power оставлял по два зомби `gsd-backlight-helper` на каждую смену
яркости (5 смен → +10), gnome-session — всех своих детей. Маски сигналов
чистые, LD_PRELOAD ни при чём (пробник на GSubprocess без подгрузки тоже
оставлял зомби). strace: GLib 2.74 делает `pidfd_open` — успешно (в ядре
бэкпорт), ждёт pidfd, забирает `waitid(P_PIDFD)` → EINVAL. P_PIDFD появился
только в 5.4. Перенесён upstream 3695eae5fee0 (kernel/exit.c + uapi wait.h),
ядро v6 прошито: пробник — 0 зомби, на телефоне 1 вместо 7, пять смен
яркости — ни одного нового. Препятствие для автояркости снято. Проба
ambient-enabled в тёмной комнате (0 лк): gsd подсветку не тронул — видимо,
реагирует только на изменение света; вернул выключенным до проверки с лампой.

### 2026-09-25 21:05 — автояркость включена (просьба хозяина)

Первая попытка: при лампе ch0 ≈ 2, ch1 ≈ 240 — ch0 оказался ИК-каналом (на
закате падал вместе с солнцем), видимый свет — ch1, как и считает прошивка
SCP. Люксы снова ch1 × 0,3 (лампа ≈ 70–150 лк). gsd-power ставит яркость
пропорционально LightLevel от точки отсчёта (яркость при первом замере):
лампа 144 лк = 55 %, телефон в руке ~3 лк → подсветка 2 из 255. Сразу
выключил, вернул 55 %. Теперь bf7-sensor-proxy отдаёт √лк + 2 (единица
«vendor») с медианой пяти замеров: подсветка стоит ровно 136–141, из-под
лампы в темноту упадёт примерно вчетверо, не в ноль. ambient-enabled=true в
dconf/00-bf7. Зомби от помощника яркости не копятся (ядро v6).
- 21:08: хозяин повернул телефон — автоповорот зеркален по горизонтали (на
  левый бок экран вставал как на правый). Ось x sc7a20 смотрит влево: знак x
  в bf7-seismograf теперь −1 по умолчанию (переопределение — /etc/bf7/akselerometr).

### 2026-09-25 21:12 — ПОПРАВКА к 14:57: ранняя фиксация кадра небезопасна, GTK4 вернулся на Cairo

Хозяин заметил на «Контактах» (Создать контакт): на панели сквозь окно видны
обои, шапки и аватара нет, есть лишь отдельные строки, а grim показывал кадр
целым. Это недорисованный кадр: ранняя фиксация отдавала буфер phoc до
окончания работы GPU. GTK4 дорисовывает только изменившиеся области поверх
старого содержимого буфера, а fencetest (ровная заливка всего кадра) и grim
(снимает позже, когда буфер уже готов) этот случай не ловят. Вывод «PowerVR
упорядочивает чтение phoc после записи клиента» — НЕ доказан; правда о
показе — только камера.

Ранняя фиксация выключена по умолчанию (включение для опытов —
`BF7_EARLY_COMMIT=1`), кадр отдаётся после забора (поток фиксации). После
этого камера совпала с grim. Но GTK4 на GPU тогда 44–46 к/с, ~30 % кадров
дольше 25 мс, а Cairo — ровные 60 к/с. `GDK_DEBUG=gl-gles` убран из
bf7-phosh.service и из окружения активации D-Bus; программы, запущенные
самим phosh, очистятся после перезапуска сеанса. Кэш шейдеров (общий журнал)
остаётся — он полезен phoc, WebKit и Vulkan. Самотест: GTK4 проверяется на
отсутствие gl-gles. Причина (поворот в 21:03 перед сбоем в 21:06 мог дать
пересоздание буферов) — вероятная; повторить с BF7_EARLY_COMMIT=1 и
поворотом при камере, когда телефон свободен.
- 01:19 (26.09): `bf7-install.sh --proverka` на копии репозитория: расхождений
  между репозиторием и телефоном 0 — всё сделанное за день попадёт в
  переустановку. Хозяин попросил выключить мониторинг SMS на ночь и
  притушить мониторы ПК до 30 % (xrandr --brightness 0.3).

### 2026-09-30 14:10 — Phonon-2 (распознавание речи, английский) на телефоне

`pip install fermion-research` + torch 2.14 (cpu) в /root/phonon-venv
(понадобился пакет python3.11-venv). Быстрый путь fermion падает SIGILL:
libphonon2_enc-linux-aarch64.so содержит `sdot` (ARMv8.2 dot-product), а
Cortex-A53 — ARMv8.0 (Features: fp asimd … без asimddp). Переносимый режим
`FERMION_P2_CPU=fp32 FERMION_P2_CPU_TDT=torch FERMION_P2_CPU_ENC=torch`
работает: английская фраза espeak-ng (10,9 с) распознана без ошибок
(«Power VR» вместо «PowerVR»), но загрузка 53 с, распознавание 43 с
(0,25× реального времени), пик памяти 2,9 ГБ из 3,8. Для живой речи на
этом телефоне непригодно. Модель только английская; русский есть у её
основы NVIDIA Parakeet TDT 0.6B v3 (FLEURS ru WER 5,51 %) — следующая проба
через sherpa-onnx int8.
- 14:18: русский — NVIDIA Parakeet TDT 0.6B v3 (основа Phonon-2) через
  sherpa-onnx 1.13.8 (pip, aarch64), сборка int8 из релизов k2-fsa (487 МБ
  архив, 650 МБ кодировщик) в /root/asr-modeli. Фраза Piper (ru_RU-dmitri,
  9,2 с): распознавание 8,3–8,6 с (1,1× реального времени, 4 потока),
  загрузка 24 с, пик памяти 965 МБ. Текст дословный, кроме «Линусе»
  (Линуксе) и «Павлына» (PowerVR). Для сравнения на этом же процессоре:
  whisper base-q5_1 0,17×, Phonon-2 (англ., fp32) 0,25×. int8 onnxruntime
  на ARMv8.0 работает без sdot. Сценарий /root/bf7-proby/phonon/parakeet-test.py.
- 14:22: bf7-sluh (распознавание собеседника в разговоре) теперь на
  Parakeet: модель /opt/bf7-golos/parakeet-tdt-0.6b-v3-int8, sherpa-onnx в
  /opt/bf7-golos/venv (подключается через sys.path — системный pip закрыт),
  whisper — запасной (--whisper). Новый ключ --proba ФАЙЛ.wav. Та же фраза:
  Parakeet 8,3 с, whisper base 21,9 с и с большим числом ошибок. Загрузка
  24 с — запись уже идёт в файл, отставание уходит на паузах. Следующий шаг:
  грузить модель во время звонка, до ответа (из bf7-zvonki).

### 2026-09-30 14:30 — зависание 26.09 (hang_detect), ядро v7

Телефон сбрасывался 26.09 в 17:06: в ramoops (/var/lib/systemd/pstore)
на 126-й секунде загрузки `bf7-seninf-up` при выходе → ISP_release →
«camsv1 already unprepared» (двойное выключение тактирования), сразу за
ним CMDQ timeout WDMA дисплея, процессы в D, выключение не шло (SIGKILL не
брал pulseaudio/journald/timesyncd), через ~5 мин hang_detect. Причина в
camera_isp.c (mt6761): ISP_suspend выключает часы столько раз, сколько
включено, а ISP_resume включал один раз — при двух открытиях после сна
лишнее выключение уводит в минус CCF общих часов (SMI делит дисплей).
В v7: resume возвращает столько включений, сколько снял suspend, и
выключение при нулевом счётчике пропускается с записью в журнал. Прошито,
самотест 94/0. Попутно: bf7-vibrad не гасит уже выключенный мотор (108
строк «vibr_reg already disabled» за 3 дня). Сейсмограмма первой ночи:
толчки 01:15:18 и 01:16:49 — вибрация самого телефона на SMS хозяина
(01:15:17, 01:16:46); «в руках» теперь отдельной строкой.

### 2026-09-30 14:38 — настоящая причина недорисованных окон: возраст буфера «2» в libhybris

Повтор сбоя 25.09 при камере (сравнение grim/камера после каждого шага):
«Контакты» на GLES → новый контакт → касание поля телефона → всплывает
клавиатура (окно меняет высоту, буферы пересоздаются).
- ранняя фиксация: камера — обои сквозь нижнюю половину окна;
- безопасная фиксация: тоже сбой (grim — обои почти во всё окно, камера —
  пятно обоев на поле телефона).
Корень: `platforms/common/nativewindowbase.cpp` отвечает на
NATIVE_WINDOW_BUFFER_AGE константой 2 («sure :)»). Драйвер отдаёт это как
EGL_BUFFER_AGE, GTK4 дорисовывает лишь изменения двух кадров — даже в
только что созданный пустой буфер. Починка без смены ABI общего класса:
окно Wayland (EGL) в конструкторе подменяет ANativeWindow::query; номер
кадра хранится в самом WaylandNativeWindowBuffer (bf7_kadr, у нового — 0),
возраст = кадр_сейчас + 1 − кадр_буфера, 0 для нового. После починки с
безопасной фиксацией — камера и grim совпадают. С BF7_EARLY_COMMIT=1 —
по-прежнему обои сквозь окно: ранняя фиксация небезопасна отдельно (GPU не
упорядочивает чтение композитора), остаётся выключенной. GTK4 — на Cairo
(60 к/с против 45 на GPU); починка возраста помогает остальным GL-клиентам.

### 2026-09-30 14:55 — огонёк уведомлений: инверсия ISINK1 (ядро v8/v9)

Камерой (поток окна-монитора, пятно у фронтальной камеры): огонёк горел
постоянно, событие message-missed-sms его не меняло; blue=255 → гаснет.
Отключить зарядку программно для проверки «без кабеля» не удалось (и сбил
thermal_input_current_limit, вернул через mtk-master-charger — см. память).
leds-mt6357: переключатель bf7_invert_isink1. Сначала max − b (v8): узор
feedbackd давал вспышку лишь на ~50 мс вершины — огонёк горит только при
полностью выключенном ISINK1. В v9 — порог: запрошено > половины → ISINK1
выключен (огонёк горит). С узором «0 250 255 250» и прямоугольником 1 с —
чёткие вспышки (≈100 ↔ ≈247 по кадру). Первая проба с fbcli ложно
«не мигала»: фоновый fbcli умирал вместе с ssh. Включено при загрузке:
tmpfiles/bf7-svetodiod.conf (параметр + яркость 0); самотест 94/0.
Безопасно и без кабеля: худшее — огонёк не вспыхнет, но гореть без повода
не будет.

### 2026-09-30 14:59 — усилитель динамика гаснет на время сна

Долг «расход в глубоком сне ~50 мА»: из двух постоянных потребителей камера
уже гаснет, теперь и усилитель (bf7-extamp: Conflicts/Before=sleep.target;
bf7-posle-sna поднимает его через ~1–3 с после пробуждения, до камеры).
Проверено через `rtcwake -m no -s 25` + `systemctl suspend`: Stopping
bf7-extamp 14:58:58 → сон → Started 14:59:02, линия 151 снова поднята.
Промахи опыта: `rtcwake -m freeze` идёт мимо systemd (крюки не работают);
экран «проверял» по lcd-backlight/brightness и усыпил с горящим — 72 строки
CMDQ ERR, дисплей выжил. Экран проверять только по card1-DSI-1/enabled.

### 2026-09-30 15:13 — сейсмограф по питанию; качели авторяркости

**Пробуждения в покое.** Замер по ctxt-переключениям (/proc/*/status, экран
погашен, 20 с): bf7-seismograf ~74 в секунду — больше всех. Каждый отсчёт
FIFO — отдельное чтение I2C (sc7a20 не заворачивает адрес), и каждое будит
поток. Сделал три режима: на зарядке (usb/ac online) — как было, 50 Гц,
CSV и ориентация; на батарее с горящим экраном — только ориентация, 10 Гц
(CTRL_REG1 0x27), забор раз в секунду, среднее 0,05; на батарее с
погашенным экраном — датчик выключен (0x07), служба раз в 2 с проверяет
питание и экран. Экран — по card1-DSI-1/enabled. Для проверки без
выдёргивания кабеля — BF7_SEISMO_BATAREYA=1.
Итог: экран погашен — 10 переключений за 20 с (было ~1480); горит — ~15 в
секунду (10 отсчётов по I2C), при горящем экране это ничто рядом с 60 к/с.
Автоповорот на 10 Гц работает (monitor-sensor: orientation normal).

**Авторяркость дёргала pkexec.** В журнале gsd-backlight-helper через pkexec
раз в 2–4 с, яркость ходила 252→194→252. LightLevel качался 8,8 ↔ 10,3.
Сырые ch1 mn78911 раз в 0,5 с: примерно поровну ~152 и ~251, ch0 ровный
~607 — мерцание внешнего источника в коротком замере. Свет панели ни при чём:
при подсветке 245/200/100 ch1 одинаковый (152). Медиана семи перескакивала
между группами. Теперь среднее двенадцати замеров за 3 с (опрос 250 мс), окно
чистится при новом пуске. После правки за 40 с — ни одного сообщения и ни
одного pkexec (правда, к тому времени мерцающий источник уже пропал).

### 2026-09-30 15:18 — замер пробуждений врал в сторону ядра

Трассировка sched_wakeup с фильтром comm ~ "migration*" за 3 с дала одно
пробуждение — от моего же `sleep` (sched_exec при exec). Прежний замер
(bash-цикл, awk и cat на каждую нить) сам порождал сотни exec и будил
migration/N, rcu и IPI0. Новый — `tools/bf7-kto-budit.py`, один процесс.
Экран погашен, сейсмограф в режиме батареи, 30 с: всего 1748 переключений
(58/с), arch_timer 32/с, IPI0 20/с; rcu_preempt 8/с, exe_cq/0 (очередь
команд eMMC) 4–8/с, kblockd, krtatm и pvr_cacheop ~2/с. migration в списке
нет. Диск будит journald (~200 КБ за 30 с при 87 строках за 2 мин);
растягивать writeback не стал — паники уже портили корневую ФС. Повторные
DHCP на wlan0 — роуминг на соседний BSSID того же роутера, не сбой.

### 2026-09-30 15:42 — GTK4 на GPU с заборами в композиторе: не делать, предел железа

Замысел: libhybris фиксирует кадр сразу и передаёт забор своим протоколом,
phoc велит GPU ждать его (eglWaitSyncKHR, расширения есть), а свой кадр
отдаёт ядру с IN_FENCE_FD. Проверки до постройки:
- счётчик загрузки GPU `/sys/kernel/ged/hal/gpu_utilization` всегда «0 0
  100», `gpu_loading` в /proc/gpufreq тоже 0 — не кормится, не использовать;
- wlroots 0.16 IN_FENCE_FD не ставит (в libwlroots.so.10 нет ни одной
  строки «fence»), после отрисовки только glFlush; наш bf7_drm ждёт лишь
  неявные заборы dma-buf, а PowerVR собран без buffer sync. Кадр phoc
  сейчас попадает на панель потому, что успевает до чтения контроллером;
- phoc 0.24 шлёт frame_done сразу после неблокирующего wlr_output_commit —
  конвейер был бы возможен;
- gtk4-widget-factory, 6 взмахов: безопасная фиксация — кадр 23–28 мс,
  GPU после swap 13,5–18,8 мс (утром на другом содержимом 10–12);
  ранняя — ровно 16,6 мс (пропускная способность GPU хватает, но задержка
  не видна: забор закрывается без ожидания).
Вывод: при заборе в композиторе его кадр готов через ~15 + 3 мс после
swap клиента — позже следующего кадрового импульса. wlroots держит один
переворот в полёте, значит провалился бы в 30 к/с весь экран, а не одно
приложение. GTK4 остаётся на Cairo (ровные 60 к/с).

Заодно в разблокированном сеансе проверено затемнение (idle-dim): подсветка
149 → 53 примерно через 40 с простоя, гашение через 120 с.

### 2026-09-30 15:57 — Диктофон и Календарь; уровень микрофона; время запуска приложений

Поставлены gnome-sound-recorder 43 («Диктофон», помечен мобильным) и
gnome-calendar 43 (26 МБ с libwebkitgtk-6.0). Календарь в узком окне
раскладывается сам, но в .desktop не помечен мобильным — добавлен в
`force-adaptive` Phosh (dconf 00-bf7). Оба проверены камерой.

Диктофон пишет (FLAC 44,1 кГц стерео). Тон 1 кГц из динамика на
громкости 0,3 — СКЗ 800 при PGA 24 дБ, 1587 при 30 дБ; тишина 4 и 8,7
(шум самого микрофона растёт вместе с усилением). PGA не трогал —
тот же предусилитель может работать в звонке. Источник bf7in стоял на
80 % (−5,8 дБ, из базы module-device-restore) — поднят до 100 %.

Нажатие кнопок без координат: `tools/bf7-atspi.py ПРИЛОЖЕНИЕ [ИМЯ]` через
AT-SPI (у кнопок-картинок GTK4 имя — класс, смысл в описании: «Остановить
запись»).

Время до первого кадра (WAYLAND_DEBUG, первый attach): Контакты 1,07 с,
Погода 1,23, Часы 1,13, Калькулятор 1,06, Консоль 0,96, Настройки 1,17,
Файлы 1,73, Книги 2,39, Камера 1,62, браузер 3,08. У браузера 0,63 с —
динамическая загрузка (186 библиотек, 565 тыс. относительных и 83 тыс.
символьных перемещений), 0,55 с — разбор встроенной темы GTK3, 0,88 с —
ожидание процесса отрисовки WebKit. Ускорять нечего без смены браузера.
Chromium из сетки не стартует под root без --no-sandbox — он уже спрятан
bf7-phosh-skryt, как и Megapixels.

### 2026-09-30 16:13 — ночной свет через блок гаммы MediaTek; звук в Bluetooth

**Ночной свет не мог работать вовсе.** gsd-color ведёт его сам (проверено:
NightLightActive=true, Temperature доползает до 2700 за ~6 с даже при
NightLightSupported=false у phosh), но гамму ставить некуда: bf7_drm не
объявляет GAMMA_LUT, wlroots 0.16 молчит. В тракте есть disp_gamma
(512 входов, GAMMA_ENTRY(r10,g10,b10)); таблица в нём не была записана ни
разу — отсюда «disp_gamma_write_lut_reg: table [0] not initialized» в
журнале. Вендорский DISP_IOCTL_SET_GAMMALUT (_IOW('x',23), 2052 байта) на
/dev/mtk_disp_mgr идёт через primary_display_user_cmd и очередь CMDQ.
Проверка камерой на белой заливке: автобаланс камеры уводит фон в синеву,
когда экран теплеет (отношение R/B в центре 0,98 → 1,025).
Ловушка: при DISP_SLEPT команда выбрасывается с ret=0 и не запоминается.
Служба `bf7-nochnoj-svet` (Python, только сигналы D-Bus) слушает
NightLightActive/Temperature у gsd-color и PowerSaveMode у phosh, после
пробуждения пишет таблицу заново через 1 с. Проверено: выключил ночной свет
при погашенном экране — после пробуждения цвет обычный. Множители — colord
color_get_blackbody_rgb_full (USE_PLANCKIAN), как у gsd: 2700 K → 1/0,676/0,348.
Расписание в dconf ручное 20:00–06:00 (геолокация для root закрыта),
по умолчанию выключен; запись «Ночной свет» в сетке переключает и сообщает
уведомлением. Прямая таблица теперь стоит всегда вместо «нет таблицы».
python fcntl.ioctl: неизменяемый буфер длиннее 1024 байт не принимает —
нужен bytearray. notify-send на телефоне нет — Notify через Gio.

**Bluetooth-звук.** pulseaudio-module-bluetooth не стоял — сопряжение было,
звука в наушники не было бы. Поставлен, в bf7.pa policy/discover и
switch-on-connect. Проверить нечем: сопряжённых устройств нет.

### 2026-09-30 16:41 — GPS: до чипа живой, спутников в комнате нет

Шлагбаумы (по совету наблюдателя), каждый мог закрыть затею:
- стоковый gps_drv.ko (4.19.191) импортирует только то, что наше ядро уже
  экспортирует (mtk_wcn_stp_*, mtk_wcn_wmt_*, mtk_wmt_gps_*, connsys_log_*,
  dvfsrc_enable_dvfs_freq_hopping) — пересобрать можно;
- с mnld говорит legacy gps.default.so (gpshal2mnl_*), HIDL его лишь грузит —
  значит, клиент через libhybris hw_get_module("gps");
- geoclue 2.6.0 знает nmea-socket; «Geolocation disabled for UID 0» — от
  выключенного org.gnome.system.location, не от root.

**Модуль** (`kernel-work/bf7-gps`): донор ubports mt6781 gps_stp, состав как
у стокового — stp_chrdev_gps + gps_emi + fw_log_gps. Две правки: нет
helio-dvfsrc.h (объявил функцию), и gps_emi НЕ ставит регион EMI MPU 29 —
его держит наш Wi-Fi (plat_priv.c), перезапись уронила бы Wi-Fi по ASSERT.
Область GPS (0xbf000000 + 2,5 МБ + 4 КБ, ~1,5 МБ) внутри памяти connsys
(0xbf000000, 4 МБ), открытой регионом 25 с теми же правами. insmod чистый,
/dev/stpgps, /dev/gps_emi, /dev/fw_log_gps; Wi-Fi жив.

**mnld** (андроидный ELF): свойства через свою подложку bf7-gps-props.so
(NDK). Без persist.vendor.connsys.chipid (0x6761, формат wmt_loader) —
«get chip id fail», dev_gps пустой, /dev/stpgps не открывается. Без
vendor.service.nvram_init=Ready — 20 с ожидания и работа без калибровки
/mnt/vendor/nvdata/APCFG/APRDEB/GPS. /dev/gps_pwr ищет 10 раз и бросает.
IOCTL_EMI_MEMORY_INIT → EPERM и в стоковом (флаг param_ready ставить некому;
это захват АЦП для заводских тестов).

**Клиент** bf7-gps-nmea (libhybris): HAL MTK ждёт GpsCallbacks на 112
байт («Bad callback, size: 96, expected: 112») — test_gps молчал. С двумя
хвостовыми заглушками — «Use GpsCallbacks», GPS_open: «WMT turn on GPS
OK», чип шлёт ~940 байт/с, АРУ 5000–6400 (тракт слышит шум), TSX 27,4 °C.
NMEA идёт (GGA/RMC/GSA/VTG + PMTK), спутников 0 за 4 и 3 минуты в комнате,
холодный старт, EPO не качается (res 26). Нужно небо.

Журнал андроидных демонов: `tools/bf7-logdw.py` слушает /dev/socket/logdw.

### 2026-09-30 16:46 — GPS как служба; проверка перезагрузкой

bf7-gps (insmod gps_drv после wmt_drv + mnld) и bf7-gps-nmea включены.
Ловушка юнита: Environment=LD_PRELOAD с бионик-подложкой попадал и в
ExecStartPre (/bin/sh на glibc) — код 127; теперь `env LD_PRELOAD=… mnld`.
Приёмник по требованию: клиент к /run/bf7-gps/nmea.sock — «WMT turn on GPS
OK», 241 строка NMEA за 15 с; отключился — GPS_close. В покое mnld (16
нитей) и раздатчик — 0 пробуждений за 30 с. geoclue.conf: nmea-socket.
Местоположение (org.gnome.system.location) оставлено выключенным.
Перезагрузка: bf7-gps поднялся на 7,5 с (1,05 с на службу, параллельно),
Wi-Fi жив, нарушений MPU нет, самопроверка 100/0, загрузка 24,7 с.

### 2026-09-30 16:53 — geoclue до сокета GPS; журнал андроидных демонов без предела

**geoclue.** Клиент через GetClient (DesktopId sm.puri.Phosh, точность 8):
при выключенном org.gnome.system.location — «Geolocation disabled for UID
0» даже для system=true (запрещает агент Phosh). На 20 с включил и вернул:
Start → bf7-gps-nmea «приёмник включён», Stop → «выключен»; geoclue пишет
«Valid NMEA GGA or RMC sentence not found» — фикса нет, так и должно быть.
Проверено и после перезагрузки (mnld и раздатчик стартуют почти разом).

**Поправка к записи выше:** tools/bf7-logdw.py был лишним — приёмник
liblog уже есть (bf7-minilogd, /usr/local/sbin/minilogd.py), а мой, заняв
/dev/socket/logdw, до перезагрузки отобрал у него сокет. Удалён.
**minilogd писал без предела:** StandardOutput=append: и нет logrotate —
/var/log/bf7-android.log 11 МБ (IMGSRV и SetupCopyFunction из блобов GPU,
wmt_loader). mnld при включённом GPS ~5 КБ/с. Теперь minilogd пишет сам и
при 4 МБ переносит в .1; в метке времени добавлена дата.

`bf7-install.sh --proverka` после дня правок через scp: расхождений 0.

### 2026-09-30 17:01 — WAKEUP после кнопки питания; будильник; события на экране блокировки

**KEY_WAKEUP после гашения кнопкой питания игнорируется первые секунды.**
Замер (гасил bf7-knopka, будил bf7-knopka wakeup, опрос DRM раз в 50 мс):
через 5 с — не зажёгся за 6 с; через 30 и 60 с — зажёгся за 1,59 с (из них
1,5 с — пауза самой bf7-knopka до нажатия). Сперва я поспешно решил, что
после кнопки питания WAKEUP не будит вовсе, — две попытки попали в это окно.
Для утреннего будильника окно не страшно, но bf7-budilnik теперь страхуется:
через 1,5 с после завершения bf7-knopka wakeup, если card1-DSI-1 всё ещё
disabled, жмёт виртуальную кнопку питания; наблюдатель «кнопка питания =
отложить» пропускает устройство bf7-knopka. Первая версия считала 1,5 с от
запуска bf7-knopka, а не от конца — и в проверке 17:03 нажала кнопку по уже
проснувшемуся экрану и погасила его: исправлено до коммита.
PowerSaveMode=0 тоже будит, но phosh остаётся в «погашен вручную», и
следующее нажатие кнопки питания уходит впустую — не годится.

**Экран блокировки:** плагин phosh upcoming-events (sm.puri.phosh.plugins
lock-screen), свайп слева направо — события Календаря по дням. Список
плагинов читается только при запуске phosh.

### 2026-09-30 17:24 — phosh 0.24.0-1+bf7.5: экран блокировки возвращается на часы

Проверка будильника показала: экран блокировки, оставленный на странице
плагинов (события Календаря), так и встречает при следующем включении — и
звонок будильника шёл поверх списка «Нет событий». Сброс в phosh есть только
для клавиатуры PIN (keypad_check_idle, 5 с); страница плагинов — отдельный
элемент HdyDeck, её никто не возвращает. Заплатка
hybris/bf7-patches/phosh-0.24-lockscreen-widget-back.patch: при уходе на
неё — таймер 30 с, затем hdy_deck_set_visible_child(carousel).
Сборка: чистое дерево из dsc + все пять заплаток, nocheck, 12 мин (484
шага). Проверено камерой: открыл события, экран погас по простою, после
пробуждения — часы. hold возвращён, служба ночного света поднялась с сеансом.

### 2026-09-30 17:31 — GPS: EPO наперёд да, SUPL нет

mtk_agpsd запускается под той же подложкой свойств (профиль GOOGLE,
supl.google.com:7275, TLS), соту видит (mcc 250), но SUPL не идёт:
getaddrinfo у bionic ходит через netd (/dev/socket/dnsproxyd), которого нет.
Чинить не стал: SUPL отправляет Google соту и SET ID (с рабочим RIL — IMSI)
— это решение хозяина; пробный agpsd остановлен, IMSI в журнал не попал
(только «IMSI is invalid»).

EPO (эфемериды на 3 суток) — публичный файл: sfepodownload/sgepodownload
без подписи отвечают 403, wepodownload.mediatek.com/EPO_GR_3_1.DAT отдаёт
48 КБ, MD5 сходится. Положен в /data/vendor/gps/EPO.DAT (+EPO_GR_3_1.MD5):
mnld — «EPO start time: Wed Sep 30 00:00:00 2026», «EPO file is valid»,
загрузил в чип (). bf7-gps-epo.timer раз в 6 ч, качает, если файлу
больше 20 ч, только при маршруте по умолчанию; проверено принудительно.
Эффект на первый фикс проверит хозяин у окна.

### 2026-09-30 17:32 — аппаратные часы отставали на 4,5 минуты

timedatectl: системное время по NTP верное, RTC (mt6397) — 14:26:51 при
14:31:20 UTC. В ядре CONFIG_RTC_SYSTOHC не включён, timesyncd в RTC не
пишет, так что RTC уходил сам по себе. Будильник во сне не страдал:
alarmtimer заводит RTC относительным интервалом от прочитанного RTC. Но
загрузка без сети начиналась бы с неверного времени (EPO, TLS, журналы).
hwclock --systohc на этом RTC работает; bf7-rtc-sync.timer раз в 12 ч
пишет время, только если NTPSynchronized=yes. Через сутки посмотреть,
как быстро RTC уходит (сверка hwclock -r с date).

### 2026-09-30 17:36 — снимок экрана; GPS переживает сон

**Снимок экрана:** клавиши Print нет, сочетания кнопок в phosh 0.24 нет;
org.gnome.Shell.Screenshot у phosh снимает в логических 360×806. Запись
«Снимок экрана» (bf7-snimok): через 3 с grim — 720×1612, в «Изображения/
Снимки экрана», уведомление с превью. При погашенном экране grim отвечает
«failed to copy output» — снимать нечего, уведомление об ошибке.

**Сон с загруженным gps_drv:** rtcwake -m no -s 25 + systemctl suspend —
вышел, bf7-gps и раздатчик живы, приёмник после сна включается (161 строка
NMEA за 10 с) и выключается. В журнале при засыпании WARN со стеком
ktime_get_real_ts64 ← aee_kernel_warning_api ← clkchk_syscore_suspend:
«suspend warning: univ2pll is on» — такт USB включён, потому что кабель;
проверка тактов MediaTek зовёт AEE, когда часы ядра уже остановлены. Только
на кабеле, к GPS отношения не имеет.

### 2026-09-30 17:39 — точка доступа

iw: AP среди режимов, до 16 клиентов. NetworkManager: wifi mode ap на ap0,
ipv4.method shared — ap0 в режиме AP, 10.42.0.1/24, dnsmasq, при этом wlan0
остаётся подключённым к HOME-AP. bf7-firewall (INPUT DROP, broadcast DROP,
FORWARD DROP) не мешает: NM вставляет nm-sh-in/fw-ap0 первыми правилами и
MASQUERADE; при выключении убирает. Запись «Точка доступа» (bf7-tochka):
сеть «BF7», пароль из 10 знаков без путаемых l/1/o/0, заводится один раз и
хранится в NM; уведомление временное. Клиентом не проверено — второго
устройства под рукой нет; точку сразу погасил.

### 2026-09-30 17:41 — правило udev «кабель выдернут» не работало ни разу

В журнале: «79-bf7-pitanie.rules:12 Invalid value … for RUN (char 99:
invalid substitution type), ignoring». Правило от 24.09 рассылает «change»
от usb и зарядников, когда меняется battery/mtk_charger_type, — иначе UPower
помнит online=1 после выдёргивания кабеля, gsd считает телефон «от сети» и
не усыпляет. Внутри RUN стоял `$d`, а udev сам разбирает $-подстановки и
отвергал правило целиком. Исправлено на `$$d`. Проверка: udevadm trigger
--action=change battery → в udevadm monitor «change» от usb, ac,
mtk-master-charger, mtk-slave-charger. Значит, всё, что после 24.09
говорилось о засыпании на батарее без кабеля, стояло на неработающем
правиле; замер расхода без кабеля ещё впереди — хозяину.

### 2026-09-30 17:48 — ap0 больше не сканирует; как читать огонёк

**ap0:** весь день раз в 2 мин «ap0: CTRL-EVENT-SCAN-FAILED ret=-22» (66 за
загрузку): NM держал второй интерфейс драйвера как обычный клиент Wi-Fi и
сканировал им. ap0 и wlan1 — managed=0 разделом [device-bf7-ap0] в
bf7-nm-unmanaged.conf. Не через unmanaged-devices: оттуда «nmcli dev set
ap0 managed yes» запрет не снимает, и точка доступа не включилась бы.
bf7-tochka берёт ap0 на время раздачи и отдаёт обратно; проверено после
перезапуска NM: точка поднимается, ap0 затем снова unmanaged, сканирований
после 17:46 нет.

**Огонёк:** с инверсией в ядре brightness читается как состояние ключа
ISINK1: записал 0 — читается 255, и это ТЁМНЫЙ огонёк. После события
feedbackd триггер остаётся pattern с пустым узором — покой. Я было принял
255 за «горит».

### 2026-09-30 17:50 — диктовка на телефоне: не делать

Идея: запись «Диктовка» — сказал, текст в буфер обмена. Замер на ru.wav
(9,2 с речи): Parakeet TDT 0.6B int8 — распознавание 8,2–8,5 с (1,1×), но
загрузка модели 25,7 с и пик памяти 1318 МБ; whisper base — 0,17× (почти
минута на ту же фразу). Держать Parakeet в памяти постоянно — треть ОЗУ,
грузить по нажатию — полминуты ожидания до первой буквы. Для звонков
(bf7-sluh) это терпимо, для диктовки нет. Вернуться, если появится модель
легче или ORT научится кэшировать оптимизированный граф.

### 2026-09-30 18:02 — ядро v10: clkchk без AEE, RTC_SYSTOHC

Две правки пакетом (дерево x657b-4.19, коммит b48c59fb5; конфиг v3):
- clkchk-mt6761: при незакрытых PLL/MTCMOS в syscore_suspend вместо
  aee_kernel_warning + WARN_ON — только строки «suspend warning». На кабеле
  univ2pll включён всегда, и каждое засыпание давало WARN со стеком из
  ktime_get_real_ts64 (часы ядра уже стоят) и пометку Tainted W;
- CONFIG_RTC_SYSTOHC=y: ядро само пишет время NTP в RTC раз в 11 минут
  (bf7-rtc-sync.timer оставлен страховкой).
Запас v9: kernel-work/build/zapas-v9-2026-09-30. Прошивка tools/bf7-proshit.sh:
суммы модулей сошлись (12 модулей, включая gps_drv), загрузка 34 с,
самопроверка 104/0. Сон 25 с: «suspend warning … univ2pll is on» есть,
WARN от сна нет; оставшиеся два WARN — с первой секунды загрузки
(gic_irq_domain_translate, proc_register), старые.

### 2026-09-30 18:27 — утечки памяти в phosh нет

Днём phosh рос 94 → 110 МБ за 25 мин, но я всё это время открывал сетку,
экран блокировки, уведомления и программы. После перезагрузки (ядро v10):
84 МБ сразу, 93 МБ через 6 мин (прогрев, кэши значков), затем 11 замеров
раз в 2 мин без касаний — RSS 93 488 КБ и PSS 53 673 КБ неизменно 20 минут.

### 2026-09-30 18:35 — калькулятор: экранная клавиатура закрывала его кнопки

Обход приложений камерой: у gnome-calculator при запуске фокус в поле
формулы, GTK4 включает zwp_text_input_v3, и squeekboard выезжает поверх
собственных кнопок калькулятора. GTK_IM_MODULE=simple не помогает — это
имя модуля из GTK3; в GTK4 он «gtk-im-context-simple», с ним text_input
.enable не шлётся (WAYLAND_DEBUG: 0 против 1). bf7-phosh-skryt научен
«окружению запуска»: подмена .desktop с Exec=env GTK_IM_MODULE=… Ловушка:
сетка phosh держит объекты приложений, созданные при старте сеанса, — новая
подмена в /usr/local видна только после перезапуска phosh (до него
GIO_LAUNCHED_DESKTOP_FILE указывал на /usr/share). Проверено нажатием
значка через AT-SPI: клавиатура не видна, окно целиком (камерой).
Прочее из обхода: Контакты, Вызовы, Настройки, Часы — в порядке.

### 2026-09-30 18:38 — обход приложений камерой, итог

Погода, Книги, Файлы, Диктофон, Календарь, Блокнот, Камера, Контакты,
Вызовы, Настройки, Часы — раскладка в узком окне без огрехов. Калькулятор —
исправлен (см. выше). Карты: при запуске фокус в поиске, выезжает
клавиатура — оставлено, там ищут текстом, а трюк калькулятора убрал бы
клавиатуру совсем. Lollypop «Сканирование отключено, коллекции недоступны»
— не поломка: так он пишет, когда в ~/Музыка нет ни одного файла
(collection_scanner.py: нет files → уведомление). «Вызовы» и «Чаты» —
службы: остановка моего юнита окно не закрывает, закрывать смахиванием
карточки в обзоре.

### 2026-09-30 18:47 — точка доступа не пускает в домашнюю сеть

Наблюдатель заметил: цепочка nm-sh-fw-ap0 пропускает всё, и при
подключённом Wi-Fi клиенты точки ходили бы через wlan0 в 192.168.27.0/24 —
к ПК хозяина. Android раздаёт только интернет. bf7-tochka теперь ставит в
FORWARD DROP для ap0 → 10/8, 172.16/12, 192.168/16 (пометка bf7-tochka) и
ставит их заново уже после подъёма точки: NM вставляет свой переход первым
правилом, и наши оказались бы за ним. Проверено: при включении наши три
правила выше nm-shared-ap0, при выключении снимаются все. Интернет через
wlan0 или мобильную сеть открыт. Клиентом не проверено — нечем.

### 2026-09-30 18:49 — ограничение заряда: разбор без правки

В mtk_charger.c есть штатная сервисная ручка /proc/mtk_battery_cmd/
current_cmd («usb_unlimited cmd_discharging»): «0 1» → cmd_discharging,
charger_dev_enable(chg1_dev, false); питание от кабеля отдельно
(en_power_path, у нас 1). Проба на 8 с с возвратом: статус «Charging» и ток
battery/current_now (-110…-180 мА) не изменились вовсе, «0 0» вернуло как
было. То есть выключение заряда до микросхемы зарядника не доходит — на
плате AW32257 (свой модуль, native-linux-build/alps191-aw32257-module-v1), и,
видимо, его charger_ops.enable не заведён или chg1_dev — не он. Это и есть
причина прежней неудачи. Порог заряда реализуем только доработкой драйвера
AW32257 (бит разрешения заряда), с перепрошивкой; проверить поведение
(сползание 80→75→80, не уходит ли питание на батарею) можно лишь без кабеля —
при хозяине.

### 2026-09-30 19:24 — whisper.cpp на GPU (Vulkan): начато и остановлено по просьбе хозяина

Цель — нейросеть распознавания на GE8300 вместо процессора. Устройство:
Vulkan 1.1.170, shaderFloat16/shaderInt8, 16-битный доступ к буферам,
subgroupSize 1. Поставлены glslc (shaderc 2023.2) и spirv-headers. Заголовки
Debian 1.3.239 для ggml-vulkan стары (нет vk::LayerSettingEXT) — взяты
Khronos Vulkan-Headers v1.3.296 в /root/src/Vulkan-Headers, сборка
/root/src/whisper.cpp/build-vk (-DGGML_VULKAN=ON -DVulkan_INCLUDE_DIR=…).
Дошла до 52 % (генерация шейдеров) без ошибок и остановлена по просьбе
хозяина. Продолжить: cmake --build build-vk -j4 --target whisper-cli
whisper-bench; запуск с LD_LIBRARY_PATH=/usr/local/lib
LD_PRELOAD=/usr/local/lib/libbf7tlsguard.so. Не проверено, примет ли
ggml-vulkan устройство 1.1.
