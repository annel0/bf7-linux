#!/usr/bin/python3
"""BF7: сейсмограмма из журналов bf7-seismograf — как барабан сейсмографа.

Строка на каждый час, в строке — пик тряски над фоном по секундам. Пропуски
(телефон спал, был без зарядки или служба стояла) — серой штриховкой:
сейсмограмма пишется только на зарядке, на батарее датчик бережёт заряд. Внизу — самые сильные
события со временем. Картинка под вертикальный экран телефона.

  bf7-seismograf-grafik [--s ЧЧ] [--chasov N] [--den ГГГГ-ММ-ДД] [--otkryt]

По умолчанию — последняя ночь: с 21:00 до 09:00 (если сейчас раньше 21:00;
иначе — последние 12 часов). Сохраняет в «Изображения/Сейсмограф».
"""
import argparse
import datetime as dt
import glob
import os
import subprocess
import sys

from PIL import Image, ImageDraw, ImageFont

KATALOG = "/var/lib/bf7-seismograf"
SHIR, POLE_L, POLE_P = 720, 70, 14
STROKA = 96          # высота строки часа
SHAPKA, PODVAL = 120, 300
SHRIFT = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
SHRIFT_Z = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"

FON = (18, 18, 22)
SETKA = (48, 48, 56)
TEKST = (220, 220, 225)
SERYJ = (120, 120, 130)
MESYATS = ["января", "февраля", "марта", "апреля", "мая", "июня", "июля",
           "августа", "сентября", "октября", "ноября", "декабря"]


def shrift(r, zhirnyj=False):
    try:
        return ImageFont.truetype(SHRIFT_Z if zhirnyj else SHRIFT, r)
    except OSError:
        return ImageFont.load_default()


def chitat(t0, t1):
    """{секунда: (скз, пик)} за [t0, t1)."""
    dannye = {}
    for f in sorted(glob.glob(os.path.join(KATALOG, "*.csv"))):
        try:
            den = dt.date.fromisoformat(os.path.basename(f)[:10])
        except ValueError:
            continue
        nachalo = dt.datetime.combine(den, dt.time()).timestamp()
        if nachalo > t1 or nachalo + 86400 < t0:
            continue
        with open(f) as fh:
            for s in fh:
                p = s.strip().split(",")
                if len(p) < 3:
                    continue
                try:
                    t, skz, pik = int(p[0]), float(p[1]), float(p[2])
                except ValueError:
                    continue
                if t0 <= t < t1:
                    dannye[t] = (skz, pik)
    return dannye


