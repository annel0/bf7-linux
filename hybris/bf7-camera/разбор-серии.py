#!/usr/bin/env python3
"""Разобрать серию сырых кадров с телефона: сложить по 1, 2, 4, 6, 8, 16 и сравнить.

Смысл в том, что все варианты берутся из одной и той же серии. Когда снимаешь
отдельно «четыре кадра» и отдельно «шестнадцать», между съёмками успевает
измениться и сцена, и настройки автоматики, и сравнивать оказывается нечего.

    разбор-серии.py <каталог с серией> [куда положить картинки]
"""
import os
import sys

import numpy as np


def опись(d):
    п = {}
    for s in open(os.path.join(d, "опись.txt"), encoding="utf-8"):
        ч = s.split()
        if len(ч) >= 2:
            п[ч[0]] = ч[1:]
    return п


def кадры(d):
    return sorted(f for f in os.listdir(d) if f.endswith(".raw"))


def сырое(path, w, h, black):
    a = np.fromfile(path, dtype="<u2").astype(np.float32)
    return np.clip((a[: w * h].reshape(h, w) / 16.0) - black, 0, None)


def мозаика(r):
    """Билинейный разбор GRBG: чётная строка G R, нечётная B G."""
    h, w = r.shape
    p = np.pad(r, 1, mode="edge")
    гор = (p[1:-1, :-2] + p[1:-1, 2:]) / 2          # слева+справа
    верт = (p[:-2, 1:-1] + p[2:, 1:-1]) / 2         # сверху+снизу
    крест = (p[1:-1, :-2] + p[1:-1, 2:] + p[:-2, 1:-1] + p[2:, 1:-1]) / 4
    диаг = (p[:-2, :-2] + p[:-2, 2:] + p[2:, :-2] + p[2:, 2:]) / 4

    y, x = np.indices((h, w))
    чёт_стр, чёт_ст = (y % 2 == 0), (x % 2 == 0)
    G_в_красной = чёт_стр & чёт_ст
    R_точка = чёт_стр & ~чёт_ст
    B_точка = ~чёт_стр & чёт_ст
    G_в_синей = ~чёт_стр & ~чёт_ст

    R = np.where(R_точка, r, np.where(G_в_красной, гор,
                 np.where(B_точка, диаг, верт)))
    G = np.where(G_в_красной | G_в_синей, r, крест)
    B = np.where(B_точка, r, np.where(G_в_синей, гор,
                 np.where(R_точка, диаг, верт)))
    return R, G, B


def цвет(R, G, B, wb, ccm):
    """Та же цепочка, что в телефоне: баланс, обесцвечивание светов, матрица, гамма."""
    rr, gg, bb = np.clip(R * wb[0], 0, 959), np.clip(G * wb[1], 0, 959), np.clip(B * wb[2], 0, 959)
    сырмакс = np.maximum(np.maximum(R, G), B)
    t = np.clip((сырмакс - 880) / 79.0, 0, 1)
    мх = np.maximum(np.maximum(rr, gg), bb)
    rr += (мх - rr) * t
    gg += (мх - gg) * t
    bb += (мх - bb) * t
    m = np.array(ccm, dtype=np.float32).reshape(3, 3) / 256.0
    cr = np.clip(m[0, 0] * rr + m[0, 1] * gg + m[0, 2] * bb, 0, 959)
    cg = np.clip(m[1, 0] * rr + m[1, 1] * gg + m[1, 2] * bb, 0, 959)
    cb = np.clip(m[2, 0] * rr + m[2, 1] * gg + m[2, 2] * bb, 0, 959)
    г = lambda v: (255.0 * (v / 959.0) ** (1 / 2.2) + 0.5).astype(np.uint8)
    return np.dstack([г(cr), г(cg), г(cb)])


def зерно(rgb):
    """Разница точки со средним по окрестности 5x5 — то же, чем мерили на телефоне."""
    я = 0.30 * rgb[:, :, 0] + 0.59 * rgb[:, :, 1] + 0.11 * rgb[:, :, 2]
    ядро = np.ones((5, 5), dtype=np.float32) / 25.0
    от = np.zeros_like(я)
    for j in range(-2, 3):
        for i in range(-2, 3):
            от += np.roll(np.roll(я, j, 0), i, 1) / 25.0
    e = (я - от)[3:-3, 3:-3]
    м = от[3:-3, 3:-3]
    тени = м < 70
    return float(np.sqrt((e ** 2).mean())), float(np.sqrt((e[тени] ** 2).mean())), int(тени.sum())


def main():
    d = sys.argv[1]
    выход = sys.argv[2] if len(sys.argv) > 2 else d
    п = опись(d)
    w, h = int(п["ширина"][0]), int(п["высота"][0])
    black = float(п["чёрное"][0])
    wb = [float(x) for x in п["баланс"]]
    ccm = [int(x) for x in п["матрица"]]
    фл = кадры(d)
    print("серия: %d кадров %dx%d, выдержка %s, усиление %s"
          % (len(фл), w, h, п["выдержка"][0], п["усиление"][0]))

    сум = None
    результаты = []
    точки = [n for n in (1, 2, 4, 6, 8, 12, 16) if n <= len(фл)]
    from PIL import Image
    for i, f in enumerate(фл, 1):
        r = сырое(os.path.join(d, f), w, h, black)
        сум = r if сум is None else сум + r
        if i in точки:
            R, G, B = мозаика(сум / i)
            rgb = цвет(R, G, B, wb, ccm)
            вс, тн, nt = зерно(rgb)
            результаты.append((i, вс, тн))
            имя = os.path.join(выход, "сложено-%02d.jpg" % i)
            Image.fromarray(rgb).save(имя, quality=92)
            print("  %2d кадров: зерно по кадру %.3f, в тенях %.3f  -> %s"
                  % (i, вс, тн, os.path.basename(имя)))

    if len(результаты) >= 2:
        n1, _, т1 = результаты[0]
        nN, _, тN = результаты[-1]
        R = (т1 ** 2 - тN ** 2) / (1 - n1 / nN) if nN > n1 else 0
        F = т1 ** 2 - R
        print()
        print("разложение шума: постоянная часть %.3f, случайная (на кадре) %.3f"
              % (np.sqrt(max(F, 0)), np.sqrt(max(R, 0))))
        print("выигрыш от %d к %d кадрам: %.2f раза" % (n1, nN, т1 / тN))


if __name__ == "__main__":
    main()
