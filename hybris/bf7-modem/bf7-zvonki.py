#!/usr/bin/env python3
"""BF7: звук разговора — замена callaudiod для «Звонков» (gnome-calls).

«Звонки» просят звук через службу сеанса org.mobian_project.CallAudio:
SelectMode(1) в начале разговора, SelectMode(0) в конце, EnableSpeaker —
громкая связь, MuteMic — микрофон. Пакетный callaudiod выполняет это
переключением профилей PulseAudio, а у нас разговор идёт мимо PulseAudio:
модем говорит с кодеком сам, через PCM Voice_MD1, и включает этот путь
заводской звуковой HAL MediaTek (bf7-rech). Поэтому на том же имени и с тем
же интерфейсом отвечает эта программа (активация D-Bus из
/usr/local/share/dbus-1/services — он в XDG_DATA_DIRS раньше /usr/share).

Что делает:
  * SelectMode(1) — запускает bf7-rech zvonok (трубка, гарнитура или
    громкая), держит запрет сна: иначе bf7-dosyp или gsd-power усыпили бы
    телефон посреди разговора;
  * EnableSpeaker / MuteMic — команды работающему bf7-rech;
  * SelectMode(0) — «stop», и после выхода HAL возвращает обычную настройку
    звука (bf7-audio-up): HAL при разговоре выключает усилитель ЦАП, и без
    этого PulseAudio остался бы без звука.
Все действия — по очереди в одном рабочем потоке: вызов D-Bus не ждёт HAL.

Со стороны bf7-rech получает команды через /run/bf7-rech.cmd — так
bf7-skazat говорит в разговор синтезированной речью.
"""
import os
import queue
import subprocess
import sys
import threading

import gi
gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib

IMYA = "org.mobian_project.CallAudio"
PUT = "/org/mobian_project/CallAudio"
XML = """<node><interface name="org.mobian_project.CallAudio">
 <method name="SelectMode"><arg direction="in" type="u" name="mode"/>
  <arg direction="out" type="b" name="success"/></method>
 <method name="EnableSpeaker"><arg direction="in" type="b" name="enable"/>
  <arg direction="out" type="b" name="success"/></method>
 <method name="MuteMic"><arg direction="in" type="b" name="mute"/>
  <arg direction="out" type="b" name="success"/></method>
 <property name="AudioMode" type="u" access="read"/>
 <property name="SpeakerState" type="u" access="read"/>
 <property name="MicState" type="u" access="read"/>
</interface></node>"""

L = "/vendor.local/lib64"
RECH_OKRUZHENIE = dict(
    os.environ,
    LD_LIBRARY_PATH="/apex/com.android.vndk.v31/lib64:/vendor/lib64:"
                    "/vendor/lib64/hw:/system/lib64",
    LD_PRELOAD=f"{L}/libbf7prop.so:{L}/libbf7log.so:{L}/libselinux-stub.so:"
               f"{L}/libbf7wakelock.so")
GARNITURA = "/sys/class/switch/h2w/state"
KANAL = "/run/bf7-rech.cmd"


def log(s):
    print("bf7-zvonki: " + s, file=sys.stderr, flush=True)


def garnitura_vstavlena():
    try:
        return open(GARNITURA).read().strip() not in ("", "0")
    except OSError:
        return False


