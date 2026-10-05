# BF7 recovery-RNDIS и аварийный root-shell

Этот документ фиксирует канал, через который можно обслуживать native Linux на
TECNO BF7 даже тогда, когда постоянный Debian rootfs не смонтировался. Канал
работает из initramfs и не зависит от Wi-Fi, `systemd`, SSH или Android.

## Архитектура канала

Initramfs поднимает USB gadget со следующими параметрами:

```text
USB VID:PID: 1d6b:0111
USB product: BF7 native Debian recovery RNDIS
host:        192.168.7.1/24
phone:       192.168.7.2/24
shell:       TCP/2323, BusyBox telnetd
```

Shell запускается от `uid=0`. Это root в текущем Linux kernel/initramfs, а не
Android `adb shell` и не контейнер. В recovery-состоянии доступны `/proc`,
`/sys`, USB networking и те device nodes, которые создал initramfs. Постоянный
Debian может быть недоступен, если его eMMC-раздел не смонтировался.

Основной сценарий появления этого shell:

1. kernel загружает initramfs;
2. initramfs монтирует `/proc` и `/sys`;
3. настраивает USB RNDIS;
4. назначает телефону `192.168.7.2/24`;
5. запускает BusyBox `telnetd` на TCP/2323 с root-shell;
6. только после этого пытается смонтировать Debian rootfs.

Поэтому RNDIS остаётся доступным даже при сообщении
`BF7: Debian root mount failed; recovery RNDIS remains available.`

## Подготовка интерфейса на ПК

Имя интерфейса обычно `enp22s0f0u9`, но его нужно проверять после каждой новой
USB-энумерации:

```sh
lsusb | grep '1d6b:0111'
ip -brief link | grep -E 'enp.*u9|usb'
```

Назначение адреса:

```sh
sudo nmcli device set enp22s0f0u9 managed no
sudo ip addr replace 192.168.7.1/24 dev enp22s0f0u9
sudo ip link set enp22s0f0u9 up
ping -c 1 -W 2 192.168.7.2
```

На recovery-RNDIS нет DHCP-сервера. Если не назначить `192.168.7.1/24` на ПК,
телефон может быть виден в `lsusb`, но TCP-соединение не заработает.

## Почему простой `nc` неудобен

Порт 2323 — telnet, а не чистый raw TCP. При подключении сервер отправляет
telnet IAC negotiation и запрос позиции курсора:

```text
ff fb 01 ff fb 03 ff fd 1f ... ESC [ 6 n
```

Клиент должен:

- отвечать `DO` на `WILL`;
- отвечать `WONT` на `DO`;
- отвечать `ESC[1;5R` на `ESC[6n`.

Без этого приглашение может быть видно, но ввод иногда не принимается.

У line editor есть дополнительная особенность: длинную команду безопаснее
печатать с задержкой около `0.10-0.15` секунды на символ, затем подождать
примерно пять секунд и только после этого отправить Enter. Если послать строку
и Enter одним пакетом, Enter периодически проглатывается.

Для ручной диагностики допустимо:

```sh
telnet 192.168.7.2 2323
```

Для автоматизации нужен небольшой telnet-aware клиент с обработкой IAC и
`ESC[6n`; простой `printf ... | nc` нельзя считать надёжным.

## Восстановление зависшего USB gadget

Если ping работает нестабильно или TCP/2323 перестал отдавать приглашение,
сначала сбрасывается только USB-устройство на ПК. Телефон при этом не
перезагружается:

```sh
dev=$(lsusb | awk '/1d6b:0111/ {
    gsub(":", "", $4); print $2 "/" $4; exit
}')
sudo /usr/bin/usbreset "$dev"
```

После reset необходимо снова проверить имя интерфейса и назначить
`192.168.7.1/24`. USB reset между большими telnet-сессиями заметно повышает
надёжность этого конкретного gadget/UDC.

## Минимальная read-only диагностика

Когда внешние BusyBox applets зависают, использовать shell builtins. Примеры:

```sh
read -r V </proc/version; echo "$V"
read -r C </proc/cmdline; echo "$C"
read -r U </proc/uptime; echo "$U"
```

Текущий слот определяется по `/proc/cmdline`:

