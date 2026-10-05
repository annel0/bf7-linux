# Утренний отчёт: BF7 ALPS191

## Готовый кандидат

Temporary-boot-only образ:

`native-linux-build/alps191-trial-v3/bf7-alps191-trial-v3-ramboot-v0-img-gz-dtb.img`

- размер: 20,838,400 байт;
- SHA-256:
  `6eb7049c09f02f99bbd259fa9f2d71866710626df56d8c4b9b5bbefddd0d1a42`;
- kernel release: `4.19.191-bf7-native-alps191-v1`;
- legacy header v0, page 2048, `Image.gz-dtb`;
- извлечённый raw OEM BF7 FDT;
- diagnostic initramfs с devtmpfs/devpts, RNDIS, root shell, pstore,
  hardware watchdog и software return-to-fastboot;
- storage автоматически проверяется только bounded 4 KiB read и
  `ext4 ro,noload` mount;
- Wi-Fi и AW32257 включены в образ, но загружаются только вручную после
  baseline; charger требует отдельного literal подтверждения.

Обычный header-v2 reference image сохранён рядом, но для `fastboot boot` не
используется: именно downloaded header-v2 path уже приводил BF7 LK к crash.

## Что проверено

- Полный `Image modules` build дважды выполнен точным OEM Clang/LLD 11.0.1 в
  разных `O=` на 16 потоках.
- После нормализации DWARF build-prefix `Image`, `vmlinux` и `.config`
  совпали побайтно между независимыми сборками.
- Kernel `Image` SHA:
  `093828bab73acf47e02ba3193868fc6a74fb0f3c775570d04f69d18b89876e02`.
- `vmlinux` SHA:
  `835ae034bd9663175b92e3949a58d09fd686436ee1356e7fb48dec232f582f2c`.
- V3 и его legacy container дважды пересобраны с одинаковыми SHA;
  reverse-unpack побайтно подтвердил kernel payload, ramdisk и DTB.
- Полный v1/v2/v3 preflight прошёл: critical Kconfig, LCM/system symbols,
  boot headers, размеры, exact recovery backup, Wi-Fi ABI closure и charger
  ABI closure.
- Operational-скрипты просканированы: в них нет `fastboot flash`, erase,
  set-active, mtkclient или записи в eMMC.
- Оба host-runner имеют hard gate и без явного аргумента завершаются до любой
  device-команды. V3 дополнительно требует маркер exact OEM legacy probe.

## Исправления этого прогона

1. Устранена ложная невоспроизводимость двух GNU build ID из-за абсолютного
   имени `O=` в DWARF.
2. Minimal OEM probe получил RNDIS shell, 60-секундный software return и
   аппаратный watchdog fallback.
3. Добавлен двухэтапный host-controller: сначала OEM legacy probe, затем v3.
4. V3 переведён с проблемного downloaded header-v2 на отдельный header-v0
   `Image.gz-dtb` контейнер.
5. Исправлен порядок mountpoint-ов: devtmpfs/tmpfs больше не скрывает
   `/dev/pts`, devpts и `/dev/ptmx` готовы до telnetd; configfs/pstore точки
   создаются после sysfs.

## Чего пока нельзя честно утверждать

Образ compile-complete и offline-preflight-complete, но ещё не
hardware-boot-proven. Без отдельно разрешённого runtime-test нельзя доказать
ранний старт нового BSP, дисплей/DSI, eMMC, USB gadget и watchdog на железе.
Ни один образ в этом прогоне не загружался и не прошивался.

Последняя read-only проверка телефона: работает установленный known-good
`4.19.191-g216400aba14e-dirty`, slot `_b`, root `/dev/mmcblk0p48`; телефон не
перезагружался и разделы не менялись.

## Следующий шаг

Следовать только [first-boot-runbook.md](first-boot-runbook.md). Сначала exact
OEM legacy probe. Только если он достигает shell и сам возвращается в
bootloader fastboot, runner разблокирует временный запуск v3. Прошивка слота
до двух успешных временных прогонов запрещена.

## Дополнение после runtime-test

Minimal OEM legacy probe был разрешён и запущен через настоящий bootloader
fastboot. Payload передался с `OKAY`, затем LK оборвал команду со статусом
`Status read failed (No such device)`. Probe USB/shell не появились;
зафиксированы Preloader и автоматический возврат установленного OEM-native
Linux. Новый cmdline содержит `androidboot.bootreason=lk_crash`.

Позднее выяснилось, что проверенный тогда артефакт был упакован неверно: после
`Image.gz` стоял весь Android DT-table, включая 64-байтовый wrapper перед raw
FDT. Поэтому тот `lk_crash` не опровергает корректный legacy layout. Probe и
v3 пересобраны с извлечённым raw FDT, дважды воспроизводимы, прошли обновлённый
offline preflight.

После этого исправленный probe SHA `0057d996...` был временно запущен и снова
дал LK crash до PID 1. Дополнительный header-match probe SHA `a1cdfde9...` с
побайтно штатной 2048-байтовой header page и штатными размерами payload дал тот
же результат. В обоих случаях probe USB `0112` отсутствовал, а установленный
known-good Linux автоматически вернулся. Свежий `/proc/aed/reboot-reason`
подтвердил `exception type: 6` (`AEE_EXP_TYPE_LK_CRASH`). Значит stale header
не объясняет отказ; проблема находится в LK downloaded-image path до запуска
ядра. V3 не запускался, marker gate отсутствует, разделы не изменялись.
