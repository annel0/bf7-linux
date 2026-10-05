#!/usr/bin/python3
"""BF7: ночной свет — тёплый экран по расписанию gsd-color.

Почему своя служба. gsd-color ведёт ночной свет сам (расписание, плавный
переход, температура в свойстве Temperature), но выставить гамму не может:
он просит phosh (SetCrtcGamma), phosh — phoc, phoc — wlroots, а наш драйвер
KMS (bf7_drm) свойства GAMMA_LUT не объявляет. В тракте MediaTek при этом
есть свой блок гаммы (disp_gamma, таблица на 512 входов по 10 бит на канал),
и вендорский ioctl DISP_IOCTL_SET_GAMMALUT на /dev/mtk_disp_mgr пишет его
через очередь команд дисплея. Проверено камерой 2026-09-30: фон кадра уходит
в синеву — автобаланс камеры подстраивается под пожелтевший экран.

Ловушка вендорского пути: пока тракт экрана спит (DISP_SLEPT), команда
выбрасывается молча и таблица не запоминается. Поэтому после каждого
пробуждения (PowerSaveMode у phosh → 0) таблица пишется заново. Записанная
при живом тракте сон переживает: disp_gamma_start ставит её при запуске пути.

Только сигналы D-Bus, никакого опроса: в покое служба не просыпается.
Живёт в сеансе (запуск из bf7-phosh-session): ей нужна сеансовая шина, и
вместе с ней она и уходит.

  bf7-nochnoj-svet                — служба
  bf7-nochnoj-svet --pereklyuchit — включить/выключить (запись в сетке)
  bf7-nochnoj-svet --temperatura K — поставить K сразу (проверка)
"""
import datetime
import fcntl
import os
import struct
import sys

import gi
gi.require_version("Colord", "1.0")
from gi.repository import Colord, Gio, GLib

UZEL = "/dev/mtk_disp_mgr"
# _IOW('x', 23, struct DISP_GAMMA_LUT_T): hw_id (enum, 4 байта) + 512 слов
RAZMER = 4 + 512 * 4
SET_GAMMALUT = (1 << 30) | (RAZMER << 16) | (ord("x") << 8) | 23

CVET = "org.gnome.SettingsDaemon.Color"
CVET_PUT = "/org/gnome/SettingsDaemon/Color"
EKRAN = "org.gnome.Mutter.DisplayConfig"
EKRAN_PUT = "/org/gnome/Mutter/DisplayConfig"
SHEMA = "org.gnome.settings-daemon.plugins.color"


def mnozhiteli(temperatura):
    """Множители R, G, B для цветовой температуры — как у gsd-color."""
    if temperatura >= 6500:
        return (1.0, 1.0, 1.0)
    c = Colord.ColorRGB()
    Colord.color_get_blackbody_rgb_full(float(temperatura), c,
                                        Colord.ColorBlackbodyFlags.USE_PLANCKIAN)
    return (c.R, c.G, c.B)


def zapisat(k):
    tablica = [0]
    for i in range(512):
        v = [min(1023, int(i * 1023 / 511 * m + 0.5)) for m in k]
        tablica.append(v[0] << 20 | v[1] << 10 | v[2])
    fd = os.open(UZEL, os.O_RDWR | os.O_CLOEXEC)
    try:
        # изменяемый буфер: неизменяемый длиннее 1024 байт fcntl не примет
        fcntl.ioctl(fd, SET_GAMMALUT, bytearray(struct.pack("<i512I", *tablica)), True)
    finally:
        os.close(fd)


class Sluzhba:
    def __init__(self):
        self.shina = Gio.bus_get_sync(Gio.BusType.SESSION)
        self.shina.connect("closed", lambda *a: self.cikl.quit())
        self.cikl = GLib.MainLoop()
        self.aktiven, self.temp = False, 6500
        self.postavleno = None
        self.shina.signal_subscribe(CVET, "org.freedesktop.DBus.Properties",
                                    "PropertiesChanged", CVET_PUT, None,
                                    Gio.DBusSignalFlags.NONE, self.cvet_izmenilsya)
        self.shina.signal_subscribe(EKRAN, "org.freedesktop.DBus.Properties",
                                    "PropertiesChanged", EKRAN_PUT, None,
                                    Gio.DBusSignalFlags.NONE, self.ekran_izmenilsya)
        # gsd-color перезапустился — прочитать его состояние заново
        Gio.bus_watch_name_on_connection(self.shina, CVET,
                                         Gio.BusNameWatcherFlags.NONE,
                                         lambda *a: self.prochitat(), None)

    def prochitat(self):
        for imya in ("NightLightActive", "Temperature"):
            try:
                v = self.shina.call_sync(CVET, CVET_PUT, "org.freedesktop.DBus.Properties",
                                         "Get", GLib.Variant("(ss)", (CVET, imya)),
                                         None, Gio.DBusCallFlags.NONE, 2000, None)
                self.obnovit({imya: v.unpack()[0]})
            except GLib.Error:
                pass
        self.primenit()

    def obnovit(self, izm):
        if "NightLightActive" in izm:
            self.aktiven = bool(izm["NightLightActive"])
        if "Temperature" in izm:
            self.temp = int(izm["Temperature"])

    def cvet_izmenilsya(self, con, otp, put, iface, sig, par):
        self.obnovit(par.unpack()[1])
        self.primenit()

    def ekran_izmenilsya(self, con, otp, put, iface, sig, par):
        izm = par.unpack()[1]
        if izm.get("PowerSaveMode") == 0:
            # тракт экрана только что проснулся; дать ему поднять путь
            GLib.timeout_add(1000, self.primenit, True)

    def primenit(self, zanovo=False):
        k = mnozhiteli(self.temp if self.aktiven else 6500)
        k = tuple(round(x, 3) for x in k)
        if k != self.postavleno or zanovo:
            try:
                zapisat(k)
                self.postavleno = k
            except OSError as e:
                print("bf7-nochnoj-svet:", e, file=sys.stderr)
        return False

    def zapusk(self):
        self.prochitat()
        self.cikl.run()


def pereklyuchit():
    s = Gio.Settings.new(SHEMA)
    vkl = not s.get_boolean("night-light-enabled")
    s.set_boolean("night-light-enabled", vkl)
    Gio.Settings.sync()
    if vkl:
        s_ch, do_ch = s.get_double("night-light-schedule-from"), s.get_double("night-light-schedule-to")
        chas = lambda x: f"{int(x):02d}:{int(round(x % 1 * 60)):02d}"
        teper = datetime.datetime.now()
        h = teper.hour + teper.minute / 60
        noch = (h >= s_ch or h < do_ch) if s_ch > do_ch else (s_ch <= h < do_ch)
        tekst = (f"Включён, до {chas(do_ch)}" if noch
                 else f"Включится в {chas(s_ch)} и погаснет в {chas(do_ch)}")
    else:
        tekst = "Выключен"
    # уведомление напрямую через шину: notify-send (libnotify-bin) не стоит
    Gio.bus_get_sync(Gio.BusType.SESSION).call_sync(
        "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
        "org.freedesktop.Notifications", "Notify",
        GLib.Variant("(susssasa{sv}i)", ("Ночной свет", 0, "night-light-symbolic",
                                         "Ночной свет", tekst, [], {}, 4000)),
        None, Gio.DBusCallFlags.NONE, 3000, None)


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--pereklyuchit":
        pereklyuchit()
    elif len(sys.argv) > 2 and sys.argv[1] == "--temperatura":
        zapisat(mnozhiteli(int(sys.argv[2])))
    else:
        Sluzhba().zapusk()
