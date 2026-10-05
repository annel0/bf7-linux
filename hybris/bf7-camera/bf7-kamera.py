#!/usr/bin/env python3
"""BF7: «Камера» — видоискатель, спуск, ночной режим, фонарик.

Прежнее окно (bf7-camera-gui.py) умело только запускать серии: без
видоискателя, шире экрана и поверх панелей Phosh. Здесь обычное приложение
телефона.

Как устроено:
  * видоискатель — GStreamer: /dev/video0 (его кормит bf7-camd,
    1024x768 YUYV) уменьшается до 512x384, 15 кадров в секунду, и рисуется
    в gtksink. Когда окно не в фокусе, поток стоит — процессор не тратится;
  * снимок делает сам bf7-camd, командой «фото» в /run/bf7-camd: он
    разбирает мозаику Байера полностью (2104x1560, вчетверо больше точек,
    чем видео) и складывает кадры — «Фото» 4, «Ночь» 16. Видео при этом не
    прерывается, поэтому видоискатель живёт и во время съёмки;
  * снимок поворачивается так же, как видоискатель. Как стоит матрица,
    проверить было нечем: по умолчанию — на четверть по часовой, как у
    задних камер Android; кнопка ⟳ поворачивает и запоминает;
  * пока окно открыто, экран не гаснет (запрет простоя через сеанс).

Снимки — в папку «Изображения» пользователя (XDG_PICTURES_DIR), IMG_*.jpg.
"""
import os
import threading
import time

import gi
gi.require_version("Gtk", "3.0")
gi.require_version("Gdk", "3.0")
gi.require_version("Gst", "1.0")
gi.require_version("GdkPixbuf", "2.0")
from gi.repository import Gdk, GdkPixbuf, Gio, GLib, Gst, Gtk

APP_ID = "ru.bf7.Kamera"
KAMERA = "/dev/video0"
CAMD_CMD = "/run/bf7-camd"
CAMD_GRAB = "/run/bf7-camd.grabbed"
FONAR = "/sys/class/leds/white:torch/brightness"
NASTROJKA = os.path.join(GLib.get_user_config_dir(), "bf7-kamera.conf")

# Порядок — по часовой стрелке; номер в списке — это и значение
# video-direction у videoflip (identity, 90r, 180, 90l). Старое свойство
# method в GStreamer 1.22 молча не поворачивает — проверено на снимке.
# Второе в паре — поворот GdkPixbuf (его углы считаются против часовой).
POVOROTY = [
    ("none", GdkPixbuf.PixbufRotation.NONE),
    ("clockwise", GdkPixbuf.PixbufRotation.CLOCKWISE),
    ("rotate-180", GdkPixbuf.PixbufRotation.UPSIDEDOWN),
    ("counterclockwise", GdkPixbuf.PixbufRotation.COUNTERCLOCKWISE),
]
POVOROT_PO_UMOLCHANIYU = 1
REZHIMY = {"foto": 4, "noch": 16}

CSS = b"""
.kamera { background: #000; }
.panel { background: #000; padding: 10px 16px 18px 16px; }
.spusk {
    min-width: 76px; min-height: 76px; border-radius: 9999px; padding: 0;
    background: #f2f2f2; border: 5px solid #8a8a8a; box-shadow: none;
}
.spusk:active { background: #c8c8c8; }
.spusk:disabled { background: #555; border-color: #333; }
.spusk.noch { background: #9db4ff; }
.krug {
    min-width: 56px; min-height: 56px; border-radius: 9999px; padding: 0;
    background: #1c1c1f; border: 1px solid #333; color: #eee;
}
.krug:checked { background: #f5d547; color: #111; }
.miniatyura { min-width: 56px; min-height: 56px; border-radius: 10px; padding: 0;
              background: #1c1c1f; border: 1px solid #333; }
.status {
    background: rgba(0, 0, 0, 0.6); color: #fff; border-radius: 14px;
    padding: 6px 16px; margin-top: 12px; font-size: 15px;
}
.rezhim { background: none; border: none; box-shadow: none; color: #999;
          font-size: 15px; padding: 4px 14px; }
.rezhim:checked { color: #f5d547; font-weight: bold; }
"""


def papka_snimkov():
    p = GLib.get_user_special_dir(GLib.UserDirectory.DIRECTORY_PICTURES)
    if not p:
        p = os.path.join(GLib.get_home_dir(), "Pictures")
    os.makedirs(p, exist_ok=True)
    return p


