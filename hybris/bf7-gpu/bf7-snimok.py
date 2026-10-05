#!/usr/bin/python3
"""BF7: снимок экрана — запись «Снимок экрана» в сетке Phosh.

У телефона нет клавиши Print, а сочетание кнопок в phosh 0.24 не предусмотрено
(снимок есть только по D-Bus org.gnome.Shell.Screenshot). Эта запись ждёт
3 с — сетка успевает закрыться и показать нужное окно, — снимает экран и
кладёт PNG в «Изображения/Снимки экрана», уведомление показывает превью.

Снимает grim, а не phosh: у phosh снимок в логических точках (360×806 при
масштабе 2), grim — в родных 720×1612.

  bf7-snimok [секунд]   — пауза перед снимком (по умолчанию 3)
"""
import datetime
import os
import subprocess
import sys
import time

from gi.repository import Gio, GLib


def papka():
    try:
        p = subprocess.run(["xdg-user-dir", "PICTURES"], capture_output=True,
                           text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        p = os.path.expanduser("~/Pictures")
    p = os.path.join(p, "Снимки экрана")
    os.makedirs(p, exist_ok=True)
    return p


def uvedomit(zagolovok, tekst, kartinka=""):
    try:
        hints = {"image-path": GLib.Variant("s", kartinka)} if kartinka else {}
        Gio.bus_get_sync(Gio.BusType.SESSION).call_sync(
            "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
            "org.freedesktop.Notifications", "Notify",
            GLib.Variant("(susssasa{sv}i)", ("Снимок экрана", 0, "applets-screenshooter",
                                             zagolovok, tekst, [], hints, 5000)),
            None, Gio.DBusCallFlags.NONE, 3000, None)
    except GLib.Error:
        pass


def main():
    pauza = float(sys.argv[1]) if len(sys.argv) > 1 else 3
    time.sleep(pauza)
    imya = datetime.datetime.now().strftime("Снимок %Y-%m-%d %H-%M-%S.png")
    put = os.path.join(papka(), imya)
    r = subprocess.run(["grim", put], capture_output=True, text=True)
    if r.returncode != 0 or not os.path.exists(put):
        uvedomit("Не вышло", (r.stderr or "grim не сохранил снимок").strip())
        return 1
    uvedomit("Сохранён", "Изображения/Снимки экрана/" + imya, put)
    return 0


if __name__ == "__main__":
    sys.exit(main())
