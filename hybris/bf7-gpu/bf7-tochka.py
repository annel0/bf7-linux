#!/usr/bin/python3
"""BF7: точка доступа (раздача интернета) — запись «Точка доступа» в сетке.

В шторке Phosh 0.24 и в «Настройках» (phosh-mobile-settings) переключателя
раздачи нет, gnome-control-center не стоит. Драйвер MediaTek держит точку
на отдельном интерфейсе ap0 одновременно с подключённым wlan0 (проверено
2026-09-30: тип AP, 10.42.0.1/24, dnsmasq от NetworkManager). Цепочки
nm-sh-* NetworkManager вставляет поверх bf7-firewall сам — DHCP, DNS,
пересылка и маскарадинг.

ap0 по умолчанию не управляется NM (bf7-nm-unmanaged.conf: иначе он раз в
2 минуты пытался им сканировать), поэтому на время раздачи берём его под
управление и потом отдаём обратно.

Клиентам точки закрыты частные сети (10/8, 172.16/12, 192.168/16) — иначе
при подключённом Wi-Fi они ходили бы через wlan0 в домашнюю сеть хозяина,
к его ПК. Интернет открыт: в него трафик идёт и через Wi-Fi, и через
мобильную сеть. Правила — в FORWARD с пометкой bf7-tochka, ставятся при
включении и снимаются при выключении.

Сеть «BF7», пароль случайный, заводится при первом включении и хранится в
NetworkManager (читается только root). Нажатие включает или выключает;
уведомление показывает имя и пароль — временное, в списке не остаётся.
"""
import secrets
import subprocess
import sys

from gi.repository import Gio, GLib

SOEDINENIE = "BF7 точка доступа"
SET = "BF7"
AZBUKA = "abcdefghjkmnpqrstuvwxyz23456789"   # без l/1/o/0 — не перепутать


def nmcli(*args, proverka=True):
    return subprocess.run(["nmcli", *args], capture_output=True, text=True, check=proverka)


def uvedomit(zagolovok, tekst):
    try:
        Gio.bus_get_sync(Gio.BusType.SESSION).call_sync(
            "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
            "org.freedesktop.Notifications", "Notify",
            GLib.Variant("(susssasa{sv}i)", ("Точка доступа", 0, "network-wireless-hotspot-symbolic",
                                             zagolovok, tekst, [],
                                             {"transient": GLib.Variant("b", True)}, 10000)),
            None, Gio.DBusCallFlags.NONE, 3000, None)
    except GLib.Error:
        pass


def aktivna():
    r = nmcli("-t", "-f", "NAME", "con", "show", "--active", proverka=False)
    return SOEDINENIE in r.stdout.splitlines()


def zavesti():
    if nmcli("-t", "-f", "NAME", "con", "show", proverka=False).stdout.splitlines().count(SOEDINENIE):
        return
    parol = "".join(secrets.choice(AZBUKA) for _ in range(10))
    nmcli("con", "add", "type", "wifi", "ifname", "ap0", "con-name", SOEDINENIE,
          "autoconnect", "no", "ssid", SET, "mode", "ap", "ipv4.method", "shared",
          "ipv6.method", "ignore", "wifi-sec.key-mgmt", "wpa-psk", "wifi-sec.psk", parol)


CHASTNYE = ("10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16")


def pravila(vkl):
    # сперва снять прежние (после сбоя могли остаться), потом поставить
    for set_ in CHASTNYE:
        while subprocess.run(["iptables", "-D", "FORWARD", "-i", "ap0", "-d", set_,
                              "-m", "comment", "--comment", "bf7-tochka", "-j", "DROP"],
                             capture_output=True).returncode == 0:
            pass
    if vkl:
        for set_ in CHASTNYE:
            subprocess.run(["iptables", "-I", "FORWARD", "1", "-i", "ap0", "-d", set_,
                            "-m", "comment", "--comment", "bf7-tochka", "-j", "DROP"],
                           check=False)


def parol():
    r = nmcli("-s", "-g", "802-11-wireless-security.psk", "con", "show", SOEDINENIE, proverka=False)
    return r.stdout.strip()


def main():
    if aktivna():
        nmcli("con", "down", SOEDINENIE, proverka=False)
        nmcli("dev", "set", "ap0", "managed", "no", proverka=False)
        pravila(False)
        uvedomit("Выключена", "Раздача интернета остановлена")
        return 0
    pravila(True)   # до подъёма точки: ни одного пакета в домашнюю сеть
    try:
        nmcli("dev", "set", "ap0", "managed", "yes", proverka=False)
        zavesti()
        r = nmcli("con", "up", SOEDINENIE, proverka=False)
    except (OSError, subprocess.CalledProcessError) as e:
        uvedomit("Не включилась", str(e))
        return 1
    if r.returncode != 0:
        nmcli("dev", "set", "ap0", "managed", "no", proverka=False)
        pravila(False)
        uvedomit("Не включилась", (r.stderr or r.stdout).strip().splitlines()[-1])
        return 1
    pravila(True)
    uvedomit("Включена", f"Сеть «{SET}», пароль {parol()}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
