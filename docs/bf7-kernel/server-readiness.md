# BF7 как полноценный Linux-сервер: чего не хватает

Аудит снят с живого устройства 2026-09-16 00:31, ядро
`4.19.325-cip124-st8-bf7-x657b-v1`, Debian 12, 3799 МиБ ОЗУ, 4 ядра,
50 ГиБ на `/` (занято 7%).

---

## Приоритет на ближайшую ночь

**1. Вывод кадра без копирования через `/dev/mtk_disp_mgr`.**
Сейчас `postBuffer()` копирует 4.6 МиБ из буфера gralloc в фреймбуфер: 8 мс
в четыре потока и ~200% CPU при анимации (видно по load average 15 при
работающем GUI). Буфер gralloc — это ION dma-buf, и движок наложения умеет
читать его напрямую:
`DISP_IOCTL_PREPARE_INPUT_BUFFER` -> `ion_import_dma_buf_fd` ->
`ION_MM_GET_IOVA` -> `DISP_IOCTL_SET_INPUT_BUFFER` ->
`DISP_IOCTL_TRIGGER_SESSION`. Подробный разбор с номерами ioctl и полями
структур — в `research-log.md`, запись за 2026-09-16.

Побочный выигрыш: уходит разрыв кадра и отпадает необходимость трогать
`fix->ypanstep` в ядре, потому что смена кадра становится сменой буфера у
слоя, а не панорамированием фреймбуфера.

Делать за флагом `MTKFB_DISP_MGR=1`, путь с `memcpy` остаётся запасным.
Перепрошивка не нужна. Главная неизвестность — что `mtkfb` настраивает тот
же слой напрямую из ядра (`primary_display_config_input_multiple`), минуя
сессии.

---

## Что уже есть

| | состояние |
|---|---|
| Ядро, консоль на панели, Debian, SSH | работает |
| GUI на GPU, тач, экранная клавиатура | работает, 60 fps |
| Wi-Fi station | работает, `wlan0`, есть интернет |
| USB-сеть (RNDIS) + SSH по USB | работает, `192.168.7.2` |
| Аварийная консоль | `192.168.7.2:2323` (initramfs) и `:2324` |
| RTC | `/dev/rtc0`, синхронизация `systemd-timesyncd` активна |
| Watchdog | `/dev/watchdog0` |
| Аппаратный генератор случайных чисел | `/dev/hwrng`, `CONFIG_HW_RANDOM_MT67XX=y` |
| Термодатчики | 27 зон, CPU 46 °C, батарея 35 °C |
| Здоровье eMMC | `life_time = 0x01 0x01` (износ 0–10%), `pre_eol_info = 0x01` |
| TRIM | `discard_max_bytes = 2 ТиБ`, поддерживается |
| Постоянный журнал | `/var/log/journal` есть |
| overlayfs, FUSE, seccomp, BPF | в ядре есть |

---

## 1. Контейнеры (Docker/Podman)

Проверено официальным `check-config.sh` из moby. **Обязательного не
хватает:**

| опция | зачем |
|---|---|
| `CONFIG_IPC_NS` | пространство имён IPC, без него Docker не стартует |
| `CONFIG_CGROUP_DEVICE` | контроль доступа контейнера к устройствам |
| `CONFIG_POSIX_MQUEUE` | `/dev/mqueue` внутри контейнера |
| `CONFIG_NETFILTER_XT_MATCH_ADDRTYPE` | правила проброса портов docker0 |

**Желательного не хватает:**
`CONFIG_USER_NS` (rootless и userns-remap),
`CONFIG_CGROUP_PIDS` (`--pids-limit`, его же хочет systemd),
`CONFIG_CFS_BANDWIDTH` (`--cpus`, квоты),
`CONFIG_BLK_DEV_THROTTLING` (`--device-read-bps`),
`CONFIG_NET_CLS_CGROUP`, `CONFIG_CGROUP_NET_PRIO`,
`CONFIG_CGROUP_HUGETLB`, `CONFIG_CGROUP_PERF`,
`CONFIG_MACVLAN`, `CONFIG_IPVLAN`, `CONFIG_VXLAN` (сети),
`CONFIG_IP_VS*` (swarm/ingress),
`CONFIG_IP6_NF_NAT` и `CONFIG_IP6_NF_TARGET_MASQUERADE` (IPv6).

