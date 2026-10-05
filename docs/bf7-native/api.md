# Локальный hardware API

Все команды ниже выполняются от `root` в native Debian.

## Особенность устройства: нет devtmpfs

OEM-ядро регистрирует устройства в sysfs, но не создаёт все `/dev/*` автоматически.
Сервис `bf7-devnodes.service` создаёт нужные character device nodes при загрузке.

```sh
systemctl status bf7-devnodes.service
/usr/local/sbin/bf7-devnodes       # безопасно запустить повторно
```

Критичные ноды:

```text
/dev/null                 c 1:3
/dev/full                 c 1:7
/dev/fb0                  c 29:0
/dev/input/event3         c 13:67  (тач; номер следует брать из sysfs при изменении)
/dev/snd/*                ALSA card 0
/dev/dri/card0            DRM display node
/dev/dri/renderD128       DRM render node; пока неработоспособен
```

## Экран: raw framebuffer

Интерфейс — `/dev/fb0`, драйвер `mtkfb`. Текущий режим:

```text
visible:       720 x 1612
virtual:       736 x 4896
stride:        2944 bytes (736 pixels)
pixel format:  32 bpp
```

У mtkfb три виртуальные страницы. После записи в page 0 нужно вызвать
`FBIOPAN_DISPLAY` с `xoffset=0`, `yoffset=0`, иначе панель может продолжать
показывать старую страницу. Именно это является обязательным «flush» для
простых raw framebuffer-программ.

Минимальный API:

```c
ioctl(fbfd, FBIOGET_VSCREENINFO, &var);
ioctl(fbfd, FBIOGET_FSCREENINFO, &fix);
void *pixels = mmap(NULL, fix.line_length * var.yres_virtual,
                    PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
/* записать пиксели в page 0, с шагом fix.line_length */
var.xoffset = 0; var.yoffset = 0; var.activate = FB_ACTIVATE_NOW;
ioctl(fbfd, FBIOPAN_DISPLAY, &var);
```

Готовая тестовая программа:

```sh
/usr/local/bin/touch-paint                # event3 -> красные точки на fb0
pkill -f '^/usr/local/bin/touch-paint'     # остановить
cat /tmp/touch-paint.log
```

Исходник хранится локально в `native-linux-build/touch-paint.c`.

## Тачскрин

Контроллер: Synaptics/OmniVision TCM по SPI. Ядро выдаёт стандартный Linux
multitouch input device `mtk-tpd`.

```sh
cat /proc/bus/input/devices | sed -n '/mtk-tpd/,+8p'
cat /sys/omnivision_tcm/info
```

Формат потока — стандартный `struct input_event`; на aarch64 это 24 байта:

```c
struct input_event { struct timeval time; __u16 type; __u16 code; __s32 value; };
```

Используемые события:

| type/code | Имя | Значение |
|---|---|---|
| `EV_ABS/0x2f` | `ABS_MT_SLOT` | текущий слот пальца |
| `EV_ABS/0x39` | `ABS_MT_TRACKING_ID` | ID касания, `-1` — отпускание |
| `EV_ABS/0x35` | `ABS_MT_POSITION_X` | X, диапазон 0..719 |
| `EV_ABS/0x36` | `ABS_MT_POSITION_Y` | Y, диапазон 0..1611 |
| `EV_SYN/SYN_REPORT` | конец пакета | отрисовывать накопленные координаты |

После холодного запуска контроллер теряет firmware и стартует в ROM bootloader.
`bf7-touch-init.service` после монтирования rootfs делает reset контроллера;
драйвер загружает заводской `hdl_firmware.img` в RAM.

```sh
systemctl status bf7-touch-init.service
cat /sys/omnivision_tcm/info
# Нормальный результат: Firmware mode: Host Download Firmware
```

Firmware находится в `/lib/firmware/synaptics/hdl_firmware.img` и соответствует
образу из заводского `vendor_a.img`. Это volatile firmware контроллера, не
прошивка раздела телефона.

## Звук

ALSA-карта: `mtsndcard` (card 0). Список PCM:

```sh
aplay -l
```

Рабочий динамик использует реальный playback `hw:0,0`, а не dummy `hw:0,30`.

```sh
speaker-test -D hw:0,0 -c 2 -t sine -l 1
```

Штатный Android mixer нужен для включения аналогового усилителя. Маршрут
постоянно применяет `bf7-audio-route.service`:

```sh
systemctl status bf7-audio-route.service
/usr/local/sbin/bf7-audio-route
```

Он включает `Speaker_Amp_Switch`, `Ext_Speaker_Amp_Switch`,
`Audio_Amp_L_Switch`, `Audio_Amp_R_Switch` и `AUD_CLK_BUF_Switch` через
заводской `tinymix`. Для ручной диагностики:

```sh
LD_LIBRARY_PATH=/system/lib64:/apex/com.android.runtime/lib64/bionic \
  /system/bin/tinymix | grep -Ei 'Speaker|Amp|CLK'
```

## GPU и камера

`/dev/dri/card0` и `/dev/dri/renderD128` создаются, но GPU render node сейчас
возвращает `No such device`; hardware 3D API пока отсутствует. Framebuffer
использовать можно независимо от GPU.

Стандартных `/dev/video*` нет. Камера требует отдельной интеграции Android HAL
или разбора проприетарного ISP-интерфейса; не считать её доступной из Linux.

