#!/usr/bin/env python3
"""BF7: обрезка снимка камеры строго по экрану телефона.

Камера смотрит на панель сбоку и сверху, поэтому экран на снимке — не
прямоугольник, а четырёхугольник: перспектива. Простой обрезкой по рамке от
неё не избавиться, нужно разогнуть.

Делается в два приёма.

Настройка (один раз, пока телефон лежит неподвижно). На экран выводится
белое поле, снимок ищет на кадре самое большое светлое пятно и берёт его
четыре угла. Углы кладутся в файл рядом.

Обычный снимок. Кадр разгибается по сохранённым углам в честные 720×1612 —
ровно столько пикселей у панели.

Где верх — решаем сверкой с эталоном. По белому полю этого не понять: оно
одинаковое с обеих сторон, а вырез под камеру слишком мелок, чтобы судить по
нему надёжно (пробовал — ошибался ровно на пол-оборота). Поэтому вторым
приёмом снимаем обычный рабочий стол и камерой, и через grim, и сравниваем
их в двух положениях: какое совпало лучше, то и верное. grim — единственная
имеющаяся правда о том, что на экране, и она тут как раз к месту.

Углы приходится снимать заново каждый раз, когда телефон подвинули.
"""

import json
import sys
from collections import deque

import numpy as np
from PIL import Image

UGLY_PO_UMOLCHANIYU = "bf7-glaz-ugly.json"
SHIRINA_PANELI = 720
VYSOTA_PANELI = 1612


def najti_pyatno(yark):
    """Самое большое светлое связное пятно на кадре.

    Порог берём посередине между тёмным фоном и светлым экраном. Просто
    «ярче ста» не годится: в кадр попадают лампы и блики, а у них яркость
    та же. Спасает именно связность — экран это одно большое пятно.
    """
    porog = (float(yark.max()) + float(yark.min())) / 2.0
    maska = yark > porog

    v, sh = maska.shape
    posesheno = np.zeros_like(maska)
    luchshee = None
    luchshij_razmer = 0

    # Идём не по каждому пикселю, а по сетке через восемь: экран заведомо
    # крупнее, а перебор всех трёхсот тысяч точек в Python небыстр.
    for y0 in range(0, v, 8):
        for x0 in range(0, sh, 8):
            if not maska[y0, x0] or posesheno[y0, x0]:
                continue

            ochered = deque([(y0, x0)])
            posesheno[y0, x0] = True
            tochki = []

            while ochered:
                y, x = ochered.popleft()
                tochki.append((y, x))
                for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    ny, nx = y + dy, x + dx
                    if 0 <= ny < v and 0 <= nx < sh:
                        if maska[ny, nx] and not posesheno[ny, nx]:
                            posesheno[ny, nx] = True
                            ochered.append((ny, nx))

            if len(tochki) > luchshij_razmer:
                luchshij_razmer = len(tochki)
                luchshee = tochki

    if luchshee is None:
        raise SystemExit("bf7-glaz: светлого пятна на кадре нет — "
                         "экран погашен или камера смотрит мимо")

    return np.array(luchshee), luchshij_razmer


def najti_ugly(tochki):
    """Четыре угла пятна: крайние точки по суммам и разностям координат.

    Для повёрнутого четырёхугольника это точный приём: у левого верхнего
    угла наименьшая сумма x+y, у правого нижнего — наибольшая, у правого
    верхнего наибольшая разность x−y, у левого нижнего — наименьшая.
    """
    y = tochki[:, 0].astype(float)
    x = tochki[:, 1].astype(float)

    summa = x + y
    raznost = x - y

    def tochka(i):
        return (float(x[i]), float(y[i]))

    return [
        tochka(int(np.argmin(summa))),    # левый верхний
        tochka(int(np.argmax(raznost))),  # правый верхний
        tochka(int(np.argmax(summa))),    # правый нижний
        tochka(int(np.argmin(raznost))),  # левый нижний
    ]


