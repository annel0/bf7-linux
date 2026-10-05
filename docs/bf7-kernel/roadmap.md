# Безопасный план собственного BF7 kernel

## Принцип

Не делать «всё сразу». Новый kernel должен проходить минимальные этапы, каждый
из которых отделён сборочным ID, логом, проверкой загрузки и возможностью
вернуться к current known-good native boot image.

## Этап 0 — только локальная подготовка

- Зафиксировать commit выбранного source tree.
- Сохранить SHA-256 OEM kernel/DTB/config и входящих firmware.
- Зафиксировать actual boot metadata из `boot_b-before-native.img`: Android
  header v2, page 2048, product `CY-BF7-H6127`, kernel address `0x40080000`,
  ramdisk `0x51b00000`, tags/DTB `0x47880000`.
- Получить machine-readable diff OEM config ↔ baseline config.
- Составить список BF7 DT nodes и соответствующих Kconfig symbols.
- Никаких образов телефона не менять.

## Этап 1 — compile-only baseline

- Собрать ALPS snapshot `6f3a19332a49` после merge Android stable 4.19.191
  точным OEM Android Clang `r383902`.
- Проверить, что получаются `Image.gz`/DTB и что отсутствуют неразрешённые
  symbol errors.
- Это доказывает только воспроизводимость host toolchain, не BF7 compatibility.

Статус 2026-08-23: выполнено. Exact Clang 11 clean build воспроизводим между
разными `O=` после нормализации DWARF build-prefix; финальный `Image` SHA-256
начинается с `093828bab73a`.

## Этап 2 — BF7 configuration and DTB audit

- Перенести **копию** OEM config через `olddefconfig`, документируя каждое
  расхождение.
- Анализировать exact OEM BF7 DTB отдельно от X657B DTS; не пытаться вручную
  «сделать похожий» DTS.
- Выявить несовпадающие драйверы: тач, камера, audio route, sensors, radio.

Статус 2026-08-23: выполнено для первого baseline. Полный diff и 107
отсутствующих OEM symbols сохранены отдельными файлами. Display восстановлен,
Wi-Fi собран внешними matching modules; touch/camera/Transsion glue остаются
явно открытыми пунктами.

## Этап 3 — минимальный boot candidate

- Паковать kernel только в проверенную схему boot header v2 с указанными выше
  адресами, page size и product name.
- Использовать current known-good native initramfs с предсказуемым RNDIS и
  массовым USB storage/log fallback.
- Не записывать ни `boot_a`, ни `boot_b`, пока exact candidate не прошёл
  временную загрузку. Сейчас LK BF7 аварийно завершает downloaded-image path
  даже на known-good image, поэтому этот этап заблокирован.
- Первый критерий успеха: kernel boot log, USB/RNDIS, watchdog-safe return.

Статус 2026-08-23: образ v2 воспроизводимо собран и проходит offline preflight,
но этап runtime заблокирован неисправным `fastboot boot` path в LK. `kexec` в
known-good OEM kernel отключён. Запись в слот не является допустимым обходом.

Дополнение: trial v3 добавляет exact AW32257 как manual-only module и также
проходит offline preflight. Это не меняет блокирующее условие первого запуска.

## Этап 4 — hardware enablement

Порядок: eMMC → framebuffer → USB → input keys → touch → Wi-Fi → audio →
battery telemetry. Для каждого блока — один patch series и отдельный test.

## Этап 5 — только после baseline

- Включение `CONFIG_DEVTMPFS=y` как самостоятельный patch. Это должно убрать
  необходимость ручного `bf7-devnodes`, но может проявить device nodes,
  которые раньше были скрыты. Их нельзя автоматически открывать из userspace.
- GPU: только с firmware, matching DDK и заранее подготовленным panic/log
  recovery plan.
- LTE, BT/GNSS, camera — отдельные проекты, не условия первого native kernel.

## Стоп-условия

Немедленно остановить текущую ветку эксперимента, если нет RNDIS/telnet
наблюдаемости, неизвестен active slot, нет known-good boot image или требуется
`mtkclient`. Последнее запрещено без отдельного явного согласования. Любой
экспериментальный BF7 image сначала должен пройти `fastboot boot`; до починки
этого пути разрешены только локальная сборка, упаковка и offline preflight.

## Отложено: настоящий композитор Wayland вместо QML-заглушки

Сейчас рабочий стол устроен так: `bf7gui` (композитор на Qt Quick) держит
единственный полноэкранный клиент — Xwayland, а в нём живёт Xfce. Всякий
второй родной клиент Wayland тоже разворачивается во весь экран и накрывает
рабочий стол собой: управления окнами у нас нет, `sendFullscreen` уходит
всем без разбора.

Правильное решение — заменить `bf7gui` настоящим композитором на DRM/GBM
(labwc, weston или подобный). Тогда окна Wayland и окна X11 живут под одним
управлением, с рамками, панелью задач и переключением.

Заготовки под это уже есть: свой драйвер KMS (`card1`, `bf7_drm.c`) и свой
бэкенд GBM поверх gralloc, проверенный ускоренным Xwayland.

Чего не хватает и почему отложено: настоящему композитору нужен полноценный
KMS — атомарная смена режима, плоскости, вертикальная синхронизация,
`drmModePageFlip`. Наш `bf7_drm.c` минимальный. Плюс нашему EGL придётся
принимать `gbm_surface` в `eglCreateWindowSurface`, чего модуль «null» не
умеет. Объём работы заметно больше, чем у пути через X11 (см. ниже), а
выигрыш для пользователя тот же — окна ведут себя как окна.

Решение от 2026-09-16: сначала делаем модуль X11 для libhybris (DRI3 +
Present), потому что он даёт нативные окна в уже работающем Xfce и попутно
переводит на железо **все** программы X11, а не только браузер. Настоящий
композитор — следующим заходом, когда дозреет KMS.
