# BF7 LK: анализ `fastboot boot`

Дата: 2026-08-23. Документ включает offline-анализ и два временных
hardware-прогона; разделы телефона не записывались.

## Подтверждённое

- Текущий LK сохранён побайтно в `firmware-backup/live-native/lk/`.
- В его бинаре присутствует отдельный `cmd_boot`, Android boot header parser,
  проверки kernel/ramdisk address, DTB/DTBO merge и строки всех ошибок.
- Реальный неуспешный `fastboot boot` оставил следующую штатную загрузку с
  `androidboot.bootreason=lk_crash`. Тот же bootreason остаётся в текущем
  known-good Linux; это след предыдущей аварии LK, не текущая авария ядра.
- Exact known-good header-v2 image и полный 32 MiB backup не доказали
  работоспособность downloaded-image path. Поэтому ошибка не может быть
  приписана только новому ALPS kernel.
- Рабочий OEM kernel имеет `CONFIG_KEXEC=n`; безопасного chainload из Debian
  нет.

## Сильная гипотеза формата

Старые MediaTek LK реализации `cmd_boot` работают только с legacy полями
boot header: копируют kernel и ramdisk, а DTB v2 могут не переносить в адрес,
который затем использует `boot_linux`. Бинар BF7 заметно новее старого
публичного source и содержит v2/DTBO функции, поэтому это ещё не доказанная
причина именно нашего crash.

Для отделения header-v2/DTB path от остальных причин был подготовлен
минимальный probe:

`native-linux-build/fastboot-ramboot-probe/bf7-oem-minimal-ramboot-probe-v0-img-gz-dtb.img`

Он использует точный working OEM kernel, минимальный initramfs без storage,
legacy header v0 и традиционный `Image.gz-dtb`. Сборщик извлекает из Android
DT-table единственный raw FDT (offset 64, size 121167, magic `d00dfeed`) и
добавляет именно его. SHA-256 исправленного probe:
`0057d996861e3d47add4ceec683cf062b4500dde8eb3be9325437395f5edb008`.
Исправленный raw-FDT probe был запущен и снова завершился аварией LK до
появления PID 1. Поэтому отдельно был подготовлен второй probe:

`native-linux-build/fastboot-ramboot-probe/bf7-oem-minimal-ramboot-probe-v2-header-match.img`

SHA-256:
`a1cdfde9cef238b3a3319f2e172b8dd15ab27ef13eb8552bc0f057e63439c160`.
Его первые 2048 байт побайтно совпадают с установленным рабочим boot image;
размеры kernel, DT-table и сжатого ramdisk совпадают со штатными. Минимальный
ramdisk дополнен нулями до OEM-размера, `gzip -t` проходит.

Сборка дважды воспроизведена после нормализации cpio metadata. Reverse unpack
подтвердил точные `Image.gz-dtb` и ramdisk; отдельный preflight сверяет OEM
kernel, OEM DTB, full known-good backup, header v0 и отсутствие storage path
в PID 1. При достижении PID 1 probe поднимает отдельный RNDIS ID `1d6b:0112`,
root shell `192.168.7.2:2324`, через 60 секунд вызывает raw reboot-to-fastboot
и дополнительно держит OEM hardware watchdog с конечной 90-секундной lease.

## Граница безопасности

Оба probe были временно загружены только командой `fastboot boot`; их нельзя
прошивать. Оба дали `lk_crash`, поэтому downloaded-image path этой версии LK
считается непригодным. Запуск ALPS candidate заблокирован, а запись candidate
в A/B по-прежнему запрещена.

