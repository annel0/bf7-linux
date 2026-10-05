#!/usr/bin/env python3
"""BF7: приготовить модем MediaTek к ModemManager.

Модем после запуска стоит в CFUN=0, а в нём MediaTek SIM не опрашивает:
AT+CPIN? отвечает «+CME ERROR: 14» (SIM занята). ModemManager на этом
сдаётся: «Couldn't get SIM lock status after 6 retries» и модем в состоянии
failed/sim-missing. Здесь: дождаться, пока модем готов (md1:4 в
/sys/kernel/ccci/boot), и перевести его в CFUN=4 — SIM включена, передатчик
выключен. Радио включит сам ModemManager (CFUN=1), когда его попросят.

Говорим через /dev/ttyp0 — тот же терминал, что потом возьмёт MM (мост
bf7-at-most к /dev/ttyC0). Все ожидания ограничены.

Заодно — хранилища SMS в стандартное «ME» (см. ниже).
"""
import os
import re
import select
import sys
import termios
import time

BOOT = "/sys/kernel/ccci/boot"
PORT = "/dev/ttyp0"


def zhdat_modem(sekund):
    konec = time.monotonic() + sekund
    while time.monotonic() < konec:
        try:
            if open(BOOT).read().split()[0] == "md1:4":
                return True
        except OSError:
            pass
        time.sleep(1)
    return False


# APN для входа в LTE по оператору SIM (MCC+MNC): (apn, логин, пароль).
# Второй оператор — с сайта оператора: internet.example, user/internet, PAP или CHAP.
IA_APN = {
    "250xx": ("internet.example", "user", "internet"),
    "25020": ("internet.tele2.ru", "", ""),
}


def sprosit(fd, cmd, sekund=10):
    os.write(fd, (cmd + "\r").encode())
    buf = b""
    konec = time.monotonic() + sekund
    while time.monotonic() < konec:
        r, _, _ = select.select([fd], [], [], 0.3)
        if r:
            buf += os.read(fd, 4096)
            t = buf.decode(errors="replace")
            if "\r\nOK\r\n" in t or "ERROR" in t:
                break
    return buf.decode(errors="replace")