В рантайме cgroup2 отдаёт только `cpu io memory`; `pids` отсутствует
из-за `CONFIG_CGROUP_PIDS`, а `cpuset` не появляется несмотря на
`CONFIG_CPUSETS=y` — надо разобрать отдельно.

Хранилище: `overlay2` доступен (`CONFIG_OVERLAY_FS=y`), btrfs/zfs нет и не
нужно.

**Из пользовательского пространства не установлено ничего:** ни `iptables`,
ни `nft`, ни `bridge-utils`, ни `docker.io`, ни `podman`.

Вывод: нужна **одна пересборка ядра** с ~14 опциями плюс установка пакетов.

## 2. Сеть и межсетевой экран

- `iptables`/`nftables` отсутствуют совсем — нет ни NAT, ни фильтрации.
  В ядре `CONFIG_NF_TABLES` нет, но `iptables-legacy` соберётся на том,
  что есть.
- Мостов нет (`brctl`/`bridge-utils` не установлены), хотя
  `CONFIG_BRIDGE=y` и `CONFIG_VETH=y`.
- IPv6 в ядре есть, у Wi-Fi адреса IPv6 нет (провайдер/роутер).

## 3. Wi-Fi в режиме точки доступа

Хорошая новость: `phy1` заявляет `AP` и `P2P-GO`, и интерфейс `ap0`
уже создан драйвером (`type AP`). То есть железо и драйвер `wlan_drv_gen4m`
это умеют.

Не хватает: `hostapd` и `dnsmasq` (не установлены), плюс NAT из пункта 2.
Риск: MTK gen4m часто требует своей сборки hostapd или поднятия `ap0` через
собственный механизм драйвера, а не через штатный nl80211. Нужен натурный
опыт.

Одновременная работа station + AP на одном радио (`wlan0` + `ap0`)
возможна только если драйвер отдаёт нужную комбинацию интерфейсов — проверить
по `valid interface combinations` в `iw phy1 info`.

## 4. USB / OTG

Сейчас телефон — USB-устройство: гаджет `musb-hdrc`, configfs с RNDIS,
ACM, mass_storage, serial, FunctionFS.

- Для режима **хоста** всё собрано: `CONFIG_USB_MTK_OTG=y`,
  `CONFIG_MTK_MUSB_DUAL_ROLE=y`, есть узел
  `/sys/devices/platform/mt_usb/musb-hdrc/cmode`. Классовые драйверы в
  ядре: `USB_STORAGE`, `USB_HID`, `USB_NET_AX88179_178A` (USB-Ethernet).
  **Не проверено на железе** — нужен OTG-кабель и подача VBUS через
  зарядный контроллер.
- Не хватает функций гаджета `NCM`/`ECM`/`EEM`
  (`CONFIG_USB_CONFIGFS_NCM` и т.д.) — RNDIS работает, но NCM заметно
  аккуратнее и быстрее с Linux-хостом.
- Одновременно хост и устройство — нет, разъём один.

## 5. Мониторинг

- Температуры, частоты, загрузка, здоровье eMMC — читаются из sysfs, можно
  отдавать `node_exporter`-ом (не установлен).
- **Батарея: `capacity = -1`** — топливомер `mtk-gauge` не отдаёт
  проценты (на стоке этим занимается андроидный демон). Напряжение
  (4.347 В) и температура (35.8 °C) читаются. Для сервера на постоянном
  питании это важно: без процента заряда непонятно состояние при пропадании
  電 питания.
- Нет `smartctl`-эквивалента, но `life_time`/`pre_eol_info` заменяют его
  для eMMC.
- Нет ни `node_exporter`, ни `prometheus`, ни `netdata`, ни `zabbix-agent`.

