# BF7: собственное ядро — журнал исследования

Эта папка — постоянная рабочая документация по пути от заводского Android
kernel к самостоятельно собираемому ядру для native Debian на TECNO BF7.
Она не является инструкцией по прошивке. Состояние устройства и каждый
выполненный аппаратный тест фиксируются отдельно в журнале.

## Цель

Получить воспроизводимое Linux 4.19-ядро для BF7 на базе открытого MediaTek
кода, с сохранением работоспособности native Debian. Заводской `boot_a` с
Android остаётся аварийным путём; эксперименты допустимы только как отдельные
образы для временной RAM-загрузки и только после доказанного плана отката.

## Идентичность целевого устройства

| Поле | Значение |
|---|---|
| Телефон | TECNO BF7 / Spark Go 2023 / Pop 7 Pro |
| Board/product | `TECNO-BF7`, `bf7_h6127`, `CY-BF7-H6127` |
| SoC | MediaTek MT6761V/WBB (Helio A22) |
| Архитектура | arm64, 4 x Cortex-A53 |
| OEM kernel | `4.19.191-g216400aba14e-dirty` |
| OEM kernel build | 2024-12-07 22:52:37 CST |
| Заводской vendor fingerprint | `TECNO/BF7-GL/TECNO-BF7:12/SP1A.210812.016/241207V1848:user/release-keys` |
| Boot header | v2, page 2048; product `CY-BF7-H6127` |
| OEM boot layout | kernel `0x40080000`, ramdisk `0x51b00000`, tags `0x47880000`, DTB `0x47880000` |
| Базовая MTK family | `k61v1_64_bsp` |

## Карта файлов

- [sources.md](sources.md) — все внешние репозитории и результаты проверки.
- [compatibility-matrix.md](compatibility-matrix.md) — что уже закрыто кодом,
  заводскими бинарными артефактами и BF7-specific данными.
- [roadmap.md](roadmap.md) — безопасный план самостоятельного kernel-порта.
- [research-log.md](research-log.md) — хронологический журнал проверок и
  решений; новые находки добавляются сюда и в соответствующую таблицу.
- [alps191-v1-status.md](alps191-v1-status.md) — актуальный итог основной
  ветки, хэши воспроизводимой сборки, пределы доказанного и план runtime-теста.
- [fastboot-ramboot-analysis.md](fastboot-ramboot-analysis.md) — почему
  текущий LK не даёт считать `fastboot boot` рабочим и какой минимальный
  legacy probe подготовлен для будущего отдельного теста.
- [first-boot-runbook.md](first-boot-runbook.md) — двухэтапный автономный
  runtime-test с host monitoring, программным возвратом и watchdog fallback.
- [morning-report-2026-08-23.md](morning-report-2026-08-23.md) — готовый
  артефакт, финальные SHA, выполненные проверки и честная граница результата.
- [slot-test-2026-09-11.md](slot-test-2026-09-11.md) — первый аппаратный
  запуск кандидата через A/B-слот: дисплей работает, фикс videolfb, GPU открыт.
- [uart-hardware-access.md](uart-hardware-access.md) — точные фотографии платы,
  найденные service/schematic источники и безопасный план пассивного поиска
  аппаратного UART.
- [oem-vs-alps191.config-diff.txt](oem-vs-alps191.config-diff.txt) — полный
  машинный diff конфигураций.
- [oem-symbols-missing-from-alps191.txt](oem-symbols-missing-from-alps191.txt)
  — 107 OEM/Transsion Kconfig-символов, которых нет в открытой базе.

Существующий практический инвентарь работающего OEM-ядра —
[../bf7-native/kernel-driver-inventory.md](../bf7-native/kernel-driver-inventory.md).
Документация по PowerVR и известному риску kernel panic —
[../bf7-native/gpu.md](../bf7-native/gpu.md).

## Правила ведения

1. Для каждой внешней находки записывать точный URL, ветку/commit, версию
   Linux, источник происхождения и вывод о применимости.
2. Отделять подтверждённые факты от гипотез.
3. Не считать совпадение SoC доказательством совместимости устройства:
   экран, тач, питание, камеры, аудио и радиочасть задаются DTB и vendor ABI.
4. Любой будущий образ, сборка или тест должны иметь отдельную запись в
   `research-log.md`, контрольные хэши и план возврата к known-good boot image.
5. `mtkclient` не использовать без отдельного явного согласования.