def prochest_povorot():
    try:
        with open(NASTROJKA) as f:
            for s in f:
                k, _, v = s.strip().partition("=")
                if k == "povorot" and v.isdigit() and int(v) < len(POVOROTY):
                    return int(v)
    except OSError:
        pass
    return POVOROT_PO_UMOLCHANIYU


def zapisat_povorot(i):
    try:
        os.makedirs(os.path.dirname(NASTROJKA), exist_ok=True)
        with open(NASTROJKA, "w") as f:
            f.write("povorot=%d\n" % i)
    except OSError:
        pass


def fonar(vkl):
    try:
        with open(FONAR, "w") as f:
            f.write("5" if vkl else "0")
    except OSError:
        pass


class Okno(Gtk.ApplicationWindow):
    def __init__(self, app):
        super().__init__(application=app, title="Камера")
        self.set_default_size(360, 720)
        self.get_style_context().add_class("kamera")
        self.povorot = prochest_povorot()
        self.rezhim = "foto"
        self.zanyato = False
        self.inhibit_cookie = 0
        self.truba = None

        glavnyj = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        self.add(glavnyj)

        nalozhenie = Gtk.Overlay()
        glavnyj.pack_start(nalozhenie, True, True, 0)

        self.zaglushka = Gtk.Label(label="Камера включается…")
        self.zaglushka.get_style_context().add_class("status")
        self.vidoiskatel = Gtk.Box()
        self.vidoiskatel.pack_start(self.zaglushka, True, True, 0)
        nalozhenie.add(self.vidoiskatel)

        self.status = Gtk.Label()
        self.status.get_style_context().add_class("status")
        self.status.set_halign(Gtk.Align.CENTER)
        self.status.set_valign(Gtk.Align.START)
        self.status.set_no_show_all(True)
        nalozhenie.add_overlay(self.status)

        b_povorot = Gtk.Button.new_from_icon_name(
            "object-rotate-right-symbolic", Gtk.IconSize.LARGE_TOOLBAR)
        b_povorot.get_style_context().add_class("krug")
        b_povorot.set_tooltip_text("Повернуть кадр")
        b_povorot.set_halign(Gtk.Align.END)
        b_povorot.set_valign(Gtk.Align.START)
        b_povorot.set_margin_top(10)
        b_povorot.set_margin_end(10)
        b_povorot.connect("clicked", self.povernut)
        nalozhenie.add_overlay(b_povorot)

        panel = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
        panel.get_style_context().add_class("panel")
        glavnyj.pack_end(panel, False, False, 0)

        rezhimy = Gtk.Box(spacing=6, halign=Gtk.Align.CENTER)
        self.b_foto = Gtk.RadioButton.new_with_label(None, "Фото")
        self.b_noch = Gtk.RadioButton.new_with_label_from_widget(self.b_foto, "Ночь")
        for b, r in ((self.b_foto, "foto"), (self.b_noch, "noch")):
            b.set_mode(False)
            b.get_style_context().add_class("rezhim")
            b.connect("toggled", self.smena_rezhima, r)
            rezhimy.pack_start(b, False, False, 0)
        panel.pack_start(rezhimy, False, False, 0)

        ryad = Gtk.CenterBox() if hasattr(Gtk, "CenterBox") else Gtk.Box()
        ryad.set_homogeneous(True)
        panel.pack_start(ryad, False, False, 0)

        self.b_mini = Gtk.Button()
        self.b_mini.get_style_context().add_class("miniatyura")
        self.b_mini.set_tooltip_text("Последний снимок")
        self.b_mini.set_halign(Gtk.Align.START)
        self.b_mini.set_valign(Gtk.Align.CENTER)
        self.b_mini.connect("clicked", self.otkryt_snimok)
        self.mini = Gtk.Image()
        self.b_mini.add(self.mini)
        ryad.pack_start(self.b_mini, True, True, 0)

        self.b_spusk = Gtk.Button()
        self.b_spusk.get_style_context().add_class("spusk")
        self.b_spusk.set_tooltip_text("Снять")
        self.b_spusk.set_halign(Gtk.Align.CENTER)
        self.b_spusk.connect("clicked", self.snyat)
        ryad.pack_start(self.b_spusk, True, True, 0)

        self.b_fonar = Gtk.ToggleButton()
        self.b_fonar.add(Gtk.Image.new_from_icon_name(
            "display-brightness-symbolic", Gtk.IconSize.LARGE_TOOLBAR))
        self.b_fonar.get_style_context().add_class("krug")
        self.b_fonar.set_tooltip_text("Подсветка")
        self.b_fonar.set_halign(Gtk.Align.END)
        self.b_fonar.set_valign(Gtk.Align.CENTER)
        self.b_fonar.connect("toggled", lambda b: fonar(b.get_active()))
        ryad.pack_start(self.b_fonar, True, True, 0)

        # Рамка фокуса на сенсорном экране только мешает: пунктир вокруг
        # кнопки, которую нажали пальцем.
        for b in (b_povorot, self.b_foto, self.b_noch, self.b_mini,
                  self.b_spusk, self.b_fonar):
            b.set_can_focus(False)

        self.poslednij = None
        self.najti_poslednij()

        self.connect("notify::is-active", self.fokus)
        self.connect("destroy", self.zakryt)
        self.postroit_trubu()

    # --- видоискатель -----------------------------------------------------
    def postroit_trubu(self):
        opisanie = (
            "v4l2src device=%s ! video/x-raw,format=YUY2,width=1024,height=768 "
            "! videorate drop-only=true ! video/x-raw,framerate=15/1 "
            "! videoscale method=0 ! video/x-raw,width=512,height=384 "
            "! videoflip name=flip video-direction=%d ! videoconvert "
            "! gtksink name=sink sync=false" % (KAMERA, self.povorot))
        try:
            self.truba = Gst.parse_launch(opisanie)
        except GLib.Error as e:
            self.pokazat("Нет видоискателя: %s" % e.message, 0)
            return
        vidzhet = self.truba.get_by_name("sink").get_property("widget")
        self.vidoiskatel.remove(self.zaglushka)
        self.vidoiskatel.pack_start(vidzhet, True, True, 0)
        vidzhet.show()
        shina = self.truba.get_bus()
        shina.add_signal_watch()
        shina.connect("message::error", self.oshibka_truby)

    def oshibka_truby(self, _shina, msg):
        err, _ = msg.parse_error()
        self.truba.set_state(Gst.State.NULL)
        self.pokazat("Камера недоступна: %s" % err.message, 0)

    def fokus(self, *_):
        if not self.truba:
            return
        # В фоне поток не нужен: разбор кадров стоит процессора и заряда.
        self.truba.set_state(Gst.State.PLAYING if self.is_active() else Gst.State.NULL)
        app = self.get_application()
        if self.is_active() and not self.inhibit_cookie:
            self.inhibit_cookie = app.inhibit(
                self, Gtk.ApplicationInhibitFlags.IDLE, "Камера открыта")
        elif not self.is_active() and self.inhibit_cookie:
            app.uninhibit(self.inhibit_cookie)
            self.inhibit_cookie = 0
            self.b_fonar.set_active(False)

    def povernut(self, _b):
        self.povorot = (self.povorot + 1) % len(POVOROTY)
        zapisat_povorot(self.povorot)
        if self.truba:
            self.truba.get_by_name("flip").set_property(
                "video-direction", self.povorot)

    def zakryt(self, *_):
        fonar(False)
        if self.truba:
            self.truba.set_state(Gst.State.NULL)

    # --- съёмка -----------------------------------------------------------
    def smena_rezhima(self, knopka, rezhim):
        if not knopka.get_active():
            return
        self.rezhim = rezhim
        ctx = self.b_spusk.get_style_context()
        (ctx.add_class if rezhim == "noch" else ctx.remove_class)("noch")
        if rezhim == "noch":
            self.pokazat("Ночь: 16 кадров, держите ровно полсекунды", 3)

    def snyat(self, _b):
        if self.zanyato:
            return
        self.zanyato = True
        self.b_spusk.set_sensitive(False)
        self.pokazat("Держите ровно…", 0)
        self.otklik("camera-shutter")
        papka = papka_snimkov()
        imya = time.strftime("IMG_%Y%m%d_%H%M%S.jpg")
        vremennyj = os.path.join(papka, ".bf7-kamera-" + imya)
        gotovyj = os.path.join(papka, imya)
        n = REZHIMY[self.rezhim]
        threading.Thread(target=self.snyat_v_fone,
                         args=(vremennyj, gotovyj, n, self.povorot),
                         daemon=True).start()

    def snyat_v_fone(self, vremennyj, gotovyj, n, povorot):
        try:
            os.remove(CAMD_GRAB)
        except OSError:
            pass
        try:
            with open(CAMD_CMD, "w") as f:
                f.write("фото %s %d\n" % (vremennyj, n))
        except OSError as e:
            GLib.idle_add(self.konec, None, "Съёмщик не отвечает: %s" % e.strerror)
            return
        # Кадры собраны — можно двигаться; дальше bf7-camd разбирает и пишет.
        srok = time.monotonic() + 10
        while time.monotonic() < srok and not os.path.exists(CAMD_GRAB):
            time.sleep(0.05)
        GLib.idle_add(self.pokazat, "Можно двигаться, обрабатываю…", 0)
        srok = time.monotonic() + 60
        razmer = -1
        while time.monotonic() < srok:
            try:
                r = os.path.getsize(vremennyj)
            except OSError:
                r = 0
            if r > 0 and r == razmer:
                break
            razmer = r
            time.sleep(0.3)
        else:
            GLib.idle_add(self.konec, None, "Снимок не получился")
            return
        try:
            povorot_pb = POVOROTY[povorot][1]
            if povorot_pb == GdkPixbuf.PixbufRotation.NONE:
                os.rename(vremennyj, gotovyj)
            else:
                pb = GdkPixbuf.Pixbuf.new_from_file(vremennyj)
                pb.rotate_simple(povorot_pb).savev(
                    gotovyj, "jpeg", ["quality"], ["92"])
                os.remove(vremennyj)
        except (GLib.Error, OSError) as e:
            GLib.idle_add(self.konec, None, "Не сохранить: %s" % e)
            return
        GLib.idle_add(self.konec, gotovyj, "Снято")

    def konec(self, put, tekst):
        self.zanyato = False
        self.b_spusk.set_sensitive(True)
        self.pokazat(tekst, 2)
        if put:
            self.poslednij = put
            self.obnovit_miniatyuru()
        return False

    # --- мелочи -----------------------------------------------------------
    def pokazat(self, tekst, sekund):
        self.status.set_text(tekst)
        self.status.show()
        if getattr(self, "tajmer", 0):
            GLib.source_remove(self.tajmer)
            self.tajmer = 0
        if sekund:
            def spryatat():
                self.status.hide()
                self.tajmer = 0
                return False
            self.tajmer = GLib.timeout_add_seconds(sekund, spryatat)
        return False

    def otklik(self, sobytie):
        # Звук и вибро спуска — через feedbackd, как у всех программ.
        try:
            Gio.bus_get_sync(Gio.BusType.SESSION, None).call(
                "org.sigxcpu.Feedback", "/org/sigxcpu/Feedback",
                "org.sigxcpu.Feedback", "TriggerFeedback",
                GLib.Variant("(ssa{sv}i)", (APP_ID, sobytie, {}, -1)),
                None, Gio.DBusCallFlags.NONE, 500, None, None, None)
        except GLib.Error:
            pass

    def najti_poslednij(self):
        papka = papka_snimkov()
        try:
            snimki = sorted(f for f in os.listdir(papka)
                            if f.startswith("IMG_") and f.endswith(".jpg"))
        except OSError:
            snimki = []
        if snimki:
            self.poslednij = os.path.join(papka, snimki[-1])
        self.obnovit_miniatyuru()

    def obnovit_miniatyuru(self):
        if not self.poslednij:
            self.mini.set_from_icon_name("image-x-generic-symbolic",
                                         Gtk.IconSize.LARGE_TOOLBAR)
            return
        try:
            pb = GdkPixbuf.Pixbuf.new_from_file_at_scale(self.poslednij, 52, 52, True)
            self.mini.set_from_pixbuf(pb)
        except GLib.Error:
            pass

    def otkryt_snimok(self, _b):
        if self.poslednij and os.path.exists(self.poslednij):
            Gio.AppInfo.launch_default_for_uri(
                GLib.filename_to_uri(self.poslednij, None), None)


class Prilozhenie(Gtk.Application):
    def __init__(self):
        super().__init__(application_id=APP_ID)

    def do_startup(self):
        Gtk.Application.do_startup(self)
        css = Gtk.CssProvider()
        css.load_from_data(CSS)
        Gtk.StyleContext.add_provider_for_screen(
            Gdk.Screen.get_default(), css, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)
        Gtk.Settings.get_default().set_property(
            "gtk-application-prefer-dark-theme", True)

    def do_activate(self):
        okno = self.get_active_window() or Okno(self)
        okno.show_all()
        okno.present()


if __name__ == "__main__":
    Gst.init(None)
    GLib.set_prgname(APP_ID)
    Prilozhenie().run(None)