## 6. Память и своп

- Свопа нет вообще. `zram0` существует, но не настроен (0 байт).
  Для сервера с 3.8 ГиБ стоит поднять zram на 1–2 ГиБ
  (`zram-tools` или `systemd-zram-generator`).
- `CONFIG_MEMCG_SWAP=y` есть, то есть учёт свопа в контейнерах заработает.

## 7. Безопасность

- **VNC слушал `0.0.0.0:5900` без пароля**, а он даёт полное управление
  касаниями. Исправлено: по умолчанию слушает только USB-сеть
  (`192.168.7.2`), наружу отдаётся осознанно через `BF7VNC_BIND`.
- SSH пускает root; ключи есть, но пароль root тоже стоит проверить.
- Межсетевого экрана нет (см. пункт 2), `fail2ban` нет.
- Автообновлений безопасности (`unattended-upgrades`) нет.

## 8. Прочее железо

- **Bluetooth**: комбо-чип есть (`mtk_wmtd` работает), но интерфейса
  `hci0` нет и `bluez` не установлен. Нужен запуск стека через
  `/dev/stpbt`.
- **Модем LTE**: узлы `/dev/ccci_*` присутствуют, драйвер CCCI в ядре.
  Но управление модемом на MTK — это `mtkfusionrild` из Android, то есть
  большая отдельная работа. `ModemManager` сам по себе не подойдёт.
- Камеры, датчики, звук — для сервера не нужны, не проверялись.

---

## Найденные поломки (обновлено 2026-09-16 01:12)

Всё ниже найдено в ходе работы, а не предполагается.

### Уже исправлено (обновлено 2026-09-16 02:00)

| что было | как лечилось |
|---|---|
| VNC слушал `0.0.0.0:5900` **без пароля**, а это полное управление касаниями | привязан к USB-сети, наружу только через `BF7VNC_BIND` |
| Свопа не было вовсе; под Chromium оставалось 144 МиБ | zram 1.9 ГиБ, zstd, `vm.swappiness = 150`, `vm.page-cluster = 0`; юнит `dev-zram0.swap` прописан явно, потому что генератор создаёт его только на раннем этапе загрузки |
| `HOME` не задан службам systemd — терминал Xfce открывался в `/root/gui` | `Environment=HOME=/root`, `WorkingDirectory=/root`, плюс `XDG_*` и `cd "$HOME"` в скрипте сеанса |
| **Ошибка в libhybris**: `my_property_get()` при отсутствующем свойстве делал `value = '\0'` — присваивание указателю вместо первого байта, вызывающий получал мусор из стека | `value[0] = '\0'` |
| `hw_get_module` брал `gralloc.default.so` вместо `gralloc.rogue.so`, PowerVR не признавал хендлы | `ro.hardware.gralloc=rogue` в build.prop |
| MTK `libgralloc_extra` печатала полную раскрутку стека на каждый вызов gralloc: 5.7 КиБ лога и **51 мс CPU** на кадр | `libutilscallstack.so` подменена заглушкой |
| Частота GPU была закреплена на 660 МГц при отладке | закрепление снято (`echo -1 > /proc/gpufreq/gpufreq_opp_freq`) |
| Система считала себя контейнером: `/.dockerenv` от сборки образа, systemd не обслуживал `.swap`-юниты | файл убран, `systemd-detect-virt` = `none` |
| Системная шина D-Bus не запускалась вовсе — молчали `networkctl`, `timedatectl` | явный симлинк в `sockets.target.wants` |
| `/etc/hosts` пустой, часовой пояс UTC, локаль не задана, журнал 1.3 ГиБ | заполнено, `Europe/Moscow`, `ru_RU.UTF-8`, журнал ограничен 200 МиБ |
| Маршрут по умолчанию пропадал при роуминге точки доступа | `IgnoreCarrierLoss=30s` и `KeepConfiguration=yes` |
| `iptables` в Debian по умолчанию nftables, а в ядре его нет | переключено на `iptables-legacy`; поднят экран `bf7-firewall` |
| Композитор съедал целое ядро на копировании кадра | вывод слоем наложения через `/dev/mtk_disp_mgr`, простой 99.5% |
| Топливомер молчал (`capacity = -1`) — алгоритм у MTK в демоне, не в ядре | `bf7-fuelgauge.service` поверх `/vendor/bin/fuelgauged` |
| `upower` не стартовал: `PrivateUsers=yes` требует `CONFIG_USER_NS` | ядро v2 прошито, костыль убран |
| **Узлов binder не было вовсе**, хотя ядро собрано с `CONFIG_ANDROID_BINDERFS=y` | `bf7-binderfs.service`: `mount -t binder binder /dev/binderfs` плюс симлинки `/dev/{binder,hwbinder,vndbinder}` |
| **Ни одна транзакция binder не проходила** (`FAILED_TRANSACTION`): диспетчер просит контекст SELinux, а LSM выключен | заглушка `ioctl` отклоняет `BINDER_SET_CONTEXT_MGR_EXT`, `libhwbinder` откатывается на обычный ioctl |
| `hwservicemanager` падал в `AccessControl` без selinuxfs | разрешающая заглушка libselinux на девять точек входа |
| Клиенты HIDL крутились на 100% CPU: `/dev/__properties__` нет, а `libhidlbase` ждёт `hwservicemanager.ready` | подмена `__system_property_*` с таблицей из настоящих `build.prop` |
| Ошибки блобов терялись: `logd` без init не получает сокеты | перехват liblog в stderr |
| 32-битная bionic не запускается при `pid > 65535` (pid лежит внутри `pthread_mutex_t`), а systemd ставит `pid_max=4194304` | `/etc/sysctl.d/90-bf7-pid-max.conf` → 65536 |

