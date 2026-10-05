#!/usr/bin/env python3
"""VNC-сервер поверх /dev/fb0 для BF7.

Отдаёт содержимое панели по протоколу RFB 3.8 и превращает движения мыши
клиента в события тачскрина, так что телефоном можно управлять с ПК.

Кодировки: Raw и Zlib. Кадр разбит на плитки 64x64, отправляются только
изменившиеся.

Запуск: bf7vnc.py [порт]   (по умолчанию 5900)
"""
import fcntl
import mmap
import os
import socket
import socketserver
import struct
import sys
import threading
import time
import subprocess
import zlib

FBIOGET_VSCREENINFO = 0x4600
FBIOGET_FSCREENINFO = 0x4602

TILE = 64
TOUCH_DEV = os.environ.get("BF7VNC_TOUCH", "/dev/input/event2")
# Устройство повёрнуто на 180° относительно панели (см. 99-bf7-touch.rules).
FLIP = os.environ.get("BF7VNC_FLIP", "1") not in ("0", "no", "")

EV_SYN, EV_KEY, EV_ABS = 0x00, 0x01, 0x03
SYN_REPORT = 0
BTN_TOUCH = 0x14A
ABS_MT_SLOT, ABS_MT_POSITION_X, ABS_MT_POSITION_Y, ABS_MT_TRACKING_ID = 0x2F, 0x35, 0x36, 0x39


class Framebuffer:
    def __init__(self, path="/dev/fb0"):
        self.f = open(path, "rb")
        v = fcntl.ioctl(self.f, FBIOGET_VSCREENINFO, bytes(160))
        (self.xres, self.yres, self.xvirt, self.yvirt,
         self.xoff, self.yoff, self.bpp) = struct.unpack_from("<7I", v, 0)
        (self.r_off, self.r_len, _, self.g_off, self.g_len, _,
         self.b_off, self.b_len, _, self.t_off, self.t_len, _) = struct.unpack_from("<12I", v, 32)
        fx = fcntl.ioctl(self.f, FBIOGET_FSCREENINFO, bytes(80))
        self.stride = struct.unpack_from("<I", fx, 48)[0]
        self.map = mmap.mmap(self.f.fileno(), self.stride * self.yvirt,
                             mmap.MAP_SHARED, mmap.PROT_READ)

    def current_yoffset(self):
        """Видимая страница может переключаться панорамированием."""
        v = fcntl.ioctl(self.f, FBIOGET_VSCREENINFO, bytes(160))
        return struct.unpack_from("<I", v, 20)[0]

    def grab(self):
        """Снимок видимой страницы: bytes, построчно, stride как у панели."""
        yo = self.current_yoffset()
        start = yo * self.stride
        return self.map[start:start + self.yres * self.stride]


class Touch:
    """Указатель клиента -> события мультитача."""

    def __init__(self, dev=TOUCH_DEV):
        self.fd = -1
        self.down = False
        self.lock = threading.Lock()
        try:
            self.fd = os.open(dev, os.O_WRONLY | os.O_NONBLOCK)
        except OSError as e:
            print(f"bf7vnc: тачскрин {dev} недоступен: {e}", flush=True)

    def _ev(self, t, c, v):
        os.write(self.fd, struct.pack("<qqHHi", 0, 0, t, c, v))

    def move(self, x, y, pressed):
        if self.fd < 0:
            return
        with self.lock:
            try:
                if pressed:
                    self._ev(EV_ABS, ABS_MT_SLOT, 0)
                    if not self.down:
                        self._ev(EV_ABS, ABS_MT_TRACKING_ID, 55)
                    self._ev(EV_ABS, ABS_MT_POSITION_X, x)
                    self._ev(EV_ABS, ABS_MT_POSITION_Y, y)
                    if not self.down:
                        self._ev(EV_KEY, BTN_TOUCH, 1)
                    self._ev(EV_SYN, SYN_REPORT, 0)
                    self.down = True
                elif self.down:
                    self._ev(EV_ABS, ABS_MT_SLOT, 0)
                    self._ev(EV_ABS, ABS_MT_TRACKING_ID, -1)
                    self._ev(EV_KEY, BTN_TOUCH, 0)
                    self._ev(EV_SYN, SYN_REPORT, 0)
                    self.down = False
            except OSError as e:
                print(f"bf7vnc: впрыск касания не удался: {e}", flush=True)


fb = Framebuffer()
touch = Touch()
NAME = b"BF7 panel"

