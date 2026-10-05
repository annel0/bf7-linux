# TECNO BF7: native Debian server

## Что это

Телефон TECNO BF7 (MediaTek MT6761) загружается в штатное Android-ядро
`4.19.191`, но вместо Android userspace монтирует постоянный Debian 12 с eMMC
и запускает `systemd` как PID 1. Это **native Linux**, а не chroot, proot или
Termux-контейнер.

Android в слоте `a` сохранён как аварийная система. Native Debian использует
слот `b`, который помечен bootloader'ом как successful. Загрузочные разделы не
следует менять без отдельной резервной копии и понятного плана отката.

## Состояние на момент фиксации

| Компонент | Состояние | Примечание |
|---|---|---|
| Постоянный Debian rootfs | работает | `/dev/mmcblk0p48`, ext4, около 50 GiB |
| SSH | работает | root по ключу, Wi-Fi и USB-RNDIS |
| Wi-Fi | работает | штатный MediaTek driver + firmware |
| Экран | работает | сырой framebuffer `/dev/fb0` |
| Тач | работает | Synaptics/OmniVision TCM, `/dev/input/event3` |
| Звук через динамик | работает | ALSA + штатные mixer-маршруты |
| GUI shell | прототип | socket compositor, launcher, touch capture; отключён по умолчанию |
| CPU / RAM / eMMC | работает | 4 ядра до 2.0 GHz, 3.72 GiB RAM |
| GPU 3D | не используется | Решение принято сознательно, см. [gpu.md](gpu.md) |
| Камера | **работает** | `/dev/video0`, 30 к/с, ручки V4L2, фотография; см. [camera.md](camera.md) |
| LTE | не проверен | в телефоне не было SIM |

## Карта документации
- [api.md](api.md) — локальные интерфейсы: экран, тач, звук, device nodes.
- [camera.md](camera.md) — камера: `/dev/video0`, настройка из обычных
  программ, фотография, сложение кадров и тёмный кадр.
- [networking.md](networking.md) — Wi-Fi, SSH, USB-RNDIS и время.
- [recovery-shell.md](recovery-shell.md) — аварийный RNDIS root-shell,
  telnet-negotiation, USB reset и передача бинарников без `wget`.
- [boot-and-services.md](boot-and-services.md) — запуск Debian и постоянные
  systemd-сервисы.
- [operations-and-troubleshooting.md](operations-and-troubleshooting.md) —
  обычные операции и диагностические рецепты.
- [gui-shell.md](gui-shell.md) — прототип безопасной GUI-прослойки и её
  локальный socket API.
- [kernel-driver-inventory.md](kernel-driver-inventory.md) — матрица OEM
  драйверов, Android-зависимостей и практической возможности native-подъёма.
- [gpu.md](gpu.md) — PowerVR Rogue: почему `renderD128` отдаёт ENODEV, чем это
  лечится и почему аппаратный GLES решено не поднимать.
- [../bf7-kernel/README.md](../bf7-kernel/README.md) — отдельный журнал
  поиска исходников и план собственного BF7 kernel-порта.

## Важные правила безопасности

1. Не перезаписывать `boot_a`: это известная рабочая Android-спасалка.
2. Не использовать `mtkclient` без отдельного явного согласования: MediaTek
   режимы низкого уровня легко превратить в тяжёлое восстановление.
3. Перед экспериментами с boot-образом проверять активный слот через fastboot.
4. Не хранить Wi-Fi пароль и приватный SSH-ключ в репозитории или этой папке.
5. Не читать AppHint-параметры `pvrsrvkm`, например
   `/sys/module/pvrsrvkm/parameters/DisableClockGating`: пока GPU firmware не
   установлена, это роняет ядро в NULL dereference. Подробности и лечение —
   [gpu.md](gpu.md).

## Где лежит документация

- Рабочая копия: `docs/bf7-native/` в проекте.
- Копия на телефоне: `/opt/bf7-docs/`.
