#!/usr/bin/python3
"""BF7: net.hadess.SensorProxy — ориентация для автоповорота phosh.

iio-sensor-proxy ищет датчики в IIO ядра, а у нас их там нет: акселерометр
sc7a20 читает с шины сама служба bf7-seismograf и кладёт ориентацию в
/run/bf7-akselerometr/orientaciya. Здесь — тот же интерфейс D-Bus на
системной шине, что у iio-sensor-proxy (phosh и gsd берут ориентацию
оттуда): HasAccelerometer, AccelerometerOrientation, Claim/Release.

Свет и приближение — mn78911 на i2c-1:0x41 (родня Elan EPL259x, но карта
регистров своя). Последовательность запуска и раскладка данных вынуты из
прошивки SCP (docs/bf7-kernel/research-log.md, 2026-09-25): 0x02 — пуск (02)
и стоп (01), 0x00 — режим (0x83 = свет и приближение), свет — 8 байт с 0x34
(ch1 в 0x38..0x39, люксы = ch1 * 0,3; ch0 — ИК), приближение — флаги в 0x44 (бит 3 —
«ниже нижнего порога», то есть далеко; после чтения 44=02) и данные в
0x47..0x48. Пороги 5000/5500 зашиты в чип (0x40..0x43). Чип работает, только
пока его кто-то затребовал (phosh — приближение во время звонка, gsd —
свет для автояркости).
"""
import ctypes
import fcntl
import os
import sys

from gi.repository import Gio, GLib

IMYA = "net.hadess.SensorProxy"
PUT = "/net/hadess/SensorProxy"
FAJL = "/run/bf7-akselerometr/orientaciya"

XML = """
<node>
  <interface name="net.hadess.SensorProxy">
    <method name="ClaimAccelerometer"/>
    <method name="ReleaseAccelerometer"/>
    <method name="ClaimLight"/>
    <method name="ReleaseLight"/>
    <method name="ClaimProximity"/>
    <method name="ReleaseProximity"/>
    <property name="HasAccelerometer" type="b" access="read"/>
    <property name="AccelerometerOrientation" type="s" access="read"/>
    <property name="HasAmbientLight" type="b" access="read"/>
    <property name="LightLevelUnit" type="s" access="read"/>
    <property name="LightLevel" type="d" access="read"/>
    <property name="HasProximity" type="b" access="read"/>
    <property name="ProximityNear" type="b" access="read"/>
  </interface>
</node>
"""

sostoyanie = {"orient": "undefined", "lux": 0.0, "blizko": False}
svyaz = None

# ---- mn78911 через /dev/i2c-1 --------------------------------------------
I2C_RDWR, I2C_M_RD, ADR = 0x0707, 0x0001, 0x41


class Msg(ctypes.Structure):
    _fields_ = [("addr", ctypes.c_uint16), ("flags", ctypes.c_uint16),
                ("len", ctypes.c_uint16), ("buf", ctypes.POINTER(ctypes.c_uint8))]


class RdWr(ctypes.Structure):
    _fields_ = [("msgs", ctypes.POINTER(Msg)), ("nmsgs", ctypes.c_uint32)]


