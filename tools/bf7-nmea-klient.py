import socket, sys, time
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect("/run/bf7-gps/nmea.sock")
s.settimeout(3)
konec = time.time() + float(sys.argv[1] if len(sys.argv) > 1 else 15)
buf = b""
while time.time() < konec:
    try:
        d = s.recv(4096)
    except socket.timeout:
        continue
    if not d:
        break
    buf += d
s.close()
stroki = [l for l in buf.decode(errors="replace").splitlines() if l.startswith("$")]
tipy = {}
for l in stroki:
    t = l.split(",")[0]
    tipy[t] = tipy.get(t, 0) + 1
print("строк NMEA:", len(stroki), dict(sorted(tipy.items(), key=lambda x: -x[1])[:6]))
