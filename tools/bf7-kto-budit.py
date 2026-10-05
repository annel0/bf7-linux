#!/usr/bin/python3
# BF7: кто будит процессор — разность ctxt-переключений по всем нитям и
# прерываний за N секунд (по умолчанию 20). Один процесс на весь замер:
# цикл на bash с awk/cat сам будит migration/N и rcu (см. research-log 2026-09-30).
import os, time, sys
def snap():
    r = {}
    for p in os.listdir("/proc"):
        if not p.isdigit(): continue
        try: tasks = os.listdir(f"/proc/{p}/task")
        except OSError: continue
        for t in tasks:
            try:
                s = open(f"/proc/{p}/task/{t}/status").read()
                name = s.split("\n",1)[0].split("\t",1)[1]
                c = sum(int(l.split()[1]) for l in s.splitlines() if "ctxt" in l)
                r[(t, name)] = c
            except (OSError, IndexError): pass
    return r
def irq():
    r = {}
    for l in open("/proc/interrupts").read().splitlines()[1:]:
        f = l.split()
        try: r[f[0]] = (sum(int(x) for x in f[1:5]), " ".join(f[5:])[-30:])
        except ValueError: pass
    return r
N = int(sys.argv[1]) if len(sys.argv) > 1 else 20
a, ia = snap(), irq()
time.sleep(N)
b, ib = snap(), irq()
print("экран:", open("/sys/class/drm/card1-DSI-1/enabled").read().strip(), "за", N, "с")
d = sorted(((b[k] - a[k], k[1]) for k in b if k in a and b[k] - a[k] > 10), reverse=True)
print("всего переключений:", sum(b[k] - a[k] for k in b if k in a))
for x, n in d[:18]: print(f"{x:6} {n}")
print("---")
d = sorted(((ib[k][0] - ia[k][0], k, ib[k][1]) for k in ib if k in ia and ib[k][0] - ia[k][0] > 10), reverse=True)
for x, k, n in d[:10]: print(f"{x:6} {k} {n}")