### Открытые

1. **Видео декодируется процессором** (~145% CPU под видео в Chromium), но
   **аппаратный декодер уже запущен** — см. research-log за 2026-09-16 08:38:
   `start() -> C2_OK` на `c2.mtk.avc.decoder`, `c2.mtk.hevc.decoder` и
   `c2.mtk.vp9.decoder`, выходной кадр выделяется через gralloc4. Осталось
   скормить настоящий поток, забрать кадр и довести его до браузера.
   Со стороны ядра делать нечего: драйвер `CONFIG_MTK_VIDEOCODEC_DRIVER=y`
   собран, `/dev/Vcodec` работает. Живой путь — Codec2, и он доведён до
   настоящего драйвера: компонент `c2.mtk.avc.decoder` создаётся, слушатель
   ставится, пул буферов выдаётся, а `start()` доходит до
   `libvcodecdrv.so`. Два условия, без которых ничего не работает:
   помощник обязан быть **32-битным** (настоящий драйвер только ELF32) и
   нужны **узлы binder**. Оба выполнены. Осталась обычная работа с Codec2:
   параметры потока, подача пакетов, сбор кадров, затем передача dma-buf на
   64-битную сторону через `SCM_RIGHTS`. Исходники: `hybris/bf7-codec2/`.
2. **Осталась загрузка кадра Xwayland в текстуру.** Копирование кадра в
   фреймбуфер снято выводом слоем наложения (процессор на неподвижном столе
   простаивает на 99.5%), но буфер Xwayland по-прежнему идёт через
   разделяемую память и загружается в текстуру на каждый кадр. Это свойство
   Xwayland без dmabuf; лечится только поддержкой обмена буферами GPU
   (`EGL_WL_bind_wayland_display`, см. пункт ниже).