def main():
    if not zhdat_modem(150):
        print("bf7-modem-sim: модем не поднялся за 150 с", file=sys.stderr)
        return 1
    fd = os.open(PORT, os.O_RDWR | os.O_NOCTTY)
    a = termios.tcgetattr(fd)
    a[0] = a[1] = a[3] = 0
    termios.tcsetattr(fd, termios.TCSANOW, a)
    termios.tcflush(fd, termios.TCIOFLUSH)
    for _ in range(5):
        if "OK" in sprosit(fd, "AT", 3):
            break
        time.sleep(1)
    fun = sprosit(fd, "AT+CFUN?")
    if "+CFUN: 0" in fun:
        sprosit(fd, "AT+CFUN=4", 20)
    konec = time.monotonic() + 30
    sim = ""
    while time.monotonic() < konec:
        sim = sprosit(fd, "AT+CPIN?", 5)
        if "+CPIN:" in sim:
            break
        time.sleep(2)
    # Хранилища SMS. У MediaTek по умолчанию входящие ложатся в своё «SM_P»
    # (+CPMS: "ME",…,"SM_P",…,"SM_P"), которого ModemManager не знает: он
    # пишет «Cannot set default storage when current mem1 storage is
    # unknown», и уведомление +CMTI: "SM_P",n о новом SMS может не
    # разобрать. Ставим стандартное «ME» во все три.
    if "READY" in sim:
        sprosit(fd, 'AT+CPMS="ME","ME","ME"')
    # Разрешение передачи данных. Заводской RIL однослотового телефона шлёт
    # его при каждой загрузке («Single SIM project, send EDALLOW=1 at boot
    # time» в libmtk-ril.so); без него AT+CGACT отвечает +CME ERROR: 5745.
    # Только так, без «?»: запрос AT+EDALLOW? вешает командный канал модема.
    sprosit(fd, "AT+EDALLOW=1", 10)
    # Одобрение входящих, как у заводского RIL (AT+EAIC=2 в libmtk-ril.so):
    # модем присылает «+EAIC: <id>,…,<seq>» и ждёт AT+EAIC=0,<id>,<seq> —
    # его шлёт наш ModemManager. В режиме 3 (по умолчанию) вызов звонит, но
    # не отвечается: ATA → +CME ERROR: 100, +CLCC пуст.
    sprosit(fd, "AT+EAIC=2", 10)
    lte_vhod = False
    # APN для входа в LTE (initial attach). В LTE сеть требует APN уже при
    # регистрации; заводской RIL шлёт его командой AT+EIAAPN (строки
    # libmtk-ril.so: apn, rat, протокол, протокол в роуминге, auth, логин,
    # пароль). Без неё модем видел соту LTE второго оператора, но оставался на EDGE
    # (~125 кбит/с); с ней — LTE, ~18 Мбит/с (2026-09-24). APN выбираем по
    # оператору SIM (первые 5 цифр IMSI; сам IMSI никуда не пишем).
    if "READY" in sim:
        imsi = "".join(c for c in sprosit(fd, "AT+CIMI", 5) if c.isdigit())
        ia = IA_APN.get(imsi[:5])
        lte_vhod = bool(ia)
        if ia:
            apn, login, parol = ia
            otv = sprosit(fd, 'AT+EIAAPN="%s",0,"IPV4V6","IPV4V6",%d,"%s","%s"'
                          % (apn, 3 if login else 0, login, parol), 10)
            print("bf7-modem-sim: APN для LTE — %s: %s" % (apn, "OK" if "OK" in otv else otv.strip()),
                  file=sys.stderr)
            # Контекст по умолчанию с тем же APN. В работающей системе его
            # заводит ModemManager при включении интернета, и тогда вход в
            # LTE удаётся за минуту; при загрузке контексты пусты — и за 120 с
            # модем в LTE не регистрировался (2026-09-24).
            sprosit(fd, 'AT+CGDCONT=1,"IPV4V6","%s"' % apn, 10)
        else:
            print("bf7-modem-sim: APN для LTE не задан — оператор SIM неизвестен", file=sys.stderr)
    # Сеть: вход в LTE, потом 2G+3G+4G с предпочтением LTE. С одним
    # предпочтением (AT+ERAT=6,4) модем, севший на GSM, в LTE сам не уходит —
    # сеть второго оператора его туда не зовёт. Рабочий порядок (2026-09-24, трижды):
    # «только LTE» (AT+ERAT=3) — модем входит в LTE с APN из EIAAPN, — затем
    # AT+ERAT=6,4, и он остаётся в LTE, а звонки идут через CSFB в 2G.
    # Признак LTE — первое поле «+ERAT: <текущая сеть>,…» (7 — LTE): ответ
    # +COPS в порту перемешан с непрошеными строками и врёт. Больше 120 с не
    # ждём: всё это время без 2G телефон вне связи. 6,4 — в любом случае.
    v_lte = None
    if lte_vhod:
        sprosit(fd, "AT+ERAT=3", 10)
        sprosit(fd, "AT+CFUN=1", 20)
        v_lte = False
        # «+ERAT: 7» значит только «нашёл соту LTE»: вернув 6,4 в этот
        # момент, мы обрывали вход (при загрузке MM потом 46 с видел модем
        # незарегистрированным, и тот садился на GSM). Ждём ещё и +CEREG с
        # состоянием 1/5 — зарегистрирован в LTE.
        konec = time.monotonic() + 120
        while time.monotonic() < konec:
            time.sleep(5)
            m = re.search(r"\+ERAT:\s*(\d+),", sprosit(fd, "AT+ERAT?", 5))
            if not (m and m.group(1) == "7"):
                continue
            c = re.search(r"\+CEREG:\s*\d+,\s*(\d+)", sprosit(fd, "AT+CEREG?", 5))
            if c and c.group(1) in ("1", "5"):
                v_lte = True
                break
    erat = sprosit(fd, "AT+ERAT=6,4", 10)
    if "OK" not in erat:
        print("bf7-modem-sim: AT+ERAT=6,4 — %s" % erat.strip().replace("\r\n", " "),
              file=sys.stderr)
    if v_lte is not None:
        print("bf7-modem-sim: LTE — %s" % ("вошёл" if v_lte else "не нашёлся за 120 с, остаюсь в 2G"),
              file=sys.stderr)
    os.close(fd)
    stroka = next((s for s in sim.splitlines() if s.startswith("+CPIN")), "нет ответа")
    print("bf7-modem-sim: SIM — %s" % stroka)
    return 0


if __name__ == "__main__":
    sys.exit(main())