class Mn78911:
    def __init__(self):
        self.fd = os.open("/dev/i2c-1", os.O_RDWR)

    def _perenos(self, spisok):
        msgs = (Msg * len(spisok))()
        bufs = []
        for i, (flags, data) in enumerate(spisok):
            b = (ctypes.c_uint8 * len(data))(*data)
            bufs.append(b)
            msgs[i] = Msg(ADR, flags, len(data), b)
        fcntl.ioctl(self.fd, I2C_RDWR, RdWr(msgs, len(spisok)))
        return bufs

    def pisat(self, reg, *vals):
        self._perenos([(0, [reg, *vals])])

    def chitat(self, reg, n):
        return list(self._perenos([(0, [reg]), (I2C_M_RD, [0] * n)])[1])

    def pusk(self, svet, blizost):
        # как у прошивки SCP: стоп, режим (бит 0 — свет, бит 1 — приближение,
        # 0x80 — как у неё), пороги, пуск. Приближение без нужды не включаем:
        # его ИК-светодиод вспыхивает на каждом замере.
        self.pisat(0x02, 0x01)
        self.pisat(0x00, 0x80 | (1 if svet else 0) | (2 if blizost else 0))
        self.pisat(0x40, 0x88, 0x13, 0x7C, 0x15)
        self.pisat(0x30, 0x11, 0x27, 0x10, 0x27)
        self.pisat(0x52, 0x09, 0xF4, 0x0B, 0xB3, 0x0F, 0x61)
        self.pisat(0x34, 0x00)
        self.pisat(0x34, 0x02)
        self.pisat(0x44, 0x00)
        self.pisat(0x44, 0x02)
        self.pisat(0x02, 0x02)

    def stop(self):
        self.pisat(0x02, 0x01)

    def lux(self):
        # Формула прошивки SCP: ch1 (0x38..0x39) × 0,3. ch1 — видимый свет, ch0
        # (0x36..0x37) — инфракрасный: 2026-09-25 на закате ch0 упал с 730 до
        # 8, но при включённой светодиодной лампе ch0 ≈ 2, а ch1 ≈ 240 (≈ 72 лк).
        # Вечером я было перешёл на ch0 — лампу он не видит, вернул ch1.
        d = self.chitat(0x34, 8)
        return (d[4] | d[5] << 8) * 0.3

    def blizko(self):
        f = self.chitat(0x44, 1)[0]
        self.pisat(0x44, 0x02)
        return not (f & 0x08)


try:
    chip = Mn78911()
except OSError:
    chip = None
zayavki = {"light": set(), "prox": set()}
okno = []   # последние замеры света для среднего
tajmery = {"light": 0, "prox": 0}
rabotaet = [(False, False)]


def prochitat():
    try:
        with open(FAJL) as f:
            o = f.read().strip()
    except OSError:
        o = "undefined"
    return o if o in ("normal", "left-up", "right-up", "bottom-up") else "undefined"


def svojstvo(con, sender, path, iface, name):
    znach = {
        "HasAccelerometer": GLib.Variant("b", True),
        "AccelerometerOrientation": GLib.Variant("s", sostoyanie["orient"]),
        "HasAmbientLight": GLib.Variant("b", chip is not None),
        "LightLevelUnit": GLib.Variant("s", "vendor"),   # √лк + 2, см. opros()
        "LightLevel": GLib.Variant("d", sostoyanie["lux"]),
        "HasProximity": GLib.Variant("b", chip is not None),
        "ProximityNear": GLib.Variant("b", sostoyanie["blizko"]),
    }
    return znach.get(name)


def soobshchit(imya, znach):
    if svyaz:
        svyaz.emit_signal(None, PUT, "org.freedesktop.DBus.Properties", "PropertiesChanged",
                          GLib.Variant("(sa{sv}as)", (IMYA, {imya: znach}, [])))


