# Инвентаризация OEM kernel: что можно поднять без Android

Дата первичного аудита: 2026-08-13. Ядро: `4.19.191-g216400aba14e-dirty`,
MediaTek MT6761V/WBB.

## Граница и безопасность аудита

Это **пассивный** срез. На работающем телефоне были прочитаны только
`/proc/config.gz`, `/proc/modules`, `/proc/devices`, `/proc/interrupts`,
списки `/sys/class`/driver bindings и существующая сетeвая metadata. Никакой
`/dev/*` character node не открывался; не читались PowerVR/AppHint sysfs,
не загружались модули, не запускались Android/vendor daemons и не менялись
состояния устройств.

Factory `vendor_a.img` исследовался локально, read-only через `debugfs`.

## Главный вывод: драйверы не нужно «извлекать»

Почти все интересующие драйверы уже **встроены** в OEM kernel. В `/proc/modules`
вынесены лишь Wi-Fi/ConnSys-компоненты:

```text
wlan_drv_gen4m, wmt_chrdev_wifi, wmt_drv, connfem
```

Остальные подсистемы нужно оценивать так:

```text
kernel driver -> firmware / device node -> vendor daemon or HAL -> Linux API
```

Переносить `.ko` имеет смысл лишь там, где factory специально оставляет модуль.
Модуль должен совпадать с этим точным kernel build; он не является переносимым
драйвером для произвольного Linux.

## Почему `/dev` требует нашего helper

В опубликованной kernel config:

```text
# CONFIG_DEVTMPFS is not set
CONFIG_MODULES=y
CONFIG_ANDROID_BINDER_IPC=y
CONFIG_ASHMEM=y
CONFIG_ION=y
```

Следовательно, ядро регистрирует device numbers в sysfs, но само не создаёт
`/dev` nodes. Это объясняет `bf7-devnodes`/`bf7-wait-nodes`.

Не следует автоматически материализовать **все** nodes из `/sys/class`: создание
char device не активирует драйвер, но последующее случайное открытие custom CCCI,
camera, GPU или TEE node может иметь непредсказуемый ioctl/protocol side effect.
Добавлять node нужно только для выбранного эксперимента и только после
read-only анализа его ABI.

## Матрица возможностей

| Подсистема | Доказательство в ядре | Внешняя зависимость | Native-оценка |
|---|---|---|---|
| eMMC, ext4, CPUfreq, thermal | `mmc`, `CONFIG_MTK_CPU_FREQ`, `CONFIG_MTK_THERMAL` | нет | готово |
| Display/fb | major `29 fb`, `CONFIG_MTK_FB`, `mtkfb@0` | `/dev/fb0`, userspace compositor | готово |
| Touch/keys | `omnivision_tcm`, input major `13`, IRQ | `hdl_firmware.img` в RAM TCM | готово |
| Audio | major `116 alsa`, `CONFIG_SND`, MT6357 | mixer route через factory `tinymix` | готово |
| Wi-Fi | `CONFIG_MTK_COMBO_WIFI`, WMT modules | заводские firmware + маленький WMT shim | готово |
| USB gadget/RNDIS | `musb-hdrc`, `android_usb`, UDC | configfs/initramfs setup | готово |
| Battery telemetry | `CONFIG_POWER_SUPPLY`, MT6357 gauge | нет для V/I/T/status | готово частично |
| Battery SOC | `mtk-gauge`, `fuelgauged`, `fuelgauged_nvram` | Android fuel-gauge/NVRAM flow | исследовать |
| Bluetooth | `CONFIG_MTK_BTIF`, `CONFIG_MTK_COMBO_BT` | `bt_drv_connac1x.ko`, BT firmware, HAL/HCI bridge | средне сложно |
| GNSS | `CONFIG_MTK_GPS_SUPPORT`, `CONFIG_MTK_COMBO_GPS` | `gps_drv.ko`, GNSS HAL, `mtk_agpsd` | средне сложно |
| LTE data/modem | `CONFIG_MTK_ECCCI_*`, `CONFIG_MTK_NET_CCMNI` | modem bootstrap + CCCI daemon/RIL | сложно, но перспективно |
| Camera | `CONFIG_VIDEO_V4L2=y`, `CONFIG_MTK_IMGSENSOR`, ISP | camera HAL/server, proprietary sensor/ISP ABI | сложно |
| Video codec | `CONFIG_MTK_VIDEOCODEC_DRIVER` | ION + Android C2/OMX HAL | сложно, низкий приоритет |
| Sensors | MTK sensor hub + custom sensor major `253` | Android multihal / vendor ioctl ABI | средне-сложно |
| FM radio | `fmradio_drv_mt6631.ko`, MT6631 firmware | module + factory daemon/protocol | средне сложно |
| GPU PowerVR | DRM config, Rogue driver/firmware | exact DDK userspace; known risky sysfs behavior | отложить |
| Fingerprint/TEE | custom nodes, Trustonic | HAL, Binder/TEE protocol | нецелесообразно |

