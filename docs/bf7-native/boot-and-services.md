# Загрузка и постоянные сервисы

## Схема запуска

```text
boot_b
  -> штатное Android kernel 4.19.191 + custom initramfs
  -> mount /dev/mmcblk0p48 (ext4, Debian root)
  -> systemd PID 1
  -> device nodes, touch firmware, Wi-Fi, звук, SSH
```

Это даёт полноценный системный namespace и настоящий root в ядре. Ограничения
не от контейнера, а от старого OEM-ядра, проприетарных драйверов и отсутствия
devtmpfs.

## Важные сервисы

```sh
systemctl --type=service --all | grep -E 'bf7|sshd|timesync'
```

| Сервис | Назначение |
|---|---|
| `bf7-devnodes.service` | создаёт необходимые `/dev` nodes |
| `bf7-touch-init.service` | reset TCM после root mount и загрузка touch firmware |
| `bf7-wifi.service` | поднимает MediaTek ConnSys/Wi-Fi |
| `bf7-wpa.service` | подключает Wi-Fi |
| `bf7-dhcp.service` | получает адрес на `wlan0` |
| `bf7-audio-route.service` | ждёт ALSA nodes и включает физический путь к динамику |
| `bf7-term.service` | постоянный терминал на `/dev/fb0`; запускается при каждой native-загрузке |
| `bf7-status.service` | ручной dashboard на `/dev/fb0`; отключён из автозапуска |
| `ssh.service` | удалённая консоль |

OEM-ядро регистрирует устройства, но не монтирует `devtmpfs`. Поэтому
`bf7-wait-nodes` перед запуском экранного терминала и audio route вызывает
`bf7-devnodes` и ждёт реальные character nodes до 90 секунд. Не использовать
`ConditionPathExists=/dev/fb0` для критичного автозапуска: systemd проверяет
условие один раз и при раннем отсутствии framebuffer просто пропускает unit.

`bf7-term` и `bf7-status` нельзя держать одновременно: оба рисуют в единственный
`/dev/fb0`. При обычной загрузке используется терминал. Для временного запуска
dashboard сначала остановить терминал, а затем после завершения вернуть его:

```sh
systemctl stop bf7-term
systemctl start bf7-status
# ...
systemctl stop bf7-status
systemctl start bf7-term
```

Проверить всё одной командой:

```sh
systemctl --no-pager --full status \
  bf7-devnodes bf7-touch-init bf7-wifi bf7-wpa bf7-static-wifi \
  bf7-audio-route bf7-term ssh
```

## Диск, RAM, CPU

Корневая файловая система должна быть ext4 с UUID:

```text
e33080c1-b732-4240-85eb-7ff6fca3da3f
```

Её строка в `/etc/fstab` позволяет утилитам (например btop) правильно видеть
eMMC. Нормальные ориентиры: 3.72 GiB RAM, idle native userspace примерно
180–200 MiB, eMMC около 50 GiB.

CPUfreq работает штатно: `schedutil`, диапазон 850–2001 MHz. Частота растёт под
нагрузкой, менять governor без задачи не требуется.

```sh
cat /sys/devices/system/cpu/cpufreq/policy0/scaling_{governor,cur_freq,max_freq}
df -h /
free -h
```

## A/B boot control: важное ограничение

До выполнения early-init helper'а native boot получал из bootloader:

```text
androidboot.slot=b
androidboot.successful_boot=0
```

Android update_engine в Debian отсутствует, поэтому установленный native
`boot_b` (`bf7-native-debian-v20-mark-success.img`) выполняет аналог
`MarkBootSuccessful()` в раннем initramfs. Заводской boot-control HAL хранит
control block в Android `/misc`; на BF7 `/misc` отображён на GPT-раздел `para`
(`mmcblk0p3`).

Raw-доступ к `para` из позднего native Debian запрещён OEM MMC driver даже для
root. Поэтому helper запускается до `switch_root`; он валидирует Android BCB
magic, slot suffix и CRC32, а затем меняет только флаг successful для B и
пересчитывает CRC. Если B уже successful, helper ничего не записывает.

Проверено после установки: `current-slot: b`, `slot-successful:b: yes`,
`slot-unbootable:b: no`. Значение `slot-retry-count:b: 1` допустимо: успешный
slot не расходует оставшиеся попытки. Для дальнейшей read-only проверки:

```sh
fastboot getvar current-slot
fastboot getvar slot-successful:b
fastboot getvar slot-unbootable:b
fastboot getvar slot-retry-count:b
```

Не пытаться вручную менять BCB из позднего Debian или заменять v20 без
сохранения этой логики.
