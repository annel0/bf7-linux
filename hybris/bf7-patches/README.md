# Заплатки к чужим деревьям

Здесь лежат правки к репозиториям, которые мы не держим в этом дереве целиком.

## libhybris-bf7.patch

Наши правки к libhybris. Дерево живёт в `hybris/libhybris/` и не
отслеживается этим репозиторием: там свой `.git`, и `git add` тихо
создал бы gitlink вместо файлов.

* исток: `https://github.com/libhybris/libhybris.git`
* база: `7079712` (upstream, Merge pull request #611); в заплатке и два наших коммита сверху, и незакоммиченные правки
* файлов: 28

Как наложить на чистое дерево:

    git clone https://github.com/libhybris/libhybris.git
    cd libhybris
    git checkout 7079712
    git apply /путь/hybris/bf7-patches/libhybris-bf7.patch

Как обновить заплатку после новых правок:

    cd hybris/libhybris
    git add -N hybris/egl/platforms/x11 hybris/platforms/wayland/bf7-dmabuf-client-protocol.h hybris/platforms/wayland/bf7-dmabuf-protocol.c
    git diff 7079712 > ../bf7-patches/libhybris-bf7.patch
    git reset -q

`git add -N` нужен потому, что новые файлы в `git diff` иначе не попадают,
а именно в них лежит сгенерированный код протокола `zwp_linux_dmabuf_v1`.

### Что в ней важного

**Кэш шейдеров драйвера (2026-09-25)** — `egl/egl.c`: после eglInitialize
драйверу выдаются функции `eglSetBlobCacheFuncsANDROID` (как это делает
libEGL Android); хеш-таблица в памяти + журнал `$HOME/.cache/bf7-egl/<имя>.blob`
(читается при запуске, не больше 64 МБ). Без него PowerVR перекомпилировал
варианты шейдеров постоянно: GTK4 на GLES — 55% процессора в libusc/
libglslcompiler; с кэшем −64% процессора, phoc −6%. `BF7_EGL_NO_BLOB=1` —
выключить, `BF7_EGL_BLOB_TRACE=1` — счётчики при выходе.

**Быстрый отказ для несуществующих gl* (2026-09-25)** — `egl/egl.c`,
`hybris_egl_lookup_glesv2`: имена gl*, определённые в libGLESv2_mtk, читаются
один раз из её ELF-файла (.dynsym); имени нет — сразу NULL, без обхода всего
дерева зависимостей в android_dlsym. glvnd на каждый контекст спрашивает
тысячи имён (gnome-calculator: 3401, из них 2789 несуществующих); обход
брал 14% запуска. Процессорное время запуска калькулятора 1,78 → 1,57 с.

**Синхронизация ядра EGL 1.5 (2026-09-25)** — `egl/egl.c`: `eglCreateSync`,
`eglDestroySync`, `eglClientWaitSync`, `eglWaitSync`, `eglGetSyncAttrib` —
переходники к KHR-функциям драйвера (разница только в EGLAttrib/EGLint).
Дисплей объявляет EGL 1.5, клиенты зовут имена ядра, а glvnd ищет их у
поставщика через `getProcAddress` — было NULL, вызов молча давал
EGL_NO_SYNC. Так DMABuf-рендерер WebKit застревал после первого кадра
(серые страницы); с правкой браузер работает без подпорок. Проверять:
`BF7_EGL_TRACE=1` → `getProcAddress("eglCreateSync") -> есть`.

**Vulkan на Wayland (2026-09-23)** — `vkcube` видел GPU, но показать кадр не
мог. Правки в `hybris/vulkan/`:
- `platforms/wayland/vulkanplatform_wayland.cpp` — подключать
  `zwp_linux_dmabuf_v1`, как EGL: иначе кадры уходили в типе `android_wlegl`,
  и wlroots отвечал «unknown buffer type» с ошибкой протокола;
- `vulkan.c` — `vkGetPhysicalDeviceSurfaceCapabilities{,2}KHR` отвечают
  «размер не задан» (0xFFFFFFFF) и допускают до 4096×4096: окно заводится
  1×1, и андроидный Vulkan сообщал программе swapchain в один пиксель;
- `platforms/wayland/wayland_window.cpp` — при нехватке свободных буферов
  заводить новый (до шести), а не ждать: при пересоздании swapchain один
  буфер ещё у композитора, и оба ждали друг друга. Там же (21:20) —
  `queueBuffer` сначала ждёт готовности кадра (`sync_wait`), потом отдаёт
  его композитору; было наоборот, как не было в EGL-части. Замер старой и
  новой библиотеки поочерёдно: vkcube 54/54, RetroArch 84/83 кадра/с —
  бесплатно.
- `platforms/wayland/wayland_window_common.cpp` + `vulkan/…/wayland_window.h`
  (22:05) — учёт буферов при прямом показе. Найдено камерой: RetroArch на
  весь экран застывал на своей заставке. Причины две. (1) `destroyBuffer`
  вычитал из свободных и занятый буфер, а `setBufferCount(3)` при каждом
  новом swapchain уничтожал буферы с начала списка — вместе с тем, что как
  раз на панели (при прямом показе композитор держит его до следующего
  кадра); счётчик уходил вниз, `dequeueBuffer` вечно ждал release.
  (2) Андроидный Vulkan при новом swapchain делает `api_disconnect/connect`
  — на Android очередь сама забирает всё, что держал прежний swapchain, в
  libhybris это пустышки: брошенные картинки оставались «занятыми»
  навсегда. Теперь в Vulkan-окне `setBufferCount` убирает буферы прежнего
  swapchain (свободные — сразу, те, что у композитора, — по release), а
  занятые не уменьшают счётчик и уходят из `fronted`.
  После этого из `dequeueBuffer` убрана давняя подпорка «нет свободных —
  заведи ещё, до шести»: андроидный Vulkan такой буфер не знает, отвечает
  OUT_OF_DATE, и при прямом показе RetroArch пересоздавал swapchain каждые
  несколько кадров. Взаимной блокировки vkcube без неё нет (проверено).
  Итог на панели: RetroArch Vulkan на весь экран — 60 кадров/с, 24% ядра.

Ставятся `vulkan/.libs/libvulkan.so.1.2.183` → `/usr/local/lib/` и
`vulkan/platforms/wayland/.libs/vulkanplatform_wayland.so` →
`/usr/local/lib/libhybris/`. Проверка: `vkcube-wayland --c 300` на
композиторе — ~57 кадров/с (упор в 60 Гц).


`hybris/platforms/wayland/wayland_window_common.{h,cpp}` — путь кадра через
**стандартный** `zwp_linux_dmabuf_v1` вместо андроидного `android_wlegl`.
Благодаря ему на нашем GPU рисуют обычные вейландовы программы, а не только
те, что умеют `android_wlegl`. `bf7_find_dma_fd()` достаёт dma-buf из ручки
gralloc пробой по размеру, `bf7_fourcc_from_hal()` переводит формат HAL в
fourcc, `wlbuffer_from_dmabuf()` собирает `wl_buffer` через `create_immed`.
Если dmabuf не вышел, `ClientWaylandBuffer::init()` откатывается на старый
`wlbuffer_from_native_handle`.

`hybris/egl/platforms/wayland/eglplatform_wayland.cpp` — привязка
`zwp_linux_dmabuf_v1` (нужна версия не ниже 3, иначе нет `create_immed`).
Падаем только если нет ни dmabuf, ни wlegl.

Собирается на телефоне; после правок достаточно `make -C egl/platforms/wayland`.
Конфигурировать с `--disable-wayland_serverside_buffers`.

`hybris/egl/egl.c` — **безоконные платформы берут уже загруженный модуль ws**
(добавлено 2026-09-22). Модуль оконной системы в процессе один: какой
загрузили первым, тот и стоит, а `ws_init` на другое имя отвечает отказом.
Для GBM и `EGL_PLATFORM_SURFACELESS_MESA` мы просим сквозной `"null"` — и это
проваливалось, если приложение уже завело дисплей на другой платформе. Ровно
так вставал браузер: GTK берёт вейландовый дисплей для окна, а следом WebKit
просит второй, через GBM, и получает
`Could not create GBM EGL display: EGL_NOT_INITIALIZED. Aborting…`.
Окна безоконной платформе не нужны, поэтому теперь берётся любой уже
загруженный модуль. Проверено трассой:

    [hybris] getPlatformDisplay: платформа 0x31d7, ws "null"
    [hybris]   ws_init("null") отказал
    [hybris]   беру уже загруженный ws "wayland"
    [hybris]   андроидный eglGetDisplay -> 0x1

Собирается на телефоне: `make -C hybris/egl`, затем
`install -m755 hybris/egl/.libs/libEGL_libhybris.so.0.0.0 /usr/local/lib/`.

`hybris/egl/platforms/x11/` — заход на X11 напрямую, до glamor. Оставлено
как след: рабочий путь к иксам оказался другим (Xwayland поверх нашего
композитора), но здесь видно, чего стоил прямой.

## phoc-0.24-touch-wake.patch

Одна правка к композитору phoc: касание при погашенном экране отбрасывалось
раньше, чем сбрасывался простой, и телефон нельзя было разбудить пальцем.
Подробности — в самом файле заплатки и в дев-логе за 2026-09-22.

* исток: Debian bookworm, пакет `phoc` 0.24.0-1
* стоит как пакет `phoc 0.24.0-1+bf7.1` (`/usr/bin/phoc`), закреплён
  `apt-mark hold phoc`; запасная копия — `/usr/local/bin/bf7-phoc`

Как собрать заново (на телефоне, `deb-src` уже включён) — так же, как phosh
ниже: `apt-get source phoc`, заплатку в `debian/patches/` и `series`, запись
в `debian/changelog`, `dpkg-buildpackage -us -uc -b`
(`apt-get build-dep -y phoc` — нужен ровно `libwlroots-dev` 0.15.1-6).

Двоичный файл собран против wlroots 0.15.1-6. Если `libwlroots10` когда-нибудь
обновится, пересобрать. `bf7-selftest` проверяет, что работает именно наш
композитор, и ругается, если подменился пакетный.

## phoc-0.24-idle-inhibit.patch

phoc 0.24 не объявлял `zwp_idle_inhibit_manager_v1`, а RetroArch, mpv и
SDL-игры на Wayland держат экран включённым только им (Firefox и Chromium —
ещё и через D-Bus, у них работало). Экран гас посреди игры или фильма, а
программа с вертикальной синхронизацией вставала: погашенный вывод не шлёт
«кадр показан». Заплатка подключает `wlr_idle_inhibit_v1` из wlroots 0.15:
пока есть видимое окно с запретом, таймеры простоя (`wlr_idle`, по ним
phosh и gnome-session гасят и блокируют экран) выключены. Видимость —
`phoc_desktop_view_is_visible` (телефонный режим с развёрнутыми окнами);
фоновое окно запрет не держит, раз в 5 с перепроверка, пока запреты есть.

* пакет `phoc 0.24.0-1+bf7.2` (второй строкой в `series` после touch-wake);
* проверено вторым, безэкранным phoc: `wayland-info` показывает протокол,
  RetroArch пишет «Enabling idle inhibitor», phoc — «Idle inhibited», после
  выхода — «Idle no longer inhibited».

## phoc-0.24-fullscreen-geometry.patch

При смене геометрии окна phoc сдвигает окно, чтобы начало геометрии осталось
на месте, — верно для плавающего окна, но не для окна на весь экран или
развёрнутого: оно прибито к выводу. RetroArch (рамка libdecor, заголовок на
(0,-24)) переходил на весь экран с геометрией (0,-24,…), затем ставил (0,0,…),
и phoc навсегда сдвигал окно на 24 логические точки: картинка при прямом
показе на месте, а все касания — на 48 пикселей ниже (узкие START/SELECT
экранных кнопок не нажимались). Заплатка: компенсацию не делать для
полноэкранных и развёрнутых окон. Пакет `phoc 0.24.0-1+bf7.3` (третьей в
`series`). Проверено WAYLAND_DEBUG: RetroArch с рамкой — касание в центр
экрана приходит как (180,5; 403,5) вместо (180,5; 427,5).

## phosh-0.24-no-wake-on-resume.patch

Правка к оболочке phosh: при каждом выходе из сна она зажигала экран
(`WakeUpScreen` и сброс таймеров простоя — и то и другое через gsd-power).
Телефон просыпается и без человека — от контроллера топлива, по будильникам
таймеров, — и экран вспыхивал в кармане. Пробуждение кнопкой питания
отрабатывает phoc, будильник зажигает экран сам (`bf7-budilnik`), обратно
усыпляет `bf7-dosyp`. Подробности — в заголовке заплатки и в дев-логе за
2026-09-23.

* исток: Debian bookworm, пакет `phosh` 0.24.0-1
* стоит как пакет `phosh 0.24.0-1+bf7.1`, закреплён `apt-mark hold phosh`

Как собрать (на телефоне; сборка около получаса):

    apt-get source phosh && cd phosh-0.24.0
    mkdir -p debian/patches
    cp /путь/hybris/bf7-patches/phosh-0.24-no-wake-on-resume.patch debian/patches/bf7-no-wake-on-resume.patch
    echo bf7-no-wake-on-resume.patch > debian/patches/series
    # запись 0.24.0-1+bf7.1 в начало debian/changelog
    DEB_BUILD_OPTIONS="nocheck nodoc parallel=4" dpkg-buildpackage -us -uc -b
    dpkg -i ../phosh_0.24.0-1+bf7.1_arm64.deb && apt-mark hold phosh
    systemctl restart bf7-phosh

## gsd-43-no-wake-on-resume.patch

Правка к gsd-power (gnome-settings-daemon 43): при каждом выходе из сна он
включал панель (`backlight_enable()` в `handle_resume_actions()`). Парная к
заплатке phosh: без обеих экран после непрошеного пробуждения загорался.

* исток: Debian bookworm, пакет `gnome-settings-daemon` 43.0-4
* пакет целиком не пересобираем (тянет mutter, wacom и тесты) — собирается
  одна цель `gsd-power` и ставится поверх пакетного через `dpkg-divert`;
  пакетный лежит рядом как `/usr/libexec/gsd-power.debian`

Как собрать (на телефоне, около минуты):

    apt-get source gnome-settings-daemon && cd gnome-settings-daemon-43.0
    patch -p1 < /путь/hybris/bf7-patches/gsd-43-no-wake-on-resume.patch
    apt-get install --no-install-recommends dh-exec gtk-doc-tools intltool \
        libasound2-dev libcanberra-gtk3-dev libcolord-dev libcups2-dev \
        libgeoclue-2-dev libgeocode-glib-dev libgweather-4-dev libibus-1.0-dev \
        liblcms2-dev libnotify-dev libmm-glib-dev libpackagekit-glib2-dev \
        libxt-dev patchelf
    meson setup ../gsd-build --prefix=/usr --libdir=/usr/lib \
        --libexecdir=/usr/libexec --buildtype=release -Dgcr3=true
    ninja -C ../gsd-build plugins/power/gsd-power
    # путь к libgsd.so — как у пакетного, а не в каталог сборки
    patchelf --set-rpath /usr/lib/gnome-settings-daemon-43 ../gsd-build/plugins/power/gsd-power
    dpkg-divert --add --rename --divert /usr/libexec/gsd-power.debian /usr/libexec/gsd-power
    install -m755 ../gsd-build/plugins/power/gsd-power /usr/libexec/gsd-power
    systemctl restart bf7-phosh

Вернуть пакетный: `rm /usr/libexec/gsd-power && dpkg-divert --remove --rename
/usr/libexec/gsd-power`.


## seatd-0.7-builtin-unblock-signals.patch

Правка к libseat: phoc открывает место через встроенный seatd, а тот
запускается в потомке после `fork()` без `exec` и наследует маску сигналов
phoc. phoc блокирует SIGUSR1, seatd просит ядро присылать его при отпуске
терминала (`VT_SETMODE`/`VT_PROCESS`) — и сигнал не доходит никогда. Любое
переключение терминала (`chvt`, консоль при сне с `console_suspend=N`)
вешается в `VT_WAITACTIVE`. Видно по `/proc/<потомок phoc>/status`:
`SigBlk 0000000000000200` при `SigCgt …4a02`. Правка сбрасывает маску в
потомке.

* исток: Debian bookworm, пакет `seatd` 0.7.0-6
* стоят `libseat1` и `libseat-dev` 0.7.0-6+bf7.1, закреплены
  `apt-mark hold`; сборка — `/root/src/seatd-0.7.0` на телефоне
  (нужен `scdoc`)
* действует после перезапуска phoc (`systemctl restart bf7-phosh`)

## phosh-0.24-torch-sysfs.patch

Плитка «Фонарик» в шторке phosh была, но ничего не делала: яркость phosh
ставит через `org.freedesktop.login1 … session/auto`.SetBrightness, а у нашей
оболочки нет сеанса logind (её запускает служба systemd), и ответ —
«Unknown object». Заплатка: если logind отказал, писать яркость прямо в
`brightness` устройства в sysfs (оболочка работает от root). Фонарь —
`white:torch` из `kernel-work/bf7-torch`.

* стоит как пакет `phosh 0.24.0-1+bf7.2` (вместе с no-wake-on-resume);
  собирается так же, как выше, второй строкой в `debian/patches/series`

**Ловушка:** `dpkg -i` своего пакета сбрасывает закрепление (`hold` →
`install`) — после установки заново `apt-mark hold phosh`, иначе первое же
`apt upgrade` вернёт пакетный phosh без заплаток. Проверено 2026-09-23 при
установке +bf7.2. Самопроверка следит за версией.

## phosh-0.24-notifications-translatable.patch

Заголовок «Notifications» в шторке (`src/ui/settings.ui`) не помечен
`translatable="yes"` — в русском интерфейсе висел по-английски. Перевод
«Уведомления» дописывает `bf7-perevody` в phosh.mo. Пакет `0.24.0-1+bf7.4`.

## phosh-0.24-lock-enabled.patch

Хозяин попросил убрать PIN с экрана блокировки, а в настройках такого
выключателя нет. Phosh 0.24 блокирует экран при каждом засыпании и простое и
ключ `org.gnome.desktop.screensaver lock-enabled` не читает (читают только
более новые версии). Саму блокировку не трогаем: от неё здесь зависит гашение
экрана (power-save). Заплатка: если `lock-enabled` = false, то при
перелистывании экрана блокировки на страницу ввода PIN оболочка сразу
считается разблокированной — свайп вверх, и всё.

* пакет `phosh 0.24.0-1+bf7.3` (третьей строкой в `debian/patches/series`
  после no-wake-on-resume и torch-sysfs), закреплён `apt-mark hold`;
* включить PIN обратно — одна команда в сеансе оболочки:
  `gsettings set org.gnome.desktop.screensaver lock-enabled true`
  (пересборка не нужна).

## modemmanager-1.20-number-plus.patch

MTK отдаёт в `+CLCC`/`+CLIP` международный номер (тип 145) без «+»: в
«Вызовах» входящие от хозяина шли как «7978…», исходящие — «+7978…», один
человек выглядел двумя номерами и не сопоставлялся с контактом. Для типа
145 без «+» плюс приписывается. Тесты MM (типы 161/129) не затронуты.
Пакет `1.20.4-1+bf7.9`.

## modemmanager-1.20-sim-operator-name.patch

Сеть второго оператора не сообщает имени, только код: ModemManager отдавал
в имени оператора «250xx», и phosh показывал цифры под значком связи. Если
имя состоит из одних цифр и совпадает с кодом оператора SIM (мы дома),
берётся имя поставщика услуг с SIM (SPN, здесь «OPERATOR2»). В базе
mobile-broadband-provider-info кода 250-xx нет. Пакет `1.20.4-1+bf7.8`.

## modemmanager-1.20-mtk-cgact-data.patch и modemmanager-1.20-mtk-no-clcc.patch

ModemManager 1.20.4 (плагин generic) с модемом MTK через мост `bf7-at-most`
(`/dev/ttyp0`). Обе заплатки включаются только метками udev из
`hybris/bf7-modem/77-bf7-modem.rules`, на другие модемы не влияют.

* **cgact-data** — мобильные данные. MTK не умеет `ATD*99#` (порт AT виснет),
  данные поднимаются `AT+CGACT=1,cid`; модем сам выбирает `ccmniN` и
  сообщает `+EIF: <id>, ifup` (N = id % 100). Адрес — `+CGPADDR`, DNS — свой
  разбор `+CGCONTRDP`. Метка `ID_MM_BF7_MTK_DATA` на порту AT и на ccmni.
  Перед этим `bf7-modem-sim.py` шлёт `AT+EDALLOW=1` (без него `CME 5745`).
* **no-clcc** — звонки. `+CLCC` у MTK пуст, пока входящий не одобрен:
  на `+EAIC: id,"номер",тип,режим,seq,…` MM отвечает `AT+EAIC=0,id,seq`
  (seq — ПЯТОЕ поле; не то — вызов сбрасывается через 20 с). Регулярка
  «в разговоре» без `$`: `NO CARRIER` приходит в одном куске с `+CIEV`.

* **ecpi** (`+bf7.4`, 2026-09-23 ночь) — исходящий вызов. Без `+CLCC` MM
  ставил вызову «active» сразу после OK на ATD, ещё до гудков: «Звонки»
  показывали идущий разговор, приветствие Клауда ушло в гудки. Теперь для
  MTK вызов создаётся с переходами dialing → ringing-out → active, их ведёт
  `+ECPI: <id>,<тип>` (2 — у абонента звонит, 6 — ответил). **Ставить только
  вместе с новым мостом `bf7-at-most`** (он начал пропускать `+ECPI` в MM):
  старый MM незнакомое сообщение посреди ответа на команду разобрал бы
  неверно. Регулярка `+ECPI` не должна съедать завершающий `<CR><LF>`
  (lookahead, `+bf7.5`): иначе идущий вплотную `+CIEV: 5, 1` попадал в ответ
  на ATD, и MM обрывал вызов («Unhandled response»). Проверено звонком
  хозяину 24.09 08:35: dialing → ringing-out → active по +ECPI, приветствие
  после ответа, «Слышу, всё прекрасно».

* **no-cops0** (`+bf7.6`, 2026-09-24) — при регистрации MM слал `AT+COPS=0`,
  и модем заново выбирал сеть — GSM, даже если bf7-modem-sim только что
  завёл его в LTE (сам с GSM в LTE он в этой сети не уходит). Модем MTK и так
  в автоматическом режиме — для него команда не шлётся.

* **bearer-monitor-30s** (`+bf7.7`, 2026-09-24) — проверка соединения данных
  раз в 30 с вместо 5. Каждая проверка — `AT+CGACT?` через мост, то есть
  пробуждение процессора и AT-порта модема каждые 5 с, пока подняты данные
  (видно `strace` на `bf7-modem-at`). Обрыв теперь замечается за 30 с.
  Заплатка общая (не по меткам udev): других модемов у телефона нет.

Вход в LTE при загрузке делает bf7-modem-sim: EIAAPN + CGDCONT=1 с APN
оператора (без контекста по умолчанию модем за 120 с в LTE не входил),
«только LTE» до регистрации (+ERAT 7 и +CEREG 1/5), затем ERAT=6,4.
Холодный старт: LTE через 5 с после запуска службы.

Дерево на телефоне `/root/src/modemmanager-1.20.4` (quilt), пакет
`1.20.4-1+bf7.7`, закреплён. Подробности — research-log, 2026-09-22/23.

## retroarch-1.14-vulkan.patch

RetroArch из Debian собран без Vulkan (`--disable-vulkan`,
`--disable-spirv_cross`). Заплатка включает драйвер `vulkan` и чинит сборку
с новым SPIRV-Cross (явное приведение `uint32_t(...id)` — у `TypedID` нет
неявного, тернарный оператор не компилируется).

Сборка на телефоне (`/root/src/retroarch/retroarch-1.14.0+dfsg`):

1. `apt source retroarch spirv-cross` — исходник spirv-cross нужен, потому
   что Debian вырезал `deps/SPIRV-Cross` из пакета RetroArch:
   `cp -a spirv-cross-2021.01.15+1.3.239.0 retroarch-1.14.0+dfsg/deps/SPIRV-Cross`;
2. `patch -p1 < retroarch-1.14-vulkan.patch`;
3. `dpkg-buildpackage -b -uc -us -nc` (~1 ч на телефоне; `-nc` —
   чтобы после исправления не собирать заново с нуля);
4. `dpkg -i retroarch_1.14.0+dfsg-1+bf7.1_arm64.deb && apt-mark hold retroarch`.

Там же (`+bf7.2`) — `gfx/common/wayland_common.c`: при масштабе вывода 2
(панель BF7) `gfx_ctx_wl_check_window_common` сравнивал уже умноженный на
масштаб размер с ещё раз умноженным и каждый кадр видел «изменение размера»:
на весь экран swapchain и все конвейеры пересоздавались ежекадрово (15 кадров
/с, ядро в компиляторе шейдеров). Внимание при пересборке с `-nc`: debhelper
считает сборку выполненной и молча переупаковывает старый двоичный файл —
перед `dpkg-buildpackage -nc` запустить `make V=1 GIT_VERSION=b2ceb50`.

И (`+bf7.3`) — `input/common/wayland_common.c`: координаты касаний не
умножались на `buffer_scale` (у мыши умножаются) — при масштабе 2 любое
касание попадало в левую верхнюю четверть, экранные кнопки не работали.
Проверено камерой: крестовина и A на экранных кнопках управляют µCity.

Запуск с Vulkan — только нашим загрузчиком (андроидный драйвер не ICD):
`LD_LIBRARY_PATH=/usr/local/lib LD_PRELOAD=/usr/local/lib/libbf7tlsguard.so
retroarch --appendconfig=<файл с video_driver = "vulkan">`.

## mpv-0.35-wayland-initial-configure.patch

mpv 0.35.1 показывал видео обрезанным: окно оставалось 1280×720 (при
масштабе 2 — шире экрана), хотя phoc разворачивает каждое окно и шлёт
`xdg_toplevel.configure(360, 734)`. Первое такое сообщение приходит раньше,
чем mpv задал геометрию, и отбрасывается — но размер при этом запоминается;
второе приходит с тем же размером, и проверка «ничего не изменилось» его
выбрасывает. Заплатка: при раннем выходе размер не запоминать.

Сборка на телефоне (`/root/src/mpv/mpv-0.35.1`): `apt-get source mpv`,
`apt-get build-dep mpv`, `patch -p1 < …`, версия `0.35.1-4+bf7.1` в
`debian/changelog`, `dpkg-buildpackage -b -uc -us`, затем
`dpkg -i mpv_*bf7.1*.deb libmpv2_*bf7.1*.deb && apt-mark hold mpv libmpv2`.

Системный `/etc/mpv/mpv.conf` (`units/mpv.conf`, ставит установщик) —
`hwdec=auto-copy`: 720p ~74% процессора на весь телефон вместо ~130%.
