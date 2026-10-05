# BF7 ALPS191: автономный первый boot-test

Дата: 2026-08-23. Этот runbook ничего не разрешает прошивать. Его задача —
сделать будущий тест наблюдаемым и обратимым без нажатия аппаратных кнопок.

## Неподвижная граница безопасности

1. Ни `boot_a`, ни `boot_b`, ни recovery не записываются.
2. Оба этапа используют только `fastboot boot`.
3. Trial v3 технически заблокирован, пока exact legacy probe не создаст
   маркер успешного Linux boot и возврата в bootloader fastboot.
4. Если новое ядро не дошло до PID 1, его собственный watchdog помочь не
   может. Безопасность в этом случае обеспечивается именно временной RAM
   загрузкой: аппаратный reset должен снова загрузить установленный
   known-good OEM-native образ.

## Этап 1: проверить downloaded-image path LK — выполнен, FAIL

Образ:

`native-linux-build/fastboot-ramboot-probe/bf7-oem-minimal-ramboot-probe-v0-img-gz-dtb.img`

SHA-256:

`0057d996861e3d47add4ceec683cf062b4500dde8eb3be9325437395f5edb008`

Первый исправленный вариант запускался командой:

```sh
native-linux-build/fastboot-ramboot-probe/run-host.sh \
  --temporary-ram-boot-only
```

Runner заранее запускает наблюдение, отвергает Android fastbootd, проверяет
точный SHA и выполняет только `fastboot boot`. Успех означает:

- появился USB `1d6b:0112`, serial `BF7RAMBOOTPROBE`;
- ответил root shell `192.168.7.2:2324`;
- через 60 секунд телефон программно вернулся в bootloader fastboot;
- создан `reports/LEGACY_PROBE_PASSED`, привязанный к exact SHA probe.

При достижении PID 1 также включается аппаратный watchdog: если software
return зависнет, после окончания 90-секундной lease и hardware timeout
последует reset к установленному known-good образу.

Фактический результат: PID 1 не достигнут. Исправленный raw-FDT probe и второй
probe с exact OEM header page/размерами оба дали `LK_CRASH` до запуска Linux.
Установленный known-good образ автоматически вернулся; разделы не менялись.
Повторять эти же probe без новой диагностической гипотезы не нужно.

## Этап 2: trial v3 — заблокирован

Образ:

`native-linux-build/alps191-trial-v3/bf7-alps191-trial-v3-ramboot-v0-img-gz-dtb.img`

SHA-256:

`6eb7049c09f02f99bbd259fa9f2d71866710626df56d8c4b9b5bbefddd0d1a42`

Это legacy header-v0 контейнер с новым `Image.gz` и извлечённым raw OEM FDT в
одном payload. Обычный header-v2 v3 сохранён только как reference artifact и
не используется для downloaded-image test.

Runner не запустится без маркера этапа 1. Маркер отсутствует, поэтому эту
команду сейчас выполнять нельзя:

```sh
native-linux-build/alps191-trial-v3/run-host.sh \
  --temporary-ram-boot-after-probe
```

Он запускает monitor до передачи образа, затем проверяет exact kernel release,
ждёт bounded read-only storage diagnostic, забирает report и dmesg, вызывает
raw return-to-fastboot и ждёт безопасное состояние. Безопасным состоянием
считается либо bootloader fastboot, либо ответ установленного OEM-native
kernel по SSH `192.168.27.140`.

## Что автоматически не запускается

- Wi-Fi/WMT модули;
- AW32257 charger module и любые его I2C register writes;
- GPU firmware/userspace;
- запись ext4 или любого block device;
- mark-boot-success, смена A/B slot и любая fastboot flash/erase команда.

Wi-Fi и charger проверяются только следующими отдельными этапами после
успешного baseline. Charger loader дополнительно требует literal аргумент
`--load-with-register-writes` и не должен выгружаться через `rmmod`.

## Интерпретация результата

- Legacy probe не появляется и снова оставляет `lk_crash`: downloaded-image
  path LK остаётся непригодным; trial v3 не запускать и тем более не шить.
- Legacy probe проходит, v3 не доходит до shell, затем возвращается known-good:
  ранний дефект нового kernel; анализировать UART/pstore, образ не шить.
- V3 доходит до shell, но storage READY не появляется: сохранить dmesg,
  выполнить software return, разбирать MSDC/eMMC; образ не шить.
- V3 получает READY и возвращается: только тогда доказан первый baseline boot.
  Это ещё не разрешение прошивки; сначала отдельно проверяются повторный
  временный boot, framebuffer, watchdog reset и Wi-Fi.

## Надёжный переход из native Linux в fastboot

Использовать:

```sh
native-linux-build/storage-diagnostic-v1/enter-fastboot-host.sh
```

Скрипт сначала пассивно наблюдает sysfs USB и фиксирует `0e8d:201c`, а только
после этого запускает единственный fastboot client. Это устраняет прежнюю
гонку, когда несколько циклов `fastboot devices` конкурировали за один USB
интерфейс и сообщали ложный `FASTBOOT_NOT_OBSERVED` уже при видимом fastboot.
Он проверяет exact SHA helper и не выполняет flash/erase/set-active.
Живой контрольный прогон подтвердил распознавание всей USB-цепочки и готового
bootloader fastboot; последующий `fastboot reboot` вернул installed
known-good Linux без физического вмешательства.