def opros(chto):
    if not zayavki[chto] or not chip:
        tajmery[chto] = 0
        return False
    try:
        if chto == "light":
            # gsd-power ставит яркость пропорционально LightLevel, отсчитывая
            # от яркости в момент первого замера. На сырых люксах лампа 144 лк
            # → телефон в руке 3 лк увели подсветку с 141 до 2 из 255
            # (2026-09-25). Отдаём сжатую шкалу √лк + 2 («vendor», не люксы):
            # из-под лампы в темноту яркость падает примерно вчетверо, а не в
            # ноль. Порог 10 % не даёт gsd дёргать подсветку при ровном свете.
            #
            # Среднее, а не медиана: 2026-09-30 днём ch1 скакал между двумя
            # уровнями, ~152 и ~251, примерно поровну (ch0 ровный) — мерцание
            # источника (ШИМ ламп или мониторов) попадает в короткий замер.
            # Медиана семи перескакивала из группы в группу, LightLevel качался
            # 8,8 ↔ 10,3, и gsd раз в 2–4 с звал pkexec gsd-backlight-helper.
            # Среднее двенадцати замеров за 3 с ровное.
            okno.append(chip.lux())
            del okno[:-12]
            lux = sum(okno) / len(okno)
            l = round(lux ** 0.5 + 2.0, 2)
            if abs(l - sostoyanie["lux"]) >= max(0.2, 0.10 * sostoyanie["lux"]):
                sostoyanie["lux"] = l
                soobshchit("LightLevel", GLib.Variant("d", l))
        else:
            b = chip.blizko()
            if b != sostoyanie["blizko"]:
                sostoyanie["blizko"] = b
                soobshchit("ProximityNear", GLib.Variant("b", b))
    except OSError as e:
        print("mn78911:", e, file=sys.stderr)
    return True


def peresmotret():
    rezhim = (bool(zayavki["light"]), bool(zayavki["prox"]))
    if chip and rezhim != rabotaet[0]:
        try:
            chip.pusk(*rezhim) if any(rezhim) else chip.stop()
            if rezhim[0] and not rabotaet[0][0]:
                okno.clear()    # замеры до гашения экрана устарели
            rabotaet[0] = rezhim
        except OSError as e:
            print("mn78911:", e, file=sys.stderr)
    for chto, ms in (("light", 250), ("prox", 200)):
        if zayavki[chto] and not tajmery[chto]:
            tajmery[chto] = GLib.timeout_add(ms, opros, chto)
            GLib.timeout_add(300, lambda c=chto: opros(c) and False)   # первый замер сразу


nablyudateli = {}


def ushel(con, imya):
    for z in zayavki.values():
        z.discard(imya)
    w = nablyudateli.pop(imya, None)
    if w:
        Gio.bus_unwatch_name(w)
    peresmotret()


def vyzov(con, sender, path, iface, method, params, inv):
    chto = {"Light": "light", "Proximity": "prox"}.get(method.replace("Claim", "").replace("Release", ""))
    if chto:
        if method.startswith("Claim"):
            zayavki[chto].add(sender)
            if sender not in nablyudateli:
                nablyudateli[sender] = Gio.bus_watch_name_on_connection(
                    con, sender, Gio.BusNameWatcherFlags.NONE, None, ushel)
        else:
            zayavki[chto].discard(sender)
        print(method, sender, file=sys.stderr, flush=True)
        peresmotret()
    # акселерометр и так всё время работает (сейсмограф) — захват ничего не стоит
    inv.return_value(None)


def izmenilos(*_):
    o = prochitat()
    if o == sostoyanie["orient"]:
        return
    sostoyanie["orient"] = o
    soobshchit("AccelerometerOrientation", GLib.Variant("s", o))


def poluchena_shina(con, name):
    global svyaz
    svyaz = con
    info = Gio.DBusNodeInfo.new_for_xml(XML).interfaces[0]
    con.register_object(PUT, info, vyzov, svojstvo, None)


def main():
    sostoyanie["orient"] = prochitat()
    if chip:
        try:
            chip.stop()
        except OSError:
            pass
    Gio.bus_own_name(Gio.BusType.SYSTEM, IMYA, Gio.BusNameOwnerFlags.NONE,
                     poluchena_shina, None, lambda *a: sys.exit("шина не отдала имя " + IMYA))
    # следим за каталогом: файл заменяется переименованием
    kat = Gio.File.new_for_path("/run/bf7-akselerometr")
    try:
        kat.make_directory_with_parents(None)
    except GLib.Error:
        pass
    mon = kat.monitor_directory(Gio.FileMonitorFlags.WATCH_MOVES, None)
    mon.connect("changed", izmenilos)
    GLib.MainLoop().run()


if __name__ == "__main__":
    main()
