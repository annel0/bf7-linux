#!/usr/bin/python3
# BF7: будильник, который звонит и во сне телефона.
#
# Будильники заводятся как обычно — в GNOME Clocks. Но Clocks 43 проверяет их
# только пока открыто его окно (тикает AlarmFace), а систему из сна не будит
# вовсе. С автосном на батарее (dconf/00-bf7) это значит: телефон уснул через
# десять минут простоя — и утром тишина.
#
# Эта служба:
#   1. читает будильники из настроек Clocks (org.gnome.clocks alarms: hour,
#      minute, active, days 1=пн…7=вс — формат из alarm-item.vala) и ищет
#      ближайший звонок по тем же правилам, что Clocks;
#   2. держит на него один таймер systemd с WakeSystem=yes: ядро будит
#      телефон через RTC (проверено 2026-09-23 на лёгком сне), а сам таймер в
#      миг звонка шлёт службе SIGUSR1 — таймеры systemd идут по настенным
#      часам и срабатывают верно и после сна. Своих опросов у службы нет;
#   3. в миг звонка зажигает экран (bf7-knopka wakeup — «действие
#      пользователя», чтобы gsd-power сам вышел из простоя) и держит запрет гашения и сна у
#      gnome-session — иначе gsd, считающий сеанс простаивающим, усыпил бы
#      телефон прямо во время звонка;
#   4. если Clocks запущен — звонит он сам; если нет — звонит служба:
#      feedbackd «alarm-clock-elapsed» (звук и нарастающая вибрация) плюс
#      уведомление с кнопками «Стоп» и «Отложить». Кнопка питания во время
#      звонка — «отложить» (спросонья первым жмут её, а кнопок уведомления на
#      экране блокировки не видно).
#
# Шину сеанса берём из окружения phosh (как bf7-otperet). Сеанс
# перезапустился — шина пропала — выходим, systemd поднимет заново.
#
# Проверка: tools/bf7-budilnik-proverka.sh.
import datetime
import os
import signal
import struct
import subprocess
import sys
import time

TAJMER = "bf7-budilnik-zvonok"
ZVONIT_MINUT = 5            # как ring_minutes у Clocks по умолчанию
OPOZDANIE_MAKS = 30 * 60    # позже этого пропущенный звонок уже не звоним


def log(*a):
    print("bf7-budilnik:", *a, file=sys.stderr, flush=True)


def shina_seansa(predel=120):
    """Адрес шины сеанса из окружения процесса phosh; ждём не дольше предела."""
    konec = time.monotonic() + predel
    while time.monotonic() < konec:
        for pid in filter(str.isdigit, os.listdir("/proc")):
            try:
                if os.readlink(f"/proc/{pid}/exe").endswith("/phosh"):
                    with open(f"/proc/{pid}/environ", "rb") as f:
                        for s in f.read().split(b"\0"):
                            if s.startswith(b"DBUS_SESSION_BUS_ADDRESS="):
                                return s.split(b"=", 1)[1].decode()
            except OSError:
                pass
        time.sleep(2)
    return None


adres = shina_seansa()
if not adres:
    log("нет сеанса phosh — выхожу, systemd перезапустит")
    sys.exit(1)
os.environ["DBUS_SESSION_BUS_ADDRESS"] = adres

import gi  # noqa: E402  — после выставления шины сеанса
gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib  # noqa: E402


def blizhajshij(budilniki, sejchas):
    """(время, будильник) ближайшего звонка строго позже sejchas — как в Clocks."""
    luchshij = None
    for b in budilniki:
        if not b.get("active", True):
            continue
        chas, minuta = b.get("hour"), b.get("minute")
        if chas is None or minuta is None:
            continue
        dni = [d for d in b.get("days", []) if 1 <= d <= 7]
        t = sejchas.replace(hour=chas, minute=minuta, second=0, microsecond=0)
        if not dni:
            if t <= sejchas:
                t += datetime.timedelta(days=1)
        else:
            for _ in range(8):
                if t > sejchas and t.isoweekday() in dni:
                    break
                t += datetime.timedelta(days=1)
            else:
                continue
        if luchshij is None or t < luchshij[0]:
            luchshij = (t, b)
    return luchshij


