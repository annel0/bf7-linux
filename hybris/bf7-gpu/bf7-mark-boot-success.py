#!/usr/bin/env python3
"""BF7: отметить текущий слот успешно загрузившимся.

Зачем. Загрузчик A/B держит для каждого слота счётчик попыток и отнимает по
одной с каждой загрузкой, пока слот не отмечен успешным. Когда счётчик доходит
до нуля, телефон уходит на другой слот — у нас это слот B со стоковым ядром, и
возвращаться приходится через `fastboot set_active a` с кнопок.

Где лежит. Раздела `misc` на BF7 нет: структура `bootloader_control` живёт в
`para`, это `mmcblk0p3`, по смещению 2048. Каталога `/dev/block/` тоже нет —
узел зовётся просто `/dev/mmcblk0p3`.

Раскладка (32 байта, проверена сходящейся контрольной суммой 2026-09-22):

    0..3    суффикс текущего слота, «_a\0\0»
    4..7    магия 0x42414342
    8       версия
    9..11   число слотов, попытки восстановления, состояние слияния
    12..19  по два байта на слот; первый байт разбирается по битам:
            младшие 4 — приоритет, следующие 3 — оставшиеся попытки,
            старший — отметка успешной загрузки
    20..27  запас
    28..31  crc32 первых 28 байт, порядок младшим байтом вперёд

Осторожность. Пишем ровно 32 байта и только после того, как сошлись и магия, и
контрольная сумма. Если что-то не сошлось — не трогаем ничего: лучше остаться с
тающим счётчиком, чем испортить служебный раздел.
"""

import os
import struct
import sys
import zlib

PARA = "/dev/mmcblk0p3"
OFFSET = 2048
SIZE = 32
MAGIC = 0x42414342


def read_block():
    with open(PARA, "rb") as f:
        f.seek(OFFSET)
        return bytearray(f.read(SIZE))


def slot_index(block):
    suffix = block[0:4].split(b"\0")[0].decode("ascii", "replace")
    if suffix == "_a":
        return 0, suffix
    if suffix == "_b":
        return 1, suffix
    return None, suffix


def describe(byte):
    return (byte & 0x0F, (byte >> 4) & 0x07, (byte >> 7) & 0x01)


def main():
    if not os.path.exists(PARA):
        print(f"bf7-mark-boot-success: нет {PARA}", file=sys.stderr)
        return 1

    block = read_block()
    if len(block) != SIZE:
        print("bf7-mark-boot-success: не прочитались 32 байта", file=sys.stderr)
        return 1

    magic = struct.unpack_from("<I", block, 4)[0]
    if magic != MAGIC:
        print(f"bf7-mark-boot-success: не та магия ({magic:#x}), ничего не трогаю",
              file=sys.stderr)
        return 1

    stored = struct.unpack_from("<I", block, 28)[0]
    calc = zlib.crc32(bytes(block[:28])) & 0xFFFFFFFF
    if stored != calc:
        print(f"bf7-mark-boot-success: сумма не сходится "
              f"(в блоке {stored:08x}, посчитано {calc:08x}), ничего не трогаю",
              file=sys.stderr)
        return 1

    index, suffix = slot_index(block)
    if index is None:
        print(f"bf7-mark-boot-success: непонятный суффикс «{suffix}»", file=sys.stderr)
        return 1

    pos = 12 + index * 2
    was = block[pos]
    priority, tries, ok = describe(was)

    if ok and tries == 7:
        print(f"bf7-mark-boot-success: слот {suffix} уже отмечен "
              f"(приоритет {priority}, попыток {tries})")
        return 0

    # Приоритет оставляем как есть: его расставляет загрузчик при смене слота.
    # Попытки поднимаем до потолка, отметку успеха ставим.
    block[pos] = (priority & 0x0F) | (7 << 4) | (1 << 7)
    struct.pack_into("<I", block, 28, zlib.crc32(bytes(block[:28])) & 0xFFFFFFFF)

    fd = os.open(PARA, os.O_WRONLY | os.O_SYNC)
    try:
        os.lseek(fd, OFFSET, os.SEEK_SET)
        written = os.write(fd, bytes(block))
        os.fsync(fd)
    finally:
        os.close(fd)

    if written != SIZE:
        print(f"bf7-mark-boot-success: записалось {written} байт вместо {SIZE}",
              file=sys.stderr)
        return 1

    check = read_block()
    if check != block:
        print("bf7-mark-boot-success: перечитанное не совпало с записанным",
              file=sys.stderr)
        return 1

    now = describe(check[pos])
    print(f"bf7-mark-boot-success: слот {suffix} отмечен успешным "
          f"(было: приоритет {priority}, попыток {tries}, успех {ok}; "
          f"стало: приоритет {now[0]}, попыток {now[1]}, успех {now[2]})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