3. **Вендорские HIDL-службы заработали — стоит пересмотреть отложенное.**
   Раньше HWC, gralloc4 и вендорские службы считались недостижимыми, потому
   что не было hwbinder. Теперь работает и binder, и диспетчер служб:
   - ✅ `hwservicemanager` и `vndservicemanager` подняты
     (`/usr/local/bin/hal-start.sh`);
   - ✅ gralloc4 через `android.hardware.graphics.allocator@4.0-service-mediatek`
     зарегистрирован и выдаёт кадры;
   - возвращение к `hwcomposer.mt6761.so`, который падал в коде MediaTek PQ и
     требовал пятнадцать интерфейсов `vendor.mediatek.hardware.pq@2.*`;
   - тот же ключ, что открыл аппаратное видео.

   Проверять по одной штуке: каждая служба тянет свои зависимости, и часть
   из них рассчитывает на андроидный `init` с его сокетами и свойствами.
   Готовый набор подпорок вместо init лежит в
   `hybris/bf7-codec2/{selinux-stub,propstub,logstub,binderstub}.c`.

   Отдельная заметка: `vendor.mediatek.hardware.mtkpower@1.0-service`
   поднимается, но её поток `mtkScnHanler` жжёт целое ядро
   (`checkFPSupdate()` → `getFPSvalue()` в `libpowerhal.so` опрашивает
   частоту кадров без пауз). Декодеру она не обязательна — подставляется
   заглушка клиента `libmtkperf_client_vendor.so`. Если служба понадобится
   ради HWC, цикл придётся чем-то гасить.

4. **У X-клиентов нет OpenGL вообще.** Xwayland сообщает
   `GBM backend is not available` и `Failed to initialize glamor, falling
   back to sw`; GLX и DRI отсутствуют. Поэтому недоступны GNOME, KDE и любое
   приложение, которому нужен GL. Chromium работает в программной отрисовке.
5. ✅ **`EGL_WL_bind_wayland_display` заработало** (2026-09-16). libhybris
   пересобран с `--enable-wayland`, композитор объявляет `android_wlegl`,
   клиент Wayland получает GL ES 3.2 на PowerVR и отдаёт буферы GPU без
   копирования. Проверка — `hybris/bf7-gpu/wlgltest.c`. Осталось перевести на
   этот путь браузер: Chromium 152 из Debian допускает только ANGLE, а тот
   падает в вендорской обёртке `libGLES_meow.so`.
6. ✅ **Топливомер батареи** — закрыт `bf7-fuelgauge.service` (см. таблицу
   исправленного). Было: `capacity = -1`, в журнале
   `power_supply mtk-gauge: driver failed to report 'energy_empty' property:
   -22`. Напряжение и температура читаются. Для сервера важно: при пропадании
   питания не видно остатка заряда.
7. **Пропадает маршрут по умолчанию на Wi-Fi.** В
   `/etc/systemd/network/20-wlan0.network` статический `Gateway=`, но
   маршрут исчезал из таблицы дважды за ночь, и apt переставал работать.
   Причина не найдена, лечится `ip route replace default via 192.168.27.1 dev
   wlan0`. Нужен разбор — для сервера это отказ сети.
8. ✅ **Панорамирование `/dev/fb0`** — закрыто в ядре v2 (`ypanstep = 1` плюс
   снятый `FBINFO_HWACCEL_YPAN`). Было: не работало, потому что мы сами выставили
   `fix->ypanstep = 0` ради fbcon. Отсюда одна страница и возможный разрыв
   кадра. Либо решится выводом через `mtk_disp_mgr`, либо правкой ядра:
   вернуть `ypanstep = 1` и убрать `FBINFO_HWACCEL_YPAN` из
   `info->flags`.
9. **`cpuset` не появляется в cgroup2**, хотя `CONFIG_CPUSETS=y`;
   `/sys/fs/cgroup/cgroup.controllers` отдаёт только `cpu io memory`.
   Docker на это ругается. Разобрать отдельно.
10. **Подготовка андроидной среды делается после загрузки** юнитом
   `bf7-android-mounts.service`: тома из `super`, apex, overlay. Просили
   перенести в initramfs, чтобы Debian стартовал в готовой среде.
11. **Заглушка `libutilscallstack.so` — это костыль**, от которого зависит
    производительность. Если overlay не смонтировался, всё тихо замедляется
    впятеро. Нужен внятный признак в логе при старте сеанса.
