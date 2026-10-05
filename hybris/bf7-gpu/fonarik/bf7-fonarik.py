#!/usr/bin/python3
"""BF7: «Фонарик» — маленькое приложение для светодиода вспышки.

Почему приложение, а не голый переключатель. Первым был ярлык на скрипт,
который зажигал или гасил фонарь. Работал, но Phosh после любого запуска
сворачивает сетку и ждёт окна, а окна нет — оставался пустой синий фон, и
чтобы погасить фонарь, сетку приходилось разворачивать заново. У обычного
телефона фонарик — приложение с большой кнопкой; так и сделано.

Поведение как у телефонных фонариков:
  * открыл — фонарь загорается на последней выбранной яркости;
  * большая кнопка — погасить и зажечь;
  * ползунок — яркость, пять ступеней (ступени — в модуле bf7_torch);
  * закрыл окно — фонарь гаснет, чтобы не остаться гореть забытым.

Устройство — /sys/class/leds/white:torch, его даёт модуль bf7_torch
(kernel-work/bf7-torch). Сеанс у нас от root, поэтому пишем в sysfs прямо.
Кнопка в шторке Phosh работала бы через logind, а сеанса logind у нашей
оболочки нет — см. docs/bf7-kernel/research-log.md, 2026-09-23.
"""

import os
import signal
import sys

import gi

gi.require_version("Gtk", "3.0")
gi.require_version("Gdk", "3.0")
gi.require_version("Handy", "1")
from gi.repository import Gdk, GLib, Gtk, Handy  # noqa: E402

FONAR = "/sys/class/leds/white:torch"
PAMYAT = os.path.expanduser("~/.config/bf7-fonarik")


def prochitat(imya, zapas=0):
    try:
        with open(os.path.join(FONAR, imya)) as f:
            return int(f.read().strip())
    except (OSError, ValueError):
        return zapas


def zapisat_yarkost(znachenie):
    try:
        with open(os.path.join(FONAR, "brightness"), "w") as f:
            f.write("%d\n" % znachenie)
        return True
    except OSError as e:
        print("bf7-fonarik: не записать яркость: %s" % e, file=sys.stderr)
        return False


def vspomnit_stupen(maksimum):
    try:
        with open(PAMYAT) as f:
            s = int(f.read().strip())
        if 1 <= s <= maksimum:
            return s
    except (OSError, ValueError):
        pass
    return maksimum


def zapomnit_stupen(s):
    try:
        os.makedirs(os.path.dirname(PAMYAT), exist_ok=True)
        with open(PAMYAT, "w") as f:
            f.write("%d\n" % s)
    except OSError:
        pass


