# Переводы, которых нет в пакетах Debian

`mobile-settings-ru.po` — русский перевод «Мобильных настроек»
(phosh-mobile-settings). В пакете 0.24.1 перевода нет вовсе, приложение было
целиком по-английски; взят из исходников проекта (ветка main,
https://gitlab.gnome.org/World/Phosh/phosh-mobile-settings/-/raw/main/po/ru.po,
GPL-3.0-or-later, как и само приложение). Строки, которых в 0.24.1 нет,
просто не используются. Домен gettext у приложения — `mobile-settings`
(проверено strace), поэтому файл ставится как
`/usr/share/locale/ru/LC_MESSAGES/mobile-settings.mo`.

Пересобрать: `msgfmt -o mobile-settings.mo mobile-settings-ru.po`.
Пакет этот путь не занимает, так что dpkg-divert не нужен.

`portfolio-ru.po` — русский перевод файлового менеджера Portfolio
(portfolio-filemanager 0.9.14): у проекта русского нет вовсе, переведено
нами (61 строка, список взят из po/de.po исходников). Домен `portfolio`,
ставится как `/usr/share/locale/ru/LC_MESSAGES/portfolio.mo`.
Пересобрать: `msgfmt -o portfolio.mo portfolio-ru.po`.
