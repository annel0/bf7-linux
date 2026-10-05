#!/usr/bin/env python3
"""Мини-замена logd: печатает то, что андроидные библиотеки шлют в liblog.

liblog пишет датаграммы в юникс-сокет /dev/socket/logdw. Без logd сообщения
ALOGE/ALOGW просто пропадают, и диагностика из libEGL, gralloc и HAL не видна.
Формат: android_log_header_t {u8 id; u16 tid; u32 sec; u32 nsec}, дальше
приоритет (1 байт), tag\\0, message\\0.

Пишет сам в /var/log/bf7-android.log и при 4 МБ переносит его в .1 (одна
старая копия). Раньше вывод шёл через StandardOutput=append: без ротации, а
logrotate на телефоне нет — к 2026-09-30 набралось 11 МБ (блобы GPU, WMT), а
mnld при включённом GPS пишет десятки строк в секунду.
"""
import os, socket, struct, sys, datetime

PATH = "/dev/socket/logdw"
ZHURNAL = "/var/log/bf7-android.log"
PREDEL = 4 << 20
BUF_IDS = {0: "main", 1: "radio", 2: "events", 3: "system", 4: "crash", 5: "stats", 6: "security", 7: "kernel"}
PRIO = {0: "?", 1: "?", 2: "V", 3: "D", 4: "I", 5: "W", 6: "E", 7: "F", 8: "S"}

os.makedirs("/dev/socket", exist_ok=True)
try:
    os.unlink(PATH)
except FileNotFoundError:
    pass

s = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
s.bind(PATH)
os.chmod(PATH, 0o666)
try:
    s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1 << 20)
except OSError:
    pass
print(f"minilogd слушает {PATH}", file=sys.stderr, flush=True)


def otkryt():
    return open(ZHURNAL, "a", buffering=1, encoding="utf-8")


f = otkryt()

while True:
    data = s.recv(65536)
    if len(data) < 11:
        continue
    bid, tid, sec, nsec = struct.unpack_from("<BHII", data, 0)
    body = data[11:]
    prio = body[0] if body else 0
    parts = body[1:].split(b"\0")
    tag = parts[0].decode("utf-8", "replace") if parts else ""
    msg = parts[1].decode("utf-8", "replace") if len(parts) > 1 else ""
    ts = datetime.datetime.fromtimestamp(sec).strftime("%m-%d %H:%M:%S") + f".{nsec // 1000000:03d}"
    f.write(f"{ts} {BUF_IDS.get(bid, bid)} {PRIO.get(prio, '?')} {tag}: {msg}\n")
    if f.tell() > PREDEL:
        f.close()
        os.replace(ZHURNAL, ZHURNAL + ".1")
        f = otkryt()