## Детали по наиболее интересным неизвестным

### LTE / modem

Ядро содержит `ccci_load_firmware`, CCCI и CCMNI. В sysfs зарегистрированы
custom major `504 ccci`, status major `252`, а также `ccmni0..20`, но все они
сейчас `operstate=down`. Это ожидаемо: native Debian не запускает factory flow:

```text
/vendor/bin/ccci_mdinit 0
/vendor/bin/ccci_rpcd
/vendor/bin/hw/mtkfusionrild -l libmtk-ril.so
```

`ccci_mdinit` готовит modem side, а `mtkfusionrild` — Android RIL с Binder,
properties, radio users/groups и Android sockets. Наличие CCCI значит, что
модем **не отсутствует**; но «просто создать `/dev/ttyC*`» недостаточно.

Первая будущая стадия — только статически восстановить, откуда kernel берёт
modem firmware и какие files/Android properties нужны `ccci_mdinit`. Потом —
отдельный эксперимент только с SIM и заранее подготовленным recovery path.

### Bluetooth и GNSS

Factory хранит точные модули `bt_drv_connac1x.ko` и `gps_drv.ko`; init scripts
грузят их после `vendor.connsys.driver.ready=yes`. Встроены BTIF и shared
ConnSys; Wi-Fi уже доказал, что основная radio-подсистема работает native.

Однако factory затем запускает Android HIDL services:

```text
android.hardware.bluetooth@1.1-service-mediatek
android.hardware.gnss-service.mediatek
mtk_agpsd
```

Значит реалистичный путь — не Android UI, а маленький native bridge: для BT
довести модуль/firmware до стандартного HCI и использовать BlueZ; для GNSS
найти безопасный Linux-facing endpoint после `gps_drv`. Оба требуют сначала
разбора ABI, а не blind `insmod`.

### Camera

Kernel config включает `CONFIG_MEDIA_SUPPORT=y`, `CONFIG_VIDEO_V4L2=y`,
`CONFIG_MTK_IMGSENSOR=y`, camera ISP и image-sensor platform drivers. Но
`/sys/class/video4linux` и `/sys/class/media` сейчас пусты: готового
`/dev/video*` / media graph нет. Вместо него присутствуют custom camera/ISP
majors, а factory запускает `camerahalserver` с Android camera service model.

Вывод: камера не «отвалилась», но это не обычная V4L2 camera. Путь без Android
возможен только после reverse engineering custom ioctl pipeline либо при
пересборке/адаптации camera kernel stack. Это высокая сложность.

### Battery SOC

Честные `status`, напряжение, ток и температура уже доступны через
`power_supply`. `capacity=-1`, `UI_SOC=65535`. Factory запускает маленькие
`fuelgauged` и `fuelgauged_nvram`; вероятнее всего они либо инициализируют
gauge/NVRAM, либо передают калибровку Android health stack. Это лучший
следующий кандидат: ограниченная область, реальная польза серверу и нет
потребности открывать опасные character devices.

### Sensors, LED, vibrator

Сенсоры зарегистрированы не как обычный IIO API, а как MTK custom sensor
nodes (`m_acc_misc`, `m_gyro_misc`, `m_mag_misc` и др., major `253`) плюс
Android Sensors multihal. Их binary ioctl ABI пока неизвестен.

LED/backlight/vibrator имеют kernel class/platform drivers и выглядят заметно
проще; однако запись в соответствующий sysfs — это реальное включение железа.
Пока достаточно считать их перспективным low-risk проектом после отдельного
read-only ABI обзора.

## Рекомендуемый порядок следующих исследований

1. **Battery SOC:** статически разобрать `fuelgauged`/`fuelgauged_nvram`, их
   dynamic dependencies, свойства и NVRAM file paths. Ничего не запускать.
2. **Bluetooth:** извлечь и разобрать factory module metadata и init chain,
   затем составить план единственной контролируемой загрузки.
3. **LTE:** только при установленной SIM; сначала factory firmware/property
   flow, потом минимальная диагностическая сессия.
4. **GNSS:** после Bluetooth/WMT, так как они используют shared ConnSys.
5. **Camera:** только если появится практическая задача; это большой проект.

GPU не трогать до отдельного восстановимого плана: ранее даже чтение отдельных
PowerVR AppHint parameters приводило к kernel panic, поэтому этот аудит
намеренно не открывал и не читал GPU-specific nodes.