12. **Копятся зомби-процессы** в сеансе Xfce (`xdg-user-dirs-u`,
    `xfsettingsd`, `exo-open`, `chromium`) — их никто не подбирает.
    Мелочь, но за сутки накопится.
13. **SELinux выключен, и это уже стоило нам двух поломок** (падение
    `hwservicemanager` в `AccessControl` и провал всех транзакций binder на
    `security_secid_to_secctx -> -EOPNOTSUPP`). Обе обойдены заглушками, но
    вопрос открыт. Что известно:

    - в ядре он **собран**: `CONFIG_SECURITY_SELINUX=y`, выключает его
      `security=none` в командной строке от загрузчика, а не сборка;
    - `CONFIG_SECURITY_SELINUX_DEVELOP` **не задан** — permissive-режима нет,
      только enforcing, и `enforcing=0` в командной строке будет
      проигнорирован;
    - на rootfs Debian **нет ни одной метки** `security.selinux` (проверено на
      `/sbin/init`), пакетов политики не установлено, `/etc/selinux` пуст,
      кроме `semanage.conf`.

    Рабочий вариант: убрать `security=none` и **не загружать политику**. Пока
    `ss_initialized == 0`, ядро разрешает всё, AVC даже не опрашивается, а
    `is_selinux_enabled()` уже возвращает 1 — андроидным блобам этого хватает.
    Debian не пострадает: systemd слинкован с libselinux, но без
    `/etc/selinux/config` его `selinux_init_load_policy()` отработает вхолостую.
    Загружать андроидную политику в enforcing **нельзя ни при каких условиях**
    — всё немеченое попадёт под `unlabeled_t` и система умрёт мгновенно.

    Цена: правка командной строки → новый boot-образ → прошивка. В ту же
    сборку стоит добавить `CONFIG_SECURITY_SELINUX_DEVELOP=y` как страховку.
    Решение за вами; пока работают заглушки, срочности нет.
14. **Масштаб и поворот делаются растягиванием на GPU**, а не аппаратным
    масштабатором наложения. В `disp_input_config` есть раздельные
    `src_width/src_height` и `tgt_width/tgt_height`: тогда GPU рисовал бы
    сразу в логическом разрешении (при 1.5x это 44% пикселей), а растягивал
    бы дисплейный контроллер — и чётче, и дешевле.

---

## Порядок работ

Ночной приоритет (вывод без копирования) описан в начале файла. Дальше:

2. **Перенести подготовку андроидной среды в initramfs** — тома из `super`,
   apex, overlay, заглушка `libutilscallstack`. Тогда Debian получает
   управление в готовой среде, без порядковых зависимостей в systemd.
3. **Одна пересборка ядра**, закрывающая всё железное сразу:
   - четыре обязательные для Docker: `IPC_NS`, `CGROUP_DEVICE`,
     `POSIX_MQUEUE`, `NETFILTER_XT_MATCH_ADDRTYPE`;
   - желательные: `USER_NS`, `CGROUP_PIDS`, `CFS_BANDWIDTH`,
     `BLK_DEV_THROTTLING`, `NET_CLS_CGROUP`, `CGROUP_NET_PRIO`,
     `CGROUP_HUGETLB`, `CGROUP_PERF`, `MACVLAN`, `IPVLAN`, `VXLAN`, `IP_VS*`,
     `IP6_NF_NAT`, `IP6_NF_TARGET_MASQUERADE`;
   - `USB_CONFIGFS_NCM` вместо голого RNDIS;
   - `ypanstep = 1` и снятие `FBINFO_HWACCEL_YPAN`, если вывод через
     `mtk_disp_mgr` не закроет вопрос;
   - заодно проверить `VIDEO_MEDIATEK_VCODEC` на предмет mt6765.

   Правило: тем же заходом пересобрать и **поставить модули связности**,
   иначе Wi-Fi не поднимется и это будет выглядеть как незагрузка.
4. **Пакеты**: `iptables`, `bridge-utils`, `docker.io` (или `podman`),
   `hostapd`, `dnsmasq`, `prometheus-node-exporter`, `unattended-upgrades`.
