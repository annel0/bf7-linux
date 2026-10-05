# Операции и типовые проблемы

## Пакеты Debian

Обычная последовательность безопасна: она меняет persistent Debian rootfs, а не
boot-разделы или OEM kernel.

```sh
apt update
apt upgrade
```

Перед более широкими изменениями проверить план:

```sh
apt-get -s full-upgrade
apt-get -s autoremove
```

Не делать `full-upgrade`/`autoremove` вслепую. В Debian нет собственного
пакета ядра для этого устройства; это хорошо — `apt upgrade` не заменит
загружаемое MediaTek kernel image.

### `apt-key: cannot create /dev/null: Permission denied`

Причина — на системе без devtmpfs `/dev/null` мог оказаться обычным файлом.
Лечение:

```sh
rm -f /dev/null
mknod -m 666 /dev/null c 1 3
systemctl restart bf7-devnodes.service
```

Проверить `ls -l /dev/null`: ожидается `crw-rw-rw- 1 root root 1, 3`.

### `btop: No UTF-8 locale detected`

Это не ошибка железа. Включить локаль, например:

```sh
apt install locales
dpkg-reconfigure locales
export LANG=C.UTF-8
```

Или временно: `btop --utf-force`.

## Экран не обновляется

Проверить framebuffer и запуск программы:

```sh
ls -l /dev/fb0
cat /sys/class/graphics/fb0/{name,virtual_size,stride,bits_per_pixel}
cat /tmp/touch-paint.log
```

При direct framebuffer-рисовании обязательно вызвать `FBIOPAN_DISPLAY` на
`xoffset=0,yoffset=0`. Без этого mtkfb может отображать предыдущую виртуальную
страницу: запись успешна, но визуально ничего не меняется.

### После загрузки экран остаётся пустым

Основной экран — `bf7-term.service`. Он сам ждёт `/dev/fb0` и input nodes;
проверять его так:

```sh
systemctl --no-pager --full status bf7-term
journalctl -b -u bf7-term --no-pager
```

`bf7-status.service` намеренно отключён: одновременно с терминалом его не
запускать, поскольку оба процесса пишут в один framebuffer.

## Тач не выдаёт события

```sh
cat /sys/omnivision_tcm/info
ls -l /dev/input/event3
cat /proc/interrupts | grep -i omnivision
systemctl restart bf7-devnodes bf7-touch-init
```

Если `Firmware mode: Rom Bootloader`, чаще всего firmware недоступна на момент
первичной инициализации. Проверить:

```sh
sha256sum /lib/firmware/synaptics/hdl_firmware.img
systemctl restart bf7-touch-init.service
sleep 3
cat /sys/omnivision_tcm/info
```

Нормальный режим — `Host Download Firmware`; это не означает запись прошивки в
память телефона, firmware загружается в RAM touch-контроллера.

## Звука нет

```sh
systemctl restart bf7-devnodes bf7-audio-route
aplay -l
speaker-test -D hw:0,0 -c 2 -t sine -l 1
```

Не использовать `hw:0,30`: это dummy route и он возвращает `EINVAL`.

## Wi-Fi не подключился

```sh
systemctl --no-pager status bf7-wifi bf7-wpa bf7-dhcp
journalctl -u bf7-wifi -u bf7-wpa -u bf7-dhcp -b --no-pager
ip link show wlan0
```

При чистом перезапуске ConnSys лучше перезагрузить телефон, а не многократно
дёргать низкоуровневые vendor процессы в уже частично инициализированном
состоянии.

## Батарея: процент неизвестен, остальная телеметрия есть

На native Linux MTK gauge сейчас возвращает `capacity=-1` / `UI_SOC=65535`.
Это не означает, что батарея не работает: доступны `status`, напряжение, ток и
температура.

```sh
for f in status capacity voltage_now current_now current_avg temp charge_full; do
  printf '%s=' "$f"; cat "/sys/class/power_supply/battery/$f"
done
cat /sys/devices/platform/1000d000.pwrap/1000d000.pwrap:main_pmic/mt6357-gauge/UI_SOC
```

Не подставлять напряжение в качестве «точного процента»: под зарядкой и под
нагрузкой это особенно неточно. Вероятная причина — штатный Android battery
stack, который обычно инициализирует/калибрует UI_SOC, не запущен. Решение
требует отдельного разбора MTK gauge и NVRAM-калибровки; не писать в gauge sysfs
узлы без понимания их назначения.

## Не загружается native Debian

1. Не паниковать и не стирать данные.
2. Использовать Android slot `a` как rescue.
3. В fastboot сначала только читать статус и проверять активный слот.
4. Не использовать mtkclient без отдельного согласования.
5. При доступном USB-RNDIS проверять `192.168.7.2` и SSH как минимальный
   diagnostic path.
