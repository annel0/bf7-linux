#!/bin/sh
# BF7: «Фото» — eog сразу с папкой «Изображения» (см. bf7-foto.desktop).
exec eog "$(xdg-user-dir PICTURES)"