```text
androidboot.slot_suffix=_b androidboot.slot=b
```

Названия eMMC-разделов можно получить из sysfs:

```sh
grep -H PARTNAME=boot /sys/class/block/mmcblk0p*/uevent
```

Для BF7 подтверждено:

```text
mmcblk0p23 = boot_a
mmcblk0p38 = boot_b
mmcblk0p3  = para (Android /misc, BCB)
```

Нельзя вычислять `dev_t` большого номера раздела как `179:<partition>`.
Например, у `mmcblk0p38` фактический `dev_t` был `259:6`. Источник истины:

```sh
read -r D </sys/class/block/mmcblk0p38/dev; echo "$D"
```

При отсутствии devtmpfs старый вручную созданный `/dev/mmcblk0p38` может
указывать не туда. Для raw-утилиты создаётся временный block node с `dev_t` из
sysfs и уникальным именем.

## Передача бинарника без `wget`

Для маленьких rescue-утилит используется только уже открытый telnet-shell.
Бинарные данные кодируются на ПК как последовательности `\\ooo`, а BusyBox
shell восстанавливает их builtin-командой `printf`.

Схема приёма на телефоне:

```sh
echo LOAD_READY
while read -r -s L; do
    [ "$L" = END ] && break
    printf "$L"
    echo ACK >&2
done >/bin/bf7-mark-boot-success
echo LOAD_DONE
```

Практические правила:

- отправлять примерно по 400-500 исходных байт на строку;
- ждать `ACK` после каждой строки;
- завершать отдельной строкой `END`;
- перезаписывать существующий executable только в RAM, чтобы сохранился mode
  `+x`;
- после холодной загрузки все такие изменения initramfs исчезают.

Этот способ использовался для no-libc AArch64 ELF размером от 1 до 4 KiB. Для
больших файлов лучше запускать переданный raw TCP receiver и передавать payload
отдельным TCP-потоком с точным размером и ACK.

Исходники утилит:

```text
native-linux-build/tools/bf7-raw-boot-receiver.S
native-linux-build/tools/bf7-raw-boot-dump.S
native-linux-build/tools/bf7-raw-root-probe.S
native-linux-build/tools/bf7-raw-reboot-bootloader.S
native-linux-build/tools/bf7-raw-para-dump.S
native-linux-build/tools/bf7-raw-set-slot-a.S
native-linux-build/tools/bf7-raw-bcb-fastboot.S
```

## Что показала попытка reboot в fastboot

Был напрямую вызван:

```c
reboot(LINUX_REBOOT_CMD_RESTART2, "bootloader");
```

Syscall был принят: соединение оборвалось, появился MediaTek Preloader
`0e8d:2000`, но затем загрузчик снова запустил slot B и recovery-RNDIS.

Причина понятна из donor source/config: arm64 restart выполняется через PSCI
`SYSTEM_RESET`, а `psci_sys_reset()` не использует строку `cmd`. Следовательно,
`RESTART2` на этом экспериментальном ядре превращается в обычный warm reboot и
сам по себе не выставляет fastboot flag.

Первая попытка использовала неверную для Android 12 строку `boot-fastboot`.
Последующий локальный аудит штатной BF7 `libbootloader_message.so` доказал
реальный формат: функция `write_reboot_bootloader()` обнуляет 2048-байтный
`bootloader_message`, записывает в поле `command` строку
`bootonce-bootloader` и целиком пишет структуру в `/misc` (`para`) со смещения
0.

Этот точный формат был записан raw helper'ом через правильный `dev_t` `179:3`
и `fsync`; helper сообщил `BF7_BCB_WRITTEN`. После warm reboot и затем после
настоящего cold power cycle fastboot не появился. Read-only проверки после
обоих запусков показали, что `bootonce-bootloader` осталась в `para` без
изменений. Значит, LK этой BF7 не читает Android BCB на проверенных путях.

## Правила безопасности boot-экспериментов

1. Экспериментальный образ проверяется только командой `fastboot boot
   candidate.img`.
2. Не прошивать непроверенное ядро в `boot_a` или `boot_b`.
3. До теста сохранить полный образ известного рабочего boot-раздела и SHA-256.
4. После временной загрузки проверить kernel version, RNDIS, rootfs, SSH,
   экран и возможность возврата в fastboot.
