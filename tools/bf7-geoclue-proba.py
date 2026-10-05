import sys, time
from gi.repository import Gio, GLib
b = Gio.bus_get_sync(Gio.BusType.SYSTEM)
G = "org.freedesktop.GeoClue2"
def call(path, iface, m, args=None, typ=None):
    return b.call_sync(G, path, iface, m, args, typ, Gio.DBusCallFlags.NONE, 10000, None)
p = call("/org/freedesktop/GeoClue2/Manager", G + ".Manager", "GetClient", None, GLib.VariantType("(o)")).unpack()[0]
print("клиент", p)
def setp(n, v):
    call(p, "org.freedesktop.DBus.Properties", "Set", GLib.Variant("(ssv)", (G + ".Client", n, v)))
setp("DesktopId", GLib.Variant("s", "sm.puri.Phosh"))
setp("RequestedAccuracyLevel", GLib.Variant("u", 8))
try:
    call(p, G + ".Client", "Start")
    print("Start принят")
except GLib.Error as e:
    print("Start:", e.message)
loop = GLib.MainLoop()
GLib.timeout_add_seconds(int(sys.argv[1]) if len(sys.argv) > 1 else 15, loop.quit)
loop.run()
try:
    call(p, G + ".Client", "Stop")
    print("Stop")
except GLib.Error as e:
    print("Stop:", e.message)