def koefficienty(vyhod, vhod):
    """Восемь чисел перспективы для PIL.

    PIL просит преобразование «из выхода в вход»: для каждой точки
    готового снимка он спрашивает, откуда её взять на исходном кадре.
    """
    stroki = []
    for (xv, yv), (xi, yi) in zip(vyhod, vhod):
        stroki.append([xv, yv, 1, 0, 0, 0, -xi * xv, -xi * yv])
        stroki.append([0, 0, 0, xv, yv, 1, -yi * xv, -yi * yv])

    a = np.array(stroki, dtype=float)
    b = np.array(vhod, dtype=float).reshape(8)
    return np.linalg.solve(a, b)


def razognut(kadr, ugly, shirina, vysota):
    vyhod = [(0, 0), (shirina, 0), (shirina, vysota), (0, vysota)]
    koef = koefficienty(vyhod, ugly)
    return kadr.transform((shirina, vysota), Image.PERSPECTIVE,
                          tuple(koef), Image.BICUBIC)


def shodstvo(a, b):
    """Насколько два изображения похожи: обычная корреляция яркостей.

    Мелкие различия неважны — камера даёт размытую и шумную картинку.
    Важно лишь отличить совпадение от поворота на пол-оборота, а это
    корреляция различает уверенно.
    """
    x = np.asarray(a.convert("L").resize((90, 202)), dtype=float).ravel()
    y = np.asarray(b.convert("L").resize((90, 202)), dtype=float).ravel()

    x = x - x.mean()
    y = y - y.mean()
    znam = (np.sqrt((x * x).sum()) * np.sqrt((y * y).sum()))
    if znam == 0:
        return 0.0
    return float((x * y).sum() / znam)


def povernut_ugly(ugly):
    """Тот же четырёхугольник, но начиная с противоположного угла."""
    return [ugly[2], ugly[3], ugly[0], ugly[1]]


def sdvinut_ugly(ugly):
    """Тот же четырёхугольник, начиная со следующего угла — поворот на четверть."""
    return [ugly[1], ugly[2], ugly[3], ugly[0]]