5. **Настроить** межсетевой экран и автообновления. (zram уже поднят.)
6. **Разобраться с пропажей маршрута по умолчанию** на Wi-Fi — для сервера
   это отказ сети.
7. **Проверить OTG-хост натурно**: флешка, USB-Ethernet, клавиатура.
8. **Поднять точку доступа** на `ap0`.
9. **Топливомер батареи** — для мониторинга питания.
10. **Аппаратное декодирование видео** — отдельный проект, см. пункт 1
   открытых поломок.
11. **Bluetooth и модем** — по желанию, тоже отдельные проекты.

## Что осталось сделать для «любое приложение работает с графикой»

Состояние на 2026-09-16, после прошивки ядра с починкой звука.

Ключевые факты, проверенные на железе:

- `/dev/dri/card0` — **без единого соединителя**, привязан к `mfg_clark@13000000`,
  то есть это узел GPU для рендера. **KMS у нас нет вовсе**; экран живёт через
  `mtkfb` и `mtk_disp_mgr`.
- У нашего EGL **нет `EGL_EXT_image_dma_buf_import`** — чужой dmabuf в текстуру
  не импортируется.
- В Mesa **нет** `pvr_dri.so`, и открытого драйвера GL для Rogue не существует.

По возрастанию стоимости:

1. ✅ **Vulkan заработал** (2026-09-16). Ноль устройств давала не поломка
   драйвера, а отсутствие `/vendor/lib64/hw` в путях поиска линковщика
   libhybris: загрузчик Vulkan передаёт в `dlopen` голое имя
   `vulkan.mt6761.so`, не находит его и молча подставляет заглушку, которая
   возвращает `VK_SUCCESS` и `count = 0`. Исправлено в
   `hybris/common/q/linker.cpp` (`kDefaultLdPaths`). Теперь
   `vkEnumeratePhysicalDevices` отдаёт `PowerVR Rogue GE8300`, Vulkan 1.1.170.
   Проверка — `hybris/bf7-gpu/vktest.c`.
   **Следующий шаг, ради которого всё делалось:** ANGLE на Vulkan для Chromium
   (`--use-angle=vulkan`) и **Zink** из Mesa, дающий поверх Vulkan настоящий
   десктопный OpenGL для приложений, которым нужен GL, а не GLES.
2. ✅ **glvnd заработал** (2026-09-16). Клиенты Wayland получают
   `PowerVR Rogue GE8300`, `OpenGL ES 3.2` **без единой переменной окружения** —
   ни `LD_LIBRARY_PATH`, ни `EGL_PLATFORM`, ни `LIBEGL`/`LIBGLESV2`, ни даже
   предзагрузки сторожа TLS (он нужен был из-за андроидной обёртки `libEGL`, а
   мы теперь ходим прямо в вендорский драйвер мимо неё). Проверено после
   перезапуска сеанса.

   Ключевая тонкость, стоившая больше всего времени: **функции GL нельзя брать
   через `eglGetProcAddress`**. Голый вендорский драйвер отдаёт оттуда
   указатели, которым нужны хуки в слотах TLS bionic, и без них `glGetString`
   молча возвращает NULL. Резолвить надо прямым `android_dlsym` в
   `libGLESv2_mtk.so` — как это делает собственная обёртка libhybris. Сделано
   функцией `hybris_egl_lookup_glesv2()` в `egl.c`, вызывается из
   `glvnd/eglglvnd.cpp` для всех имён на `gl`.

   Что понадобилось: пакеты `libglvnd-core-dev` и **`libgles2`** (диспетчера GL
   от glvnd в системе не было вовсе), сборка с
   `--enable-glvnd --with-default-egl-platform=wayland --datarootdir=/usr/share`
   и новые опции `--with-default-libegl=`/`--with-default-libglesv2=`.
   Файл поставщика — `/usr/share/glvnd/egl_vendor.d/10_libhybris.json`.

   Остаётся: Qt5-приложения нативный Wayland не получат, пакет `qtwayland5` не
   установлен. И сам композитор пока запускается по-старому, через переменные —
   его перевод на общий путь стоит сделать отдельно и под присмотром.

