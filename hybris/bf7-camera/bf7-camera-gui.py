#!/usr/bin/env python3
"""Кнопки съёмки на экране телефона.

Нужны, когда телефон уносят из-под провода: набирать команды в терминале в
темноте неудобно, а тут три крупные кнопки и крупная надпись о том, что
сейчас происходит. Отклик дублируется вибро — на экран в момент съёмки
смотреть не обязательно.
"""
import os
import subprocess
import threading

import gi
gi.require_version("Gtk", "3.0")
from gi.repository import Gtk, GLib

DIR = os.environ.get("BF7_PHOTO_DIR", "/root/фото")
CSS = """
window { background: #101014; }
label#state { color: #e8e8ea; font-size: 22px; }
label#hint  { color: #8a8a92; font-size: 16px; }
button { font-size: 25px; margin: 5px 16px; padding: 14px; border-radius: 12px;
         background: #23232c; color: #e8e8ea; border: 1px solid #3a3a46; }
button:active { background: #3a6b4a; }
button:disabled { color: #55555e; }
button.тёмный { background: #2c2230; border-color: #4a3a50; font-size: 21px; }
button.тёмный:active { background: #5a3a6a; }
""".encode()


class Okno(Gtk.Window):
    def __init__(self):
        super().__init__(title="Съёмка")
        self.fullscreen()
        self.connect("destroy", Gtk.main_quit)

        css = Gtk.CssProvider()
        css.load_from_data(CSS)
        Gtk.StyleContext.add_provider_for_screen(
            self.get_screen(), css, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)

        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=2)
        self.add(box)

        self.state = Gtk.Label(label="готов")
        self.state.set_name("state")
        self.state.set_margin_top(10)
        box.pack_start(self.state, False, False, 0)

        self.hint = Gtk.Label(label="снято: %d" % self.skolko())
        self.hint.set_name("hint")
        box.pack_start(self.hint, False, False, 0)

        self.knopki = []
        for подпись, режим, n in (("Серия 16 кадров", "серия", 16),
                                  ("Серия 8 кадров", "серия", 8),
                                  ("Снимок", "фото", 4),
                                  ("Тёмный кадр — объектив закрыть", "темнота", 16)):
            b = Gtk.Button(label=подпись)
            if режим == "темнота":
                b.get_style_context().add_class("тёмный")
            b.connect("clicked", self.snyat, режим, n)
            box.pack_start(b, True, True, 0)
            self.knopki.append(b)

        v = Gtk.Button(label="закрыть")
        v.connect("clicked", lambda _: Gtk.main_quit())
        box.pack_start(v, False, False, 0)

    def skolko(self):
        n = 0
        for к in (DIR, "/root/серии"):
            try:
                n += len(os.listdir(к))
            except OSError:
                pass
        return n

    def snyat(self, _knopka, режим, n):
        for b in self.knopki:
            b.set_sensitive(False)
        if режим == "темнота":
            self.state.set_text("снимаю тёмный кадр…")
            self.hint.set_text("объектив должен быть закрыт наглухо")
        else:
            self.state.set_text("держите ровно…")
            self.hint.set_text("%s, %d кадров" % (режим, n))
        self.режим = режим
        threading.Thread(target=self.rabota, args=(режим, n), daemon=True).start()

    def rabota(self, режим, n):
        try:
            os.remove("/run/bf7-camd.grabbed")
        except OSError:
            pass
        команда = {"серия": ["bf7-burst", str(n)],
                   "фото": ["bf7-snap", str(n)],
                   "темнота": ["bf7-dark", str(n)]}[режим]
        p = subprocess.Popen(команда,
                             stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        # как только кадры собраны — сказать, что можно двигаться
        for _ in range(80):
            if os.path.exists("/run/bf7-camd.grabbed"):
                GLib.idle_add(self.state.set_text, "можно двигаться, считаю…")
                break
            threading.Event().wait(0.1)
        out, _ = p.communicate()
        name = out.decode().strip().split("/")[-1] if out else ""
        GLib.idle_add(self.gotovo, p.returncode == 0, name)

    def gotovo(self, ok, name):
        self.state.set_text("готово" if ok else "не вышло")
        if not ok:
            self.hint.set_text("смотрите журнал bf7-camd")
        elif getattr(self, "режим", "") == "темнота":
            self.hint.set_text("калибровка снята — теперь открывайте объектив")
        else:
            self.hint.set_text(name)
        for b in self.knopki:
            b.set_sensitive(True)
        return False


if __name__ == "__main__":
    os.makedirs(DIR, exist_ok=True)
    # экран не гасить, пока окно открыто: в темноте искать кнопку наощупь плохо
    subprocess.call(["xset", "s", "off", "-dpms"])
    Okno().show_all()
    Gtk.main()
