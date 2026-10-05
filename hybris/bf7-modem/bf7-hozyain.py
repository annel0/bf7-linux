#!/usr/bin/env python3
"""BF7: взять трубку, когда звонит хозяин — чтобы Клауд мог с ним говорить.

Слушает ModemManager (сигнал CallAdded). Входящий с номера хозяина
(последние 10 цифр из /etc/bf7/hozyain-nomer) через 2 с принимается
(Call.Accept), и в /run/bf7-dialog.log пишется «ЗВОНОК: хозяин, принят» —
по этой строке ПК-сторож (golos-pc/dialog-storozh) здоровается и включает
слух. Чужие вызовы не трогаются: звонят как обычно.

Номер — в отдельном файле, не в коде: это личные данные.

Трубка берётся, только если Клауд на связи: ПК-сторож раз в 30 с трогает
/run/bf7-klaud-na-svyazi. Отметка старше 2 минут (ПК выключен, сторож не
запущен) — вызов звонит как обычный: иначе хозяин услышал бы тишину.
"""
import os
import sys
import time

import gi
gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib

MM = "org.freedesktop.ModemManager1"
NOMER_FAJL = "/etc/bf7/hozyain-nomer"
DIALOG = "/run/bf7-dialog.log"
ZADERZHKA_MS = 2000
NA_SVYAZI = "/run/bf7-klaud-na-svyazi"
SVEZHEST_S = 120


def klaud_na_svyazi():
    try:
        return time.time() - os.stat(NA_SVYAZI).st_mtime < SVEZHEST_S
    except OSError:
        return False


def log(s):
    print("bf7-hozyain: " + s, file=sys.stderr, flush=True)


def v_dialog(s):
    try:
        with open(DIALOG, "a", encoding="utf-8") as f:
            f.write("%s ЗВОНОК: %s\n" % (time.strftime("%H:%M:%S"), s))
    except OSError:
        pass


def cifry(s):
    return "".join(c for c in (s or "") if c.isdigit())[-10:]


class Hozyain:
    def __init__(self, shina, nomer):
        self.shina = shina
        self.nomer = nomer
        shina.signal_subscribe(MM, "org.freedesktop.ModemManager1.Modem.Voice",
                               "CallAdded", None, None, Gio.DBusSignalFlags.NONE,
                               self.novyj_vyzov)

    def svojstvo(self, put, imya):
        otv = self.shina.call_sync(MM, put, "org.freedesktop.DBus.Properties", "Get",
                                   GLib.Variant("(ss)", ("org.freedesktop.ModemManager1.Call", imya)),
                                   GLib.VariantType("(v)"), Gio.DBusCallFlags.NONE, 5000, None)
        return otv.unpack()[0]

    def novyj_vyzov(self, _con, _snd, _put, _iface, _sig, param):
        put = param.unpack()[0]
        try:
            nomer = self.svojstvo(put, "Number")
            napravlenie = self.svojstvo(put, "Direction")   # 1 — входящий
        except GLib.Error as e:
            log("вызов %s: %s" % (put, e.message))
            return
        if napravlenie != 1:
            return
        if cifry(nomer) != self.nomer:
            log("входящий не от хозяина — не трогаю")
            return
        if not klaud_na_svyazi():
            log("звонит хозяин, но Клауд не на связи — не беру")
            v_dialog("хозяин, Клауд не на связи — не взят")
            return
        log("звонит хозяин — беру через %d мс" % ZADERZHKA_MS)
        GLib.timeout_add(ZADERZHKA_MS, self.vzyat, put)

    def vzyat(self, put):
        try:
            if self.svojstvo(put, "State") != 3:        # 3 — ringing-in
                log("вызов уже не звонит — не беру")
                return False
            self.shina.call_sync(MM, put, "org.freedesktop.ModemManager1.Call", "Accept",
                                 None, None, Gio.DBusCallFlags.NONE, 30000, None)
            log("трубка взята")
            v_dialog("хозяин, принят")
        except GLib.Error as e:
            log("не взял: %s" % e.message)
            v_dialog("хозяин, взять не удалось: %s" % e.message)
        return False


def main():
    try:
        nomer = cifry(open(NOMER_FAJL).read())
    except OSError:
        log("нет %s — автоответ выключен" % NOMER_FAJL)
        return 0
    if len(nomer) != 10:
        log("в %s не номер" % NOMER_FAJL)
        return 1
    shina = Gio.bus_get_sync(Gio.BusType.SYSTEM, None)
    Hozyain(shina, nomer)
    log("жду звонка хозяина")
    GLib.MainLoop().run()


if __name__ == "__main__":
    sys.exit(main())
