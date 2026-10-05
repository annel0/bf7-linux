#!/usr/bin/env python3
"""Окно-лампа: ровная заливка заданного цвета на мониторе хозяина.

Служит управляемым источником света для камеры телефона. Цвет читается из
файла /tmp/lamp-colour при каждой перерисовке, так что менять его можно
снаружи, не трогая окно.
"""
import os, sys, time
from Xlib import X, display, Xatom

COLOUR_FILE = "/tmp/lamp-colour"

d = display.Display()
s = d.screen()
root = s.root
w, h = root.get_geometry().width, root.get_geometry().height

win = root.create_window(0, 0, w, h, 0, s.root_depth,
                         X.InputOutput, X.CopyFromParent,
                         background_pixel=s.black_pixel,
                         event_mask=X.ExposureMask | X.StructureNotifyMask)
win.set_wm_name("Lampa dlya kamery")
win.change_property(d.intern_atom('_NET_WM_STATE'), Xatom.ATOM, 32,
                    [d.intern_atom('_NET_WM_STATE_FULLSCREEN')])
win.map()
d.sync()

gc = win.create_gc()
cmap = s.default_colormap
prev = None
while True:
    try:
        c = open(COLOUR_FILE).read().strip() or "#606060"
    except OSError:
        c = "#606060"
    if c == "выход":
        break
    if c != prev:
        try:
            px = cmap.alloc_named_color(c).pixel
        except Exception:
            px = cmap.alloc_named_color("#606060").pixel
        gc.change(foreground=px)
        prev = c
    win.fill_rectangle(gc, 0, 0, w, h)
    d.sync()
    while d.pending_events():
        d.next_event()
    time.sleep(0.3)
win.destroy()
d.sync()
