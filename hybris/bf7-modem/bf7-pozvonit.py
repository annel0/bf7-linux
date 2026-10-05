#!/usr/bin/env python3
"""BF7: позвонить хозяину и отдать разговор Клауду, когда он ответит.

    bf7-pozvonit [номер]     — по умолчанию номер из /etc/bf7/hozyain-nomer

ModemManager у модема MTK ставит вызову «active» сразу после ATD: списка
вызовов (+CLCC) у него нет, а ATD отвечает OK ещё до гудков. Настоящий ответ
абонента виден только в сообщениях модема +ECPI: <id>,6 — их мост
bf7-at-most вырезает из потока и пишет в журнал. Поэтому здесь ждём именно
его и лишь потом пишем в /run/bf7-dialog.log «ЗВОНОК: хозяин, принят» — по
этой строке ПК-сторож (golos-pc/dialog-storozh) здоровается и включает слух.
Раньше приветствие уходило в гудки.

Хозяину — только с 09:00 до 21:00 (его условие); ночью отказывает.
"""
import datetime
import os
import re
import select
import subprocess
import sys
import time

NOMER_FAJL = "/etc/bf7/hozyain-nomer"
DIALOG = "/run/bf7-dialog.log"
ZHDAT_OTVETA_S = 60
# +ECPI: <id>,<тип>,…  тип 6 — соединение, 133 — вызов снят. По пути бывают
# 3, 4, 2 (гудки) и 132 — это ещё не ответ и не сброс (видено 2026-09-23).
ECPI = re.compile(r"\+ECPI: (\d+),(\d+),")


def log(s):
    print("bf7-pozvonit: " + s, file=sys.stderr, flush=True)


def v_dialog(s):
    with open(DIALOG, "a", encoding="utf-8") as f:
        f.write("%s ЗВОНОК: %s\n" % (time.strftime("%H:%M:%S"), s))


def cifry(s):
    return "".join(c for c in s if c.isdigit())


def mmcli(*a):
    r = subprocess.run(["mmcli", *a], capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


def sostoyanie(vyzov):
    _, out = mmcli("-o", vyzov, "-K")
    m = re.search(r"call\.properties\.state\s*:\s*(\S+)", out)
    return m.group(1) if m else ""


def main(a):
    hozyain = cifry(open(NOMER_FAJL).read()) if os.path.exists(NOMER_FAJL) else ""
    nomer = cifry(a[0]) if a else hozyain
    if len(nomer) < 10:
        log("нет номера")
        return 2
    # BF7_HOZYAIN_RAZRESHIL=1 — хозяин сам сказал «можно сейчас» (его слово в чате).
    if (nomer[-10:] == hozyain[-10:] and not 9 <= datetime.datetime.now().hour < 21
            and os.environ.get("BF7_HOZYAIN_RAZRESHIL") != "1"):
        log("хозяину звонить только с 09:00 до 21:00 — не звоню")
        return 3

    # Журнал читаем с этой секунды, до набора — чтобы не пропустить ответ.
    zhurnal = subprocess.Popen(["journalctl", "-f", "-n0", "-o", "cat", "-t", "bf7-at-most"],
                               stdout=subprocess.PIPE)
    fd = zhurnal.stdout.fileno()
    hvost = b""
    try:
        kod, out = mmcli("-m", "any", "--voice-create-call", "number=+" + nomer)
        m = re.search(r"/Call/(\d+)", out)
        if kod or not m:
            log("вызов не создан: " + out.strip())
            return 1
        vyzov = m.group(1)
        kod, out = mmcli("-o", vyzov, "--start")
        if kod and sostoyanie(vyzov) in ("", "unknown", "terminated"):
            log("не набрал: " + out.strip())
            return 1
        log("набираю, жду ответа до %d с" % ZHDAT_OTVETA_S)
        konec = time.monotonic() + ZHDAT_OTVETA_S
        while time.monotonic() < konec:
            r, _, _ = select.select([fd], [], [], 1.0)
            if not r:
                if sostoyanie(vyzov) in ("", "terminated"):
                    log("вызов сброшен до ответа")
                    return 1
                continue
            # Читаем сырыми кусками: select не видит строк, осевших в буфере readline.
            kusok = os.read(fd, 4096)
            if not kusok:
                log("журнал закрылся")
                return 1
            *stroki, hvost = (hvost + kusok).split(b"\n")
            for stroka in stroki:
                m = ECPI.search(stroka.decode("utf-8", "replace"))
                if not m:
                    continue
                tip = int(m.group(2))
                if tip == 6:
                    log("ответили")
                    v_dialog("хозяин, принят (звонит Клауд)" if nomer[-10:] == hozyain[-10:]
                             else "собеседник, принят (звонит Клауд)")
                    return 0
                if tip == 133:
                    log("вызов снят до ответа (+ECPI тип 133)")
                    return 1
        log("не ответили за %d с — кладу трубку" % ZHDAT_OTVETA_S)
        mmcli("-o", vyzov, "--hangup")
        return 1
    finally:
        zhurnal.kill()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
