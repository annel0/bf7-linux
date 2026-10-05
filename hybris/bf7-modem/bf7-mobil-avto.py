#!/usr/bin/env python3
"""BF7: мобильный интернет сам — но только на той SIM, где это разрешено.

У NetworkManager есть привязка профиля к SIM (gsm.sim-operator-id), но при
старте модема оператор SIM ещё не известен, и проверка проходит: NM поднял
профиль «Tele2» на SIM хозяина (второй оператор). Поэтому у сотовых профилей
автоподключение выключено, а решает эта служба: когда ModemManager уже
назвал оператора SIM и модем в сети, поднимается профиль, чей
gsm.sim-operator-id совпал, — если его имя есть в /etc/bf7/mobil-avto
(по строке). Выключатель мобильных данных в шторке (радио WWAN) уважается:
при выключенном NM сам не даст подключиться.

Проверка раз в 30 с — только пока нет активного сотового соединения; во сне
таймер телефон не будит.
"""
import subprocess
import sys
import time

RAZRESHENO = "/etc/bf7/mobil-avto"
PAUZA = 30


def log(s):
    print("bf7-mobil-avto: " + s, file=sys.stderr, flush=True)


def vyvod(*a):
    r = subprocess.run(list(a), capture_output=True, text=True, timeout=60)
    return r.stdout if r.returncode == 0 else ""


def razresheny():
    try:
        return [s.strip() for s in open(RAZRESHENO, encoding="utf-8") if s.strip()]
    except OSError:
        return []


def operator_sim():
    for s in vyvod("mmcli", "-i", "any", "-K").splitlines():
        if s.startswith("sim.properties.operator-code"):
            return s.split(":", 1)[1].strip()
    return ""


def modem_v_seti():
    for s in vyvod("mmcli", "-m", "any", "-K").splitlines():
        if s.startswith("modem.generic.state "):
            return s.split(":", 1)[1].strip() in ("registered", "connected")
    return False


def sotovoe_aktivno():
    for s in vyvod("nmcli", "-t", "-f", "TYPE", "c", "show", "--active").splitlines():
        if s.strip() == "gsm":
            return True
    return False


def main():
    podnyat_zrya = {}
    log("жду модем в сети")
    while True:
        try:
            if not sotovoe_aktivno() and modem_v_seti():
                op = operator_sim()
                for imya in razresheny():
                    prof_op = vyvod("nmcli", "-g", "gsm.sim-operator-id", "c", "show", imya).strip()
                    if not op or prof_op != op:
                        continue
                    if time.monotonic() - podnyat_zrya.get(imya, -1e9) < 300:
                        continue
                    log("SIM %s — поднимаю «%s»" % (op, imya))
                    r = subprocess.run(["nmcli", "c", "up", imya], capture_output=True,
                                       text=True, timeout=120)
                    if r.returncode:
                        podnyat_zrya[imya] = time.monotonic()
                        log("не поднялся: %s" % (r.stdout + r.stderr).strip()[-200:])
                    break
        except (subprocess.SubprocessError, OSError) as e:
            log("ошибка: %s" % e)
        time.sleep(PAUZA)


if __name__ == "__main__":
    sys.exit(main())