class Budilnik:
    def __init__(self):
        self.seans = Gio.bus_get_sync(Gio.BusType.SESSION)
        self.sistema = Gio.bus_get_sync(Gio.BusType.SYSTEM)
        self.seans.connect("closed", self.shina_zakryta)
        self.nastrojki = Gio.Settings.new("org.gnome.clocks")
        self.nastrojki.connect("changed::alarms", lambda *a: self.peresmotret("изменены будильники"))
        self.sistema.signal_subscribe(
            "org.freedesktop.login1", "org.freedesktop.login1.Manager",
            "PrepareForSleep", "/org/freedesktop/login1", None,
            Gio.DBusSignalFlags.NONE, self.son, None)
        self.seans.signal_subscribe(
            "org.freedesktop.Notifications", "org.freedesktop.Notifications",
            None, "/org/freedesktop/Notifications", None,
            Gio.DBusSignalFlags.NONE, self.uvedomlenie, None)
        GLib.unix_signal_add(GLib.PRIORITY_DEFAULT, signal.SIGUSR1, self.signal_tajmera)
        self.zhdu = None          # (время, будильник) ближайшего звонка
        self.otlozhen = None      # (время, будильник) отложенного звонка
        self.zaveden_na = None    # время, на которое стоит таймер systemd
        self.zvon = None          # состояние идущего звонка
        self.peresmotret("запуск")

    # --- события ---------------------------------------------------------

    def shina_zakryta(self, *a):
        log("шина сеанса закрыта — выхожу")
        GLib.idle_add(lambda: sys.exit(1))

    def son(self, con, sender, path, iface, sig, params, data):
        if not params.unpack()[0]:
            self.peresmotret("проснулись")

    def signal_tajmera(self):
        self.peresmotret("таймер")
        return GLib.SOURCE_CONTINUE

    def dozhech_ekran(self):
        if not self.zvon:
            return False
        try:
            with open("/sys/class/drm/card1-DSI-1/enabled") as f:
                gorit = f.read().strip() == "enabled"
        except OSError:
            gorit = True
        if not gorit:
            log("экран не загорелся от WAKEUP — нажимаю кнопку питания")
            try:
                p = subprocess.Popen(["/usr/local/bin/bf7-knopka"],
                                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                GLib.child_watch_add(GLib.PRIORITY_DEFAULT, p.pid, lambda pid, st: p.poll())
            except OSError as e:
                log("не зажёг экран:", e)
        return False

    # --- кнопка питания во время звонка ------------------------------

    def slushat_knopki(self):
        """Следить за всеми устройствами с KEY_POWER, пока звонит."""
        self.knopki = {}
        self.monitor_vvoda = Gio.File.new_for_path("/dev/input").monitor_directory(
            Gio.FileMonitorFlags.NONE, None)
        self.monitor_vvoda.connect("changed", self.novoe_ustrojstvo)
        for imya in os.listdir("/dev/input"):
            self.otkryt_knopku(imya)

    def novoe_ustrojstvo(self, monitor, fajl, drugoj, sobytie):
        if sobytie == Gio.FileMonitorEvent.CREATED:
            # Узел появляется раньше, чем udev даст права, — чуть подождём.
            imya = fajl.get_basename()
            GLib.timeout_add(300, lambda: self.otkryt_knopku(imya) and False)

    def otkryt_knopku(self, imya):
        if not imya.startswith("event") or imya in self.knopki or not self.zvon:
            return False
        # Своя виртуальная кнопка (дожечь экран, см. dozhech_ekran) — не хозяин.
        try:
            with open(f"/sys/class/input/{imya}/device/name") as f:
                if f.read().strip() == "bf7-knopka":
                    return False
        except OSError:
            pass
        try:
            with open(f"/sys/class/input/{imya}/device/capabilities/key") as f:
                slova = f.read().split()
        except OSError:
            return False
        # Битовая маска клавиш: слова по 64 бита, старшее слово первым.
        bity = int("".join(w.rjust(16, "0") for w in slova), 16) if slova else 0
        if not bity >> 116 & 1:          # KEY_POWER = 116
            return False
        try:
            fd = os.open(f"/dev/input/{imya}", os.O_RDONLY | os.O_NONBLOCK)
        except OSError:
            return False
        self.knopki[imya] = (fd, GLib.unix_fd_add_full(
            GLib.PRIORITY_DEFAULT, fd, GLib.IOCondition.IN, self.chitat_knopku, imya))
        return False

    def chitat_knopku(self, fd, uslovie, imya):
        try:
            dannye = os.read(fd, 24 * 64)
        except OSError:
            return GLib.SOURCE_REMOVE
        for i in range(0, len(dannye) - 23, 24):
            tip, kod, znach = struct.unpack_from("HHi", dannye, i + 16)
            if tip == 1 and kod == 116 and znach == 1 and self.zvon:
                log(f"кнопка питания во время звонка ({imya}) — откладываю")
                GLib.idle_add(lambda: self.otlozhit() and False)
                break
        return GLib.SOURCE_CONTINUE

    def zabyt_knopki(self):
        for fd, istochnik in getattr(self, "knopki", {}).values():
            GLib.source_remove(istochnik)
            os.close(fd)
        self.knopki = {}
        if getattr(self, "monitor_vvoda", None):
            self.monitor_vvoda.cancel()
            self.monitor_vvoda = None

    def uvedomlenie(self, con, sender, path, iface, sig, params, data):
        if not self.zvon or params.unpack()[0] != self.zvon.get("uvedomlenie"):
            return
        if sig == "ActionInvoked" and params.unpack()[1] == "otlozhit":
            self.otlozhit()
        elif sig in ("ActionInvoked", "NotificationClosed"):
            self.ostanovit("остановлен хозяином")

    # --- главное ---------------------------------------------------------

    def peresmotret(self, prichina):
        sejchas = datetime.datetime.now()
        budilniki = [dict(b) for b in self.nastrojki.get_value("alarms").unpack()]

        # Настал ли ожидаемый звонок (или отложенный)?
        for klyuch in ("otlozhen", "zhdu"):
            zhdu = getattr(self, klyuch)
            if zhdu and zhdu[0] <= sejchas + datetime.timedelta(seconds=2):
                setattr(self, klyuch, None)
                opozdanie = (sejchas - zhdu[0]).total_seconds()
                if opozdanie <= OPOZDANIE_MAKS:
                    self.zvonit(zhdu[1], opozdanie)
                else:
                    log(f"звонок {zhdu[0]:%d.%m %H:%M} пропущен ({opozdanie:.0f} с назад) — не звоню")

        # Будильник выключили или удалили, пока он отложен, — отсрочку снимаем.
        if self.otlozhen:
            ostalis = {b.get("id") for b in budilniki if b.get("active", True)}
            if self.otlozhen[1].get("id") not in ostalis:
                log("отложенный будильник выключен в Clocks — отсрочку снимаю")
                self.otlozhen = None

        self.zhdu = blizhajshij(budilniki, sejchas)
        cel = min((z[0] for z in (self.zhdu, self.otlozhen) if z), default=None)
        self.zavesti(cel)
        log(f"{prichina}: будильников {len(budilniki)}, ближайший звонок "
            + (f"{cel:%d.%m %H:%M}" if cel else "не назначен"))

    def zavesti(self, cel):
        """Один таймер systemd на ближайший звонок, будящий систему."""
        if cel == self.zaveden_na:
            return
        subprocess.run(["systemctl", "stop", f"{TAJMER}.timer"], capture_output=True, timeout=20)
        subprocess.run(["systemctl", "reset-failed", f"{TAJMER}.timer", f"{TAJMER}.service"],
                       capture_output=True, timeout=20)
        self.zaveden_na = None
        if cel is None:
            return
        r = subprocess.run(
            ["systemd-run", "--quiet", f"--unit={TAJMER}",
             f"--on-calendar={cel:%Y-%m-%d %H:%M:%S}",
             "--timer-property=WakeSystem=yes", "--timer-property=AccuracySec=1s",
             "systemctl", "kill", "--kill-who=main", "--signal=SIGUSR1", "bf7-budilnik.service"],
            capture_output=True, text=True, timeout=20)
        if r.returncode == 0:
            self.zaveden_na = cel
        else:
            log("не завёлся таймер:", r.stderr.strip())

    # --- звонок ----------------------------------------------------------

    def vyzov(self, shina, imya, put, iface, metod, parametry, otvet=None):
        try:
            v = shina.call_sync(imya, put, iface, metod, parametry,
                                GLib.VariantType(otvet) if otvet else None,
                                Gio.DBusCallFlags.NONE, 5000, None)
            return v.unpack() if v else None
        except GLib.Error as e:
            log(f"{metod}: {e.message}")
            return None

    def zvonit(self, b, opozdanie):
        imya = b.get("name") or "Будильник"
        if self.zvon:
            self.ostanovit("новый звонок")
        log(f"звонок «{imya}» {b.get('hour'):02d}:{b.get('minute'):02d}"
            + (f", опоздание {opozdanie:.0f} с" if opozdanie > 5 else ""))
        z = {"budilnik": b}

        # Не гасить и не усыплять, пока звонит (флаги gnome-session: 4 — сон, 8 — простой).
        otv = self.vyzov(self.seans, "org.gnome.SessionManager", "/org/gnome/SessionManager",
                         "org.gnome.SessionManager", "Inhibit",
                         GLib.Variant("(susu)", ("bf7-budilnik", 0, "Звонит будильник", 4 | 8)), "(u)")
        z["zapret"] = otv[0] if otv else None
        # Зажечь экран «действием пользователя» — безвредной клавишей WAKEUP.
        # Тогда gsd-power сам выходит из простоя и зажигает экран. Выставлять
        # PowerSaveMode=0 нельзя: это мимо gsd, и нажатие кнопки питания он
        # потом принимал за выход из простоя и зажигал экран обратно.
        try:
            p = subprocess.Popen(["/usr/local/bin/bf7-knopka", "wakeup"],
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                 env=dict(os.environ, XDG_RUNTIME_DIR="/run/bf7-phosh",
                                          WAYLAND_DISPLAY="wayland-0"))
            # Забрать по завершении (иначе зомби, как было в bf7-dosyp) и
            # через 1,5 с проверить экран. Отсчёт — от конца bf7-knopka: сама
            # она ждёт 1,5 с до нажатия, чтобы композитор увидел устройство.
            #
            # WAKEUP будит только экран, погашенный по простою (gsd). Погашенный
            # кнопкой питания phosh будит лишь кнопка питания (проверено
            # 2026-09-30) — утром после «погасил на ночь» будильник звонил бы
            # в тёмный экран. Не загорелся — нажать её; только в этом случае,
            # так что горящий экран кнопка не гасит.
            def posle_wakeup(pid, st):
                p.poll()
                GLib.timeout_add(1500, self.dozhech_ekran)
            GLib.child_watch_add(GLib.PRIORITY_DEFAULT, p.pid, posle_wakeup)
        except OSError as e:
            log("не зажёг экран:", e)

        clocks = self.vyzov(self.seans, "org.freedesktop.DBus", "/org/freedesktop/DBus",
                            "org.freedesktop.DBus", "NameHasOwner",
                            GLib.Variant("(s)", ("org.gnome.clocks",)), "(b)")
        if clocks and clocks[0]:
            log("Clocks запущен — звонит он сам, я держу экран и запрет сна")
        else:
            otv = self.vyzov(self.seans, "org.sigxcpu.Feedback", "/org/sigxcpu/Feedback",
                             "org.sigxcpu.Feedback", "TriggerFeedback",
                             GLib.Variant("(ssa{sv}i)", ("org.gnome.clocks", "alarm-clock-elapsed",
                                                         {}, ZVONIT_MINUT * 60)), "(u)")
            z["otklik"] = otv[0] if otv else None
            otlozhit_min = b.get("snooze_minutes", 10)
            otv = self.vyzov(self.seans, "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
                             "org.freedesktop.Notifications", "Notify",
                             GLib.Variant("(susssasa{sv}i)", (
                                 "Будильник", 0, "alarm-symbolic", imya,
                                 f"{b.get('hour'):02d}:{b.get('minute'):02d}",
                                 ["stop", "Стоп", "otlozhit", f"Отложить на {otlozhit_min} мин"],
                                 {"urgency": GLib.Variant("y", 2),
                                  "category": GLib.Variant("s", "x-gnome.alarm"),
                                  "resident": GLib.Variant("b", True)},
                                 0)), "(u)")
            z["uvedomlenie"] = otv[0] if otv else None
            log(f"звоню сам: отклик {z.get('otklik')}, уведомление {z['uvedomlenie']}")
        z["konec"] = GLib.timeout_add_seconds(ZVONIT_MINUT * 60, self.otzvonil)
        self.zvon = z
        self.slushat_knopki()

    def otzvonil(self):
        self.zvon["konec"] = None   # источник снимется сам: возвращаем False
        self.ostanovit("отзвонил")
        return False

    def ostanovit(self, prichina):
        z, self.zvon = self.zvon, None
        if not z:
            return False
        self.zabyt_knopki()
        if z.get("konec"):
            GLib.source_remove(z["konec"])
        if z.get("otklik"):
            self.vyzov(self.seans, "org.sigxcpu.Feedback", "/org/sigxcpu/Feedback",
                       "org.sigxcpu.Feedback", "EndFeedback", GLib.Variant("(u)", (z["otklik"],)))
        if z.get("uvedomlenie"):
            self.vyzov(self.seans, "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
                       "org.freedesktop.Notifications", "CloseNotification",
                       GLib.Variant("(u)", (z["uvedomlenie"],)))
        if z.get("zapret"):
            self.vyzov(self.seans, "org.gnome.SessionManager", "/org/gnome/SessionManager",
                       "org.gnome.SessionManager", "Uninhibit", GLib.Variant("(u)", (z["zapret"],)))
        log("звонок снят:", prichina)
        return False

    def otlozhit(self):
        b = self.zvon["budilnik"]
        self.ostanovit("отложен")
        kogda = datetime.datetime.now() + datetime.timedelta(minutes=b.get("snooze_minutes", 10))
        self.otlozhen = (kogda.replace(microsecond=0), b)
        self.peresmotret(f"отложен до {kogda:%H:%M}")


Budilnik()
GLib.MainLoop().run()
