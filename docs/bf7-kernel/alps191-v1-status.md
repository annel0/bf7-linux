# BF7 ALPS 4.19.191 v1: актуальный статус

Дата фиксации: 2026-08-23. Это отчёт о локально собранном кандидате, а не
утверждение, что он уже загрузился на BF7.

## Итог

Собран наиболее полный на данный момент BF7-кандидат trial v3: Linux 4.19.191 на
MT6761, точный OEM Clang 11, восстановленный драйвер активной панели,
заводской DT table, диагностический initramfs, USB-RNDIS, root telnet,
аппаратный и программный watchdog, matching Wi-Fi/WMT модули и exact
AW32257 charger module в manual-only режиме.

Все доступные офлайн-проверки проходят. Аппаратный boot-test пока честно не
выполнен: LK BF7 падает в downloaded-image path даже при `fastboot boot`
точного known-good image. Запись этого кандидата в A/B слот поэтому запрещена.

## Зафиксированная исходная база

- source tree: `kernel-work/mediatek-mt6761-alps188-gpu115`;
- ветка: `codex/bf7-alps191-base`;
- ALPS/GPU base: `6f3a19332a49efbc3e3e960468b198818addc137`;
- merge Android stable 4.19.191: `9c2539cb9aef6f2fc85cad43fe76eab90916820b`;
- BF7 port: `1459f1fb7cf5`;
- GPU diagnostic fix: `8bc5bc1a670a`;
- compiler: Android Clang/LLD 11.0.1, prebuilt `r383902`;
- kernel release: `4.19.191-bf7-native-alps191-v1`.

## Финальная воспроизводимая сборка

| Артефакт | SHA-256 |
|---|---|
| `Image` | `093828bab73acf47e02ba3193868fc6a74fb0f3c775570d04f69d18b89876e02` |
| `vmlinux` | `835ae034bd9663175b92e3949a58d09fd686436ee1356e7fb48dec232f582f2c` |
| config | `be527ad1efea446f3c7e0b0b3907f4f94d9fb70d57b25eceb49dfb8e7baefc5a` |
| exact OEM DT table | `8f69c79437d9e91c14122a97a6417d2600eeae7c6f530d6708862dfab474c8f0` |

Полная чистая сборка `Image modules` завершена без compiler/linker errors и
undefined references. Сборочный wrapper нормализует абсолютный `O=` path в
DWARF через `-fdebug-prefix-map`; две полные сборки в разных каталогах после
этого дали побайтно одинаковые `Image` и `vmlinux`. До нормализации код также
совпадал, но два 8-байтовых GNU build ID зависели от имени build-каталога.
Остались legacy warnings донора и два дублирующихся optional-export warning
между `mt6357-accdet.ko` и `mt6359-accdet.ko`; они не входят друг в друга и
не являются ошибкой финальной линковки ядра.

## Дисплей

Встроен только активный BF7 LCM
`td4160_hdp_dsi_vdo_hx_inx_bf7`. После независимого аудита исправлены:

- `data_format.format`: именованное RGB888-присваивание вместо ошибочного
  raw word 125;
- полный bias power path: ENP/ENN и I2C-клиент `0x3e`;
- небезопасная ранняя зависимость от `tpd_gpio_output` удалена;
- layout `LCM_PARAMS` синхронизирован с OEM: размер `0x518`, RGB format —
  word 127, RGB888 = 3;
- exact OEM init/suspend table и тайминги сохранены.

Это сильное compile-time и binary-derived доказательство, но реальный DSI,
pinctrl, bias IC и выбор `plcm_name` всё ещё требуют загрузки на телефоне.

## Wi-Fi

Из официальных Android 12 MediaTek connectivity trees Motorola tag
`MMI-STA32.79-77-28` собраны три внешних модуля именно против этого kernel
`Module.symvers`:

| Модуль | Commit | SHA-256 | ABI imports |
|---|---|---|---:|
| `wmt_drv.ko` | `dd2d236cb46469f4102b8b83855de8bf6557996e` | `ba4db09ecb3ff1a6047934be55a5851cb5fb0ef188355ebd2db757990d819c54` | 238 |
| `wmt_chrdev_wifi.ko` | `4370ded09ce824cc21df16f9771bd74220446c22` | `bbd92506cc14f6c42aa1748426721e37573343e960c0e8e269074609e288cda3` | 43 |
| `wlan_drv_gen4m.ko` | `e07ad500ebb4f963a80609a1698980bffc540e08` | `035e872c0ba394725428af57793a62de2b5cce3e6d8c513e45be8fd6823a911c` | 304 |

У всех совпадает vermagic, а проверка каждого импортируемого symbol CRC дала
`unresolved=0 crc_mismatch=0`. Выбраны MT6761 WMT, Connac, AXI и chip path
6765. Модули остаются hardware-untested и в v2 не загружаются автоматически.

## Trial images