class Zvonki:
    def __init__(self, shina):
        self.shina = shina
        self.rezhim = 0      # 0 — обычный, 1 — разговор
        self.dinamik = 0     # 1 — громкая связь
        self.mikrofon = 0    # 1 — включён
        self.rech = None
        self.zaprety = []
        self.ochered = queue.Queue()
        threading.Thread(target=self.rabochij, daemon=True).start()
        threading.Thread(target=self.kanal_komand, daemon=True).start()

    # --- D-Bus -------------------------------------------------------------
    def vyzov(self, _con, _snd, _put, _iface, metod, param, inv):
        arg = param.unpack()[0]
        if metod == "SelectMode":
            if arg not in (0, 1):
                inv.return_value(GLib.Variant("(b)", (False,)))
                return
            self.rezhim = arg
            if arg == 1:
                self.mikrofon = 1
            else:
                self.dinamik = 0
                self.mikrofon = 0
        elif metod == "EnableSpeaker":
            self.dinamik = 1 if arg else 0
        elif metod == "MuteMic":
            self.mikrofon = 0 if arg else 1
        self.ochered.put((metod, arg))
        self.izmenilis()
        inv.return_value(GLib.Variant("(b)", (True,)))

    def svojstvo(self, _con, _snd, _put, _iface, imya):
        return GLib.Variant("u", {"AudioMode": self.rezhim,
                                  "SpeakerState": self.dinamik,
                                  "MicState": self.mikrofon}[imya])

    def izmenilis(self):
        self.shina.emit_signal(
            None, PUT, "org.freedesktop.DBus.Properties", "PropertiesChanged",
            GLib.Variant("(sa{sv}as)", (IMYA, {
                "AudioMode": GLib.Variant("u", self.rezhim),
                "SpeakerState": GLib.Variant("u", self.dinamik),
                "MicState": GLib.Variant("u", self.mikrofon)}, [])))

    # --- работа ------------------------------------------------------------
    def rabochij(self):
        while True:
            metod, arg = self.ochered.get()
            try:
                if metod == "SelectMode":
                    self.nachat() if arg == 1 else self.zakonchit()
                elif metod == "EnableSpeaker":
                    self.komanda("gromko" if arg else self.ustrojstvo())
                elif metod == "MuteMic":
                    self.komanda("mikrofon %d" % (0 if arg else 1))
                elif metod == "komanda":
                    self.komanda(arg)
            except Exception as e:  # чтобы поток не умер от одной беды
                log("ошибка %s(%s): %s" % (metod, arg, e))

    def kanal_komand(self):
        """Команды bf7-rech со стороны: строка в /run/bf7-rech.cmd уходит
        работающему bf7-rech (например, «igraj <файл>» от bf7-skazat)."""
        try:
            if not os.path.exists(KANAL):
                os.mkfifo(KANAL, 0o600)
        except OSError as e:
            log("канал команд %s: %s" % (KANAL, e))
            return
        while True:
            with open(KANAL) as f:
                for s in f:
                    s = s.strip()
                    if s:
                        self.ochered.put(("komanda", s))

    def ustrojstvo(self):
        return "garnitura" if garnitura_vstavlena() else "trubka"

    def nachat(self):
        if self.rech and self.rech.poll() is None:
            return
        kuda = "gromko" if self.dinamik else self.ustrojstvo()
        log("разговор начался, звук: %s" % kuda)
        self.zapretit_son()
        self.rech = subprocess.Popen(
            ["/usr/local/bin/bf7-rech", "zvonok", kuda],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, env=RECH_OKRUZHENIE, text=True, bufsize=1)
        threading.Thread(target=self.chitat, args=(self.rech,), daemon=True).start()

    def chitat(self, p):
        # Всё, что пишет HAL, — в /run/bf7-rech.log (для разбора звонка),
        # свои строки bf7-rech — ещё и в журнал.
        try:
            zhurnal = open("/run/bf7-rech.log", "w", buffering=1)
        except OSError:
            zhurnal = None
        for s in p.stdout:
            if zhurnal:
                zhurnal.write(s)
            if s.startswith("bf7-rech:"):
                log(s.strip())

    def komanda(self, s):
        if self.rech and self.rech.poll() is None:
            try:
                self.rech.stdin.write(s + "\n")
                self.rech.stdin.flush()
            except OSError as e:
                log("bf7-rech не принял «%s»: %s" % (s, e))

    def zakonchit(self):
        if self.rech and self.rech.poll() is None:
            self.komanda("stop")
            try:
                self.rech.stdin.close()
            except OSError:
                pass
            try:
                self.rech.wait(10)
            except subprocess.TimeoutExpired:
                log("bf7-rech не вышел за 10 с — гашу")
                self.rech.kill()
                self.rech.wait(5)
            self.rech = None
            # HAL выключает усилитель ЦАП — вернуть обычный звук.
            subprocess.run(["/usr/local/bin/bf7-audio-up"], timeout=60,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            log("разговор окончен, обычный звук возвращён")
        self.razreshit_son()

    # --- сон ---------------------------------------------------------------
    def zapretit_son(self):
        if self.zaprety:
            return
        try:
            sis = Gio.bus_get_sync(Gio.BusType.SYSTEM, None)
            otv, spisok = sis.call_with_unix_fd_list_sync(
                "org.freedesktop.login1", "/org/freedesktop/login1",
                "org.freedesktop.login1.Manager", "Inhibit",
                GLib.Variant("(ssss)", ("sleep:idle", "bf7-zvonki", "Разговор", "block")),
                GLib.VariantType("(h)"), Gio.DBusCallFlags.NONE, -1, None, None)
            self.zaprety.append(("fd", spisok.steal_fds()[0]))
        except GLib.Error as e:
            log("запрет сна logind: %s" % e.message)
        try:
            otv = self.shina.call_sync(
                "org.gnome.SessionManager", "/org/gnome/SessionManager",
                "org.gnome.SessionManager", "Inhibit",
                GLib.Variant("(susu)", ("bf7-zvonki", 0, "Разговор", 4 | 8)),
                GLib.VariantType("(u)"), Gio.DBusCallFlags.NONE, 5000, None)
            self.zaprety.append(("sm", otv.unpack()[0]))
        except GLib.Error as e:
            log("запрет сна сеанса: %s" % e.message)

    def razreshit_son(self):
        for vid, z in self.zaprety:
            try:
                if vid == "fd":
                    os.close(z)
                else:
                    self.shina.call_sync(
                        "org.gnome.SessionManager", "/org/gnome/SessionManager",
                        "org.gnome.SessionManager", "Uninhibit",
                        GLib.Variant("(u)", (z,)), None, Gio.DBusCallFlags.NONE,
                        5000, None)
            except (OSError, GLib.Error):
                pass
        self.zaprety = []


def main():
    shina = Gio.bus_get_sync(Gio.BusType.SESSION, None)
    z = Zvonki(shina)
    shina.register_object(PUT, Gio.DBusNodeInfo.new_for_xml(XML).interfaces[0],
                          z.vyzov, z.svojstvo, None)
    petlya = GLib.MainLoop()

    def poluchil(*_):
        log("служба звука разговора готова")

    def poteryal(*_):
        log("имя %s занято или потеряно — выхожу" % IMYA)
        petlya.quit()

    Gio.bus_own_name_on_connection(shina, IMYA, Gio.BusNameOwnerFlags.NONE,
                                   poluchil, poteryal)
    petlya.run()
    z.zakonchit()


if __name__ == "__main__":
    main()
