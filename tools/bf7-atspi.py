#!/usr/bin/python3
# bf7-atspi: список кнопок приложения или нажатие по имени (или описанию)
#   bf7-atspi ПРИЛОЖЕНИЕ             — показать кнопки
#   bf7-atspi ПРИЛОЖЕНИЕ ИМЯ-КНОПКИ   — нажать
import sys
import gi
gi.require_version("Atspi", "2.0")
from gi.repository import Atspi

cel, knopka = sys.argv[1].lower(), (sys.argv[2] if len(sys.argv) > 2 else None)
d = Atspi.get_desktop(0)
najdeno = []

def obojti(o, gl=0):
    try:
        r, n = o.get_role_name(), o.get_name()
        opis = o.get_description()
    except Exception:
        return
    # у кнопок-картинок GTK4 имя — имя класса, смысл лежит в описании
    if opis and (not n or n.startswith("Gtk")):
        n = opis
    if "button" in r or r in ("label", "heading"):
        najdeno.append((o, r, n))
        if not knopka:
            print("  " * gl + f"{r}: {n!r}")
    for i in range(o.get_child_count()):
        obojti(o.get_child_at_index(i), gl + 1)

for i in range(d.get_child_count()):
    a = d.get_child_at_index(i)
    if a and cel in (a.get_name() or "").lower():
        obojti(a)
if knopka:
    for o, r, n in najdeno:
        if "button" in r and n == knopka:
            Atspi.Action.do_action(o, 0)
            print("нажал", n)
            break
    else:
        print("нет кнопки", knopka)
        sys.exit(1)
