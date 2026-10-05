#!/usr/bin/python3
# BF7: уснуть снова после непрошеного пробуждения.
#
# Телефон на батарее засыпает по простою (gsd-power, dconf/00-bf7), а будят
# его и без человека: контроллер топлива (mtk_gauge_coulomb_high,
# mtk_gauge_nafg) раз в десятки минут, будильники RTC таймеров. gsd-power
# после такого пробуждения усыпляет снова лишь через полный тайм-аут простоя
# (десять минут) — и то только потому, что phosh сбрасывал таймеры простоя.
# С нашей заплаткой phosh экран при пробуждении не трогает, и gsd не усыпил
# бы телефон вовсе.
#
# Служба ждёт выхода из сна (logind PrepareForSleep(false)) и заводит у
# phosh наблюдатель простоя (org.gnome.Mutter.IdleMonitor.AddIdleWatch): он
# сработает, если полминуты после пробуждения никто не касался телефона
# (отсчёт идёт с создания наблюдателя; GetIdletime phosh 0.24 не умеет —
# «Not supported»). Тогда — за это время Wi-Fi переподключится, почта и
# уведомления успеют прийти — усыпляет снова, но только если ВСЁ верно:
#   1. телефон на батарее — на кабеле он не спит вовсе;
#   2. экран погашен — значит, его не будили ни кнопкой, ни будильником;
#   3. никто не держит запрет сна: ни у gnome-session (так делает звонящий
#      bf7-budilnik), ни блокирующий у logind;
#   4. не играет звук;
#   5. человек ничего не трогал с пробуждения — сработал наблюдатель простоя.
#
# Причину каждого пробуждения служба пишет в журнал. Если разбудила кнопка
# питания, а экран через 1,5 с тёмный, — зажигает его сама (страховка к
# заплаткам phosh и gsd-power, которые экран при пробуждении не трогают).
#
# Для проверки на кабеле: BF7_DOSYP_NA_KABELE=1 (только для испытаний).
import os
import subprocess
import sys
import time

ZHDAT_S = 30
# Модем MediaTek сам по себе каждые 3–30 с шлёт отладочные отчёты о соте
# (+EDMFAPP) — включены в нём по умолчанию, AT-командой не выключаются
# (проверено 2026-09-23), и каждый будит телефон прерыванием CCIF. Ждать
# после такого пробуждения 30 с простоя — значит почти не спать. Как на
# Android с автосном: даём ModemManager несколько секунд показать новый
# звонок или SMS, и если их нет — сразу обратно в сон. Коротко можно:
# звонящий модем повторяет RING каждые несколько секунд, а SMS лежит в его
# памяти, и ModemManager дочитает его на следующем пробуждении. Замер:
# 4 с — спал 51% времени.
ZHDAT_MODEM_MS = 2500


MM = "org.freedesktop.ModemManager1"
ZVONOK_VHODYASHCHIJ = 3   # MM_CALL_STATE_RINGING_IN
ZVONOK_ZAVERSHEN = 7      # MM_CALL_STATE_TERMINATED


def log(*a):
    print("bf7-dosyp:", *a, file=sys.stderr, flush=True)


def shina_seansa(predel=120):
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

import gi  # noqa: E402
gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib  # noqa: E402


def prochitat(put):
    try:
        with open(put) as f:
            return f.read().strip()
    except OSError:
        return ""


def ekran_gorit():
    """По разъёму DRM, а не по подсветке: при спящем тракте экрана файл
    lcd-backlight/brightness может хранить прежнюю яркость (класс backlight
    пропускает первое гашение после загрузки), хотя панель тёмная."""
    return prochitat("/sys/class/drm/card1-DSI-1/enabled") == "enabled"


def na_kabele():
    if os.environ.get("BF7_DOSYP_NA_KABELE") == "1":
        return False
    baza = "/sys/class/power_supply"
    for imya in os.listdir(baza):
        if prochitat(f"{baza}/{imya}/type") in ("USB", "Mains") and prochitat(f"{baza}/{imya}/online") == "1":
            return True
    return False



def knopka_wakeup():
    """Зажечь экран «действием пользователя» и забрать процесс по завершении.

    Прежде здесь был голый Popen: bf7-knopka завершалась и висела зомби, пока
    жив bf7-dosyp (замечено 2026-09-25). child_watch забирает её, не блокируя
    главный цикл.
    """
    p = subprocess.Popen(["/usr/local/bin/bf7-knopka", "wakeup"],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                         env=dict(os.environ, XDG_RUNTIME_DIR="/run/bf7-phosh",
                                  WAYLAND_DISPLAY="wayland-0"))
    GLib.child_watch_add(GLib.PRIORITY_DEFAULT, p.pid, lambda pid, status: p.poll())

