#!/usr/bin/python3
# BF7: кормилец детектора зависаний MediaTek (hang_detect, /dev/RT_Monitor).
#
# Зачем. 2026-09-24 телефон дважды зависал так, что живым оставалось только
# ядро (ping), а ssh, консоль и запуск программ — нет; хозяин был не дома.
# В ядре есть hang_detect (drivers/misc/mediatek/monitor_hang): поток ядра
# раз в 30 с уменьшает счётчик и, дойдя до нуля дважды, перезагружает телефон.
# На Android его кормит system_server; у нас не кормил никто, и он то молчал,
# то срабатывал через 15-25 минут.
#
# Как. Раз в минуту проверяем, что система в целом жива: запускается новая
# программа и отвечает systemd (PID 1) — `systemctl show` с пределом 20 с.
# Прошло — кормим с тайм-аутом 300 с (меньше ядро не принимает). Не прошло или
# этот процесс сам застрял на запуске программы — кормёжки нет, и через ~5-6
# минут ядро перезагрузит телефон. При остановке службы вручную детектор
# выключается (HANG_KICK 0), чтобы обслуживание не вызвало перезагрузку; при
# выключении системы — остаётся взведённым (страховка от зависшего выключения).
#
# Во сне поток ядра стоит вместе со всем, счёт не идёт.
import fcntl
import os
import signal
import subprocess
import sys
import time

RT_MONITOR = "/dev/RT_Monitor"
# HANG_KICK = _IOR('p', 0x0A, int): dir=2 (read), size=4, type='p', nr=0x0a
HANG_KICK = (2 << 30) | (4 << 16) | (ord("p") << 8) | 0x0A
TAIMAUT_S = 300
SHAG_S = 60


def kormit(sekund):
    fd = os.open(RT_MONITOR, os.O_RDONLY)
    try:
        fcntl.ioctl(fd, HANG_KICK, sekund)
    finally:
        os.close(fd)


def sistema_zhiva():
    try:
        r = subprocess.run(["systemctl", "show", "-p", "Version", "--value"],
                           capture_output=True, timeout=20)
        return r.returncode == 0 and r.stdout.strip() != b""
    except (subprocess.TimeoutExpired, OSError):
        return False


def vyklyuchaetsya():
    try:
        r = subprocess.run(["systemctl", "is-system-running"], capture_output=True, timeout=5)
        return r.stdout.strip() == b"stopping"
    except (subprocess.TimeoutExpired, OSError):
        return True


def stop(signum, frame):
    try:
        if vyklyuchaetsya():
            # Выключение: детектор оставляем взведённым — зависнет выключение
            # (бывало после сна, раньше чем успевал запуститься
            # bf7-straza-vykl), и ядро перезагрузит телефон через 5 минут.
            kormit(TAIMAUT_S)
            print("bf7-hang-kick: выключение — детектор оставлен взведённым", flush=True)
        else:
            kormit(0)
            print("bf7-hang-kick: остановлен, детектор зависаний выключен", flush=True)
    finally:
        sys.exit(0)


def main():
    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    print("bf7-hang-kick: кормлю hang_detect раз в %d с, тайм-аут %d с" % (SHAG_S, TAIMAUT_S),
          flush=True)
    propushcheno = 0
    while True:
        if sistema_zhiva():
            kormit(TAIMAUT_S)
            if propushcheno:
                print("bf7-hang-kick: система снова отвечает", flush=True)
            propushcheno = 0
        else:
            propushcheno += 1
            print("bf7-hang-kick: systemd не ответил (%d раз подряд) — не кормлю" % propushcheno,
                  flush=True)
        time.sleep(SHAG_S)


if __name__ == "__main__":
    main()