def dlina(a, b):
    return ((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2) ** 0.5


def polozhit_stoymya(ugly):
    """Развернуть четырёхугольник так, чтобы длинная сторона стала высотой.

    Телефон обычно лежит боком, и найденные углы описывают горизонтальный
    четырёхугольник. Если отдать его как есть, панель 720×1612 будет
    растянута поперёк: узкая сторона уйдёт в высоту. Сравниваем, какое из
    двух положений ближе к настоящему отношению сторон панели.
    """
    cel = float(SHIRINA_PANELI) / float(VYSOTA_PANELI)

    verh = dlina(ugly[0], ugly[1])
    bok = dlina(ugly[1], ugly[2])
    if verh <= 0 or bok <= 0:
        return ugly

    kak_est = verh / bok
    posle_povorota = bok / verh

    if abs(kak_est - cel) <= abs(posle_povorota - cel):
        return ugly
    return sdvinut_ugly(ugly)


def nastroit(put_kadra, put_uglov):
    kadr = Image.open(put_kadra).convert("RGB")
    yark = np.asarray(kadr.convert("L"), dtype=float)

    tochki, razmer = najti_pyatno(yark)
    dolya = razmer / float(yark.size)
    if dolya < 0.03:
        raise SystemExit(
            "bf7-glaz: светлое пятно заняло всего %.1f%% кадра — это не "
            "экран. Нужно белое поле: bf7-zalivka ffffff 20" % (dolya * 100))

    ugly = polozhit_stoymya(najti_ugly(tochki))

    with open(put_uglov, "w") as f:
        json.dump({
            "ugly": ugly,
            "kadr": list(kadr.size),
            "dolya_ekrana": round(dolya, 4),
            "perevernut": False,
        }, f, ensure_ascii=False, indent=1)

    print("bf7-glaz: углы найдены, экран занял %.1f%% кадра" % (dolya * 100))
    for imya, (x, y) in zip(("лев-верх", "прав-верх", "прав-низ", "лев-низ"),
                            ugly):
        print("  %-9s %6.1f %6.1f" % (imya, x, y))
    print("  записано в %s" % put_uglov)


def opredelit_verh(put_kadra, put_ehtalona, put_uglov):
    """Сверить камеру с эталоном от grim и запомнить, нужен ли переворот."""
    with open(put_uglov) as f:
        dannye = json.load(f)

    kadr = Image.open(put_kadra).convert("RGB")
    ehtalon = Image.open(put_ehtalona).convert("RGB")
    ugly = [tuple(t) for t in dannye["ugly"]]

    pryamo = razognut(kadr, ugly, SHIRINA_PANELI // 4, VYSOTA_PANELI // 4)
    vverh_nogami = pryamo.rotate(180)

    a = shodstvo(pryamo, ehtalon)
    b = shodstvo(vverh_nogami, ehtalon)

    dannye["perevernut"] = bool(b > a)
    with open(put_uglov, "w") as f:
        json.dump(dannye, f, ensure_ascii=False, indent=1)

    print("bf7-glaz: сходство как есть %.3f, вверх ногами %.3f — %s" %
          (a, b, "переворачиваю" if dannye["perevernut"] else "оставляю"))

    # Порог тот же, что у сверки: на тёмном экране даже полное согласие даёт
    # около 0.23, и требовать больше — значит ругаться на исправную систему.
    if max(a, b) < 0.15:
        print("  ВНИМАНИЕ: совпадение слабое (%.3f). Возможно, экран сменился "
              "между снимками или сбита резкость." % max(a, b))


def obrezat(put_kadra, put_uglov, put_vyhoda, delitel):
    try:
        with open(put_uglov) as f:
            dannye = json.load(f)
    except FileNotFoundError:
        raise SystemExit("bf7-glaz: нет %s — сначала настройка "
                         "(bf7-glaz.sh --nastroyka)" % put_uglov)

    kadr = Image.open(put_kadra).convert("RGB")
    if list(kadr.size) != dannye.get("kadr"):
        raise SystemExit("bf7-glaz: кадр %dx%d, а настройка делалась на %s" %
                         (kadr.size[0], kadr.size[1], dannye.get("kadr")))

    gotovo = razognut(kadr, [tuple(t) for t in dannye["ugly"]],
                      SHIRINA_PANELI // delitel, VYSOTA_PANELI // delitel)
    if dannye.get("perevernut"):
        gotovo = gotovo.rotate(180)
    gotovo.save(put_vyhoda)
    print("bf7-glaz: экран вырезан в %s (%dx%d)" %
          (put_vyhoda, gotovo.size[0], gotovo.size[1]))


def sverit(put_kamery, put_grim):
    """Сравнить два свидетельства об одном экране и сказать, сходятся ли.

    Порог низкий нарочно. Экран телефона почти всегда тёмный, светлого на
    нём мало, и даже при полном согласии корреляция редко переваливает за
    половину. Зато при настоящем расхождении она падает к нулю или уходит
    в минус — этого достаточно.
    """
    kamera = Image.open(put_kamery).convert("RGB")
    ehtalon = Image.open(put_grim).convert("RGB")

    s = shodstvo(kamera, ehtalon)
    print("bf7-glaz: сходство камеры и grim %.3f" % s)

    if s >= 0.15:
        print("  свидетели согласны: путь показа доносит кадр до матрицы")
    else:
        print("  РАСХОЖДЕНИЕ: композитор собрал одно, панель показала другое.")
        print("  Смотреть слой показа (bf7-disp), MVA и первый кадр.")


def main():
    dovody = sys.argv[1:]
    if not dovody:
        raise SystemExit(__doc__)

    rezhim = dovody[0]
    if rezhim == "nastroit":
        nastroit(dovody[1], dovody[2])
    elif rezhim == "verh":
        opredelit_verh(dovody[1], dovody[2], dovody[3])
    elif rezhim == "sverit":
        sverit(dovody[1], dovody[2])
    elif rezhim == "obrezat":
        delitel = int(dovody[4]) if len(dovody) > 4 else 2
        obrezat(dovody[1], dovody[2], dovody[3], delitel)
    else:
        raise SystemExit("bf7-glaz: неизвестный приём %r" % rezhim)


if __name__ == "__main__":
    main()