# При выводе слоем наложения композитор не трогает /dev/fb0, и читать нам
# становится нечего. Пока есть хоть один зритель, кладём флаг — платформа
# mtkfb увидит его и будет дополнительно копировать кадр в фреймбуфер.
MIRROR_FLAG = os.environ.get("BF7VNC_MIRROR_FLAG", "/run/bf7-gui/mirror-fb")
_mirror_lock = threading.Lock()
_mirror_users = 0


def force_repaint():
    """Разбудить рабочий стол, чтобы композитор выдал кадр.

    При выводе слоем наложения фреймбуфер обновляется только вместе с новым
    кадром. На неподвижном рабочем столе кадров нет, и подключившийся зритель
    увидел бы то, что лежало в буфере с прошлого раза.
    """
    try:
        subprocess.Popen(["xrefresh", "-display", os.environ.get("BF7VNC_DISPLAY", ":1")],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except OSError:
        pass


def mirror_hold(delta):
    global _mirror_users
    with _mirror_lock:
        _mirror_users += delta
        try:
            if _mirror_users > 0:
                os.makedirs(os.path.dirname(MIRROR_FLAG), exist_ok=True)
                with open(MIRROR_FLAG, "w") as f:
                    f.write(str(_mirror_users))
            else:
                os.unlink(MIRROR_FLAG)
        except OSError:
            pass


def recv_exact(sock, n):
    buf = b""
    while len(buf) < n:
        c = sock.recv(n - len(buf))
        if not c:
            raise ConnectionError("клиент отключился")
        buf += c
    return buf


class Handler(socketserver.BaseRequestHandler):
    def setup(self):
        self.request.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.zs = zlib.compressobj(1)
        self.zlib_ok = False
        self.prev = None
        # Формат пикселей, который просит клиент; по умолчанию — наш.
        self.cbpp = 32
        self.cdepth = 24
        self.cbig = 0
        self.ctrue = 1
        self.cshift = (fb.r_off, fb.g_off, fb.b_off)

    def handle(self):
        s = self.request
        peer = self.client_address
        print(f"bf7vnc: подключился {peer}", flush=True)
        mirror_hold(+1)
        force_repaint()
        s.sendall(b"RFB 003.008\n")
        ver = recv_exact(s, 12)
        # список типов защиты: только None
        s.sendall(bytes([1, 1]))
        sec = recv_exact(s, 1)[0]
        if sec != 1:
            reason = "поддерживается только None".encode("utf-8")
            s.sendall(struct.pack(">I", 1) + struct.pack(">I", len(reason)) + reason)
            return
        s.sendall(struct.pack(">I", 0))          # SecurityResult: ok
        recv_exact(s, 1)                          # ClientInit (shared flag)

        s.sendall(struct.pack(">HH", fb.xres, fb.yres) +
                  self.pixel_format() +
                  struct.pack(">I", len(NAME)) + NAME)

        last_send = 0.0
        while True:
            s.settimeout(0.05)
            try:
                msg = recv_exact(s, 1)[0]
            except socket.timeout:
                msg = None
            except (ConnectionError, OSError):
                break
            if msg is not None and not self.dispatch(s, msg):
                break
            if self.pending and time.monotonic() - last_send > 0.04:
                try:
                    self.send_update(s)
                except (ConnectionError, OSError, socket.timeout):
                    break
                last_send = time.monotonic()
        mirror_hold(-1)
        print(f"bf7vnc: отключился {peer}", flush=True)

    pending = False

    def pixel_format(self):
        return struct.pack(">BBBBHHHBBBBBB",
                           self.cbpp, self.cdepth, self.cbig, self.ctrue,
                           255, 255, 255,
                           self.cshift[0], self.cshift[1], self.cshift[2],
                           0, 0, 0)

    def dispatch(self, s, msg):
        try:
            if msg == 0:          # SetPixelFormat
                body = recv_exact(s, 19)
                (bpp, depth, big, true_, rmax, gmax, bmax,
                 rs, gs, bs) = struct.unpack_from(">xxxBBBBHHHBBB", body, 0)
                if bpp == 32 and true_:
                    self.cbpp, self.cdepth, self.cbig = bpp, depth, big
                    self.cshift = (rs, gs, bs)
                    self.prev = None
                    print(f"bf7vnc: формат клиента {bpp}bpp сдвиги {rs}/{gs}/{bs}", flush=True)
            elif msg == 2:        # SetEncodings
                n = struct.unpack(">xH", recv_exact(s, 3))[0]
                encs = struct.unpack(">%di" % n, recv_exact(s, 4 * n))
                self.zlib_ok = 6 in encs
            elif msg == 3:        # FramebufferUpdateRequest
                recv_exact(s, 9)
                self.pending = True
            elif msg == 4:        # KeyEvent
                recv_exact(s, 7)
            elif msg == 5:        # PointerEvent
                mask, x, y = struct.unpack(">BHH", recv_exact(s, 5))
                x = min(x, fb.xres - 1)
                y = min(y, fb.yres - 1)
                if FLIP:
                    # Тачскрин повёрнут на 180° относительно панели, и libinput
                    # разворачивает его матрицей калибровки. Впрыск идёт мимо
                    # неё, в сырое устройство, поэтому разворачиваем сами.
                    x = fb.xres - 1 - x
                    y = fb.yres - 1 - y
                touch.move(x, y, bool(mask & 1))
            elif msg == 6:        # ClientCutText
                n = struct.unpack(">xxxI", recv_exact(s, 7))[0]
                recv_exact(s, n)
            else:
                return False
        except (ConnectionError, OSError, struct.error):
            return False
        return True

    def convert(self, raw):
        """Перестановка каналов, если клиент просит другой порядок."""
        if self.cshift == (fb.r_off, fb.g_off, fb.b_off) and not self.cbig:
            return raw
        out = bytearray(raw)
        # сдвиги кратны 8 в любом разумном клиенте
        src = {fb.r_off // 8: 0, fb.g_off // 8: 1, fb.b_off // 8: 2}
        dst = [self.cshift[0] // 8, self.cshift[1] // 8, self.cshift[2] // 8]
        idx = [0, 0, 0, 0]
        for pos, comp in src.items():
            idx[dst[comp]] = pos
        mv = memoryview(raw)
        for i in range(0, len(raw), 4):
            out[i] = mv[i + idx[0]]
            out[i + 1] = mv[i + idx[1]]
            out[i + 2] = mv[i + idx[2]]
        return bytes(out)

    def send_update(self, s):
        cur = fb.grab()
        w, h, stride = fb.xres, fb.yres, fb.stride
        rects = []
        if self.prev is None:
            rects = [(0, 0, w, h)]
        else:
            for ty in range(0, h, TILE):
                th = min(TILE, h - ty)
                for tx in range(0, w, TILE):
                    tw = min(TILE, w - tx)
                    changed = False
                    for y in range(ty, ty + th, 8):   # выборочно по строкам
                        o = y * stride + tx * 4
                        if cur[o:o + tw * 4] != self.prev[o:o + tw * 4]:
                            changed = True
                            break
                    if changed:
                        rects.append((tx, ty, tw, th))
        self.prev = cur
        self.pending = False
        if not rects:
            rects = []
        # склеиваем соседние плитки в одной строке
        merged = []
        for r in rects:
            if merged and merged[-1][1] == r[1] and merged[-1][0] + merged[-1][2] == r[0] \
               and merged[-1][3] == r[3]:
                x, y, ww, hh = merged[-1]
                merged[-1] = (x, y, ww + r[2], hh)
            else:
                merged.append(r)
        s.sendall(struct.pack(">BxH", 0, len(merged)))
        for (x, y, ww, hh) in merged:
            rows = []
            for row in range(y, y + hh):
                o = row * stride + x * 4
                rows.append(cur[o:o + ww * 4])
            raw = self.convert(b"".join(rows))
            if self.zlib_ok:
                data = self.zs.compress(raw) + self.zs.flush(zlib.Z_SYNC_FLUSH)
                s.sendall(struct.pack(">HHHHi", x, y, ww, hh, 6) +
                          struct.pack(">I", len(data)) + data)
            else:
                s.sendall(struct.pack(">HHHHi", x, y, ww, hh, 0) + raw)


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 5900
    # Пароля в протоколе тут нет, а управление касаниями — это полный доступ к
    # телефону. По умолчанию слушаем только USB-сеть; чтобы отдать наружу,
    # задайте BF7VNC_BIND=0.0.0.0 осознанно.
    bind = os.environ.get("BF7VNC_BIND", "192.168.7.2")
    print(f"bf7vnc: панель {fb.xres}x{fb.yres} stride={fb.stride} "
          f"каналы r{fb.r_off}/g{fb.g_off}/b{fb.b_off}, слушаю {bind}:{port}", flush=True)
    # poll_interval=None: по умолчанию serve_forever проверяет флаг остановки
    # каждые 0,5 с — два пробуждения процессора в секунду без единого клиента
    # (замер 2026-09-23). Остановку мы не зовём никогда, так что ждём
    # подключения без тайм-аута.
    Server((bind, port), Handler).serve_forever(poll_interval=None)