Публичный source, близкий по строкам и алгоритму:
[MediaTek LK `sys_commands.c`](https://github.com/l33tnoob/lk_mtk/blob/master/bootable/bootloader/lk/app/mt_boot/sys_commands.c).

## Аппаратный результат 2026-08-23

Предыдущий legacy probe SHA
`604691a881cc0a526b4ccedd8753ddc3b2a3449049b57fa2bfc4f36f5d1cca54`
был временно отправлен из настоящего bootloader fastboot (`is-userspace=no`).
Передача 12,998 KiB завершилась `OKAY`, но команда boot оборвалась:

```text
Booting FAILED (Status read failed (No such device))
```

Наблюдавшаяся USB-последовательность:

```text
0e8d:201c fastboot -> 0e8d:2000 Preloader -> disconnect ->
1d6b:0111 installed OEM-native Linux
```

`1d6b:0112 BF7RAMBOOTPROBE` не появился ни разу, shell 2324 не ответил.
После возврата установленный kernel подтвердил
`androidboot.bootreason=lk_crash`.

Поздний разбор упаковки обнаружил дефект этого конкретного probe: после
`Image.gz` был добавлен весь Android DT-table (`d7b7ab1e`), а не его raw FDT.
LK находил вложенный FDT в конце, но после gzip-потока оставался 64-байтовый
table header. Поэтому этот runtime-результат доказывает аварийность только
старого артефакта и **не опровергает** исправленный header-v0 + raw-FDT layout.

Телефон автоматически и безопасно вернулся к установленному
`4.19.191-g216400aba14e-dirty`, slot `_b`; разделы в тесте не записывались.
Маркер `LEGACY_PROBE_PASSED` не создан, поэтому v3 runner остаётся технически
заблокирован.

## Повторные аппаратные результаты 2026-08-23

Исправленный legacy v0 + raw-FDT probe SHA `0057d996...` был отправлен через
настоящий bootloader fastboot. Передача 12,998 KiB получила `OKAY`, затем
команда boot снова потеряла USB transport. Probe `1d6b:0112` не появился;
после Preloader загрузился установленный `1d6b:0111`. Read-only проверка
`/proc/aed/reboot-reason` показала:

```text
WDT status: 6 fiq step: 71 exception type: 6
```

В reference LK enum значение 6 соответствует `AEE_EXP_TYPE_LK_CRASH`. Это
локализует отказ до передачи управления Linux PID 1.

Затем был отправлен header-match probe SHA `a1cdfde9...`. Передача 16,972 KiB
также получила `OKAY`, после чего вернулась та же ошибка transport. Через
92 секунды после следующей загрузки подтверждены установленный OEM kernel,
PID 1 systemd, root `/dev/mmcblk0p48`, slot B и свежий
`androidboot.bootreason=lk_crash`; `/proc/aed/reboot-reason` снова содержит
`exception type: 6`.

Следовательно, исправленный raw FDT не решил отказ, а точное совпадение header
page и размеров исключает stale/cached downloaded header как достаточное
объяснение. Наиболее узкая текущая формулировка: vendor LK падает внутри
downloaded-image `cmd_boot` до запуска ядра. Без UART/crash-PC или патча LK
точную инструкцию отказа пока определить нельзя. Custom ALPS kernel в этих
прогонах вообще не выполнялся, поэтому эти результаты ничего не говорят о его
работоспособности.

## Разница slot boot и `fastboot boot`

Близкий MediaTek LK source и строки/логи BF7 показывают два разных пути:

- normal A/B boot вызывает `load_vfy_boot()`, применяет AVB policy, загружает
  boot image из раздела, вызывает `load_bootinfo_bootimg()`, затем
  `prepare_kernel_dtb()` и только после этого `boot_linux()`;
- downloaded `fastboot boot` попадает в отдельный `cmd_boot()`: проверяет
  header, размеры и диапазоны RAM, вручную переносит ramdisk и вызывает
  `boot_linux()`. В reference реализации после `load_bootinfo_bootimg()` нет
  вызова `prepare_kernel_dtb()`.

Это не дополнительная проверка способности ядра загрузиться. Наоборот, normal
slot path полнее. Команда `boot` зарегистрирована как разрешённая при hardware
secure boot только на unlocked устройстве; внутри `cmd_boot()` AVB verification
нет. На BF7 normal log сообщает `img_auth_required=0`, а verified-boot state —
orange. При signature/policy rejection ожидался бы fastboot `FAIL`, тогда как
наблюдается авария самого LK.

Normal BF7 log подтверждает отдельные операции `load_lk_dtb`, загрузку
`dtbo_b`, выбор index 0 и успешный `dtb_overlay`. Поэтому отсутствие
`prepare_kernel_dtb()` в downloaded path сейчас является наиболее сильной
конкретной гипотезой:

- exact OEM header-v2 и полный known-good boot всё равно требуют merge/copy
  DTB в конечный tags buffer;
- исправленный legacy v0 probe содержал только base FDT, но не результат
  factory DTBO overlay;
- обе разновидности поэтому могут дойти до `boot_linux_fdt()` с неполным или
  неподготовленным FDT и уронить LK до jump в EL1.

Точный crash PC не сохранён в доступных `/proc/aed` или pstore, поэтому это
ещё не побайтовое доказательство конкретной инструкции. Доказано лишь, что
отказ находится в LK до probe PID 1 и не зависит от нового ALPS kernel.

## Следующий безопасный обход

Рабочий OEM-native Linux отдаёт уже слитый FDT через `/sys/firmware/fdt`:

```text
size    229877
SHA256  bd867f48c093ce8ede11679cc1b0e634c3e48888d049a533576dc679966b2f0c
magic   d00dfeed
model   MT6761V/WBB
dtbo    androidboot.dtbo_idx=0
```

Следующий минимальный эксперимент должен использовать exact OEM kernel,
minimal ramdisk и этот runtime-merged FDT в legacy `Image.gz-dtb`. Это
сохраняет временный `fastboot boot`, но заранее выполняет ту операцию, которую
vendor `cmd_boot()` вероятно пропустил. Только если OEM probe достигнет PID 1,
тот же контейнерный layout можно применить к ALPS candidate.

Другие варианты по возрастанию риска: загрузка candidate из неактивного A/B
слота через normal storage path (требует persistent write, backup и доказанный
rollback); recovery-as-boot с той же проблемой persistent write; патч LK
(практически исключён hardware secure boot); BootROM/DA path (отдельная
опасная ветка, MediaTek tooling нельзя применять без явного предупреждения и
разрешения). `kexec` недоступен, потому что installed kernel имеет
`CONFIG_KEXEC=n`.
