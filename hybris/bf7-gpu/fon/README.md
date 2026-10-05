# Обои телефона

`adwaita-l-720x1612.jpg`, `adwaita-d-720x1612.jpg` — стандартные обои GNOME
Adwaita (пакет `gnome-backgrounds`, `/usr/share/backgrounds/gnome/adwaita-*.webp`),
вырезанные по центру под портретный экран 720×1612 и пересохранённые в JPEG.

Зачем. Исходники — WebP 4096×4096, а загрузчика WebP для gdk-pixbuf на
телефоне нет: phosh писал «Failed to load background: Unrecognized image file
format» и показывал голый синий цвет. Ставить загрузчик и раскодировать
4096×4096 (≈64 МБ) на каждом старте ради экрана 720×1612 незачем.

Лицензия исходных изображений: Creative Commons Attribution-ShareAlike 3.0
(см. `/usr/share/doc/gnome-backgrounds/copyright`); эти производные — под той
же лицензией.