class Dosyp:
    def __init__(self):
        self.seans = Gio.bus_get_sync(Gio.BusType.SESSION)
        self.sistema = Gio.bus_get_sync(Gio.BusType.SYSTEM)
        self.seans.connect("closed", lambda *a: GLib.idle_add(lambda: sys.exit(1)))
        self.sistema.signal_subscribe(
            "org.freedesktop.login1", "org.freedesktop.login1.Manager",
            "PrepareForSleep", "/org/freedesktop/login1", None,
            Gio.DBusSignalFlags.NONE, self.son, None)
        self.seans.signal_subscribe(
            "org.gnome.Mutter.IdleMonitor", "org.gnome.Mutter.IdleMonitor", "WatchFired",
            "/org/gnome/Mutter/IdleMonitor/Core", None,
            Gio.DBusSignalFlags.NONE, self.nabludatel_srabotal, None)
        # Звонки (ModemManager). Модем будит телефон прерыванием CCIF;
        # входящий звонок — «непрошеное» пробуждение, и без этой проверки
        # телефон уснул бы снова посреди звонка. На входящем тёмный экран
        # зажигаем сами — и когда телефон не спал, а лишь погасил экран.
        self.sistema.signal_subscribe(
            MM, "org.freedesktop.ModemManager1.Modem.Voice", "CallAdded", None, None,
            Gio.DBusSignalFlags.NONE, self.zvonok_dobavlen, None)
        self.sistema.signal_subscribe(
            MM, "org.freedesktop.ModemManager1.Call", "StateChanged", None, None,
            Gio.DBusSignalFlags.NONE, self.zvonok_sostoyanie, None)
        self.sistema.signal_subscribe(
            MM, "org.freedesktop.ModemManager1.Modem.Messaging", "Added", None, None,
            Gio.DBusSignalFlags.NONE, self.sms_prishlo, None)
        self.nabludatel = None    # номер наблюдателя простоя у phosh
        self.predel = None        # не ждать его бесконечно
        self.sobytie_modema = False   # звонок или SMS после пробуждения
        self.pokolenie = 0            # чтобы старый таймер не сработал в новом цикле
        log("жду выхода из сна")

    def sms_prishlo(self, con, sender, path, iface, sig, params, data):
        if params.unpack()[1]:
            log("пришло SMS")
            self.sobytie_modema = True
            # phosh 0.24 не зажигает экран на уведомление (настройки
            # wakeup-screen в нём ещё нет) — SMS ночью ждало бы на тёмном
            # экране блокировки. Зажигаем, как на входящий звонок.
            self.zazhech_ekran("новое SMS")

    def posle_modema(self, pokolenie):
        if pokolenie != self.pokolenie:
            return False
        if self.sobytie_modema or self.zvonki_idut():
            log("модем разбудил не зря — обычный путь")
            return False
        self.snyat_nabludatel()
        self.proverit()
        return False

    def sostoyanie_zvonka(self, put):
        otv = self.vyzov(self.sistema, MM, put, "org.freedesktop.DBus.Properties", "Get",
                         GLib.Variant("(ss)", ("org.freedesktop.ModemManager1.Call", "State")),
                         "(v)")
        return otv[0] if otv else ZVONOK_ZAVERSHEN

    def zvonki_idut(self):
        """Есть ли у ModemManager незавершённый звонок."""
        otv = self.vyzov(self.sistema, MM, "/org/freedesktop/ModemManager1",
                         "org.freedesktop.DBus.ObjectManager", "GetManagedObjects", None,
                         "(a{oa{sa{sv}}})")
        for modem, ifaces in (otv[0] if otv else {}).items():
            for put in ifaces.get("org.freedesktop.ModemManager1.Modem.Voice", {}).get("Calls", []):
                if self.sostoyanie_zvonka(put) != ZVONOK_ZAVERSHEN:
                    return True
        return False

    def zvonok_dobavlen(self, con, sender, path, iface, sig, params, data):
        self.sobytie_modema = True
        put = params.unpack()[0]
        if self.sostoyanie_zvonka(put) == ZVONOK_VHODYASHCHIJ:
            self.vhodyashchij()

    def zvonok_sostoyanie(self, con, sender, path, iface, sig, params, data):
        if params.unpack()[1] == ZVONOK_VHODYASHCHIJ:
            self.vhodyashchij()

    def vhodyashchij(self):
        log("входящий звонок")
        self.zazhech_ekran("входящий звонок")

    def zazhech_ekran(self, zachem):
        if not ekran_gorit():
            log(f"{zachem}, экран тёмный — зажигаю")
            knopka_wakeup()

    def vyzov(self, shina, imya, put, iface, metod, parametry, otvet):
        try:
            return shina.call_sync(imya, put, iface, metod, parametry, GLib.VariantType(otvet),
                                   Gio.DBusCallFlags.NONE, 5000, None).unpack()
        except GLib.Error as e:
            log(f"{metod}: {e.message}")
            return None

    def prichina_probuzhdeniya(self):
        """Последняя строка ядра «Resume caused by IRQ N, имя» (печать MediaTek)."""
        r = subprocess.run(["dmesg"], capture_output=True, text=True, timeout=10)
        stroki = [s for s in r.stdout.splitlines() if "Resume caused by IRQ" in s]
        return stroki[-1].split("Resume caused by ", 1)[1].strip() if stroki else "неизвестно"

    def razbudit_ekran_esli_temno(self):
        # Страховка к заплаткам phosh и gsd-power: экран после пробуждения
        # кнопкой зажигает phoc — событие нажатия доходит до него. Если же
        # экран всё ещё тёмный, зажигаем «действием пользователя».
        if not ekran_gorit():
            log("разбудили кнопкой, а экран тёмный — зажигаю")
            knopka_wakeup()
        return False

    def son(self, con, sender, path, iface, sig, params, data):
        self.snyat_nabludatel()
        if params.unpack()[0]:
            return
        prichina = self.prichina_probuzhdeniya()
        log(f"проснулись: {prichina}")
        self.pokolenie += 1
        self.sobytie_modema = False
        if "pmic-keys" in prichina:
            GLib.timeout_add(1500, self.razbudit_ekran_esli_temno)
        if "CCIF_AP_DATA" in prichina:
            GLib.timeout_add(ZHDAT_MODEM_MS, self.posle_modema, self.pokolenie)
        otv = self.vyzov(self.seans, "org.gnome.Mutter.IdleMonitor", "/org/gnome/Mutter/IdleMonitor/Core",
                         "org.gnome.Mutter.IdleMonitor", "AddIdleWatch",
                         GLib.Variant("(t)", (ZHDAT_S * 1000,)), "(u)")
        if otv is None:
            return
        self.nabludatel = otv[0]
        # Человек взял телефон — наблюдатель сработает лишь после его простоя,
        # когда об этом пробуждении уже нечего думать. Снимаем через 5 минут.
        self.predel = GLib.timeout_add_seconds(300, self.istek)

    def istek(self):
        self.predel = None
        self.snyat_nabludatel()
        return False

    def snyat_nabludatel(self):
        if self.predel:
            GLib.source_remove(self.predel)
            self.predel = None
        if self.nabludatel is not None:
            self.vyzov(self.seans, "org.gnome.Mutter.IdleMonitor", "/org/gnome/Mutter/IdleMonitor/Core",
                       "org.gnome.Mutter.IdleMonitor", "RemoveWatch",
                       GLib.Variant("(u)", (self.nabludatel,)), "()")
            self.nabludatel = None

    def nabludatel_srabotal(self, con, sender, path, iface, sig, params, data):
        if self.nabludatel is None or params.unpack()[0] != self.nabludatel:
            return
        predel = self.predel
        self.predel = None           # не снимать таймер предела вместе с наблюдателем
        self.snyat_nabludatel()
        self.predel = predel
        if self.proverit() == "экран горит" and self.predel:
            # Экран зажёг звонок, SMS или человек. Погаснет — через 30 с
            # простоя спать, а не ждать 10 минут до gsd-power; предел
            # в 5 минут от пробуждения по-прежнему действует.
            self.postavit_nabludatel()
            return
        if self.predel:
            GLib.source_remove(self.predel)
            self.predel = None

    def postavit_nabludatel(self):
        otv = self.vyzov(self.seans, "org.gnome.Mutter.IdleMonitor", "/org/gnome/Mutter/IdleMonitor/Core",
                         "org.gnome.Mutter.IdleMonitor", "AddIdleWatch",
                         GLib.Variant("(t)", (ZHDAT_S * 1000,)), "(u)")
        self.nabludatel = otv[0] if otv else None

    def pomeha(self):
        """Причина НЕ засыпать или None."""
        if na_kabele():
            return "на кабеле"
        if ekran_gorit():
            return "экран горит"
        if self.zvonki_idut():
            return "идёт звонок"
        otv = self.vyzov(self.seans, "org.gnome.SessionManager", "/org/gnome/SessionManager",
                         "org.gnome.SessionManager", "IsInhibited", GLib.Variant("(u)", (4,)), "(b)")
        if otv is None or otv[0]:
            return "запрет сна у gnome-session"
        otv = self.vyzov(self.sistema, "org.freedesktop.login1", "/org/freedesktop/login1",
                         "org.freedesktop.login1.Manager", "ListInhibitors", None, "(a(ssssuu))")
        for chto, kto, zachem, rezhim, uid, pid in (otv[0] if otv else []):
            if rezhim == "block" and "sleep" in chto.split(":"):
                return f"запрет сна у logind: {kto} ({zachem})"
        r = subprocess.run(["pactl", "list", "short", "sinks"], capture_output=True, text=True,
                           timeout=10, env=dict(os.environ, PULSE_SERVER="unix:/run/bf7-pulse/native"))
        if "RUNNING" in r.stdout:
            return "играет звук"
        return None

    def proverit(self):
        prichina = self.pomeha()
        if prichina:
            log(f"не засыпаю: {prichina}")
            return prichina
        log("пробуждение было непрошеным — засыпаю снова")
        subprocess.run(["systemctl", "suspend"], timeout=30)
        return None


Dosyp()
GLib.MainLoop().run()