5. Не использовать `mtkclient` без отдельного явного предупреждения и нового
   согласия владельца.
6. Не писать в PMIC/RTC, GPT, `boot_para`, `para` или boot-control metadata без
   точного формата и заранее проверенного recovery-пути.

## Зафиксированное состояние аварии 2026-08-22

- активен slot B;
- реально работает donor kernel
  `4.19.325-cip124-st8-g75934cf7f57b-dirty`;
- постоянный Debian rootfs не монтируется;
- recovery-RNDIS и root telnet-shell работают;
- запись полного backup в предполагаемый `boot_b` получила ACK, но после
  холодного запуска donor kernel остался, поэтому восстановление нельзя считать
  доказанным;
- прямой `RESTART2("bootloader")` дал обычную перезагрузку, не fastboot;
- точный Android 12 BCB `bootonce-bootloader` пережил warm и cold reboot
  неизменённым; этот путь входа в fastboot закрыт;
- Android/slot A нельзя запускать без отдельного решения владельца;
- `mtkclient` запрещён без нового явного согласия.

## Переключение на slot A для штатного recovery

Read-only чтение 32 байт `bootloader_control` из `para` со смещения 2048
показало:

```text
old = 5f62000042434142010200009e009f000000000300000004000024006e281478
suffix = _b
A = priority 14, tries 1, successful 1
B = priority 15, tries 1, successful 1
stored CRC32 = calculated CRC32 = 0x7814286e
```

Lossless snapshot сохранён в
`firmware-backup/live-native/bootloader-control-before-slot-a-20260822.hex`.

Helper `bf7-raw-set-slot-a.S` потребовал полного совпадения old snapshot,
очистил только BCB `[0..2047]`, минимально поменял suffix/приоритеты и записал:

```text
new = 5f61000042434142010200009f009e000000000300000004000024004a75e7f8
suffix = _a
A = priority 15, tries 1, successful 1
B = priority 14, tries 1, successful 1
stored CRC32 = calculated CRC32 = 0xf8e7754a
```

Получен `BF7_SLOT_A_READY` после `fsync` и внутреннего readback. Отдельный
read-only `dd | od` подтвердил 32 нулевых байта в начале BCB и точное значение
new control выше. Helper намеренно не перезагружал устройство.

Для входа в сохранившийся recovery слота A следующий старт должен выполняться
физической комбинацией Power + Volume Up. Эта комбинация уже доказанно задаёт
`boot_mode=2`; при обычном старте того же состояния было `boot_mode=0`.

## Итог восстановления 2026-08-23

После активации A физическая комбинация Power + Volume Up загрузила штатный
recovery слота A. Пункт `Enter fastboot` поднял userspace fastbootd:

```text
USB = 18d1:4ee0
serial = 10476373A5000145
is-userspace = yes
current-slot = a
unlocked = yes
partition-size:boot_b = 0x2000000
```

Перед восстановлением полный backup был заново проверен:

```text
file = firmware-backup/live-native/boot_b-before-kernel-lcm-test-20260822-r2.img
size = 33554432
SHA-256 = 337a456e3835cac210bae417c9ed67a18997f5a1b192880a5c84447e8bdcbc2a
kernel = 4.19.191-g216400aba14e-dirty
kernel SHA-256 = 3cc9f88b1930660d344c9a97829f044d436e90b2896a0a03adb37ed339fd1db8
```

В fastbootd выполнены только восстановительные операции:

```text
fastboot flash boot_b <full-backup>
fastboot set_active b
```

Fastboot подтвердил `current-slot: b`, `slot-successful:b: yes` и
`slot-unbootable:b: no`. После reboot подтверждены:

```text
kernel = OEM 4.19.191-g216400aba14e-dirty
rootfs = /dev/mmcblk0p48, ext4
SSH USB = root@192.168.7.2
SSH Wi-Fi = root@192.168.27.140
```

Авария закрыта. Slot A и его штатный recovery не прошивались. Правило для всех
следующих экспериментальных ядер остаётся жёстким: только `fastboot boot`, до
любой записи в boot-раздел.