3. **Прослойка DRM/KMS для дисплея.** Ключевой камень; разведка сделана
   2026-09-16, подробности в research-log. Коротко: **полный порт
   `drivers/gpu/drm/mediatek/` отпадает** — mt6761 там нет вовсе, наш DT
   использует легаси-имена, у disp-узлов нет `clocks`, узла панели нет (имя LCM
   приходит тегом от загрузчика). Это переписывание фрагмента DT с нуля плюс
   новые driver_data с выверенными офсетами плюс драйвер панели — недели.
   **Рекомендуемый путь — минимальная прослойка на 500–1000 строк:** один CRTC,
   один connector без hotplug, один primary plane, а вывод отдаётся уже
   рабочему вендорскому тракту (`primary_display_frame_cfg()`,
   `primary_display_trigger()`, `primary_display_wait_for_vsync()`). Образец
   архитектуры — `drivers/gpu/drm/udl/`. Линковать проще всего тем же `.o`
   внутрь ядра рядом с `primary_display.c`: у вендорских функций нет
   `EXPORT_SYMBOL`, а `CONFIG_MTK_FB` — `bool`, не `tristate`. Конфиг почти
   готов, не хватает только `CONFIG_DRM_FBDEV_EMULATION`.
   Даёт glamor и GL для X-приложений, wlroots, mutter, kwin, стандартный
   `linux-dmabuf`, а с ними GNOME и KDE.
4. **GBM поверх этого** — своя небольшая реализация (образец — `libgbm_kms`
   у Renesas) либо через gralloc.
5. **Импорт dmabuf.** В блобе **EGL** расширения нет и добавить нельзя; чужие
   буферы пришлось бы заворачивать через gralloc
   (`EGL_ANDROID_get_native_client_buffer`). **Но на стороне Vulkan
   `VK_EXT_external_memory_dma_buf` есть** (проверено 2026-09-16) — значит
   обмен буферами можно строить через Vulkan, и это снимает часть ограничения.
   Не хватает только `VK_EXT_image_drm_format_modifier`, из-за чего ANGLE
   бракует устройство.

Не нужно: менять блобы на glibc-набор DDK (риск потерять работающий стол) и
модуль V4L2 — он про видео, а не про графику.

### Открытые хвосты помельче

- **Маршрутизация звука.** Разобрана 2026-09-16, скрипт —
  `/root/bf7-audio-route.sh` на устройстве. Аналоговый тракт PMIC MT6357
  включается (подтверждено сверкой регистров `AUDDEC_ANA_CON*` до/после с
  кодом драйвера). **Наушники скорее всего уже работают.** А вот динамик
  упирается в отдельную дыру: на i2c-3 @ 0x34 стоит smart-PA
  (`mediatek,speaker_amp`), драйвера под конкретную микросхему в ядре нет,
  диспетчер MediaTek молча откатывается на пустышку, а GPIO
  `aud_smartpa_reset/on/off` в device tree отсутствуют. Кандидаты по
  `SmartPa_AudioParam.xml` из `/vendor`: NXP TFA9887/9890/9874, Maxim 98926,
  Richtek RT5509, Cirrus CS35L35/41. Это отдельная задача — драйвер плюс
  правка DT.
- **`wlan_drv_gen4m.ko`, собранный против v2, даёт `Invalid module format`**,
  хотя `wmt_drv` и `wmt_chrdev_wifi` встают. Рабочий комплект — в
  `/root/modules-v2` на устройстве.
- **Firefox: WebRender не поднимается.** Контекст EGL создаётся, `eglMakeCurrent`
  без поверхности проходит, но Firefox всё равно объявляет неудачу. Причина не
  найдена.
- **Chromium: ANGLE не получает дисплей EGL** (`Failed to get system egl
  display`), хотя libhybris отдаёт `eglGetPlatformDisplayEXT` через
  `eglGetProcAddress` и объявляет `EGL_EXT_platform_base`.