class Okno(Handy.ApplicationWindow):
    def __init__(self, prilozhenie):
        super().__init__(application=prilozhenie, title="Фонарик")
        self.set_default_size(360, 640)

        self.maksimum = max(1, prochitat("max_brightness", 1))
        self.stupen = vspomnit_stupen(self.maksimum)
        self.obnovlyayu = False

        korob = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        shapka = Handy.HeaderBar(title="Фонарик", show_close_button=True)
        korob.pack_start(shapka, False, False, 0)

        telo = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=28,
                       valign=Gtk.Align.CENTER, halign=Gtk.Align.CENTER,
                       vexpand=True, margin=24)

        self.knopka = Gtk.ToggleButton()
        self.knopka.get_style_context().add_class("bf7-fonar")
        self.knopka.set_size_request(200, 200)
        # Без этого ящик растягивает кнопку по ширине ползунка, и круг
        # становится овалом.
        self.knopka.set_halign(Gtk.Align.CENTER)
        self.znachok = Gtk.Image.new_from_icon_name("bf7-fonarik", Gtk.IconSize.DIALOG)
        self.znachok.set_pixel_size(112)
        self.knopka.add(self.znachok)
        self.knopka.connect("toggled", self.na_knopku)
        telo.pack_start(self.knopka, False, False, 0)

        self.nadpis = Gtk.Label()
        self.nadpis.get_style_context().add_class("title-2")
        telo.pack_start(self.nadpis, False, False, 0)

        if self.maksimum > 1:
            podpis = Gtk.Label(label="Яркость")
            podpis.get_style_context().add_class("dim-label")
            telo.pack_start(podpis, False, False, 0)
            self.polzunok = Gtk.Scale.new_with_range(
                Gtk.Orientation.HORIZONTAL, 1, self.maksimum, 1)
            self.polzunok.set_digits(0)
            self.polzunok.set_draw_value(False)
            self.polzunok.set_size_request(260, -1)
            for i in range(1, self.maksimum + 1):
                self.polzunok.add_mark(i, Gtk.PositionType.BOTTOM, None)
            self.polzunok.set_value(self.stupen)
            self.polzunok.connect("value-changed", self.na_polzunok)
            telo.pack_start(self.polzunok, False, False, 0)
        else:
            self.polzunok = None

        korob.pack_start(telo, True, True, 0)
        self.add(korob)
        # В GTK3 дочерние виджеты скрыты, пока их не показать: без этой
        # строки окно выходило пустым серым фоном.
        korob.show_all()

        self.connect("destroy", self.na_zakrytie)

        # Открыли фонарик — значит, нужен свет.
        self.zazhech(True)
        # Кто-то другой может погасить фонарь (скрипт, другая программа) —
        # сверяемся раз в полсекунды, чтобы кнопка не врала.
        GLib.timeout_add(500, self.sverit)

    def zazhech(self, vkl):
        zapisat_yarkost(self.stupen if vkl else 0)
        self.pokazat()

    def pokazat(self):
        gorit = prochitat("brightness") > 0
        self.obnovlyayu = True
        self.knopka.set_active(gorit)
        self.obnovlyayu = False
        self.nadpis.set_text("Горит" if gorit else "Погашен")

    def na_knopku(self, knopka):
        if not self.obnovlyayu:
            self.zazhech(knopka.get_active())

    def na_polzunok(self, polzunok):
        self.stupen = int(round(polzunok.get_value()))
        zapomnit_stupen(self.stupen)
        if prochitat("brightness") > 0:
            zapisat_yarkost(self.stupen)

    def sverit(self):
        self.pokazat()
        return True

    def na_zakrytie(self, *_):
        zapisat_yarkost(0)


STIL = b"""
.bf7-fonar {
    border-radius: 9999px;
    background: #2b2f36;
    border: 3px solid #3d434d;
}
.bf7-fonar:checked {
    background: radial-gradient(circle, #fff3b0 0%, #ffd84a 55%, #f0a500 100%);
    border-color: #ffe27a;
    box-shadow: 0 0 36px rgba(255, 216, 74, 0.55);
}
"""


def main():
    if not os.path.isdir(FONAR):
        print("bf7-fonarik: нет %s — не загружен модуль bf7_torch" % FONAR,
              file=sys.stderr)
        return 1

    Handy.init()
    prilozhenie = Gtk.Application(application_id="org.bf7.Fonarik")

    def na_zapusk(app):
        stil = Gtk.CssProvider()
        stil.load_from_data(STIL)
        Gtk.StyleContext.add_provider_for_screen(
            Gdk.Screen.get_default(), stil,
            Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)
        okno = app.get_active_window() or Okno(app)
        okno.present()

    prilozhenie.connect("activate", na_zapusk)

    # Закрыть могут и не окном: сигналом при выключении, kill из консоли.
    # Обычный Python умирает от SIGTERM, не выполнив ничего, — и фонарь
    # остался бы гореть. Гасим и выходим по-хорошему.
    def na_signal(*_):
        zapisat_yarkost(0)
        prilozhenie.quit()
        return GLib.SOURCE_REMOVE

    for nomer in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP):
        GLib.unix_signal_add(GLib.PRIORITY_DEFAULT, nomer, na_signal)

    return prilozhenie.run(sys.argv)


if __name__ == "__main__":
    sys.exit(main())