def cvet(v):
    """Цвет по силе над фоном (мg): зелёный → жёлтый → красный."""
    x = max(0.0, min(1.0, v / 150.0))
    if x < 0.5:
        return (int(80 + 350 * x), 200, 90)
    return (255, int(200 - 300 * (x - 0.5)), 60)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--s", type=int, help="с какого часа")
    ap.add_argument("--chasov", type=int, default=12)
    ap.add_argument("--den", help="день начала ГГГГ-ММ-ДД")
    ap.add_argument("--otkryt", action="store_true", help="открыть картинку")
    ap.add_argument("--vyhod", help="куда сохранить")
    a = ap.parse_args()

    seychas = dt.datetime.now()
    if a.s is not None or a.den:
        den = dt.date.fromisoformat(a.den) if a.den else seychas.date()
        nachalo = dt.datetime.combine(den, dt.time(a.s if a.s is not None else 21))
    elif seychas.hour < 21:
        nachalo = dt.datetime.combine(seychas.date() - dt.timedelta(days=1), dt.time(21))
        if seychas - nachalo > dt.timedelta(hours=a.chasov + 3):
            nachalo = seychas.replace(minute=0, second=0, microsecond=0) - dt.timedelta(hours=a.chasov - 1)
    else:
        nachalo = seychas.replace(minute=0, second=0, microsecond=0) - dt.timedelta(hours=a.chasov - 1)
    t0 = int(nachalo.timestamp())
    t1 = t0 + 3600 * a.chasov
    d = chitat(t0, t1)

    # фон — медиана пиков: у нашего 8-битного датчика покой даёт 16–32 мg
    piki = sorted(p for _, p in d.values())
    fon = piki[len(piki) // 2] if piki else 0.0

    vys = SHAPKA + STROKA * a.chasov + PODVAL
    img = Image.new("RGB", (SHIR, vys), FON)
    g = ImageDraw.Draw(img)
    f_z, f_m, f_s = shrift(34, True), shrift(22), shrift(18)

    konec = dt.datetime.fromtimestamp(t1)
    if nachalo.date() == konec.date() or konec.hour == 0 and konec.minute == 0:
        dni = f"{nachalo.day} {MESYATS[nachalo.month - 1]}"
    else:
        dni = f"{nachalo.day}→{konec.day} {MESYATS[konec.month - 1]}"
    g.text((POLE_L - 50, 22), "Сейсмограф", font=f_z, fill=TEKST)
    g.text((POLE_L - 50, 70), f"{dni}, {nachalo:%H:%M}–{konec:%H:%M}   фон {fon:.0f} мg",
           font=f_m, fill=SERYJ)

    shir_polya = SHIR - POLE_L - POLE_P
    for ch in range(a.chasov):
        y0 = SHAPKA + ch * STROKA
        sred = y0 + STROKA // 2
        g.line((POLE_L, y0, SHIR - POLE_P, y0), fill=SETKA)
        g.text((8, sred - 12), f"{(nachalo + dt.timedelta(hours=ch)):%H}:00", font=f_s, fill=SERYJ)
        for m in (15, 30, 45):
            x = POLE_L + shir_polya * m // 60
            g.line((x, y0 + 4, x, y0 + 10), fill=SETKA)
        tch = t0 + ch * 3600
        for px in range(shir_polya):
            a_s = tch + px * 3600 // shir_polya
            b_s = tch + (px + 1) * 3600 // shir_polya
            vals = [d[s][1] for s in range(a_s, b_s) if s in d]
            x = POLE_L + px
            if a_s >= int(seychas.timestamp()):
                continue
            if not vals:
                if px % 4 == 0:	# пропуск: сон, батарея или служба стояла
                    g.line((x, sred - 6, x, sred + 6), fill=(60, 60, 68))
                continue
            v = max(0.0, max(vals) - fon)
            h = int((STROKA / 2 - 6) * min(1.0, (v / 300.0) ** 0.5))
            g.line((x, sred - h, x, sred + h), fill=cvet(v))
            if h == 0:
                g.point((x, sred), fill=(70, 140, 80))

    # Толчки сильнее RUKI мg — телефон брали в руки (26.09: автоповорот дал
    # 1600 мg, и весь список заняли они). Их — одной строкой по отрезкам,
    # а в список — только то, что слабее: шаги, стук, хлопки.
    RUKI = 400
    ruki = sorted(t for t, (_, p) in d.items() if p - fon >= RUKI)
    otrezki = []
    for t in ruki:
        if otrezki and t - otrezki[-1][1] <= 120:
            otrezki[-1][1] = t
        else:
            otrezki.append([t, t])
    y = SHAPKA + STROKA * a.chasov + 24
    g.text((POLE_L - 50, y), "Самые сильные толчки:", font=f_m, fill=TEKST)
    y += 38
    sobytiya = []
    for t, (_, p) in sorted(d.items(), key=lambda kv: -kv[1][1]):
        # и всё, что рядом с «в руках»: там не каждый удар дотягивает до RUKI
        if p - fon >= RUKI or any(a0 - 60 <= t <= a1 + 60 for a0, a1 in otrezki):
            continue
        if p - fon < 30:
            break
        if all(abs(t - s) > 10 for s, _ in sobytiya):
            sobytiya.append((t, p))
        if len(sobytiya) == 5:
            break
    if not sobytiya:
        g.text((POLE_L - 50, y), "тихо — ничего заметного над фоном", font=f_s, fill=SERYJ)
    for t, p in sorted(sobytiya):
        g.text((POLE_L - 50, y), f"{dt.datetime.fromtimestamp(t):%H:%M:%S}   {p - fon:.0f} мg над фоном",
               font=f_s, fill=cvet(p - fon))
        y += 30
    if otrezki:
        spisok = ", ".join(
            f"{dt.datetime.fromtimestamp(a0):%H:%M}" + ("" if a1 - a0 < 60 else f"–{dt.datetime.fromtimestamp(a1):%H:%M}")
            for a0, a1 in otrezki[:4])
        g.text((POLE_L - 50, y + 6), f"телефон брали в руки: {spisok}", font=f_s, fill=SERYJ)
    zapisano = len(d)
    g.text((POLE_L - 50, vys - 40),
           f"записано {zapisano // 3600} ч {zapisano % 3600 // 60} мин из {a.chasov} ч; серое — не писал (сон или без зарядки)",
           font=f_s, fill=SERYJ)

    if a.vyhod:
        put = a.vyhod
    else:
        try:
            kart = subprocess.run(["xdg-user-dir", "PICTURES"], capture_output=True,
                                  text=True).stdout.strip() or os.path.expanduser("~/Изображения")
        except OSError:
            kart = os.path.expanduser("~/Изображения")
        os.makedirs(os.path.join(kart, "Сейсмограф"), exist_ok=True)
        put = os.path.join(kart, "Сейсмограф", f"сейсмограф-{nachalo:%Y-%m-%d_%H%M}.png")
    img.save(put)
    print(put)
    if a.otkryt:
        subprocess.Popen(["xdg-open", put], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return 0


if __name__ == "__main__":
    sys.exit(main())