| Образ | Размер | SHA-256 | Назначение |
|---|---:|---|---|
| `alps191-trial-v1` | 16,855,040 B | `362228ca1c021596d4ef6cfc75641e172b073a9fd36d56d7805a5b82117ce761` | минимальный storage/RNDIS baseline |
| `alps191-trial-v2` | 20,830,208 B | `cce2c173fe513542ab8ee0853c2616603c5ea991c7996fcb2b18210788f82164` | baseline плюс manual-only Wi-Fi stack |
| `alps191-trial-v3` | 20,840,448 B | `372870410949c3b0402007a2af97eb31dd85fc799a2e6cf60ec2674f752bfaa3` | v2 плюс devtmpfs и manual-only exact AW32257 module |
| `alps191-trial-v3 RAM-boot v0` | 20,838,400 B | `6eb7049c09f02f99bbd259fa9f2d71866710626df56d8c4b9b5bbefddd0d1a42` | временный test-container: header v0 + `Image.gz-dtb` с raw FDT |

Все три образа пересобраны с нормализованными cpio metadata; для v3 две
последовательные сборки дали одинаковый хэш. Reverse-unpack подтвердил
byte-for-byte совпадение kernel, ramdisk и raw FDT с входами. Все меньше 32 MiB boot
partition. В trial ramdisk нет `bf7-mark-boot-success`.

Для runtime используется только строка `trial-v3 RAM-boot v0`: прежние
downloaded header-v2 образы уже приводили LK к `lk_crash`. V0-контейнер
переносит новый kernel и raw OEM FDT тем же legacy layout, который сначала
обязан доказать минимальный OEM probe.

В v3 PID 1 больше не закрывает kernel devtmpfs обычным tmpfs. Он оставляет
уже смонтированный devtmpfs либо монтирует его сам, а ручной tmpfs использует
только как fallback. Это позволяет драйверам штатно публиковать `/dev` nodes,
но никакой node автоматически не открывается.

## Charger AW32257

Официальный Awinic exact V1.2 source адаптирован к live BF7 DT без изменения
автоматического boot path. Получен `aw32257.ko`, SHA-256
`244486945bf3bbb8ffe0fd1eddf30de7bec63c8850def8a17d9e7001ad7936c4`.
Vermagic совпадает, 37 imported symbol CRC проверены:
`unresolved=0 crc_mismatch=0`. Модуль находится в
`/opt/bf7-charger/aw32257.ko`, но init его не загружает. Явный loader требует
аргумент `--load-with-register-writes`, сначала проверяет exact DT node и
сохраняет before/after dmesg. `rmmod` запрещён: в vendor source нет remove
callback.

OEM probe отдельно восстановлен дизассемблированием заводского Image. Наша
инициализация повторяет его wire sequence `06:ff`, `00:d0`, `01:38`, `08:8e`
и затем ITERM из DT. Заодно устранён присутствующий в официальном source и
OEM binary однобайтовый heap overflow: длина I2C message осталась прежней,
но memcpy больше не копирует байт за выделенный буфер.

## Наблюдаемость и пределы удалённого восстановления

После достижения PID 1 кандидат:

- поднимает RNDIS `192.168.7.2/24` и root shell на TCP `2323`;
- запускает аппаратный watchdog с ограниченной lease;
- имеет независимый software deadline, который вызывает raw TopRGU fastboot;
- выполняет только bounded 4 KiB read и `ext4 ro,noload` probe;
- сохраняет отчёт и dmesg в tmpfs;
- может наблюдаться `alps191-trial-v2/monitor-host.sh`, который не содержит
  boot/flash/reboot/slot-команд.

Обе lease продлеваются только при ответе trial RNDIS shell. Если host monitor
пропал, продление прекращается и recovery deadline снова отсчитывается с
конечного значения. Токен software lease находится только в tmpfs.

Это не гарантирует возврат при сбое до PID 1. Текущее OEM-ядро также собрано
без `CONFIG_KEXEC`, поэтому новое ядро нельзя безопасно chainload-нуть из
рабочего Debian. Пока `fastboot boot` сломан в LK, полностью автономный и
одновременно обратимый первый hardware-test отсутствует.

## Что остаётся неизвестным

- попадёт ли точный BF7 DT/DTBO в ожидаемые pinctrl и display probe paths;
- стартуют ли eMMC, USB gadget и watchdog на новом BSP без раннего зависания;
- заработает ли ConnSys/Wi-Fi hardware с выбранным Motorola source snapshot;
- exact BF7 OVT touchscreen driver в базе отсутствует;
- camera, fingerprint и часть Transsion power/charger glue не перенесены;
- audio route, GPU runtime и modem не входят в первый безопасный тест.

Полный config diff: `oem-vs-alps191.config-diff.txt`. Перечень отсутствующих
OEM Kconfig symbols: `oem-symbols-missing-from-alps191.txt`.

## Разрешённый следующий шаг

Только временная загрузка exact v3 через `fastboot boot`, если downloaded-image
path удастся починить или обойти без записи разделов. После появления RNDIS
проверяются kernel release, watchdog, storage report, framebuffer и только
затем вручную Wi-Fi. До такого теста ни `boot_a`, ни `boot_b` не менять.

## Обновление 2026-09-11

`fastboot boot` на этом LK неработоспособен для любого образа (доказано по
`expdb` + дизассемблированию). Кандидат запущен через A/B-слот `boot_a` с
фиксом `tag_video_lfb` (`b6fdf84f159f`): дисплей инициализируется recovered
драйвером, suspend/resume и вывод из ядра подтверждены. См.
[slot-test-2026-09-11.md](slot-test-2026-09-11.md). Хэши в этом файле выше
относятся к ядру до фикса; актуальные: Image `f9366fb5…`, образ v3 `5b56af0d…`.

